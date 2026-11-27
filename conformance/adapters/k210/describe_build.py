"""Record the declarations and verified identity of K210 cross-build artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
from math import prod
from pathlib import Path
import re
import subprocess
import sys

from conformance.adapters.k210.collect import ROOT, sha256


DECLARED_LIMITS = {
    "native_teardown_us": ("firmware/src/app_runtime/runtime_private.h", "HK_APP_RUNTIME_TEARDOWN_BUDGET_US"),
    "time_interval_us": ("firmware/include/hackylens/capability/time.h", "HK_TIME_MAX_SLEEP_US"),
    "time_check_slice_us": ("firmware/include/hackylens/capability/time.h", "HK_TIME_CANCEL_PROBE_MAX_US"),
    "script_heap_bytes": ("firmware/src/services/micropython_runtime.h", "MICROPYTHON_RUNTIME_HEAP_BYTES"),
    "script_source_bytes": ("firmware/src/services/micropython_runtime.h", "MICROPYTHON_RUNTIME_SOURCE_MAX"),
    "script_stack_bytes": ("firmware/src/services/micropython_runtime.c", "MICROPYTHON_STACK_LIMIT_BYTES"),
    "script_output_ring_bytes": ("firmware/src/services/micropython_runtime.c", "MICROPYTHON_OUTPUT_BYTES"),
    "script_default_limit_ms": ("firmware/src/services/micropython_runtime.h", "MICROPYTHON_RUNTIME_DEFAULT_LIMIT_MS"),
    "script_max_limit_ms": ("firmware/src/services/micropython_runtime.h", "MICROPYTHON_RUNTIME_MAX_LIMIT_MS"),
    "script_ordinary_request_limit_us": (
        "firmware/src/adapters/micropython/micropython_capability_bridge.c", "MICROPYTHON_BINDING_RPC_TIMEOUT_US",
    ),
    "stop_watchdog_grace_us": ("firmware/src/services/micropython_runtime.c", "MICROPYTHON_WATCHDOG_STOP_GRACE_US"),
    "expiry_watchdog_grace_us": ("firmware/src/services/micropython_runtime.c", "MICROPYTHON_WATCHDOG_DEADLINE_GRACE_US"),
    "watchdog_requested_timeout_ms": ("firmware/src/services/micropython_runtime.c", "MICROPYTHON_WATCHDOG_TIMEOUT_MS"),
}


def match(pattern: str, text: str, field: str) -> str:
    found = re.search(pattern, text, re.MULTILINE)
    if not found:
        raise ValueError(f"missing declaration for {field}")
    return found.group(1)


def integer_define(checkout: Path, filename: str, name: str) -> int:
    expression = match(rf"^#define {name}\s+([^\r\n]+)",
                       (checkout / filename).read_text(encoding="utf-8"), name)
    expression = re.sub(r"UINT(?:32|64)_C\((\d+)\)", r"\1", expression)
    expression = re.sub(r"(?<=\d)U(?:LL)?\b", "", expression).strip().strip("()")
    if not re.fullmatch(r"\d+(?:\s*\*\s*\d+)*", expression):
        raise ValueError(f"expected an unsigned integer or product of integers for {name}")
    return prod(int(part) for part in expression.split("*"))


def compiler_metadata(directory: Path) -> dict:
    compiler_records = list((directory / "CMakeFiles").glob("*/CMakeCCompiler.cmake"))
    if len(compiler_records) != 1:
        raise ValueError(f"expected one CMake compiler record under {directory}")
    compiler = compiler_records[0].read_text(encoding="utf-8")
    cache = (directory / "CMakeCache.txt").read_text(encoding="utf-8")
    flags = match(r"^CMAKE_C_FLAGS:STRING=([^\r\n]+)", cache, "C flags")
    return {
        "id": match(r'^set\(CMAKE_C_COMPILER_ID "([^"]+)"\)', compiler, "compiler ID"),
        "version": match(r'^set\(CMAKE_C_COMPILER_VERSION "([^"]+)"\)', compiler, "compiler version"),
        "isa": match(r"-march=([^\s]+)", flags, "target ISA"),
        "abi": match(r"-mabi=([^\s]+)", flags, "target ABI"),
        "cmake_version": compiler_records[0].parent.name,
    }


def describe(checkout: Path, build: Path) -> dict:
    sys.path.insert(0, str(ROOT / "tools"))
    try:
        from app_composition import load_model
        from board_contract import board_ids, load_board
        from firmware_attestation import read_and_validate
    finally:
        sys.path.pop(0)

    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=checkout, text=True).strip()
    trees = ["firmware", "platforms", "sdk", "boards"]
    changes = subprocess.check_output(["git", "diff", "--name-only", "HEAD", "--", *trees],
                                      cwd=checkout, text=True).splitlines()
    untracked = subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard", "--", *trees],
                                        cwd=checkout, text=True).splitlines()
    staged = build / "kendryte-sdk/src/hackylens_full"
    overrides = {}
    for name in sorted(set(changes + untracked)):
        source_hash = sha256(checkout / name)
        if sha256(staged / name) != source_hash:
            raise ValueError(f"staged source differs from the checkout for {name}")
        overrides[name] = source_hash
    limits = {field: {"value": integer_define(checkout, filename, macro), "source": filename, "macro": macro}
              for field, (filename, macro) in DECLARED_LIMITS.items()}
    native_limits = {entry["id"]: entry["limits"]
                     for entry in load_model(checkout / "firmware/src/apps")["apps"]}
    boards = []
    for identity in board_ids(checkout):
        board = load_board(identity, root=checkout)
        directory = build / identity
        image = directory / "hackylens-full.bin"
        raw = directory / "hackylens-full.attestation.json"
        attestation = read_and_validate(raw, image, board, expected_target="full", require_release_qualified=True)
        capabilities_bytes = (directory / "capabilities.json").read_bytes()
        if hashlib.sha256(capabilities_bytes).hexdigest() != attestation["composition"]["capabilities_sha256"]:
            raise ValueError(f"capability record differs from the build attestation for {identity}")
        capabilities = json.loads(capabilities_bytes)
        if capabilities["board"] != identity:
            raise ValueError(f"capability record has the wrong board for {identity}")
        binding_versions = {}
        for provider in capabilities["bindings"]:
            capability = Path(provider).stem.removesuffix("_adapter").upper()
            contract = checkout / "docs/spec/capabilities" / f"{capability}.md"
            binding_versions[provider] = match(r"^version:\s+([^\s]+)", contract.read_text(encoding="utf-8"),
                                               str(contract.name))
        boards.append({"board_id": identity, "attestation": attestation, "capabilities": capabilities,
                       "binding_versions": binding_versions,
                       "native_descriptor_limits": {app: native_limits[app]
                                                    for app in attestation["composition"]["enabled_apps"]},
                       "compiler": compiler_metadata(directory / "sdk-full"),
                       "board_sha256": sha256(checkout / "boards" / identity / "board.toml")})
    return {
        "schema_version": 1,
        "specification": "0.1.0",
        "evidence_kind": "cross-build",
        "implementation_revision": revision,
        "firmware_overrides_sha256": overrides,
        "declared_profiles": ["Core", "Shared Script", "Recovery"],
        "execution_model": {"foreground_native_slots": 1, "services_core": 0, "script_executor_core": 1,
                            "script_request_slots": 1},
        "declared_limits": limits,
        "recovery_policy": {"trigger": "noncooperation after the applicable grace period",
                            "mechanism": "WDT1 reset", "scope": "device boot", "observed_on_hardware": False},
        "boards": boards,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path, help="source checkout used for the cross-build")
    parser.add_argument("destination", type=Path, help="JSON record to write")
    args = parser.parse_args()
    checkout = args.checkout.resolve()
    report = describe(checkout, checkout / "build")
    args.destination.parent.mkdir(parents=True, exist_ok=True)
    args.destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
