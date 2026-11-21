# Sky Glide on K210

Sky Glide is the first HackyLens workload for comparing native C and
MicroPython. The game uses the same replay, fixed-point physics and RGB565
rectangle sequence in both implementations. Press OK to flap or restart
after a collision. Press BACK to exit.

The native app is listed as **SKY GLIDE C**. For MicroPython, upload
[`sky_glide_py.py`](../examples/sky_glide_py.py), select it in **MICRO-PYTHON**
and choose **PIN TO MENU**. The script uses the three-call Display path on
firmware 0.9.0 and prefers the new frame call when it is available.
On 0.9.0 use the pinned shortcut for gameplay so the script receives OK.

## Reproduce the measurements

Build the diagnostic firmware for the connected board. The image has a
board-validated flash sidecar and stays under `build/`.

```powershell
. .\env.ps1
python tools/build_firmware.py full --board sipeed-maix-cube --sky-glide-bench
python tools/hkflash.py flash build/sipeed-maix-cube/hackylens-full-skybench.bin --board sipeed-maix-cube --port COM5
python tools/sky_glide_bench.py --board sipeed-maix-cube --port COM5 --output build/sky-glide-cube.jsonl
python tools/sky_glide_bench.py --summary build/sky-glide-cube.jsonl
```

For SEN0305, substitute `huskylens-sen0305` and its runtime port. Record
the input benchmark separately by adding `--modes input`. Press the physical
OK button 20 times for each prompted run, with a release between presses.
The glider changes height for each recognized press. Wait for the next scene
before starting a new group.
Use `--install` to upload the playable script. The runner reuses only exact
matches of its content-addressed test files and resumes missing cases when
given the same output file.

- Logic replays 128 batches of 256 fixed steps. C sums its active batch
  times, excluding the waits between timer dispatches. All paths check the
  state after each batch and emit one accumulated digest.
- The paced replay measures 30 seconds after two seconds of warm-up.
  The burst replay takes 300 more frames after 50 warm-up frames. A
  diagnostic probe records successful Display provider returns and their
  time spans. Both versions clear the whole board display, with the same
  centered 240×240 scene. Counts come from completed transfers. Keepalive
  packets keep the Python command session open, with no frame telemetry
  sent during the window.
- The input scene holds a static pose between presses. The timestamp is
  taken when the app observes a debounced rising edge, and matched to the
  first completed frame that follows it. This measures software response
  and the completed LCD transfer after app detection.
- For the footprint comparison, build `full` with and without
  `--disable-app sky-glide`. Compare the raw BIN sizes and ELF
  `.data` and `.bss` using the toolchain's `riscv64-unknown-elf-size -A`.
  Python measures GC bytes between complete frames in a separate run.
  The observed heap peak is sampled and includes the script's live VM
  objects. The firmware reserves a 128 KiB GC arena for every script.

C runs in the native runtime on core 0. MicroPython runs on core 1 and
submits display work to core 0 through the capability bridge. C presents
through the BASE plane and Python through OVERLAY. The headless workload
measures the implementations of the same game step. The display workloads
also include scheduling, command staging and the two presentation paths.
The C build uses the firmware's `-Os` flags. The Python replay starts over
HMPY from the launcher and keeps that foreground state for the measurement.
A paced replay may skip a render while advancing to the correct simulation
step. The burst replay submits each of the same 300 measured scenes.

## Results

[Raw runs and display traces](research/sky-glide-runs.jsonl.gz) · [Build and launch evidence](research/sky-glide-evidence.json)

Tables use the median across completed runs. The frame interval columns show
the median of each run's p50 and p95, followed by the highest observed
interval. All display durations include the provider's work around the LCD
transfer.

### Fixed-step logic

| Board | Path | Runs | 32,768 steps, ms | Steps/s |
| --- | --- | ---: | ---: | ---: |
| SEN0305 | C | 5 | 6.09 | 5,377,092 |
| SEN0305 | MicroPython three calls | 5 | 2155.85 | 15,200 |
| SEN0305 | MicroPython one call | 5 | 2155.85 | 15,200 |
| Maix Cube | C | 5 | 6.11 | 5,364,768 |
| Maix Cube | MicroPython three calls | 5 | 2157.59 | 15,187 |
| Maix Cube | MicroPython one call | 5 | 2157.59 | 15,187 |

Every run produced digest `1f4800`.

### 30-second replay at 25 FPS

| Board | Path | Runs | FPS | Completed | Missed | Frame interval p50 / p95 / max, ms | Display p50 / p95, ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| SEN0305 | C | 5 | 20.53 | 616 | 134 | 48.76 / 49.06 / 51.30 | 47.58 / 47.87 |
| SEN0305 | MicroPython three calls | 5 | 10.17 | 305 | 445 | 98.98 / 101.08 / 101.35 | 38.69 / 38.77 |
| SEN0305 | MicroPython one call | 5 | 16.97 | 509 | 241 | 58.90 / 59.01 / 61.72 | 38.70 / 38.80 |
| Maix Cube | C | 5 | 24.60 | 738 | 12 | 40.67 / 40.94 / 42.55 | 39.06 / 39.34 |
| Maix Cube | MicroPython three calls | 5 | 12.07 | 362 | 388 | 73.31 / 93.27 / 95.32 | 31.67 / 31.77 |
| Maix Cube | MicroPython one call | 5 | 19.13 | 574 | 176 | 52.27 / 52.40 / 54.18 | 31.67 / 31.78 |

### 300-frame unpaced replay

