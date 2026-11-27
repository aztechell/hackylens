# HackyLens K210 v0.9.0 evidence report

Implementation revision `7d3e7dbfc94ded50e40af0d35dd226b14286fca5`.
Specification v0.1.0. Evaluated scopes Core, Lights, External Link, Shared Script.
Configuration K210 Time binding with host hooks, native runtime with simulated providers, Lights and External Link with peripheral hooks, the MicroPython capability bridge with a controlled scheduler, MicroPython runtime with a VM stub and controlled executor, and SEN0305 composition fixtures.
See the [manifest](manifest.json) and [machine-readable results](report.json).

## Requirement results

| ID | Scope | Status | Basis |
| --- | --- | --- | --- |
| ARC-01 | Core | NOT RUN | No independent result for this requirement. |
| ARC-02 | Core | NOT RUN | No independent result for this requirement. |
| ARC-03 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| ARC-04 | Core | NOT RUN | No independent result for this requirement. |
| CMP-01 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| CMP-02 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| CMP-03 | Core | PASS | Equivalent manifest inputs produced identical registries. Compiled lookup preserved the disabled app ID and the peer's identity. [JSONL 1](traces/equivalent-manifest-ordering.jsonl), [JSONL 2](traces/preserve-disabled-autostart-id.jsonl) |
| LIF-01 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| LIF-02 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| LIF-03 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| LIF-04 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| LIF-05 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| LIF-06 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| LIF-07 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| LIF-08 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| LIF-09 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| OWN-01 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| OWN-02 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| OWN-03 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| OWN-04 | Core | NOT RUN | No independent result for this requirement. |
| OWN-05 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| TIM-01 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| TIM-02 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| TIM-03 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| ERR-01 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| ERR-02 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| SCR-01 | Shared Script | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| SCR-02 | Shared Script | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| SCR-03 | Shared Script | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| SCR-04 | Shared Script | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| SCR-05 | Shared Script | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| REC-01 | Recovery | NOT APPLICABLE | Outside the declared evaluation scope. |
| REC-02 | Recovery | NOT APPLICABLE | Outside the declared evaluation scope. |
| S-TIME-01 | Core | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| S-INPUT-01 | Input | NOT APPLICABLE | Outside the declared evaluation scope. |
| S-INPUT-02 | Input | NOT APPLICABLE | Outside the declared evaluation scope. |
| S-LIGHT-01 | Lights | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| S-LIGHT-02 | Lights | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| S-DISP-01 | Display | NOT APPLICABLE | Outside the declared evaluation scope. |
| S-DISP-02 | Display | NOT APPLICABLE | Outside the declared evaluation scope. |
| S-RENDER-01 | Native Render | NOT APPLICABLE | Outside the declared evaluation scope. |
| S-RENDER-02 | Native Render | NOT APPLICABLE | Outside the declared evaluation scope. |
| S-LINK-01 | External Link | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| S-LINK-02 | External Link | NOT RUN | The host observations provide partial evidence. Full requirement evaluation is pending. |
| S-POWER-01 | Power | NOT APPLICABLE | Outside the declared evaluation scope. |
| S-ACCEL-01 | Accelerometer | NOT APPLICABLE | Outside the declared evaluation scope. |

## CT-02 host observations

| Check | Status | Trace / finding |
| --- | --- | --- |
| reject-invalid-identity | PASS | [JSONL](traces/reject-invalid-identity.jsonl) |
| reject-duplicate-identity | PASS | [JSONL](traces/reject-duplicate-identity.jsonl) |
| exclude-absent-required-service | PASS | [JSONL](traces/exclude-absent-required-service.jsonl) |
| reject-explicitly-required-app | PASS | [JSONL](traces/reject-explicitly-required-app.jsonl) |
| admit-with-optional-service-absent | PASS | [JSONL](traces/admit-with-optional-service-absent.jsonl) |
| equivalent-manifest-ordering | PASS | [JSONL](traces/equivalent-manifest-ordering.jsonl) |
| preserve-disabled-autostart-id | PASS | [JSONL](traces/preserve-disabled-autostart-id.jsonl) |

## CT-03 host observations

| Check | Status | Trace / finding |
| --- | --- | --- |
| switch-foreground | PASS | [JSONL](traces/switch-foreground.jsonl) |
| ordered-events-through-close | PASS | [JSONL](traces/ordered-events-through-close.jsonl) |
| retained-close-during-start | PASS | [JSONL](traces/retained-close-during-start.jsonl) |

## CT-04 host observations

| Check | Status | Trace / finding |
| --- | --- | --- |
| timer-skips-missed-periods | PASS | [JSONL](traces/timer-skips-missed-periods.jsonl) |
| timer-overrun-unwinds | PASS | [JSONL](traces/timer-overrun-unwinds.jsonl) |

## CT-05 host observations

| Check | Status | Trace / finding |
| --- | --- | --- |
| invalid-descriptor | PASS | [JSONL](traces/invalid-descriptor.jsonl) |
| partial-preparation | PASS | [JSONL](traces/partial-preparation.jsonl) |
| start-failure | PASS | [JSONL](traces/start-failure.jsonl) |
| event-and-retirement-failure | PASS | [JSONL](traces/event-and-retirement-failure.jsonl) |

## CT-06 host observations

