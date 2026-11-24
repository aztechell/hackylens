# Object Gate on Maix Cube

Object Gate is the third HackyLens comparison of a MicroPython prototype
and a native C port. It verifies that a person is in the central zone of
the camera preview. Both versions use the installed YOLOv2 VOC20
`object20` model and the detector service shared with OBJECT DETECT.
The comparison measures the two applications around that existing
detector.

The cyan outline marks the middle half of the frame in each direction.
An amber outline shows a person whose box center is inside the zone and
whose confidence is at least 500 out of 1000. The outline turns green after
three consecutive matches. Three consecutive misses rearm the gate for
another entry. OK resets the gate. BACK exits, mapped to a short POW press
on Maix Cube.

- [MicroPython prototype](../examples/object_gate.py)
- [Native app](../firmware/src/apps/object_gate/object_gate_app.c)
- [Native state rules](../firmware/src/apps/object_gate/object_gate_core.c)
- [Model identity, tensor contract and SD paths](AI_MODELS.md#object-detect)

## Run on Cube

Put the tracked `sdcard/hackylens.kmodels/object20` directory on a FAT32
card at `/hackylens.kmodels/object20`. Upload `object_gate.py` over HMPY
and run it from MICRO-PYTHON, or pin it in the launcher. Loading the
model takes several seconds. The native reference is registered as
`object-gate` and can be launched on a build containing it with
`HKAPP object-gate` on the debug UART.

## Reproduce the same-input comparison

```powershell
. .\env.ps1
python tools/object_gate_replay.py tests/fixtures/object_gate_person.rgb
python tools/uart_logger_fixture.py COM5 build/sipeed-maix-cube/hackylens-object-gate.bin
python tools/object_gate_bench.py --port COM5 --image build/sipeed-maix-cube/hackylens-object-gate.bin --out build/object-gate-runs.jsonl
```

For SEN0305, the runtime port in this test is COM10.

```powershell
. .\env.ps1
python tools/object_gate_replay.py tests/fixtures/object_gate_person.rgb --board huskylens-sen0305
python tools/object_gate_ram.py COM10 build/huskylens-sen0305/hackylens-object-gate.bin
python tools/object_gate_bench.py --board huskylens-sen0305 --port COM10 --image build/huskylens-sen0305/hackylens-object-gate.bin --out build/object-gate-sen-runs.jsonl
```

The diagnostic image runs from RAM. It cycles three dark input tensors,
six tensors with a person, and three dark tensors. The substitute pixels
enter the existing model input buffer before KPU submission. Frame leases,
the camera preview and display output still run on the physical board.
The preview image comes from the camera sensor, so its background need
not match the diagnostic input. `HKOGREPLAY OFF` resumes live inference.

The person fixture contains three 144×180 color planes. It is a
bilinear resize of crop `(110, 35, 455, 465)` from the NASA photograph
of Eileen Collins distributed with scikit-image. Fixture SHA-256 is
`8b1b522a258b064cea39d4d2d5216d222d1fd590151daa1c3c6e4be432e7f347`.
[scikit-image fixture source and attribution](https://scikit-image.org/docs/stable/api/skimage.data.html#skimage.data.astronaut)

Each run opens `object20`, processes four warm-up frames, then measures the
next 24. The runner reverses the order of C and MicroPython in every other
pair. The digest covers canvas dimensions and all decoded detection fields.
Both implementations also count confirmed entries and completed previews.

The shared model timer measures from inference submission to its completion
callback. Detection API time also includes input preparation, detection
postprocessing and waiting for the result at the application boundary.
Policy time surrounds the gate rule alone. Preview time surrounds the camera
present call. End-to-end throughput also includes the waits between frames.

## Results on Cube

[Raw runs](research/object-gate-cube-runs.jsonl) ·
[Live-camera and build evidence](research/object-gate-cube-evidence.json)

Five runs per implementation completed successfully. Both confirmed
two entries and presented all 24 measured frames in every run. Each
produced the same decoded-result digest, `3065711780`. Host tests also
compared the chosen rectangle, color, occupancy state and entry count
through mixed detections, intermittent misses and re-entry.

| Metric | C | MicroPython |
| --- | ---: | ---: |
| Complete frames/s | 11.46 | 9.11 |
| Shared model run to callback, ms/frame | 12.686 | 12.687 |
| Detect API, ms/frame | 44.96 | 49.86 |
| Gate decision, µs/frame | 4.0 | 102.3 |
| Camera present, ms/frame | 30.76 | 35.78 |
| Open model from SD, s | 4.10 | 4.72 |

Values are medians across the five runs. Per-frame timings are averaged
within each run. The KPU path is effectively equal across the two
implementations. C completed about 26% more frames per second. The gate
decision accounts for about 0.10 ms of the per-frame difference. Most of
the measured difference is in the calls around inference, presentation
and scheduling.

In normal Cube builds, linking the native app adds 2368 bytes to the
BIN, 1024 bytes to `.bss` and 32 bytes to `.data`. The Python source is
4285 bytes. A separate short live-camera run observed 3936 bytes live
before capture, 9440 bytes allocated at the highest sample, and 4256
bytes after collection in the Python heap. The shared model and planar
input use firmware memory outside the Python GC arena.

For the footprint comparison, build the same `--disable-app sky-glide`
profile twice, adding `--disable-app object-gate` to the
baseline. Save its BIN and ELF before building the variant with Object
Gate. Compare the raw BIN sizes and the ELF sections with
`riscv64-unknown-elf-size -A`.

With the replay disabled, the C app opened, processed live sensor frames
and released the camera on exit. After Cube booted the usual firmware
from flash, the Python probe completed eight measured live previews.
The live scene exercised the empty zone. The matched replay exercised
positive detections, confirmation and rearming. The native app was also
built for SEN0305 with MicroPython disabled.

On the test PC, a clean Cube build with the native app took 30.2 s.
Uploading the playable Python source over HMPY took 3.25 s. After
readback and model startup, the running script was verified at 12.0 s.
The Cube was returned to its usual flash image with `object_gate.py`
installed in USERFS. The native hardware runs used the RAM image.

## Results on SEN0305

[Raw runs](research/object-gate-sen-runs.jsonl) ·
[Live-camera and boot evidence](research/object-gate-sen-evidence.json)

Five runs per implementation presented all 24 measured frames and
confirmed two entries in each run. The decoded-result digest was
`809083552` for every run. Comparisons are paired within each board.
SEN0305 reports a 320×240 logical canvas, while Cube reports 240×320.
Canvas dimensions and logical box coordinates are part of the digest.

| Metric | C | MicroPython |
| --- | ---: | ---: |
| Complete frames/s | 8.08 | 7.92 |
| Shared model run to callback, ms/frame | 12.96 | 12.68 |
| Detect API, ms/frame | 52.80 | 57.06 |
| Gate decision, µs/frame | 4.1 | 101.3 |
| Camera present, ms/frame | 37.75 | 42.54 |
| Open model from SD, s | 7.15 | 7.51 |

C completed about 2% more frames per second on SEN0305 in this series.
The complete window includes camera acquisition and scheduling between
two previews, alongside the individually timed parts in the table.

With replay off, the native app processed live frames from the OV2640
and returned to the menu. MicroPython completed eight measured live
previews. The live scene exercised the empty zone, and the replay
exercised the two positive entries. The temporary test script was
removed, and the board booted its previous `0.9.0` firmware from flash.

## Portability

Both implementations keep the same class ID, confidence scale, logical
coordinates, three-frame rule and preview colors. Python runs a loop over
camera leases. C moves camera startup into `start`, then advances model
loading and inference through the existing SDK calls on Timer events. The
manifest declares the services that the target board supplies. The
application uses the existing MicroPython camera and model wrappers.
