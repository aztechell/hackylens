"""Rebuild the comparable run index and summaries from archived K210 studies."""

from __future__ import annotations

import argparse
from collections import defaultdict
import gzip
import hashlib
import json
import math
from pathlib import Path
from statistics import median


ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / "docs/research"
SOURCES = {
    "object-gate-cube": ("object-gate", "object-gate-cube-runs.jsonl"),
    "object-gate-sen": ("object-gate", "object-gate-sen-runs.jsonl"),
    "sky-glide": ("sky-glide", "sky-glide-runs.jsonl.gz"),
    "uart-logger-cube": ("uart-logger", "uart-logger-cube-runs.jsonl.gz"),
    "uart-logger-sen": ("uart-logger", "uart-logger-sen-runs.jsonl.gz"),
}


def source_record(path: Path) -> dict:
    data = path.read_bytes()
    if path.suffix != ".gz":
        data = data.replace(b"\r\n", b"\n")
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": hashlib.sha256(data).hexdigest()}


def object_gate(row: dict) -> dict:
    frames = row["frames"]
    return {
        "frames": frames,
        "presented": row["presented"],
        "entries": row["entries"],
        "kpu_submissions": row["kpu_count"],
        "fps": frames * 1_000_000 / row["window_us"],
        "model_load_ms": row["load_us"] / 1000,
        **{f"{key}_us_per_frame": row[f"{key}_us"] / frames
           for key in ("detect", "kpu", "policy", "present")},
    }


def sky_glide(mode: str, row: dict) -> dict:
    values = row["metrics"]
    if mode == "logic":
        return {"steps": values["steps"], "active_us": values["us"],
                "steps_per_second": values["steps"] * 1_000_000 / values["us"]}
    if mode == "input":
        return {"presses": values["presses"], "input_to_lcd_median_us": median(values["samples_us"])}
    if mode == "memory":
        return {f"{key}_bytes": values[key] for key in ("gc_total", "before", "sampled_peak", "after_gc")}
    if mode in {"paced", "burst"}:
        return {**{key: values[key] for key in ("completed", "requested", "missed", "fps", "failed")},
                "frame_interval_median_us": values["interval_us"]["median"],
                "provider_median_us": values["provider_us"]["median"]}
    raise ValueError(f"unknown Sky Glide mode {mode}")


def uart_logger(mode: str, row: dict) -> dict:
    values = row["metrics"]
    if mode == "parse":
        return {"packets": values["count"], "active_us": values["active_us"],
                "packets_per_second": values["count"] * 1_000_000 / values["active_us"]}
    if mode in {"graph", "record", "memory"}:
        return {**{key: values[key] for key in ("frames", "fps", "missing_frames", "unmatched_marks")},
                **{f"{key}_median_ms": values[f"{key}_ms"]["median"]
                   for key in ("display", "interval", "sample_to_lcd")}}
    raise ValueError(f"unknown UART Logger mode {mode}")


def normalize(workload: str, row: dict, evidence: dict, path: str, line: int) -> dict:
    board = row["board"]
    mode = row.get("mode", "tensor-replay")
    variant = row.get("language", row.get("path"))
    valid = row.get("ok", True)
    if type(valid) is not bool or variant not in {"c", "python", "combined", "legacy"}:
        raise ValueError("invalid outcome or variant")

    if workload == "sky-glide":
        image = evidence["boards"][board]["diagnostic_image"]["sha256"]
        script = evidence["script"]["sha256"]
        image_basis = "board diagnostic metadata"
        metrics = sky_glide(mode, row)
    else:
        image = row["image_sha256"]
        script = row["source_sha256"]
        image_basis = "recorded per trial"
        if workload == "object-gate":
            metrics = object_gate(row)
        else:
            if image != evidence["diagnostic_image_sha256"] or script != evidence["logger_python_source_sha256"]:
                raise ValueError("trial image/script fingerprints disagree with board metadata")
            metrics = uart_logger(mode, row) if valid else {}
    if not all(type(value) in {int, float} and math.isfinite(value) and value >= 0
               for value in metrics.values()):
        raise ValueError("metrics must be finite and nonnegative")
    digest = None
    if workload == "object-gate":
        digest = row["digest"]
    elif valid and mode in {"logic", "parse"}:
        digest = row["metrics"]["digest"]

    return {
        "workload": workload, "board": board, "mode": mode, "variant": variant,
        "run": row["run"], "attempt": row.get("attempt"), "valid": valid,
        "date_utc": row.get("date_utc"), "failure": row["error"] if not valid else None,
        "image_sha256": image, "image_basis": image_basis, "script_sha256": script,
        "firmware_version": row.get("firmware_version"), "metrics": metrics, "workload_digest": digest,
        "raw": {"path": path, "line": line},
    }


