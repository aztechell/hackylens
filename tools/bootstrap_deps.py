#!/usr/bin/env python3
"""Bootstrap local HackyLens dependencies under hackylens/_deps."""

from __future__ import annotations

import argparse
import ast
import binascii
import hashlib
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / "_deps"
DOWNLOADS = DEPS / "downloads"

SDK_DIR = DEPS / "kendryte-standalone-sdk"
KFLASH_REF_DIR = DEPS / "kflash.py-reference"
MICROPYTHON_DIR = DEPS / "micropython"
LITTLEFS_DIR = DEPS / "littlefs"
ISP_STUB = DEPS / "isp_prog.bin"

SDK_REPO = "https://github.com/kendryte/kendryte-standalone-sdk"
KFLASH_REPO = "https://github.com/sipeed/kflash.py"
MICROPYTHON_REPO = "https://github.com/micropython/micropython"
LITTLEFS_REPO = "https://github.com/littlefs-project/littlefs"
SDK_REVISION = "02576ba67e8797444f3ee3f34c625b5ed048e707"
KFLASH_REVISION = "550828c768b16ef329695d3f5eace3f6bcf14af2"
MICROPYTHON_REVISION = "e0e9fbb17ed6fd06bb76e266ae554784c9c80804"  # v1.28.0
LITTLEFS_REVISION = "adad0fbbcf5382c20978d07f94f9c13be9041c1b"  # v2.11.2
TOOLCHAIN_URL = (
    "https://github.com/kendryte/kendryte-gnu-toolchain/releases/download/"
    "v8.2.0-20190409/kendryte-toolchain-win-i386-8.2.0-20190409.tar.xz"
)
TOOLCHAIN_ARCHIVE = DOWNLOADS / "kendryte-toolchain-win-i386-8.2.0-20190409.tar.xz"
TOOLCHAIN_SHA256 = "ec8aa2bd3f1a42f4d227696c0d6d9baf32bef0e725d24d23b8e955521d4ae89e"


def run(cmd: list[str], cwd: Path | None = None) -> None:
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


def require_git() -> str:
    git = shutil.which("git")
    if not git:
        raise RuntimeError("git is required to validate pinned dependencies")
    return git


