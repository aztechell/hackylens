# Architecture Decision Records

This directory preserves the context of earlier HackyLens architecture
choices. Numbered ADRs are historical records, including those whose original
metadata says `accepted`. That status records acceptance at the time; it does
not override the current
[Simplification masterplan](../SIMPLIFICATION_MASTERPLAN.md).

The earlier requirements to create an ADR for architectural changes, maintain
supersedes/superseded-by chains, or supply machine-readable governance evidence
are retired for current work. In particular, ADR-0001's governance process and
ADR-0003's mandatory migration metadata do not add gates to simplification.
Other ADRs explain the existing implementation, not a requirement to retain
mechanisms scheduled for replacement by S1–S9.

Do not rewrite an old decision to suggest it originally chose the new design.
Record current behavior, compatibility impact, and rationale in the affected
technical documentation and change description. A new ADR is optional when a
specific decision benefits from a separate historical record; neither the
numbering template nor its section list is a prerequisite for implementation.

[Technical specifications](../spec/README.md) describe current interfaces.
Documented behavior and resource safety requirements remain in force until an
intentional, tested migration; classifying ADRs as history does not waive them.
