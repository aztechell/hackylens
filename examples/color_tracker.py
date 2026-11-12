"""Track the largest patch of a color on SEN0305 and Maix Cube."""
import hackylens as hl

# Paste the tuple from an RGB565 snapshot in HackyLens Code here.
THRESHOLD = (20, 31, 0, 18, 0, 10)

# Use the whole frame, or specify (x, y, width, height) in frame pixels.
ROI = None


def main():
    hl.camera_open()
    try:
        last_report = hl.ticks_ms()
        last_ok = hl.button(hl.BUTTON_OK)
        save_requested = False
        while not hl.button(hl.BUTTON_BACK):
            ok = hl.button(hl.BUTTON_OK)
            if ok and not last_ok:
                save_requested = True
            last_ok = ok
            frame = hl.camera_frame()
            if frame is None:
                hl.sleep_ms(5)
                continue

            lease, width, height, sequence = frame
            try:
                blob = hl.camera_find_color(lease, THRESHOLD, ROI, 24)
                box = (blob[0], blob[1], blob[2], blob[3]) if blob else None
                hl.camera_present(lease, box)
                if save_requested:
                    save_requested = False
                    stem = str(hl.ticks_ms() % 100000000)
                    try:
                        hl.camera_save(lease, stem + ".BMP")
                        record = str(sequence) + " " + str(blob) + "\n"
                        hl.sd_write(stem + ".TXT", bytes(record, "ascii"))
                        print("Saved in /PYDATA", stem)
                    except OSError as error:
                        print("Save failed", error)
            finally:
                hl.camera_release(lease)

            now = hl.ticks_ms()
            if now - last_report >= 250:
                print(sequence, (width, height), blob)
                last_report = now
    finally:
        hl.camera_close()


main()
