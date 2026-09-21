# Interface versions and compatibility

Firmware, native SDK, wire protocols and stored formats have independent version
lines. Changing one does not implicitly change another. Current interfaces are
experimental; a version number alone does not imply a stability promise.

| Interface | Version | Source |
|---|---|---|
| Firmware | 0.4.0 | `VERSION` |
| App Runtime / SDK | 0.2.0 | SDK headers and [App Runtime](APP_RUNTIME.md) |
| Native App Manifest | 0.1.0; schema 1 | [App Manifest](APP_MANIFEST.md) |
| Typed hardware API | 0.1.0 | Public service headers |
| Board Port | 0.1.0 | [Board Port](BOARD_PORT.md) |
| HMPY | 1.1.0; wire major 1 | [HMPY](../HMPY_PROTOCOL.md) and codec constants |
| MicroPython API | 1.0.0 | [MicroPython API](../MICROPYTHON_API.md) |
| External Link | 1.0.0; wire major 1 | [External Link](../EXTERNAL_LINK_PROTOCOL.md) |
| AI Model Package | 1.0.0; schema 1 | [AI Models](../AI_MODELS.md) |

Native runtime/SDK 0.2.x uses start, event, optional render and stop. The older
callback interface has no compatibility adapter. Project Format and dynamic
native loading have no published interface.

Use `MAJOR.MINOR.PATCH`. For an experimental interface, intentional breaking
changes and compatible additions increment MINOR; compatible corrections use
PATCH. PATCH must not intentionally break consumers. For a stable interface,
breaking changes increment MAJOR and require a documented migration path.

Wire and stored representations have their own encoded major/schema fields.
Incompatible representation changes require an appropriate discriminator and
migration handling. MicroPython API v1, HMPY, stable app identities and persisted
settings retain their compatibility requirements despite experimental status.

Explain observable changes and migration steps in ordinary prose, and update
headers, consumers, tests and relevant documentation together. Documentation
clarifications do not automatically change API constants or firmware versions.
