---
type: "Wtop Specification"
title: "Invocation and command model"
description: "Preserve a useful default TUI while defining explicit noninteractive and unavailable modes."
resource: "urn:wtop:spec:wt-030"
tags: ["wtop", "interfaces"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-030", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-011", "WT-012", "WT-020"], "requirements": ["WT-REQ-030-01", "WT-REQ-030-02", "WT-REQ-030-03"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}]
---

# Invocation and command model

[commands.json](../catalog/commands.json) owns tokens, arguments, defaults,
candidate/implemented status and acceptance references. Documentation and later
help generation derive from that registry. At this bootstrap, registry candidate
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

Future query/inspect/log/export and typed process actions share the model and
selector parser. Do not overload ambiguous positional words or execute an unknown
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
2 for invalid/conflicting/unavailable invocation, and a distinct documented
cancellation result where the host supports it. Partial results include explicit
status and may require a nonzero exit; each future command decides this in its
registry entry. Unknown options and malformed numeric/CSV selectors are errors,
not silently ignored. Numeric parsing checks signs, overflow and trailing bytes.

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
