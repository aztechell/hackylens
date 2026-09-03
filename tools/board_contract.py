#!/usr/bin/env python3
"""Load and validate K210 board.toml for build, package, and hkflash."""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path
import re
import tomllib
from typing import Any, Mapping


ROOT = Path(__file__).resolve().parents[1]
BOARDS_DIR = ROOT / "boards"
BOARD_ID_RE = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
IDENTIFIER_RE = BOARD_ID_RE
MACRO_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
VID_RE = re.compile(r"^[0-9A-F]{4}$")
PARTITION_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
PLATFORM_ID = "kendryte-k210"
RUNTIME_PROFILE = "hackylens-full"

ROOT_FIELDS = {
    "schema", "id", "platform", "support", "releaseable",
    "runtime_profile", "available", "present", "routes", "defaults",
    "flash", "partitions", "programming",
}
ROUTE_FIELDS = {
    "id", "macro", "pin", "function", "peripheral", "runtime_mux_group",
}
PROGRAMMING_FIELDS = {
    "supported", "reset_profile", "reboot_profile", "isp_stub",
    "flash_type", "flash_mode", "boot_baud", "flash_baud",
    "reset_attempts", "usb_detection",
}
USB_FIELDS = {"mode", "vids"}
FLASH_FIELDS = {
    "address_bytes", "minimum_capacity", "maximum_capacity",
    "erase_size", "program_size",
}
PARTITION_FIELDS = {
    "name", "offset", "size", "required_capacity", "runtime_writable",
}
DEFAULT_FIELDS = {
    "lcd_width", "lcd_height", "lcd_spi", "lcd_chip_select", "lcd_spi_hz",
    "gpiohs_lcd_reset", "gpiohs_lcd_dc", "gpiohs_button_left",
    "gpiohs_button_ok", "gpiohs_button_right", "gpiohs_button_back",
    "gpiohs_sd_chip_select", "sd_spi", "sd_chip_select", "flash_spi",
    "flash_chip_select", "led_pwm_device", "led_pwm_channel",
    "rgb_pwm_device", "rgb_pwm_channel0", "rgb_pwm_channel1",
    "rgb_pwm_channel2", "screen_pwm_device", "screen_pwm_channel",
    "pwm_frequency_hz", "camera_sccb_address", "camera_xclk_hz",
    "camera_sccb_hz", "camera_max_width", "camera_max_height",
}
GPIOHS_DEFAULT_ROUTES = {
    "gpiohs_lcd_reset": "IO_LCD_RST",
    "gpiohs_lcd_dc": "IO_LCD_DC_OR_AUX",
    "gpiohs_button_left": "IO_BTN_LEFT",
    "gpiohs_button_ok": "IO_BTN_OK",
    "gpiohs_button_right": "IO_BTN_RIGHT",
    "gpiohs_button_back": "IO_BTN_BACK",
    "gpiohs_sd_chip_select": "IO_SD_CS",
}
SPI_DEFAULT_ROUTES = {
    "lcd_spi": "IO_LCD_SCLK",
    "sd_spi": "IO_SD_SCLK",
}
SPI_CHIP_SELECT_DEFAULT_ROUTES = {
    "lcd_chip_select": "IO_LCD_CS",
}
DEFAULT_RANGES = {
    "lcd_width": (1, 640),
    "lcd_height": (1, 480),
    "lcd_spi": (0, 3),
    "lcd_chip_select": (0, 3),
    "lcd_spi_hz": (1, 100_000_000),
    "gpiohs_lcd_reset": (0, 31),
    "gpiohs_lcd_dc": (0, 31),
    "gpiohs_button_left": (0, 31),
    "gpiohs_button_ok": (0, 31),
    "gpiohs_button_right": (0, 31),
    "gpiohs_button_back": (0, 31),
    "gpiohs_sd_chip_select": (0, 31),
    "sd_spi": (0, 3),
    "sd_chip_select": (0, 3),
    "flash_spi": (0, 3),
    "flash_chip_select": (0, 3),
    "led_pwm_device": (0, 2),
    "led_pwm_channel": (0, 3),
    "rgb_pwm_device": (0, 2),
    "rgb_pwm_channel0": (0, 3),
    "rgb_pwm_channel1": (0, 3),
    "rgb_pwm_channel2": (0, 3),
    "screen_pwm_device": (0, 2),
    "screen_pwm_channel": (0, 3),
    "pwm_frequency_hz": (1, 100_000_000),
    "camera_sccb_address": (1, 0x7F),
    "camera_xclk_hz": (1, 50_000_000),
    "camera_sccb_hz": (1, 1_000_000),
    "camera_max_width": (1, 640),
    "camera_max_height": (1, 480),
}
PWM_DEFAULT_ROUTES = (
    ("led_pwm_device", "led_pwm_channel", "IO_LED_ILLUM"),
    ("rgb_pwm_device", "rgb_pwm_channel0", "IO_RGB_PWM0"),
    ("rgb_pwm_device", "rgb_pwm_channel1", "IO_RGB_PWM1"),
    ("rgb_pwm_device", "rgb_pwm_channel2", "IO_RGB_PWM2"),
    ("screen_pwm_device", "screen_pwm_channel", "IO_LCD_BL"),
)
K210_FIXED_DEFAULTS = {
    "flash_spi": 3,
    "flash_chip_select": 0,
}
SERVICE_KINDS = {
    "processor", "internal-flash", "display", "camera", "buttons",
    "lights", "sd-card", "external-uart", "external-i2c",
}
RUNTIME_REQUIRED_SERVICES = frozenset(SERVICE_KINDS)
PREPARE_CALLBACKS = {
    "display": "display_prepare",
    "camera": "camera_prepare",
    "buttons": "buttons_prepare",
    "lights": "lights_prepare",
    "internal-flash": "internal_flash_prepare",
    "sd-card": "sd_prepare",
    "external-uart": "external_uart_prepare",
    "external-i2c": "external_i2c_prepare",
}
RESET_PROFILES = {"huskylens-uploader", "manual"}
REBOOT_PROFILES = {"uploader-normal"}
USB_DETECTION_MODES = {"vid-any", "explicit-port"}
FLASH_TYPES = {"spi0"}
FLASH_MODES = {"qio", "dio"}
FPIOA_FUNCTIONS = {
    "timer0-toggle1": ("timer0", "FUNC_TIMER0_TOGGLE1"),
    "timer2-toggle1": ("timer2", "FUNC_TIMER2_TOGGLE1"),
    "timer2-toggle2": ("timer2", "FUNC_TIMER2_TOGGLE2"),
    "timer2-toggle3": ("timer2", "FUNC_TIMER2_TOGGLE3"),
    "timer2-toggle4": ("timer2", "FUNC_TIMER2_TOGGLE4"),
    "gpiohs2": ("gpiohs", "FUNC_GPIOHS2"),
    "gpiohs3": ("gpiohs", "FUNC_GPIOHS3"),
    "gpiohs4": ("gpiohs", "FUNC_GPIOHS4"),
    "gpiohs5": ("gpiohs", "FUNC_GPIOHS5"),
    "gpiohs6": ("gpiohs", "FUNC_GPIOHS6"),
    "gpiohs14": ("gpiohs", "FUNC_GPIOHS14"),
    "gpiohs15": ("gpiohs", "FUNC_GPIOHS15"),
    "spi0-ss3": ("spi0", "FUNC_SPI0_SS3"),
    "spi0-sclk": ("spi0", "FUNC_SPI0_SCLK"),
    "spi0-d0": ("spi0", "FUNC_SPI0_D0"),
    "spi1-sclk": ("spi1", "FUNC_SPI1_SCLK"),
    "spi1-d0": ("spi1", "FUNC_SPI1_D0"),
    "spi1-d1": ("spi1", "FUNC_SPI1_D1"),
    "cmos-pclk": ("dvp", "FUNC_CMOS_PCLK"),
    "cmos-xclk": ("dvp", "FUNC_CMOS_XCLK"),
    "cmos-href": ("dvp", "FUNC_CMOS_HREF"),
    "cmos-pwdn": ("dvp", "FUNC_CMOS_PWDN"),
    "cmos-vsync": ("dvp", "FUNC_CMOS_VSYNC"),
    "cmos-rst": ("dvp", "FUNC_CMOS_RST"),
    "sccb-sclk": ("sccb", "FUNC_SCCB_SCLK"),
    "sccb-sda": ("sccb", "FUNC_SCCB_SDA"),
    "uart1-rx": ("uart1", "FUNC_UART1_RX"),
    "uart1-tx": ("uart1", "FUNC_UART1_TX"),
    "i2c0-sclk": ("i2c0", "FUNC_I2C0_SCLK"),
    "i2c0-sda": ("i2c0", "FUNC_I2C0_SDA"),
}
ROUTE_ROLES = {
    "IO_LCD_BL": ("timer0-toggle1", "timer0"),
    "IO_LCD_DC_OR_AUX": ("gpiohs15", "gpiohs"),
    "IO_LCD_CS": ("spi0-ss3", "spi0"),
    "IO_LCD_SCLK": ("spi0-sclk", "spi0"),
    "IO_LCD_RST": ("gpiohs14", "gpiohs"),
    "IO_LCD_MOSI": ("spi0-d0", "spi0"),
    "IO_LED_ILLUM": ("timer2-toggle4", "timer2"),
    "IO_RGB_PWM0": ("timer2-toggle1", "timer2"),
    "IO_RGB_PWM1": ("timer2-toggle2", "timer2"),
    "IO_RGB_PWM2": ("timer2-toggle3", "timer2"),
    "IO_BTN_LEFT": ("gpiohs2", "gpiohs"),
    "IO_BTN_OK": ("gpiohs3", "gpiohs"),
    "IO_BTN_RIGHT": ("gpiohs4", "gpiohs"),
    "IO_BTN_BACK": ("gpiohs5", "gpiohs"),
    "IO_CAM_PCLK": ("cmos-pclk", "dvp"),
    "IO_CAM_XCLK": ("cmos-xclk", "dvp"),
    "IO_CAM_HREF": ("cmos-href", "dvp"),
    "IO_CAM_PWDN": ("cmos-pwdn", "dvp"),
    "IO_CAM_VSYNC": ("cmos-vsync", "dvp"),
    "IO_CAM_RST": ("cmos-rst", "dvp"),
    "IO_CAM_SCCB_SCLK": ("sccb-sclk", "sccb"),
    "IO_CAM_SCCB_SDA": ("sccb-sda", "sccb"),
    "IO_SD_SCLK": ("spi1-sclk", "spi1"),
    "IO_SD_D0": ("spi1-d0", "spi1"),
    "IO_SD_D1": ("spi1-d1", "spi1"),
    "IO_SD_CS": ("gpiohs6", "gpiohs"),
    "IO_EXTERNAL_UART_R": ("uart1-rx", "uart1"),
    "IO_EXTERNAL_UART_T": ("uart1-tx", "uart1"),
    "IO_EXTERNAL_I2C_R": ("i2c0-sclk", "i2c0"),
    "IO_EXTERNAL_I2C_T": ("i2c0-sda", "i2c0"),
}
LEGACY_RUNTIME_MUX = {
    "external-four-pin-mode": frozenset({
        ("IO_EXTERNAL_UART_R", "uart1-rx", 34, "uart1"),
        ("IO_EXTERNAL_UART_T", "uart1-tx", 35, "uart1"),
        ("IO_EXTERNAL_I2C_R", "i2c0-sclk", 34, "i2c0"),
        ("IO_EXTERNAL_I2C_T", "i2c0-sda", 35, "i2c0"),
    }),
}


