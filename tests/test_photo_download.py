import binascii
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import hkflash


class SerialFixture(io.BytesIO):
    def __init__(self, response):
        super().__init__(response)
        self.commands = []

    def write(self, data):
        self.commands.append(data)
        return len(data)


class PhotoDownloadTests(unittest.TestCase):
    def test_list_orders_numbered_photos_newest_first(self):
        serial = SerialFixture(
            b"[SD] mounted\r\nHKPHOTOS ITEM IMG00001.PPM 3\n"
            b"HKPHOTOS ITEM IMG00012.BMP 123\nHKPHOTOS END\n"
        )
        self.assertEqual(hkflash.list_sd_photos(serial, 1),
                         [("IMG00012.BMP", 123), ("IMG00001.PPM", 3)])
        self.assertEqual(serial.commands, [b"HKPHOTOS\n"])

    def test_binary_chunks_and_crc(self):
        first = bytes(range(256)) * 4
        last = b"\nHKPHOTO DATA \xff\x00"
        header = f"HKPHOTO BEGIN IMG00001.BMP {len(first) + len(last)}\n".encode()
        header += f"HKPHOTO DATA 0 1024 {binascii.crc32(first):08X}\n".encode() + first
        final = f"HKPHOTO DATA 1024 {len(last)} {binascii.crc32(last):08X}\n".encode()
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "IMG00001.BMP"
            bad_serial = SerialFixture(header + (final + bytes(len(last))) * 3)
            with self.assertRaisesRegex(hkflash.ProtocolError, "CRC mismatch"):
                hkflash.download_sd_photo(bad_serial, target.name, target, 1)
            self.assertEqual(list(target.parent.iterdir()), [])

            serial = SerialFixture(header + final + bytes(len(last)) + final + last)
            hkflash.download_sd_photo(serial, target.name, target, 1)
            self.assertEqual(target.read_bytes(), first + last)
            self.assertEqual(serial.commands,
                             [b"HKPHOTO IMG00001.BMP\n", b"HKPHOTOREAD 0\n",
                              b"HKPHOTOREAD 1024\n", b"HKPHOTOREAD 1024\n"])

    def test_card_removed_during_download(self):
        serial = SerialFixture(b"HKPHOTO BEGIN IMG00001.BMP 50\nHKPHOTO ERR CARD CHANGED\n")
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "IMG00001.BMP"
            with self.assertRaisesRegex(hkflash.ProtocolError, "CARD CHANGED"):
                hkflash.download_sd_photo(serial, target.name, target, 1)
            self.assertEqual(list(target.parent.iterdir()), [])
