# Conformance plan v0.1

Proposed evidence protocol, not an executable suite or passing report.
Normative requirements: [Architecture](README.md) and [Services](SERVICES.md).

A claim identifies exact implementation and specification revisions, profiles, features, bindings, configuration, limits, test revision, environment and results. Each applicable requirement needs PASS, FAIL, NOT RUN or NOT APPLICABLE with reason. Missing evidence is not PASS. An untested mandatory requirement prevents an unqualified claim. Host tests establish semantics; structural review establishes boundaries and bounded storage; target tests establish physical behavior. A fake backend is not an independent runtime.

| Case | Requirements | Required observation |
| --- | --- | --- |
| CT-01 | ARC-01..04 | Review declaration, dependency graph and capacities; exhaust bounded pools; no competing authority or unbounded growth. |
| CT-02 | CMP-01..03 | Duplicate IDs, missing required service, optional absence, reordered inputs and disabled app; correct rejection and stable registry identity. |
| CT-03 | LIF-01..03 | Launch A, switch B, reentrant close during start, ordered events; one foreground, clear state, no dispatch after close. |
| CT-04 | LIF-04, TIM-01 | Missed periods, budget excess, clock regression and overflow; no catch-up, common unwind. |
| CT-05 | LIF-05..06, ERR-02 | Inject validation, preparation, start, event, render, close and stop failures; exact callback counts/order and first error. |
| CT-06 | LIF-07..08, OWN-04..05 | Expired deadline, clock failure and safe-off failure; same deadline throughout, all cleanup attempted, local quarantine before reuse. |
| CT-07 | LIF-09, OWN-02 | Stale context/session/token after switch/reopen; reject without mutating new state; verify non-aliasing at identity exhaustion. |
| CT-08 | OWN-01..05, ERR-01 | Conflicting claims, copied handle, invalid buffer, cancellation and failed retirement; no unauthorized effects or premature reuse. |
| CT-09 | TIM-01..03, S-TIME-01 | Target/cancel/expiry race, excessive interval, overflow, freeze/regression; unchanged deadline and declared check bounds. |
| CT-10 | S-INPUT-01..02 | Transitions, holds, two readers and overflow; exact masks/lost count, independent resync. |
| CT-11 | S-LIGHT-01..02 | Overlap/disjoint claims, invalid values, expired close and failed safe-off; atomic claims and local quarantine. |
| CT-12 | S-DISP-01..02 | Clipping, zero area, overflow, stride, invalid state, stale borrow and failed present; correct pixels/trace and ownership. |
| CT-13 | S-RENDER-01..02 | Initial render, busy display, repeated and in-render invalidation, null render, callback/present failure; runtime transaction ownership. |
| CT-14 | S-LINK-01..02 | Mode conflict, invalid config, active switch, partial transfer, cancel, stale token and retirement failure; no stale buffer access. |
| CT-15 | S-POWER-01 | Unsupported action, pending preparation, expiry, hardware failure; no premature action, mount restoration where applicable. |
| CT-16 | S-ACCEL-01 | Absent, settling, signed sample, I/O error, unsupported motion; unchanged output on failure. |
| CT-17 | SCR-01..03 | Same shared service from native/script, normalized result and conflict parity; inspect common provider; saturate and stress handoff. |
| CT-18 | SCR-04..05, OWN-03..05 | Exit, exception, STOP, timeout, launcher close and delayed handoff; no reuse before quiescence; documented failure path. |
| CT-19 | REC-01..02 | Physical non-cooperation injection; independent recovery event, cause, suppressed autostart and committed data preservation where supported. |
| CT-20 | ARC-03, LIF-08, SCR-02..05 | Repeated launch/stop at capacity; report high-water and residual ownership; no monotonic growth of retained run-owned resources. |

Adapters translate calls and results but MUST NOT implement the behavior under test. Extract binding-neutral fixtures and trace oracles from existing harnesses; run against another implementation. Report timing endpoints and configured limits separately from sampled latency; percentiles are not WCET. Physical connector claims require an external peer. Recovery needs observed independent recovery, not only post-reset status. Fault injection is isolated from production.

Minimum report fields: specification, implementation revision, configuration, profiles, extensions/bindings, declared limits, suite revision, case results with evidence, unresolved requirement IDs. No independent suite execution is asserted by this document.
