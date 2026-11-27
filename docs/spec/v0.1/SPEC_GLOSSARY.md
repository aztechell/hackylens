# Architecture Specification glossary

These terms describe the [portable specification](README.md). The
[firmware glossary](../GLOSSARY.md) covers concrete K210 API and build terms.

| Term | Meaning in the specification |
| --- | --- |
| Service | Typed operations and their observable rules, including results, timing, authority and lifetime. |
| Provider | Component that implements a service contract and mediates access to its resources. A provider can delegate device operations to private platform code. |
| Binding | Selection of an implementation for a typed service, with a declared interface version and configuration. A language binding presents that interface to a client through an adapter. |
| Adapter | Translation boundary for a service or a test interface. It maps values, units and results while preserving the original authority, lifetime and deadline. |
| Resource | A service-mediated entity with a capacity, authority or lifetime rule. Examples include a connector, output channel, history cursor, buffer or operation slot. |
| Authoritative owner | The role or component that decides which access is valid and serializes conflicting operations for a resource. |
| Claim | Service-granted client authority with defined conflict granularity and lifetime. A provider remains responsible for arbitration while a client holds a claim. |
| Handle | Client-side reference to a context, claim, operation or borrow. The receiving boundary validates identity and caller authority before use. |
| Scope | Runtime-managed lifetime and authority domain for persistent services, one native instance or one script run. Scope retirement determines which resources are cleaned up together. |
| Instance | One admitted execution of a native app descriptor with state, scope and identity. A later launch of the same descriptor is a different instance. |
| Script run | One interpreter execution identified independently of its launcher and any foreground instance. |
| Operation | One identified request from acceptance through terminal outcome and release. An operation can finish synchronously or progress through pending steps. |
| Borrow | Time-limited access to existing storage with stated access mode, capacity and layout where relevant. The owner retains the storage lifetime. |
| Quiescence | Condition in which an old operation can no longer access a resource or its reachable storage, including from deferred work or hardware. |
| Cancellation | Request to stop an operation. Its effect and precedence are part of the operation contract. Quiescence is observed separately where access can continue. |
| Retirement | Forced scope or resource cleanup that revokes logical authority and achieves quiescence or blocks unsafe reuse. |
| Invalidation | End of the acceptance of a previously issued identity or handle. Distinct from quiescence when an old operation retains access. |
| Quarantine | Prevention of reuse of an unsafe resource or its reachable storage until a documented safe boundary. |
| Deadline | Absolute bound in the boot-local monotonic time domain. The same operation propagates it unchanged through retries, handoffs and cleanup. |
| Recovery | Documented independent escalation for a non-cooperating worker, with an observable cause and stated reset scope. |
| Profile | Named set of applicable architecture requirements. Feature and extension declarations determine additional service obligations. |

Qualification applies to a declared implementation, configuration and
evidence set. The [rationale](SPEC_RATIONALE.md) ties these terms to the
observations required for each requirement.
