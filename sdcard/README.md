# HackyLens SD card

Copy the contents of this directory to the root of a FAT32 card.

Expected model paths:

```text
/hackylens.kmodels/detect.kmodel
/hackylens.kmodels/detect.UPSTREAM.txt
/hackylens.kmodels/faces/ld5.kmodel
/hackylens.kmodels/faces/feature_extraction.kmodel
/hackylens.kmodels/faces/LICENSE.CanMV
/hackylens.kmodels/object20/model.kmodel
/hackylens.kmodels/object20/manifest.hkai
/hackylens.kmodels/object20/manifest.json
/hackylens.kmodels/object20/labels.txt
```

`manifest.json` is for audit/reproducibility; firmware validates
`manifest.hkai` plus the model CRC and exact KModel contract.

The FACE model is pinned to Kendryte's standalone `face_detect` example:

```text
bytes            388776
SHA-256          916e679defa91ad76f9feed18b6b37d26328ec9a2c0c8ab0d1ca5983e105b7c0
upstream commit  e89c35465fadb5524c892d2a1c7a76dc76e219ed
```

See `hackylens.kmodels/detect.UPSTREAM.txt` for its exact URL and provenance
limitations.

FACE RECOG uses the two bundled CanMV models in `/hackylens.kmodels/faces/`.
Keep `LICENSE.CanMV` with those files. The
[FACE RECOG setup guide](https://github.com/aztechell/hackylens/blob/main/docs/FACE_RECOGNITION.md#install-the-models)
lists their exact sizes, hashes and upstream source.