| Board | Path | Runs | FPS | Completed | Missed | Frame interval p50 / p95 / max, ms | Display p50 / p95, ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| SEN0305 | C | 5 | 20.52 | 300 | 0 | 48.72 / 49.06 / 51.30 | 47.52 / 47.87 |
| SEN0305 | MicroPython three calls | 5 | 10.20 | 300 | 0 | 98.98 / 101.02 / 240.45 | 38.69 / 38.78 |
| SEN0305 | MicroPython one call | 5 | 16.97 | 300 | 0 | 58.89 / 59.01 / 61.26 | 38.69 / 38.80 |
| Maix Cube | C | 5 | 24.61 | 300 | 0 | 40.65 / 40.94 / 42.54 | 39.04 / 39.35 |
| Maix Cube | MicroPython three calls | 5 | 12.10 | 300 | 0 | 73.27 / 93.27 / 94.92 | 31.67 / 31.78 |
| Maix Cube | MicroPython one call | 5 | 19.12 | 300 | 0 | 52.27 / 52.40 / 54.31 | 31.66 / 31.78 |

### Memory

| Board | Native BIN delta, B | Native .bss delta, B | Native .data delta, B |
| --- | ---: | ---: | ---: |
| SEN0305 | +2,752 | +1,024 | +8 |
| Maix Cube | +2,688 | +1,024 | -56 |

| Board | Python path | Runs | Live before, B | Sampled peak, B | Live after collection, B |
| --- | --- | ---: | ---: | ---: | ---: |
| SEN0305 | MicroPython three calls | 5 | 10,496 | 129,120 | 10,496 |
| SEN0305 | MicroPython one call | 5 | 10,400 | 129,024 | 10,400 |
| Maix Cube | MicroPython three calls | 5 | 10,496 | 129,120 | 10,496 |
| Maix Cube | MicroPython one call | 5 | 10,400 | 129,024 | 10,400 |

### Physical buttons

| Board | Path | Runs | Presses | Edge to LCD p50 / p95 / max, ms |
| --- | --- | ---: | ---: | ---: |
| SEN0305 | C | 5 | 100 | 47.50 / 47.52 / 47.52 |
| SEN0305 | MicroPython three calls | 5 | 100 | 44.13 / 44.74 / 46.94 |
| SEN0305 | MicroPython one call | 5 | 100 | 42.01 / 42.57 / 44.64 |
| Maix Cube | C | 5 | 100 | 38.07 / 38.08 / 38.09 |
| Maix Cube | MicroPython three calls | 5 | 100 | 36.41 / 40.59 / 95.31 |
| Maix Cube | MicroPython one call | 5 | 100 | 34.30 / 37.63 / 93.00 |

## Interpretation

Both boards completed the API probe for microsecond ticks, explicit
`count=None`, malformed frames, the 255-rectangle limit and the existing
`display_present()` call. Manual play exercised C and Python on both boards.
UART state showed the Python process still RUNNING after a physical OK on both
boards. The pinned shortcut launched and returned through POW on Cube
and BACK on SEN0305 without a traceback.

The first manual Python run exposed an input conflict. The script manager
accepted OK as a stop request while a Python display frame was visible.
The manager now hands OK to a script once it presents a display frame.
Requested termination also omits the synthetic `KeyboardInterrupt` traceback.
The host regression tests cover both paths.

Native logic took about 0.19 µs per fixed step. MicroPython took about
66 µs per step. Two game steps therefore account for approximately 0.13 ms
of a 40 ms frame budget in MicroPython. The end-to-end frame rate depends
mostly on Display and the path used to reach it.

The combined call saves two synchronous trips between the interpreter and
the display owner. Median Display provider time for the two Python paths
agrees within roughly 0.1 ms on each board. The difference in complete
frame intervals comes mainly from the work between provider calls. The
three-call path has wider variation in delivery intervals across runs.
SEN0305 burst rates ranged from about 10 to 19 FPS with a worst frame
interval of 240 ms. The one-call path stayed near 17 FPS.

The provider also applies the native draw list to its BASE shadow after
transfer. The overlay path retains its draw commands. The trace includes
both of these costs. On SEN0305 a native full-screen provider call alone
exceeds the 40 ms budget. Reaching 25 FPS with this workload would require
reducing the area transferred per frame or shortening the display path.

The isolated input scene starts the clock after debounced input reaches
the app. Python's median time from that point to LCD completion is lower
than C's on both boards, consistent with the shorter OVERLAY provider
call. Cube's first Python response in each batch took about 92 to 95 ms.
The other presses were typically much shorter, so the maximum is
important alongside the median and p95.

The C implementation uses a manifest and SDK callbacks and compiles into
the firmware. The Python implementation ships as one USERFS script that
can be updated without a firmware rebuild. The study source including its
bench modes occupies 456 nonblank native lines across the app files and
255 nonblank lines in Python. Python's live heap after collection is stable
in the memory runs, with transient allocations approaching the reserved
arena size during repeated draws.

The clean native builds took about 31 seconds on Cube and 34 seconds on
SEN0305 on the study host. The diagnostic USB flashes took about 43 and
75 seconds respectively at the recorded programming settings. A fresh
SEN0305 USERFS upload of the 8,979-byte script took 1.73 seconds. The HMPY
run reached RUNNING 0.18 seconds after its request. These are individual
deployment measurements on the same host.

The firmware's minimal MicroPython profile omits string formatting with
the percent operator. The benchmark uses positional arguments to `print()`
and uses the existing public input and display methods during gameplay.
