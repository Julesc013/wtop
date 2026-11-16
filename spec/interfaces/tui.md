---
type: "Wtop Specification"
title: "TUI presentation and feature parity"
description: "Prioritize a dense readable process table, safe actions and configurable meters."
resource: "urn:wtop:spec:wt-031"
tags: ["wtop", "interfaces"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-031", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-011", "WT-012", "WT-030"], "requirements": ["WT-REQ-031-01", "WT-REQ-031-02", "WT-REQ-031-03"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}, {"id": "ntop", "resource": "../references/sources.json#ntop"}]
---

# TUI presentation and feature parity

Keep the htop/NTop visual hierarchy: compact CPU/memory meters, a large process
table with stable headers and a visible command/status footer. The default uses
conservative colors, no mandatory Unicode artwork and no decorative animation.
Provide monochrome and low-color fallbacks, readable selection and an ASCII path.
Optional history charts belong to selectable panels, not a forced btop layout.

The existing NTop interaction is the migration baseline: scrolling, searching,
sorting, tree view, tagging, following, explicit killing and Vi-style commands.
Read the inherited code before claiming feature parity.[^ntop] The parity work
adds consistent live filters, configurable columns, a discoverable help view,
mouse input where available, thread view, priority/affinity controls where the
kernel permits, and a process-detail view. Unsupported actions are visibly
disabled with a reason. Windows priority classes are not relabeled as Unix nice.

Selection and tags follow lifetime IDs, not row positions. Sorting is stable
with a deterministic tie-breaker. A selected process that exits is retired;
the next process reusing its PID is not automatically selected for an action.
Tree building detects missing parents, cycles and depth/capacity limits.
Filtering has a defined scope: parent context can remain visible but must not
be counted as a match. Searching highlights without changing the collection set.

Rendering computes a cell buffer and writes changed runs/rectangles using the
target's renderer. Width/height changes clamp all positions and recompute layout;
small terminals get a usable minimal view rather than out-of-range writes.
Wide characters, surrogate pairs and display width require an explicit Unicode
renderer; the conservative renderer substitutes unsupported glyphs predictably.
Process command lines are inert text: escape terminal control characters.

Themes and keymaps are declarative settings, not executable scripts or DLLs.
Reserve explicit command names independent of key bindings so mouse, keyboard,
CLI and future GUI can invoke the same action semantics. Every destructive bulk
action displays the intended identities and reports individual outcomes; no
refresh-driven implicit mutation occurs.

Performance claims must record host, terminal size, refresh interval, process
count, provider set and output transport. Avoid asserting that C or a smaller
screen is automatically faster. Low-overhead SSH/serial behavior requires a
qualified output adapter and measured bytes/CPU, not a blanket claim.

[^ntop]: Inherited interactive behavior is source evidence, not new Wtop testing.

## Requirements and acceptance

### WT-REQ-031-01

The TUI MUST preserve process identity through sorting, filtering, tree changes and resize without unsafe indexing.

Acceptance: WT-AC-031-01 — Exercise zero rows, shrinking lists, PID reuse, tree cycles and small/rapidly resized terminals.

### WT-REQ-031-02

The htop-tier parity plan MUST cover search/filter/tree/tag/follow, column and meter configuration, help, and supported process controls with visible capability limits.

Acceptance: WT-AC-031-02 — A feature audit maps each interaction to an acceptance case and observed implementation status; missing features remain planned.

### WT-REQ-031-03

Rendered external text and themes MUST be inert, bounded and legible through ASCII/monochrome fallbacks.

Acceptance: WT-AC-031-03 — Control-byte, long-name, Unicode-width and invalid-theme fixtures cannot inject terminal commands or corrupt layout.
