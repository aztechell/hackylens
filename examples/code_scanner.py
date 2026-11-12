"""Scan QR codes or AprilTags on leased camera frames."""
import hackylens as hl

# Set VISION_APRILTAG to scan for tag36h11 markers.
SOURCE = hl.VISION_QR


def main():
    hl.camera_open()
    try:
        last_report = hl.ticks_ms()
        while not hl.button(hl.BUTTON_BACK):
            frame = hl.camera_frame()
            if frame is None:
                hl.sleep_ms(5)
                continue
            lease = frame[0]
            try:
                result = hl.camera_scan(lease, SOURCE)
                box = None
                if result["items"]:
                    item = result["items"][0]
                    box = (item[3], item[4], item[5] - item[3], item[6] - item[4])
                hl.camera_present(lease, box)
            finally:
                hl.camera_release(lease)
            now = hl.ticks_ms()
            if result["items"] and now - last_report >= 250:
                if SOURCE == hl.VISION_QR:
                    print(result["qr_payload"])
                else:
                    for item in result["items"]:
                        print(item[2])
                last_report = now
    finally:
        hl.camera_close()


main()
