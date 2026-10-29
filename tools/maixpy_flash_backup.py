#!/usr/bin/env python3
"""Back up a stock MaixPy Cube through raw REPL; never program device flash.

Probe first. A backup requires config.json to identify the board as `cube`.
The image is accepted only after an independent on-device SHA-256 pass.
Entering raw REPL interrupts the current Python application.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import time


class RawRepl:
    def __init__(self, port: str, baud: int):
        import serial
        self.serial = serial.Serial(port=None, baudrate=baud, timeout=0.2,
                                    write_timeout=5)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()
        self.pending = bytearray()
        self.serial.set_buffer_size(rx_size=65536, tx_size=8192)

    def until(self, marker: bytes, timeout: float = 10, limit: int = 32768) -> bytes:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            position = self.pending.find(marker)
            if position >= 0:
                result = bytes(self.pending[:position])
                del self.pending[:position + len(marker)]
                return result
            if len(self.pending) > limit:
                raise RuntimeError("Unexpected or excessive raw REPL response")
            self.pending.extend(self.serial.read(self.serial.in_waiting or 1))
        raise TimeoutError("No complete raw REPL response")

    def enter(self):
        self.serial.write(b"\r\x03\x03")
        time.sleep(0.2)
        self.serial.reset_input_buffer()
        self.serial.write(b"\r\x01")
        self.until(b"raw REPL; CTRL-B to exit\r\n>")

    def execute(self, source: str, timeout: float = 10) -> bytes:
        self.send_source(source)
        self.until(b"OK", timeout=timeout, limit=1024)
        result = self.until(b"\x04", timeout=timeout)
        error = self.until(b"\x04", timeout=timeout)
        self.until(b">", timeout=timeout, limit=1024)
        if error.strip():
            raise RuntimeError("MaixPy error: " + error.decode("utf-8", "replace"))
        return result.strip()

    def send_source(self, source: str):
        # Stock MaixPy has a small REPL RX ring, without UART flow control.
        data = source.encode("utf-8")
        for offset in range(0, len(data), 16):
            self.serial.write(data[offset:offset + 16])
            time.sleep(0.01)
        self.serial.write(b"\x04")

    def close(self):
        try:
            self.serial.write(b"\x02")
        finally:
            self.serial.close()

    def switch_baud(self, baud: int):
        # Raw REPL sends OK before executing; change the host during the delay.
        source = ("import time, machine\ntime.sleep_ms(250)\n"
                  "machine.UART.repl_uart().init(%d, 8, None, 1)\n"
                  "time.sleep_ms(250)\nprint('BAUD_READY')" % baud)
        self.send_source(source)
        self.until(b"OK", limit=1024)
        self.serial.baudrate = baud
        result = self.until(b"\x04")
        error = self.until(b"\x04")
        self.until(b">", limit=1024)
        if error.strip() or result.strip() != b"BAUD_READY":
            raise RuntimeError("UART speed change failed")


PROBE = """import os, ujson
_backup_config = None
for _backup_path in ('/flash/config.json', '/config.json'):
    try:
        with open(_backup_path) as _backup_file:
            _backup_config = ujson.loads(_backup_file.read())
        break
    except OSError:
        pass
print(ujson.dumps({'uname': list(os.uname()), 'config': _backup_config}))
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("probe", "backup"))
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--transfer-baud", type=int, default=115200)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--resume", action="store_true", help="resume an aligned partial image; full device rehash remains mandatory")
    args = parser.parse_args()
    if args.command == "backup" and args.output is None:
        parser.error("backup requires --output")
    output = args.output
    partial = output.with_suffix(output.suffix + ".partial") if output else None
    manifest = output.with_suffix(output.suffix + ".json") if output else None
    if output and (output.exists() or manifest.exists() or (partial.exists() and not args.resume)):
        parser.error("output, partial image, or manifest already exists")
    repl = RawRepl(args.port, args.baud)
    try:
        repl.enter()
        identity = json.loads(repl.execute(PROBE))
        print(json.dumps(identity, ensure_ascii=False, indent=2), flush=True)
        if args.command == "probe":
            return
        if (identity.get("config") or {}).get("type") != "cube":
            raise RuntimeError("Refusing backup: stock config does not identify a Cube")
        if args.transfer_baud != args.baud:
            repl.switch_baud(args.transfer_baud)
        # Cube's stock 16 MiB capacity; confirm the physical model before use.
        size, chunk = 16 * 1024 * 1024, 4096
        repl.execute("""import Maix, ubinascii, uhashlib
def _backup_chunk(offset, count):
    data = Maix.utils.flash_read(offset, count)
    checksum = ubinascii.hexlify(uhashlib.sha256(data).digest()).decode()
    encoded = ('F' if data == b'\\xff' * count else
               'Z' if data == b'\\x00' * count else
               ubinascii.b2a_base64(data).decode())
    print(checksum + ':' + encoded)
""")
        digest = hashlib.sha256()
        output.parent.mkdir(parents=True, exist_ok=True)
        start = 0
        if args.resume and partial.exists():
            start = partial.stat().st_size
            if start % 1024 or start > size:
                raise RuntimeError("Partial image is not aligned or exceeds flash capacity")
            with partial.open("rb") as previous:
                while data := previous.read(65536):
                    digest.update(data)
            print("Resuming at 0x%x" % start, flush=True)
        with partial.open("ab" if args.resume else "xb") as image:
            for offset in range(start, size, chunk):
                count = min(chunk, size - offset)
                for attempt in range(3):
                    response = repl.execute("_backup_chunk(%d, %d)" % (offset, count))
                    try:
                        checksum, encoded = response.split(b":", 1)
                        data = (b"\xff" * count if encoded == b"F" else
                                b"\x00" * count if encoded == b"Z" else
                                base64.b64decode(encoded, validate=True))
                        if len(data) == count and hashlib.sha256(data).hexdigest().encode() == checksum:
                            break
                    except ValueError:
                        pass
                    if attempt == 2:
                        raise RuntimeError("Invalid flash transfer at 0x%x" % offset)
                image.write(data)
                digest.update(data)
                if (offset - start) % (256 * 1024) == 0:
                    print("Read %.1f%%" % (100 * offset / size), flush=True)
            image.flush()
            os.fsync(image.fileno())
        device_second = repl.execute(
            "_backup_hash2 = uhashlib.sha256()\n"
            "for _backup_offset in range(0, %d, %d):\n"
            "    _backup_hash2.update(Maix.utils.flash_read(_backup_offset, %d))\n"
            "print(ubinascii.hexlify(_backup_hash2.digest()).decode())" % (size, chunk, chunk),
            timeout=180,
        ).decode()
        if device_second != digest.hexdigest():
            raise RuntimeError("Independent flash SHA-256 mismatch; retaining partial image")
        # Also verify the bytes actually persisted on the host.
        with partial.open("rb") as image:
            disk_hash = hashlib.file_digest(image, "sha256").hexdigest()
        if disk_hash != digest.hexdigest():
            raise RuntimeError("Host disk SHA-256 mismatch; retaining partial image")
        partial.rename(output)
        metadata = dict(identity=identity, port=args.port, size=size,
                        sha256=disk_hash, independent_device_read_verified=True)
        with manifest.open("x", encoding="utf-8") as report:
            json.dump(metadata, report, ensure_ascii=False, indent=2)
            report.write("\n")
        print("Verified backup: %s\nSHA-256: %s" % (output, disk_hash), flush=True)
    finally:
        try:
            if repl.serial.baudrate != args.baud:
                repl.switch_baud(args.baud)
        finally:
            repl.close()


if __name__ == "__main__":
    main()
