# HackyLens MicroPython examples

Upload a `.py` file from this directory with HackyLens Code or the
[USB CLI](../README.md#micropython-workflow). Open **MICRO-PYTHON** on the
device, select the file, and choose **RUN**. Press **BACK** to stop.

| Example | Boards | Setup |
| --- | --- | --- |
| [Fireplace](fireplace.py) | SEN0305, Maix Cube | Runs on the display with no camera or microSD setup. |
| [Color tracker](color_tracker.py) | SEN0305, Maix Cube | Copy an RGB565 threshold from a snapshot in HackyLens Code. Press **OK** to save a BMP and detection data to `/PYDATA` on microSD. |
| [QR and AprilTag scanner](code_scanner.py) | SEN0305, Maix Cube | Select `VISION_QR` or `VISION_APRILTAG` in `SOURCE`. AprilTag uses the tag36h11 family. Results appear in the console. |
| [KPU detector](kpu_detector.py) | SEN0305, Maix Cube | Set `SOURCE` to `VISION_FACE` for `detect.kmodel` or `VISION_OBJECT_DETECT` for `object20`. See [model installation](../docs/AI_MODELS.md). |
| [Audio demo](audio_demo.py) | Maix Cube | Plays a tone through the built-in speaker and reports a microphone capture. |

The vision scripts require a firmware build exposing the corresponding
camera bindings. See the [MicroPython API](../docs/MICROPYTHON_API.md).

Use the [marker cards and USB check](markers/README.md) to try the QR and
AprilTag scanner with known inputs.
