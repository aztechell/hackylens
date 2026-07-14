#!/usr/bin/env python3
"""Create a small metadata sidecar for HackyLens firmware images."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Prepare a HackyLens firmware image artifact")
    parser.add_argument("input", type=Path)
    parser.add_argument("--out-dir", type=Path, default=Path("dist"))
    parser.add_argument("--name", default=None)
    args = parser.parse_args(argv)

    if not args.input.is_file():
        print(f"input image not found: {args.input}", file=sys.stderr)
        return 1

    args.out_dir.mkdir(parents=True, exist_ok=True)
    out_name = args.name or args.input.name
    out_image = args.out_dir / out_name
    shutil.copy2(args.input, out_image)

    meta = {
        "project": "HackyLens",
        "image": out_image.name,
        "size": out_image.stat().st_size,
        "sha256": sha256_file(out_image),
        "flash_address": "0x000000",
    }

    meta_path = out_image.with_suffix(out_image.suffix + ".json")
    meta_path.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")

    print(out_image)
    print(meta_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
