"""Verify normalized outcomes, authority, progress and effects on a controlled link."""

from conformance.oracle.time import require, uint


def check(case: dict, events: list[dict], limits: dict) -> None:
    del limits
    require(len(events) == len(case["actions"]), "missing or excess connector observations")
    for action, event, expected in zip(case["actions"], events, case["expect"], strict=True):
        require(event["event"] == "external_link" and event["action"] == action,
                f"expected observation of {action}")
        for field, wanted in expected.items():
            actual = event[field]
            require(type(actual) is type(wanted) and actual == wanted,
                    f"{action}: expected {field}={wanted!r}, observed {actual!r}")
    if token_indices := case.get("different_tokens_at"):
        tokens = [events[index]["token"] for index in token_indices]
        require(all(uint(token) and token != 0 for token in tokens) and len(set(tokens)) == len(tokens),
                "operation identity was reused after reopening the connector")
