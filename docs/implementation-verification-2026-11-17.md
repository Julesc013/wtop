# Wtop 0.4 development implementation and verification

Date: 2026-11-17. Product version: **0.4.0-dev.1**. Integration target: canonical
`master`; implementation review: [PR 3](https://github.com/Julesc013/wtop/pull/3).
This record describes candidate code and scoped evidence. It does not promote a
formal acceptance case, historical target, installer or stable release.

The [audit](wtop-release-readiness-2026-11-17.md) and
[design](implementation-plan-2026-11-17.md) remain attached to their earlier source
context. Their findings are not erased by this follow-up.

## Implemented boundaries

| Stream | Production implementation | Qualification still separate |
| --- | --- | --- |
| Measurements and actions | `metrics` exact split-word arithmetic, checked system commit, per-process QPC observation windows, named normalization; `actions` per-target outcomes and same-handle identity validation | Real multi-group/hotplug/clock behavior, all token/protected-process combinations, requested versus observed exit |
| Freshness and lifecycle | Collection start/observe/publish/read times; per-field stale status; one pending sample owned independently of the front buffer; no rendering or OS calls under the publication lock | Arbitrarily blocked baseline native calls, all close/host paths, independently measured resource ceilings |
| TUI and Setup | Shared action/label/enabled policy, live status, htop defaults and vi/Emacs alternatives, mouse fallback, tab CRUD, retained results, conservative scalar-safe renderer | Human visual/accessibility review, other console hosts, true wide/combining/emoji layout |
| Settings | Schema 2 stable IDs, explicit portable/user/machine discovery, exact byte baseline, cooperative zero-wait lock, checked replacement/recovery and explicit migration | Noncooperating writers, unusual filesystems/ACLs/reparse races, interruption and broader upgrade matrix |
| CLI | Checked catalog-generated help, preserved `-d`, explicit delay units, query/inspect/log, schema 2 typed system/process/provider output, readonly policy | Complete future CLI control/detail/thread/affinity scope, downstream compatibility consumers |
| Delivery | Final PE inspection, deterministic ZIP/exact-payload manifest, pinned MSI candidate and acquisition script | Atomic ownership during maintenance, upgrades/rollback, standard-user scope, signing/source/acquired-byte qualification and WinGet |
| Optional collection | Same-EXE isolated command-line, SCM service, account-name and PDH per-core providers with bounded wire/capacity/deadline/retirement state | Real slow domain/WMI behavior, legacy hosts, large service/CPU inventories and privilege permutations |
| Historical work | Shared C90 probe model, real DOS1 COM and DOS2+ MZ probe builds, NT probe, Win16/OS2 entrypoint sources | Actual old OS execution, toolchains/SDKs for remaining families, full collectors/renderers/actions/storage |

Portable modules remain C90 without Windows handles. Native ABI structures,
callbacks, console handles, providers and filesystem transactions stay in their
own adapters. `ntop.c` is still the inherited integration host; extraction is
incremental, not a claim that every responsibility has already been separated.

## Measurement and action semantics

System commit is system `CommitTotal * PageSize`, with overflow and zero-limit
handling, distinct from physical used/available memory. Process CPU subtracts
exact execution counters before conversion and uses QPC-bracketed observation
midpoints. Default one-core normalization permits values above 100%; machine
normalization uses the observed active logical-processor capacity. Raw timestamps,
acquisition width and unavailable/reset states are retained.

Snapshot schema 2 explicitly replaces the earlier `cpu_share` interpretation.
64-bit values are decimal strings. Invalid/stale values are null with status.
Unknown values sort after valid observations in both directions; ties use lifetime
identity. Name resolution has a bounded 32-entry cache with observed and attempted
times; stale names are marked separately from valid SID ownership. Unresolved
account-name filtering is a partial result, not proof of an empty match set.

Actions bind PID and creation time, revalidate using the same opened handle and
retain an individual result for every requested target. Termination accepted by
the kernel is reported as requested, not confirmed exit. Cancellation and targets
not attempted remain separate. Tags have a 4096-identity cap. Process samples have
a 65536-row cap with explicit completeness metadata. Tree construction uses an
indexed PID join, constant-time sibling append and iterative traversal; native
fixtures exercise a full-capacity star and reversed chain. Parent links require
strictly older known creation identities; unresolved links remain visible roots,
with status/count metadata, instead of inventing ancestry. Flat views skip tree
construction entirely.

## Bounded optional work and shutdown

Providers run a private protocol in another instance of the same executable.
The child is created suspended, placed in its owned job, and resumed only after
successful containment. An explicit handle allowlist, 1 MiB wire limit, one-process
job limit and 256 MiB job commit limit bound admitted work. Service/core arrays
are capped at 512 and command-line text at its declared protocol capacity.
Optional COM/WMI/PDH entrypoints are dynamically resolved; they do not become
mandatory loader dependencies.

The parent waits to a deadline, requests termination of its owned worker job when
necessary, and distinguishes response validation from confirmed retirement.
`response_received` cannot be claimed by the child. If retirement is unconfirmed,
the session retains ownership and closes new admission. No replacement-worker
storm or forced termination of a thread sharing the main heap is used.

PDH requires an effective collection interval of at least one second. Its own
observation time is retained. TUI per-core collection has a separate owned thread
and single pending result; it cannot delay baseline sample publication. The
noninteractive path collects optional per-core data before the baseline sample.
Unsupported/denied providers stop; consecutive failures back off and stop after
three attempts. Domain-name lookup is similarly isolated with limited cold
lookups per process sample.

Baseline kernel/console/filesystem calls do not all have a proven hard deadline.
Normal cleanup restores the caller's console, stops/joins owned work and releases
resources. A worker that does not retire within the grace period causes an
explicit own-process exit 124. This mitigation is not evidence that every native
call is cancellable or that an arbitrary external kill restores a console.
Cooperative CLI cancellation is exit 130; explicit partial/provider/capacity
results are exit 3. Runtime failures are 1 and usage/unavailable invocation is 2.

Logging is foreground and explicitly requested. It creates a new destination or
uses stdout; no service, autorun or updater is installed. Count/duration stop
conditions and a complete-record output byte cap are enforced. Known sampling
waits are shortened to remaining duration. One full bounded model record is staged
before the delivered-byte limit is checked: `--max-bytes` is not a total temporary
I/O or hard wall-time bound. Slow/broken output and uninterruptible native work
remain named boundaries rather than hidden unlimited guarantees.

## Settings and presentation

Schema 2 has monotonically allocated stable numeric tab IDs. Scoped unknown
records follow the original identity through reordering; removing a tab retains
its unknown data as inert comments. Future/malformed versions are rejected.
Migration happens only on an explicit save, preserves a recovery backup and
never silently overwrites the original legacy source. CLI export uses a separate
store context and does not redirect subsequent TUI saves.

The store reports resolved module, owner, read and write paths without writing.
Loaded exact bytes form the concurrency baseline. Save takes a zero-wait
cooperative lock, verifies the baseline, writes a checked temporary file, uses
replacement semantics and verifies the destination. Conflict, failure and recovery
outcomes are distinct. Noncooperating edits between verification and replacement
remain a filesystem race requiring wider qualification. Explicit exports print
recovery paths on success as well as failure. TUI saves report the outcome and
retained-path notice, writing full paths to the invoking stderr stream (visible
after leaving its alternate screen). Cleanup warnings remain visible even after
a verified save; dirty state remains set after failure.

Contextual keys/buttons share one enabled/action resolver. Input acts on the
currently displayed front model before a new pending sample is adopted; resize
invalidates hit geometry. Every native process mutation still revalidates lifetime.
The final-row bar is shown by default and hideable; the live status/error row
remains. Setup supports bounded tab creation, duplication, renaming, confirmed
deletion, visibility and ordering along with meters/columns/colors/keymaps.

The conservative renderer maps non-ASCII scalars to one `?` cell and reports
substitution, retaining raw Unicode in configuration and JSON. Scalar-safe
clipping/editor movement avoids split surrogate pairs. This intentionally limited
profile does not imply full terminal grapheme-width support. The minimum normal
layout is 40 by 10 cells; smaller buffers get a resize/quit fallback.

## Verification record

Initial implementation commit:
[`5f07aa83005a0215d469b7ae79b3d2983a58bd88`](https://github.com/Julesc013/wtop/commit/5f07aa83005a0215d469b7ae79b3d2983a58bd88).
Its first actual Windows run
[44062679851](https://github.com/Julesc013/wtop/actions/runs/44062679851)
passed the specification/portable-core and ARM64 build lanes, but failed x86/x64
native checks. The failures were retained and investigated:

- Provider handle accounting compared the first process launch with a cold
  runtime. An independent launch/retire preflight and per-iteration exact counts
  now distinguish one-time initialization from a repeated provider leak.
- A readonly-save test expected no siblings, contradicting the new recovery
  contract. The replacement assertion requires unchanged original bytes, complete
  loadable replacement data and disclosure of every owned recovery sibling.
- A pending process sample invalidated mouse controls before redraw. The input
  loop now handles the displayed view before adopting the next sample. The
  single-click acknowledgement and readonly refusal assertions remain strict.

Follow-up local checks have passed seven C90 core suites; focused Windows
x64 cross-compilation is useful build evidence but does not execute Windows APIs.
Native follow-up results must be recorded here before merging. Linux unittest
skips for native/optional emulator cases are not passes. Sanitizer scope covers
portable C policy, not Windows handle ownership or arbitrary native callbacks.

The native fixtures cover actual CLI output, isolated console input, settings
conflicts/recovery, provider protocol/retirement, finite logs and disposable
process actions. Candidate MSI fixtures run only on a disposable runner and
preserve logs. Their elevated token does not qualify an unelevated install.

Historical DOS instruction tests use scripted emulator interrupt services where
available; they are not tests inside licensed DOS/Windows/OS2 installations.
No ARM64-native, old Windows, CE, WinPE, RT, ReactOS or OS2 support is promoted.

## Release gate

No tag, release, WinGet listing or signed installer is created by this work.
Candidate promotion deliberately fails. MSI uses an ownership/hash preflight,
not atomic compare-and-delete/replace, and automatic upgrades are refused.
Closing these races, rollback/interruption, exact native platform/token/console
coverage, performance/resource evidence and release provenance remains necessary
for stable 0.4.0. The wider historical and advanced TUI/CLI scope remains required
for the agreed 1.0 goal; it is not declared complete by renaming probes as ports.
