---
type: "Wtop Specification"
title: "Configuration, themes and storage paths"
description: "Separate installed user preferences, portable data and executable identity without hidden writes."
resource: "urn:wtop:spec:wt-040"
tags: ["wtop", "configuration"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T12:38:25Z"}
wtop: {"id": "WT-040", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-030", "WT-020"], "requirements": ["WT-REQ-040-01", "WT-REQ-040-02", "WT-REQ-040-03", "WT-REQ-040-04"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}, {"id": "known-folders", "resource": "../references/sources.json#known-folders"}, {"id": "winpe", "resource": "../references/sources.json#winpe"}]
---

# Configuration, themes and storage paths

The current settings work extends the compatibility reader with an explicit
save/export operation and versioned presentation preferences. Normal startup/exit
stays read-only. Saving requires an explicit action and a disclosed destination;
no first-run migration, implicit writeback, registry operation or managed-install
claim follows from that action. Full installed/portable ownership detection and
migration remain planned.

Current read precedence is explicit --config PATH; otherwise an existing
executable-adjacent wtop.conf, legacy ntop.conf, then the roaming user's
Wtop/wtop.conf, then compiled defaults. This chooses one settings source rather
than implicitly merging files. Errors in an existing selected file are reported,
not bypassed by falling through to another profile. --no-config disables file
reads and the implicit TUI save destination; an explicitly named --write-config
PATH remains allowed and performs no process collection.

Explicit TUI save writes the selected --config path or adjacent wtop.conf; an
imported adjacent ntop.conf saves to sibling wtop.conf and leaves the legacy file
unchanged. With no adjacent file, the candidate resolves the roaming user folder
without creating it during lookup and saves to Wtop/wtop.conf only on request.
The selected user subdirectory may be created for explicit saving; an arbitrary
explicit destination requires an existing parent. An unavailable user-folder API
leaves in-memory operation usable and saving unavailable. Never fall back to a
different writable folder after a save failure. Expose the resolved destination.

The current NT adapter resolves roaming AppData through optional SHGetFolderPath
without CSIDL_FLAG_CREATE. This is a native implementation choice, not an old-OS
support claim or managed-install detection. Side-by-side instances still need a
qualified concurrent-edit policy: checking loaded bytes before replacement can
catch ordinary changes but is not a filesystem compare-and-swap transaction.

The eventual precedence is command-line override, explicit config path, explicit
portable root, installed user preference, system defaults, compiled defaults.
--no-config suppresses all file-derived settings. Never use the current working
directory as an implicit configuration search root: it may be an untrusted or
unrelated folder. Executable-relative paths are resolved from the actual module
path, not argv[0]. An explicit portable mode/marker establishes ownership; a
writable executable directory alone does not.

A future portable ZIP may carry an explicit wtop.portable marker; a bare
executable can be forced into portable mode with the planned --portable option.
An installed/package-managed executable is identified through a registered
installation owner record matched against its canonical path. Do not infer
ownership from a writable directory or a hard-coded WinGet default path.
An unregistered bare executable still runs without installation; absent an
explicit persistence choice it must not create machine integration or write
settings implicitly. This detection/write policy is planned, not a claim about
the current explicit-reader/save candidate.

Portable settings live under the chosen portable root, alongside the application
or a named data subdirectory. A read-only portable folder remains usable with
defaults and an explicit diagnostic; do not silently fall back to an installed
profile or write elsewhere. Installed modern Windows preferences/themes/keymaps
belong under the user's roaming application-data Wtop directory, while caches,
history/logs and machine-specific state belong under local application data.
System defaults belong under the machine application-data location, remain
read-only to ordinary users and never replace individual preferences.[^known-folders]
The planned modern paths are %APPDATA%\Wtop\wtop.conf for roaming
preferences, %APPDATA%\Wtop\themes and keymaps for user customization,
%LOCALAPPDATA%\Wtop for machine-specific cache/history, and
%PROGRAMDATA%\Wtop for system defaults. Use known-folder APIs, not literal
environment-string concatenation; the strings describe roles, not fixed paths.

An all-users program install still uses each user's own settings. It does not
write mutable preferences under Program Files. Legacy profiles use an explicitly
qualified adapter and distinguish real per-user isolation from mere directory
convention. DOS/early single-user systems may expose only a selected configuration
directory. WinPE uses an explicit writable external path or in-memory settings.
Package-manager payload directories are not durable user-data locations. A
manager may replace files or remove an extracted package directory during update
or uninstall; installed preferences therefore stay under the separate user root.

Use a small versioned declarative file with stable keys, bounded line/value/file
lengths and comments. Unknown safe keys survive supported edits. A malformed file
does not trigger partial writes or a shell action. Migration preserves the original, records schema versions and changes only owned files.
The initial compatibility reader does not silently migrate legacy input. Saving uses a
same-directory temporary file, checked flush and replacement where the target
supports it; degraded durability on old filesystems is disclosed.

The initial presentation model contains built-in theme selection with color-role
overrides, htop/vi/Emacs keymap selection, mouse and function-bar preferences,
ordered visible header rows, and bounded named tabs with ordered stable column
IDs, sort direction and tree preference. Main and I/O are initial real views.
Reject duplicate columns/tabs, invalid IDs, unsupported versions and oversized
values before applying a partial configuration. Preserve unknown inert records
within declared limits; syntax outside those limits fails explicitly.

Themes contain color roles and optional glyph choices. Keymaps name command IDs;
column profiles name metric IDs. Paths and overrides have one resolver shared by
CLI/TUI/install code. Report the resolved path and source on demand without
leaking unrelated environment or credentials. Logs are opt-in and bounded.

[^known-folders]: Microsoft's modern folder roles; older APIs require a target adapter.

## Requirements and acceptance

### WT-REQ-040-01

Configuration loading and ordinary startup/exit MUST remain read-only and obey explicit-path/no-config/executable-adjacent/user-fallback precedence without searching arbitrary current directories or silently bypassing an invalid selected file.

Acceptance: WT-AC-040-01 — Run from a different working directory with explicit, adjacent, legacy and user sentinel configs, denied/malformed paths and --no-config; verify read source, precedence and zero writes.

### WT-REQ-040-02

Future portable/per-user/system settings MUST follow explicit ownership and MUST not silently redirect or overwrite another installation's data.

Acceptance: WT-AC-040-02 — Read-only portable, multi-user and side-by-side install fixtures preserve sentinels and disclose inability to save.

### WT-REQ-040-03

Config/theme/keymap parsing and migration MUST be bounded, inert and versioned, preserving unknown safe settings during supported edits.

Acceptance: WT-AC-040-03 — Malformed, oversized, unknown-key and interrupted-save fixtures cannot execute content or destroy the original.

### WT-REQ-040-04

Settings writes MUST require an explicit save/export action, disclose the destination, and preserve the previous file on failure without implicit machine integration.

Acceptance: WT-AC-040-04 — Exercise explicit/legacy/user destinations, unavailable folder lookup, denied/read-only/missing parent, external changes, existing-file preservation, temporary cleanup and restart round-trip; ordinary startup/exit makes no settings write.
