#!/usr/bin/env python3
"""Generate or freshness-check manifest-driven app build composition."""

from __future__ import annotations

import argparse
import sys

import app_composition


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.check:
            failures = app_composition.freshness_failures()
            if failures:
                for failure in failures:
                    print(f"[ERR] {failure}", file=sys.stderr)
                return 1
            print("[OK] manifest app composition is deterministic and fresh")
        else:
            app_composition.write_generated()
            print("[OK] generated manifest app composition")
    except app_composition.CompositionError as exc:
        print(f"[ERR] {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
