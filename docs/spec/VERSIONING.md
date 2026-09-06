---
contract-id: hackylens.versioning-policy
owner: platform-architecture
version: 0.5.0
stability: experimental
---

# HackyLens Contract Versioning Policy

This policy defines independent semantic-version lines and the lifecycle of
HackyLens public contracts.

During simplification, [the active plan](../SIMPLIFICATION_MASTERPLAN.md)
controls scope and takes precedence over the former governance process.
Version and compatibility conventions below continue to describe interface
changes. They do not require a new ADR, metadata validator, or phase evidence
schema. This documentation update does not change any firmware, wire, schema,
or API version.

## Version and stability are independent

Semantic version identifies a compatibility line. Stability identifies the
promise made for that line.

> A `1.0.0` contract has a defined compatibility line. It is not stable unless
> `stability` is explicitly set to `stable`.

Phase 0 marks every existing technical contract `experimental`. It does not
promote any firmware, protocol, API, or format to stable.

## Independent version axes

| Axis | Current version | Stability/maturity | Canonical source |
| --- | --- | --- | --- |
| Firmware | `0.4.0` | Technology preview; Phase 2 physically accepted on SEN0305; Maix Cube compile-conformance-only; general hardware portability not claimed | `VERSION` |
| HMPY | `1.1.0` (wire major `1`) | Experimental | HMPY contract and codec constants |
| Board Port Contract | `0.1.0` | Experimental | `BOARD_PORT.md` and board descriptors |
| Capability API | `0.1.0` | Experimental | `CAPABILITY_API.md` |
| App Runtime | `0.2.0` | Experimental | `APP_RUNTIME.md` |
| Native App Manifest | `0.1.0` | Experimental; build-time-only | `APP_MANIFEST.md` |
| Feature App SDK | `0.2.0` | Experimental | `APP_SDK.md` |
| Project Format | Unpublished | No public contract | Future Project Format spec |

The first published experimental Capability API, App Runtime, Native App
Manifest, Feature App SDK, or Project Format contract starts at `0.1.0`. App
Runtime and Feature App SDK `0.2.0` are the first experimental breaking MINOR
on those axes: the public lifecycle is `start`/`event`/`render`/`stop`, with
timer-as-event and no probe/prepare/tick/app-cleanup compatibility wrapper.
The authority for that change is `docs/SIMPLIFICATION_MASTERPLAN.md` package
S6 rather than a new ADR. Native App Manifest remains `0.1.0`. A version
change on one axis MUST NOT implicitly change any other axis. Native App
Manifest contract `0.1.0` is independently encoded and is not the future Project
Format schema.

Additional existing technical contracts retain their own compatibility lines:

| Contract | Version | Stability |
| --- | --- | --- |
| MicroPython API | `1.0.0` | Experimental |
| External Link Protocol | `1.0.0` (wire major `1`) | Experimental |
| AI Model Package | `1.0.0` (schema major `1`) | Experimental |
| Legacy App Lifecycle | `0.2.0` | Experimental |

`hackylens.legacy-app-lifecycle` version `0.2.0` describes the lifecycle shipped
by firmware `0.2.0`. This historical match does not establish permanent version
coupling with firmware or with future App SDK/runtime contracts.

## Semantic version rules

Versions use `MAJOR.MINOR.PATCH`.

- For an experimental contract at any major version, an intentional breaking
  change increments MINOR. PATCH MUST NOT contain an intentional breaking
  change.
- For an experimental contract, a backward-compatible addition increments
  MINOR and a compatible correction increments PATCH.
- For a stable contract, an incompatible change increments MAJOR, a backward-
  compatible addition increments MINOR, and a compatible correction increments
  PATCH.
- A firmware release MAY contain unchanged contract versions.
- Documentation-only clarification MAY increment PATCH when it changes no
  observable requirement. A typo or link correction need not change the
  contract version.
- Wire and storage formats increment their encoded major/schema field for
  incompatible representation changes. That encoded discriminator is separate
  from the contract's semantic version; the semantic version follows the
  experimental or stable lifecycle rule above. Compatible additions use the
  contract's MINOR version and explicit feature discovery where the format
  provides it.

## Lifecycle

### Experimental

An experimental contract is usable and testable, but its interface may evolve
through intentional versioned changes. Explain observable behavior and migration
impact, and update affected consumers and behavioral tests. Experimental status
does not waive the active plan's explicit compatibility requirements for
MicroPython API v1, HMPY, stable app identities, or persistence.

### Stable

A stable contract follows the compatibility rules of its semantic-version line.
Breaking it requires a new major version and a documented migration path.
Marking a contract stable requires evidence for its compatibility promise;
current simplification does not promote any interface to stable.

### Deprecated

Deprecated identifies a supported interface scheduled for replacement. Document
the affected interface, replacement or migration steps, and support/removal
versions in ordinary prose. Keep compatibility tests while the interface is
supported and respect any published stable compatibility promise. Stable
incompatible removal follows the major-version rule above.

The former mandatory YAML migration routes and minimum-removal formula are
historical governance, not current requirements. No new migration schema or
validator is needed for simplification.

## Changing a contract

A change to a public interface should:

1. identify the affected interface, previous/new version, and stability;
2. explain compatibility and migration impact;
3. update public headers, affected consumers, documentation, and behavioral tests;
4. explain the decision in the change description; a separate ADR is optional;
5. record hardware, size, or protocol evidence when relevant.
