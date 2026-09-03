#!/usr/bin/env python3
"""Generate one private board_config.h from board.toml."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

from board_contract import (
    Board,
    ContractError,
    ROOT,
    function_macro,
    load_board,
    validate_board_source,
)


BUILD_BOARD_CONFIG = ROOT / "build" / "generated" / "board_config.h"

DEFAULT_MACROS = {
    "lcd_width": "LCD_W",
    "lcd_height": "LCD_H",
    "lcd_spi": "LCD_SPI",
    "lcd_chip_select": "LCD_CS",
    "lcd_spi_hz": "LCD_SPI_HZ",
    "gpiohs_lcd_reset": "GPIOHS_LCD_RST",
    "gpiohs_lcd_dc": "GPIOHS_LCD_DC_OR_AUX",
    "gpiohs_button_left": "GPIOHS_BTN_LEFT",
    "gpiohs_button_ok": "GPIOHS_BTN_OK",
    "gpiohs_button_right": "GPIOHS_BTN_RIGHT",
    "gpiohs_button_back": "GPIOHS_BTN_BACK",
    "gpiohs_sd_chip_select": "GPIOHS_SD_CS",
    "sd_spi": "SD_SPI",
    "sd_chip_select": "SD_SPI_CS",
    "flash_spi": "FLASH_SPI",
    "flash_chip_select": "FLASH_SPI_CS",
    "led_pwm_device": "LED_PWM_DEVICE",
    "led_pwm_channel": "LED_PWM_CHANNEL",
    "rgb_pwm_device": "RGB_PWM_DEVICE",
    "rgb_pwm_channel0": "RGB_PWM_CHANNEL0",
    "rgb_pwm_channel1": "RGB_PWM_CHANNEL1",
    "rgb_pwm_channel2": "RGB_PWM_CHANNEL2",
    "screen_pwm_device": "SCREEN_BL_PWM_DEVICE",
    "screen_pwm_channel": "SCREEN_BL_PWM_CHANNEL",
    "pwm_frequency_hz": "PWM_FREQ_HZ",
    "camera_sccb_address": "CAMERA_SCCB_ADDR",
    "camera_xclk_hz": "CAMERA_XCLK_HZ",
    "camera_sccb_hz": "CAMERA_SCCB_HZ",
    "camera_max_width": "CAMERA_MAX_W",
    "camera_max_height": "CAMERA_MAX_H",
}


def render_board_config(board: Board) -> str:
    profile = board.runtime_profile or ""
    lines = [
        "#ifndef HK_BOARD_CONFIG_H",
        "#define HK_BOARD_CONFIG_H",
        "",
        f"/* Generated from boards/{board.id}/board.toml. Do not edit. */",
        "",
        f'#define HK_BOARD_ID "{board.id}"',
        f'#define HK_BOARD_PLATFORM "{board.platform}"',
        f'#define HK_BOARD_SUPPORT "{board.support}"',
        f"#define HK_BOARD_RELEASEABLE {1 if board.releaseable else 0}U",
        f'#define HK_BOARD_RUNTIME_PROFILE "{profile}"',
        "",
    ]
    for route in board.selected_routes():
        lines.append(f"#define {route['macro']} {route['pin']}")
        lines.append(f'#define {route["macro"]}_LABEL "IO{route["pin"]}"')
        lines.append(
            f"#define {route['macro']}_FUNCTION {function_macro(route['function'])}"
        )
    if board.selected_routes():
        lines.append("")
    defaults = board.data.get("defaults", {})
    for field, macro in DEFAULT_MACROS.items():
        if field not in defaults:
            continue
        value = defaults[field]
        suffix = (
            ".0" if field == "pwm_frequency_hz"
            else "" if field in {"lcd_width", "lcd_height"}
            else "U"
        )
        lines.append(f"#define {macro} {value}{suffix}")
        lines.append(f'#define {macro}_LABEL "{value}"')
    if "gpiohs_lcd_dc" in defaults:
        lines.append("#define GPIOHS_LCD_DC_BIT (1U << GPIOHS_LCD_DC_OR_AUX)")
    if "camera_max_width" in defaults and "camera_max_height" in defaults:
        lines.append("#define CAMERA_MAX_FRAME_PIXELS (CAMERA_MAX_W * CAMERA_MAX_H)")
    if defaults:
        lines.append("")
    flash = board.flash
    lines.extend([
        f"#define HK_FLASH_ADDRESS_BYTES {flash['address_bytes']}U",
        f"#define HK_FLASH_MIN_CAPACITY 0x{flash['minimum_capacity']:08X}UL",
        f"#define HK_FLASH_MAX_CAPACITY 0x{flash['maximum_capacity']:08X}UL",
        f"#define HK_FLASH_ERASE_SIZE 0x{flash['erase_size']:08X}UL",
        f"#define HK_FLASH_PROGRAM_SIZE 0x{flash['program_size']:08X}UL",
        "",
        "typedef enum",
        "{",
    ])
    for index, part in enumerate(board.partitions):
        lines.append(f"    HK_FLASH_PARTITION_{part['name'].upper()} = {index},")
    lines.extend([
        f"    HK_FLASH_PARTITION_COUNT = {len(board.partitions)}",
        "} hk_flash_partition_id_t;",
        "",
    ])
    for part in board.partitions:
        name = part["name"].upper()
        lines.extend([
            f"#define HK_FLASH_PARTITION_{name}_OFFSET 0x{part['offset']:08X}UL",
            f"#define HK_FLASH_PARTITION_{name}_SIZE 0x{part['size']:08X}UL",
            f"#define HK_FLASH_PARTITION_{name}_REQUIRED_CAPACITY "
            f"0x{part['required_capacity']:08X}UL",
            f"#define HK_FLASH_PARTITION_{name}_RUNTIME_WRITABLE "
            f"{1 if part['runtime_writable'] else 0}U",
            "",
        ])
    lines.extend(["#endif", ""])
    return "\n".join(lines)


def write_board_config(board: Board, path: Path | None = None) -> Path:
    validate_board_source(board)
    destination = path or BUILD_BOARD_CONFIG
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(render_board_config(board), encoding="utf-8", newline="\n")
    return destination


def board_config_include_dir(board_id: str = "huskylens-sen0305") -> Path:
    """Write board_config.h and return its include directory."""
    return write_board_config(load_board(board_id)).parent


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", required=True, help="Canonical board.toml ID")
    parser.add_argument(
        "--out",
        type=Path,
        default=BUILD_BOARD_CONFIG,
        help="Output path for board_config.h",
    )
    args = parser.parse_args(argv)
    try:
        board = load_board(args.board)
        path = write_board_config(board, args.out)
    except (ContractError, ValueError) as exc:
        print(f"board contract error: {exc}", file=sys.stderr)
        return 2
    print(f"generated {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
