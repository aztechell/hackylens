# UART Logger on Maix Cube and SEN0305

UART Logger is the second HackyLens workload for comparing a MicroPython
prototype with a native C port. Both applications receive an external UART
stream, plot the last 96 samples and optionally write a CSV on microSD.
The chart updates up to 10 times per second. OK starts a recording, then
publishes it on a second press. BACK (POW on Cube) returns to the menu and
cancels an unfinished recording. Published files use `/PYDATA/TLxxxxxx.CSV`.
The screen shows the latest value, missing sequence count and recording state.

The wire protocol is UART 115200 8N1. A packet begins with `A5 5A`, followed
by a little-endian unsigned sequence and a little-endian signed value, both
16-bit. A little-endian CRC-16/CCITT-FALSE covers the four payload bytes.
Sequence arithmetic wraps at 65536. A forward distance of 1 through 32767
advances the sequence. Duplicates and older samples leave the chart and CSV
unchanged. In a corrupt packet the receiver searches for the next sync marker.

## Reproduce on Cube

Use a FAT32 microSD card. Connect NodeMCU D1 (GPIO5, TX) to Cube SP-MOD IO6
(RX), D2 (GPIO4, RX) to IO7 (TX), and join GND. Power each board over USB.
The example ports are COM5 for Cube and COM3 for NodeMCU. The ESP8266 build
uses the Arduino ESP8266 3.1.2 package and its Xtensa compiler.

```powershell
. .\env.ps1
python tools/build_firmware.py full --board sipeed-maix-cube --disable-app sky-glide --require-app uart-logger --uart-logger-bench
python tools/uart_logger_fixture.py COM5 build/sipeed-maix-cube/hackylens-full-uartbench.bin
python tools/hkflash.py cmd --board sipeed-maix-cube --port COM5 HKLINKSPMOD
python tools/uart_logger_bench.py --cube-port COM5 --sender-port COM3 --esp-core $espCore --esp-gcc $espCompiler --image build/sipeed-maix-cube/hackylens-full-uartbench.bin --out build/uart-logger/cube --memory-run
```

Set `$espCore` to the Arduino `hardware/esp8266/3.1.2` directory and
`$espCompiler` to the full path of `xtensa-lx106-elf-gcc` in that package's
toolchain. The loader runs the diagnostic image from Cube RAM. The runner
loads the sender into NodeMCU RAM and calibrates its cycle counter. The runner
continues an earlier series when given the same output directory, board, TX
pin, firmware image and Python source. It saves each trial as JSONL, plus a
CSV read from the card for each recording trial. Run physical button checks
after the automated series.

Build the everyday UART Logger image with `--uart-logger-menu` instead of
`--uart-logger-bench`. Use `tools/hkflash.py flash` with this image and the
Cube board and port. Select SP-MOD under **Settings → Connection → Port**.
Upload
[`uart_logger.py`](../examples/uart_logger.py) over HMPY. In **MICRO-PYTHON**,
select the script and choose **PIN TO MENU**. The native menu entry is
**UART LOGGER C**.

## Reproduce on SEN0305

Use the Gravity port and a FAT32 microSD card. The tested wiring puts NodeMCU
D2 (GPIO4, TX) on SEN0305 `R` (IO34) and D1 (GPIO5, RX) on `T` (IO35). Join
GND and power each board over USB. Set the sender's TX option to match the wire
attached to `R`. The logger borrows the connector in UART mode and restores the
configured External Link mode on exit.

Build the diagnostic image and boot it on SEN0305 before starting the runner.
The SEN0305 measurements below used a RAM boot. The runner verifies each CSV
against a reference and records the display timing for each trial.

```powershell
. .\env.ps1
python tools/build_firmware.py full --board huskylens-sen0305 --require-app uart-logger --uart-logger-bench
python tools/uart_logger_bench.py --board huskylens-sen0305 --port COM10 --sender-port COM3 --sender-tx D2 --esp-core $espCore --esp-gcc $espCompiler --image build/huskylens-sen0305/hackylens-full-uartbench.bin --out build/uart-logger/sen --memory-run
```

## Measurement

- The parser replays 4096 encoded packets in memory on each path. Time starts
  after packet generation and covers byte parsing, validation, sequence
  tracking and the history update. Both paths also accumulate a digest.
