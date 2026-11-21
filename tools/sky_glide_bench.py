#!/usr/bin/env python3
"""Run and record Sky Glide comparisons on a single connected board."""

from __future__ import annotations

import argparse
from collections import defaultdict
import gzip
import hashlib
import json
from pathlib import Path
import re
from statistics import median, quantiles
import time

import serial

from hmpy_client import HmpyClient, HmpyClientError, open_serial_transport
from hmpy_protocol import MessageType, ProtocolDecodeError


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "examples" / "sky_glide_py.py"
BOARDS = ("huskylens-sen0305", "sipeed-maix-cube")
PATHS = ("c", "legacy", "combined")
MODES = ("logic", "paced", "burst", "input", "memory")


def send(link, command):
    link.write((command + "\n").encode("ascii"))
    link.flush()


def lines_until(link, pattern, timeout=6):
    end = time.monotonic() + timeout
    lines = []
    while time.monotonic() < end:
        line = link.readline().decode("ascii", "replace").strip()
        if line:
            lines.append(line)
            if re.fullmatch(pattern, line):
                return lines
    raise TimeoutError(f"expected {pattern!r}; last lines: {lines[-8:]!r}")


def command(link, text, pattern):
    send(link, text)
    return lines_until(link, pattern)


def source_for(mode, path):
    return SCRIPT.read_bytes().replace(
        b'BENCH_MODE = "play"', f'BENCH_MODE = "{mode}"'.encode(), 1
    ).replace(b'FRAME_PATH = "auto"', f'FRAME_PATH = "{path}"'.encode(), 1)


def upload_once(client, name, source):
    for item in client.list_files():
        if item.name == name:
            if item.size != len(source):
                raise RuntimeError(f"refusing to replace a different USERFS file named {name}")
            # Short replies are more robust across USB-UART bridges.
            for offset in range(0, len(source), 192):
                expected = source[offset:offset + 192]
                if client.read_file(name, offset, len(expected)) != expected:
                    raise RuntimeError(f"refusing to replace a different USERFS file named {name}")
            return False
    client.upload(name, source)
    return True


def python_run(link, board, mode, path, install=False, verified=None):
    client = HmpyClient(link, timeout=8)
    client.open(timeout=10)
    try:
        hello = client.hello()
        if hello.board != board or (not install and not hello.firmware_version.endswith("-skybench")):
            raise RuntimeError(f"unexpected device {hello}")
        if install:
            source = SCRIPT.read_bytes()
            started = time.monotonic()
            uploaded = upload_once(client, SCRIPT.name, source)
            return [str(hello), json.dumps({
                "uploaded": uploaded, "script_bytes": len(source),
                "source_sha256": hashlib.sha256(source).hexdigest(),
                "elapsed_seconds": time.monotonic() - started,
            }, sort_keys=True)]
        source = source_for(mode, path)
        name = f"sg_{hashlib.sha256(source).hexdigest()[:12]}.py"
        if verified is None or name not in verified:
            upload_once(client, name, source)
            if verified is not None:
                verified.add(name)
        seen = len(client.events)
        run = client.run(name, 120_000)
        deadline = time.monotonic() + 122
        keepalive = time.monotonic() + 4
        std = bytearray()
        shown = False
        failure = None
        while time.monotonic() < deadline:
            client.poll(0.2)
            events = client.events[seen:]
            seen = len(client.events)
            for event in events:
                if event.run_id != run:
                    continue
                if event.type in {MessageType.STDOUT, MessageType.STDERR}:
                    std.extend(event.data)
                    if mode == "input" and b"SG INPUT READY" in std and not shown:
                        print("Press physical OK twenty times, releasing between presses.", flush=True)
                        shown = True
                if event.type == MessageType.DROPPED:
                    raise RuntimeError(f"lost Python output: {event}")
                if event.type == MessageType.STATE and event.exit_reason:
                    if event.exit_reason != 1:
                        failure = event.exit_reason
            if failure is not None:
                client.poll(0.5)
                for event in client.events[seen:]:
                    if event.run_id == run and event.type in {MessageType.STDOUT, MessageType.STDERR}:
                        std.extend(event.data)
                raise RuntimeError(f"MicroPython exit {failure}: {std.decode('utf-8', 'replace')}")
            if re.search(rb"(?m)^SG (LOGIC|PACED|BURST|MEMORY) [^\r\n]+\r?\n|^SG INPUT DONE\r?\n", std):
                status = client.status()
                if status.exit_reason == 1 and status.run_id == run:
                    return std.decode("ascii", "replace").splitlines()
                if status.run_id == run and status.exit_reason:
                    raise RuntimeError(f"MicroPython exit {status.exit_reason}: {std!r}")
            if time.monotonic() >= keepalive:
                client.ping()
                keepalive = time.monotonic() + 4
        raise TimeoutError(f"{name} did not finish: {std!r}")
    finally:
        client.close()


