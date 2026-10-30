# K210 ISP stubs

The board-specific binaries are bundled from
[`aztechell/huskylens-isp-stub`](https://github.com/aztechell/huskylens-isp-stub).

- Source commit: [`666f5a57f2415b6b16fc0427bda01770d9bd23d2`](https://github.com/aztechell/huskylens-isp-stub/commit/666f5a57f2415b6b16fc0427bda01770d9bd23d2)
- Build: `python tools/build.py --board all`, pinned toolchain `8.2.0-20190409`
- License: Apache-2.0, reproduced in `LICENSE`

| Board | Binary | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| HuskyLens SEN0305 | `isp_prog_huskylens.bin` | 17,600 | `12b9a22619b4e798b467a23f71c729904280f3561f4abf3e3475877cc5a2eb3b` |
| Sipeed Maix Cube 2757 | `isp_prog_cube.bin` | 18,048 | `a06131d21fd2208a8d2472f5875ec7249b1c8d7d3d3d6ec6fc9cbc794a8bd121` |

The upstream [artifact manifest](artifacts.json) is copied alongside the
binaries. `hkflash.py` selects the stub via `programming.isp_stub` in the board
descriptor; `--isp-stub` remains an explicit override. No runtime board
auto-detection or kflash protocol extension is used.

HuskyLens uses a 320x240 serial ST7789 display. Cube uses a 240x240 panel with
its own pin routing, active-low backlight and parallel SPI0/DVP DMA transport.
Both builds and native host tests passed. Cube SRAM startup and full firmware
write/reboot were checked on COM15. The BSP enables the DMA clock and accepts
normal source/destination completion flags; only DMA error flags disable
rendering. On 2026-10-30, the user confirmed the waiting screen and increasing
percentages/filling progress bar during firmware programming at 460800 baud.
HuskyLens flashing regression remains a separate hardware acceptance step.

The protocol, SLIP implementation, and flash driver derive from
`loboris/ktool` commit `0345aa90d9b3830641373fb4e3ce4edf45d0a46f`.
The pinned Kendryte SDK baseline is
`02576ba67e8797444f3ee3f34c625b5ed048e707`.