- The NodeMCU sends 1600 packets at 50 Hz in each live trial. The first
  100 packets fill the history during warm-up. In recording trials the
  remaining 1500 samples go in the CSV. The runner computes the expected
  digest and CSV independently on the PC. Each combination of language and
  recording mode runs five times, reversing the case order on even rounds.
- Frame timing comes from successful provider returns after the LCD transfer.
  Software latency begins when the application accepts the latest sample used
  in that frame. It includes scheduling and rendering after acceptance.
  Each run has a 30-second measurement window and a target of 300 frames.
- The native footprint compares everyday Cube builds with and without
  `--disable-app uart-logger`, holding the other settings fixed. Use
  `riscv64-unknown-elf-size -A` for sections. The Python script occupies USERFS.
  The separate memory run samples GC usage on each loop iteration and
  reports live bytes before the loop and after collection. The VM reserves a
  128 KiB GC arena.

## Cube results

[Raw runs and LCD traces](research/uart-logger-cube-runs.jsonl.gz) ·
[Build and hardware evidence](research/uart-logger-cube-evidence.json)

### Parser replay

| Path | Runs | 4096 packets, median ms | Packets/s |
| --- | ---: | ---: | ---: |
| C | 5 | 8.495 | 482,166 |
| MicroPython | 5 | 3500.385 | 1,170 |

All runs produced digest `1974799667`. The parser runs about 412 times
faster in C in this replay.

### Live UART and SD

Each combination has five valid trials. Counts and FPS are medians across
those trials. Timing columns show the median of the per-trial p50 and p95
and the largest observed value.

| Path | Mode | FPS | Frames | Missed | Frame interval p50 / p95 / max, ms | Sample to LCD p50 / p95 / max, ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| C | Chart | 9.87 | 296 | 4 | 101.16 / 102.68 / 103.02 | 43.64 / 53.63 / 55.24 |
| MicroPython | Chart | 9.67 | 290 | 10 | 102.63 / 104.24 / 118.19 | 70.27 / 86.02 / 87.88 |
| C | Chart + CSV | 9.83 | 295 | 5 | 101.48 / 104.44 / 139.87 | 43.97 / 53.91 / 76.74 |
| MicroPython | Chart + CSV | 9.53 | 286 | 14 | 102.80 / 112.35 / 170.91 | 70.51 / 86.06 / 138.15 |

| Path | Display provider p50 / p95 / max, chart, ms | Display provider p50 / p95 / max, chart + CSV, ms |
| --- | ---: | ---: |
| C BASE | 37.64 / 37.93 / 38.10 | 37.91 / 38.19 / 38.37 |
| MicroPython OVERLAY | 29.91 / 29.98 / 30.05 | 30.02 / 30.09 / 30.16 |

Both versions accepted every sample with zero CRC, sequence and UART errors.
All ten benchmark CSVs matched the independent 13,173-byte reference,
SHA-256 `c5122431bbc8e28ee6a36cd9319e578074cecebe55f53b842f05d76d7287d985`.
Here the 50 Hz input is well within both implementations' parser capacity.
The visible difference is roughly 26 ms in median sample-to-LCD latency.
The Python bridge stages rendering commands on core 0, while native drawing
happens in the C render callback. The provider column measures only the
configured display path's final transfer and associated work.

The raw log retains setup attempts affected by the UART sender's initial
clock calibration, a CSV download header parser error and malformed HMPY
upload framing. All scored runs used the calibrated sender and the same
diagnostic firmware.

### Footprint

| Build | BIN bytes | ELF .bss bytes | ELF .data bytes |
| --- | ---: | ---: | ---: |
| Without C logger | 1,655,032 | 3,067,840 | 8,896 |
| With C logger | 1,658,104 | 3,068,864 | 8,896 |
| C logger delta | +3,072 | +1,024 | 0 |

The C version also builds with `--disable-app micropython --require-app
uart-logger`. The production script takes 11,505 bytes in USERFS. Counting
all lines, including measurement code and blank lines, the Python script
has 340 lines. The C application has 413 lines plus 179 in the core and
its header, and 10 in the manifest.

In the separate chart + CSV memory run, MicroPython had 17,024 bytes live
before the loop, reached an observed peak of 129,728 bytes, and returned
to 17,184 bytes after collection. The peak is a sample of the allocated
heap and includes objects waiting for collection.

### Build and launch

On the test PC, a clean local C build took 34.7 seconds. Writing the
firmware partition and rebooting took 43.7 seconds. The native entry opened
on the installed image. The Python script uploaded in 7.9 seconds over
HMPY at 115200. Upload, byte-for-byte readback and verification that the VM
was running took 15.6 seconds in total.

