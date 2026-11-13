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
