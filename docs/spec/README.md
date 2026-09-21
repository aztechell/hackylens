# Technical references

[Architecture](../ARCHITECTURE.md) describes the current implementation.
These references describe public interfaces, resource lifetimes and physical
board constraints. Headers and behavioral tests should change with their docs.

## Native apps

- [App Runtime](APP_RUNTIME.md): start, event, render, stop and failure unwind
- [App Manifest](APP_MANIFEST.md): build-time app composition
- [App SDK](APP_SDK.md): public C/C++ entry surface

## Typed hardware services

- [Shared rules](CAPABILITY_API.md)
- [Time](capabilities/TIME.md)
- [Input](capabilities/INPUT.md)
- [Lights](capabilities/LIGHTS.md)
- [Display](capabilities/DISPLAY.md)
- [External Link](capabilities/EXTERNAL_LINK.md)

Camera and Storage use existing typed firmware services. They do not need
additional public wrappers without a concrete consumer.

## Other references

- [Board Port](BOARD_PORT.md)
- [Glossary](GLOSSARY.md)
- [Versioning](VERSIONING.md)
- [HMPY Protocol](../HMPY_PROTOCOL.md)
- [MicroPython API](../MICROPYTHON_API.md)
- [External Link Protocol](../EXTERNAL_LINK_PROTOCOL.md)
- [AI Model Package](../AI_MODELS.md)

## Change process

Update affected headers, build inputs, consumers, tests and API documentation
together. Explain observable changes and compatibility or migration impact in
the change description. Preserve MicroPython API v1, HMPY and persisted data
compatibility. Validate the affected behavior and hardware paths; documentation
cleanup alone changes no wire or API constants.
