# K210 ISP stubs

The board-specific binaries are bundled from
[`aztechell/huskylens-isp-stub`](https://github.com/aztechell/huskylens-isp-stub).

- Source commit: [`7d3a740af7879bfcc96e7f183f1b127e5190ce39`](https://github.com/aztechell/huskylens-isp-stub/commit/7d3a740af7879bfcc96e7f183f1b127e5190ce39)
- Build: `python tools/build.py --board all`, pinned toolchain `8.2.0-20190409`
- License: Apache-2.0, reproduced in `LICENSE`

| Board | Binary | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| HuskyLens SEN0305 | `isp_prog_huskylens.bin` | 17,536 | `7820a83823e572d84d43b3167b45fdc61df7c7dcff772f917c2d2742a3a4dc0e` |
| Sipeed Maix Cube 2757 | `isp_prog_cube.bin` | 18,048 | `4f57b2c924c473fcaf448640083876e78fb06fb831477b82048bd0baf665220a` |

The upstream [artifact manifest](artifacts.json) is copied alongside the
binaries. `hkflash.py` selects the stub via `programming.isp_stub` in the board
descriptor; `--isp-stub` remains an explicit override. No runtime board
auto-detection or kflash protocol extension is used.

HuskyLens uses a 320x240 serial ST7789 display. Cube uses a 240x240 panel with
its own pin routing, active-low backlight and parallel SPI0/DVP DMA transport.
Both builds and native host tests passed; physical Cube progress display and
HuskyLens flashing regression remain separate hardware acceptance steps.

The protocol, SLIP implementation, and flash driver derive from
`loboris/ktool` commit `0345aa90d9b3830641373fb4e3ce4edf45d0a46f`.
The pinned Kendryte SDK baseline is
`02576ba67e8797444f3ee3f34c625b5ed048e707`.
