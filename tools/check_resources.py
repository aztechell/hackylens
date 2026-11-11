"""Report measured firmware size changes and check direct runtime resources."""
import json
from pathlib import Path
import resource_checks as resources

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "docs/baselines/s7-resources.json"


def size_deltas(current, previous):
    return {
        "static_ram_bytes": current["elf"]["static_ram_bytes"] - previous["elf"]["static_ram_bytes"],
        "raw_image_bytes": current["image"]["raw_bytes"] - previous["image"]["raw_bytes"],
    }


def main():
    baseline = json.loads(BASELINE.read_text(encoding="utf-8"))
    current = resources.artifact_measurement(resources.artifact_paths())
    failures = resources._new_direct_resources(baseline["commit"])
    if failures:
        raise SystemExit("[ERR] Firmware resources: " + "; ".join(failures))
    print("[OK] Firmware size measurement and direct runtime resource check")
    current["size_delta_bytes"] = size_deltas(current, baseline["measurement"])
    print(json.dumps(current, indent=2))


if __name__ == "__main__":
    main()
