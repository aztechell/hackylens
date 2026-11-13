from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import zipfile


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import firmware_attestation
import firmware_sidecar
import package_release_bundles as bundles
import service_bindings


class ReleaseBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="hackylens-bundles-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        self.source = self.root / "firmware"
        self.source.mkdir()
        self.output = self.root / "release"
        self.stage = self.root / "stage"
        self.sdcard = self.root / "sdcard"
        self.sdcard.mkdir()
        (self.sdcard / "model.txt").write_text("model fixture", encoding="utf-8")
        for identifier in bundles.RELEASE_BOARDS:
            board = bundles.package_release.load_board(identifier)
            image = self.source / f"hackylens-full-{identifier}.bin"
            image.write_bytes(f"firmware {identifier}".encode())
            firmware_sidecar.write(image.with_suffix(".json"), image, board)
            self.attest(identifier)

    def attest(self, identifier, disabled_apps=None):
        board = bundles.package_release.load_board(identifier)
        image = self.source / f"hackylens-full-{identifier}.bin"
        composition = service_bindings.select(board, disabled_apps or set(), set(), set())
        firmware_attestation.write(
            image.with_suffix(".attestation.json"), image, board,
            target="full", disabled_apps=set(composition.disabled_apps),
            exclusions=list(composition.exclusions),
            capabilities_sha256="a" * 64,
        )

    def args(self):
        return [
            "--boards", *bundles.RELEASE_BOARDS, "--tag", f"v{self.version}",
            "--firmware-dir", str(self.source), "--sdcard", str(self.sdcard),
            "--stage-dir", str(self.stage), "--out-dir", str(self.output),
        ]

    def package(self):
        def notices(manifest, archive, version):
            manifest.write_text(json.dumps({"firmware_version": version}), encoding="utf-8")
            with zipfile.ZipFile(archive, "w") as target:
                target.writestr("licenses/test-LICENSE", "MIT fixture")
        with mock.patch.object(bundles.package_release, "write_firmware_notices",
                               side_effect=notices), contextlib.redirect_stdout(io.StringIO()):
            return bundles.main(self.args())

    def update_outer_sums(self):
        (self.output / "SHA256SUMS.txt").write_text("".join(
            f"{bundles.package_release.sha256(self.output / name)}  {name}\n"
            for name in sorted(bundles.bundle_names(self.version) | {bundles.sdcard_name(self.version)})
        ), encoding="ascii")

    def test_complete_board_packages_reference_one_shared_sdcard_archive(self):
        self.assertEqual(self.package(), 0)
        shared = bundles.sdcard_name(self.version)
        shared_hash = bundles.package_release.sha256(self.output / shared)
        self.assertEqual({p.name for p in self.output.iterdir()},
                         bundles.bundle_names(self.version) | {shared, "SHA256SUMS.txt"})
        self.assertEqual(len(bundles.verify_public(self.output, self.version)), 4)
        with zipfile.ZipFile(self.output / shared) as archive:
            self.assertEqual(archive.namelist(), ["sdcard/model.txt"])
            self.assertEqual(archive.read("sdcard/model.txt"), b"model fixture")
        for identifier in bundles.RELEASE_BOARDS:
            stem = f"hackylens-{identifier}-v{self.version}"
            with zipfile.ZipFile(self.output / f"{stem}.zip") as archive:
                self.assertEqual(set(archive.namelist()), {
                    f"{stem}.bin", f"{stem}.json", f"{stem}-attestation.json",
                    f"{stem}-release.json", f"{stem}-third-party.json",
                    f"{stem}-third-party-notices.zip", "SHA256SUMS.txt",
                })
                for name in archive.namelist():
                    self.assertEqual(archive.read(name), (self.stage / identifier / name).read_bytes())
                metadata = json.loads(archive.read(f"{stem}.json"))
                self.assertEqual(metadata["board_id"], identifier)
                self.assertEqual(metadata["image"], f"{stem}.bin")
                attestation = json.loads(archive.read(f"{stem}-attestation.json"))
                self.assertTrue(attestation["release_qualified"])
                self.assertEqual(attestation["composition"]["disabled_apps"],
                                 ["accelerometer", "recorder"] if identifier == "huskylens-sen0305" else [])
                manifest = json.loads(archive.read(f"{stem}-release.json"))
                self.assertEqual(manifest["board_id"], identifier)
                self.assertEqual(manifest["version"], self.version)
                self.assertEqual(manifest["sdcard"], shared)
                self.assertEqual(manifest["sdcard_sha256"], shared_hash)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(bundles.main([*self.args(), "--verify-only"]), 0)

    def test_feature_build_and_mismatched_sidecar_cannot_be_bundled(self):
        identifier = bundles.RELEASE_BOARDS[1]
        self.attest(identifier, {"pong"})
        with self.assertRaisesRegex(SystemExit, "release_qualified"):
            self.package()
        self.assertEqual(list(self.output.iterdir()), [])
        self.attest(identifier)
        image = self.source / f"hackylens-full-{identifier}.bin"
        image.write_bytes(b"tampered source image")
        with self.assertRaisesRegex(SystemExit, "sidecar validation failed"):
            self.package()
        self.assertEqual(list(self.output.iterdir()), [])

    def test_public_and_flat_staging_reject_stray_files(self):
        self.output.mkdir()
        (self.output / "diagnostic.bin").write_bytes(b"not a release")
        with mock.patch.object(bundles.package_release, "main") as package:
            with self.assertRaisesRegex(SystemExit, "unexpected files"):
                self.package()
            package.assert_not_called()
        (self.output / "diagnostic.bin").unlink()
        stage = self.stage / bundles.RELEASE_BOARDS[0]
        stage.mkdir(parents=True)
        (stage / "stray.log").write_text("debug", encoding="utf-8")
        with self.assertRaisesRegex(SystemExit, "unexpected files"):
            self.package()
        self.assertEqual(list(self.output.iterdir()), [])

    def test_verify_rejects_missing_extra_and_corrupt_public_files(self):
        self.package()
        stray = self.output / "extra.txt"
        stray.write_text("extra", encoding="utf-8")
        with self.assertRaisesRegex(SystemExit, "unexpected files"):
            bundles.verify_public(self.output, self.version)
        stray.unlink()
        path = self.output / sorted(bundles.bundle_names(self.version))[0]
        original = path.read_bytes()
        path.unlink()
        with self.assertRaisesRegex(SystemExit, "allowlist"):
            bundles.verify_public(self.output, self.version)
        path.write_bytes(original + b"corruption")
        with self.assertRaisesRegex(SystemExit, "bundle checksum mismatch"):
            bundles.verify_public(self.output, self.version)

    def test_verify_checks_inner_hashes_even_when_outer_checksum_matches(self):
        self.package()
        path = self.output / sorted(bundles.bundle_names(self.version))[0]
        with zipfile.ZipFile(path) as archive:
            members = {name: archive.read(name) for name in archive.namelist()}
        binary = next(name for name in members if name.endswith(".bin"))
        members[binary] += b"tampered"
        with zipfile.ZipFile(path, "w") as archive:
            for name, data in members.items():
                archive.writestr(name, data)
        self.update_outer_sums()
        with self.assertRaisesRegex(SystemExit, "member checksum mismatch"):
            bundles.verify_public(self.output, self.version)

    def test_verify_rejects_sdcard_replacement_even_when_outer_checksum_matches(self):
        self.package()
        with zipfile.ZipFile(self.output / bundles.sdcard_name(self.version), "w") as archive:
            archive.writestr("sdcard/model.txt", "different model")
        self.update_outer_sums()
        with self.assertRaisesRegex(SystemExit, "manifest does not match shared SD-card archive"):
            bundles.verify_public(self.output, self.version)

    def test_both_boards_and_matching_tag_are_required(self):
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                bundles.main(["--boards", bundles.RELEASE_BOARDS[0], "--tag", f"v{self.version}"])
        with self.assertRaisesRegex(SystemExit, "does not match VERSION"):
            bundles.main(["--boards", *bundles.RELEASE_BOARDS, "--tag", "v0.0.0"])


if __name__ == "__main__":
    unittest.main()
