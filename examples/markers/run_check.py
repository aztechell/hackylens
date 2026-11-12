"""Run the optical marker check over USB and stream the board's output."""

import argparse
from pathlib import Path
import secrets
import sys
import time

import serial

DIRECTORY = Path(__file__).resolve().parent
sys.path.insert(0, str(DIRECTORY.parents[1] / "tools"))

from hmpy_client import HmpyClient, open_serial_transport
from hmpy_protocol import MessageType

ACTIVE_STATES = (1, 2, 3)
PASS_LINE = b"PASS MARKER CHECK"


def run_check(client, marker, seconds=90):
    if client.status().runtime_state in ACTIVE_STATES:
        raise RuntimeError("A Python script is already running")

    name = f"marker-check-{secrets.token_hex(4)}.py"
    if any(entry.name.casefold() == name.casefold() for entry in client.list_files()):
        raise RuntimeError("Temporary test filename already exists")
    source = (DIRECTORY / "check_markers.py").read_bytes()
    source = source.replace(b'MODE = "both"', f'MODE = "{marker}"'.encode("ascii"), 1)
    source = source.replace(b"PHASE_TIMEOUT_MS = 90000",
                            f"PHASE_TIMEOUT_MS = {seconds * 1000}".encode("ascii"), 1)
    timeout_ms = (seconds * (2 if marker == "both" else 1) + 15) * 1000
    run_id = None
    output = bytearray()
    try:
        client.upload(name, source)
        if client.read_file(name) != source:
            raise RuntimeError("Uploaded script differs from the local source")
        run_id = client.run(name, timeout_ms)
        deadline = time.monotonic() + timeout_ms / 1000 + 5
        while time.monotonic() < deadline:
            status = client.status()
            client.poll(0.2)
            for event in client.events:
                if event.run_id != run_id:
                    continue
                if event.type == MessageType.STDOUT:
                    output.extend(event.data)
                    sys.stdout.buffer.write(event.data)
                    sys.stdout.buffer.flush()
                elif event.type == MessageType.STDERR:
                    sys.stderr.buffer.write(event.data)
                    sys.stderr.buffer.flush()
            client.events.clear()
            if status.run_id == run_id and status.runtime_state not in ACTIVE_STATES:
                if status.exit_reason == 1 and PASS_LINE in output.splitlines():
                    return
                raise RuntimeError(f"Marker check failed (exit reason {status.exit_reason})")
        raise TimeoutError("No final state received from the board")
    finally:
        status = client.status()
        if run_id is not None and status.run_id == run_id and status.runtime_state in ACTIVE_STATES:
            client.stop()
            deadline = time.monotonic() + 5
            while status.run_id == run_id and status.runtime_state in ACTIVE_STATES:
                if time.monotonic() >= deadline:
                    raise RuntimeError("STOP did not finish before cleanup")
                client.poll(0.2)
                status = client.status()
        if any(entry.name == name for entry in client.list_files()):
            client.delete(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--board", required=True, choices=("huskylens-sen0305", "sipeed-maix-cube"))
    parser.add_argument("--marker", choices=("both", "qr", "apriltag"), default="both")
    parser.add_argument("--seconds", type=int, default=90, help="time allowed per marker")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 90:
        parser.error("--seconds must be between 1 and 90")
    with open_serial_transport(serial, args.port, 115200, timeout=0.05, write_timeout=2) as port:
        client = HmpyClient(port)
        client.open()
        try:
            info = client.hello()
            if info.board != args.board:
                raise RuntimeError(f"Expected {args.board}, got {info.board}")
            print(info.board, info.firmware_version, flush=True)
            run_check(client, args.marker, args.seconds)
        finally:
            client.close()


if __name__ == "__main__":
    main()
