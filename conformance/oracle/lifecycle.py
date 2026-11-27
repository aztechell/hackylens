"""Compare lifecycle observations with the state and ordering contract."""

from __future__ import annotations

from conformance.oracle.time import require, uint


class TimestampRegression(ValueError):
    """An accepted application event regressed in the same instance."""


def check(case: dict, events: list[dict], limits: dict) -> None:
    del limits  # Lifecycle compares the deadline passed to two separate hooks.
    app = None
    instance = None
    prior_instances = set()
    sequence = 0
    timestamp = 0
    deadline = None
    observations = []
    timer_delays = []
    for event in events:
        kind = event["event"]
        fields = [kind]
        if kind in {"prepare", "start", "media", "runtime_close", "wakeup", "timer", "stop"}:
            fields.append(event["app"])
        if kind in {"prepare", "request_close", "retire", "saved_context"}:
            fields.append(event["outcome"])
        if kind == "saved_context":
            require(event["borrowed"] is False, "a copied context exposed active instance storage")
        if kind == "start":
            instance = event["instance"]
            require(uint(instance) and instance > 0 and instance not in prior_instances,
                    "start reused or failed to establish an instance identity")
            require(event["state_nonzero_bytes"] == 0, "declared state was not cleared before start")
            prior_instances.add(instance)
            app = event["app"]
            sequence = 0
            timestamp = 0
        if kind in {"media", "runtime_close", "wakeup", "timer", "stop"}:
            require(event["app"] == app and event["instance"] == instance,
                    "callback belongs to a different instance")
        if kind in {"media", "runtime_close", "wakeup", "timer"}:
            require(uint(event["sequence"]) and event["sequence"] > sequence,
                    "accepted event sequence did not increase")
            event_time = event["timestamp_us"]
            require(uint(event_time), "invalid monotonic timestamp")
            if case.get("check_timestamps") and event_time < timestamp:
                raise TimestampRegression(
                    f"{kind} timestamp {event_time} preceded prior event {timestamp} in instance {instance}"
                )
            sequence, timestamp = event["sequence"], event_time
        if kind == "timer":
            require(uint(event["scheduled_us"]) and event["scheduled_us"] <= event["observed_us"]
                    and event["observed_us"] == timestamp,
                    "timer scheduled and observed fields disagree with the supplied time domain")
            timer_delays.append(event["observed_us"] - event["scheduled_us"])
        if kind == "stop":
            deadline = event["deadline_us"]
            if case.get("expired_at_stop"):
                require(uint(deadline) and deadline <= event["now_us"],
                        "clock failure did not pass an expired deadline to stop")
            else:
                require(uint(deadline) and deadline < 2**64 - 1 and deadline > event["now_us"],
                        "stop did not receive a finite future deadline in the nominal-clock fixture")
        if kind == "retire":
            if deadline is not None:
                require(event["deadline_us"] == deadline, "stop and retirement received different deadlines")
                if case.get("expired_at_retire"):
                    require(event["now_us"] >= deadline, "retirement was not observed after expiry")
            deadline = None
        if kind == "return":
            fields.extend(event[name] for name in ("action", "outcome", "active_app", "first_error"))
        observations.append(" ".join(fields))

    expected = case["observations"]
    for index, (actual, wanted) in enumerate(zip(observations, expected), start=1):
        require(actual == wanted, f"observation {index}: expected {wanted!r}, observed {actual!r}")
    require(len(observations) == len(expected),
            f"expected {len(expected)} observations, received {len(observations)}")
    require(timer_delays == case.get("timer_delays_us", []),
            f"observed timer delays {timer_delays}, expected {case.get('timer_delays_us', [])}")
