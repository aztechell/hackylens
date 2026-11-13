#!/usr/bin/env python3
"""Prepare the public firmware files and shared SD-card archive."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import zipfile


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))
import package_release

ROOT = TOOLS_DIR.parent
RELEASE_BOARDS = ("huskylens-sen0305", "sipeed-maix-cube")
NOTICES_NAME = "THIRD_PARTY_NOTICES.txt"
CHECKSUMS_NAME = "SHA256SUMS.txt"


def firmware_stem(identifier: str, version: str) -> str:
    return f"hackylens-{identifier}-v{version}"


def board_members(identifier: str, version: str) -> set[str]:
    stem = firmware_stem(identifier, version)
    return {stem + suffix for suffix in (
        ".bin", ".json", "-attestation.json", "-release.json",
        "-third-party.json", "-third-party-notices.zip",
    )} | {"SHA256SUMS.txt"}


def public_names(version: str) -> set[str]:
    return {
        firmware_stem(identifier, version) + suffix
        for identifier in RELEASE_BOARDS
        for suffix in (".bin", ".json")
    } | {sdcard_name(version), NOTICES_NAME}


def sdcard_name(version: str) -> str:
    return f"hackylens-sdcard-v{version}.zip"


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


def verify_public(directory: Path, version: str, sdcard: Path) -> list[Path]:
    names = public_names(version)
    exact_directory(directory, names | {CHECKSUMS_NAME})
    checksums = read_sums((directory / CHECKSUMS_NAME).read_text(encoding="ascii"), names)
    for name, checksum in checksums.items():
        if package_release.sha256(directory / name) != checksum:
            raise SystemExit(f"release file checksum mismatch: {name}")
    for identifier in RELEASE_BOARDS:
        stem = firmware_stem(identifier, version)
        board = package_release.load_board(identifier)
        image = directory / f"{stem}.bin"
        package_release.validate_release_firmware(image, board)
        try:
            package_release.read_and_validate_sidecar(image.with_suffix(".json"), image, board)
        except package_release.SidecarError as exc:
            raise SystemExit(f"published firmware sidecar validation failed: {exc}") from exc
    source_files = {
        (Path("sdcard") / path.relative_to(sdcard)).as_posix(): path
        for path in sdcard.rglob("*") if path.is_file()
    }
    with zipfile.ZipFile(directory / sdcard_name(version)) as archive:
        members = archive.namelist()
        if not source_files or len(members) != len(source_files) or set(members) != source_files.keys():
            raise SystemExit("shared SD-card archive contents do not match source")
        for name, path in source_files.items():
            with archive.open(name) as source:
                digest = hashlib.file_digest(source, "sha256").hexdigest()
            if digest != package_release.sha256(path):
                raise SystemExit(f"shared SD-card archive member mismatch: {name}")
    return [directory / name for name in sorted(names | {CHECKSUMS_NAME})]


def write_public_notices(destination: Path) -> None:
    licenses = (
        ("HackyLens", ROOT / "LICENSE"),
        ("Kendryte standalone SDK", ROOT / "_deps/kendryte-standalone-sdk/LICENSE"),
        ("MicroPython", ROOT / "firmware/third_party/micropython/LICENSE"),
        ("littlefs", ROOT / "firmware/third_party/littlefs/LICENSE.md"),
        ("quirc", ROOT / "firmware/third_party/quirc/LICENSE"),
        ("AprilTag", ROOT / "firmware/third_party/apriltag/LICENSE.md"),
    )
    sections = ["HackyLens firmware license notices for SEN0305 and Maix Cube"]
    for title, path in licenses:
        if not path.is_file():
            raise SystemExit(f"missing license for {title}: {path}")
        sections.append(f"{title}\n{'=' * len(title)}\n\n{path.read_text(encoding='utf-8').strip()}")
    destination.write_text("\n\n".join(sections) + "\n", encoding="utf-8", newline="\n")


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
        for path in verify_public(args.out_dir, version, args.sdcard):
            print(path)
        return 0

    stage_root, output_root = args.stage_dir.resolve(), args.out_dir.resolve()
    if stage_root == output_root or stage_root in output_root.parents or output_root in stage_root.parents:
        raise SystemExit("release staging and public output directories must be separate")
    shared_sdcard = sdcard_name(version)
    names = public_names(version)
    expected = names | {CHECKSUMS_NAME}
    package_release.ensure_allowlisted_output(args.out_dir, expected)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    package_release.validate_output_paths(args.out_dir, [args.out_dir / name for name in expected])
    sdcard_archive = args.stage_dir / shared_sdcard
    notices = args.stage_dir / NOTICES_NAME
    package_release.validate_output_paths(args.stage_dir, [sdcard_archive, notices])
    args.stage_dir.mkdir(parents=True, exist_ok=True)
    package_release.write_sdcard_archive(args.sdcard, sdcard_archive)
    write_public_notices(notices)
    sdcard_checksum = package_release.sha256(sdcard_archive)
    stages = []
    for identifier in args.boards:
        stage = args.stage_dir / identifier
        package_release.main([
            "--board", identifier,
            "--tag", args.tag,
            "--firmware", str(args.firmware_dir / f"hackylens-full-{identifier}.bin"),
            "--sdcard-archive", str(sdcard_archive),
            "--out-dir", str(stage),
        ])
        exact_directory(stage, board_members(identifier, version))
        stem = firmware_stem(identifier, version)
        manifest = json.loads((stage / f"{stem}-release.json").read_text(encoding="utf-8"))
        if (manifest["sdcard"], manifest["sdcard_sha256"]) != (shared_sdcard, sdcard_checksum):
            raise SystemExit(f"release manifest does not match shared SD-card archive: {stem}")
        stages.append((stem, stage))
    # Copy the public files after both staged builds have passed qualification.
    shutil.copy2(sdcard_archive, args.out_dir / shared_sdcard)
    shutil.copy2(notices, args.out_dir / NOTICES_NAME)
    for stem, stage in stages:
        for suffix in (".bin", ".json"):
            name = stem + suffix
            shutil.copy2(stage / name, args.out_dir / name)
    (args.out_dir / CHECKSUMS_NAME).write_text(
        "".join(f"{package_release.sha256(args.out_dir / name)}  {name}\n" for name in sorted(names)),
        encoding="ascii", newline="\n",
    )
    for path in verify_public(args.out_dir, version, args.sdcard):
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
