# HackyLens Technical Specifications

## Authority during simplification

[SIMPLIFICATION_MASTERPLAN.md](../SIMPLIFICATION_MASTERPLAN.md) controls the
scope, order, invariants, and exit gates of current work. It takes precedence
where older architecture or governance rules conflict with simplification.
[Architecture](../ARCHITECTURE.md) describes the implementation;
[Architecture Vision](../ARCHITECTURE_VISION.md) and
[Roadmap](../ROADMAP.md) describe goals, not additional implementation gates.

The specifications below describe existing technical interfaces and behavioral
contracts. Their MUST/MUST NOT requirements still apply to the current
implementation until the relevant interface is deliberately migrated. They do
not require preserving a generic broker, inventory, version negotiation, or
owner/lease representation after the corresponding simplification package.
Changing a specification alone does not migrate its consumers or qualify a new
implementation. MicroPython API v1/HMPY compatibility and the plan's resource
safety invariants remain required.

## Native app contracts

- [App Runtime](APP_RUNTIME.md)
- [Native App Manifest](APP_MANIFEST.md)
- [Feature App SDK](APP_SDK.md)

## Capability contracts

These describe the current broker-backed interfaces. S8 may replace their
internal machinery service by service while preserving required app-facing
semantics and testing the changed bindings.

- [Capability API](CAPABILITY_API.md)
- [Time Capability](capabilities/TIME.md)
- [Input Capability](capabilities/INPUT.md)
- [Display Capability](capabilities/DISPLAY.md)
- [External Link Capability](capabilities/EXTERNAL_LINK.md)
- [Lights Capability](capabilities/LIGHTS.md)

Camera and Storage do not require new generic Capability wrappers merely to
appear in this index. A typed service boundary is sufficient when it serves the
actual consumer and preserves architecture and resource ownership.

## Other technical references

- [Board Port Contract](BOARD_PORT.md)
- [Glossary](GLOSSARY.md)
- [Versioning Policy](VERSIONING.md)
- [HMPY Protocol](../HMPY_PROTOCOL.md)
- [MicroPython API](../MICROPYTHON_API.md)
- [External Link Protocol](../EXTERNAL_LINK_PROTOCOL.md)
- [Current App Lifecycle](../APP_LIFECYCLE.md)
- [AI Model Package](../AI_MODELS.md)

## Change process

Update affected public headers, build inputs, consumers, behavioral tests, and
current API documentation together. Explain observable changes and compatibility
or migration impact in ordinary prose. Use the existing version conventions
when an interface changes; wire/API constants are not changed by documentation
cleanup. Validate the affected behavior and hardware paths as required by the
active plan.

A separate ADR, mandatory front-matter schema, machine-readable migration
route, phase receipt, or new checker is not required. Existing contract IDs,
versions, stability fields, and logical owner labels remain useful reference
metadata; they do not create a new governance gate. Historical decisions are
available in [ADRs](../adr/README.md).
