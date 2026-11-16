---
type: "Wtop Specification"
title: "Dependency-ordered implementation"
description: "Advance through useful vertical slices while keeping legacy and GUI work explicit."
resource: "urn:wtop:spec:wt-070"
tags: ["wtop", "roadmap"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-070", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-001", "WT-002", "WT-010", "WT-020", "WT-030", "WT-062"], "requirements": ["WT-REQ-070-01", "WT-REQ-070-02"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}]
---

# Dependency-ordered implementation

[work/units.json](../work/units.json) is the only work queue. Each unit names
prerequisites, context concepts, paths, deliverables, acceptance IDs and evidence.
The spec tool prints current active work and dependency-ready planned units.
It never executes commands, expands permission or marks work complete.

The sequence starts with the existing fork, scoped defect repair and modern
Windows build. It then establishes invocation, a shared snapshot model and
truthful CLI serialization. Process identity/actions and TUI parity build on that
model; configuration and packaging use one path/ownership policy. Optional
hardware/event/history features follow bounded collection and native proof.

Legacy work first proves narrow startup/API/renderer/collector prototypes.
Win32 legacy, DOS, Win16, OS2, CE, WinPE and ReactOS are independent profiles
where feasible; failure in one does not halt already authorized independent work.
Do not spend early work manufacturing speculative abstraction layers to satisfy
an untested platform. Introduce a shared abstraction only when two users or a
specific platform constraint justify it.

The GUI work is deferred and depends on the command/model/lifecycle boundary.
It should preserve the user's table-first presentation in native controls or a
native cell-grid view. No GUI runtime or separate executable is shipped merely
to reserve its future design. Native UI differences may require host-specific
entry/console behavior; qualify the same-executable launch model per target.

Each work unit stops implementation when its bounded deliverable and relevant
acceptance are addressed, records evidence and remaining risk, then continues
to the next authorized dependency-ready unit. Do not invent a mandatory approval
gate for every reversible task. Human review, release authorization and source
implementation are distinct. Unresolved questions become concrete decision records
only when they materially affect the next implementation, not a vast speculative
framework.

Code organization follows proven responsibilities. Retain inherited names until
a small tested rename is useful. Do not copy SysPane, ScreenSave or DiskEd source
trees wholesale; record reusable contracts and settle licenses for copied code.
Keep one owner for process accounting, config paths, command parsing and each
native resource lifetime.

## Requirements and acceptance

### WT-REQ-070-01

Work MUST proceed through dependency-ready bounded units with explicit deliverables and evidence, preserving active partial work.

Acceptance: WT-AC-070-01 — A cyclic/missing dependency or completed-without-evidence unit fails; next lists only admissible dependency-ready work.

### WT-REQ-070-02

GUI, installation channels and historical targets MUST remain explicitly deferred/unqualified until their separate acceptance closes.

Acceptance: WT-AC-070-02 — Status output cannot turn a reserved command or targeted platform into an implemented/released feature.
