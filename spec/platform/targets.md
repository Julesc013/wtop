---
type: "Wtop Specification"
title: "Target profiles and compatibility"
description: "Qualify ABI, CPU, API and execution environment independently of source portability."
resource: "urn:wtop:spec:wt-020"
tags: ["wtop", "platform"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-020", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-010", "WT-011"], "requirements": ["WT-REQ-020-01", "WT-REQ-020-02", "WT-REQ-020-03"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}, {"id": "pe-format", "resource": "../references/sources.json#pe-format"}, {"id": "watcom", "resource": "../references/sources.json#watcom"}, {"id": "ce", "resource": "../references/sources.json#ce"}, {"id": "winpe", "resource": "../references/sources.json#winpe"}, {"id": "rt-policy", "resource": "../references/sources.json#rt-policy"}, {"id": "reactos", "resource": "../references/sources.json#reactos"}, {"id": "dos1", "resource": "../references/sources.json#dos1"}, {"id": "winpe-components", "resource": "../references/sources.json#winpe-components"}, {"id": "phone-kernel", "resource": "../references/sources.json#phone-kernel"}]
---

# Target profiles and compatibility

[targets.json](../catalog/targets.json) is the only target inventory. Each profile
names ABI, CPU, OS range, native presentation, packaging constraints and proof
state. An unqualified profile is a research/build objective, not a user support
promise. New evidence may consolidate or split profiles; never create a second
untracked list in build scripts or release prose.

The proposed legacy Win32 x86 profile initially investigates NT3.1 and Win9x using
strict import/runtime gating. Separate collector code is required even if one PE32
can launch across both kernels. Split artifacts only when a prototype demonstrates
an ABI, CRT or API reason. Modern x86, x64 and ARM64 outputs have independent
qualification; Windows11 has no native x86 OS edition. XP/Server2003 x64 remains
a legitimate historical x64 research target. Non-x86 historical NT architectures
are separate research entries, not silently covered by an x86 claim.

MZ, NE, LE/LX and PE identify different executable families, not interchangeable
APIs. A filename ending in .exe establishes nothing about CPU or minimum kernel.
PE machine/subsystem fields must match the target or qualified emulation.[^pe-format]
Open Watcom advertises DOS, Windows1 through3.x and OS2 targets, but that alone
does not establish Wtop compatibility.[^watcom]

Microsoft's DOS1.25 source contains both COM and relocatable EXE loading, so a
COM-only assumption is incorrect.[^dos1] The actual constraints include the
older FCB APIs, loader/header assumptions and absence of DOS2 handle-based file
calls; other 1.0/OEM variants still require explicit source/target validation.
DOS2+ and
multitasker profiles have their own semantics. Win16 uses native text-grid windows
where the later console API does not exist. OS2 VIO/PM and 16/32-bit memory models
have distinct adapters. Plain DOS task/TSR inspection must not pretend to expose
NT-style independently scheduled processes and affinity.

CE is an SDK/image/CPU/ISA profile; optional console, shell, filesystem and
telemetry components are probed.[^ce] Windows Mobile/CE must not be confused
with Windows Phone8's NT kernel; later mobile app models need their own
profile instead of inheriting desktop or CE assumptions.[^phone-kernel] WinPE's baseline uses matching
architecture, a subset of APIs and external/explicit persistence. MSI remains
unavailable. Cross-architecture execution is not a default capability; the
documented WinPE-x64-Support optional component permits x64 emulation on suitable
Arm64 WinPE images and needs its own ADK/image/component proof.[^winpe][^winpe-components]
Stock Windows RT's third-party desktop policy blocks
the requested ordinary native monitor/install path; ARM64 desktop Windows is a
different target.[^rt-policy] ReactOS remains a pinned-build compatibility target,
not automatic qualification for every Windows API.[^reactos]

Historical claims require actual target or qualified emulator execution, precise
image/service-pack/toolchain identities and interaction/collector/cleanup tests.
Compilation on a modern host, header availability and a PE import audit each
answer narrower questions. Never publish a broad historic support badge from them.

[^pe-format]: Microsoft executable-format specification.
[^watcom]: Toolchain's advertised target list, not Wtop results.
[^ce]: OEM platform SDK contract.
[^winpe]: Microsoft WinPE subset and limitations.
[^rt-policy]: Stock RT deployment restrictions.
[^reactos]: Upstream compatibility objective.
[^dos1]: Pinned Microsoft DOS1.25 COMMAND.ASM loader and MSDOS.ASM dispatcher.
[^winpe-components]: Microsoft optional-component reference; components must match image architecture and ADK build.

## Requirements and acceptance

### WT-REQ-020-01

Each delivered artifact MUST bind exactly one declared ABI/CPU/API profile and disclose tested OS images separately from intended range.

Acceptance: WT-AC-020-01 — Inspect artifact imports/header/runtime records; no targeted or build-only profile becomes supported.

### WT-REQ-020-02

Unsupported kernel capabilities MUST remain visibly unsupported; platform adapters MUST NOT invent parity or bypass stock platform policy.

Acceptance: WT-AC-020-02 — DOS/Win16/CE/RT fixture profiles expose only declared capabilities and precise unavailable reasons.

### WT-REQ-020-03

A historical profile MUST remain unqualified until native or explicitly qualified-emulator behavior is recorded for its actual artifact.

Acceptance: WT-AC-020-03 — A modern-host cross-build/import record cannot satisfy the historical runtime case.

[^phone-kernel]: Microsoft Research on the CE-to-NT transition for Windows Phone8.
