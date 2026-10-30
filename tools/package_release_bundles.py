#!/usr/bin/env python3
"""Wrap verified flat releases into the two public board bundles."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import sys
import zipfile


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))
import package_release

ROOT = TOOLS_DIR.parent
RELEASE_BOARDS = ("huskylens-sen0305", "sipeed-maix-cube")


def flat_names(identifier: str, version: str) -> set[str]:
    stem = f"hackylens-{identifier}-v{version}"
    return {stem + suffix for suffix in (
        ".bin", ".json", "-attestation.json", "-release.json",
        "-sdcard.zip", "-third-party.json", "-third-party-notices.zip",
    )} | {"SHA256SUMS.txt"}


def bundle_names(version: str) -> set[str]:
    return {f"hackylens-{identifier}-v{version}.zip"
            for identifier in RELEASE_BOARDS}


def read_sums(text: str, expected: set[str]) -> dict[str, str]:
    result = {}
    for line in text.splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([^/\\]+)", line)
        if not match or match[2] in result:
            raise SystemExit("invalid or duplicate release checksum entry")
        result[match[2]] = match[1]
    if set(result) != expected:
        raise SystemExit("release checksum file does not match the artifact allowlist")
    return result


def exact_directory(directory: Path, expected: set[str]) -> None:
    package_release.ensure_allowlisted_output(directory, expected)
    if not directory.is_dir() or {path.name for path in directory.iterdir()} != expected:
        raise SystemExit(f"release directory does not match the artifact allowlist: {directory}")
    paths = [directory / name for name in sorted(expected)]
    package_release.validate_output_paths(directory, paths)
    if not all(path.is_file() for path in paths):
        raise SystemExit("release artifacts must be regular files")


def verify_public(directory: Path, version: str) -> list[Path]:
    names = bundle_names(version)
    exact_directory(directory, names | {"SHA256SUMS.txt"})
    checksums = read_sums((directory / "SHA256SUMS.txt").read_text(encoding="ascii"), names)
    for identifier in RELEASE_BOARDS:
        path = directory / f"hackylens-{identifier}-v{version}.zip"
        if package_release.sha256(path) != checksums[path.name]:
            raise SystemExit(f"release bundle checksum mismatch: {path.name}")
        expected = flat_names(identifier, version)
        with zipfile.ZipFile(path) as archive:
            if len(archive.namelist()) != len(expected) or set(archive.namelist()) != expected:
                raise SystemExit(f"release bundle contents do not match allowlist: {path.name}")
            inner = read_sums(archive.read("SHA256SUMS.txt").decode("ascii"),
                              expected - {"SHA256SUMS.txt"})
            for name, checksum in inner.items():
                digest = hashlib.sha256()
                with archive.open(name) as source:
                    for chunk in iter(lambda: source.read(1024 * 1024), b""):
                        digest.update(chunk)
                if digest.hexdigest() != checksum:
                    raise SystemExit(f"release bundle member checksum mismatch: {name}")
    return [directory / name for name in sorted(names | {"SHA256SUMS.txt"})]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boards", nargs="+", required=True, choices=RELEASE_BOARDS)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--stage-dir", type=Path, default=ROOT / "build" / "release-stage")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "dist" / "release")
    parser.add_argument("--firmware-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--sdcard", type=Path, default=ROOT / "sdcard")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args(argv)
    if sorted(args.boards) != sorted(RELEASE_BOARDS):
        parser.error("select each release board exactly once")
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", version):
        raise SystemExit("VERSION is not release SemVer")
    if args.tag != f"v{version}":
        raise SystemExit(f"release tag {args.tag!r} does not match VERSION={version!r}")
    if args.verify_only:
        for path in verify_public(args.out_dir, version):
            print(path)
        return 0

    stage_root, output_root = args.stage_dir.resolve(), args.out_dir.resolve()
    if stage_root == output_root or stage_root in output_root.parents or output_root in stage_root.parents:
        raise SystemExit("release staging and public output directories must be separate")
    expected = bundle_names(version) | {"SHA256SUMS.txt"}
    package_release.ensure_allowlisted_output(args.out_dir, expected)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    package_release.validate_output_paths(args.out_dir, [args.out_dir / name for name in expected])
    # Keep the flat API and every board/sidecar/attestation qualification gate.
    stages = []
    for identifier in args.boards:
        stage = args.stage_dir / identifier
        package_release.main([
            "--board", identifier,
            "--tag", args.tag,
            "--firmware", str(args.firmware_dir / f"hackylens-full-{identifier}.bin"),
            "--sdcard", str(args.sdcard),
            "--out-dir", str(stage),
        ])
        exact_directory(stage, flat_names(identifier, version))
        stages.append((identifier, stage))
    # Publish nothing until both board packages have passed qualification.
    bundles = []
    for identifier, stage in stages:
        output = args.out_dir / f"hackylens-{identifier}-v{version}.zip"
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=9) as archive:
            for name in sorted(flat_names(identifier, version)):
                archive.write(stage / name, name)
        bundles.append(output)
    (args.out_dir / "SHA256SUMS.txt").write_text(
        "".join(f"{package_release.sha256(path)}  {path.name}\n" for path in sorted(bundles)),
        encoding="ascii", newline="\n",
    )
    for path in verify_public(args.out_dir, version):
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
