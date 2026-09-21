#!/usr/bin/env python3
"""Check the typed board bindings shared by native apps and MicroPython."""
from pathlib import Path
import sys
import service_bindings
from board_contract import load_board

def validate(root: Path = service_bindings.ROOT) -> list[str]:
    failures = []
    try:
        for board_id in ("huskylens-sen0305", "sipeed-maix-cube"):
            board = load_board(board_id, root=root)
            service_bindings.select(board, set(), set(), set(), root=root)
    except (ValueError, OSError) as exc:
        failures.append(str(exc))
    return failures

def main() -> int:
    failures = validate()
    for failure in failures:
        print(f"[ERR] {failure}", file=sys.stderr)
    if not failures:
        print("[OK] typed board bindings passed")
    return bool(failures)

if __name__ == "__main__":
    raise SystemExit(main())
