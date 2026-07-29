# HackyLens AI model lab

This directory defines the reproducible boundary between a source neural network
and firmware that can execute it on the K210.

`tools/ai_model.py` has four workflows:

- `inspect` identifies native KModel v3, 32-bit word-swapped images extracted
  from the original HUSKYLENS package, nncase containers, and the original
  `object_detect.bin` data store.
- `convert` invokes the pinned legacy `ncc` compiler with an explicit
  calibration directory and then packages the result.
- `package` validates tensor sizes and emits an SD-ready `model.kmodel`,
  `manifest.hkai`, human-readable `manifest.json`, and optional `labels.txt`.
- `verify` checks manifest CRC, model CRC, KModel contract, output sizes, and
  label count without requiring the device.

The binary `manifest.hkai` is exactly 256 bytes and is parsed by
`ai_model_storage`. It records the model ID and CRC, KModel v3 header contract,
input shape/type/layout/normalization, output byte sizes, post-processing type,
and label metadata. Feature apps still own preprocessing and post-processing;
the shared runtime owns only model validation, KPU execution, and lifecycle.
The declared input byte count may exceed the logical tensor payload because
K210 first-layer DMA uses channel/row padding; the inspector reports that exact
hardware byte count.

## Conversion

The locked compiler is Kendryte nncase `v0.1.0-rc5`. Run its Linux build in a
container or WSL; do not silently substitute a modern K230 compiler. The
workflow deliberately requests the legacy `k210model` output rather than a
newer nncase container.

```text
python tools/ai_model.py bootstrap

python tools/ai_model.py convert \
  --source detector.tflite \
  --format tflite \
  --calibration calibration-images \
  --spec models/spec.example.json \
  --out-dir out/detector
```

ONNX and Caffe are accepted as explicit legacy `ncc` frontends, but actual
operator compatibility must be verified for each network. A successful
conversion does not replace device measurements of KPU RAM, latency, accuracy,
and output semantics.

## Original package findings

`unpacked/mobilenetv1_1.0.kmodel` is a 32-bit word-swapped KModel v3
classifier: 34 layers, one 4,000-byte output, therefore 1,000 float scores. It
does not produce bounding boxes. Normalize a research copy with:

```text
python tools/ai_model.py inspect unpacked/mobilenetv1_1.0.kmodel \
  --normalize-out build/mobilenetv1.native.kmodel
```

`unpacked/object_detect.bin` starts with the original data-store magic
`12 34 56 78`; it is not a KModel and cannot be passed to the KPU runtime.
