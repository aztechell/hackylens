# RAM / Flash Budget

The firmware uses statically allocated camera frame buffers, an LCD shadow, SD sectors, PNG inflate buffers, and quirc storage.

Current ownership:

- LCD shadow and row buffer: `drivers/lcd_st7789.c`; the 153,600-byte shadow is also the leased RGB565-BE full-frame staging surface, so LCD present does not allocate another framebuffer.
- Two camera stream slots: `drivers/frame_pool.c`; preview, photo, QR, and debug snapshots lease these slots through `drivers/camera_stream.c` without allocating a third frame.
- SD/FAT sector buffers: `storage/fat32_sd.c`.
- PNG inflate and image row buffers: `storage/image_decode_png_inflate.c` and `storage/image_decode_common.c`.
- Terminal history ring and viewport: `apps/terminal/terminal_buffer.c`; they are omitted when Terminal is disabled.

Build guard: `hackylens.bin` must remain below `0x007FE000`, preserving the two 4 KB settings slots at `0x007FE000` and `0x007FF000`.
