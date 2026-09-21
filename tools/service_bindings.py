#!/usr/bin/env python3
"""Select typed hardware bindings from board resources; no runtime inventory."""
from __future__ import annotations
from dataclasses import dataclass
import hashlib
from pathlib import Path
import re
from board_contract import Board
import app_composition
from app_manifest import canonical_json_bytes, OPTIONAL_FALLBACKS, FIRMWARE_SERVICES

ROOT = Path(__file__).resolve().parents[1]

@dataclass(frozen=True)
class Binding:
    name: str
    resources: tuple[str, ...]
    routes: tuple[str, ...]
    c_type: str

    @property
    def provider_source(self) -> str:
        return f"platforms/k210/capabilities/{self.name.replace('-', '_')}_adapter.c"

    @property
    def symbol(self) -> str:
        return f"hk_{self.name.replace('-', '_')}_binding"

BINDINGS = (
    Binding('time', ('processor',), (), 'hk_time_t'),
    Binding('input', ('buttons',), ('button-left', 'button-ok', 'button-right', 'button-back'), 'hk_input_t'),
    Binding('display', ('display',), ('lcd-backlight', 'lcd-dc', 'lcd-chip-select', 'lcd-clock', 'lcd-reset', 'lcd-mosi'), 'hk_display_service_t'),
    Binding('external-link', ('external-uart', 'external-i2c'), ('external-uart-rx', 'external-uart-tx', 'external-i2c-clock', 'external-i2c-data'), 'hk_external_link_service_t'),
    Binding('lights', ('lights',), ('lcd-backlight', 'illumination', 'rgb-red', 'rgb-green', 'rgb-blue'), 'hk_lights_service_t'),
 )

@dataclass(frozen=True)
class Selection:
    board: Board
    bindings: tuple[Binding, ...]
    absences: tuple[dict[str, object], ...]
    disabled_apps: frozenset[str]
    disabled_capabilities: frozenset[str]
    exclusions: tuple[dict[str, object], ...]
    optional_fallbacks: tuple[dict[str, object], ...]

def select(board: Board, disabled_apps: set[str], required_apps: set[str],
           disabled_services: set[str], *, model=None, root: Path = ROOT) -> Selection:
    if board.platform != "kendryte-k210":
        raise ValueError(f"unsupported binding platform: {board.platform}")
    model = app_composition.load_model() if model is None else model
    apps = app_composition.app_map(model)
    unknown = (disabled_apps | required_apps) - set(apps)
    if unknown:
        raise ValueError(f"unknown app(s): {sorted(unknown)}")
    if disabled_apps & required_apps:
        raise ValueError("app is both disabled and required")
    # Preserve the diagnostic CLI spelling in existing build attestations.
    disabled = {name.removeprefix("hackylens.cap.") for name in disabled_services}
    if disabled - {item.name for item in BINDINGS}:
        raise ValueError(f"unknown disabled service: {sorted(disabled)}")
    present, supported = board.present_kinds(), board.driver_supported_kinds()
    routes = {item["id"] for item in board.selected_routes()}
    bindings, absences = [], []
    for item in BINDINGS:
        reasons = [
            ("provider-excluded", ["disabled-by-build"] if item.name in disabled else []),
            ("resource-absent", sorted(set(item.resources) - present)),
            ("driver-unsupported", sorted(set(item.resources) - supported)),
            ("route-unavailable", sorted(set(item.routes) - routes)),
        ]
        failure = next(((code, detail) for code, detail in reasons if detail), None)
        if failure:
            absences.append({"id": item.name, "code": failure[0], "detail": failure[1]})
            continue
        source = (root / item.provider_source).read_text(encoding="utf-8")
        if not re.search(rf"\bconst\s+{item.c_type}\s+{item.symbol}\s*=\s*\{{", source):
            raise ValueError(f"{item.provider_source}: immutable binding definition is missing")
        bindings.append(item)
    available = {item.name for item in bindings}
    if "time" not in available:
        raise ValueError("runtime requires the Time binding")
    excluded = set(disabled_apps)
    exclusions, fallbacks = [], []
    firmware = (set(supported) & FIRMWARE_SERVICES) | {"settings"}
    for name, app in sorted(apps.items()):
        if name in excluded:
            continue
        missing = sorted(set(app["requires"]) - available - firmware)
        if missing:
            if name in required_apps:
                raise ValueError(f"required app {name!r} is unavailable on {board.id}: {missing}")
            excluded.add(name)
            exclusions.append({"app": name, "code": "driver-unsupported", "missing": missing})
            continue
        for optional in app["optional"]:
            if optional not in available:
                fallbacks.append({"consumer": name, "id": optional,
                                  "fallback": OPTIONAL_FALLBACKS[optional]})
    return Selection(board, tuple(bindings), tuple(absences), frozenset(excluded),
                     frozenset(disabled_services), tuple(exclusions), tuple(fallbacks))

def generated_c(selection: Selection) -> str:
    lines = ["/* Absent typed service bindings selected by the board build. */"]
    available = {item.name for item in selection.bindings}
    for item in BINDINGS:
        if item.name not in available:
            lines += [f'#include "{item.name.replace("-", "_")}_provider.h"',
                      f"const {item.c_type} {item.symbol} = {{0}};"]
    return "\n".join(lines) + "\n"

def write_generated_c(selection: Selection, stage: Path) -> Path:
    path = stage / "firmware/src/capabilities/absent_bindings.c"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(generated_c(selection), encoding="utf-8", newline="\n")
    return path

def write_artifacts(selection: Selection, output_dir: Path) -> tuple[Path, Path]:
    # Existing attestation field/file names remain readable by flash tooling.
    output_dir.mkdir(parents=True, exist_ok=True)
    bindings = {"board": selection.board.id,
                "bindings": [item.provider_source for item in selection.bindings],
                "absences": list(selection.absences)}
    path = output_dir / "capabilities.json"
    path.write_bytes(canonical_json_bytes(bindings))
    composition = output_dir / "composition.json"
    composition.write_bytes(canonical_json_bytes({
        "board": selection.board.id, "disabled_apps": sorted(selection.disabled_apps),
        "disabled_capabilities": sorted(selection.disabled_capabilities),
        "exclusions": list(selection.exclusions),
        "capabilities_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }))
    return path, composition
