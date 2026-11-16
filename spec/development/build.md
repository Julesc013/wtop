---
type: "Wtop Specification"
title: "Build and dependency policy"
description: "Use reproducible native target lanes with explicit imports and no hidden runtime dependencies."
resource: "urn:wtop:spec:wt-060"
tags: ["wtop", "development"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-060", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-010", "WT-020"], "requirements": ["WT-REQ-060-01", "WT-REQ-060-02", "WT-REQ-060-03"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}, {"id": "watcom", "resource": "../references/sources.json#watcom"}, {"id": "pe-format", "resource": "../references/sources.json#pe-format"}]
---

# Build and dependency policy

Begin by compiling the inherited Windows program under the current maintained
toolchain, repairing concrete warnings, pointer-width/Unicode/formatting defects
and unsafe library use without a wholesale rewrite. Keep the diff reviewable.
The baseline build must not be confused with historical qualification.
Application C/headers and inherited build/configuration/packaging inputs live
under source/. Root CMakeLists.txt remains the build entry point;
source/build.bat resolves that root from its own location. Build artifacts stay
outside tracked source. source/_config.yml is retained site input, and
source/ntop.nuspec is legacy packaging metadata, not a Wtop installer manifest.
source/wtop.conf is generated from current defaults; source/ntop.conf retains the
legacy import example.

The shared model aims at strict C90. Native adapters may use isolated compiler
extensions when required by Windows headers/ABI; record each exception rather
than raising the whole source language silently. Modern toolchains and their CRTs
may import APIs unavailable on old systems. A version macro, subsystem version or
static CRT switch alone does not establish compatibility.

Each target lane records compiler/linker/SDK identifiers, flags, architecture,
object/executable format, runtime linkage and import policy. Build output belongs
under owned build directories. A missing optional toolchain reports unavailable;
it does not pass by skipping the entire target. Pin dependencies when introduced,
keep license/provenance, and prefer existing OS facilities over bundled frameworks.

The current native binary may remain Unicode/modern-NT only during extraction.
The classic ANSI and 16-bit adapters are later explicit profiles. Ensure string
and API selection is intentional: a wide build does not automatically work on
Win9x, and ANSI text needs a declared code page/encoding conversion policy.
DOS1/Win1/OS2 prototypes first prove startup, input/output, enumeration and config
read without importing modern runtime assumptions.

For modern profiles, use warning-clean builds, meaningful sanitizers on portable
code where available, stack/heap checks and explicit overflow tests. For old
profiles, inspect imports/headers/segments and run target-specific smoke tests.
Cross-compilation is valuable evidence of syntax/linkage only; execution and
interaction require separate records. Build tooling may require modern Python
or CMake on the development host without imposing those on the runtime target.

## Requirements and acceptance

### WT-REQ-060-01

Every build candidate MUST record source revision, toolchain, flags, dependencies and artifact identity; historical import limits MUST be audited explicitly.

Acceptance: WT-AC-060-01 — Rebuild a named lane and inspect the produced machine/subsystem/import/CRT data against its profile.

### WT-REQ-060-02

Warnings or test failures MUST be fixed or scoped explicitly without weakening unrelated gates or silently skipping a required target.

Acceptance: WT-AC-060-02 — A missing compiler or failed gate appears as unavailable/failed in evidence, never pass.

### WT-REQ-060-03

Portable source MUST be checked for integer-width, allocation and encoding assumptions before a legacy lane is promoted.

Acceptance: WT-AC-060-03 — Strict core build and adversarial width/encoding fixtures expose truncation and invalid conversions.