| Check | Status | Trace / finding |
| --- | --- | --- |
| expired-stop-keeps-retirement | PASS | [JSONL](traces/expired-stop-keeps-retirement.jsonl) |
| deadline-clock-failure-keeps-retirement | PASS | [JSONL](traces/deadline-clock-failure-keeps-retirement.jsonl) |

## CT-07 host observations

| Check | Status | Trace / finding |
| --- | --- | --- |
| wakeup-after-switch | PASS | [JSONL](traces/wakeup-after-switch.jsonl) |
| instance-identity-exhaustion-blocks-reuse | PASS | [JSONL](traces/instance-identity-exhaustion-blocks-reuse.jsonl) |
| copied-context-cannot-borrow-from-next-instance | PASS | [JSONL](traces/copied-context-cannot-borrow-from-next-instance.jsonl) |

## CT-08 host observations

| Check | Status | Trace / finding |
| --- | --- | --- |
| connector-claims-and-validation-before-effects | PASS | [JSONL](traces/connector-claims-and-validation-before-effects.jsonl) |
| cancelled-transfer-releases-peripheral-borrow | PASS | [JSONL](traces/cancelled-transfer-releases-peripheral-borrow.jsonl) |

## CT-09 host observations

| Check | Status | Trace / finding |
| --- | --- | --- |
| target-before-deadline | PASS | [JSONL](traces/target-before-deadline.jsonl) |
| target-wins-three-way-race | PASS | [JSONL](traces/target-wins-three-way-race.jsonl) |
| cancel-wins-at-deadline | PASS | [JSONL](traces/cancel-wins-at-deadline.jsonl) |
| cancel-between-checks | PASS | [JSONL](traces/cancel-between-checks.jsonl) |
| earlier-deadline | PASS | [JSONL](traces/earlier-deadline.jsonl) |
| already-expired | PASS | [JSONL](traces/already-expired.jsonl) |
| reached-at-entry | PASS | [JSONL](traces/reached-at-entry.jsonl) |
| frozen-clock | PASS | [JSONL](traces/frozen-clock.jsonl) |
| unbounded-deadline-rejected | PASS | [JSONL](traces/unbounded-deadline-rejected.jsonl) |
| regressing-clock | PASS | [JSONL](traces/regressing-clock.jsonl) |
| finite-zero-interval | PASS | [JSONL](traces/finite-zero-interval.jsonl) |
| maximum-interval | PASS | [JSONL](traces/maximum-interval.jsonl) |
| excessive-interval | PASS | [JSONL](traces/excessive-interval.jsonl) |
| deadline-overflow | PASS | [JSONL](traces/deadline-overflow.jsonl) |

## CT-11 host observations

| Check | Status | Trace / finding |
| --- | --- | --- |
| lights-atomic-overlap | PASS | [JSONL](traces/lights-atomic-overlap.jsonl) |
| lights-validation-and-copied-authority | PASS | [JSONL](traces/lights-validation-and-copied-authority.jsonl) |
| lights-close-retry | PASS | [JSONL](traces/lights-close-retry.jsonl) |
| lights-retirement-quarantines-locally | PASS | [JSONL](traces/lights-retirement-quarantines-locally.jsonl) |

## CT-14 host observations

| Check | Status | Trace / finding |
| --- | --- | --- |
| held-transfer-completes-before-buffer-release | PASS | [JSONL](traces/held-transfer-completes-before-buffer-release.jsonl) |
| stale-link-operation-cannot-affect-reopened-connector | PASS | [JSONL](traces/stale-link-operation-cannot-affect-reopened-connector.jsonl) |
| expired-retirement-quiesces-borrow-and-quarantines-connector | PASS | [JSONL](traces/expired-retirement-quiesces-borrow-and-quarantines-connector.jsonl) |

## CT-17 host observations

| Check | Status | Trace / finding |
| --- | --- | --- |
| native-and-script-lights-normalize-units-and-results | PASS | [JSONL](traces/native-and-script-lights-normalize-units-and-results.jsonl) |
| native-and-script-share-exclusive-light-authority | PASS | [JSONL](traces/native-and-script-share-exclusive-light-authority.jsonl) |
| second-script-request-cannot-overwrite-in-flight-request | PASS | [JSONL](traces/second-script-request-cannot-overwrite-in-flight-request.jsonl) |

## CT-18 host observations

| Check | Status | Trace / finding |
| --- | --- | --- |
| script-expiry-acknowledged-before-slot-reuse | PASS | [JSONL](traces/script-expiry-acknowledged-before-slot-reuse.jsonl) |
| script-stop-waits-for-consumer-before-unwind | PASS | [JSONL](traces/script-stop-waits-for-consumer-before-unwind.jsonl) |
| terminal-script-failure-survives-rejected-run-before-executor-ack | PASS | [JSONL](traces/terminal-script-failure-survives-rejected-run-before-executor-ack.jsonl) |
| stop-acknowledgement-does-not-retire-live-worker | PASS | [JSONL](traces/stop-acknowledgement-does-not-retire-live-worker.jsonl) |
| finite-script-expiry-retires-after-worker-returns | PASS | [JSONL](traces/finite-script-expiry-retires-after-worker-returns.jsonl) |

## CT-20 host observations

| Check | Status | Trace / finding |
| --- | --- | --- |
| repeated-script-drain-at-observed-capacity | PASS | [JSONL](traces/repeated-script-drain-at-observed-capacity.jsonl) |
