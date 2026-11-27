"""Build deliberate Time contract violations and verify that the oracle rejects them."""

from __future__ import annotations

from pathlib import Path

from conformance.adapters.k210.collect import TIME_SOURCE, collect
from conformance.mutations import experiment


MUTATIONS = {
    "expiry-before-target-and-cancel": (
        "        if(now >= wake_target.at_us)\n"
        "            return HK_OK;\n"
        "        if(cancelled(cancel))\n"
        "            return HK_ERR_CANCELLED;\n"
        "        if(now >= operation_deadline.at_us)\n"
        "            return HK_ERR_DEADLINE_EXCEEDED;",
        "        if(now >= operation_deadline.at_us)\n"
        "            return HK_ERR_DEADLINE_EXCEEDED;\n"
        "        if(now >= wake_target.at_us)\n"
        "            return HK_OK;\n"
        "        if(cancelled(cancel))\n"
        "            return HK_ERR_CANCELLED;",
        {"target-wins-three-way-race", "cancel-wins-at-deadline"},
    ),
    "wait-past-deadline": (
        "uint64_t stop_at = wake_target.at_us < operation_deadline.at_us ?\n"
        "                           wake_target.at_us : operation_deadline.at_us;",
        "uint64_t stop_at = wake_target.at_us;",
        {"earlier-deadline"},
    ),
    "success-with-frozen-clock": (
        "if(now <= previous)\n"
        "        {\n"
        "            handle->fault(handle->context);\n"
        "            return HK_ERR_INTERNAL;\n"
        "        }",
        "if(now <= previous)\n"
        "            return HK_OK;",
        {"frozen-clock"},
    ),
}


def main() -> int:
    return experiment.main(__doc__, Path(__file__), TIME_SOURCE,
                           lambda bundle, source: collect(bundle, time_only=True,
                                                          source_overrides={TIME_SOURCE: source}), MUTATIONS)


if __name__ == "__main__":
    raise SystemExit(main())
