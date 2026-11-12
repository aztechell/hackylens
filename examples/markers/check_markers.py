"""Check marker identity, frame metadata and consecutive reads on the board."""

import hackylens as hl


MODE = "both"
PHASE_TIMEOUT_MS = 90000


def matching_box(result, source):
    for item in result["items"]:
        if source == hl.VISION_QR:
            matches = result["qr_payload"] == b"HL"
        else:
            matches = item[2] == 42 and item[1] == 0
        if matches:
            x0, y0, x1, y1 = item[3], item[4], item[5], item[6]
            return x0, y0, x1 - x0, y1 - y0


def scan(source, label):
    print("SHOW", label)
    started = hl.ticks_ms()
    last_report = started
    previous = None
    last_detection = None
    streak = 0
    best = matches = errors = consecutive_errors = 0
    while hl.ticks_ms() - started < PHASE_TIMEOUT_MS:
        frame = hl.camera_frame()
        if frame is None:
            hl.sleep_ms(5)
            continue

        lease, width, height, sequence = frame
        try:
            try:
                result = hl.camera_scan(lease, source)
            except RuntimeError as error:
                if str(error) != "binding inactive":
                    raise
                errors += 1
                consecutive_errors += 1
                if consecutive_errors == 1:
                    print("RETRY", label, sequence, error)
                if consecutive_errors == 5:
                    raise
                streak = 0
                continue
            consecutive_errors = 0
            assert result["frame_id"] == sequence
            assert (result["width"], result["height"]) == (width, height)
            box = matching_box(result, source)
            if box is not None:
                x, y, w, h = box
                assert 0 <= x and 0 <= y and 0 < w <= width - x and 0 < h <= height - y
            if result["items"]:
                detection = result["qr_payload"] if source == hl.VISION_QR else result["items"][0][2]
                if detection != last_detection:
                    print("SEEN", label, detection)
                    last_detection = detection
            try:
                hl.camera_present(lease, box)
            except RuntimeError as error:
                print("DISPLAY FAILED", label, sequence, error)
                raise
        finally:
            hl.camera_release(lease)

        if sequence != previous:
            previous = sequence
            if box is None:
                streak = 0
            else:
                streak += 1
                matches += 1
                best = max(streak, best)
        if streak == 3:
            print("OK", label, "frame", sequence, "box", box, "errors", errors)
            return
        now = hl.ticks_ms()
        if now - last_report >= 5000:
            print("WAIT", label, "frame", sequence, "matches", matches, "best", best, "errors", errors)
            last_report = now

    print("TOTAL", label, "matches", matches, "best", best, "errors", errors)
    raise RuntimeError("marker timed out: " + label)


def main():
    hl.camera_open()
    try:
        if MODE != "apriltag":
            scan(hl.VISION_QR, "QR HL")
        if MODE != "qr":
            scan(hl.VISION_APRILTAG, "APRILTAG 42")
    finally:
        hl.camera_close()
    print("PASS MARKER CHECK")


main()
