#!/usr/bin/env python3
"""Validate every known board descriptor and optional Cube compile-conformance."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from board_contract import Board, ContractError, ROOT, board_ids, load_board, validate_board_source
from gen_board import write_board_config


def compile_conformance_board(board: Board, *, compiler: str | None = None) -> None:
    """Compile and link the selected BSP against the generated board_config.h."""
    if board.support != "conformance":
        raise ContractError("conformance compile requires support=conformance")
    compiler = compiler or os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        raise ContractError("host C compiler unavailable for conformance compile")
    with tempfile.TemporaryDirectory(prefix=f"hk-{board.id}-") as temporary:
        generated = Path(temporary) / "board_config.h"
        write_board_config(board, generated)
        output = Path(temporary) / "board-conformance"
        command = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{ROOT / 'firmware' / 'src' / 'internal'}",
            f"-I{generated.parent}",
            str(board.directory / "board.c"),
            str(ROOT / "tools" / "board_conformance_harness.c"),
            "-o",
            str(output),
        ]
        print("+ " + " ".join(command))
        subprocess.run(command, check=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument("--board", help="Canonical board.toml ID")
    selection.add_argument("--all", action="store_true", help="Check every known board")
    parser.add_argument("--compile", action="store_true",
                        help="Host compile-check a selected conformance BSP")
    args = parser.parse_args(argv)
    try:
        identifiers = board_ids() if args.all else [args.board]
        if not identifiers:
            raise ContractError("no board descriptors found")
        for identifier in identifiers:
            board = load_board(identifier)
            validate_board_source(board)
            if args.compile:
                if args.all:
                    raise ContractError("--compile requires one explicit --board")
                if board.support != "conformance":
                    raise ContractError("--compile is reserved for support=conformance BSPs")
                compile_conformance_board(board)
    except (ContractError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"board contract error: {exc}", file=sys.stderr)
        return 2
    print(f"[OK] Board Port Contract passed for {len(identifiers)} board(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
