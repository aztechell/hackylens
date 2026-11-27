"""Run controlled source mutations through the production collector and independent scorer."""

from __future__ import annotations

import argparse
from collections.abc import Callable
import json
from pathlib import Path
import tempfile

from conformance.adapters.k210.collect import sha256
from conformance.runner.run import write_report


def checks(bundle: Path) -> dict[str, str]:
    report = write_report(bundle)
    return {check["id"]: check["status"]
            for scenario in report["scenarios"] for check in scenario["checks"]}


def main(description: str, definition: Path, production: Path, collect: Callable[[Path, Path], None],
         mutations: dict[str, tuple[str, str, set[str]]]) -> int:
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    destination = args.destination
    destination.mkdir(parents=True, exist_ok=False)
    initial = production.read_text(encoding="utf-8")
    control = destination / "control"
    collect(control, production)
    baseline = checks(control)
    if not baseline or set(baseline.values()) != {"PASS"}:
        raise RuntimeError("the unmodified control must pass every check before mutation testing")

    results = []
    with tempfile.TemporaryDirectory(prefix="hackylens-mutant-") as directory:
        source = Path(directory) / production.name
        for identity, (original, mutant, expected) in mutations.items():
            if initial.count(original) != 1 or original == mutant:
                raise ValueError(f"expected one source match for mutation {identity}")
            if not expected or not expected <= baseline.keys():
                raise ValueError(f"unknown or empty expected check set for mutation {identity}")
            source.write_bytes(initial.replace(original, mutant, 1).encode("utf-8"))
            bundle = destination / identity
            collect(bundle, source)
            statuses = checks(bundle)
            failed = {check for check, status in statuses.items() if status == "FAIL"}
            incomplete = {check for check, status in statuses.items() if status == "NOT RUN"}
            incomplete.update(baseline.keys() - statuses.keys())
            results.append({
                "mutation": identity, "detected": expected <= failed and not incomplete,
                "expected_failures": sorted(expected), "observed_failures": sorted(failed),
                "not_run": sorted(incomplete), "result": f"{identity}/report.json",
                "mutated_source_sha256": sha256(source),
            })

    summary = {"mutation_source_sha256": sha256(definition), "runner_sha256": sha256(Path(__file__)),
               "production_sha256": sha256(production), "control": "control/report.json", "results": results}
    (destination / "summary.json").write_text(json.dumps(summary, indent=2) + "\n",
                                               encoding="utf-8", newline="\n")
    for result in results:
        label = "DETECTED" if result["detected"] else "MISSED"
        print(f"{label} {result['mutation']}: {', '.join(result['observed_failures'])}")
    return 0 if all(result["detected"] for result in results) else 1
