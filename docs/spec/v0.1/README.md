# HackyLens Architecture Specification v0.1

Status: experimental candidate 0.1.0; not a recognized standard or a firmware version.

## Scope and profiles

This specification defines observable behavior for resource-constrained embedded applications. MUST and MUST NOT are normative; SHOULD permits a justified exception; MAY is optional. Requirement IDs are stable. Semantic compatibility does not imply binary compatibility or identical timing.

Core requires the architecture, composition, lifecycle, ownership, time and error contracts below. Shared Script adds SCR and at least one common typed service other than Time. Recovery adds REC to Shared Script. Service extensions are individually declared. Core may be headless. A native-only device cannot claim Shared Script.

## Architecture

- ARC-01: Implementations MUST declare specification revision, profiles, services/features, binding versions, capacities, timing and recovery policies, execution model and tested configuration.
- ARC-02: Portable apps and language adapters MUST use typed public services. Board wiring, drivers, peripheral identities and platform synchronization stay private. Providers MUST NOT depend on app policy or language adapters.
- ARC-03: Runtime bookkeeping MUST use bounded preallocated storage. State, buffers, sessions, outstanding operations and script memory MUST have declared finite limits; exhaustion returns an explicit outcome. Separately budgeted script heaps and provider workspaces are permitted.
- ARC-04: Each resource MUST have one authoritative owner and serialization rule across clients. CPU ISA, core count, RTOS, IPC transport and placement are implementation choices.

## Composition

- CMP-01: Native apps MUST be statically composed into an immutable registry before execution. Descriptors declare stable identity, entry, required/optional services and finite state/scheduling limits. Identity MUST NOT depend on enumeration or menu order.
- CMP-02: Invalid and duplicate identities MUST be rejected. Missing required services exclude the app; explicitly required apps fail the build. Optional absence has an explicit result or fallback. Requirements do not acquire resources.
- CMP-03: Equivalent inputs MUST produce equivalent semantic composition. Persisted identities MUST NOT be reassigned when an app is disabled. TOML, C symbols, generator and build system are implementation choices.

## Lifecycle

One foreground native instance is the unit of execution. Persistent services and script workers are separate scopes.

    INACTIVE -> STARTING -> RUNNING -> STOPPING -> INACTIVE
                     |                  ^
                     +------------------+
    STOPPING includes retirement and invalidation before reuse.
    FAULTED denotes a non-reusable slot when identity cannot safely advance.

- LIF-01: Runtime MUST validate descriptor/state, clear declared state, establish a new identity and prepare scope before start. At most one foreground native instance is admitted. Switch retires the previous instance first.
- LIF-02: Callbacks are start, event, optional render and stop. They MUST execute synchronously and serially, return terminal results and respect finite work budgets. No untracked tasks or unbounded retries. Ordinary event/render dispatch occurs only in RUNNING; no new ordinary callback begins after teardown acceptance.
- LIF-03: Accepted events MUST carry increasing nonzero sequence and monotonic timestamp within an instance. Storage is bounded. Timer and runtime-close are Core events. Back, if supported, is ordinary input. App close requests are legal only in RUNNING callbacks and take effect after successful callback completion. External close during start is retained and unwound before ordinary dispatch.
- LIF-04: Timer events distinguish scheduled and observed time. Cadence and callback budget are independent positive limits. Next due time is observed callback start plus interval; no catch-up burst. Clock failure, overflow or observed budget excess terminates through common unwind.
- LIF-05: A running instance MUST receive exactly one runtime-close event before stop, including terminal callback failure. Its error cannot erase the original failure or skip cleanup. Instances that never reached the running event surface do not receive it.
- LIF-06: Before scope preparation, no stop or live cleanup; after partial preparation but before start, retire partial scope without stop; once start was entered, stop exactly once even on start failure, then retire scope. Stop MUST be idempotent.
- LIF-07: Teardown MUST create one finite absolute monotonic deadline from a runtime-owned positive budget. Clock failure uses an already-expired deadline and records the error. Stop and every retirement receive that same deadline even after expiry. No renewal or new scoped acquisition during teardown.
- LIF-08: Order MUST be stop if entered, attempt all scoped retirement, quarantine unsafe resources, invalidate handles/tokens, invalidate context, clear/release state. Stop failure or expiry cannot skip stages. Provider-local failure does not by itself poison unrelated providers. Identity exhaustion prevents unsafe reuse.
- LIF-09: Deferred work MUST identify its intended instance and operation; stale completion is rejected before dispatch or mutation. Identity MUST NOT alias an outstanding stale reference after reuse.

