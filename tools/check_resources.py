"""Enforce firmware image, RAM and runtime-resource regression limits."""
import json
from pathlib import Path
import resource_checks as resources

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "docs/baselines/s7-resources.json"


def budget_failures(current, baseline):
    before = baseline["measurement"]
    limits = baseline["limits"]
    failures = []
    for section, field, limit in (
        ("elf", "static_ram_bytes", "static_ram_growth_bytes"),
        ("image", "raw_bytes", "raw_image_growth_bytes"),
    ):
        delta = current[section][field] - before[section][field]
        if delta > limits[limit]:
            failures.append(f"{field}: growth {delta} exceeds {limits[limit]}")
    return failures


def main():
    baseline = json.loads(BASELINE.read_text(encoding="utf-8"))
    current = resources.artifact_measurement(resources.artifact_paths())
    failures = budget_failures(current, baseline)
    failures += resources._new_direct_resources(baseline["commit"])
    if failures:
        raise SystemExit("[ERR] Firmware resources: " + "; ".join(failures))
    print("[OK] Firmware image/RAM budgets and no new direct runtime resources")
    print(json.dumps(current, indent=2))


if __name__ == "__main__":
    main()
