# Architecture viewpoints

These views describe the candidate architecture across implementations. The
[K210 mapping](../../implementation/K210_SPEC_MAPPING.md) relates its concrete
choices to these contracts.

## Execution and lifecycle

The audience includes runtime authors and app authors. The concerns are launch
eligibility, foreground admission, ordered dispatch, time bounds and unwind.
The state machine tracks one foreground slot. Persistent services and script
runs have independently managed scopes.

```mermaid
stateDiagram-v2
    [*] --> Inactive
    Inactive --> Starting: validate, clear state, establish identity and scope
    Starting --> Running: successful start
    Starting --> Stopping: startup failure or retained close
    Running --> Running: serial event or optional render
    Running --> Stopping: close, switch or terminal failure
    Stopping --> Inactive: retire, invalidate and release
    Stopping --> Faulted: identity cannot advance safely
```

At the transition out of the running event surface, the runtime delivers a
single runtime-close event. If start was entered, stop runs once. Retirement
uses the same deadline throughout and preserves the first terminal failure.
Scheduled and observed callback times use the boot-local Time domain.
The relevant contracts are [composition](README.md#composition),
[lifecycle](README.md#lifecycle) and [time and errors](README.md#time-and-errors).

## Resource ownership and lifetime

The audience includes service authors and conformance reviewers. The concerns
are exclusive effects, bounded capacity, physical quiescence and old authority
after a scope closes. The sequence shows a forced retirement with outstanding
access. The owner arbitrates other requests using the service's documented
conflict granularity.

```mermaid
sequenceDiagram
    participant C as Client scope
    participant O as Authoritative provider
    participant W as Operation worker or device
    C->>O: Claim resource
    O-->>C: Scoped handle
    C->>O: Accept operation with borrow and deadline
    O->>W: Bounded access
    C->>O: Forced retirement
    O->>W: Request quiescence within the deadline
    alt Quiescent
        W-->>O: Access has ceased
        O->>O: Invalidate authority and release storage
    else Unsafe to reuse
        O->>O: Quarantine resource and reachable storage
        O->>O: Invalidate authority
    end
    C->>O: Use old handle or deliver old completion
    O-->>C: Stale outcome before effects
```

Scopes can retire independently. The runtime uses its own records to retire
scope resources. Authority, access and storage lifetime are checked under
[ownership](README.md#ownership), [lifecycle](README.md#lifecycle) and each
typed [service contract](SERVICES.md).

## Portability and binding

The audience includes platform integrators and app authors. The concerns are
service substitution, stable native and interpreted behavior, and the boundary
of implementation-specific knowledge. Arrows show the permitted direction of
typed calls and provider dependencies for a shared operation.

```mermaid
flowchart TD
    Native[Native app] --> Port[Typed service contract and binding]
    Script[Interpreted client] --> Adapter[Typed language adapter]
    Adapter --> Port
    Port --> Provider[Authoritative production provider]
    Provider --> Private[Private platform services and drivers]
    Private --> Device[Board hardware]
```

The binding fixes the declared provider selection. A language adapter
preserves units, outcome categories, scope and deadlines. A cross-context
implementation adds a bounded, synchronized handoff with explicit request
ownership. Shared Script routes corresponding native and interpreted calls to
the same authoritative provider. See [architecture](README.md#architecture),
[Shared Script](README.md#shared-script) and the [glossary](SPEC_GLOSSARY.md).

## Correspondence across views

The instance established in the execution view has the native scope in the
ownership view. Callback execution determines the lifetime of context and
render borrows. Work surviving a callback has its own operation identity and
must validate the intended instance before effects. Every path in the binding
view reaches the authority in the ownership view. Runtime, provider and
adapter use the same time domain for the deadline shown in both behavioral
views.
