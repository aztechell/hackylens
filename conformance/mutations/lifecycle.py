"""Build deliberate lifecycle contract violations and verify their detection."""

from __future__ import annotations

from pathlib import Path

from conformance.adapters.k210.collect import RUNTIME_SOURCE, collect
from conformance.mutations import experiment


MUTATIONS = {
    "skip-stop-after-start": (
        "if(runtime->start_entered && !runtime->stop_called)",
        "if(0U && runtime->start_entered && !runtime->stop_called)",
        {"start-failure", "switch-foreground"},
    ),
    "renew-teardown-deadline": (
        "    result = runtime->ops.cleanup(runtime->ops.user, runtime->teardown_deadline);",
        "    hk_deadline_t renewed = runtime->teardown_deadline;\n"
        "    (void)runtime->ops.deadline_after_us(runtime->ops.user, runtime->teardown_budget_us, &renewed);\n"
        "    result = runtime->ops.cleanup(runtime->ops.user, renewed);",
        {"expired-stop-keeps-retirement"},
    ),
    "accept-previous-instance-wakeup": (
        "runtime->state != HK_APP_RUNTIME_RUNNING || !runtime->context_valid ||\n"
        "       token.context_generation != runtime->context_generation ||\n"
        "       token.epoch != runtime->active_epoch)",
        "runtime->state != HK_APP_RUNTIME_RUNNING || !runtime->context_valid)",
        {"wakeup-after-switch"},
    ),
}


def main() -> int:
    return experiment.main(__doc__, Path(__file__), RUNTIME_SOURCE,
                           lambda bundle, source: collect(bundle, source_overrides={RUNTIME_SOURCE: source}), MUTATIONS)


if __name__ == "__main__":
    raise SystemExit(main())
