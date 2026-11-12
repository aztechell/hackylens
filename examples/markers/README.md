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

Show the selected card when the script prints `SHOW`. Each phase passes
after three consecutive scanned frames with the expected result. A
single `SEEN` line reports a detection, but does not pass the check. Use
`--marker both` for QR followed by AprilTag, or `--seconds` to change the
90-second limit per card. The runner removes its temporary script when
the check exits. Keep other scripts stopped while running the check.
