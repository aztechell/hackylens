# Maix Cube experimental runtime

The `sipeed-maix-cube` board selects the `hackylens-cube` runtime profile with
all twelve native apps, including the embedded MicroPython app. This is
HackyLens firmware, not a MaixPy firmware build. Physical acceptance is pending;
the descriptor remains `releaseable=false`.

## Hardware and composition

The port targets the Sipeed Maix Cube 2757 layout: K210/M1n, ST7789V 240x240
parallel LCD, OV7740 camera, AXP173, 16-MiB SPI flash, SD, three-way control,
RGB LED, white illumination and backlight. Audio and accelerometer are outside
this port. Pin mappings come from Sipeed's
[MaixPy-v1 Cube board config](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/boards/config/cube.config.json)
and [Cube schematic](https://dl.sipeed.com/fileList/MAIX/HDK/Sipeed-Maix-Cube/Maix-Cube-2757/Maix-Cube-2757%28Schematic%29.pdf).
Camera/LCD initialization follows the corresponding MaixPy-v1 drivers.

LCD control uses CS36, RESET37, DC38 and WR39 with the fixed shared SPI0/DVP
octal data bus. Banks 6/7 use 1.8 V. Camera control pins are 40-47; OV7740 uses
7-bit address 0x21 on I2C2, SCL41/SDA40, 100-kHz control bus and
22-MHz requested XCLK. LCD WR requests 15 MHz.
PMIC management uses I2C1 on SCL30/SDA31;
it probes power status without changing battery charging or regulator policy.
RGB pins are 13/12/14, white illumination32, and backlight17. All are active-low:
RGB packages have common anodes; the white LED and backlight use SI2301
high-side switches. PWM is driven to its inactive high level before pin routing.

Grove signal pins 24/25 support mutually exclusive UART1 RX/TX and I2C0 SCL/SDA
through the existing External Link service. PMIC management stays on its
separate I2C1 instance. Use 3.3-V logic and the documented connector supply;
external controller/target and UART behavior require physical loopback/bus
acceptance.

Capture and AI processing retain 320x240 input. The 240x240 UI presents the
complete camera image rotated 90 degrees clockwise in a centered 180x240
viewport, with proportional overlay mapping. The private board default
`camera_preview_rotate = 1` selects this mounting correction for every camera
session; capture, saved photos and AI coordinates retain sensor orientation.
The menu uses a 3x3 grid, two pages and page indicators. SEN0305 keeps its 4x3
menu and original 320x240 geometry.
Pong obtains its field dimensions through the public display capability;
its physics and rendering use the same 240x240 bounds on Cube.

## Controls

The generated routes use ENTER10 for the center, BACK11 for previous and NEXT16
for next. Schematic S3 confirms KEY2/KEY1/BOOT in left/center/right order.

- Move to either side to send LEFT/RIGHT.
- Center is normal OK, including held OK for app contexts.
- POW short or long press is BACK; holding POW does not switch the board off.
- BUTTON TEST exits on one POW/BACK pulse. It does not require holding BACK.
- Hold the physical center during startup to suppress persisted autostart.

## Build and artifacts

From the repository root on Windows:

```powershell
. .\env.ps1
python tools\build_firmware.py full --board sipeed-maix-cube
```

The experimental image is `build/sipeed-maix-cube/hackylens-full.bin`, with a
same-stem `.json` flasher sidecar and `.attestation.json`. Its attestation binds
the image hash and full app composition but sets `release_qualified=false`.
The build does not publish it into `dist/`; `package_release.py` rejects it.
Do not relabel it as a SEN0305 image or bypass identity/layout validation.

The exact-16-MiB layout retains an 8-MiB firmware/settings region, reserves
0x00800000-0x00C70000 and places userfs at 0x00C70000-0x01000000. Firmware
programming changes only its firmware partition. Runtime settings/userfs writes
are rejected unless the flash capacity matches the descriptor. Preserve the
original full flash before the first install; MicroPython userfs formatting
continues to require its explicit existing confirmation command.

Cube programming uses an explicit identified COM port and manual bootloader
entry. Automatic reset and reboot fallback pulses are not selected by this
descriptor. Flash writes must wait until the connected board identity and
backup have been verified.

Stock MaixPy backups can be read without programming flash:

```powershell
python tools\maixpy_flash_backup.py probe --port COM11
python tools\maixpy_flash_backup.py backup --port COM11 --output ..\outputs\cube-original.bin
```

Use the port actually identified for the connected Cube. The utility stops the
running Python program to enter raw REPL, verifies each transferred block,
rehashes the complete flash independently on the device and verifies the host
file. `--resume` continues an aligned `.partial` file; the complete independent
rehash is still required. Keep the default 115200 baud for stock firmware.

## Recorded development checks (2026-10-28)

- Connected stock firmware: COM11, MaixPy 0.6.2, config type `cube`, LCD 240x240;
  FTDI USB 0403:6010 exposes COM9 and COM11.
- Stock sensor initialization identified OV7740, sensor ID `0x7742`.
- No `/sd` mount was present during inspection.
- Original flash backup: 16,777,216 bytes, independently re-read and verified;
  `../outputs/cube-original-20261028-stock-speed.bin` in the parent workspace.
  SHA-256: `5aa6954b8d9ec655b3b570c4cf6dcad648b3f310a2304dcda075ead81923c8bf`.
- Host suite: 236 tests passed after the Cube and Pong changes. Architecture
  guard passed. Both full Cube and SEN0305 cross-builds passed.
- First Cube image (`19abb5ba...`) was installed successfully after manual
  BootROM entry. Boot reached the menu, registered all twelve apps, read AXP173
  status `0xC1` and flash JEDEC `EF6018` (16 MiB). HMPY HELLO reported the correct
  board and capabilities; userfs was initially unformatted (later formatted for
  the HMPY workflow below).
- The first hardware test FAILED acceptance: the framebuffer screenshot shows
  the correct 240x240 menu, but the user observed white noise on the physical
  LCD, and the sensor probe returned zero. A framebuffer capture alone does not
  validate the LCD transport.
- Corrected candidate: LCD clock 15 MHz; camera SCL41/SDA40; DVP SCCB wire
  address `0x42`; representable 400-kHz divider. Nine Cube host tests and the
  full Cube build passed. Image: 1,547,384 bytes; SHA-256
  `2f6ad47707c2df7c3fd7086d40307fa84e04a0f433599ab22e5c4cea7bff911a`.
  This candidate was installed: physical LCD still showed distorted yellow/black
  output and the sensor probe failed (`FEFF/FFFF`). It is not accepted.
- Next candidate uses the stock MaixPy camera control transport: I2C2 at 100 kHz,
  seven-bit address `0x21`, separate STOP before register reads. Missing-device
  waits are bounded to 20 ms. SEN0305 retains the DVP SCCB implementation.
  LCD octal transactions preload the first FIFO batch before selecting CS;
  coordinate windows and RGB565 byte packing were checked and retained.
  Build: 1,547,448 bytes, SHA-256
  `0bb6aa1ca5f2aaaa92c7645653a024b860b9cea7ecec6b765c6b489ea0e954f3`.
  Cube full build, nine Cube tests, 39 board-contract tests and architecture
  guard and sequential SEN0305 full cross-build (1,546,808 bytes) passed.
  Physical LCD and camera acceptance remain pending;
  `releaseable = false`.
- HMPY hardware workflow passed on the installed `2f6ad477` candidate after
  userfs formatting: file/upload/run/session recovery and STOP in 123–124 ms
  (`build/cube-hmpy-workflow.json`). This does not validate LCD/camera.
- The I2C2/preload candidate (`0bb6aa1c...`) was installed. OV7740 passed:
  manufacturer `0x7FA2`, product `0x7742`, 320x240 at 29.8 fps with zero
  timeouts and no camera failure (`build/cube-i2c-fifo-camera.log`). UART
  screenshot `screenshots/cube-camera-i2c-fifo.bmp` shows a recognizable,
  colored scene and the correct 240x180 viewport. The physical LCD distortion
  remained: FIFO preloading alone was not a fix.
- LCD DMA candidate: 1,547,896 bytes, SHA-256
  `d1fd615658c6556926541f9caae861e62b7d142d7c59b491e1a970c57ad326f6`.
  The complete octal transaction is staged as uncached 32-bit FIFO entries
  and transferred by DMA0, matching the reference transport without CPU/row
  gaps. DMA0 has no other firmware owner; KPU retains DMA5. DMA initialization
  runs once during clock startup so first KPU use cannot reset display DMA.
  Transfer completion and SPI drain share the caller deadline; timeout disables
  DMA0 and the SPI transaction, and a still-busy DMA cannot reuse the buffer.
  Nine Cube host tests cover byte/word ordering, DMA start before CS, and timeout
  cleanup/recovery. Architecture guard and sequential Cube/SEN full builds pass.
  Installed successfully. Camera hardware passed on this DMA candidate:
  `0x7FA2/0x7742`, 320x240 at 24.9 fps, zero timeouts, failure NONE
  (`build/cube-dma-camera.log`). The user confirmed the physical LCD is now correct.
  The board remains experimental pending the remaining acceptance checks.
  HMPY workflow and lease reconnect also PASS on this installed candidate
  (`build/cube-dma-hmpy.json`, result PASS, exit code 0); STOP 119–125 ms.
  Static staging costs 230,400 bytes. The linked image includes MicroPython's
  131,072-byte heap and two 32,768-byte stacks, leaving 1,751,296 bytes of SDK
  heap. The supplied object20 model plus KPU main buffer consume 1,396,340 bytes
  (354,956 bytes remain before other allocations); AI runtime acceptance is
  still required. Face model plus KPU main buffer consume 433,776 bytes.
- Neither of the two standard KFlash RTS/DTR entry sequences produced a
  BootROM response on COM11. Stock MaixPy remained accessible afterward;
  keep manual entry selected. Once BOOT/NEXT is held during RST, use:

```powershell
python tools\hkflash.py flash --board sipeed-maix-cube --port COM11 --manual --flash-baud 115200 build\sipeed-maix-cube\hackylens-full.bin
```

## Physical acceptance still required

Verify cold power-on after disconnecting USB and cycling the battery/power
button, power/reboot, LCD orientation/colors, all control gestures, RGB/white LED/
backlight polarity, OV7740 ID and stable frames, SD insert/remove, persisted
settings, userfs, all twelve apps, AI models, MicroPython start/STOP/recovery,
and Grove UART plus I2C controller/target. Host tests and cross-compilation do
not replace those checks. Keep the board non-releaseable until the evidence is
recorded.

## Driver references

The hardware reference revision is MaixPy-v1
`d8901fd2272f000226f1c1037c1eb7c412b88e66`:

- [OV7740](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/micropython/port/src/omv/ov7740.c):
  default registers, frame sizes and color bars; OpenMV MIT notice is retained
  in `ov7740_sensor.c`.
- [Camera control bus](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/micropython/port/src/omv/cambus.c):
  hardware I2C address convention, 100-kHz initialization and separate register
  selection/read transactions.
- [Sensor integration](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/micropython/port/src/omv/sensor.c):
  OV7740 uses 22-MHz XCLK and YUV input to the K210 DVP RGB565 output path.
- [ST7789 transport](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/drivers/lcd/src/st7789.c)
  and [panel initialization](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/drivers/lcd/src/lcd_mcu.c):
  octal command/data phases, RGB565 format and panel startup.
- [MaixPy startup](https://github.com/sipeed/MaixPy-v1/blob/d8901fd2272f000226f1c1037c1eb7c412b88e66/components/micropython/port/src/maixpy_main.c):
  I/O banks 6/7 at 1.8 V and the fixed SPI0/DVP data-path selection.

The Cube schematic powers the M1n, LCD, SD and light switches from the same
3.3-V supply; M1n has fixed local regulators for the camera/data-bus 1.8-V
rail. Neither Cube's board config nor common MaixPy startup programs AXP173
rail voltages. The port therefore preserves the PMIC's power-on rail and
charging settings. Its separate I2C1 management bus probes status, configures
PEK events and disables PEK automatic power-off for POW-as-BACK; explicit
POWER OFF confirmation sets the PMIC shutdown bit as described below.
A successful warm-reset test does not replace the cold power-on check above.

## POW button as BACK

A short or long POW press is consumed from AXP173 PEK and exposed as one debounced
BACK press/release. The center button is ordinary OK, including held OK for
app contexts; LEFT/RIGHT remain directional. The former delayed click,
double-press and center-hold BACK gestures are removed.
AXP173 PEK automatic power-off is disabled by clearing only register `0x36`
bit 3, preserving all timing fields. The setting is read back before enabling
the mapping. POW serves only as BACK while firmware runs.

The X-Powers AXP173 datasheet (pp. 36, 41-42; [manufacturer-authored PDF mirror](https://dl.linux-sunxi.org/AXP/AXP173%20Datasheet%20v1.12_cn.pdf),
[readable mirror](https://manuals.plus/m/ae4d7772bf0616d33560f2abd2d632f80a8cb3513e08ea2b6337a69dad7dfc26))
defines IRQ enable register `0x42` bit 1 for short PEK, and W1C status `0x46`
bit 1 for short / bit 0 for long. Both PEK IRQ classes are enabled; setup preserves every other enable bit and
clears stale PEK events so booting with POW does not immediately navigate back.
Polling uses the independent I2C1 PMIC bus every 20 ms; errors back off to
100 ms. One latched event is delivered once even if its acknowledgement fails.
Only the two PEK status bits are acknowledged; unrelated IRQ status survives.
A combined short/long latch produces one BACK edge, not repeats. The PMIC
reports classified press events rather than physical release, so no held BACK
state is invented. `hk_input_info_t.pulse_buttons` exposes this limitation;
BUTTON TEST uses one BACK to exit, while MicroPython STOP already uses its
BACK press edge.
The short event becomes a pulse long enough for the portable input debounce.
During POW/BACK setup the only power-policy change is `0x36` bit 3.
Sleep/wake `0x31`, shutdown `0x32`, rail and charger settings are untouched
until an explicit POWER OFF confirmation. On 2026-10-29 the user confirmed
short POW exits BUTTONS and an eight-second hold returns back without powering
off on the corrected `9fa472d9...` image.

## POWER OFF app

Cube adds POWER OFF beside SLEEP. Launching it never cuts power: the screen
requires a fresh OK press after the menu OK has been released; BACK cancels.
It uses the public Power capability, available only with the AXP173 binding.
SEN0305 excludes the app because it has no controllable power-off provider.

After confirmation, shutdown waits up to two seconds for an active MicroPython
run to stop, flushes dirty settings, closes any userfs upload and unmounts userfs.
Only then does the PMIC driver set register `0x32` bit 7, preserving the other
shutdown/charger-LED fields (X-Powers datasheet p.35). A STOP timeout, failed
settings flush or filesystem operation prevents the power command. A failed
PMIC write remounts userfs and leaves an error screen with BACK/OK recovery.
Host tests cover confirmation, held-launch OK, cancellation, STOP, save failure,
PMIC failure/recovery and deadline. Actual power-off should be checked last in
hardware acceptance. POW navigation and long-hold behavior were confirmed by
the user on the corrected image. On 2026-10-29 the user also confirmed that
POW cancels POWER OFF and a fresh center OK powers the Cube off. Separate
battery-only versus USB-powered shutdown coverage was not established.
Camera/LCD PASS evidence above belongs to the earlier installed DMA image.

## Reset-entry regression guard

The first POWER OFF build did not boot: its C callback named `start` was
compiled into `.text.start`, which the SDK linker reserves for CRT reset code.
That callback preceded `_start` by 0x68 bytes in the raw image. K210 flash boot
starts at 0x80000000 and cannot use the ELF entry address, so execution reached
a C function before stack/BSS initialization. Renaming the callback to
`power_off_start` restores `_start` at the image base. This was a binary layout
fault before PMIC setup; it was not evidence of a PMIC rail change.

`build_firmware.py` now rejects an ELF unless its first load address, entry
address and `_start` symbol all equal 0x80000000, before publishing sidecars or
attestations. Host fixtures cover the displaced entry and a misleading ELF
entry header. The PMIC receive-FIFO drain also has a timeout. Physical boot and
POW/POWER OFF acceptance must use the corrected image, not the failed build.

The corrected image was installed successfully on 2026-10-29: 1,549,752 bytes,
SHA-256 `9fa472d9cb47db3a542fbe1dec30b798cd517616ce2aa056b7ad4d15fbf27739`.
`build/cube-entry-fix-boot.log` records verified POW/BACK policy, settings loaded
from slot 0 (sequence 1), all thirteen apps and successful menu startup.
HKPING and BUTTONS launch also passed. Both board builds, architecture checks
and all 241 host tests passed. After reconnecting USB, the app smoke run passed
launch/return for Terminal, Camera, QR, Face Detect, AprilTag, Object Detect,
Files, Pong, Settings, MicroPython and Sleep; BUTTONS and POWER OFF were checked
separately. See `build/cube-apps-smoke.log`, `build/cube-terminal-smoke.log`
and `build/cube-poweroff-open.log`. These are launch checks, not complete
recognition acceptance: no SD/models were available, no known QR/tag target
was presented, and QR currently requests its existing 640x480 capture mode
(320x240 decoder), unlike the 320x240 default camera mode.

`build/cube-pow-hmpy.json` records PASS for file upload/readback, script run,
STOP (122-125 ms), interpreter reuse, lease reconnect and cleanup on this
installed image. The user confirmed POW short/long BACK and POWER OFF
cancel/confirmed shutdown. SD, AI models, external UART/I2C loopback, all light
channels, physical MicroPython STOP/WDT recovery and cold-start persistence
still need hardware acceptance. The board remains non-releaseable.
