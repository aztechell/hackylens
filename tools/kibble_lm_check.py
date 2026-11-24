"""Compare KibbleLM C and MicroPython on a board running a test build."""

import argparse
import ast
from hashlib import sha256
import json
from pathlib import Path
import re
import time

import hkflash
from hmpy_client import HmpyClient
from hmpy_protocol import MessageType
from sky_glide_bench import command, lines_until, send, upload_once


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "examples/kibble_lm.py"
BOARDS = ("huskylens-sen0305", "sipeed-maix-cube")
PROMPTS = ("Once upon a time", "The puppy found")
FIELDS = ("tokens", "first_us", "elapsed_us", "accelerator_us", "head_us")
NATIVE_INFO = r"HKKLINFO phase=(\d+)" + "".join(rf" {field}=(\d+)" for field in FIELDS)
PYTHON_RESULT = re.compile(r"KLPY(?: \d+){5} .+")


def menu(link):
    send(link, "HKMENU")
    command(link, "HKPING", "HKPONG")


def native_result(link, prompt):
    command(link, "HKAPP kibble-lm", "HKAPP OK")
    try:
        wait_phase(link, 1)
        command(link, "HKKL " + prompt, "HKKL OK")
        return wait_phase(link, 3)
    finally:
        menu(link)


def wait_phase(link, target):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        send(link, "HKKLINFO")
        lines = lines_until(link, r"HKKL TEXT .*|HKKLINFO active=0")
        info = next((re.fullmatch(NATIVE_INFO, line) for line in lines
                     if line.startswith("HKKLINFO phase=")), None)
        if info is None:
            raise RuntimeError(f"unexpected C app status: {lines}")
        phase, *values = map(int, info.groups())
        if phase == target:
            return {**dict(zip(FIELDS, values, strict=True)), "text": lines[-1][10:]}
        if phase == 4:
            raise RuntimeError(f"KibbleLM failed to load or generate: {lines}")
        time.sleep(0.12)
    raise TimeoutError(f"KibbleLM never reached phase {target}: {lines}")


def test_source(source):
    app, _ = source.rsplit('if __name__ == "__main__":', 1)
    return (app + f"""
import hackylens as hl
app = StoryApp(hl)
app.render()
while not app.open_step():
    hl.sleep_ms(5)
app.render()
for prompt, seed in (("", 210), ("x" * 25, 210), ("x\\0y", 210),
                     ("\\u00e9", 210), (b"Hello", 210), ("Hello", -1),
                     ("Hello", 1 << 32)):
    try:
        hl.kibble_start(prompt, seed)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid prompt or seed accepted")
for prompt in {PROMPTS!r}:
    app.start(prompt, 210)
    start = hl.ticks_us()
    elapsed = first = kpu = head = 0
    app.render()
    while app.phase == GENERATING:
        token = app.poll()
        if token is not None:
            elapsed = hl.ticks_us() - start
            if not first and not token[1]:
                first = elapsed
            kpu += token[2]
            head += token[3]
            app.render()
        hl.sleep_ms(5)
    print("KLPY", app.tokens, first, elapsed, kpu, head, repr(app.text))
app.start({PROMPTS[0]!r}, 210)
assert app.poll() is None
print("KLPY STOP READY")
while True:
    hl.sleep_ms(20)
""").encode("utf-8")


