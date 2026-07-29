# AI models

HackyLens separates reusable model mechanics from feature behavior.

## Firmware contract

`core/ai_model_types.h` describes a model without binding it to a detector:

- KModel header and first-layer input contract;
- input shape, element type, layout, normalization, and padded DMA byte count;
- up to four output tensors;
- labels and post-processing category (`raw`, `classification`, `yolo2`, or
  `embedding`);
- SD paths and unload timeout.

`storage/ai_model_storage.*` mounts FAT32, validates the optional 256-byte
`manifest.hkai`, allocates a 256-byte-aligned model buffer, reads the model,
and calculates CRC32. It does not call the KPU.

`services/ai_model_runtime.*` is an instance API with one global KPU owner, as
required by K210 hardware. It validates the descriptor and sidecar, loads the
model through `hal_kpu`, checks every output size, runs asynchronous inference,
reports timing, and completes deferred unload or forced stop. It does not know
about cameras, DVP, labels, bounding boxes, or a particular app.

FACE DETECT is the first migrated consumer. Its SD path and user behavior are
unchanged; only the generic load/run/unload state machine moved out of the
feature.

## SD package

`tools/ai_model.py package` emits:

- `model.kmodel`;
- `manifest.hkai`, the compact record consumed by firmware;
- `manifest.json`, a human-readable audit record with SHA-256;
- `labels.txt` when the spec defines labels.

Both the package tool and firmware verify the model CRC and exact KModel
contract. A model with the same filename but different tensors is rejected
before inference.

## Original HUSKYLENS assets

The inspector establishes the following facts:

| File | Result |
|---|---|
| `mobilenetv1_1.0.kmodel` | 32-bit word-swapped KModel v3; 34 layers; padded `1x3x224x224` input of 172,032 bytes; one 4,000-byte output (1,000 floats). This is a classifier, not a bounding-box detector. |
| `object_detect.bin` | Original data-store format with `0x12345678` magic; not a KModel. |
| `detect.kmodel`, `key_point.kmodel`, `feature.kmodel` | Original package representations are not native SD-loadable KModel v3 images; FACE DETECT continues to use the known standalone-demo-compatible `detect.kmodel`. |

The original MobileNet can be normalized and packaged for research with
`models/huskylens_mobilenetv1_1000.json`, but it is not the planned 20-class
object detector.

## Conversion boundary

The repository locks legacy Kendryte nncase `v0.1.0-rc5` by URL, byte size, and
SHA-256. `tools/ai_model.py bootstrap` verifies the archive before extraction.
`convert` always supplies the target, calibration dataset, uint8 inference,
L2 calibration, and legacy `k210model` output explicitly. The resulting file
must pass the same package validation as an externally supplied model.

Conversion success is only the first gate. Each candidate still requires:

1. source-framework versus converted-output comparison;
2. supported-operator and allocator review;
3. KPU/main-memory measurement;
4. on-device inference time and camera-to-overlay latency;
5. task accuracy measurement with fixed test data.
