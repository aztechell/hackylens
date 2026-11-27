"""Verify shared-service effects and controlled producer/consumer handoff."""

from conformance.oracle.time import require, uint


class ExitResultReplaced(ValueError):
    """A rejected start replaced the earlier worker's terminal failure."""


def check(case: dict, events: list[dict], limits: dict) -> None:
    del limits
    count = len(case["actions"])
    cycles = case.get("repeat", 1)
    require(len(events) == count * cycles, "missing or excess script observations")
    increments = case.get("cumulative_per_cycle", {})
    event_type = case.get("event", "shared_script")
    seen_runs = set()
    for cycle in range(cycles):
        observation = events[cycle * count:(cycle + 1) * count]
        for action, event, expected in zip(case["actions"], observation, case["expect"], strict=True):
            require(event["event"] == event_type and event["action"] == action,
                    f"cycle {cycle + 1}: expected observation of {action}")
            for field, wanted in expected.items():
                actual = event[field]
                if field in increments:
                    wanted += cycle * increments[field]
                if (case.get("preserve_terminal_result") and action in {"try-start", "try-invalid-start"}
                        and field == "exit_reason" and actual != wanted):
                    raise ExitResultReplaced(f"{action}: previous terminal failure {wanted!r} became {actual!r}")
                require(type(actual) is type(wanted) and actual == wanted,
                        f"{action}: expected {field}={wanted!r}, observed {actual!r}")
            if action == "script-prepare":
                run = event["run"]
                require(uint(run) and run != 0 and run not in seen_runs, "duplicate or missing fixture run ID")
                seen_runs.add(run)
