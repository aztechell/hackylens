# QR and AprilTag test cards

Open a card full screen or print it with the white margin intact. Point the
camera at it and use the [code scanner](../code_scanner.py) to read the payload
or tag ID.

| Card | Expected result |
| --- | --- |
| [QR-HL.png](QR-HL.png) | QR payload `b"HL"` |
| [QR-HL-H.png](QR-HL-H.png) | Same payload with QR error correction level H |
| [AprilTag-36h11-42.png](AprilTag-36h11-42.png) | tag36h11, ID 42 |

For a repeatable USB check, close any other program using the serial port
and run one of these commands from the repository root. The host runner
requires Python 3 and `pyserial`. Replace the port with your board's port.

```powershell
python examples/markers/run_check.py --port COM11 --board sipeed-maix-cube --marker qr
python examples/markers/run_check.py --port COM10 --board huskylens-sen0305 --marker apriltag
```

Show the selected card when the script prints `SHOW`. The smoke check
finishes after three consecutive scanned frames with the expected result.
Use `--marker both` for QR followed by AprilTag, or `--seconds` to change
the 90-second limit per card. The runner removes its temporary script
when the check exits. Keep other scripts stopped while running the check.

For a fixed-duration measurement, put one card in view before starting.

```powershell
python examples/markers/run_check.py --port COM11 --board sipeed-maix-cube --marker qr --measure --seconds 10
```

- `TOTAL` counts distinct frames submitted for scanning, exact matches
  and frames with a different decoded marker. Divide matches by frames
  to get the read rate.
- `TIMING` reports the time to the first match in milliseconds (`-1`
  if absent) and the longest interval without a match, including the
  intervals before the first and after the last match.
- `QUALITY` reports the longest streak of matching scanned frames and
  the number of binding errors.

Record the camera resolution, card type (screen or paper), width of the
code in pixels excluding the white margin, angle and lighting. For a
single-read scanner, compare the number of separate presentations read
within the time limit and the time to first match. For continuous
recognition, compare the read rate and the longest gap. Run with the card
out of view to check for spurious results.

When comparing different QR versions, divide the code width by the
number of modules across. A [study of QR detectors near the resolution
limit](https://pmc.ncbi.nlm.nih.gov/articles/PMC9572759/) found that about
3–3.5 pixels per module were needed for practical decoding in its
experiments. This gives a starting point for choosing test sizes.

## Distance measurements

The APRILTAG app has a persistent **Distance** switch. Hold OK to open
**TAG SETTINGS**. Aim the crosshair at a tag and press OK to select or deselect
its ID. Selected tags have a yellow outline. Distance measures the visible
selected tag nearest the middle of the image and shows its ID with the
straight-line range. During calibration, the selected tag must be under the
crosshair.

Generate the two-page print PDF with Python, Pillow and ReportLab.

```powershell
python examples/markers/make_distance_cards.py AprilTag-42-A4.pdf
```

Print A4 in black and white at **Actual size (100%)**. Verify that the
printed ruler spans 100 mm. Measure the black square across both axes and
keep its white margin. Use one sheet at a time because both cards are ID 42.

1. Start with SEN0305 and the 60 mm sheet. Support the camera and the flat
   sheet, align the tag center with the crosshair and face the camera
   straight at the sheet. Measure 300 mm from the front of the lens to the
   tag plane. Enter the measured square side under **Tag mm** and the
   reference distance under **Ref mm**.
2. Select ID 42 with a short OK in the camera view before arming calibration.
   Turn **Distance** on, select **Calibrate**, then press BACK to return to
   the camera view. Hold the setup still while five consistent detections
   are collected. A centimeter reading confirms calibration. **Focal** in
   TAG SETTINGS shows the value in detector pixels. SEN0305 defaults to
   154.00 px. Press OK to edit, tap LEFT/RIGHT to change it in 0.01 px steps
   or hold to move faster, then press OK to save or BACK to cancel. On Cube,
   the unset editor starts at 120.00 px and saves it only on OK.
   If the 60 mm
   square is too small at 300 mm, use the 100 mm sheet or a closer measured
   reference distance and update both inputs as needed. A short OK cancels
   an armed calibration. The saved focal value can be reused with another
   tag size by changing **Tag mm**.
3. Compare against a tape measure at 200, 400 and 800 mm. Use the 100 mm
   square for the farther positions as needed. At each position collect
   ten new readings facing the tag, then repeat with the tag angled at
   about 20° and 35° while keeping its center fixed. Repeat a centered
   and an off-center case. Record absolute error, spread and how often the
   app can still read the ID but has no valid range. Use **HKTAGINFO** to
   record `range_mm` over USB. A zero field means no valid reading.
4. Calibrate the Cube optics independently. For **Level range**, hold
   the Cube upright with the camera pointing horizontally and select
   **Set upright**. Pitch the Cube straight down by 90° without rolling
   it and select **Set lens down**. Turn on **Level range**. The **H**
   readout is the horizontal separation to the tag. It is available
   after the board has settled and both references form a right angle.
   Compare the **H** reading with the horizontal separation from a plumb
   projection or from measured vertical and straight-line separation.
   Toggle the option off and repeat to check the optical reading.

The prototype uses one optical reference distance, a centered principal
point and square pixels. Measurements at different angles and positions
will show how much lens distortion and corner resolution affect each board.

## Near and far search

**TAG SETTINGS → Search** defaults to **NEAR**, which searches the whole
image at 160×120. **FAR** starts with the center of the 320×240 capture
and scans a 160×120 window in native pixels. Aim the crosshair at a distant
tag to acquire it. After detection the window follows the tag, preferring
selected IDs. When a small tag is tracked without decimation, a 64×64 window
reduces the area searched. After repeated misses a whole-image scan looks
for larger tags and the window returns to the center if that scan also misses.
Both modes use the same **Focal** calibration.

For a range comparison, face the 100 mm print in even light. Run both
modes at 1, 1.5 and 2 m from lens to paper. Record time to first correct
ID after pointing at the tag and the fraction of processed frames with a
read during a steady hold. The change in `reads` divided by the change
in `frames` from two `HKTAGINFO` samples gives this fraction when only
one tag is in the scene. Record `range_mm` separately. The on-screen FPS
counter measures LCD updates, which are capped at about 12 per second.

In a SEN0305 trial with a 100 mm TAG36H11 ID 42 in FAR mode, a centered
hold yielded the following USB counter deltas. These are steady-hold
measurements after acquisition.

| Lens to tag | Reads / processed frames | Sample window |
| --- | ---: | ---: |
| 1 m | 68 / 68 | ~6 s |
| 1.5 m | 195 / 196 | ~9 s |
| 2 m | 203 / 210 | ~15 s |

With Focal set to 154.00 px, the 1.5 m trial reported 1533–1613 mm in
the sampled range readings. Distance was disabled for the other two holds.
