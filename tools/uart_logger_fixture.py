"""RAM loaders and serial fixture for the Cube UART logger study."""

import argparse
import binascii
from contextlib import contextmanager
import importlib.util
from pathlib import Path
from statistics import median
import struct
import subprocess
import sys
import time

import hkflash
from sky_glide_bench import lines_until, send


FIXTURES = Path(__file__).resolve().parent / "fixtures"


def build_sender(platform, out):
    core, compiler = platform
    out.mkdir(parents=True, exist_ok=True)
    elf = out / "sender.elf"
    source = FIXTURES / "uart_logger_sender.c"
    subprocess.run([
        str(compiler), "-Os", "-ffreestanding", "-fno-builtin", "-nostdlib",
        "-mlongcalls", "-mtext-section-literals", "-I", str(core / "tools/sdk/include"),
        f"-Wl,-T,{source.with_suffix('.ld')}",
        f"-Wl,-T,{core / 'tools/sdk/ld/eagle.rom.addr.v6.ld'}",
        str(source), "-o", str(elf),
    ], check=True)
    esptool = core / "tools/esptool/esptool.py"
    prefix = out / "sender-"
    subprocess.run([sys.executable, str(esptool), "--chip", "esp8266", "elf2image",
                    "--version", "1", "-o", str(prefix), str(elf)], check=True)
    return esptool, out / "sender-0x00000.bin"


def load_cube(port, image):
    raw = image.read_bytes()
    if not 0 < len(raw) < 2 * 1024 * 1024:
        raise ValueError("expected a raw K210 image smaller than 2 MiB")
    with hkflash.open_runtime_serial(hkflash.require_serial(), port, 115200) as link:
        isp = hkflash.K210Isp(link)
        try:
            hkflash.connect_bootrom_sipeed_goe(isp, link, attempts=3)
            try:
                baud = 1_500_000
                # The Sipeed goE BootROM uses a different divisor calibration.
                isp.write_packet(hkflash.request(0xc6, struct.pack("<III", 0, 4, int(baud * 38.6 / 38))))
                time.sleep(0.05)
                link.baudrate = baud
                isp.bootrom_greeting(retries=2, timeout=0.4)
                isp.memory_write(hkflash.STUB_ADDRESS, raw)
            except (hkflash.ProtocolError, OSError):
                link.baudrate = 115200
                hkflash.connect_bootrom_sipeed_goe(isp, link, attempts=3)
                isp.memory_write(hkflash.STUB_ADDRESS, raw)
            isp.boot_sram(hkflash.STUB_ADDRESS)
            link.baudrate = 115200
            for attempt in range(6):
                send(link, "HKLINKINFO")
                try:
                    print(lines_until(link, r"HKLINKINFO .*pins=IO6/IO7.*", timeout=2)[-1])
                    break
                except TimeoutError:
                    if attempt == 5:
                        raise
        except BaseException:
            link.baudrate = 115200
            hkflash.reset_to_boot_sipeed_goe(link)
            raise


@contextmanager
def sender_port(port, esptool_path, image):
    spec = importlib.util.spec_from_file_location("esptool", esptool_path)
    esptool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(esptool)
    fw = esptool.LoadFirmwareImage("ESP8266", str(image))
    if not fw.segments or not all(
        0x40100000 <= segment.addr <= 0x40108000 - len(segment.data)
        or 0x3ffe9000 <= segment.addr <= 0x3ffea000 - len(segment.data)
        for segment in fw.segments
    ):
        raise ValueError("expected ESP8266 RAM segments")
    esp = esptool.ESP8266ROM(port, 115200)
    try:
        esp.connect(attempts=3)
        esptool.load_ram(esp, argparse.Namespace(filename=str(image)))
        esp._port.timeout = 0.2
        esp._port.write(b"I")
        line = lines_until(esp._port, r"UL TX CLOCK [0-9a-f]{8}")[-1]
        sender = Sender(esp._port, int(line.split()[3], 16))
        sender.calibrate()
        yield sender
    finally:
        esp._setDTR(False)
        esp.hard_reset()
        esp._port.close()


class Sender:
    def __init__(self, port, cycles_per_ms):
        self.port = port
        self.cycles_per_ms = cycles_per_ms

    def calibrate(self):
        estimates = []
        for _ in range(3):
            self.port.write(b"C")
            lines_until(self.port, r"UL CLOCK START")
            start = time.monotonic()
            lines_until(self.port, r"UL CLOCK END")
            estimates.append(320000 / (time.monotonic() - start))
        # Round to a whole MHz to absorb USB and Windows scheduling jitter.
        self.cycles_per_ms = 1000 * round(median(estimates) / 1000)
        self.port.write(struct.pack("<BI", ord("S"), self.cycles_per_ms))
        lines_until(self.port, r"UL CLOCK SET")

    def start(self, count):
        self.port.write(struct.pack("<BH", ord("T"), count))
        lines_until(self.port, r"UL TX START")

    def finish(self, count, timeout=6):
        line = lines_until(self.port, r"UL TX DONE [0-9a-f]{8} [0-9a-f]{16}", timeout)[-1]
        sent, elapsed = (int(word, 16) for word in line.split()[3:])
        if sent != count:
            raise RuntimeError(f"sender returned {sent} packets, expected {count}")
        return {"samples": sent, "cycles": elapsed, "cycles_per_ms": self.cycles_per_ms}

    def ping(self):
        payload = b"LOGGER"
        body = struct.pack("<BBHH", 1, 3, 42, len(payload)) + payload
        packet = b"HK" + body + struct.pack("<H", binascii.crc_hqx(body, 0xffff))
        self.port.write(bytes([ord("U"), len(packet)]) + packet)
        line = lines_until(self.port, r"UL WIRE [0-9a-f]{2}( [0-9a-f]*)?")[-1]
        reply = body[:1] + b"\x83" + body[2:]
        expected = b"HK" + reply + struct.pack("<H", binascii.crc_hqx(reply, 0xffff))
        if line != "UL WIRE 00 " + expected.hex():
            raise RuntimeError(f"external PING response: {line}")
        return line


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    load_cube(args.port, args.image)


if __name__ == "__main__":
    main()
