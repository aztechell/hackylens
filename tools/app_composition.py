#!/usr/bin/env python3
"""Manifest-derived native app build composition."""

from __future__ import annotations

import hashlib
from pathlib import Path
import re
from typing import Any, Mapping

from app_manifest import (
    LEGACY_SERVICE_PREFIX,
    ManifestError,
    canonical_json_bytes,
    validate_tree,
)


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_ROOT = ROOT / "firmware" / "src" / "apps"
GENERATED_ROOT = ROOT / "firmware" / "generated" / "app_composition"
GENERATED_JSON = GENERATED_ROOT / "composition.json"
GENERATED_DEFAULTS = ROOT / "firmware" / "config" / "app_config_defaults.h"


class CompositionError(ValueError):
    """The canonical manifest model cannot form a safe build composition."""


def enable_definition(app_id: str) -> str:
    return "HK_ENABLE_APP_" + re.sub(r"[^A-Za-z0-9]", "_", app_id).upper()


def _legacy_requirements(app: Mapping[str, Any]) -> list[str]:
    requirements: list[str] = []
    for service in app["services"]:
        service_id = service["id"]
        if not service_id.startswith(LEGACY_SERVICE_PREFIX):
            continue
        if app["lifecycle"] != "legacy":
            raise CompositionError(
                f"{app['id']}: transitional legacy service requires lifecycle=legacy"
            )
        requirement = service_id[len(LEGACY_SERVICE_PREFIX):]
        if not requirement:
            raise CompositionError(f"{app['id']}: empty legacy service requirement")
        requirements.append(requirement)
    return sorted(requirements)


def load_model(manifest_root: Path = MANIFEST_ROOT) -> dict[str, Any]:
    try:
        model = validate_tree(manifest_root)
    except ManifestError as exc:
        raise CompositionError(str(exc)) from exc

    manifest_directories = {app["directory"] for app in model["apps"]}
    source_directories = {
        path.relative_to(manifest_root).as_posix()
        for path in manifest_root.iterdir()
        if path.is_dir() and any(path.rglob("*.c"))
    }
    if manifest_directories != source_directories:
        missing = sorted(source_directories - manifest_directories)
        extra = sorted(manifest_directories - source_directories)
        raise CompositionError(
            "manifest/app directory mismatch: "
            f"missing={missing}, extra={extra}"
        )

    for app in model["apps"]:
        directory = manifest_root / app["directory"]
        declared = set(app["sources"])
        present = {
            path.relative_to(directory).as_posix()
            for path in directory.rglob("*")
            if path.is_file() and path.suffix.casefold() in {".c", ".cc", ".cpp", ".cxx"}
            and "tests" not in path.relative_to(directory).parts
        }
        if declared != present:
            raise CompositionError(
                f"{app['id']}: manifest sources must exactly cover app translation units; "
                f"missing={sorted(present - declared)}, extra={sorted(declared - present)}"
            )
        _legacy_requirements(app)
    return model


def generated_document(
    model: Mapping[str, Any],
    *,
    manifest_root_relative: str = "firmware/src/apps",
) -> dict[str, Any]:
    apps: list[dict[str, Any]] = []
    for app in model["apps"]:
        base = Path(manifest_root_relative) / app["directory"]
        sources = sorted((base / path).as_posix() for path in app["sources"])
        include_paths = [base.as_posix()]
        include_paths.extend(
            (base / path).as_posix() for path in app["private_includes"]
        )
        apps.append({
            "id": app["id"],
            "directory": base.as_posix(),
            "enable_definition": enable_definition(app["id"]),
            "sources": sources,
            "private_includes": sorted(set(include_paths)),
            "legacy_requirements": _legacy_requirements(app),
            "capabilities": app["capabilities"],
            "services": app["services"],
        })
    model_bytes = canonical_json_bytes(model)
    return {
        "schema": 1,
        "manifest_schema": model["schema"],
        "manifest_model_sha256": hashlib.sha256(model_bytes).hexdigest(),
        "apps": apps,
    }


def generated_defaults(model: Mapping[str, Any]) -> str:
    lines = [
        "#ifndef HK_GENERATED_APP_CONFIG_DEFAULTS_H",
        "#define HK_GENERATED_APP_CONFIG_DEFAULTS_H",
    ]
    for app in model["apps"]:
        definition = enable_definition(app["id"])
        lines.extend([
            f"#ifndef {definition}",
            f"#define {definition} 1",
            "#endif",
        ])
    lines.extend(["#endif", ""])
    return "\n".join(lines)


def generated_files(
    manifest_root: Path = MANIFEST_ROOT,
) -> dict[Path, bytes]:
    model = load_model(manifest_root)
    return {
        GENERATED_JSON: canonical_json_bytes(generated_document(model)),
        GENERATED_DEFAULTS: generated_defaults(model).encode("utf-8"),
    }


def write_generated(manifest_root: Path = MANIFEST_ROOT) -> None:
    for path, content in generated_files(manifest_root).items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)


def freshness_failures(manifest_root: Path = MANIFEST_ROOT) -> list[str]:
    failures: list[str] = []
    for path, expected in generated_files(manifest_root).items():
        if not path.is_file():
            failures.append(f"{path.relative_to(ROOT).as_posix()}: generated file is missing")
        elif path.read_bytes() != expected:
            failures.append(f"{path.relative_to(ROOT).as_posix()}: generated file is stale")
    return failures


def app_map(model: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    return {app["id"]: app for app in model["apps"]}
