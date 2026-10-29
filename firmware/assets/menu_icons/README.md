# Launcher icons

These are the approved thin V2 launcher masks: 48 by 48 pixels, one bit per
pixel, most significant bit first, six bytes per row, 288 bytes per icon.
Set bits use the requested foreground; clear bits use the background. The
launcher places each mask six pixels inside its existing 60-pixel tile.

`manifest.json` records the approved SHA-256 hashes. Regenerate the C header
with `python tools/make_menu_icons.py`, or check it with `--check`. Changing a
mask requires updating its approved checksum and reviewing both selection
states. The generator rejects changed or incorrectly sized source masks.

The launcher Power icon is the thin V2 variant. The separate Sleep, Off, and
Restart glyphs inside the Power app remain in `hackylens_power_icons_1bpp.h`.
