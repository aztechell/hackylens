#!/usr/bin/env python3
"""Validate Native App Manifest schema 1 and emit its canonical model."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import app_manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--scan-root",
        type=Path,
        required=True,
        help="directory recursively containing app.toml files",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="write canonical JSON here; omit to write it to stdout",
    )
    args = parser.parse_args(argv)
    try:
        model = app_manifest.validate_tree(args.scan_root)
        encoded = app_manifest.canonical_json_bytes(model)
        if args.output is None:
            sys.stdout.buffer.write(encoded)
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(encoded)
            print(
                f"[OK] validated {len(model['apps'])} Native App Manifest(s); "
                f"canonical model: {args.output}"
            )
    except (OSError, app_manifest.ManifestError) as exc:
        print(f"[FAIL] Native App Manifest: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