def git_output(git: str, path: Path, *args: str) -> str:
    result = subprocess.run(
        [git, "-C", str(path), *args],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout.strip()


def validate_git_checkout(path: Path, revision: str) -> None:
    git = require_git()
    if not path.is_dir():
        raise RuntimeError(f"missing Git checkout: {path}")
    try:
        inside = git_output(git, path, "rev-parse", "--is-inside-work-tree")
        head = git_output(git, path, "rev-parse", "HEAD").lower()
        dirty = git_output(
            git, path, "status", "--porcelain=v1", "--untracked-files=all"
        )
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(f"invalid Git checkout: {path}: {exc.stderr.strip()}") from exc
    if inside != "true":
        raise RuntimeError(f"dependency is not a Git worktree: {path}")
    if head != revision.lower():
        raise RuntimeError(
            f"dependency revision mismatch at {path}: expected {revision}, got {head}"
        )
    if dirty:
        raise RuntimeError(f"dependency checkout is dirty: {path}:\n{dirty}")
    print(f"[OK] pinned clean checkout: {path} @ {head}")


def ensure_git_checkout(path: Path, repo: str, revision: str) -> None:
    git = require_git()
    if path.exists():
        if not path.is_dir() or not (path / ".git").exists():
            raise RuntimeError(
                f"refusing non-Git dependency directory; remove it and retry: {path}"
            )
        print(f"[OK] checkout exists: {path}")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        run([git, "clone", "--no-checkout", repo, str(path)])

    try:
        git_output(git, path, "cat-file", "-e", f"{revision}^{{commit}}")
    except subprocess.CalledProcessError:
        run([git, "-C", str(path), "fetch", "--no-tags", "origin", revision])
    # Dependency trees are disposable caches. Reset both tracked and ignored
    # build outputs so a restored CI cache is byte-for-byte the pinned tree.
    run([git, "-C", str(path), "checkout", "--detach", "--force", revision])
    run([git, "-C", str(path), "reset", "--hard", revision])
    run([git, "-C", str(path), "clean", "-ffdx"])
    validate_git_checkout(path, revision)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_sha256(path: Path, expected: str) -> None:
    actual = file_sha256(path)
    if actual.lower() != expected.lower():
        raise RuntimeError(
            f"SHA-256 mismatch for {path}: expected {expected.lower()}, got {actual}"
        )


def download_file(url: str, out: Path, sha256: str, offline: bool = False) -> None:
    if out.is_file() and out.stat().st_size > 0:
        try:
            verify_sha256(out, sha256)
        except RuntimeError:
            if offline:
                raise
            print(f"[STALE] cached download failed SHA-256 verification: {out}")
            out.unlink()
        else:
            print(f"[OK] verified download: {out}")
            return
    if offline:
        raise RuntimeError(f"offline download cache missing: {out}")
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(out.suffix + ".part")
    print(f"[GET] {url}")
    try:
        with urllib.request.urlopen(url) as src, tmp.open("wb") as dst:
            shutil.copyfileobj(src, dst, length=1024 * 1024)
        verify_sha256(tmp, sha256)
        tmp.replace(out)
    except Exception:
        tmp.unlink(missing_ok=True)
        raise


def safe_extract_tar(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    root = destination.resolve()
    with tarfile.open(archive, "r:*") as tar:
        members = tar.getmembers()
        for member in members:
            target = (destination / member.name).resolve()
            if root != target and root not in target.parents:
                raise RuntimeError(f"refusing unsafe tar member: {member.name}")
        tar.extractall(destination, members=members)


def find_toolchain_bin() -> Path | None:
    names = ("riscv64-unknown-elf-gcc.exe", "riscv64-unknown-elf-gcc")
    for name in names:
        for gcc in DEPS.rglob(name):
            if gcc.is_file():
                return gcc.parent
    return None


def ensure_toolchain(offline: bool = False) -> Path:
    # Keep the original, revision-named archive in the cache and authenticate it
    # even when an extracted compiler is already present.
    download_file(TOOLCHAIN_URL, TOOLCHAIN_ARCHIVE, TOOLCHAIN_SHA256, offline)
    existing = find_toolchain_bin()
    if existing:
        print(f"[OK] toolchain: {existing}")
        return existing

    print(f"[EXTRACT] {TOOLCHAIN_ARCHIVE}")
    safe_extract_tar(TOOLCHAIN_ARCHIVE, DEPS)

    found = find_toolchain_bin()
    if not found:
        raise RuntimeError("toolchain extracted, but riscv64-unknown-elf-gcc was not found")
    print(f"[OK] toolchain: {found}")
    return found


def extract_isp_stub() -> None:
    if ISP_STUB.is_file() and ISP_STUB.stat().st_size > 0:
        print(f"[OK] ISP stub: {ISP_STUB}")
        return

    source = KFLASH_REF_DIR / "kflash.py"
    if not source.is_file():
        raise RuntimeError(f"kflash reference missing: {source}")

    text = source.read_text(encoding="utf-8")
    match = re.search(r"ISP_PROG\s*=\s*('(?:[^'\\]|\\.)*')", text)
    if not match:
        raise RuntimeError("could not locate ISP_PROG in kflash.py reference")

    compressed_hex = ast.literal_eval(match.group(1))
    data = zlib.decompress(binascii.unhexlify(compressed_hex))
    ISP_STUB.write_bytes(data)
    print(f"[OK] ISP stub: {ISP_STUB} ({len(data)} bytes)")


def write_env(toolchain_bin: Path) -> None:
    env_path = ROOT / "env.ps1"
    sdk = SDK_DIR.resolve()
    toolchain = toolchain_bin.resolve()
    content = f"""# Generated by hackylens/tools/bootstrap_deps.py
$env:KENDRYTE_SDK_DIR = "{sdk}"
$env:KENDRYTE_TOOLCHAIN_BIN = "{toolchain}"
$env:HACKYLENS_MICROPYTHON_DIR = "{MICROPYTHON_DIR.resolve()}"
$env:HACKYLENS_LITTLEFS_DIR = "{LITTLEFS_DIR.resolve()}"
$env:Path = "{toolchain};" + $env:Path
Write-Host "[HackyLens] KENDRYTE_SDK_DIR=$env:KENDRYTE_SDK_DIR"
Write-Host "[HackyLens] KENDRYTE_TOOLCHAIN_BIN=$env:KENDRYTE_TOOLCHAIN_BIN"
Write-Host "[HackyLens] HACKYLENS_MICROPYTHON_DIR=$env:HACKYLENS_MICROPYTHON_DIR"
Write-Host "[HackyLens] HACKYLENS_LITTLEFS_DIR=$env:HACKYLENS_LITTLEFS_DIR"
"""
    env_path.write_text(content, encoding="utf-8")
    print(f"[OK] env: {env_path}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Bootstrap local HackyLens dependencies")
    parser.add_argument("--skip-download", action="store_true", help="Only validate existing dependencies")
    args = parser.parse_args(argv)

    DEPS.mkdir(parents=True, exist_ok=True)
    checkouts = (
        (SDK_DIR, SDK_REPO, SDK_REVISION),
        (KFLASH_REF_DIR, KFLASH_REPO, KFLASH_REVISION),
        (MICROPYTHON_DIR, MICROPYTHON_REPO, MICROPYTHON_REVISION),
        (LITTLEFS_DIR, LITTLEFS_REPO, LITTLEFS_REVISION),
    )
    for path, repo, revision in checkouts:
        if args.skip_download:
            validate_git_checkout(path, revision)
        else:
            ensure_git_checkout(path, repo, revision)

    if not SDK_DIR.is_dir():
        raise SystemExit(f"missing SDK checkout: {SDK_DIR}")
    if not KFLASH_REF_DIR.is_dir():
        raise SystemExit(f"missing kflash reference checkout: {KFLASH_REF_DIR}")
    if not (MICROPYTHON_DIR / "ports" / "embed" / "embed.mk").is_file():
        raise SystemExit(f"missing MicroPython embed checkout: {MICROPYTHON_DIR}")
    if not (LITTLEFS_DIR / "lfs.c").is_file():
        raise SystemExit(f"missing littlefs checkout: {LITTLEFS_DIR}")

    toolchain = ensure_toolchain(args.skip_download)
    extract_isp_stub()
    write_env(toolchain)

    print("[DONE] run: . .\\env.ps1")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exc:
        print(f"[ERR] {exc}", file=sys.stderr)
        raise SystemExit(1)
