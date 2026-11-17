# Wtop 0.4.0-beta.1 — modern Windows x64 portable preview

**Experimental prerelease, not stable 0.4.0 and not full htop parity.**
When this page is attached to a **draft**, it is not a public release or proof that
human/desktop qualification has completed. `native-receipt.json` identifies the
actual automated test host; `desktop-receipt.json` is required before publication.

## What is included

A native htop-style process table, htop/vi/Emacs keymaps, mouse fallback, contextual
controls and live freshness, configurable meters/columns/tabs/colors, explicit
settings saves and version-2 migration. System commit and measured process CPU
have defined semantics. PID-plus-creation identity protects process actions and
per-target outcomes survive partial failure.

The CLI provides typed system/process snapshots, query/inspect, opt-in command
line/service/per-core providers and finite foreground JSONL logging. No service,
startup entry, updater or installer is created during ordinary operation.

This beta hardens transactional metric calculation, validates complete action
batches before dispatch, and corrects AGE sorting to use its own observation time.
Native terminal fixtures now cover additional small/resized views. An explicit
desktop review procedure and privacy-aware support reporter accompany the source.

## Download and verify

Choose the **win-nt-x64 portable ZIP**. It contains `wtop.exe`, LICENSE,
`payload.json`, portable instructions and an inert example configuration. The
standalone EXE is byte-identical. The full corresponding source archive contains
the source, build scripts, tests and license at the exact recorded Git tree.
`SHA256SUMS` and `beta-manifest.json` bind the assets. Compare hashes against this
trusted release, not against a manifest received from an untrusted third party.

The candidate is unsigned unless the attached native receipt explicitly records
`Valid`. A `NotSigned` receipt means unsigned: verify provenance and follow your
organization's policy. No instruction here asks you to disable security controls.

```powershell
.\wtop.exe --readonly --no-config
.\wtop.exe --paths --format json
.\wtop.exe query system --format json
.\wtop.exe --portable
```

Settings save only when explicitly requested; ordinary exit does not save. Portable
mode uses its disclosed destination, not the current working directory. No PATH
or registry installation is performed by these commands.

## Compatibility and known limits

Only the x64 portable beta is distributed here. Read exact host/token/terminal
observations in the receipts. Windows 11 client testing does not certify every
Windows build. x86, ARM64, Windows RT, WinPE, ReactOS, legacy Windows, DOS, CE and
OS/2 are not qualified by this release. There is no published Wtop WinGet package,
MSI installer, native GUI, background daemon, or automatic upgrade mechanism.

Unicode text is preserved in JSON/configuration, but the conservative TUI displays
unsupported non-ASCII scalars as one-cell placeholders. Thread rows, affinity,
collapsible subtrees, pause, advanced handles/backtraces, GPU/sensors and full
htop feature parity remain incomplete. Process I/O is not physical disk bandwidth.
CPU is one logical CPU = 100% by default; `--cpu-normalization machine` is explicit.
Optional providers can be denied/unavailable/stale. Some baseline OS and output
calls cannot be guaranteed hard-deadline cancellable. Do not treat this beta as
an unrestricted leak-free, crash-free or all-platform guarantee.

## Reports and privacy

Use the repository's issue templates once public feedback is enabled. Include the
beta version, executable hash, Windows build, terminal, reproduction and observed
result. Do not post credentials, full process snapshots, command arguments,
environment values, usernames or screenshots containing private information.
`tools/support_report.py` in the corresponding source creates an opt-in allowlisted
build/host report; it never uploads. Read `docs/beta-testing.md` before testing.

NTop ancestry and GPL-3.0-only notices are retained. The source and release records
separate inherited provenance, implemented changes and qualification limits.
