"""Run a shared KPU detector on leased camera frames."""
import hackylens as hl

# Set VISION_OBJECT_DETECT to use the object20 package.
SOURCE = hl.VISION_FACE


def main():
    hl.camera_open()
    try:
        hl.camera_model_open(SOURCE)
        last_report = hl.ticks_ms()
        while not hl.button(hl.BUTTON_BACK):
            frame = hl.camera_frame()
            if frame is None:
                hl.sleep_ms(5)
                continue
            lease = frame[0]
            try:
                result = hl.camera_detect(lease)
                box = None
                if result["items"]:
                    kind, flags, item_id, x0, y0, x1, y1, confidence = result["items"][0]
                    box = (x0, y0, x1 - x0, y1 - y0)
                hl.camera_present(lease, box)
            finally:
                hl.camera_release(lease)
            now = hl.ticks_ms()
            if now - last_report >= 250:
                print(result["frame_id"], result["items"])
                last_report = now
    finally:
        hl.camera_close()


main()
