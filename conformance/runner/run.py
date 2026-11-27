"""Score a conformance bundle without loading or executing its implementation."""

from __future__ import annotations

import argparse
from collections import defaultdict
import csv
import hashlib
import json
from pathlib import Path
import re

from conformance.oracle import composition, external_link, lifecycle, lights, shared_script, time


ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / "docs/spec/v0.1"
FIXTURE = ROOT / "conformance/fixtures/time.json"
LIFECYCLE_FIXTURE = ROOT / "conformance/fixtures/lifecycle.json"
COMPOSITION_FIXTURE = ROOT / "conformance/fixtures/composition.json"
LIGHTS_FIXTURE = ROOT / "conformance/fixtures/lights.json"
LINK_FIXTURE = ROOT / "conformance/fixtures/external_link.json"
SCRIPT_FIXTURE = ROOT / "conformance/fixtures/shared_script.json"
SCRIPT_RUNTIME_FIXTURE = ROOT / "conformance/fixtures/script_runtime.json"
REQUIREMENT_ID = r"(?:[A-Z]+-)+\d{2}"
OBSERVATIONS = {
    "CT-02": ("CMP-01", "CMP-02", "CMP-03"),
    "CT-03": ("LIF-01", "LIF-02", "LIF-03"),
    "CT-04": ("LIF-04", "TIM-01"),
    "CT-05": ("LIF-05", "LIF-06", "ERR-02"),
    "CT-06": ("LIF-07", "LIF-08"),
    "CT-07": ("LIF-09", "OWN-02"),
    "CT-08": ("OWN-01", "OWN-02", "OWN-03", "ERR-01"),
    "CT-09": ("S-TIME-01", "TIM-01", "TIM-02", "TIM-03"),
    "CT-11": ("S-LIGHT-01", "S-LIGHT-02"),
    "CT-14": ("S-LINK-01", "S-LINK-02"),
    "CT-17": ("SCR-01", "SCR-03"),
    "CT-18": ("SCR-04", "SCR-05", "OWN-05"),
    "CT-20": ("ARC-03", "SCR-02", "SCR-03", "SCR-05"),
}
QUALIFIED_COMPOSITION_CHECKS = ("equivalent-manifest-ordering", "preserve-disabled-autostart-id")


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def matrix() -> list[dict]:
    sources = {}
    for file in ("README.md", "SERVICES.md"):
        for identity in re.findall(rf"^- ({REQUIREMENT_ID}):", (SPEC / file).read_text(encoding="utf-8"), re.M):
            if identity in sources:
                raise ValueError(f"duplicate normative requirement {identity}")
            sources[identity] = file

    audit = (SPEC / "SPEC_RATIONALE.md").read_text(encoding="utf-8")
    if re.findall(rf"^\| \[({REQUIREMENT_ID})\]", audit, re.M) != list(sources):
        raise ValueError("the rationale must cover each normative requirement in source order")

    cases_by_id = defaultdict(list)
    plan = (SPEC / "CONFORMANCE.md").read_text(encoding="utf-8")
    for case, coverage in re.findall(r"^\| (CT-\d+) \| ([^|]+) \|", plan, re.M):
        for term in coverage.strip().split(", "):
            first, _, stop = term.partition("..")
            prefix, _, start = first.rpartition("-")
            for index in range(int(start), int(stop or start) + 1):
                cases_by_id[f"{prefix}-{index:02}"].append(case)

    with (SPEC / "requirements.csv").open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if [row["requirement_id"] for row in rows] != list(sources) or set(cases_by_id) != set(sources):
        raise ValueError("matrix or scenario plan does not match the normative requirement IDs")
    for row in rows:
        identity = row["requirement_id"]
        source = row["normative_source"].split("#", 1)[0]
        if source != sources[identity] or row["scenario_ids"].split() != cases_by_id[identity]:
            raise ValueError(f"matrix source or scenarios disagree with the specification for {identity}")
    return rows


def score_case(case: dict, traces: Path, limits: dict, oracle) -> dict:
    identity = case["id"]
    path = traces / f"{identity}.jsonl"
    if not path.is_file():
        return {"id": identity, "status": "NOT RUN", "reason": "Trace is missing."}
    evidence = {"path": f"traces/{path.name}", "sha256": digest(path)}
    try:
        events = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
        if not events or any(not isinstance(event, dict) or not isinstance(event.get("event"), str)
                             for event in events):
            raise ValueError("trace does not contain JSONL event objects")
    except (UnicodeError, ValueError) as error:
        return {"id": identity, "status": "NOT RUN", "reason": str(error), "evidence": evidence}
    try:
        oracle(case, events, limits)
    except lifecycle.TimestampRegression as error:
        return {"id": identity, "status": "FAIL", "reason": str(error), "evidence": evidence,
                "violated_requirement": "LIF-03"}
    except shared_script.ExitResultReplaced as error:
        return {"id": identity, "status": "FAIL", "reason": str(error), "evidence": evidence,
                "violated_requirement": "ERR-02"}
    except (KeyError, TypeError) as error:
        return {"id": identity, "status": "NOT RUN", "reason": f"Invalid trace fields: {error}",
                "evidence": evidence}
    except ValueError as error:
        return {"id": identity, "status": "FAIL", "reason": str(error), "evidence": evidence}
    return {"id": identity, "status": "PASS", "evidence": evidence}


