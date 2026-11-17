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
job limit and 256 MiB per-process commit limit bound admitted work. Service/core arrays
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
The TUI per-core loop stops on unsupported/denied results; consecutive failures
back off and stop after three attempts. This policy is separate from explicit
CLI provider requests. Domain-name lookup is similarly isolated with limited
cold lookups per process sample.

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

- Provider handle accounting compared the first process launch and pipe use with
  a cold runtime. Independent process and pipe primitive probes now establish the
  baseline before any provider is invoked; every provider iteration must retain
  that exact count. The initial stderr-initialization hypothesis was disproved.
- A readonly-save test expected no siblings, contradicting the new recovery
  contract. The replacement assertion requires unchanged original bytes, complete
  loadable replacement data and disclosure of every owned recovery sibling.
- A pending process sample invalidated mouse controls before redraw. The input
  loop now handles the displayed view before adopting the next sample. The
  single-click acknowledgement and readonly refusal assertions remain strict.

Further failures were retained rather than bypassed. Windows path comparisons
now resolve existing 8.3 aliases while preserving absent destination components.
The new mouse fixture waits for the initial complete footer before issuing its
single click; it never retries a lost click. Native pipe diagnostics identified
one additional File handle on the first independent anonymous-pipe call, distinct
from both returned endpoints, with both endpoints verified closed. Five later
primitive cycles and five provider invocations each retain an exact count of 75
on the recorded x64 host. This supports attribution to first-use behavior outside
the provider; the specific internal Windows/runtime owner is not established.
No handle-count tolerance or provider warmup was added. The candidate MSI build
then exposed a duplicate WiX-generated scope property; the duplicate was removed
and machine fixtures explicitly request `ALLUSERS=1`.

### Passing source and host

[Run 44069558700](https://github.com/Julesc013/wtop/actions/runs/44069558700)
completed successfully on 2026-11-17 at 10:59:04 UTC, with all four jobs green.
The PR head is
[`8733cf278227e5cbefef9b8f078bc66c0c55e298`](https://github.com/Julesc013/wtop/commit/8733cf278227e5cbefef9b8f078bc66c0c55e298);
Actions checked out synthetic merge `d981abff2a03e07819def21d64432e30a5e74489`.
Both have source tree `401dfa272dcea0c7b029683f872c84ad6a044320`.
The [machine-readable observation](evidence/implementation-2026-11-17.json)
retains exact artifact IDs/digests, provenance, native receipts and limits. It is
an informative observation, not a second acceptance registry.

The Windows execution host was Server 2025, kernel `10.0.26100`, x64, runner
image `20261103.298.1`; the compiler was MSVC `19.51.36260.0` through the Visual
Studio 18 2026 generator. The recorded provenance does not establish the exact
Windows SDK revision; that field remains explicitly unverified.

| Lane | Observed result | Scope limit |
| --- | --- | --- |
| Linux specification/core | Specification/help checks passed; 69 Python cases discovered, 36 passed and 33 skipped; seven C90 CTest suites passed with ASan/UBSan | Skips are not passes; sanitizers cover portable policy, not native handles/callbacks |
| Windows x86 | All 18 CTest entries passed, including 25 CLI cases, six provider cases and 13 grouped TUI scenarios | x86 execution on this x64 Windows host; no old Windows support implied |
| Windows x64 | All 18 CTest entries passed with the same nested CLI/provider/TUI scope | One OS/token/console-host environment, not a performance or support matrix |
| Windows ARM64 | Application/tests built and candidate PE/ZIP verification passed | No ARM64-native execution |
| x64 MSI | Build, user and machine install, exact payload, collision/refusal, unchanged removal and preservation checks passed | Elevated runner; automatic upgrades deliberately refused; races/rollback remain open |

The native entries include settings conflict/recovery, provider protocol and
retirement, finite log delivery, tree capacity and disposable process actions.
The nested Python cases are included in the 18 CTest entries, not additional
independent full suites. TUI receipts explicitly record
`human_visual_acceptance: false`.

Both x86 and x64 provider receipts report actual valid command-line observations,
four observed PDH cores and 259 SCM service records, with a received response and
confirmed worker retirement. The zero-service result for the disposable inspected
process is separate from the nonempty system inventory. These observed successes
are stronger than the typed-degradation cases alone, which may also pass when a
provider is unavailable.

### Exact candidate payloads

All five downloaded Actions artifact archives were checked against their reported
SHA-256 digest. `tools/package_candidate.py verify` passed independently on each
downloaded candidate directory. The native x86/x64 provider receipts match the
downloaded executable hashes; both MSI scope receipts match the same x64 payload.

| Profile | EXE bytes | EXE SHA-256 |
| --- | ---: | --- |
| `win-nt-x86` | 305152 | `f278fe3989c56f74e8a86a2ec1a5d9b789e765682ae0be1d2cf775d93a13daa7` |
| `win-nt-x64` | 355328 | `29e718870ce80ac1cb74ed38985c2eacc6b9885d53efaa2f882359dce3557a61` |
| `win-nt-arm64` | 358400 | `6809bb996e23c57f544ab53fbc5468ae24eda782f7e6f737565968981d501b09` |

The x64 candidate MSI is 319488 bytes, SHA-256
`f7ce6bf948bc923b34a21bb40c8af032363ba97a87ccb77647380e4d6f456225`.
It contains the x64 payload above. These are hashes for this run, not promises of
byte-for-byte reproducibility on a later rebuild or evidence of signing.

Candidate MSI fixtures preserve their raw logs. User and machine scope checks
both preserved foreign files/registry values and user settings, and refused
modified-payload repair/removal and automatic upgrades. The receipt keeps
`qualified: false`: preflight-to-operation races, standard-user UAC, interruption,
rollback, other hosts and the upgrade implementation remain unresolved.

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
