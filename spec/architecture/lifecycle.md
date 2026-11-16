---
type: "Wtop Specification"
title: "Sampling, callbacks and shutdown"
description: "Give every sample, callback and resource an explicit owner and a bounded observable lifecycle."
resource: "urn:wtop:spec:wt-012"
tags: ["wtop", "architecture"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-012", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-010", "WT-011"], "requirements": ["WT-REQ-012-01", "WT-REQ-012-02", "WT-REQ-012-03"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}, {"id": "syspane-owner", "resource": "../references/sources.json#syspane-owner"}]
---

# Sampling, callbacks and shutdown

The baseline scheduler has one owner. Sampling cadence and redraw cadence are
separate concepts even when the first implementation uses one interval for both.
The candidate interval range is owned by the command registry. Slow collection
does not launch overlapping unbounded work; skipped deadlines and stale data are
observable. Refresh never changes the wall clock or resets the source epoch.

A provider request has a cancellation flag, deadline, row/string/byte limits and
an explicit completion result. A deadline is checked around calls and between
bounded operations. An API that can block inside the kernel is not made
preemptible by checking a flag. Until its behavior or isolation is qualified,
mark that provider's shutdown bound unproven. Do not claim a universal hard bound
for the inherited synchronous NTop collector. In particular, account-name lookup
can block during an initial or one-shot sample; a later background-worker grace
period does not bound those synchronous calls. Keep that limitation visible until
the provider/caching/isolation design is qualified.

The current modern-NT candidate adds two emergency exit paths: normal cleanup
restores the console and gives the owned worker 3000 ms to finish; a console
control callback waits up to 3500 ms for cleanup completion. On timeout either
path terminates only Wtop's own process with exit code 124. The callback path also
covers a stalled initial or one-shot query, but is an emergency process exit,
not provider cancellation. If the main thread cannot reach cleanup after TUI
activation, that path can bypass console restoration. Native fault-injection
and timing evidence are still absent, so these constants are candidate policy,
not a qualified restoration or shutdown guarantee. The lifecycle requirements
below remain goals that this fallback alone does not satisfy.

Callbacks record owner, context, allowed thread, reentrancy, lifetime and failure
behavior. No retained callback points into a stack frame or an already-destroyed
view. Platform callbacks must not let language exceptions cross a C boundary.
On Win16 cooperative hosts, work is chunked around the native message loop;
modern threads are not forced onto targets without them.

Shutdown stops admission, cancels outstanding work, drains or joins only owned
work, restores terminal modes/cursor/screen buffer, closes handles and releases
memory in reverse dependency order. A user interrupt during output or collection
has a distinct cancellation result. Repeated close is safe. If a provider needs
process isolation, document the exact helper invocation and owned-process exit
protocol; basic monitoring still installs no service.

SysPane's MeasurementOwner serializes one stateful clock and preserves exact
ticks/exceptions, while its contract keeps the owner alive through worker exit.
It explicitly says synchronization is not preemption.[^syspane-owner] Adapt that
ownership rule, not an assumption that a mutex solves blocking calls.

[^syspane-owner]: Serialized measurement owner and inspected lifecycle handoff.

## Requirements and acceptance

### WT-REQ-012-01

Refresh scheduling MUST prevent overlapping unbounded samples and report stale/missed intervals truthfully.

Acceptance: WT-AC-012-01 — A deliberately slow collector stays within its declared in-flight/queue bounds and leaves input responsive where promised.

### WT-REQ-012-02

Every callback and worker MUST have a checked owner/lifetime contract; shutdown MUST restore terminal state and release owned resources.

Acceptance: WT-AC-012-02 — Exercise close, resize, interrupt, provider failure and repeated shutdown under memory/handle checks.

### WT-REQ-012-03

A timeout MUST NOT be described as preemption; potentially blocking providers MUST retain an explicit unqualified or isolated lifecycle status.

Acceptance: WT-AC-012-03 — An uncooperative provider fixture cannot produce a false bounded-shutdown pass.
