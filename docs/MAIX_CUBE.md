# Sipeed Maix Cube

HackyLens supports Maix Cube 2757 from v0.5.0. The `sipeed-maix-cube` board
uses the `hackylens-cube` runtime profile. Its full build includes fifteen
apps, with RECORDER and ACCELEROMETER available on Cube. See the
[v0.9.0 release notes](releases/v0.9.0.md) for the release checks and the
[hardware test log](MAIX_CUBE_TEST_LOG.md) for the builds, conditions, and
results of individual trials. A diagnostic build passed native
[UART and I2C exchanges over SP-MOD](MAIX_CUBE_TEST_LOG.md#uart-and-i2c-over-sp-mod-2026-11-22).
Physical Grove exchange on Cube was
[waived for release acceptance](MAIX_CUBE_TEST_LOG.md#release-050-final-io-acceptance-2026-10-30).

## Hardware and composition

| Component | Configuration |
| --- | --- |
| Processor and flash | Kendryte K210 / M1n, 16 MiB SPI NOR |
| Display | Parallel ST7789V, 240 × 240, SPI0/DVP shared data path |
| Camera | OV7740 at 7-bit address `0x21` on I2C2, SCL41/SDA40 at 100 kHz, requested XCLK 22 MHz |
| Power and motion | AXP173 PMIC and MSA301 accelerometer on internal I2C1, SCL30/SDA31 |
| Audio | ES8374 at `0x10` on internal I2C1, built-in speaker and microphone, I2S0 and DMA3 |
| Lights | Active-low RGB, white illumination, and LCD backlight |
| Grove | Pins 24/25 serve as UART1 RX/TX or I2C0 SCL/SDA. The modes are exclusive and use 3.3 V logic. |
| Storage | FAT32 microSD and internal littlefs for MicroPython scripts |

The [board descriptor](../boards/sipeed-maix-cube/board.toml) defines the pin
routes, defaults, and flash map. See [External Link](EXTERNAL_LINK_PROTOCOL.md#physical-port)
for connector limits and protocol use.

I/O banks 6 and 7 use 1.8 V for the camera and display data bus. The LCD
requests a 15 MHz write clock and uses DMA0. KPU inference uses DMA5.
Backlight PWM runs at 500 Hz and illumination/RGB PWM runs at 20 kHz.
The PMIC provider preserves the stock rail and charging settings.

The camera preview rotates 90° clockwise to compensate for sensor mounting.
At 320 × 240 the display shows the centered 240 × 240 portion of the rotated
frame, using the same crop for boxes and labels. Camera processing retains
the full frame. FILES fits complete images into the display. The launcher
uses a paginated 3 × 3 grid.

## Controls

- Move the three-way control left or right for LEFT or RIGHT. Its center is
  OK and supports hold gestures.
- A short or long press of POW sends one BACK event while HackyLens is
  running. Hold the physical center during boot to skip autostart.
- Use POWER for sleep, shutdown, or restart. One new OK press executes
  the selected action.

## Recorder and WAV playback

With a FAT32 microSD card inserted, open RECORDER and use LEFT/RIGHT to
select RECORD, RECORDINGS, or VOLUME. Press OK on RECORD to start. Press
OK or BACK to save the recording to `/hackyens.records/REC00001.WAV` or the
next available number. Recordings are 16-bit mono PCM at 15,869 Hz. Use
FILES to free an entry if the directory reaches 64 entries. An interrupted
recording can leave a `.TMP` file.

Open a WAV in RECORDINGS or FILES. OK starts or stops playback, LEFT/RIGHT
adjusts the saved volume, and BACK returns to the list. The player takes
16-bit PCM WAV at 8–48 kHz with one or two channels. Stereo is mixed to mono.
RECORDER, FILES, and the [MicroPython audio API](MICROPYTHON_API.md#built-in-audio)
share the volume setting.

## Accelerometer

LEFT/RIGHT switches between LEVEL, TRACE, and EVENTS. LEVEL estimates
tilt from gravity. TRACE plots 48 readings over about five seconds, using
the sensor's native XYZ axes and a ±2 g scale. EVENTS shows orientation,
tap, double tap, motion, and free-fall status. `LIVE` marks an active flag.
`SEEN` marks an event since opening the app or clearing the view with OK.
MicroPython reads the same XYZ data via `hackylens.acceleration_mg()`.
See the [accelerometer contract](spec/capabilities/ACCELEROMETER.md).

## Build and artifacts

From the repository root on Windows, load the environment and select Cube.

```powershell
. .\env.ps1
python tools\build_firmware.py full --board sipeed-maix-cube
```

The build produces `build/sipeed-maix-cube/hackylens-full.bin` and its
flasher JSON and attestation. A qualified full build also publishes
`dist/hackylens-full-sipeed-maix-cube.bin` with matching metadata. Use the
Cube image and sidecar together. See [flashing instructions](../README.md#flash-and-debug)
and the [board port contract](spec/BOARD_PORT.md#layout-and-artifact-safety).

Runtime writes to settings and userfs require JEDEC to report exactly
16 MiB. The firmware/settings region ends at 8 MiB and userfs occupies
`0x00C70000` through `0x00FFFFFF`. A normal firmware flash preserves both
settings and userfs. Save the original full flash before the first install.

Cube uses the `sipeed-goe` DTR/RTS profile for BootROM entry and
`sipeed-goe-normal` for normal boot. Use the identified Cube port. The
flasher's `--manual` option is available for manual BootROM entry. To
read a stock MaixPy backup, use the actual port in place of the example.

```powershell
python tools\maixpy_flash_backup.py probe --port COM11
python tools\maixpy_flash_backup.py backup --port COM11 --output ..\outputs\cube-original.bin
```

The utility enters raw REPL, verifies each block and compares a full
device-side flash hash with the host file. `--resume` continues an aligned
`.partial` file and still verifies the full hash. Keep the stock transport
at 115200 baud.

## Driver references

The wiring references are Sipeed's
[MaixPy-v1 Cube config](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/boards/config/cube.config.json)
and [Cube 2757 schematic](https://dl.sipeed.com/fileList/MAIX/HDK/Sipeed-Maix-Cube/Maix-Cube-2757/Maix-Cube-2757%28Schematic%29.pdf).
Camera and panel setup draw on Sipeed's pinned
[OV7740](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/micropython/port/src/omv/ov7740.c),
[camera bus](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/micropython/port/src/omv/cambus.c),
[sensor integration](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/micropython/port/src/omv/sensor.c),
[ST7789 transport](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/drivers/lcd/src/st7789.c),
[panel initialization](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/drivers/lcd/src/lcd_mcu.c),
and [startup code](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/micropython/port/src/maixpy_main.c).
The audio routes follow the [Cube codec example](https://github.com/sipeed/MaixPy-v1_scripts/blob/master/modules/others/es8374/es8374.py).
