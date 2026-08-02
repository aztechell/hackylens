# HackyLens SD card

Copy the contents of this directory to the root of a FAT32 card.

Expected model paths:

```text
/hackylens.kmodels/detect.kmodel
/hackylens.kmodels/detect.UPSTREAM.txt
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
