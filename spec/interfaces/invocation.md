---
type: "Wtop Specification"
title: "Invocation and command model"
description: "Preserve a useful default TUI while defining explicit noninteractive and unavailable modes."
resource: "urn:wtop:spec:wt-030"
tags: ["wtop", "interfaces"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T12:38:25Z"}
wtop: {"id": "WT-030", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-011", "WT-012", "WT-020"], "requirements": ["WT-REQ-030-01", "WT-REQ-030-02", "WT-REQ-030-03", "WT-REQ-030-04"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}]
---

# Invocation and command model

[commands.json](../catalog/commands.json) owns tokens, arguments, defaults,
candidate/implemented status and acceptance references. Documentation and checked
help generation derive from that registry. Registry candidate
means implementation is being developed and needs evidence; planned entries must
not be advertised as completed commands.

The intended default and --tui run the interactive process view. The current
candidate CLI introduces --snapshot and --dump (plus inherited -d) as one
synchronous sample, and --format text|json; supplying format alone selects a
snapshot. Explicit --tui with noninteractive format/snapshot options is an error.
The first rate sample may be unavailable, never fabricated as measured zero.
The inherited filter/sort/monochrome options retain compatible syntax where safe.

--help, --version and --capabilities are bounded information paths. They must not
start a collector, load settings unnecessarily, alter console state or require
administrator privileges. --capabilities describes implemented or unavailable
capabilities with evidence scope; compile-time availability is not a live metric.
--gui is reserved and returns an explicit unavailable diagnostic and usage exit
code. No blank window, fallback TUI or pretend GUI is acceptable.

Presentation options select htop/vi/vim/Emacs bindings, htop/NTop/monochrome
color roles, mouse preference and function-bar visibility. Command-line choices
override file settings deterministically. --write-config PATH is an explicit
settings export/save action, separate from process collection. Information paths
still perform no writes or collection. Save/export conflicts with explicit
TUI/snapshot/GUI modes, output format, information actions and unsaved -p/-n/-u
selectors. Presentation settings, interval and active-tab sort can be exported.
Its destination and encoding are documented by the settings contract; it is not
an installer.

Candidate `query processes|system|services`, `inspect PID` and finite `log`
now share the model and selector parser. Inspect accepts optional command-line
and service providers; snapshots/query/log can request per-core collection.
`query system|services` does not open every process for unused row metrics.
`--paths` resolves ownership without loading configuration or collecting processes.
`--readonly` blocks process mutations, shell execution and settings writes, while
explicit telemetry logging is permitted. Typed CLI mutation commands remain planned.

Inherited `-d` remains dump; htop's decisecond delay is spelled `--delay` (1..600).
`--interval` is milliseconds (50..60000); later values win. Short/long aliases are
keymap-independent; long options accept equals syntax. A final `--` is accepted,
but unsupported positional operands still fail. Help is generated from the sole
command catalog and CI checks it for drift.

Snapshot schema 2 replaces `cpu_share` with `cpu_percent` plus named core/machine
normalization, adds typed system totals, per-observation timing and optional
provider results. Information commands remain schema 1. Exact 64-bit values are
decimal strings; unavailable values are null with status. Account SID is the
stable owner value; display-name lookup carries separate state and timing.

`log --output PATH|-` requires `--samples` and/or `--duration` in milliseconds;
its output is JSON Lines with a default 16 MiB complete-record cap. Existing
files are refused. Sampling and optional-provider waits are clamped to remaining
duration, but blocking native calls/streams have no hard wall-time guarantee.
The output cap does not cap temporary staging I/O: one bounded full model record
is staged before its size is checked. No background service is created.
 Do not overload ambiguous positional words or execute an unknown
command through a shell. Inherited :exec remains an explicit interactive shell
action, never a configuration or telemetry interpretation. New process mutations
require explicit target identity and action, with per-target outcomes.

Output goes to stdout, diagnostics to stderr. JSON is valid UTF-8 data with no
ANSI sequences, progress text or locale-specific decimal separators. Text output
may be localized later while stable field/command identifiers remain invariant.
No interactive prompt is emitted when input is redirected. Noninteractive mode
does not modify terminal buffers or cursor state. Broken pipes and cancellation
are handled without success claims for an incomplete requested export.

The baseline exit policy is 0 for completed success, 1 for runtime failure,
2 for invalid/conflicting/unavailable invocation, 3 for explicitly partial/provider/capacity results, 130 for cooperative
cancellation and 124 for emergency own-process shutdown. Partial results include explicit
status and may require a nonzero exit; each future command decides this in its
registry entry. Unknown options and malformed numeric/CSV selectors are errors,
not silently ignored. Numeric parsing checks signs, overflow and trailing bytes.

The [candidate output contract](../../docs/cli-output.md) records schema-2 field
types, provider error domains, identity/timing and exit semantics.

## Requirements and acceptance

### WT-REQ-030-01

Mode selection, aliases, argument bounds and conflict handling MUST match the sole command registry and preserve explicit GUI unavailability.

Acceptance: WT-AC-030-01 — Table-driven parser cases cover default/TUI/snapshot/format, missing values, duplicates, conflicts, overflows and --gui exit2.

### WT-REQ-030-02

Help/version/capabilities and noninteractive output MUST not acquire interactive terminal state or launch unnecessary collection.

Acceptance: WT-AC-030-02 — Use an injected collector/console spy and redirected output; information commands perform zero samples and no console mutation.

### WT-REQ-030-03

Machine output MUST be versioned, parseable and truthful about unavailable first-sample rates and partial failures.

Acceptance: WT-AC-030-03 — Parse JSON with an independent parser and compare fixed counters/status against model fixtures, including hostile process names.

### WT-REQ-030-04

Presentation modifiers and explicit settings export MUST have deterministic precedence and remain separate from collection and informational commands.

Acceptance: WT-AC-030-04 — Combine CLI keymap/theme/mouse/bar overrides with config/no-config and metadata; invalid/conflicting forms fail before collection and write-config performs no process sample.