def score(bundle: Path) -> dict:
    rows = matrix()
    manifest = read_json(bundle / "manifest.json")
    if manifest["schema_version"] != 1 or manifest["specification"] != "0.1.0":
        raise ValueError("unsupported manifest or specification version")
    fixture_digests = manifest["fixtures_sha256"]
    fixtures = {"CT-02": COMPOSITION_FIXTURE, "CT-09": FIXTURE, "CT-11": LIGHTS_FIXTURE,
                "lifecycle": LIFECYCLE_FIXTURE, "external_link": LINK_FIXTURE,
                "shared_script": SCRIPT_FIXTURE, "script_runtime": SCRIPT_RUNTIME_FIXTURE}
    if "CT-09" not in fixture_digests or not set(fixture_digests) <= set(fixtures):
        raise ValueError("manifest must declare CT-09 and may declare supported optional fixtures")
    for name, recorded in fixture_digests.items():
        if recorded != digest(fixtures[name]):
            raise ValueError(f"fixture fingerprint for {name} does not match this suite version")
    for key in ("implementation", "implementation_version", "implementation_revision",
                "environment", "configuration", "bindings", "input_sha256", "source_variant"):
        if not manifest[key]:
            raise ValueError(f"manifest requires {key}")
    scopes = set(manifest["evaluated_scopes"])
    if "Core" not in scopes or not scopes <= {row["scope"] for row in rows}:
        raise ValueError("evaluation scopes must include Core and use names from the matrix")
    if "Recovery" in scopes and "Shared Script" not in scopes:
        raise ValueError("Recovery evaluation includes Shared Script")
    if "Native Render" in scopes and "Display" not in scopes:
        raise ValueError("Native Render evaluation includes Display")
    limits = manifest["limits"]["Time"]
    for key in ("max_interval_us", "max_slice_us"):
        if not time.uint(limits[key]) or limits[key] == 0 or limits[key] == 2**64 - 1:
            raise ValueError(f"invalid declared Time limit {key}")

    cases = read_json(FIXTURE)
    for case in cases:
        if case.get("interval") == "max":
            case["interval_us"] = limits["max_interval_us"]
        if case.get("interval") == "over_max":
            case["interval_us"] = limits["max_interval_us"] + 1
    checks_by_scenario = {
        "CT-09": [score_case(case, bundle / "traces", limits, time.check) for case in cases],
    }
    violations = defaultdict(list)
    for check in checks_by_scenario["CT-09"]:
        if check["status"] == "FAIL":
            violations["S-TIME-01"].append(check["id"])
    if "CT-11" in fixture_digests and "Lights" not in scopes:
        raise ValueError("Lights observations require the Lights evaluation scope")
    if "external_link" in fixture_digests and "External Link" not in scopes:
        raise ValueError("connector observations require the External Link evaluation scope")
    if fixture_digests.keys() & {"shared_script", "script_runtime"} and "Shared Script" not in scopes:
        raise ValueError("script observations require the Shared Script evaluation scope")
    for scenario, oracle in (("CT-02", composition.check), ("CT-11", lights.check)):
        if scenario in fixture_digests:
            checks_by_scenario[scenario] = [score_case(case, bundle / "traces", limits, oracle)
                                           for case in read_json(fixtures[scenario])]
    for group, oracle in (("lifecycle", lifecycle.check), ("external_link", external_link.check),
                          ("shared_script", shared_script.check), ("script_runtime", shared_script.check)):
        if group in fixture_digests:
            for case in read_json(fixtures[group]):
                check = score_case(case, bundle / "traces", limits, oracle)
                checks_by_scenario.setdefault(case["scenario"], []).append(check)
                if violation := check.get("violated_requirement"):
                    violations[violation].append(check["id"])
    observed = {identity for scenario, checks in checks_by_scenario.items()
                if any(check["status"] in {"PASS", "FAIL"} for check in checks)
                for identity in OBSERVATIONS[scenario]}
    composition_results = {check["id"]: check for check in checks_by_scenario.get("CT-02", ())}
    composition_proof = [composition_results[identity] for identity in QUALIFIED_COMPOSITION_CHECKS
                         if identity in composition_results]
    for check in composition_proof:
        if check["status"] == "FAIL":
            violations["CMP-03"].append(check["id"])
    composition_complete = (len(composition_proof) == len(QUALIFIED_COMPOSITION_CHECKS)
                            and all(check["status"] == "PASS" for check in composition_proof))
    requirements = []
    for row in rows:
        identity = row["requirement_id"]
        result = {"id": identity, "scope": row["scope"], "scenarios": row["scenario_ids"].split()}
        if row["scope"] not in scopes:
            result.update(status="NOT APPLICABLE", reason="Outside the declared evaluation scope.")
        elif identity in violations:
            result.update(status="FAIL", reason="An independent observation violated this requirement.",
                          failed_checks=violations[identity])
        elif identity == "CMP-03" and composition_complete:
            result.update(status="PASS", reason="Equivalent manifest inputs produced identical registries. "
                          "Compiled lookup preserved the disabled app ID and the peer's identity.",
                          evidence=[check["evidence"] for check in composition_proof])
        elif identity in observed:
            result.update(status="NOT RUN", reason="The host observations provide partial evidence. "
                          "Full requirement evaluation is pending.")
        else:
            result.update(status="NOT RUN", reason="No independent result for this requirement.")
        requirements.append(result)
    return {
        "schema_version": 1,
        "manifest": manifest,
        "manifest_sha256": digest(bundle / "manifest.json"),
        "suite_sha256": {str(path.relative_to(ROOT).as_posix()): digest(path) for path in (
            Path(__file__), Path(time.__file__), Path(lifecycle.__file__), Path(composition.__file__),
            Path(lights.__file__), Path(external_link.__file__), Path(shared_script.__file__),
            *fixtures.values(),
            SPEC / "requirements.csv", SPEC / "README.md", SPEC / "SERVICES.md",
            SPEC / "CONFORMANCE.md", SPEC / "SPEC_RATIONALE.md",
        )},
        "requirements": requirements,
        "scenarios": [{"id": scenario, "covered_observations": list(OBSERVATIONS[scenario]),
                       "coverage": "partial", "checks": checks}
                      for scenario, checks in sorted(checks_by_scenario.items())],
    }


