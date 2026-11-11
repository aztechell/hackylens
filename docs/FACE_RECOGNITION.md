# FACE RECOG

FACE RECOG runs on HUSKYLENS SEN0305 and Sipeed Maix Cube. It recognizes
previously enrolled faces on the device and assigns an ID from 01 to 10.

## Install the models

Copy the bundled FACE DETECT model from `sdcard/hackylens.kmodels/detect.kmodel`
to `/hackylens.kmodels/detect.kmodel` on a FAT32 microSD card. Put the two files
from the [01Studio CanMV K210 face-recognition example][canmv] in
`/hackylens.kmodels/faces/`. The [01Studio resource package][resources] contains
the example files.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `ld5.kmodel` | 159,768 | `521e67a5901b3f5a31be14d4f52c25e3d4a9ff5ff8e0f48bbf6134cdc712d49e` |
| `feature_extraction.kmodel` | 1,108,368 | `994a7ea039ab25e759c8249d1ade2d2ce3d528e22d4a80c75dd2bf0203598f3d` |

The firmware validates the exact landmark and feature models by CRC32 and
tensor contract. These two weight files are supplied separately from the
firmware. The public example describes their use but does not specify a
separate license for the pretrained weights.

## Learn, recognize and forget

1. Open **FACE RECOG**. Use LEFT and RIGHT to select an empty ID.
2. Point the camera at one face, then press and release OK. Keep the face
   in view until **FACE SAVED** appears. The five-second enrollment window
   accepts a single face with usable landmarks.
3. Return to the camera with a face in view. A green box shows a learned ID.
   **UNKNOWN** appears for other detections and for faces too small or
   poorly aligned to compare. The selected ID controls enrollment and
   deletion. Recognition searches all saved IDs.
4. To forget a face, select its ID, hold OK, release it and press OK again
   to confirm. BACK cancels enrollment or deletion, then exits the app.

Each identity is one quantized 192-value feature vector in `FR01.DAT` through
`FR10.DAT` on the card. The app stores feature vectors and does not save face
images. Moving the card to the other supported board keeps those IDs.
Each record has a format marker, model identity and payload CRC32. A damaged
record is skipped and its ID can be deleted from the app.

## How recognition works

The pipeline follows [MaixPy's detect, align and compare workflow][maixpy]
and the 64 × 64 alignment template from [CanMV][canmv]. It runs the shared
320 × 240 face detector, estimates five landmarks in a 128 × 128 crop, then
computes a feature vector from the aligned face. The three models stay
resident and take turns on the KPU. The detector retains its own planar
input until recognition completes. The live preview uses the normal camera
frame pool and renders the latest completed recognition results.
The detector and embedding weights reuse the shared SRAM arena alongside two
QVGA camera slots. The landmark model and its crop scratch use the heap.
Embedding weights load into the arena in 16 KiB steps so the app can handle
BACK while the card is being read. See the [RAM layout](RAM_BUDGET.md).

The app compares quantized vectors using cosine similarity and requires a
score of at least 0.61 with a 0.05 lead over the second-best ID. It follows
[HUSKYLENS enrollment][husky] in using stable numeric IDs and storing them
across restarts. C and MicroPython receive the same BLOCK results through the
[vision API](MICROPYTHON_API.md#vision-results), with source
`VISION_FACE_RECOGNITION`, ID zero for unknown faces, and similarity scaled
to 0–1000.

## Hardware verification

SEN0305 and Maix Cube both loaded the three models from FAT32 cards and ran
their live QVGA previews. A separate test image sent two reference portraits
through the detector input and used a dedicated test record on each card.
On both boards, the empty gallery reported UNKNOWN, enrollment produced
ID01, and the other portrait scored 233/1000 and remained UNKNOWN. After a
hardware reset, the shifted first portrait scored 976/1000 and matched ID01
from the saved record. Landmark inference took about 1.5 ms and embedding
inference about 47 ms on both boards in this test.

With the regular firmware restored, the operator enrolled a live face using
OK on each board. Both showed FACE SAVED and reported repeated matches before
reset. Both reloaded ID01 from their cards after a hardware reset.

[canmv]: https://wiki.01studio.cc/en/docs/canmv_k210/machine_vision/face_recognition/
[resources]: https://wiki.01studio.cc/en/docs/canmv_k210/download/
[maixpy]: https://wiki.sipeed.com/soft/maixpy/en/course/ai/image/face_recognization.html
[husky]: https://wiki.dfrobot.com/sen0305/docs/22612
