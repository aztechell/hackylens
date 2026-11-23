"""Boot a temporary Object Gate image on SEN0305 without writing flash."""

import argparse
from contextlib import suppress
from pathlib import Path
import struct
import time

import hkflash
from sky_glide_bench import lines_until, send


class PacedIsp(hkflash.K210Isp):
    def write_packet(self, packet):
        wire = hkflash.slip_encode(packet)
        for offset in range(0, len(wire), 256):
            self.ser.write(wire[offset:offset + 256])
            time.sleep(0.002)


def load_sen(port, image, baud):
    raw = image.read_bytes()
    if not 0 < len(raw) < 2 * 1024 * 1024:
        raise ValueError("expected a raw K210 image smaller than 2 MiB")
    with hkflash.open_runtime_serial(hkflash.require_serial(), port, 115200) as link:
        isp = PacedIsp(link)
        try:
            hkflash.connect_bootrom_uploader(isp, link, attempts=3)
            if baud != 115200:
                try:
                    # K210 BootROM divisor correction, as in the Cube RAM loader.
                    divisor = round(baud * 38.6 / 38)
                    isp.write_packet(hkflash.request(0xc6, struct.pack("<III", 0, 4, divisor)))
                    time.sleep(0.05)
                    link.baudrate = baud
                    isp.bootrom_greeting(retries=3, timeout=0.5)
                except (OSError, hkflash.ProtocolError):
                    link.baudrate = 115200
                    hkflash.connect_bootrom_uploader(isp, link, attempts=3)
            print(f"[RAM] uploading at {link.baudrate} baud", flush=True)
            isp.memory_write(hkflash.STUB_ADDRESS, raw)
            isp.boot_sram()
            link.baudrate = 115200
            for attempt in range(6):
                send(link, "HKOGREPLAY OFF")
                try:
                    received = lines_until(link, "HKOGREPLAY OFF", timeout=2)
                except TimeoutError:
                    if attempt == 5:
                        raise
                else:
                    print(received[-1], flush=True)
                    return
        except BaseException:
            with suppress(OSError):
                link.baudrate = 115200
                hkflash.reset_to_boot_uploader(link, pulse=0.1)
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("image", type=Path)
    parser.add_argument("--baud", type=int, choices=(115200, 460800), default=460800)
    args = parser.parse_args()
    load_sen(args.port, args.image, args.baud)


if __name__ == "__main__":
    main()