def render(report: dict) -> str:
    manifest = report["manifest"]
    lines = [
        f"# {manifest['implementation']} v{manifest['implementation_version']} evidence report",
        "",
        f"Implementation revision `{manifest['implementation_revision']}`.",
        f"Specification v{manifest['specification']}. Evaluated scopes "
        f"{', '.join(manifest['evaluated_scopes'])}.",
        f"Configuration {manifest['configuration']}.",
        "See the [manifest](manifest.json) and [machine-readable results](report.json).",
        "",
        "## Requirement results",
        "",
        "| ID | Scope | Status | Basis |",
        "| --- | --- | --- | --- |",
    ]
    for outcome in report["requirements"]:
        basis = outcome["reason"]
        if evidence := outcome.get("evidence"):
            basis += " " + ", ".join(f"[JSONL {index}]({item['path']})"
                                      for index, item in enumerate(evidence, 1))
        lines.append(f"| {outcome['id']} | {outcome['scope']} | {outcome['status']} | {basis} |")
    for scenario in report["scenarios"]:
        lines.extend(("", f"## {scenario['id']} host observations", "", "| Check | Status | Trace / finding |",
                      "| --- | --- | --- |"))
        for check in scenario["checks"]:
            evidence = check.get("evidence")
            detail = f"[JSONL]({evidence['path']})" if evidence else ""
            if reason := check.get("reason"):
                detail = f"{detail} {reason.replace('|', '/')}".strip()
            lines.append(f"| {check['id']} | {check['status']} | {detail} |")
    return "\n".join([*lines, ""])


def write_report(bundle: Path) -> dict:
    report = score(bundle)
    (bundle / "report.json").write_text(json.dumps(report, indent=2) + "\n",
                                        encoding="utf-8", newline="\n")
    (bundle / "report.md").write_text(render(report), encoding="utf-8", newline="\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path, nargs="?")
    parser.add_argument("--check-matrix", action="store_true")
    args = parser.parse_args()
    try:
        if args.check_matrix:
            print(f"Verified {len(matrix())} normative requirement IDs against the rationale and plan.")
        if args.bundle is None:
            if args.check_matrix:
                return 0
            parser.error("provide a bundle or --check-matrix")
        report = write_report(args.bundle)
    except (KeyError, OSError, TypeError, ValueError) as error:
        parser.exit(2, f"Cannot score conformance evidence: {error}\n")
    outcomes = {check["status"] for scenario in report["scenarios"] for check in scenario["checks"]}
    if "FAIL" in outcomes:
        return 1
    if "NOT RUN" in outcomes:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