def read_trace(link):
    header = command(link, "HKSGTRACE STOP", r"SG TRACE \d+ \d+ \d+")[-1]
    size, dropped, origin = map(int, header.split()[2:])
    if dropped:
        raise RuntimeError(f"display trace lost {dropped} rows")
    rows = []
    for offset in range(0, size, 8):
        page = command(link, f"HKSGTRACE READ {offset}", r"SG TRACE END")
        for line in page:
            if line.startswith("SG T "):
                row, begin, duration, result = map(int, line.split()[2:])
                if row != len(rows):
                    raise RuntimeError(f"trace row {row}, expected {len(rows)}")
                rows.append([origin + begin, duration, result])
    if len(rows) != size:
        raise RuntimeError(f"read {len(rows)} rows, expected {size}")
    return rows


def record(link, board, mode, path, run, verified):
    send(link, "")
    command(link, "HKMPSTOP", r"HKMPSTOP (OK|IDLE)")
    send(link, "HKMENU")
    time.sleep(0.3)
    if path == "c":
        command(link, "HKAPP sky-glide", r"HKAPP OK")
        time.sleep(0.2)
    trace_needed = mode in {"paced", "burst", "input"}
    if trace_needed:
        plane = "BASE" if path == "c" else "OVERLAY"
        command(link, f"HKSGTRACE {plane}", r"SG TRACE ARMED")
    if path == "c":
        command(link, f"HKSG{mode.upper()}", r"SG ARMED")
        if mode == "input":
            print("Press physical OK twenty times, releasing between presses.", flush=True)
        lines = lines_until(link, rf"SG {mode.upper()} .+", timeout=122)
    else:
        lines = python_run(link, board, mode, path, verified=verified)
    trace = read_trace(link) if trace_needed else []
    result = {
        "board": board, "run": run, "mode": mode, "path": path,
        "lines": [line for line in lines if line.startswith("SG ")], "trace": trace,
    }
    result["metrics"] = metrics(result)
    return result


def record_with_retry(link, board, mode, path, run, verified):
    for attempt in range(3):
        try:
            return record(link, board, mode, path, run, verified)
        except (HmpyClientError, ProtocolDecodeError, TimeoutError) as error:
            if mode == "input" or attempt == 2:
                raise
            print(f"transport retry {attempt + 1}: {error}", flush=True)
            # Let a session with a lost close acknowledgement expire, then
            # discard any partial framed or raw line before the next case.
            time.sleep(10.2)
            link.reset_input_buffer()


def distribution(values):
    p95 = quantiles(values, n=100, method="inclusive")[94] if len(values) > 1 else values[0]
    return {"median": median(values), "p95": p95, "max": max(values)}


