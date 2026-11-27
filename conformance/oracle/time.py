"""Score neutral Time traces using specification outcomes and invariants."""

from __future__ import annotations


def check(case: dict, events: list[dict], limits: dict) -> None:
    operation = case["operation"]
    if operation == "sleep":
        check_sleep(case, events, limits)
    elif operation == "deadline":
        check_deadline(case, events)
    elif operation == "read":
        check_read(case, events)
    else:
        raise ValueError(f"unknown operation {operation}")


def require(condition: bool, explanation: str) -> None:
    if not condition:
        raise ValueError(explanation)


def uint(value: object) -> bool:
    return type(value) is int and 0 <= value < 2**64


def check_sleep(case: dict, events: list[dict], limits: dict) -> None:
    target = case["target_us"]
    deadline = case["deadline_us"]
    cancellation = case["cancel_at_us"]
    now = case["start_us"]
    last_clock = None
    wait_count = 0
    require(bool(events), "missing sleep observation")
    for event in events[:-1]:
        kind = event["event"]
        require(uint(event["now_us"]) and event["now_us"] == now,
                "trace contradicts the controlled clock")
        if kind == "clock":
            last_clock = now
        elif kind == "cancel_probe":
            require(cancellation is not None and event["active"] is (now >= cancellation),
                    "cancellation probe contradicts the fixture")
        elif kind == "wait":
            duration = event["duration_us"]
            require(last_clock == now, "wait did not observe the current clock")
            require(uint(duration) and 0 < duration <= limits["max_slice_us"],
                    "wait exceeded the declared check bound")
            require(now + duration <= min(target, deadline),
                    "wait exceeded the original target or deadline")
            require(cancellation is None or now < cancellation,
                    "new wait began after cancellation was observable")
            wait_count += 1
            last_clock = None
            if not case["freeze"]:
                now += duration
        else:
            raise ValueError(f"unexpected sleep trace event {kind}")

    result = events[-1]
    expected = case["expect"]
    require(result["event"] == "sleep_result" and result["outcome"] == expected,
            f"expected {expected}, observed {result.get('outcome')}")
    require(uint(result["now_us"]) and result["now_us"] == now,
            "terminal clock disagrees with observed waits")
    require(bool(wait_count) is case["waits"], "unexpected presence or absence of wait")
    if expected == "success":
        require(last_clock == now and now >= target, "success did not observe the target")
    if expected == "cancelled":
        require(any(event["event"] == "cancel_probe" and event["active"]
                    for event in events[:-1]), "cancellation was never observed")
        require(last_clock == now and now < target and now >= cancellation,
                "cancellation was reported before its trigger or after target")
        require(now - max(cancellation, case["start_us"]) <= limits["max_slice_us"],
                "cancellation exceeded the declared check bound")
    if expected == "deadline":
        require(last_clock == now and deadline <= now < target,
                "expiry did not observe the original deadline before target")
    if case["freeze"]:
        require(wait_count > 0 and last_clock == case["start_us"],
                "frozen clock failure did not observe a lack of progress")


def check_deadline(case: dict, events: list[dict]) -> None:
    now = case["start_us"]
    expected = case["expect"]
    deadline = now + case["interval_us"] if expected == "success" else None
    require(len(events) >= 2, "missing deadline observations")
    require(all(event == {"event": "clock", "now_us": now} for event in events[:-1]),
            "deadline construction did not observe the fixture clock")
    require(events[-1] == {"event": "deadline_result", "outcome": expected, "value": deadline},
            f"expected {expected} with value {deadline}, observed {events[-1]}")


def check_read(case: dict, events: list[dict]) -> None:
    samples = case["samples_us"]
    require(len(events) == 2 * len(samples), "missing or extra read observations")
    for i, (sample, expected) in enumerate(zip(samples, case["expect"], strict=True)):
        require(events[2 * i] == {"event": "clock", "now_us": sample},
                "clock read did not sample the supplied value")
        value = sample if expected == "success" else None
        require(events[2 * i + 1] == {"event": "read_result", "outcome": expected, "value": value},
                f"expected {expected} for clock sample {sample}, observed {events[2 * i + 1]}")
