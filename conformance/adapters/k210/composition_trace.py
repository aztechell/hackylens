"""Translate composition stimuli into calls to the production build tools."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / "conformance/fixtures/composition.json"
APP_FIXTURES = ROOT / "tests/fixtures/app_manifests/valid"
CORE = ROOT / "firmware/src/core"
REGISTRY_FILES = ("hk_app_registry.c", "hk_app_registry.h", "hk_app.h", "hk_events.h")
REGISTRY_TRACE = Path(__file__).with_name("registry_trace.c")


def collect(traces: Path, compiler: str, flags: list[str]) -> dict[str, Path]:
    sys.path.insert(0, str(ROOT / "tools"))
    try:
        import app_composition
        import board_contract
        import service_bindings
    finally:
        sys.path.pop(0)

    board = board_contract.load_board("huskylens-sen0305")
    cases = json.loads(FIXTURE.read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory(prefix="hackylens-ct02-") as directory:
        stage = Path(directory)
        for case in cases:
            observations = []
            for variant in case["variants"]:
                apps = stage / case["id"] / variant
                shutil.copytree(APP_FIXTURES, apps)
                prepare_manifest(apps, variant)
                try:
                    model = app_composition.load_model(apps)
                except app_composition.CompositionError:
                    observations.append({"outcome": "rejected", "phase": "manifest"})
                    continue
                disabled_apps = {"alpha-tool"} if variant == "disable-alpha" else set()
                required_apps = {"alpha-tool"} if variant == "require-alpha-without-input" else set()
                service_exclusion = {
                    "no-input": {"input"}, "require-alpha-without-input": {"input"},
                    "no-display": {"display"},
                }
                try:
                    selection = service_bindings.select(board, disabled_apps, required_apps,
                                                        service_exclusion.get(variant, set()), model=model)
                except ValueError:
                    observations.append({"outcome": "rejected", "phase": "admission"})
                    continue
                generated = app_composition.generated_registry_files(model)
                definitions = {app_composition.enable_definition(app["id"]):
                               int(app["id"] not in selection.disabled_apps) for app in model["apps"]}
                registry = observe_registry(apps / "compiled", generated, definitions, compiler, flags)
                observations.append({
                    "outcome": "accepted",
                    **registry,
                    "fallbacks": [{"app": fallback["consumer"], "service": fallback["id"],
                                   "behavior": fallback["fallback"]} for fallback in selection.optional_fallbacks],
                    "registry_sha256": hashlib.sha256(generated["registry.c"]).hexdigest(),
                })
            (traces / f"{case['id']}.jsonl").write_text(
                "".join(json.dumps({"event": "composition", **event}) + "\n" for event in observations),
                encoding="utf-8", newline="\n",
            )
    inputs = [Path(__file__), REGISTRY_TRACE, *(CORE / name for name in REGISTRY_FILES),
              *(Path(module.__file__) for module in
                                (app_composition, board_contract, service_bindings)),
              ROOT / "tools/app_manifest.py", ROOT / "tools/app_registry.py",
              *sorted((ROOT / "boards/huskylens-sen0305").glob("*.toml")),
              *sorted(path for path in APP_FIXTURES.rglob("*") if path.is_file())]
    return {path.relative_to(ROOT).as_posix(): path for path in inputs}


def observe_registry(stage: Path, generated: dict[str, bytes], definitions: dict[str, int],
                     compiler: str, flags: list[str]) -> dict:
    core = stage / "firmware/src/core"
    registry = stage / "firmware/generated/app_registry"
    core.mkdir(parents=True)
    registry.mkdir(parents=True)
    for name in REGISTRY_FILES:
        shutil.copyfile(CORE / name, core / name)
    for name, content in generated.items():
        (registry / name).write_bytes(content)
    (stage / "hk_config.h").write_text(
        "".join(f"#define {symbol} {enabled}\n" for symbol, enabled in definitions.items()),
        encoding="utf-8", newline="\n",
    )
    executable = stage / ("registry.exe" if os.name == "nt" else "registry")
    subprocess.run([
        compiler, *flags, f"-I{stage}", f"-I{ROOT / 'sdk/include'}", f"-I{ROOT / 'firmware/include'}",
        str(registry / "registry.c"), str(core / "hk_app_registry.c"), str(REGISTRY_TRACE),
        "-o", str(executable),
    ], cwd=ROOT, check=True, timeout=30)
    result = subprocess.run([str(executable)], cwd=ROOT, check=True, capture_output=True, timeout=5)
    return json.loads(result.stdout)


def prepare_manifest(apps: Path, variant: str) -> None:
    peer = apps / "zeta-legacy/app.toml"
    with peer.open("a", encoding="utf-8") as stream:
        peer_id = 42 if variant == "duplicate-id" else 43
        stream.write(f"\nautostart_id = {peer_id}\n")
    if variant == "invalid-id":
        path = apps / "alpha-tool/app.toml"
        original = path.read_text(encoding="utf-8")
        path.write_text(original.replace('id = "alpha-tool"', 'id = "Invalid ID"', 1), encoding="utf-8")
    elif variant == "reordered":
        for path in apps.glob("*/app.toml"):
            lines = path.read_text(encoding="utf-8").splitlines()
            lines = [line.replace('["input", "settings"]', '["settings", "input"]') for line in lines]
            path.write_text("\n".join(reversed(lines)) + "\n", encoding="utf-8")
