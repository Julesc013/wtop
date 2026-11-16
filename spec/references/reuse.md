---
type: "Wtop Specification"
title: "Sibling-project reuse decisions"
description: "Reuse small proven contracts with their qualification and licensing limits intact."
resource: "urn:wtop:spec:wt-080"
tags: ["wtop", "references"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-080", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-001", "WT-010"], "requirements": ["WT-REQ-080-01"]}
sources: [{"id": "ntop", "resource": "../references/sources.json#ntop"}, {"id": "syspane-cpu", "resource": "../references/sources.json#syspane-cpu"}, {"id": "syspane-owner", "resource": "../references/sources.json#syspane-owner"}, {"id": "screensave", "resource": "../references/sources.json#screensave"}, {"id": "disked-okf", "resource": "../references/sources.json#disked-okf"}, {"id": "aide-okf", "resource": "../references/sources.json#aide-okf"}]
---

# Sibling-project reuse decisions

NTop supplies the actual starting process-console code and GPL provenance.
Keep its useful presentation and Vi-style interaction while repairing unsafe
edges and separating measured state from screen operations.[^ntop]

SysPane provides concrete CPU state semantics: first/reset/gap/topology reasons,
weighted raw-counter aggregation and a serialized measurement owner.[^syspane-cpu][^syspane-owner]
Its current Windows provider is modern x64/PDH code; one-group native evidence
does not prove all processors or historic Windows. Its C++ implementation is
not a C90/Win16 library. Adapt contracts and independent fixtures first; copying
source requires explicit compatible rights.

ScreenSave contributes a portable-model/native-host boundary, versioned C
descriptors and evidence classes.[^screensave] Its existing unsigned-int width
assertion does not prove 16-bit portability. Its broad Windows target range and
successful builds are not universal runtime certification. Preserve the lesson
that a visual rejection remains a rejection despite other passing technical tests.

DiskEd contributes small concept modules, stable IDs, canonical catalogs and
requirement-to-acceptance mapping.[^disked-okf] AIDE contributes separation of
authored knowledge, generated status and actual execution provenance.[^aide-okf]
Neither becomes a mandatory Wtop runtime, scheduler or approval service.

FacMan, Dominium, Eureka and other projects are potential sources of package,
ownership or integration patterns. No code-level reuse conclusion is made here
without a pinned relevant source review. Add a small source-bound decision when
an actual Wtop requirement calls for it; do not infer compatibility from a name.

For any extraction record: the exact source/revision and files, owner/license,
problem solved, minimal interface, target dependencies, retained tests, modified
semantics and evidence limits. Prefer a narrow header/algorithm/fixture over a
second platform framework. Shared concepts do not require importing every
upstream implementation detail.

[^ntop]: Actual inherited baseline.
[^syspane-cpu]: Inspected CPU state/acquisition contract.
[^syspane-owner]: Clock callback owner.
[^screensave]: Portable semantics and evidence boundaries.
[^disked-okf]: Specification organization and traceability.
[^aide-okf]: Knowledge profile and authority distinction.

## Requirements and acceptance

### WT-REQ-080-01

Each reused component MUST have a minimal boundary, source/license record and retained qualification limits; a sibling framework MUST not become a hidden runtime prerequisite.

Acceptance: WT-AC-080-01 — Review a proposed extraction against its dependency/rights/test record and reject unsupported parity claims.
