---
type: "Wtop Specification"
title: "Acceptance and evidence"
description: "Bind each claimed result to the actual source, command, environment, artifact and independent oracle."
resource: "urn:wtop:spec:wt-062"
tags: ["wtop", "development"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-062", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-002", "WT-011", "WT-060", "WT-061"], "requirements": ["WT-REQ-062-01", "WT-REQ-062-02", "WT-REQ-062-03"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}]
---

# Acceptance and evidence

[traceability.json](../catalog/traceability.json) maps every requirement to at
least one acceptance design. [acceptance.json](../catalog/acceptance.json) owns
method, independent oracle and execution state. [evidence.json](../catalog/evidence.json)
contains actual records only. A requirement heading is the sole authored statement;
the trace table does not duplicate it. No initial product acceptance is passed
merely because these files validate.

Evidence identifies the source revision and exact source-file hashes, environment,
command, timestamps, exit result, raw log hash and limitations. Each record names
its exact target and command IDs; a passing native result for one target cannot
qualify another. A native test identifies the OS image/build, architecture and
artifact hash. Store bounded logs
under an owned evidence location and retain failures alongside later successes.
A hash establishes identity/integrity, not human review or authenticity.

Use independent arithmetic oracles for CPU/rates; parser fuzz/boundary cases for
untrusted inputs; deterministic fixed snapshots for output; actual target behavior
for process rights and terminal lifetime; and downloaded-byte tests for releases.
A test that repeats the same implementation expression is weak evidence. Prefer
cases that would fail a plausible incorrect implementation: missing CPU groups,
counter reset, stale PID, mismatched intervals, false zero and path fallback.

Performance measurement records process/thread count, terminal dimensions, enabled
providers, sample/redraw interval, transport and hardware. Report wall-clock
latency, CPU time, working set/allocation peak, handle count and bytes written with
units and dispersion. There is no justified universal speed ranking or promised
sub-megabyte executable in the initial spec. Initial budgets must be measured and
set per target before release; inability to measure remains unknown.

Native UI checks exercise keyboard/mouse where supported, resize, empty lists,
large trees, readability, cancellation and restoration. GUI tests are deferred
until a GUI exists. Hardware sensor coverage needs actual hardware evidence;
synthetic topology cannot certify a 128-CPU machine or hotplug.

The spec tool validates structure and evidence references; it cannot determine
that a human truly reviewed an artifact or that a recorded command's scientific
oracle is correct. Review that content separately. A changed source invalidates
an unqualified applicability claim; a narrow test need not rerun unrelated checks.

## Requirements and acceptance

### WT-REQ-062-01

Every passed acceptance MUST bind to existing source and log hashes, a concrete command/environment and its stated oracle.

Acceptance: WT-AC-062-01 — Missing logs, changed source, invented IDs and empty evidence are rejected; a valid scoped record does not certify other targets.

### WT-REQ-062-02

CPU, memory, latency and compatibility claims MUST state units, conditions, scope and unresolved limitations.

Acceptance: WT-AC-062-02 — Review a benchmark and a native compatibility record against required conditions; unknown measurements remain unknown.

### WT-REQ-062-03

Failures and inconclusive outcomes MUST remain preserved when later attempts pass.

Acceptance: WT-AC-062-03 — A multi-attempt evidence fixture keeps original failure records and attaches the later pass to its own exact subject.
