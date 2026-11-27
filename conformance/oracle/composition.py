"""Check neutral outcomes from static composition and admission."""

import re

from conformance.oracle.time import require


def check(case: dict, events: list[dict], limits: dict) -> None:
    del limits
    require(len(events) == len(case["expect"]), "missing or excess composition observations")
    for event, expected in zip(events, case["expect"], strict=True):
        require(event["event"] == "composition", "unexpected composition trace event")
        for field, wanted in expected.items():
            actual = event[field]
            require(type(actual) is type(wanted) and actual == wanted,
                    f"expected {field}={wanted!r}, observed {actual!r}")
    if case.get("same_registry"):
        fingerprints = [event["registry_sha256"] for event in events]
        require(all(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) for value in fingerprints),
                "registry fingerprint missing or malformed")
        require(len(set(fingerprints)) == 1,
                "equivalent manifest arrangements produced different registries")
