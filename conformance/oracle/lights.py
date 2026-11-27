"""Compare authority, outcomes and side effects in the Lights traces."""

from conformance.oracle.time import require


def check(case: dict, events: list[dict], limits: dict) -> None:
    del limits
    require(len(events) == len(case["actions"]), "missing or excess Lights observations")
    for action, event, expected in zip(case["actions"], events, case["expect"], strict=True):
        require(event["event"] == "lights" and event["action"] == action, "unexpected Lights action")
        actual = [event[key] for key in ("outcome", "effects", "active", "safe_off")]
        require(actual == expected, f"{action}: expected {expected!r}, observed {actual!r}")
