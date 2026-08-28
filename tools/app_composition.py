#!/usr/bin/env python3
"""Manifest-derived native app build composition."""

from __future__ import annotations

import hashlib
from pathlib import Path
import re
from typing import Any, Mapping

import app_registry
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
GENERATED_REGISTRY_ROOT = ROOT / "firmware" / "generated" / "app_registry"
GENERATED_REGISTRY_HEADER = GENERATED_REGISTRY_ROOT / "registry.h"
GENERATED_REGISTRY_SOURCE = GENERATED_REGISTRY_ROOT / "registry.c"
TRANSLATION_UNIT_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx"})


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

    manifest_directories = {
        Path(str(app["directory"])): app for app in model["apps"]
    }
    present_by_directory: dict[Path, set[str]] = {
        directory: set() for directory in manifest_directories
    }
    orphans: list[str] = []
    for path in manifest_root.rglob("*"):
        if not path.is_file() or path.suffix.casefold() not in TRANSLATION_UNIT_SUFFIXES:
            continue
        relative = path.relative_to(manifest_root)
        if "tests" in relative.parts:
            continue
        owners = [
            directory for directory in manifest_directories
            if relative.is_relative_to(directory)
        ]
        if not owners:
            orphans.append(relative.as_posix())
            continue
        owner = max(owners, key=lambda item: len(item.parts))
        present_by_directory[owner].add(relative.relative_to(owner).as_posix())
    if orphans:
        raise CompositionError(
            "orphan app translation units have no canonical manifest owner: "
            f"{sorted(orphans)}"
        )

    for app in model["apps"]:
        relative_directory = Path(str(app["directory"]))
        declared = set(app["sources"])
        present = present_by_directory[relative_directory]
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
    try:
        registry_header = app_registry.generated_header(model)
        registry_source = app_registry.generated_source(model, enable_definition)
    except app_registry.RegistryGenerationError as exc:
        raise CompositionError(str(exc)) from exc
    return {
        GENERATED_JSON: canonical_json_bytes(generated_document(model)),
        GENERATED_DEFAULTS: generated_defaults(model).encode("utf-8"),
        GENERATED_REGISTRY_HEADER: registry_header.encode("utf-8"),
        GENERATED_REGISTRY_SOURCE: registry_source.encode("utf-8"),
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
