"""Compare C and MicroPython UART loggers on one board's diagnostic image."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import time

import hkflash
from hmpy_client import HmpyClient, HmpyClientError
from hmpy_protocol import MessageType, ProtocolDecodeError
from sky_glide_bench import command, distribution, lines_until, read_trace, send, upload_once
from uart_logger_fixture import SENDER_TX_PINS, build_sender, sender_port


SCRIPT = Path(__file__).resolve().parents[1] / "examples" / "uart_logger.py"
BOARD = "sipeed-maix-cube"
BOARD_PINS = {"sipeed-maix-cube": "IO6/IO7", "huskylens-sen0305": "IO34/IO35"}
CASES = (("c", "graph"), ("python", "graph"), ("c", "record"), ("python", "record"))
WARMUP = 100
TOTAL = 1600


def value_for(sequence):
    return (sequence % 201 - 100) * 10


def expected_digest(count):
    digest = 2166136261
    for sequence in range(count):
        for byte in struct.pack("<Hh", sequence, value_for(sequence)):
            digest = ((digest ^ byte) * 16777619) & 0xffffffff
    return digest


def expected_csv():
    return ("seq,value\n" + "".join(
        f"{sequence},{value_for(sequence)}\n" for sequence in range(WARMUP, TOTAL)
    )).encode("ascii")


def menu(link):
    send(link, "HKMENU")
    command(link, "HKPING", r"HKPONG")


def link_config(link, board=BOARD):
    line = command(link, "HKLINKINFO", r"HKLINKINFO mode=(UART|I2C) pins=IO\d+/IO\d+ .*")[-1]
    if f"pins={BOARD_PINS[board]} " not in line:
        raise RuntimeError(f"{board} benchmark expects {BOARD_PINS[board]}. Current line: {line}")
    mode, baud = re.search(r"mode=(\w+).* uart=(\d+)", line).groups()
    return mode, int(baud)


def script_for(mode):
    source = SCRIPT.read_bytes().replace(
        b'BENCH_MODE = "play"', f'BENCH_MODE = "{mode if mode != "memory" else "record"}"'.encode(), 1)
    if mode == "memory":
        source = source.replace(
            b"    hl.uart_init(115200)",
            b"    import gc\n    gc.collect()\n    before = peak = gc.mem_alloc()\n\n    hl.uart_init(115200)", 1)
        source = source.replace(
            b"            recorder.step()",
            b"            recorder.step()\n            peak = max(peak, gc.mem_alloc())", 1)
        source = source.replace(
            b'        print("UL RESULT"',
            b'        gc.collect()\n        print("UL MEM", before, peak, gc.mem_alloc())\n        print("UL RESULT"', 1)
    return source


def python_run(link, mode, ready, verified, record):
    command(link, "HKAPP micropython", r"HKAPP OK")
    source = script_for(mode)
    name = f"ul_{hashlib.sha256(source).hexdigest()[:12]}.py"
    record["script"] = name
    client = HmpyClient(link, timeout=8)
    client.open(timeout=10)
    output = bytearray()
    try:
        hello = client.hello()
        if hello.board != record["board"] or not hello.firmware_version.endswith("-uartbench"):
            raise RuntimeError(f"unexpected device {hello}")
        record["firmware_version"] = hello.firmware_version
        if name not in verified:
            started = time.monotonic()
            if upload_once(client, name, source, chunk_size=192):
                record["upload_seconds"] = time.monotonic() - started
            verified.add(name)
        seen = len(client.events)
        run = client.run(name, 120_000)
        deadline = time.monotonic() + 120
        keepalive = time.monotonic() + 4
        started_sender = False
        while time.monotonic() < deadline:
            client.poll(0.15)
            for event in client.events[seen:]:
                if event.run_id == run and event.type in (MessageType.STDOUT, MessageType.STDERR):
                    output.extend(event.data)
                if event.run_id == run and event.type == MessageType.DROPPED:
                    raise RuntimeError(f"lost MicroPython output: {event}")
            seen = len(client.events)
            if ready and not started_sender and re.search(rb"(?m)^UL READY \d+\r?\n", output):
                ready()
                started_sender = True
            pattern = rb"(?m)^UL PARSE .+\r?\n" if mode == "parse" else rb"(?m)^UL END \d+\r?\n"
            if re.search(pattern, output) or any(
                    event.run_id == run and event.type == MessageType.STATE and event.exit_reason
                    for event in client.events[max(0, seen - 12):seen]):
                status = client.status()
                if status.run_id == run and status.exit_reason:
                    if status.exit_reason == 1:
                        client.poll(0.25)
                        for event in client.events[seen:]:
                            if event.run_id == run and event.type in (MessageType.STDOUT, MessageType.STDERR):
                                output.extend(event.data)
                        if re.search(pattern, output):
                            return
                    raise RuntimeError(f"MicroPython exit {status.exit_reason}: {output[-1024:]!r}")
            if time.monotonic() >= keepalive:
                client.ping()
                keepalive = time.monotonic() + 4
        raise TimeoutError(f"{name} did not finish: {output[-512:]!r}")
    finally:
        record["lines"] = output.decode("utf-8", "replace").splitlines()
        client.close()


def native_run(link, mode, ready, record):
    command(link, "HKULMODE " + mode.upper(), r"UL MODE OK")
    send(link, "HKAPP uart-logger")
    lines = record["lines"] = []
    if mode == "parse":
        lines.extend(lines_until(link, r"UL PARSE \d+ \d+ \d+"))
        lines.extend(lines_until(link, r"HKAPP OK"))
        return
    lines.extend(lines_until(link, r"UL READY \d+"))
    ready()
    lines.extend(lines_until(link, r"UL END \d+", timeout=42))
    count = int(lines[-1].split()[2])
    for offset in range(0, count, 8):
        lines.extend(command(link, f"HKULMARK {offset}", r"UL MARK END"))


def csv_on_device(link, name):
    if not re.fullmatch(r"TL[0-9A-F]{6}\.CSV", name):
        raise ValueError(f"unexpected CSV name {name!r}")
    hkflash.send_photo_command(link, "HKULFILE " + name)
    header = hkflash.photo_response(link, b"HKPHOTO BEGIN ", 6).decode("ascii").split()
    if len(header) != 2 or header[0] != name or not header[1].isdigit():
        raise RuntimeError(f"unexpected CSV header: {header}")
    size = int(header[1])
    data = bytearray()
    while len(data) < size:
        count = min(size - len(data), hkflash.PHOTO_CHUNK_BYTES)
        for attempt in range(3):
            try:
                data.extend(hkflash.read_photo_chunk(link, len(data), count, 4))
                break
            except (hkflash.ProtocolError, TimeoutError):
                if attempt == 2:
                    raise
                link.reset_input_buffer()
    return bytes(data)


def line_fields(lines, prefix):
    return next(line.removeprefix(prefix).split() for line in lines if line.startswith(prefix))


def render_metrics(record):
    lines = record["lines"]
    start, = map(int, line_fields(lines, "UL READY "))
    size, = map(int, line_fields(lines, "UL END "))
    marks = [list(map(int, line.split()[2:])) for line in lines if line.startswith("UL M ")]
    if [row[0] for row in marks] != list(range(size)) or not size:
        raise RuntimeError(f"expected {size} consecutive display marks, got {len(marks)}")

    completions, latency, display = [], [], []
    scan = 0
    for index, (_, begin, sample, _) in enumerate(marks):
        until = start + marks[index + 1][1] if index + 1 < size else float("inf")
        while scan < len(record["trace"]) and record["trace"][scan][0] < start + begin:
            scan += 1
        if scan == len(record["trace"]) or record["trace"][scan][0] >= until:
            continue
        send_start, duration, result = record["trace"][scan]
        scan += 1
        if result == 0:
            completions.append(send_start + duration)
            latency.append((send_start + duration - start - sample) / 1000)
            display.append(duration / 1000)
    if len(completions) < 2:
        raise RuntimeError("fewer than two recorded LCD completions")
    intervals = [(b - a) / 1000 for a, b in zip(completions, completions[1:])]
    seconds = (TOTAL - WARMUP) / 50
    return {
        "frames": len(completions), "missing_frames": round(seconds * 10) - len(completions),
        "unmatched_marks": size - len(completions), "fps": len(completions) / seconds,
        "interval_ms": distribution(intervals), "display_ms": distribution(display),
        "sample_to_lcd_ms": distribution(latency),
    }


def trial(link, transmitter, out, record, verified):
    language, mode = record["language"], record["mode"]
    runners = {
        "c": lambda ready: native_run(link, mode, ready, record),
        "python": lambda ready: python_run(link, mode, ready, verified, record),
    }
    menu(link)
    default = link_config(link, record["board"])
    sender_started = False

    def start_sender():
        nonlocal sender_started
        transmitter.start(TOTAL)
        sender_started = True

    try:
        if mode == "parse":
            runners[language](None)
            count, digest, us = map(int, line_fields(record["lines"], "UL PARSE "))
            if count != 4096 or digest != expected_digest(count) or us <= 0:
                raise RuntimeError(f"offline parser mismatch: {count} {digest} {us}")
            record["metrics"] = {"count": count, "digest": digest, "active_us": us,
                                 "samples_per_second": count * 1_000_000 / us}
            return

        plane = "BASE" if language == "c" else "OVERLAY"
        command(link, f"HKDTRACE {plane}", r"SG TRACE ARMED")
        runners[language](start_sender)
        record["sender"] = transmitter.finish(TOTAL)
        record["trace"] = read_trace(link)
        fields = line_fields(record["lines"], "UL RESULT ")
        actual = list(map(int, fields[:7]))
        expected = [TOTAL, TOTAL - WARMUP, 0, 0, 0, 0, expected_digest(TOTAL)]
        if actual != expected:
            raise RuntimeError(f"unexpected accepted/missed/CRC counters or digest: {fields}")
        record["metrics"] = render_metrics(record)
        if mode in ("record", "memory"):
            if fields[7] != "SAVED":
                raise RuntimeError(f"recording did not publish: {fields}")
            name = fields[8]
            data = csv_on_device(link, name)
            (out / f"{record['run']:02d}-{language}-{name}").write_bytes(data)
            record["csv"] = {"path": "/PYDATA/" + name, "bytes": len(data),
                             "sha256": hashlib.sha256(data).hexdigest()}
            if data != expected_csv():
                raise RuntimeError(f"{name} differs from the independent CSV reference")
        if mode == "memory":
            before, peak, after = map(int, line_fields(record["lines"], "UL MEM "))
            record["gc_bytes"] = {"initialized": before, "observed_peak": peak, "after_collect": after}
    finally:
        if sender_started and "sender" not in record:
            record["sender"] = transmitter.finish(TOTAL, timeout=36)
        menu(link)
        restored = link_config(link, record["board"])
        record["link_before"] = default
        record["link_after"] = restored
        if restored != default:
            raise RuntimeError(f"default link was not restored: {default} -> {restored}")


def run_cases(args):
    args.out.mkdir(parents=True, exist_ok=True)
    results = args.out / "runs.jsonl"
    sources = hashlib.sha256(SCRIPT.read_bytes()).hexdigest()
    image = hashlib.sha256(args.image.read_bytes()).hexdigest()
    configuration = (args.board, args.sender_tx, image, sources)
    done = set()
    if results.exists():
        for text in results.read_text(encoding="utf-8").splitlines():
            row = json.loads(text)
            if not row.get("ok"):
                continue
            recorded = (row["board"], row.get("sender_tx", "D1"), row["image_sha256"], row["source_sha256"])
            if recorded != configuration:
                raise RuntimeError("choose a new output directory when changing the board, TX pin, image or script")
            done.add((row["run"], row["language"], row["mode"]))
    esptool, sender_image = build_sender((args.esp_core, args.esp_gcc), args.out, args.sender_tx)
    verified = set()
    serial = hkflash.require_serial()
    with sender_port(args.sender_port, esptool, sender_image) as tx, \
            hkflash.open_runtime_serial(serial, args.port, 115200) as link:
        for attempt in range(4):
            try:
                command(link, "HKPING", r"HKPONG")
                break
            except TimeoutError:
                if attempt == 3:
                    raise
        print(f"{args.board} {args.port}, NodeMCU {args.sender_port} TX {args.sender_tx} "
              f"at {tx.cycles_per_ms} cycles/ms", flush=True)
        groups = ((args.parse_runs, (("c", "parse"), ("python", "parse"))), (args.runs, CASES),
                  (int(args.memory_run), (("python", "memory"),)))
        for runs, cases in groups:
            for index in range(1, runs + 1):
                ordered = cases if index % 2 else reversed(cases)
                for language, mode in ordered:
                    if (index, language, mode) in done:
                        continue
                    for attempt in range(1, 4):
                        row = {"date_utc": datetime.now(timezone.utc).isoformat(), "board": args.board,
                               "run": index, "language": language, "mode": mode, "attempt": attempt,
                               "image_sha256": image, "source_sha256": sources, "sender_tx": args.sender_tx}
                        print(f"Starting {index} {language} {mode} (attempt {attempt})", flush=True)
                        try:
                            trial(link, tx, args.out, row, verified)
                            row["ok"] = True
                            print(f"{index} {language} {mode} {row['metrics']}", flush=True)
                            break
                        except Exception as error:
                            row["ok"] = False
                            row["error"] = repr(error)
                            if attempt == 3 or not isinstance(
                                    error, (HmpyClientError, ProtocolDecodeError, TimeoutError)):
                                raise
                            print(f"Transport retry: {error}", flush=True)
                        finally:
                            with results.open("a", encoding="utf-8") as log:
                                log.write(json.dumps(row, sort_keys=True) + "\n")
                        time.sleep(10.2)
                        link.reset_input_buffer()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=tuple(BOARD_PINS), default=BOARD)
    parser.add_argument("--port", "--cube-port", required=True)
    parser.add_argument("--sender-port", required=True)
    parser.add_argument("--sender-tx", choices=tuple(SENDER_TX_PINS), default="D1",
                        help="NodeMCU pin connected to the board's RX")
    parser.add_argument("--esp-core", type=Path, required=True)
    parser.add_argument("--esp-gcc", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--parse-runs", type=int, default=5)
    parser.add_argument("--memory-run", action="store_true")
    run_cases(parser.parse_args())


if __name__ == "__main__":
    main()
