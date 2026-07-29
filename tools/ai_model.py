#!/usr/bin/env python3
"""Inspect, convert, package, and verify K210 AI models for HackyLens."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
from typing import Any
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_LOCK = ROOT / "models" / "toolchain.lock.json"
MANIFEST_BYTES = 256
MANIFEST_VERSION = 1
MAX_OUTPUTS = 4
ELEMENT_SIZES = {"u8": 1, "i8": 1, "f32": 4}
ELEMENT_IDS = {"u8": 0, "i8": 1, "f32": 2}
LAYOUT_IDS = {"chw": 0, "hwc": 1, "flat": 2}
NORMALIZATION_IDS = {"none": 0, "zero_to_one": 1, "negative_one_to_one": 2, "affine": 3}
POSTPROCESS_IDS = {"raw": 0, "classification": 1, "yolo2": 2, "embedding": 3}


class ModelError(ValueError):
    pass


def bootstrap_toolchain(output_dir: Path) -> Path:
    lock = json.loads(TOOLCHAIN_LOCK.read_text(encoding="utf-8"))
    compiler = lock["compiler"]
    expected_size = int(compiler["archive_bytes"])
    expected_sha256 = str(compiler["sha256"]).lower()
    output_dir.mkdir(parents=True, exist_ok=True)
    ncc = output_dir / "ncc"
    if ncc.is_file():
        return ncc

    with tempfile.TemporaryDirectory(prefix="hackylens-nncase-") as temp:
        archive = Path(temp) / "ncc.tar.xz"
        print(f"+ download {compiler['url']}")
        urllib.request.urlretrieve(compiler["url"], archive)
        if archive.stat().st_size != expected_size:
            raise ModelError("toolchain archive size does not match lock")
        if hashlib.sha256(archive.read_bytes()).hexdigest() != expected_sha256:
            raise ModelError("toolchain archive SHA-256 does not match lock")
        with tarfile.open(archive, "r:xz") as source:
            root = output_dir.resolve()
            for member in source.getmembers():
                destination = (output_dir / member.name).resolve()
                try:
                    destination.relative_to(root)
                except ValueError as exc:
                    raise ModelError(f"unsafe path in toolchain archive: {member.name}") from exc
            source.extractall(output_dir)
    if not ncc.is_file():
        raise ModelError("toolchain archive did not contain ncc")
    return ncc


def _word_swap32(data: bytes) -> bytes:
    if len(data) % 4:
        raise ModelError("word-swapped model size is not divisible by four")
    result = bytearray(len(data))
    for offset in range(0, len(data), 4):
        result[offset : offset + 4] = data[offset : offset + 4][::-1]
    return bytes(result)


def _parse_v3(data: bytes, endian: str) -> dict[str, Any] | None:
    if len(data) < 28:
        return None
    values = struct.unpack_from(f"{endian}7I", data)
    version, flags, arch, layers, max_start, main_mem, output_count = values
    if version != 3 or arch != 0 or output_count == 0 or output_count > MAX_OUTPUTS:
        return None
    header_bytes = 28 + output_count * 8
    if len(data) < header_bytes:
        return None
    outputs = []
    for index in range(output_count):
        address, size = struct.unpack_from(f"{endian}2I", data, 28 + index * 8)
        outputs.append({"index": index, "address": address, "bytes": size})
    return {
        "format": "kmodel-v3",
        "byte_order": "little" if endian == "<" else "word-swapped",
        "version": version,
        "flags": flags,
        "arch": arch,
        "layers_length": layers,
        "max_start_address": max_start,
        "main_mem_usage": main_mem,
        "output_count": output_count,
        "outputs": outputs,
    }


def _first_conv_input(data: bytes, model_info: dict[str, Any]) -> dict[str, Any] | None:
    layer_headers = 28 + model_info["output_count"] * 8
    body_start = layer_headers + model_info["layers_length"] * 8
    if len(data) < body_start + 24:
        return None
    layer_type, body_size = struct.unpack_from("<2I", data, layer_headers)
    if layer_type != 10240 or body_size < 24:
        return None
    layer_offset = struct.unpack_from("<I", data, body_start + 8)[0]
    if layer_offset > len(data) or len(data) - layer_offset < 64:
        return None
    image_channels = struct.unpack_from("<Q", data, layer_offset + 16)[0]
    image_size = struct.unpack_from("<Q", data, layer_offset + 24)[0]
    kernel_calc = struct.unpack_from("<Q", data, layer_offset + 56)[0]
    channels = (image_channels & 0x3FF) + 1
    width = (image_size & 0x3FF) + 1
    height = ((image_size >> 10) & 0x1FF) + 1
    channel_switch = kernel_calc & 0x7FFF
    return {
        "input_bytes": channel_switch * 64 * channels,
        "input_shape": [1, channels, height, width],
    }


def inspect_bytes(data: bytes) -> dict[str, Any]:
    parsed = _parse_v3(data, "<")
    if parsed:
        input_info = _first_conv_input(data, parsed)
        if input_info:
            parsed.update(input_info)
        return parsed
    parsed = _parse_v3(data, ">")
    if parsed:
        normalized = _word_swap32(data)
        native = _parse_v3(normalized, "<")
        input_info = _first_conv_input(normalized, native) if native else None
        if input_info:
            parsed.update(input_info)
        return parsed
    if data[:4] in (b"KMDL", b"LDMK"):
        return {
            "format": "nncase-kmodel",
            "byte_order": "little",
            "identifier_hex": data[:4].hex(),
        }
    if data[:4] == bytes.fromhex("12345678"):
        return {
            "format": "husky-object-data",
            "byte_order": "big",
            "note": "not a KModel; this is an original-firmware data store",
        }
    return {"format": "unknown", "byte_order": "unknown"}


def inspect_file(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    result = inspect_bytes(data)
    result.update(
        {
            "file": str(path),
            "bytes": len(data),
            "crc32": f"{binascii.crc32(data) & 0xFFFFFFFF:08x}",
            "sha256": hashlib.sha256(data).hexdigest(),
        }
    )
    return result


def _load_spec(path: Path) -> dict[str, Any]:
    try:
        spec = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ModelError(f"cannot read model spec: {exc}") from exc
    if spec.get("schema") != 1:
        raise ModelError("spec schema must be 1")
    return spec


def _enum(mapping: dict[str, int], value: Any, field: str) -> int:
    try:
        return mapping[str(value).lower()]
    except KeyError as exc:
        raise ModelError(f"unsupported {field}: {value!r}") from exc


def _tensor_bytes(tensor: dict[str, Any]) -> int:
    shape = tensor.get("shape")
    element = str(tensor.get("element", "")).lower()
    if not isinstance(shape, list) or not 1 <= len(shape) <= 4:
        raise ModelError("tensor shape must contain one to four dimensions")
    if element not in ELEMENT_SIZES:
        raise ModelError(f"unsupported tensor element: {element!r}")
    count = 1
    for dimension in shape:
        if not isinstance(dimension, int) or not 1 <= dimension <= 65535:
            raise ModelError(f"invalid tensor dimension: {dimension!r}")
        count *= dimension
    calculated = count * ELEMENT_SIZES[element]
    declared = tensor.get("bytes", calculated)
    if not isinstance(declared, int) or declared < calculated:
        raise ModelError(
            f"tensor byte size {declared} is smaller than shape payload ({calculated})"
        )
    return declared


def _ascii_field(value: str, size: int, field: str) -> bytes:
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ModelError(f"{field} must use ASCII") from exc
    if not encoded or len(encoded) >= size:
        raise ModelError(f"{field} must be 1..{size - 1} bytes")
    return encoded + bytes(size - len(encoded))


def _validate_spec(spec: dict[str, Any], model_info: dict[str, Any]) -> dict[str, Any]:
    if model_info["format"] != "kmodel-v3" or model_info["byte_order"] != "little":
        raise ModelError("packaging requires a native little-endian KModel v3")
    model_id = str(spec.get("id", ""))
    _ascii_field(model_id, 32, "id")
    input_tensor = spec.get("input")
    outputs = spec.get("outputs")
    if not isinstance(input_tensor, dict):
        raise ModelError("spec input must be an object")
    if not isinstance(outputs, list) or not 1 <= len(outputs) <= MAX_OUTPUTS:
        raise ModelError("spec outputs must contain one to four tensors")
    if len(outputs) != model_info["output_count"]:
        raise ModelError("spec output count does not match KModel header")
    input_bytes = _tensor_bytes(input_tensor)
    if model_info.get("input_bytes") != input_bytes:
        raise ModelError(
            f"input is {input_bytes} bytes, KModel first layer requires "
            f"{model_info.get('input_bytes', 'unknown')}"
        )
    output_bytes = []
    for index, output in enumerate(outputs):
        if not isinstance(output, dict):
            raise ModelError(f"output {index} must be an object")
        size = _tensor_bytes(output)
        if size != model_info["outputs"][index]["bytes"]:
            raise ModelError(
                f"output {index} is {size} bytes, KModel declares "
                f"{model_info['outputs'][index]['bytes']}"
            )
        output_bytes.append(size)
    labels = spec.get("labels", [])
    if not isinstance(labels, list) or any(not isinstance(label, str) for label in labels):
        raise ModelError("labels must be a string array")
    if len(labels) > 65535:
        raise ModelError("too many labels")
    postprocess = str(spec.get("postprocess", "raw")).lower()
    normalization = str(input_tensor.get("normalization", "none")).lower()
    return {
        "id": model_id,
        "input": input_tensor,
        "input_bytes": input_bytes,
        "outputs": outputs,
        "output_bytes": output_bytes,
        "labels": labels,
        "postprocess": postprocess,
        "postprocess_id": _enum(POSTPROCESS_IDS, postprocess, "postprocess"),
        "normalization": normalization,
        "normalization_id": _enum(NORMALIZATION_IDS, normalization, "normalization"),
        "input_element_id": _enum(ELEMENT_IDS, input_tensor.get("element"), "input element"),
        "input_layout_id": _enum(LAYOUT_IDS, input_tensor.get("layout"), "input layout"),
    }


def _write_manifest(
    validated: dict[str, Any],
    model_info: dict[str, Any],
    model_name: str,
    labels_name: str,
    model_data: bytes,
) -> bytes:
    raw = bytearray(MANIFEST_BYTES)
    raw[0:4] = b"HKAI"
    struct.pack_into("<HH", raw, 4, MANIFEST_VERSION, MANIFEST_BYTES)
    struct.pack_into("<II", raw, 12, len(model_data), binascii.crc32(model_data) & 0xFFFFFFFF)
    struct.pack_into(
        "<8I",
        raw,
        20,
        model_info["version"],
        model_info["flags"],
        model_info["arch"],
        model_info["layers_length"],
        model_info["max_start_address"],
        model_info["main_mem_usage"],
        model_info["output_count"],
        validated["input_bytes"],
    )
    shape = validated["input"]["shape"] + [0] * (4 - len(validated["input"]["shape"]))
    struct.pack_into("<4H", raw, 52, *shape)
    raw[60] = len(validated["input"]["shape"])
    raw[61] = validated["input_element_id"]
    raw[62] = validated["input_layout_id"]
    raw[63] = validated["normalization_id"]
    raw[64] = validated["postprocess_id"]
    raw[65] = len(validated["outputs"])
    struct.pack_into("<H", raw, 66, len(validated["labels"]))
    output_bytes = validated["output_bytes"] + [0] * (MAX_OUTPUTS - len(validated["output_bytes"]))
    struct.pack_into("<4I", raw, 68, *output_bytes)
    raw[84:116] = _ascii_field(validated["id"], 32, "id")
    raw[116:180] = _ascii_field(model_name, 64, "model file")
    if labels_name:
        raw[180:244] = _ascii_field(labels_name, 64, "labels file")
    struct.pack_into("<I", raw, 8, binascii.crc32(raw[12:]) & 0xFFFFFFFF)
    return bytes(raw)


def package_model(spec_path: Path, model_path: Path, output_dir: Path, normalize: bool) -> Path:
    spec = _load_spec(spec_path)
    model_data = model_path.read_bytes()
    model_info = inspect_bytes(model_data)
    if model_info.get("byte_order") == "word-swapped":
        if not normalize:
            raise ModelError("model is word-swapped; pass --normalize-word-swapped")
        model_data = _word_swap32(model_data)
        model_info = inspect_bytes(model_data)
    validated = _validate_spec(spec, model_info)
    model_name = str(spec.get("model_file", "model.kmodel"))
    labels_name = str(spec.get("labels_file", "labels.txt")) if validated["labels"] else ""
    _ascii_field(model_name, 64, "model file")
    if labels_name:
        _ascii_field(labels_name, 64, "labels file")

    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / model_name).write_bytes(model_data)
    if labels_name:
        (output_dir / labels_name).write_text(
            "".join(f"{label}\n" for label in validated["labels"]), encoding="utf-8", newline="\n"
        )
    manifest = _write_manifest(validated, model_info, model_name, labels_name, model_data)
    (output_dir / "manifest.hkai").write_bytes(manifest)
    summary = {
        "schema": 1,
        "id": validated["id"],
        "model_file": model_name,
        "model_bytes": len(model_data),
        "model_crc32": f"{binascii.crc32(model_data) & 0xFFFFFFFF:08x}",
        "model_sha256": hashlib.sha256(model_data).hexdigest(),
        "labels_file": labels_name or None,
        "label_count": len(validated["labels"]),
        "input": validated["input"],
        "outputs": validated["outputs"],
        "normalization": validated["normalization"],
        "postprocess": validated["postprocess"],
        "kmodel": {key: model_info[key] for key in (
            "version", "flags", "arch", "layers_length", "max_start_address",
            "main_mem_usage", "output_count"
        )},
    }
    (output_dir / "manifest.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n"
    )
    return output_dir


def _read_manifest(path: Path) -> dict[str, Any]:
    raw = path.read_bytes()
    if len(raw) != MANIFEST_BYTES or raw[:4] != b"HKAI":
        raise ModelError("invalid manifest size or magic")
    version, size = struct.unpack_from("<HH", raw, 4)
    if version != MANIFEST_VERSION or size != MANIFEST_BYTES:
        raise ModelError("unsupported manifest version")
    expected_crc = struct.unpack_from("<I", raw, 8)[0]
    actual_crc = binascii.crc32(raw[12:]) & 0xFFFFFFFF
    if expected_crc != actual_crc:
        raise ModelError("manifest CRC mismatch")

    def text(offset: int, size: int) -> str:
        return raw[offset : offset + size].split(b"\0", 1)[0].decode("ascii")

    contract = struct.unpack_from("<8I", raw, 20)
    return {
        "model_size": struct.unpack_from("<I", raw, 12)[0],
        "model_crc32": struct.unpack_from("<I", raw, 16)[0],
        "contract": contract,
        "input_shape": struct.unpack_from("<4H", raw, 52),
        "input_rank": raw[60],
        "input_element": raw[61],
        "input_layout": raw[62],
        "normalization": raw[63],
        "postprocess": raw[64],
        "output_count": raw[65],
        "label_count": struct.unpack_from("<H", raw, 66)[0],
        "output_bytes": struct.unpack_from("<4I", raw, 68),
        "id": text(84, 32),
        "model_file": text(116, 64),
        "labels_file": text(180, 64),
    }


def verify_package(package_dir: Path) -> dict[str, Any]:
    manifest = _read_manifest(package_dir / "manifest.hkai")
    model_path = package_dir / manifest["model_file"]
    model_data = model_path.read_bytes()
    if len(model_data) != manifest["model_size"]:
        raise ModelError("model size does not match manifest")
    if binascii.crc32(model_data) & 0xFFFFFFFF != manifest["model_crc32"]:
        raise ModelError("model CRC does not match manifest")
    info = inspect_bytes(model_data)
    if info.get("format") != "kmodel-v3" or info.get("byte_order") != "little":
        raise ModelError("package model is not a native KModel v3")
    contract = manifest["contract"]
    actual_contract = (
        info["version"], info["flags"], info["arch"], info["layers_length"],
        info["max_start_address"], info["main_mem_usage"], info["output_count"],
        info.get("input_bytes", 0),
    )
    if contract != actual_contract:
        raise ModelError("KModel header does not match manifest contract")
    for index in range(info["output_count"]):
        if info["outputs"][index]["bytes"] != manifest["output_bytes"][index]:
            raise ModelError(f"output {index} size does not match manifest")
    if manifest["labels_file"]:
        labels = (package_dir / manifest["labels_file"]).read_text(encoding="utf-8").splitlines()
        if len(labels) != manifest["label_count"]:
            raise ModelError("label count does not match manifest")
    return {
        "id": manifest["id"],
        "model": str(model_path),
        "bytes": len(model_data),
        "crc32": f"{manifest['model_crc32']:08x}",
        "outputs": info["outputs"],
        "labels": manifest["label_count"],
        "status": "ok",
    }


def convert_model(
    ncc: Path,
    source: Path,
    source_format: str,
    calibration: Path,
    output_model: Path,
    extra_args: list[str],
) -> None:
    command = [
        str(ncc),
        "compile",
        str(source),
        str(output_model),
        "-i",
        source_format,
        "-o",
        "k210model",
        "-t",
        "k210",
        "--dataset",
        str(calibration),
        "--inference-type",
        "uint8",
        "--calibrate-method",
        "l2",
        "--max-allocator-solve-secs",
        "60",
        *extra_args,
    ]
    print("+ " + " ".join(command))
    subprocess.run(command, check=True)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    inspect_parser = sub.add_parser("inspect", help="inspect a model or original data blob")
    inspect_parser.add_argument("model", type=Path)
    inspect_parser.add_argument("--normalize-out", type=Path)

    package_parser = sub.add_parser("package", help="create an SD-ready model package")
    package_parser.add_argument("--spec", required=True, type=Path)
    package_parser.add_argument("--model", required=True, type=Path)
    package_parser.add_argument("--out-dir", required=True, type=Path)
    package_parser.add_argument("--normalize-word-swapped", action="store_true")

    verify_parser = sub.add_parser("verify", help="verify an SD-ready model package")
    verify_parser.add_argument("package_dir", type=Path)

    bootstrap_parser = sub.add_parser("bootstrap", help="download and verify pinned legacy ncc")
    bootstrap_parser.add_argument(
        "--out-dir",
        type=Path,
        default=ROOT / "_deps" / "nncase-v0.1.0-rc5",
    )

    convert_parser = sub.add_parser("convert", help="run pinned legacy ncc and package its result")
    convert_parser.add_argument(
        "--ncc",
        type=Path,
        default=ROOT / "_deps" / "nncase-v0.1.0-rc5" / "ncc",
    )
    convert_parser.add_argument("--source", required=True, type=Path)
    convert_parser.add_argument("--format", required=True, choices=("tflite", "onnx", "caffe"))
    convert_parser.add_argument("--calibration", required=True, type=Path)
    convert_parser.add_argument("--spec", required=True, type=Path)
    convert_parser.add_argument("--out-dir", required=True, type=Path)
    convert_parser.add_argument("ncc_args", nargs=argparse.REMAINDER)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        if args.command == "inspect":
            data = args.model.read_bytes()
            result = inspect_file(args.model)
            if args.normalize_out:
                if result["byte_order"] != "word-swapped":
                    raise ModelError("--normalize-out requires a word-swapped KModel v3")
                normalized = _word_swap32(data)
                if inspect_bytes(normalized).get("byte_order") != "little":
                    raise ModelError("normalization did not produce a native KModel v3")
                args.normalize_out.parent.mkdir(parents=True, exist_ok=True)
                args.normalize_out.write_bytes(normalized)
                result["normalized_out"] = str(args.normalize_out)
            print(json.dumps(result, indent=2, sort_keys=True))
        elif args.command == "package":
            output = package_model(
                args.spec, args.model, args.out_dir, args.normalize_word_swapped
            )
            print(json.dumps(verify_package(output), indent=2, sort_keys=True))
        elif args.command == "verify":
            print(json.dumps(verify_package(args.package_dir), indent=2, sort_keys=True))
        elif args.command == "bootstrap":
            print(bootstrap_toolchain(args.out_dir))
        elif args.command == "convert":
            args.out_dir.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix="hackylens-convert-") as temp:
                converted = Path(temp) / "converted.kmodel"
                convert_model(
                    args.ncc, args.source, args.format, args.calibration,
                    converted, args.ncc_args
                )
                package_model(args.spec, converted, args.out_dir, False)
            print(json.dumps(verify_package(args.out_dir), indent=2, sort_keys=True))
    except (OSError, ModelError, subprocess.CalledProcessError) as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
