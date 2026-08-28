#!/usr/bin/env python3
"""Check generated app composition and built-profile isolation."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

import app_composition
import build_firmware


ROOT = Path(__file__).resolve().parents[1]


def _tool(toolchain: Path, name: str) -> Path:
    for candidate in (toolchain / f"riscv64-unknown-elf-{name}.exe",
                      toolchain / f"riscv64-unknown-elf-{name}"):
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"toolchain utility is missing: {name}")


def validate_source() -> list[str]:
    failures = app_composition.freshness_failures()
    if (ROOT / "firmware" / "app_requirements.toml").exists():
        failures.append("firmware/app_requirements.toml remains a composition source")
    return failures


def validate_build(board_id: str) -> list[str]:
    failures = validate_source()
    composition_path = ROOT / "build" / board_id / "composition.json"
    if not composition_path.is_file():
        return failures + [f"missing build composition: {composition_path}"]
    composition = json.loads(composition_path.read_text(encoding="utf-8"))
    disabled = set(composition["disabled_apps"])
    model = app_composition.load_model()
    apps = app_composition.app_map(model)
    unknown = disabled - set(apps)
    if unknown:
        failures.append(f"build composition contains unknown disabled apps: {sorted(unknown)}")

    sdk = build_firmware.find_sdk()
    if sdk is None:
        return failures + ["Kendryte SDK stage is unavailable"]
    stage = sdk / "src" / str(build_firmware.TARGETS["full"]["project"])
    if not stage.is_dir():
        return failures + [f"firmware stage is unavailable: {stage}"]
    config_path = stage / "hk_config.h"
    config = config_path.read_text(encoding="utf-8") if config_path.is_file() else ""
    project_path = stage / "project.cmake"
    project = project_path.read_text(encoding="utf-8") if project_path.is_file() else ""
    include_block = (
        project.split("target_include_directories(${PROJECT_NAME} PRIVATE", 1)[1]
        .split(")", 1)[0]
        if "target_include_directories(${PROJECT_NAME} PRIVATE" in project
        else ""
    )
    allowed_includes, forbidden_includes = build_firmware.app_include_sets(stage, disabled)
    for include in allowed_includes:
        encoded = build_firmware.cmake_path(include)
        if encoded not in include_block:
            failures.append(f"manifest app include root is absent from CMake: {encoded}")
    for include in forbidden_includes:
        encoded = build_firmware.cmake_path(include)
        if encoded in include_block:
            failures.append(f"undeclared app header directory is an include root: {encoded}")
        if encoded not in project:
            failures.append(f"implicit app header directory is not pruned: {encoded}")

    for app_id, app in apps.items():
        relative = Path("firmware/src/apps") / str(app["directory"])
        staged = stage / relative
        definition = app_composition.enable_definition(app_id)
        expected_value = 0 if app_id in disabled else 1
        if f"#define {definition} {expected_value}" not in config:
            failures.append(f"{app_id}: generated enable definition is not {expected_value}")
        if app_id in disabled:
            if staged.exists():
                failures.append(f"{app_id}: disabled private source directory remains staged")
        else:
            for source in app["sources"]:
                if not (staged / source).is_file():
                    failures.append(f"{app_id}: enabled source is absent from stage: {source}")
    if list(stage.rglob("app.toml")):
        failures.append("runtime firmware stage contains build-time app.toml")

    if "qr-camera" in disabled:
        for name in ("quirc.c", "decode.c", "identify.c", "version_db.c"):
            if (stage / name).exists():
                failures.append(f"qr-camera: disabled third-party input remains: {name}")
    if "apriltag" in disabled:
        for name in ("apriltag.c", "tag36h11.c", "apriltag_quad_thresh.c"):
            if (stage / name).exists():
                failures.append(f"apriltag: disabled third-party input remains: {name}")
    if "micropython" in disabled:
        project = (stage / "project.cmake").read_text(encoding="utf-8")
        for token in ("HACKYLENS_MICROPYTHON_SOURCES", "lfs.c", "micropython_embed"):
            if token in project:
                failures.append(f"micropython: disabled dependency remains in project: {token}")

    elf = ROOT / "build" / board_id / "sdk-full" / "hackylens_full"
    toolchain = build_firmware.find_toolchain_bin()
    if not elf.is_file() or toolchain is None:
        return failures + ["linked firmware ELF/toolchain is unavailable"]
    symbols = subprocess.check_output(
        [str(_tool(toolchain, "nm")), "-g", str(elf)], text=True, errors="replace"
    )
    sections = subprocess.check_output(
        [str(_tool(toolchain, "objdump")), "-h", str(elf)], text=True, errors="replace"
    )
    section_names = {
        match.group(1) for match in re.finditer(r"^\s*\d+\s+(\S+)", sections, re.MULTILINE)
    }
    for app_id in disabled:
        entry = str(apps[app_id]["entry"])
        if re.search(rf"\b{re.escape(entry)}$", symbols, re.MULTILINE):
            failures.append(f"{app_id}: disabled entry symbol remains linked: {entry}")
        token = app_id.replace("-", "_")
        leaked_sections = sorted(name for name in section_names if token in name)
        if leaked_sections:
            failures.append(f"{app_id}: disabled resource section remains: {leaked_sections}")
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-build", metavar="BOARD")
    args = parser.parse_args(argv)
    try:
        failures = validate_build(args.verify_build) if args.verify_build else validate_source()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        failures = [str(exc)]
    if failures:
        for failure in failures:
            print(f"[ERR] {failure}", file=sys.stderr)
        return 1
    print("[OK] manifest-driven app composition guard passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
