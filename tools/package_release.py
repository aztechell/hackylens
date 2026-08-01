#!/usr/bin/env python3
"""Create versioned HackyLens firmware and SD-card release artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile


ROOT = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Package a HackyLens release")
    parser.add_argument("--firmware", type=Path,
                        default=ROOT / "build" / "hackylens.bin")
    parser.add_argument("--sdcard", type=Path, default=ROOT / "sdcard")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--tag", help="Expected release tag, for example v0.2.0")
    args = parser.parse_args()

    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not version or (args.tag and args.tag != f"v{version}"):
        raise SystemExit(
            f"release tag {args.tag!r} does not match VERSION={version!r}")
    if not args.firmware.is_file():
        raise SystemExit(f"firmware image not found: {args.firmware}")
    if not args.sdcard.is_dir():
        raise SystemExit(f"SD-card tree not found: {args.sdcard}")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    firmware_out = args.out_dir / f"hackylens-v{version}.bin"
    sdcard_out = args.out_dir / f"hackylens-v{version}-sdcard.zip"
    metadata_out = args.out_dir / f"hackylens-v{version}.json"
    sums_out = args.out_dir / "SHA256SUMS.txt"

    shutil.copy2(args.firmware, firmware_out)
    with zipfile.ZipFile(sdcard_out, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=9) as archive:
        for path in sorted(args.sdcard.rglob("*")):
            if path.is_file():
                archive.write(path, Path("sdcard") / path.relative_to(args.sdcard))

    metadata = {
        "project": "HackyLens",
        "version": version,
        "firmware": firmware_out.name,
        "firmware_bytes": firmware_out.stat().st_size,
        "firmware_sha256": sha256(firmware_out),
        "sdcard": sdcard_out.name,
        "sdcard_sha256": sha256(sdcard_out),
        "flash_address": "0x000000",
    }
    metadata_out.write_text(json.dumps(metadata, indent=2) + "\n",
                            encoding="utf-8")
    artifacts = (firmware_out, sdcard_out, metadata_out)
    sums_out.write_text(
        "".join(f"{sha256(path)}  {path.name}\n" for path in artifacts),
        encoding="ascii")
    for path in (*artifacts, sums_out):
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
