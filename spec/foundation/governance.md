---
type: "Wtop Specification"
title: "Scope, authority and source ownership"
description: "Keep product requirements, implementation observations, outside sources and release claims separate."
resource: "urn:wtop:spec:wt-001"
tags: ["wtop", "foundation"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-001", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": [], "requirements": ["WT-REQ-001-01", "WT-REQ-001-02", "WT-REQ-001-03"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}, {"id": "ntop", "resource": "../references/sources.json#ntop"}, {"id": "aide-okf", "resource": "../references/sources.json#aide-okf"}, {"id": "disked-okf", "resource": "../references/sources.json#disked-okf"}]
---

# Scope, authority and source ownership

The intended product is a native, compact, process-oriented monitor with the dense
htop/NTop presentation the user prefers. Optional charts supplement the process
table. One source tree should produce one standalone application per qualified
ABI/CPU/API profile, with CLI and TUI first and a future native GUI in that same
application where the host permits. Version-specific artifacts may share a binary
when evidence proves compatibility; a separate binary for every marketing release
is not required.

The named scope is DOS, Windows families, OS/2 and ReactOS. It does not silently
include every unrelated Microsoft or IBM operating system. A platform cannot
expose metrics or process semantics its kernel does not supply. The project's
superset goal therefore means common interaction plus explicitly declared native
capabilities, not invented data on old kernels.

Authority order is the user's current explicit instructions, repository instructions,
adopted product decisions, this proposed engineering bundle, then informative
source observations. Existing authorization remains valid. Importing another
project's procedures does not impose its permission workflow on this repository.
No generated or verified frontmatter value is an execution credential.

The [source registry](../references/sources.json) pins observed repository revisions
and describes limits. NTop is the inherited source baseline.[^ntop] Its GPL notice
must be preserved. Code learned from sibling projects is not automatically
available under a compatible license: record origin, applicable license,
contributor rights and modifications before importing it. Learning an algorithm
or contract is distinct from copying an implementation.

Each concept owns its requirement statements. [Traceability](../catalog/traceability.json)
maps them to acceptance cases; [evidence](../catalog/evidence.json) records actual
executions. Targets, commands and work units have one canonical registry each.
Generated indexes are disposable projections, not a second authority.

[^ntop]: Inherited NTop source and license.

## Requirements and acceptance

### WT-REQ-001-01

Every external source or copied component MUST retain provenance and an explicit applicable license decision before redistribution.

Acceptance: WT-AC-001-01 — Inspect a proposed reused component and reject missing revision/license information; preserve upstream notices.

### WT-REQ-001-02

Specifications, build success, runtime qualification and released support MUST be reported as separate states.

Acceptance: WT-AC-001-02 — A build-only fixture cannot become a qualified target or a passed native acceptance case.

### WT-REQ-001-03

Each target, command and work unit MUST have one canonical registry; prose MUST link to it instead of introducing a second authoritative list.

Acceptance: WT-AC-001-03 — Inspect registry ownership and reject duplicate IDs or contradictory reference mappings.
