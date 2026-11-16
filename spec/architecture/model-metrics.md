---
type: "Wtop Specification"
title: "Identity, snapshots and metric semantics"
description: "Define truthful typed measurements, reset handling and stable process identity before rendering."
resource: "urn:wtop:spec:wt-011"
tags: ["wtop", "architecture"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-011", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-010"], "requirements": ["WT-REQ-011-01", "WT-REQ-011-02", "WT-REQ-011-03", "WT-REQ-011-04"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}, {"id": "syspane-cpu", "resource": "../references/sources.json#syspane-cpu"}]
---

# Identity, snapshots and metric semantics

A snapshot has a schema version, source epoch, provider ID, target profile,
sequence, monotonic acquisition start/end, optional wall-clock timestamp and
bounded entity/metric collections. A process key combines source epoch, native
PID and a creation/lifetime token. PID alone is not durable identity. If the host
cannot supply a trustworthy lifetime token, record that limitation and revalidate
the strongest available identity immediately before a destructive action.

Every metric has an ID, unit, value, state, measurement interval and provider
definition. The common states are valid, warming_up, unsupported, denied, stale,
reset, error and capacity. A non-valid current value is null; an optional retained
last value remains explicitly stale with its original timestamp. Zero is valid
only after a successful measurement. Never use zero for a failed API call.
Entity presence and metadata access are separate: a denied process remains visible
where its existence can be observed.

Process CPU uses delta kernel-plus-user execution divided by monotonic elapsed
time. The declared one-core normalization is 100 percent for one fully occupied
logical processor and can exceed 100 percent on a multithreaded process. Optional
whole-machine normalization divides by the declared active capacity; label the
mode and denominator. A first sample cannot produce a rate. A process lifetime,
clock/source epoch or CPU topology change invalidates the prior comparison.

Host CPU depends on provider semantics. A provider must name whether it uses
accounted busy/total time, wall time, cycle accounting or capacity-sensitive
utilization. For a Windows accounted-time profile, use non-idle kernel plus user
as busy and include idle in total. Do not double-count idle inside GetSystemTimes
kernel time, add interrupt/DPC subsets twice, or compare different API denominators.
Aggregate raw numerators/denominators, not rounded CPU percentages.[^syspane-cpu]

Report working set, private committed bytes, virtual size, available physical
memory, commit and swap as distinct quantities; never relabel Windows working set
as Linux's exact RSS accounting. I/O bytes are provider-reported transfers, not
necessarily physical disk traffic. Sampling misses tasks that start and exit
between observations unless an optional qualified event/accounting provider exists.
GPU engine time, memory and power are distinct; sensor data needs a source,
access status and age. Unknown process-parent edges, PID reuse and cycles yield
safe roots/diagnostics rather than an unbounded tree walk.

JSON snapshots use a versioned object, deterministic field order for tests and
UTF-8 escaping. Exact counters that may exceed JavaScript's integer range use
decimal strings with declared units; percentages remain bounded finite numbers
under their declared normalization. Wire encoding is independent of host struct
layout. Initial candidate output may cover only the documented smaller subset.

[^syspane-cpu]: SysPane's explicit accounting and first/reset/gap contract.

## Requirements and acceptance

### WT-REQ-011-01

Process selection, following and actions MUST use a lifetime-aware identity and MUST NOT target a reused PID silently.

Acceptance: WT-AC-011-01 — A fixture reuses a PID between snapshots and an action; the old selection is retired and action is refused.

### WT-REQ-011-02

Rates MUST use successful same-domain intervals and preserve first/reset/gap/topology/denied states as null or explicit unavailable values.

Acceptance: WT-AC-011-02 — Independent arithmetic fixtures cover zero/full load, resets, PID reuse, counter wrap, zero time, missing rows and access denial.

### WT-REQ-011-03

Metric names, units, CPU normalization and memory/I/O definitions MUST be consistent in all presentations and machine output.

Acceptance: WT-AC-011-03 — Feed a fixed snapshot through each projection and compare exact counters plus documented derived ratios.

### WT-REQ-011-04

Snapshot counts, strings, arithmetic and serialized output MUST have explicit limits and MUST reject or report capacity exhaustion without healthy truncation.

Acceptance: WT-AC-011-04 — Boundary and malformed-provider fixtures cannot overflow, hang, emit nonfinite JSON or present partial data as complete.
