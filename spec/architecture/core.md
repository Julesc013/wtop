---
type: "Wtop Specification"
title: "Architecture and portable core"
description: "Separate collection, measurement, commands and presentation while preserving a small native program."
resource: "urn:wtop:spec:wt-010"
tags: ["wtop", "architecture"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-010", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-001"], "requirements": ["WT-REQ-010-01", "WT-REQ-010-02", "WT-REQ-010-03"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}, {"id": "screensave", "resource": "../references/sources.json#screensave"}, {"id": "syspane-cpu", "resource": "../references/sources.json#syspane-cpu"}]
---

# Architecture and portable core

Refactor the existing C program incrementally. First repair observable defects
and preserve behavior; then extract boundaries when a second consumer or a target
requires them. The intended dependency direction is native provider -> raw snapshot
-> shared measurement/state -> command model -> presentation. TUI, noninteractive
output and the future GUI consume the same semantic snapshot and action results.
The model never calls a renderer, a terminal or an installer.

The shared core uses a documented C90 subset, checked allocation and explicit
integer-width contracts. Do not assume int is 32 bits, long is 64 bits, pointer
width equals a PID, or bytes can be copied directly onto a wire. A target types
header supplies verified widths; 16-bit targets may need split 64-bit arithmetic,
far-pointer policy and segmented allocation. Every multiplication/addition used
for byte counts is checked before allocation or indexing.

Avoid a mandatory runtime package, language interpreter, service, database or
plugin loader. Platform providers, console drivers and command implementations
compile into the target application. An optional future isolated helper can be
another invocation of the same executable when the OS supports it; it is not a
default background service. Loading a sensor SDK or driver is a separately declared
capability, never an implied dependency of basic process viewing.

Use small headers for values and ownership contracts. Keep Windows structures in
Windows providers; keep console cells in a presentation layer. Shared callbacks
have exact function-pointer types and explicit context ownership. OS callbacks
use the precise ABI/calling convention required by the target. No pointer punning
or generic callback casts are justified by apparent success on x64.

ScreenSave's portable/native separation is a useful model, but its current C
headers assert a four-byte unsigned int and do not already cover Win16.[^screensave]
SysPane supplies useful measurement semantics but its inspected runtime is modern
C++; contracts and independent test cases can be adapted without adopting a large
framework.[^syspane-cpu] Import only small components with settled licensing.

[^screensave]: Portable semantics, session/base headers and support matrix.
[^syspane-cpu]: Processor state and modern Windows acquisition.

## Requirements and acceptance

### WT-REQ-010-01

CLI, TUI and future GUI MUST consume one shared snapshot/action model rather than duplicate collection or CPU formulas.

Acceptance: WT-AC-010-01 — Compare text/JSON/TUI projections of a fixed snapshot and ensure identical units, identity and values.

### WT-REQ-010-02

The portable core MUST compile with explicit checked type widths and contain no host window, console or installation handles.

Acceptance: WT-AC-010-02 — Compile the core under strict C90 checks and inspect public headers; a 16-bit-width fixture must not silently truncate.

### WT-REQ-010-03

Baseline monitoring MUST require no added interpreter, service or third-party runtime installation.

Acceptance: WT-AC-010-03 — Audit linked/imported dependencies and launch the delivered application from an arbitrary clean folder.
