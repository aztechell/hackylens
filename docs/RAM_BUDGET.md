# RAM / Flash Budget

The firmware shares an SRAM arena across camera capture, PNG decoding, and large model weights. The LCD shadow and SD sector buffers have separate storage. QR and AprilTag allocate working memory when their sessions start.

Current ownership:

- LCD shadow and row buffer: `drivers/lcd_st7789.c`; the 153,600-byte shadow is also the borrowed RGB565-BE full-frame staging surface, so LCD present does not allocate another framebuffer.
- Two camera stream slots in `services/frame_pool.c` use a stride derived from the capture resolution. Together they reserve 300 KiB at QVGA or 1,200 KiB at VGA. Preview, photo, QR, and debug snapshots lease these slots through `drivers/camera_stream.c`. When capture is inactive, FILES or the Display batch provider can borrow one VGA-sized scratch slot. Copied or stale borrow tokens cannot release a later borrower.
- IDE camera transfers borrow the existing MicroPython program staging buffer for up to 38,400 bytes. The frame lease ends after downsampling. A program start or native app launch releases the staging borrow before using it again.
- Python color tracking holds one existing camera frame lease and sends only
  metadata and detection results through the binding mailbox. The C scan
  workspace occupies 15,360 bytes and tracks connected runs across two rows.
  Preview uses the existing LCD shadow. An idle Python display lease leaves
  the frame arena available to camera capture.
- SD/FAT sector buffers: `storage/fat32_sd.c`.
- Python microSD writes use a 512-byte FAT sector buffer and send script data
  through the existing binding mailbox in 256-byte chunks. BMP output uses
  one sector of the same mailbox to encode the leased frame with the CAMERA
  encoder.
- Core 1 owns the QR decoder and its heap workspace. The preview fills the
  decoder's existing image through its uncached alias, so autoscan adds no
  second luma image and retains no camera lease. Session exit schedules cleanup
  after any outstanding ticket, including after partial initialization.
- Python scans run on the VM's core and stage luma while the script holds a
  frame. QR writes to the decoder's existing image. AprilTag shares one
  19,200-byte luma buffer with the native app. VM teardown releases both
  decoder heaps.
- The vision service retains one fixed result snapshot, including up to 383
  QR payload bytes. The Python bridge transfers the snapshot in its existing
  mailbox.
- PNG decoding uses a 640 KiB tail lease for compressed input and its two
  Huffman tables alongside the VGA-sized scratch slot for decoded pixels.
  FACE RECOG and OBJECT DETECT reserve the same arena tail after the two
  QVGA slots. The full arena occupies 1,825,280 bytes, down from 1,884,160
  bytes for the former separate frame and PNG buffers.
- FACE RECOG keeps the detector and feature model weights in that lease.
  The landmark model and its crop scratch take about 204 KiB on the heap.
  Both applications hold their tail lease until their KPU models have been
  unloaded. A fault during unload quarantines the lease until reset.
- GIF decoding uses fixed 12-bit LZW tables, one 255-byte sub-block buffer, the shared image row buffer, and small precomputed viewport maps. Frames are composited directly into the 320x240 LCD shadow; disposal mode 3 temporarily reuses one existing camera frame-pool slot, so no GIF-sized logical canvas or additional framebuffer is allocated.
- Terminal history ring and viewport: `apps/terminal/terminal_buffer.c`; they are omitted when Terminal is disabled.
- APRILTAG uses one fixed uncached 19,200-byte 160x120 luma handoff, two small result banks, a small stabilized display-result copy, a 74-byte selected-ID bitmap, and temporary heap allocations owned exclusively by its core-1 worker. Camera RGB565 slots are never retained while detection runs. A 16-bit union-find halves its main threshold working set because the bounded 19,200-pixel input cannot exceed its 65,535-node limit. Only TAG36H11 is built; image file I/O, profiling images, pthreads, and all other tag families are disabled. The complete detector, selection UI, and runtime buffers are omitted with `--disable-app apriltag`.
- Settings menus keep one small instance state per owner and format only the currently drawn row into a 24-byte stack buffer. Descriptor tables live in read-only flash. The previous CAMERA/QR and system SETTINGS arrays containing formatted copies of every row were removed.
- Feature views and controllers add no duplicate framebuffer. CAMERA/QR/FACE/APRILTAG/OBJECT share the camera frame pool and preview surface; menu icons live in feature views as code/constant data. FILES disposal mode 3 continues to reuse the existing camera scratch slot.
- The shared AI runtime loads one foreground model group. FACE RECOG can
  retain companions under that lease, with one KPU inference at a time.
  Each context has the KPU SDK's declared main buffer.
  The optional SD manifest is read through one 256-byte stack buffer.
- FACE and OBJECT share one 230,720-byte aligned planar input including the DVP
  guard row; only one AI app can own it. OBJECT additionally keeps a fixed
  350-box geometry table, the full 350x20 per-class probability workspace, and
  two small result banks. Its SD model is 1,352,588 bytes and declares 43,752
  bytes of KModel main memory. The stock model weights use the arena lease.
  Compatible larger SD models can still request heap storage. The output
  tensor is read directly from KPU main memory.
- The shared core-1 executor adds only control words and a permanent idle loop
  when APRILTAG, MicroPython, or QR-CAMERA is enabled. KPU output post-processing remains on core 0.

The shared arena increased the linked heap window in the full builds measured
against revision `78641e0`. Live allocations and fragmentation determine
which blocks the runtime allocator can provide.

| Board | Previous heap window | Shared arena build |
| --- | ---: | ---: |
| Maix Cube | 1,523,712 bytes | 1,582,272 bytes |
| HUSKYLENS SEN0305 | 1,904,576 bytes | 1,962,880 bytes |

The settings record grows from the legacy 16-byte payload through the 96-byte
v2 payload, 97-byte v3 payload, and 105-byte v4 payload to a 106-byte v5
payload. V5 retains 88 opaque app bytes and widens stable autostart identity to
uint16; alignment keeps the record at 124 bytes, so it still fits in one flash
page. Build guard: `hackylens.bin` must remain below
`0x007FE000`, preserving the two 4 KB settings slots at `0x007FE000` and
`0x007FF000`.