class ContractError(ValueError):
    """A board descriptor contract violation."""


def _load_toml(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as source:
            value = tomllib.load(source)
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise ContractError(f"cannot read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ContractError(f"{path}: root must be a table")
    return value


def _strict(table: Mapping[str, Any], allowed: set[str], field: str) -> None:
    unknown = sorted(set(table) - allowed)
    if unknown:
        raise ContractError(f"{field}: unknown field(s): {', '.join(unknown)}")


def _string(value: Any, field: str, *, pattern: re.Pattern[str] | None = None) -> str:
    if not isinstance(value, str) or not value:
        raise ContractError(f"{field}: expected non-empty string")
    if pattern is not None and pattern.fullmatch(value) is None:
        raise ContractError(f"{field}: invalid identifier {value!r}")
    return value


def _boolean(value: Any, field: str) -> bool:
    if not isinstance(value, bool):
        raise ContractError(f"{field}: expected boolean")
    return value


def _integer(value: Any, field: str, *, minimum: int = 0,
             maximum: int = 0xFFFFFFFF) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ContractError(f"{field}: expected integer")
    if value < minimum or value > maximum:
        raise ContractError(f"{field}: outside {minimum}..{maximum}")
    return value


def _string_list(value: Any, field: str) -> list[str]:
    if not isinstance(value, list) or any(not isinstance(item, str) or not item for item in value):
        raise ContractError(f"{field}: expected an array of non-empty strings")
    if len(value) != len(set(value)):
        raise ContractError(f"{field}: duplicate value")
    return value


def function_macro(function_id: str) -> str:
    try:
        return FPIOA_FUNCTIONS[function_id][1]
    except KeyError as exc:
        raise ContractError(f"unknown FPIOA function {function_id!r}") from exc


def canonical_json_bytes(value: Any) -> bytes:
    return (
        json.dumps(
            value,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            indent=2,
            separators=(",", ": "),
        ).encode("utf-8")
        + b"\n"
    )


@dataclass(frozen=True)
class Board:
    path: Path
    data: dict[str, Any]
    flash: dict[str, int] = field(default_factory=dict)
    partitions: list[dict[str, Any]] = field(default_factory=list)

    @property
    def directory(self) -> Path:
        return self.path.parent

    @property
    def id(self) -> str:
        return self.data["id"]

    @property
    def platform(self) -> str:
        return self.data["platform"]

    @property
    def support(self) -> str:
        return self.data["support"]

    @property
    def releaseable(self) -> bool:
        return self.data["releaseable"]

    @property
    def runtime_profile(self) -> str | None:
        return self.data.get("runtime_profile")

    @property
    def programming(self) -> dict[str, Any]:
        return self.data["programming"]

    def present_kinds(self) -> set[str]:
        if "present" in self.data:
            return set(self.data["present"])
        return set(self.data.get("available", ()))

    def driver_supported_kinds(self) -> set[str]:
        return set(self.data.get("available", ()))

    def selected_routes(self) -> list[dict[str, Any]]:
        return list(self.data.get("routes", ()))

    def required_callbacks(self) -> set[str]:
        return {
            PREPARE_CALLBACKS[kind]
            for kind in self.driver_supported_kinds()
            if kind in PREPARE_CALLBACKS
        }


def _validate_services(data: dict[str, Any]) -> None:
    available = _string_list(data.get("available"), "available")
    unknown = sorted(set(available) - SERVICE_KINDS)
    if unknown:
        raise ContractError("available: unknown service(s): " + ", ".join(unknown))
    if "present" in data:
        present = _string_list(data["present"], "present")
        unknown_present = sorted(set(present) - SERVICE_KINDS)
        if unknown_present:
            raise ContractError(
                "present: unknown service(s): " + ", ".join(unknown_present)
            )
        missing = sorted(set(available) - set(present))
        if missing:
            raise ContractError(
                "available: driver-supported service(s) missing from present: "
                + ", ".join(missing)
            )
    elif data.get("support") == "conformance":
        raise ContractError("present: conformance board must list present services")
    if data.get("support") == "runtime":
        missing = sorted(RUNTIME_REQUIRED_SERVICES - set(available))
        if missing:
            raise ContractError(
                f"runtime_profile {RUNTIME_PROFILE!r}: missing available service(s): "
                + ", ".join(missing)
            )


def _validate_routes(data: dict[str, Any]) -> None:
    routes = data.get("routes", [])
    if not isinstance(routes, list):
        raise ContractError("routes: expected array of tables")
    ids: set[str] = set()
    macros: set[str] = set()
    runtime_mux_groups: dict[str, list[dict[str, Any]]] = {}
    for index, route in enumerate(routes):
        field = f"routes[{index}]"
        if not isinstance(route, dict):
            raise ContractError(f"{field}: expected table")
        _strict(route, ROUTE_FIELDS, field)
        route_id = _string(route.get("id"), f"{field}.id", pattern=IDENTIFIER_RE)
        macro = _string(route.get("macro"), f"{field}.macro", pattern=MACRO_RE)
        _integer(route.get("pin"), f"{field}.pin", maximum=47)
        function = _string(
            route.get("function"), f"{field}.function", pattern=IDENTIFIER_RE
        )
        peripheral = _string(
            route.get("peripheral"), f"{field}.peripheral", pattern=IDENTIFIER_RE
        )
        if function not in FPIOA_FUNCTIONS:
            raise ContractError(f"{field}.function: unknown FPIOA function {function!r}")
        expected_peripheral, _macro = FPIOA_FUNCTIONS[function]
        if peripheral != expected_peripheral:
            raise ContractError(
                f"{field}.peripheral: function {function!r} requires "
                f"{expected_peripheral!r}"
            )
        role = ROUTE_ROLES.get(macro)
        if role is None:
            raise ContractError(f"{field}.macro: unknown route role {macro!r}")
        if role != (function, peripheral):
            raise ContractError(
                f"{field}: route role {macro!r} requires function "
                f"{role[0]!r} on {role[1]!r}"
            )
        if route_id in ids:
            raise ContractError(f"{field}.id: duplicate {route_id!r}")
        if macro in macros:
            raise ContractError(f"{field}.macro: duplicate {macro!r}")
        ids.add(route_id)
        macros.add(macro)
        if "runtime_mux_group" in route:
            runtime_group = _string(
                route["runtime_mux_group"],
                f"{field}.runtime_mux_group",
                pattern=IDENTIFIER_RE,
            )
            runtime_mux_groups.setdefault(runtime_group, []).append(route)

    for left_index, left in enumerate(routes):
        for right in routes[left_index + 1:]:
            pin_collision = left["pin"] == right["pin"]
            function_collision = left["function"] == right["function"]
            same_runtime_mux_group = (
                left.get("runtime_mux_group") is not None
                and left.get("runtime_mux_group") == right.get("runtime_mux_group")
            )
            allowed = pin_collision and not function_collision and same_runtime_mux_group
            if (pin_collision or function_collision) and not allowed:
                what = "pin" if pin_collision else "function"
                raise ContractError(
                    f"routes: {left['id']!r} and {right['id']!r} duplicate {what} "
                    "outside one compatible route group"
                )

    for group, candidates in runtime_mux_groups.items():
        if data["support"] != "runtime":
            raise ContractError(
                f"runtime_mux_group {group!r}: only runtime ports may preserve a legacy mux"
            )
        expected = LEGACY_RUNTIME_MUX.get(group)
        if expected is None:
            raise ContractError(
                f"runtime_mux_group {group!r}: not a defined legacy exception"
            )
        actual = {
            (route["macro"], route["function"], route["pin"], route["peripheral"])
            for route in candidates
        }
        if actual != expected:
            raise ContractError(
                f"runtime_mux_group {group!r}: routes must exactly match the "
                "legacy preservation exception"
            )


def _validate_programming(data: dict[str, Any], board_dir: Path | None) -> None:
    programming = data.get("programming")
    if not isinstance(programming, dict):
        raise ContractError("programming: expected table")
    _strict(programming, PROGRAMMING_FIELDS, "programming")
    supported = _boolean(programming.get("supported"), "programming.supported")
    reset = _string(programming.get("reset_profile"), "programming.reset_profile")
    if reset not in RESET_PROFILES:
        raise ContractError(f"programming.reset_profile: unknown profile {reset!r}")
    usb = programming.get("usb_detection")
    if not isinstance(usb, dict):
        raise ContractError("programming.usb_detection: expected table")
    _strict(usb, USB_FIELDS, "programming.usb_detection")
    mode = _string(usb.get("mode"), "programming.usb_detection.mode")
    if mode not in USB_DETECTION_MODES:
        raise ContractError(f"programming.usb_detection.mode: unknown mode {mode!r}")
    if mode == "vid-any":
        vids = _string_list(usb.get("vids"), "programming.usb_detection.vids")
        if not vids:
            raise ContractError(
                "programming.usb_detection.vids: vid-any requires at least one VID"
            )
        if any(VID_RE.fullmatch(vid) is None for vid in vids):
            raise ContractError(
                "programming.usb_detection.vids: expected uppercase four-digit hex VIDs"
            )
    elif "vids" in usb:
        raise ContractError("programming.usb_detection.vids: only valid with mode=vid-any")

    hardware_defaults = {
        "reboot_profile", "isp_stub", "flash_type", "flash_mode",
        "boot_baud", "flash_baud", "reset_attempts",
    }
    if not supported:
        forbidden = sorted(hardware_defaults & set(programming))
        if forbidden:
            raise ContractError(
                "programming: supported=false forbids hardware defaults: "
                + ", ".join(forbidden)
            )
        if mode != "explicit-port":
            raise ContractError(
                "programming.usb_detection.mode: unsupported ports require explicit-port"
            )
        return
    missing = sorted(hardware_defaults - set(programming))
    if missing:
        raise ContractError("programming: missing required field(s): " + ", ".join(missing))
    reboot = _string(programming["reboot_profile"], "programming.reboot_profile")
    if reboot not in REBOOT_PROFILES:
        raise ContractError(f"programming.reboot_profile: unknown profile {reboot!r}")
    stub = _string(programming["isp_stub"], "programming.isp_stub")
    stub_path = Path(stub)
    if stub_path.is_absolute() or ".." in stub_path.parts:
        raise ContractError("programming.isp_stub: expected repository-relative safe path")
    if board_dir is not None:
        root = board_dir.parents[1]
        if not (root / stub_path).is_file():
            raise ContractError(f"programming.isp_stub: file does not exist: {stub}")
    if programming["flash_type"] not in FLASH_TYPES:
        raise ContractError("programming.flash_type: unknown value")
    if programming["flash_mode"] not in FLASH_MODES:
        raise ContractError("programming.flash_mode: unknown value")
    _integer(programming["boot_baud"], "programming.boot_baud", minimum=1)
    _integer(programming["flash_baud"], "programming.flash_baud", minimum=1)
    _integer(programming["reset_attempts"], "programming.reset_attempts",
             minimum=1, maximum=100)


def _validate_defaults(data: dict[str, Any]) -> None:
    defaults = data.get("defaults")
    if not isinstance(defaults, dict):
        raise ContractError("defaults: expected table")
    _strict(defaults, DEFAULT_FIELDS, "defaults")
    for name, value in defaults.items():
        minimum, maximum = DEFAULT_RANGES[name]
        _integer(value, f"defaults.{name}", minimum=minimum, maximum=maximum)
    for name, expected in K210_FIXED_DEFAULTS.items():
        if name in defaults and defaults[name] != expected:
            raise ContractError(f"defaults.{name}: K210 requires {expected}")
    if data["support"] == "runtime":
        missing_defaults = sorted(DEFAULT_FIELDS - set(defaults))
        if missing_defaults:
            raise ContractError(
                "defaults: runtime board is missing: " + ", ".join(missing_defaults)
            )
    routes_by_macro = {route["macro"]: route for route in data.get("routes", [])}
    for field, macro in GPIOHS_DEFAULT_ROUTES.items():
        if field not in defaults:
            continue
        route = routes_by_macro.get(macro)
        if route is None:
            raise ContractError(f"defaults.{field}: route {macro} is missing")
        match = re.fullmatch(r"gpiohs(\d+)", route["function"])
        if match is None or defaults[field] != int(match.group(1)):
            raise ContractError(
                f"defaults.{field}: must match {macro} function {route['function']!r}"
            )
    for field, macro in SPI_DEFAULT_ROUTES.items():
        if field not in defaults:
            continue
        route = routes_by_macro.get(macro)
        match = re.fullmatch(r"spi(\d+)", route["peripheral"] if route else "")
        if match is None or defaults[field] != int(match.group(1)):
            raise ContractError(f"defaults.{field}: must match {macro} peripheral")
    for field, macro in SPI_CHIP_SELECT_DEFAULT_ROUTES.items():
        if field not in defaults:
            continue
        route = routes_by_macro.get(macro)
        match = re.fullmatch(r"spi\d+-ss(\d+)", route["function"] if route else "")
        if match is None or defaults[field] != int(match.group(1)):
            raise ContractError(
                f"defaults.{field}: must match {macro} chip-select function"
            )
    for device_field, channel_field, macro in PWM_DEFAULT_ROUTES:
        if device_field not in defaults or channel_field not in defaults:
            continue
        route = routes_by_macro.get(macro)
        match = re.fullmatch(r"timer(\d+)-toggle(\d+)", route["function"] if route else "")
        if match is None:
            raise ContractError(
                f"defaults.{device_field}/{channel_field}: {macro} is not a PWM timer route"
            )
        expected_device = int(match.group(1))
        expected_channel = int(match.group(2)) - 1
        if (defaults[device_field] != expected_device or
                defaults[channel_field] != expected_channel):
            raise ContractError(
                f"defaults.{device_field}/{channel_field}: must match {macro} "
                f"timer{expected_device} channel {expected_channel}"
            )


def _validate_flash(data: dict[str, Any]) -> tuple[dict[str, int], list[dict[str, Any]]]:
    raw_flash = data.get("flash")
    raw_partitions = data.get("partitions")
    if not isinstance(raw_flash, dict):
        raise ContractError("flash: expected table")
    _strict(raw_flash, FLASH_FIELDS, "flash")
    if not isinstance(raw_partitions, list) or not raw_partitions:
        raise ContractError("partitions: expected a non-empty array")
    flash = {
        key: _integer(raw_flash.get(key), f"flash.{key}")
        for key in (
            "address_bytes", "minimum_capacity", "maximum_capacity",
            "erase_size", "program_size",
        )
    }
    if flash["address_bytes"] != 3:
        raise ContractError("flash.address_bytes: this driver supports exactly 3")
    if not flash["erase_size"] or not flash["program_size"]:
        raise ContractError("flash: erase_size and program_size must be non-zero")
    if flash["erase_size"] % flash["program_size"]:
        raise ContractError("flash: erase_size must be a multiple of program_size")
    if flash["minimum_capacity"] > flash["maximum_capacity"]:
        raise ContractError("flash: minimum_capacity exceeds maximum_capacity")
    address_limit = 1 << (8 * flash["address_bytes"])
    if flash["maximum_capacity"] > address_limit:
        raise ContractError("flash: maximum_capacity exceeds address command width")

    partitions: list[dict[str, Any]] = []
    seen: set[str] = set()
    previous_end = 0
    for index, raw in enumerate(raw_partitions):
        field = f"partitions[{index}]"
        if not isinstance(raw, dict):
            raise ContractError(f"{field}: expected table")
        _strict(raw, PARTITION_FIELDS, field)
        name = _string(raw.get("name"), f"{field}.name", pattern=PARTITION_NAME_RE)
        if name in seen:
            raise ContractError(f"{field}.name: duplicate {name!r}")
        seen.add(name)
        offset = _integer(raw.get("offset"), f"{field}.offset")
        size = _integer(raw.get("size"), f"{field}.size")
        required = _integer(raw.get("required_capacity"), f"{field}.required_capacity")
        writable = _boolean(raw.get("runtime_writable"), f"{field}.runtime_writable")
        if not size:
            raise ContractError(f"{field}.size: must be non-zero")
        if offset % flash["erase_size"] or size % flash["erase_size"]:
            raise ContractError(f"{field}: offset and size must be erase-aligned")
        end = offset + size
        if end > 0xFFFFFFFF or end > flash["maximum_capacity"]:
            raise ContractError(f"{field}: partition exceeds maximum flash capacity")
        if offset < previous_end:
            raise ContractError(f"{field}: overlaps the preceding partition")
        if required < end or required > flash["maximum_capacity"]:
            raise ContractError(f"{field}.required_capacity: does not contain partition")
        if required not in (flash["minimum_capacity"], flash["maximum_capacity"]):
            raise ContractError(f"{field}.required_capacity: unsupported capacity profile")
        partitions.append({
            "name": name,
            "offset": offset,
            "size": size,
            "required_capacity": required,
            "runtime_writable": writable,
        })
        previous_end = end
    if partitions[0]["offset"] != 0:
        raise ContractError("partitions: first partition must start at zero")
    if previous_end != flash["maximum_capacity"]:
        raise ContractError(
            "partitions: map must cover the maximum capacity without a tail gap"
        )
    return flash, partitions


def validate_board_data(data: dict[str, Any], *, board_dir: Path | None = None) -> None:
    _strict(data, ROOT_FIELDS, "board")
    if type(data.get("schema")) is not int or data["schema"] != 1:
        raise ContractError("schema: expected 1")
    board_id = _string(data.get("id"), "id", pattern=BOARD_ID_RE)
    if board_dir is not None and board_dir.name != board_id:
        raise ContractError(f"id: {board_id!r} does not match directory {board_dir.name!r}")
    if data.get("platform") != PLATFORM_ID:
        raise ContractError(f"platform: expected {PLATFORM_ID!r}")
    support = data.get("support")
    if support not in {"runtime", "conformance"}:
        raise ContractError("support: expected runtime or conformance")
    releaseable = _boolean(data.get("releaseable"), "releaseable")
    profile = data.get("runtime_profile")
    if support == "runtime":
        if profile != RUNTIME_PROFILE:
            raise ContractError("runtime_profile: runtime board requires hackylens-full")
    elif profile is not None:
        raise ContractError("runtime_profile: forbidden for conformance board")
    if releaseable and support != "runtime":
        raise ContractError("releaseable: true requires support=runtime")
    _validate_services(data)
    _validate_routes(data)
    _validate_defaults(data)
    _validate_flash(data)
    _validate_programming(data, board_dir)


def board_ids(root: Path = ROOT) -> list[str]:
    boards = root / "boards"
    if not boards.is_dir():
        return []
    return sorted(
        path.name for path in boards.iterdir()
        if path.is_dir() and (path / "board.toml").is_file()
    )


def load_board(board_id: str, *, root: Path = ROOT) -> Board:
    if BOARD_ID_RE.fullmatch(board_id) is None:
        raise ContractError(f"board: invalid canonical ID {board_id!r}")
    directory = root / "boards" / board_id
    path = directory / "board.toml"
    if not path.is_file():
        known = ", ".join(board_ids(root)) or "none"
        raise ContractError(f"board: unknown ID {board_id!r}; known boards: {known}")
    data = _load_toml(path)
    validate_board_data(data, board_dir=directory)
    flash, partitions = _validate_flash(data)
    if not (directory / "board.c").is_file():
        raise ContractError(f"board {board_id!r}: required file missing: board.c")
    return Board(path=path, data=data, flash=flash, partitions=partitions)


def partition_by_name(partitions: list[dict[str, Any]], name: str) -> dict[str, Any]:
    for partition in partitions:
        if partition["name"] == name:
            return partition
    raise ContractError(f"partitions: required partition {name!r} is missing")


def flash_layout_document(board: Board) -> dict[str, Any]:
    return {
        "flash": {
            "address_bytes": board.flash["address_bytes"],
            "erase_size": f"0x{board.flash['erase_size']:08X}",
            "maximum_capacity": f"0x{board.flash['maximum_capacity']:08X}",
            "minimum_capacity": f"0x{board.flash['minimum_capacity']:08X}",
            "program_size": f"0x{board.flash['program_size']:08X}",
        },
        "partitions": [
            {
                "name": part["name"],
                "offset": f"0x{part['offset']:08X}",
                "required_capacity": f"0x{part['required_capacity']:08X}",
                "runtime_writable": part["runtime_writable"],
                "size": f"0x{part['size']:08X}",
            }
            for part in board.partitions
        ],
        "schema_version": 1,
    }


def flash_layout_sha256(board: Board) -> str:
    return hashlib.sha256(canonical_json_bytes(flash_layout_document(board))).hexdigest()


def _strip_c_comments(source: str) -> str:
    result: list[str] = []
    index = 0
    state = "code"
    while index < len(source):
        current = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "line-comment":
            if current == "\n":
                result.append(current)
                state = "code"
            else:
                result.append(" ")
            index += 1
            continue
        if state == "block-comment":
            if current == "*" and following == "/":
                result.extend((" ", " "))
                index += 2
                state = "code"
            else:
                result.append("\n" if current == "\n" else " ")
                index += 1
            continue
        if state in {"string", "character"}:
            result.append(current)
            if current == "\\" and following:
                result.append(following)
                index += 2
                continue
            terminator = '"' if state == "string" else "'"
            if current == terminator:
                state = "code"
            index += 1
            continue
        if current == "/" and following == "/":
            result.extend((" ", " "))
            index += 2
            state = "line-comment"
        elif current == "/" and following == "*":
            result.extend((" ", " "))
            index += 2
            state = "block-comment"
        else:
            result.append(current)
            if current == '"':
                state = "string"
            elif current == "'":
                state = "character"
            index += 1
    return "".join(result)


def validate_board_source(board: Board) -> None:
    source_path = board.directory / "board.c"
    source = _strip_c_comments(source_path.read_text(encoding="utf-8"))
    match = re.search(
        r"const\s+hk_board_ops_t\s+hk_board_ops\s*=\s*\{(?P<body>.*?)\}\s*;",
        source,
        re.DOTALL,
    )
    if match is None:
        raise ContractError(
            f"{source_path}: missing const hk_board_ops_t hk_board_ops initializer"
        )
    body = match.group("body")
    callbacks = {
        name: value.strip()
        for name, value in re.findall(r"\.([a-z0-9_]+)\s*=\s*([^,}]+)", body)
    }
    required = {"early_init"} | board.required_callbacks()
    missing = sorted(name for name in required if callbacks.get(name) in {None, "NULL", "0"})
    if missing:
        raise ContractError(
            f"{source_path}: required non-NULL board callback(s): " + ", ".join(missing)
        )


def load_all_boards(root: Path = ROOT) -> list[Board]:
    result = [load_board(board_id, root=root) for board_id in board_ids(root)]
    for board in result:
        validate_board_source(board)
    return result
