#!/usr/bin/env python3
"""Generate immutable app registry descriptors from canonical manifests."""

from __future__ import annotations

import argparse
import sys

import app_composition


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        model = app_composition.load_model()
        first = app_composition.generated_registry_files(model)
        second = app_composition.generated_registry_files(
            app_composition.load_model()
        )
        if first != second:
            print("[ERR] app registry generation is not deterministic", file=sys.stderr)
            return 1
        if args.check:
            print("[OK] manifest app composition is valid and deterministic")
            return 0
        app_composition.write_registry(
            app_composition.BUILD_REGISTRY_ROOT, model
        )
        print("[OK] generated immutable app registry")
    except app_composition.CompositionError as exc:
        print(f"[ERR] {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
