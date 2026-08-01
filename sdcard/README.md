# HackyLens SD card

Copy the contents of this directory to the root of a FAT32 card.

Expected model paths:

```text
/hackylens.kmodels/detect.kmodel
/hackylens.kmodels/object20/model.kmodel
/hackylens.kmodels/object20/manifest.hkai
/hackylens.kmodels/object20/manifest.json
/hackylens.kmodels/object20/labels.txt
```

`manifest.json` is for audit/reproducibility; firmware validates
`manifest.hkai` plus the model CRC and exact KModel contract.
