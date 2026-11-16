---
type: "Wtop Specification"
title: "TUI presentation and feature parity"
description: "Prioritize a dense readable process table, safe actions and configurable meters."
resource: "urn:wtop:spec:wt-031"
tags: ["wtop", "interfaces"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T12:38:25Z"}
wtop: {"id": "WT-031", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-011", "WT-012", "WT-030"], "requirements": ["WT-REQ-031-01", "WT-REQ-031-02", "WT-REQ-031-03", "WT-REQ-031-04", "WT-REQ-031-05", "WT-REQ-031-06", "WT-REQ-031-07"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}, {"id": "ntop", "resource": "../references/sources.json#ntop"}, {"id": "htop-controls", "resource": "../references/sources.json#htop-controls"}, {"id": "htop-colors", "resource": "../references/sources.json#htop-colors"}]
---

# TUI presentation and feature parity

Keep the htop/NTop visual hierarchy: compact CPU/memory meters, a large process
table with stable headers and a visible command/status footer. The default uses
htop-style semantic colors, no mandatory Unicode artwork and no decorative animation.
Provide monochrome and low-color fallbacks, readable selection and an ASCII path.
Optional history charts belong to selectable panels, not a forced btop layout.

The existing NTop interaction is the code migration baseline, while the default
keymap is now htop-style. Optional vi/vim and Emacs maps use the same actions.
The inherited baseline includes: scrolling, searching,
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

## Default controls and presentation

The default htop map provides F1 help, F2 setup, F3 search, F4 filter, F5 tree,
F6 sort, F7 higher Windows priority class, F8 lower class, F9 termination choices
and F10 quit. Windows class names remain visible; do not call them Unix nice.
Arrows, paging, Home/End, tagging and tab navigation remain discoverable.[^htop-controls]
A map-specific key must not inherit a conflicting old meaning: in particular,
htop k requests a process action while vi k moves up. Emacs control/meta bindings
are mode aware and must not steal text editing or accidentally trigger actions.
Function keys and the help screen provide a common recovery path.

The bottom row is a function/control bar, shown by default. Its displayed actions
and click regions derive from the same command context. The current candidate
retains common F1–F10 navigation cells across panels; fully contextual overlay
labels remain an open portion of this requirement. Live mode, sort, filter,
selection, collection/error and unsaved-settings status belongs in the adjacent
status area when space permits. Hiding the bar is a saved preference and gives
its row back to the process view; errors and active prompts remain visible.
Narrow terminals reduce/shorten hints without moving invisible hitboxes offscreen.
The user can always recover help/setup using the common keys.

Mouse support is optional and capability checked. Clicks select rows, headers
change sorting, wheel input scrolls, tabs switch views, and visible bar cells
invoke the same actions as keys. Destructive actions still require the same
explicit confirmation and lifetime validation. The absence of mouse reporting,
a disabled mouse preference or failed console-mode negotiation preserves keyboard
use. Restore original console input/output modes on normal exit. No click should
act on an unseen replacement row after a refresh or resize.

The default htop-derived color roles are white text on black, black/green column
headers, black/cyan selection and function labels, cyan meter text, and yellow
tag/follow emphasis. CPU and used-memory bars use green. An explicitly selected
NTop scheme preserves the prior aesthetic; monochrome remains available.[^htop-colors]
These are terminal color roles, not a guarantee that every host's palette has the
same RGB values. Do not assign htop's CPU subcategory colors to uncollected
categories or label Windows commit as swap to mimic an image.

Header rows, their order, process columns/order, sort direction, tree view and
named tabs are configuration data. The candidate starts with Main and I/O views;
all exposed views must be actual projections over the same process observations.
Saved columns use stable IDs rather than screen positions. The setup interface
must disclose which setting is global and which belongs to the current tab.
Arbitrary graphical panels, plugin tabs, affinity, thread/detail views and full
htop parity remain separate work until implemented and qualified.

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

### WT-REQ-031-04

Default htop, vi/vim and Emacs input maps MUST resolve through shared actions and preserve mode-specific meanings without accidental destructive aliases.

Acceptance: WT-AC-031-04 — Compare independent key/action tables, modifiers and editing contexts; htop k requests termination while vi k navigates, with identical action guards.

### WT-REQ-031-05

The last-row function bar MUST be visible by default, hideable, context-sensitive and consistent with the active keymap and action availability.

Acceptance: WT-AC-031-05 — Exercise main/help/setup/search/filter/confirmation states and narrow/short terminals; hints and hitboxes remain inside displayed cells and hiding the bar returns its space.

### WT-REQ-031-06

Mouse input MUST use the same actions and identity guards as keyboard input, degrade to keyboard operation, and restore caller console modes.

Acceptance: WT-AC-031-06 — Inject click/wheel/header/tab/bar events, disabled/unsupported mouse and resize races; verify selection/sort/scroll outcomes, no accidental termination and restored modes.

### WT-REQ-031-07

Persisted views MUST use stable column identifiers and bounded named tabs, with explicit header visibility/order, sorting and tree settings.

Acceptance: WT-AC-031-07 — Round-trip differing tab layouts, reordered/hidden columns and headers, active tab and sort direction; reject duplicate/out-of-range layouts and preserve inactive-tab state.

[^htop-controls]: Pinned upstream manual and Action.c were inspected for key semantics.
[^htop-colors]: Pinned upstream CRT.c default scheme; only behavior/color roles are referenced.