def python_results(link, board, source):
    command(link, "HKAPP micropython", "HKAPP OK")
    script = test_source(source)
    names = {
        "probe": (f"kl_{sha256(script).hexdigest()[:12]}.py", script),
        "ui": (f"kl_{sha256(source.encode()).hexdigest()[:12]}.py", source.encode()),
    }
    client = HmpyClient(link, timeout=8)
    client.open()
    created = []
    run = None
    try:
        hello = client.hello()
        if hello.board != board:
            raise RuntimeError(f"unexpected board on serial port: {hello}")
        for name, data in names.values():
            if upload_once(client, name, data, chunk_size=192):
                created.append(name)
        seen = len(client.events)
        run = client.run(names["probe"][0], 45_000)
        output = wait_output(client, run, seen, b"KLPY STOP READY")
        client.stop()
        wait_stopped(client, run)

        run = client.run(names["ui"][0], 15_000)
        client.poll(2)
        status = client.status()
        if status.run_id != run or status.exit_reason:
            raise RuntimeError(f"the unmodified UI script exited early: {status}")
        client.stop()
        wait_stopped(client, run)
    finally:
        try:
            if run is not None:
                status = client.status()
                if status.run_id == run and not status.exit_reason:
                    client.stop()
                    wait_stopped(client, run)
            for name in created:
                client.delete(name)
        finally:
            client.close()
            menu(link)

    results = []
    for line in output.decode("ascii", "replace").splitlines():
        if PYTHON_RESULT.fullmatch(line):
            fields = line.split(maxsplit=6)
            stats = dict(zip(FIELDS, map(int, fields[1:6]), strict=True))
            results.append({**stats, "text": ast.literal_eval(fields[6])})
    return results


def wait_output(client, run, seen, marker):
    result = bytearray()
    deadline = time.monotonic() + 36
    keepalive = time.monotonic() + 4
    failure = None
    while time.monotonic() < deadline:
        client.poll(0.15)
        for event in client.events[seen:]:
            if event.run_id != run:
                continue
            if event.type in (MessageType.STDOUT, MessageType.STDERR):
                result.extend(event.data)
            if event.type == MessageType.DROPPED:
                raise RuntimeError("firmware dropped the probe output")
            if event.type == MessageType.STATE and event.exit_reason:
                failure = event.exit_reason
        seen = len(client.events)
        if failure is not None:
            client.poll(0.6)
            for event in client.events[seen:]:
                if event.run_id == run and event.type in (MessageType.STDOUT, MessageType.STDERR):
                    result.extend(event.data)
            raise RuntimeError(f"MicroPython exited with {failure}: {result.decode('utf-8', 'replace')}")
        if marker in result:
            return bytes(result)
        if time.monotonic() >= keepalive:
            client.ping()
            keepalive = time.monotonic() + 4
    raise TimeoutError(f"MicroPython did not reach the stop point: {result!r}")


def wait_stopped(client, run):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        status = client.status()
        if status.run_id == run and status.exit_reason:
            if status.exit_reason != 2:
                raise RuntimeError(f"expected requested stop: {status}")
            return
        client.poll(0.05)
    raise TimeoutError("MicroPython did not release its run")


def comparable(result):
    return result["tokens"], result["text"]


def check(link, board, source):
    menu(link)
    native = [native_result(link, prompt) for prompt in PROMPTS]
    python = python_results(link, board, source)
    if list(map(comparable, native)) != list(map(comparable, python)):
        raise RuntimeError(f"C and MicroPython differ: C={native!r} Python={python!r}")
    if any(not row["tokens"] for row in native):
        raise RuntimeError("KibbleLM did not generate a continuation")
    reopened = native_result(link, PROMPTS[0])
    if comparable(reopened) != comparable(native[0]):
        raise RuntimeError("C could not reproduce the result after stopping MicroPython")
    prompts = {prompt: {"c": c, "python": py}
               for prompt, c, py in zip(PROMPTS, native, python, strict=True)}
    return {
        "board": board, "seed": 210, "script_sha256": sha256(source.encode()).hexdigest(),
        "prompts": prompts,
        "invalid_prompt_and_seed_rejected": True,
        "stop_and_restart": True,
        "native_after_python": reopened,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--board", choices=BOARDS, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    source = SCRIPT.read_text(encoding="utf-8")
    with hkflash.open_runtime_serial(hkflash.require_serial(), args.port, 115200) as link:
        report = check(link, args.board, source)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"[OK] {args.board}: both stories match, Python stopped, C reopened")


if __name__ == "__main__":
    main()
