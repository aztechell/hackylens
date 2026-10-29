from __future__ import annotations

import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import hkflash


class ControlLines:
    def __init__(self, events):
        object.__setattr__(self, "events", events)

    def __setattr__(self, name, value):
        self.events.append((name, value))


class FlashResetTests(unittest.TestCase):
    def test_goe_bootrom_entry_matches_sipeed_line_order_and_timing(self):
        events = []
        port = ControlLines(events)
        with patch.object(hkflash.time, "sleep", side_effect=lambda delay: events.append(("wait", delay))):
            hkflash.reset_to_isp_sipeed_goe(port)
        self.assertEqual(events, [
            ("dtr", False), ("rts", False), ("wait", 0.1),
            ("dtr", True), ("rts", False), ("wait", 0.1),
            ("rts", True), ("dtr", False), ("wait", 0.1),
        ])

    def test_goe_reboot_releases_boot_and_uses_sipeed_timing(self):
        events = []
        port = ControlLines(events)
        with patch.object(hkflash.time, "sleep", side_effect=lambda delay: events.append(("wait", delay))):
            hkflash.reset_to_boot_sipeed_goe(port)
        self.assertEqual(events, [
            ("dtr", False), ("rts", False), ("wait", 0.1),
            ("rts", False), ("dtr", True), ("wait", 0.1),
            ("rts", False), ("dtr", False), ("wait", 0.1),
        ])

    def test_goe_connection_retries_after_timeout_and_stops_on_response(self):
        port, isp = Mock(), Mock()
        isp.bootrom_greeting.side_effect = [hkflash.ProtocolError("timeout"), None]
        with patch.object(hkflash, "reset_to_isp_sipeed_goe") as reset, contextlib.redirect_stdout(io.StringIO()):
            hkflash.connect_bootrom_sipeed_goe(isp, port, attempts=3)
        self.assertEqual(reset.call_count, 2)
        self.assertEqual(isp.bootrom_greeting.call_count, 2)
        self.assertEqual(port.reset_input_buffer.call_count, 2)
        isp.bootrom_greeting.assert_called_with(retries=1, timeout=0.5)
        isp.memory_write.assert_not_called()

    def test_goe_connection_failure_is_bounded_and_does_not_upload_stub(self):
        port, isp = Mock(), Mock()
        isp.bootrom_greeting.side_effect = hkflash.ProtocolError("no response")
        with patch.object(hkflash, "reset_to_isp_sipeed_goe") as reset, contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(hkflash.ProtocolError, "Sipeed goE ISP entry failed"):
                hkflash.connect_bootrom_sipeed_goe(isp, port, attempts=2)
        self.assertEqual(reset.call_count, 2)
        isp.memory_write.assert_not_called()


if __name__ == "__main__":
    unittest.main()