The Cube was left on the everyday `0.9.0-spmod` image. Its menu includes
**UART LOGGER C** and USERFS contains `uart_logger.py`. The Sky Glide files
were backed up before removal from the board.

### API checks on Cube

The storage probe rejected an invalid filename and a 257-byte append, then
cancelled a pending write twice and reused the filename with `sd_write()`.
The published CSV contained exactly the new bytes. The legacy camera
path also saved a 240×320 BMP. The stock UART answered the same PING
before and after the C logger borrowed the connector. The original
External Link setting was restored to I2C with a saved UART speed of 115200.

### Physical controls

On the installed image, the operator confirmed the live C chart, recording
through `SAVED`, exit through POW and reopening from the menu. The operator
also pinned the MicroPython script and confirmed the shortcut, live chart,
recording and exit through POW.

## SEN0305 results

[Raw runs and LCD traces](research/uart-logger-sen-runs.jsonl.gz) ·
[Build and hardware evidence](research/uart-logger-sen-evidence.json)

Five parser replays per implementation accepted all 4,096 packets and
produced digest `1974799667`. The median was 8.461 ms in C (484,104 packets/s)
and 3,491.059 ms in MicroPython (1,173 packets/s).

There were five live runs per language and mode, plus one Python memory
run. Each accepted all 1,600 packets at 50 Hz. Across 33,600 packets there
were zero CRC errors, sequence gaps, stale packets or UART errors. All
eleven recordings matched the 13,173-byte reference byte for byte.

| Path | Mode | FPS | Frames | Missed frames | Frame interval p50 / p95 / max, ms | Sample to LCD p50 / p95 / max, ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| C | Chart | 9.63 | 289 | 11 | 104.44 / 104.84 / 107.16 | 53.45 / 63.39 / 65.94 |
| MicroPython | Chart | 9.20 | 276 | 24 | 108.50 / 110.81 / 113.48 | 77.02 / 92.30 / 94.80 |
| C | Chart + CSV | 9.67 | 290 | 10 | 104.53 / 105.02 / 154.03 | 55.24 / 63.70 / 94.98 |
| MicroPython | Chart + CSV | 9.03 | 271 | 29 | 108.68 / 116.12 / 262.17 | 77.23 / 92.40 / 245.68 |

The median final LCD transfer in the chart run was 47.75 ms on the C base
plane and 37.41 ms on the Python overlay plane. The sample-to-LCD measure
also includes the time from sample acceptance to the start of the transfer.
In the separate Python memory run, 17,024 bytes were live before the loop,
the observed peak was 129,792 bytes and 17,184 bytes remained after GC.

An independent C check sent 180 packets on the installed image and
compared the 384 plotted pixels for the last 96 samples with the reference.
The screenshot command reads the BASE shadow. On the same installed image
a temporary probe of the Python play path accepted 180 packets with the
reference digest, displayed the last 96 samples and completed the OVERLAY
present call. The probe was then removed from USERFS.

After the timed runs, the installed firmware was started and the five
temporary scripts were removed. The 11 pre-existing scripts were still
listed. On the same Gravity wires, NodeMCU sent an I²C PING to SEN0305 at
`0x32` and verified the PONG payload and CRC. The benchmark CSVs remain on
microSD. Manual confirmation of OK and BACK on SEN0305 is pending.

## Porting observations

The byte-level parser, sequence rules, 96-value ring and CSV bytes port
directly from [Python](../examples/uart_logger.py) to the
[C core](../firmware/src/apps/uart_logger/uart_logger_core.c). Host tests
compare records and drawing commands through split packets, bad CRCs,
duplicates, skipped sequences and wraparound.

The [native app](../firmware/src/apps/uart_logger/uart_logger_app.c)
moves initialization and connector acquisition into `start`, bounded reads
and writes into Timer events, OK and BACK into Input events, and drawing
into `render`. It declares services in its manifest. The firmware supplies
the board drivers when linking the app.

With `sd_begin`, `sd_append`, `sd_finish` and `sd_cancel`, the prototype
uses the same recording states and short-write behavior as the native API.
The app can receive samples between storage operations on both paths.
Closing the foreground claim lets the stock External Link resume its
configured transport and speed. The same pattern applies to other streaming
apps. See [Porting a MicroPython prototype to C](PYTHON_TO_C.md).