## Ownership

- OWN-01: Each service MUST specify owner, acquisition, conflict granularity, context, lifetime and failure rules. Exclusive or overlapping claims fail without partial acquisition. Immutable read-only bindings need no per-call lease.
- OWN-02: Copying a handle MUST NOT transfer authority. Stable-address sessions are one possible implementation. Context and render surface are callback-scoped borrows. Observable stale/wrong-context use fails before effects.
- OWN-03: Buffer borrows MUST specify capacity, access and lifetime, and layout where relevant. Storage MUST NOT be reused while an operation can access it. Cancellation alone is not quiescence. Deferred work may retain separately owned staging storage, not invalidated app borrows.
- OWN-04: Ordinary close may preserve a retryable claim. Forced retirement MUST invalidate logical ownership even on failure. Unsafe resources and reachable storage MUST be quarantined or otherwise prevented from reuse until a documented safe boundary.
- OWN-05: Persistent, native-instance and script-run scopes MUST remain distinct. Runtime cleanup authority cannot depend on callback-supplied copies of bookkeeping.

## Time and errors

- TIM-01: One boot-local monotonic Time domain MUST serve runtime and clients. Invalid clock observations and arithmetic overflow fail explicitly. Finite deadlines and immediate expiry are distinct from unbounded sentinels.
- TIM-02: Absolute deadlines MUST survive retries, context transfers and cleanup unchanged. Operations document cancellation precedence, check points and maximum work between checks. Completed effects need not roll back on later cancellation.
- TIM-03: Long operations MUST have bounded steps or documented finite blocking bounds. Pending operations have terminal outcomes or a recovery path. Expiry prevents new hardware work except specified bounded safety quiescence; logical retirement remains mandatory.
- ERR-01: Results distinguish success, pending where supported, invalid arguments, absent service, unsupported feature, conflict, stale/wrong authority, invalid state, deadline, cancellation, overflow/limit and I/O/internal failure where relevant. Versioned inputs distinguish incompatibility. Validation failure has no hardware effects.
- ERR-02: First terminal failure is retained; subsequent cleanup errors cannot erase it or skip later cleanup. Each operation documents output validity on non-success.

## Shared Script

- SCR-01: Interpreted clients MUST use typed adapters to the same authoritative production services as native clients for shared operations. Units, errors and conversions are documented. Equal semantics do not imply equal latency or rendering paths. Manual migration to native is permitted; automatic translation is not required.
- SCR-02: Engine MUST declare finite memory and operation limits, distinct run identity and state/exit model; reject conflicting work at capacity. Portable adapters expose no raw driver pointers or unrestricted hardware access.
- SCR-03: Cross-context calls MUST use bounded synchronized handoff with explicit request ownership, cancellation and result publication. No reuse of in-flight storage. Single-context direct serialized calls are permitted.
- SCR-04: Engine MUST support cooperative stop and finite execution-limit mode. Local until-stopped mode may be documented. Stop acknowledgment is not terminal handoff; stopping runs admit no new work. Resources cannot be reused while worker access remains possible.
- SCR-05: Completion, exception, stop and expiry converge on terminal cleanup. Non-cooperation enters a documented non-reusable fault state or recovery. Closing a launcher cannot prematurely retire worker resources.

## Recovery

- REC-01: Non-cooperating script worker after stop/expiry MUST trigger a documented finite independent escalation policy with trigger, grace period, recovery mechanism, reset scope and observable evidence.
- REC-02: Recovery boot MUST suppress offending autostart and expose cause. Persistent programs/settings, if supported, MUST NOT be intentionally erased. Stronger crash-consistency claims require separate contracts and evidence.

## Boundaries and evolution

No mandatory CPU, core count, interpreter, heap size, framebuffer, accelerator, filesystem, IDE, wire protocol or dynamic native loader. No promise of native sandboxing, forced preemption, hard real-time/WCET, safety certification or electrical fail-safe. Actuator safety requires separate system-level policy.

Specification revisions are independent of firmware, SDK, service ABI and protocol versions. Normative changes require a revision and compatibility note; requirement IDs cannot be reused for unrelated behavior. Existing implementation contracts remain unchanged.
