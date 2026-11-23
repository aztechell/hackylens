from binascii import crc_hqx
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "firmware/src/apps/uart_logger"
SPEC = importlib.util.spec_from_file_location("uart_logger", ROOT / "examples/uart_logger.py")
logger = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(logger)


def packet(seq, value):
    body = struct.pack("<Hh", seq, value)
    return b"\xa5\x5a" + body + struct.pack("<H", crc_hqx(body, 0xffff))


class FakeStorage:
    def __init__(self):
        self.begins = self.finishes = self.cancels = 0
        self.data = bytearray()
        self.fail_write = False
        self.wait_once = True

    def sd_begin(self, name):
        assert name == "TL000010.CSV"
        self.begins += 1
        return self.begins == 2

    def sd_append(self, data):
        assert len(data) <= 256
        if self.fail_write:
            raise OSError("card removed")
        if self.wait_once:
            self.wait_once = False
            return 0
        count = min(7, len(data))
        self.data.extend(data[:count])
        return count

    def sd_finish(self):
        self.finishes += 1
        return self.finishes == 2

    def sd_cancel(self):
        self.cancels += 1


class UartLoggerTests(unittest.TestCase):
    def test_stream_csv_and_graph_match_c(self):
        cc = os.environ.get("CC") or shutil.which("gcc")
        if not cc:
            self.skipTest("C compiler unavailable")
        head = [65533, 65534, 65535, 0, 1, 3, 4]
        samples = [(seq, -32768 if seq % 2 == 0 else 32767) for seq in head + list(range(5, 114))]
        data = bytearray(b"noise\xa5\xa5")
        for seq, value in samples:
            if seq == 3:
                broken = packet(2, 12)
                data.extend(broken[:-1] + bytes([broken[-1] ^ 1]))
            if seq == 4:
                data.extend(b"\xa5\x5a\x8d")
            data.extend(packet(seq, value))
            if seq == 1:
                data.extend(packet(1, 7) + packet(65535, 9))
        data.extend(b"\xa5\x5a")

        stream = logger.Stream()
        rows = []
        for offset in range(0, len(data), 13):
            for byte in data[offset:offset + 13]:
                if stream.feed(byte):
                    rows.append(logger.csv_row(stream.seq, stream.value).strip())
        self.assertEqual(rows, [f"{seq},{value}" for seq, value in samples])
        self.assertEqual((stream.accepted, stream.gaps, stream.crc_errors, stream.stale),
                         (len(samples), 1, 2, 2))
        self.assertEqual(stream.count, 96)
        expected = rows + [
            f"S {stream.accepted} {stream.gaps} {stream.crc_errors} {stream.stale} {stream.digest}",
        ] + ["R " + " ".join(map(str, rect))
             for rect in [*logger.GRID, *(stream.point(i) for i in range(stream.count))]]

        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "logger.exe"
            wire = Path(tmp) / "wire.bin"
            wire.write_bytes(data)
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(CORE),
                            str(CORE / "uart_logger_core.c"), str(ROOT / "tests/uart_logger_harness.c"),
                            "-o", str(target)], check=True, cwd=ROOT)
            native = subprocess.check_output([str(target), str(wire)], text=True)
        self.assertEqual(native.splitlines(), expected)

    def test_recording_drains_partial_and_zero_length_appends_before_publish(self):
        sd = FakeStorage()
        recorder = logger.Recorder(sd)
        recorder.start("TL000010.CSV")
        for seq in range(33):
            recorder.accept(seq, seq - 30)
            recorder.step()
        recorder.stop()
        recorder.accept(33, 230)
        for _ in range(100):
            recorder.step()
        self.assertEqual(recorder.state, "SAVED")
        self.assertEqual((sd.begins, sd.finishes, sd.cancels), (2, 2, 0))
        self.assertEqual(bytes(sd.data),
                         ("seq,value\n" + "".join(f"{seq},{seq - 30}\n" for seq in range(33))).encode())

    def test_storage_failure_and_queue_overflow_cancel(self):
        for fail_write in (True, False):
            sd = FakeStorage()
            sd.fail_write = fail_write
            recorder = logger.Recorder(sd)
            recorder.start("TL000010.CSV")
            recorder.step()
            recorder.step()
            for seq in range(16 if fail_write else 65):
                recorder.accept(seq, 0)
            recorder.step()
            self.assertEqual(recorder.state, "SD ERROR")
            self.assertEqual((sd.finishes, sd.cancels), (0, 1))