def read_trials() -> tuple[list[dict], dict]:
    trials = []
    sources = {}
    for name, (workload, filename) in SOURCES.items():
        raw = RESEARCH / filename
        metadata = RESEARCH / f"{name}-evidence.json"
        evidence = json.loads(metadata.read_text(encoding="utf-8"))
        raw_record = source_record(raw)
        sources[name] = {"runs": raw_record, "metadata": source_record(metadata)}
        opener = gzip.open if raw.suffix == ".gz" else open
        with opener(raw, mode="rt", encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, start=1):
                try:
                    trials.append(normalize(workload, json.loads(line), evidence, raw_record["path"], line_number))
                except (ValueError, KeyError, TypeError, ZeroDivisionError) as error:
                    raise ValueError(f"{raw_record['path']}:{line_number}: {error}") from error
    return trials, sources


def summarize(trials: list[dict]) -> list[dict]:
    fields = ("workload", "board", "mode", "variant", "image_sha256", "script_sha256")
    groups = defaultdict(list)
    for trial in trials:
        groups[tuple(trial[key] for key in fields)].append(trial)

    summaries = []
    for group, rows in sorted(groups.items()):
        valid = [row for row in rows if row["valid"]]
        metrics = defaultdict(list)
        for row in valid:
            for metric, value in row["metrics"].items():
                metrics[metric].append(value)
        summary = {}
        for name, values in sorted(metrics.items()):
            middle = median(values)
            summary[name] = {"n": len(values), "median": middle, "min": min(values), "max": max(values),
                             "mad": median(abs(value - middle) for value in values) if len(values) > 1 else None}
        summaries.append({
            **dict(zip(fields, group, strict=True)), "attempts": len(rows), "valid": len(valid),
            "failed": len(rows) - len(valid), "metrics": summary,
        })
    return summaries


def replay_digests(trials: list[dict]) -> list[dict]:
    groups = defaultdict(lambda: defaultdict(set))
    for trial in trials:
        if trial["workload_digest"] is not None:
            key = trial["workload"], trial["board"], trial["mode"], trial["image_sha256"]
            groups[key][trial["variant"]].add(trial["workload_digest"])
    return [
        {"workload": workload, "board": board, "mode": mode, "image_sha256": image,
         "digests_by_variant": {variant: sorted(values) for variant, values in sorted(variants.items())},
         "match": len(variants) > 1 and len(set.union(*variants.values())) == 1}
        for (workload, board, mode, image), variants in sorted(groups.items())
    ]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    trials, sources = read_trials()
    groups = summarize(trials)
    valid = sum(trial["valid"] for trial in trials)
    report = {
        "schema_version": 1, "analysis": source_record(Path(__file__).resolve()),
        "sources": sources, "attempts": len(trials), "valid": valid, "failed": len(trials) - valid,
        "replay_digests": replay_digests(trials), "groups": groups,
    }
    args.destination.mkdir(parents=True, exist_ok=True)
    (args.destination / "trials.jsonl").write_text(
        "".join(json.dumps(trial, sort_keys=True, allow_nan=False) + "\n" for trial in trials),
        encoding="utf-8", newline="\n")
    (args.destination / "summary.json").write_text(
        json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8", newline="\n")
    print(f"{len(trials)} archived trials, {valid} accepted, {len(trials) - valid} failures, {len(groups)} groups")


if __name__ == "__main__":
    main()