def metrics(record):
    mode = record["mode"]
    last = next(line for line in record["lines"] if line.startswith("SG " + mode.upper() + " "))
    if mode == "logic":
        steps, elapsed, digest = last.split()[2:]
        digest_value = int(digest, 16 if record["path"] == "c" else 10)
        return {"steps": int(steps), "us": int(elapsed), "digest": f"{digest_value:06x}",
                "steps_per_second": int(steps) * 1_000_000 / int(elapsed)}
    if mode == "memory":
        total, before, peak, after = map(int, last.split()[2:])
        return {"gc_total": total, "before": before, "sampled_peak": peak, "after_gc": after}
    completed = [(start, start + duration, duration) for start, duration, result
                 in record["trace"] if result == 0]
    if mode == "input":
        presses = [int(line.split()[3]) for line in record["lines"] if line.startswith("SG PRESS ")]
        if len(presses) != 20:
            raise RuntimeError(f"expected twenty complete press records: {presses}")
        latency = []
        for i, press in enumerate(presses):
            finish = next(finish for start, finish, _ in completed if start >= press)
            if i + 1 < len(presses) and finish >= presses[i + 1]:
                raise RuntimeError("presses overlap; repeat with individual presses")
            latency.append(finish - press)
        return {"presses": len(presses), "input_to_lcd_us": distribution(latency),
                "samples_us": latency}
    begin, end, requested = map(int, last.split()[2:])
    frames = [row for row in completed if begin <= row[1] < end]
    finishes = [row[1] for row in frames]
    intervals = [b - a for a, b in zip(finishes, finishes[1:])]
    if not intervals or len(frames) > requested:
        raise RuntimeError(f"invalid {mode} frame count {len(frames)} / {requested}")
    return {
        "completed": len(frames), "requested": requested,
        "missed": requested - len(frames), "fps": len(frames) * 1_000_000 / (end - begin),
        "interval_us": distribution(intervals),
        "provider_us": distribution([row[2] for row in frames]),
        "failed": sum(begin <= start < end and result != 0
                      for start, _, result in record["trace"]),
    }


def summarize(path):
    groups = defaultdict(list)
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(path, "rt", encoding="utf-8") as records:
        for line in records:
            value = json.loads(line)
            groups[value["board"], value["mode"], value["path"]].append(value["metrics"])
    def midpoint(values):
        if isinstance(values[0], dict):
            return {key: midpoint([value[key] for value in values]) for key in values[0]}
        if isinstance(values[0], (int, float)):
            return round(median(values), 3)
        return values[0] if all(value == values[0] for value in values) else None
    for key, values in sorted(groups.items()):
        combined = midpoint(values)
        combined.pop("samples_us", None)
        print(*key, f"n={len(values)}", json.dumps(combined, sort_keys=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=BOARDS)
    parser.add_argument("--port")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--modes", nargs="+", choices=MODES,
                        default=["logic", "paced", "burst", "memory"])
    parser.add_argument("--paths", nargs="+", choices=PATHS, default=PATHS)
    parser.add_argument("--install", action="store_true", help="Upload the playable USERFS script")
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()
    if args.summary:
        summarize(args.summary)
        return
    if not args.board or not args.port or (not args.install and not args.output) or args.runs < 1:
        parser.error("provide --board, --port, and --output (or --install) and a positive --runs")
    with open_serial_transport(serial, args.port, 115200, timeout=0.08, write_timeout=5) as link:
        if args.install:
            print(*python_run(link, args.board, "play", "auto", install=True))
            return
        args.output.parent.mkdir(parents=True, exist_ok=True)
        done = set()
        verified = set()
        if args.output.is_file():
            for line in args.output.read_text(encoding="utf-8").splitlines():
                value = json.loads(line)
                done.add((value["board"], value["run"], value["mode"], value["path"]))
        with args.output.open("a", encoding="utf-8") as output:
            for run in range(args.runs):
                paths = args.paths[run % len(args.paths):] + args.paths[:run % len(args.paths)]
                for mode in args.modes:
                    for path in paths:
                        key = (args.board, run, mode, path)
                        if (mode == "memory" and path == "c") or key in done:
                            continue
                        print(args.board, run + 1, mode, path, flush=True)
                        value = record_with_retry(link, args.board, mode, path, run, verified)
                        output.write(json.dumps(value, separators=(",", ":")) + "\n")
                        output.flush()
                        print(json.dumps(value["metrics"], sort_keys=True), flush=True)
        send(link, "HKMENU")


if __name__ == "__main__":
    main()
