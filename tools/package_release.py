#!/usr/bin/env python3
"""Create versioned HackyLens firmware and SD-card artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import zipfile


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from gen_flash_layout import load_layout, load_validated, partition_by_name
from bootstrap_deps import LITTLEFS_REVISION, MICROPYTHON_REVISION

FLASH_LAYOUT_PATH = ROOT / "firmware" / "config" / "flash_layout.json"
_FLASH, _PARTITIONS = load_validated(FLASH_LAYOUT_PATH)
_FIRMWARE_PARTITION = partition_by_name(_PARTITIONS, "firmware")
K210_IMAGE_OVERHEAD = 37


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_release_firmware(path: Path, version: str) -> None:
    image = path.read_bytes()
    if b"-wdtfi" in image:
        raise SystemExit(
            "refusing to package a WDT fault-injection firmware image"
        )
    if version.encode("ascii") not in image:
        raise SystemExit(
            f"firmware image does not contain canonical VERSION={version!r}"
        )
    wrapped_size = len(image) + K210_IMAGE_OVERHEAD
    erased_size = (
        (wrapped_size + _FLASH["erase_size"] - 1) // _FLASH["erase_size"]
        * _FLASH["erase_size"]
    )
    if erased_size > _FIRMWARE_PARTITION["size"]:
        raise SystemExit(
            "firmware image exceeds canonical firmware partition: "
            f"raw={len(image)} wrapped={wrapped_size} erase={erased_size} "
            f"limit={_FIRMWARE_PARTITION['size']}"
        )


def ensure_allowlisted_output(directory: Path, expected_names: set[str]) -> None:
    """Refuse to publish a directory containing an unrecognised artifact."""

    if not directory.exists():
        return
    unexpected = sorted(
        path.relative_to(directory).as_posix()
        for path in directory.rglob("*")
        if path.is_file() and path.relative_to(directory).as_posix() not in expected_names
    )
    if unexpected:
        raise SystemExit(
            "release output directory is not clean; refusing unexpected files: "
            + ", ".join(unexpected)
        )


def firmware_dependency_manifest(
    version: str, embed: Path | None = None
) -> dict[str, object]:
    embed = embed or ROOT / "build" / "micropython_embed"
    if not embed.is_dir():
        raise SystemExit(
            "generated MicroPython embed package is missing; build full firmware first"
        )
    micropython_sources = sorted(
        path.relative_to(embed).as_posix()
        for path in embed.rglob("*.c")
        if path.relative_to(embed).as_posix() != "port/mphalport.c"
    )
    if not micropython_sources:
        raise SystemExit("generated MicroPython embed source list is empty")
    return {
        "schema_version": 1,
        "project": "HackyLens firmware",
        "firmware_version": version,
        "dependencies": [
            {
                "name": "MicroPython",
                "version": "1.28.0",
                "revision": MICROPYTHON_REVISION,
                "license": "MIT",
                "license_file": "licenses/MicroPython-LICENSE",
                "included_source_files": micropython_sources,
                "excluded_generated_port": "port/mphalport.c",
                "local_patch": (
                    "firmware/third_party/micropython/patches/"
                    "0001-poll-native-iterators.patch"
                ),
            },
            {
                "name": "littlefs",
                "version": "2.11.2",
                "revision": LITTLEFS_REVISION,
                "license": "BSD-3-Clause",
                "license_file": "licenses/littlefs-LICENSE.md",
                "included_source_files": [
                    "lfs.c", "lfs.h", "lfs_util.c", "lfs_util.h"
                ],
            },
        ],
    }


def write_firmware_notices(
    manifest_path: Path, archive_path: Path, version: str
) -> None:
    manifest = firmware_dependency_manifest(version)
    manifest_text = json.dumps(manifest, indent=2) + "\n"
    manifest_path.write_text(manifest_text, encoding="utf-8")

    dependency_notice = ROOT / "firmware" / "third_party" / "DEPENDENCIES.md"
    micropython_license = (
        ROOT / "firmware" / "third_party" / "micropython" / "LICENSE"
    )
    littlefs_license = (
        ROOT / "firmware" / "third_party" / "littlefs" / "LICENSE.md"
    )
    required = (dependency_notice, micropython_license, littlefs_license)
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit("firmware third-party notices are incomplete: " + ", ".join(missing))
    with zipfile.ZipFile(
        archive_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        archive.writestr("THIRD_PARTY_MANIFEST.json", manifest_text)
        archive.write(dependency_notice, "DEPENDENCIES.md")
        archive.write(micropython_license, "licenses/MicroPython-LICENSE")
        archive.write(littlefs_license, "licenses/littlefs-LICENSE.md")


def main() -> int:
    parser = argparse.ArgumentParser(description="Package a HackyLens release")
    parser.add_argument("--firmware", type=Path,
                        default=ROOT / "build" / "hackylens.bin")
    parser.add_argument("--sdcard", type=Path, default=ROOT / "sdcard")
    parser.add_argument("--out-dir", type=Path,
                        default=ROOT / "dist" / "release")
    parser.add_argument("--tag", help="Expected release tag, for example v0.2.0")
    args = parser.parse_args()

    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not version or (args.tag and args.tag != f"v{version}"):
        raise SystemExit(
            f"release tag {args.tag!r} does not match VERSION={version!r}")
    if not args.firmware.is_file():
        raise SystemExit(f"firmware image not found: {args.firmware}")
    validate_release_firmware(args.firmware, version)
    if not args.sdcard.is_dir():
        raise SystemExit(f"SD-card tree not found: {args.sdcard}")
    flash_layout = load_layout(FLASH_LAYOUT_PATH)

    firmware_out = args.out_dir / f"hackylens-v{version}.bin"
    sdcard_out = args.out_dir / f"hackylens-v{version}-sdcard.zip"
    metadata_out = args.out_dir / f"hackylens-v{version}.json"
    dependencies_out = (
        args.out_dir / f"hackylens-v{version}-third-party.json"
    )
    notices_out = (
        args.out_dir / f"hackylens-v{version}-third-party-notices.zip"
    )
    sums_out = args.out_dir / "SHA256SUMS.txt"
    expected_names = {
        firmware_out.name,
        sdcard_out.name,
        metadata_out.name,
        dependencies_out.name,
        notices_out.name,
        sums_out.name,
    }
    ensure_allowlisted_output(args.out_dir, expected_names)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(args.firmware, firmware_out)
    with zipfile.ZipFile(sdcard_out, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=9) as archive:
        for path in sorted(args.sdcard.rglob("*")):
            if path.is_file():
                archive.write(path, Path("sdcard") / path.relative_to(args.sdcard))
    write_firmware_notices(dependencies_out, notices_out, version)

    metadata = {
        "project": "HackyLens",
        "version": version,
        "firmware": firmware_out.name,
        "firmware_bytes": firmware_out.stat().st_size,
        "firmware_sha256": sha256(firmware_out),
        "sdcard": sdcard_out.name,
        "sdcard_sha256": sha256(sdcard_out),
        "flash_address": "0x000000",
        "flash_layout": flash_layout,
        "firmware_dependencies": dependencies_out.name,
        "firmware_dependencies_sha256": sha256(dependencies_out),
        "firmware_notices": notices_out.name,
        "firmware_notices_sha256": sha256(notices_out),
        "flasher_policy": {
            "firmware_max_exclusive": (
                f"0x{_FIRMWARE_PARTITION['offset'] + _FIRMWARE_PARTITION['size']:08X}"
            ),
            "preserve_partitions": [
                partition["name"] for partition in _PARTITIONS
                if partition["name"] != "firmware"
            ],
            "erase_size": f"0x{_FLASH['erase_size']:08X}",
            "k210_image_overhead_bytes": K210_IMAGE_OVERHEAD,
        },
    }
    metadata_out.write_text(json.dumps(metadata, indent=2) + "\n",
                            encoding="utf-8")
    artifacts = [firmware_out, sdcard_out, dependencies_out, notices_out]
    artifacts.append(metadata_out)
    sums_out.write_text(
        "".join(f"{sha256(path)}  {path.name}\n" for path in artifacts),
        encoding="ascii")
    for path in (*artifacts, sums_out):
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
