"""Run Object Gate C and MicroPython against the same staged KPU inputs."""

import argparse
from collections import defaultdict
from hashlib import sha256
import json
from pathlib import Path
import re
from statistics import median
import time

import hkflash
from hmpy_client import HmpyClient
from hmpy_protocol import MessageType
from sky_glide_bench import command, lines_until, send, upload_once


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "examples/object_gate.py"
BOARDS = ("sipeed-maix-cube", "huskylens-sen0305")
WARMUP = 4
MEASURED = 24
TOTAL = WARMUP + MEASURED
FIELDS = ("frames", "presented", "entries", "digest", "load_us",
          "detect_us", "policy_us", "present_us", "window_us")
RESULT = r"OG RESULT(?: \d+){9}"


def menu(link):
    send(link, "HKMENU")
    command(link, "HKPING", r"HKPONG")
    time.sleep(0.3)


def run_c(link):
    command(link, "HKOGBENCH", r"HKOGBENCH OK")
    command(link, "HKAPP object-gate", r"HKAPP OK")
    time.sleep(9)
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        lines = command(link, "HKOGINFO", r"HKOGINFO active=.*")
        status = lines[-1]
        if "active=0" in status:
            raise RuntimeError(f"C app closed before completing: {lines}")
        if status.endswith("complete=1"):
            return lines_until(link, RESULT)[-1]
        time.sleep(0.3)
    raise TimeoutError(f"Object Gate C did not complete: {status}")


def run_python(link, name, source, uploaded, board=BOARDS[0]):
    command(link, "HKAPP micropython", r"HKAPP OK")
    client = HmpyClient(link, timeout=8)
    client.open()
    output = bytearray()
    try:
        hello = client.hello()
        if hello.board != board:
            raise RuntimeError(f"unexpected firmware: {hello}")
        if not uploaded:
            upload_once(client, name, source, chunk_size=192)
        seen = len(client.events)
        run_id = client.run(name, 30000)
        deadline = time.monotonic() + 32
        keepalive = time.monotonic() + 4
        while time.monotonic() < deadline:
            client.poll(0.15)
            for event in client.events[seen:]:
                if event.run_id != run_id:
                    continue
                if event.type in (MessageType.STDOUT, MessageType.STDERR):
                    output.extend(event.data)
                if event.type == MessageType.DROPPED:
                    raise RuntimeError(f"lost output: {event}")
            seen = len(client.events)
            match = re.search(rf"(?m)^{RESULT}\r?\n", output.decode("utf-8", "replace"))
            if match or any(event.run_id == run_id and event.exit_reason for event in client.events[-8:]):
                status = client.status()
                if status.run_id == run_id and status.exit_reason:
                    if status.exit_reason != 1:
                        raise RuntimeError(output.decode("utf-8", "replace"))
                    if match:
                        return output.decode("utf-8", "replace").splitlines()
            if time.monotonic() >= keepalive:
                client.ping()
                keepalive = time.monotonic() + 4
        raise TimeoutError(output.decode("utf-8", "replace"))
    finally:
        client.close()


def summarize(rows):
    for path in ("c", "python"):
        runs = rows[path]
        if not runs:
            continue
        result = {"n": len(runs), "load_ms": median(run["load_us"] for run in runs) / 1000,
                  "fps": median(run["frames"] * 1e6 / run["window_us"] for run in runs)}
        for field in ("detect_us", "policy_us", "present_us", "kpu_us"):
            result[field] = median(run[field] / run["frames"] for run in runs)
        print(f"{path} {json.dumps(result, sort_keys=True)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=BOARDS, default=BOARDS[0])
    parser.add_argument("--port", required=True)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--runs", type=int, default=5)
    args = parser.parse_args()
    if args.runs <= 0:
        parser.error("--runs must be positive")
    script = SCRIPT.read_bytes()
    source = script.replace(b"BENCH_FRAMES = 0", f"BENCH_FRAMES = {TOTAL}".encode(), 1)
    name = f"og_{sha256(source).hexdigest()[:12]}.py"
    image = sha256(args.image.read_bytes()).hexdigest()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    rows = defaultdict(list)
    reference = None
    uploaded = False
    with hkflash.open_runtime_serial(hkflash.require_serial(), args.port, 115200) as link, \
            args.out.open("x", encoding="utf-8") as raw:
        try:
            menu(link)
            for index in range(1, args.runs + 1):
                order = ("c", "python") if index % 2 else ("python", "c")
                for path in order:
                    command(link, "HKOGREPLAY ON", r"HKOGREPLAY ON")
                    if path == "c":
                        line = run_c(link)
                    else:
                        lines = run_python(link, name, source, uploaded, args.board)
                        line = next(line for line in lines if re.fullmatch(RESULT, line))
                    uploaded |= path == "python"
                    stats = dict(zip(FIELDS, map(int, line.split()[2:]), strict=True))
                    kpu_count, kpu_us = map(int, command(link, "HKOGKPU", r"HKOGKPU \d+ \d+")[-1].split()[1:])
                    row = {"board": args.board, "path": path, "run": index,
                           "image_sha256": image, "source_sha256": sha256(script).hexdigest(),
                           "kpu_count": kpu_count, "kpu_us": kpu_us, **stats}
                    if (stats["frames"], stats["presented"], stats["entries"], kpu_count) != (MEASURED, MEASURED, 2, TOTAL):
                        raise RuntimeError(f"unexpected replay result: {row}")
                    if reference is None:
                        reference = stats["digest"]
                    if stats["digest"] != reference:
                        raise RuntimeError(f"decoded results differ: {row}")
                    rows[path].append(row)
                    raw.write(json.dumps(row, sort_keys=True) + "\n")
                    raw.flush()
                    print(path, index, row, flush=True)
                    menu(link)
        finally:
            command(link, "HKOGREPLAY OFF", r"HKOGREPLAY OFF")
            menu(link)
            if uploaded:
                client = HmpyClient(link, timeout=8)
                client.open()
                try:
                    client.delete(name)
                finally:
                    client.close()
    summarize(rows)


if __name__ == "__main__":
    main()
