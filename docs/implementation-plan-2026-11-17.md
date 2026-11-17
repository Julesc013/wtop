# Wtop implementation design: corrected core through qualified backends

**Date:** 17 November 2026. **Status:** proposed implementation design, not implemented behavior or acceptance evidence.

**Source reviewed:** canonical `master` at
[`1ca153b77e7df203b9f6d1f8cfb2ff472318bc27`](https://github.com/Julesc013/wtop/tree/1ca153b77e7df203b9f6d1f8cfb2ff472318bc27),
confirmed against the remote during this review. Application version remains
`0.1.0-dev.2`. Existing uncommitted audit documents remain separate observations.
No application code, public schema, version, work status or support claim changes
merely because this design exists.

This document translates the eight requested workstreams into concrete module
boundaries, compatibility choices, independent test cases and integration steps.
It is an informative companion to the [OKF specification](../spec/index.md),
[work catalog](../spec/work/units.json), [TODO](../TODO.md), [roadmap](../ROADMAP.md),
[release audit](wtop-release-readiness-2026-11-17.md) and
[fork audit](ntop-fork-audit-2026-11-17.md). Those catalogs retain authority over
requirements, commands, targets, acceptance and execution status. Suggested PRs
below subdivide existing work; they do not establish a second work queue.

## 1. Recommended decisions

1. Preserve the current TUI, portable parser/settings/keymaps and native
   lifetime-revalidation behavior. Correct observable defects first, then extract
   boundaries required by those fixes. Do not restart the application.
2. Use one typed snapshot and action-result model for TUI, text/JSON and future
   inspection/logging. Raw exact counters and their provenance are authoritative;
   renderers never own measurement formulas.
3. Keep portable policy in the documented C90 profile. Native handles, structures,
   callbacks, calling conventions and synchronization remain in owning adapters.
   Syntax portability alone does not qualify a segmented-memory compiler.
4. Use one state owner and bounded immutable sample transfer. Do not introduce a
   general event bus, lock-free framework, mandatory plugin ABI or worker per PID.
5. Keep collection, publication and repaint times separate. Optional slow metadata
   must not prevent basic identity/CPU/memory observations.
6. Preserve existing `-d` snapshot and `-n` name-filter aliases in 0.4.0. Add
   descriptive nonconflicting long aliases; explicitly document the htop short
   option exceptions. Keymap choice never changes command-line grammar.
7. Follow the existing metric specification's one-core process CPU semantics and
   offer explicit machine-capacity normalization. Treat the changed default from
   the current candidate as a documented compatibility change, with an explicit
   output-schema transition before release. Never preserve incorrect arithmetic
   to simulate compatibility.
8. Introduce stable tab identity before completing tab CRUD. Stored order/name
   are properties of a view, not its identity. Preserve scoped unknown fields.
9. Keep explicit configuration saves and readable files. Specify cooperative
   writer serialization, external-edit conflicts and recovery separately; do not
   promise filesystem compare-and-swap or universal power-loss atomicity.
10. Prefer portable EXE/ZIP and one dual-purpose modern MSI per qualified profile,
    with WinGet reusing that installer. Gate MSI on the existing preservation
    contract; ordinary MSI removal does not itself implement hash-based retention.
    Defer independent self-install/EXE-bootstrap engines until a concrete need.
11. One executable per profile can optionally invoke itself as an owned isolated
    provider. One distributed binary does not require every operation to share
    one address space. Isolation remains capability- and lifecycle-qualified.
12. Run a small real legacy compile/loader proof early. Add full backends through
    independent capability slices without delaying unrelated modern corrections.

These are recommendations to incorporate into the relevant canonical contract
when its implementation starts. They do not silently change current behavior.

## 2. Reuse and responsibility boundaries

Current useful foundations include `core.c/.h`, `settings.c/.h`, `ui.c/.h`,
identity-bound actions, the bounded native input editor and console test harness.
The main integration problem is `ntop.c`, which still owns many responsibilities.

| Responsibility | Reuse now | Extract only as the work needs it |
| --- | --- | --- |
| CLI parsing and dispatch policy | `core.c/.h`, existing parser tests | Keep option parsing here initially; split output from parsing once the typed model exists |
| Exact values and measurements | Existing counter/rate code and metric specifications | `metrics.c/.h`, small target-width helpers and `model.h`; no native headers |
| Process observation | `PollProcessList`, `PollSystemInfo`, useful Win32 access/error handling | An NT collection adapter; raw observations only, with explicit access states |
| Actions | Current PID/creation checks in `KillIdentity`, `ChangePriority` | Portable request/result types and a native executor; no UI messages inside executor |
| Snapshot ownership/scheduling | Private sample construction and stop events | Small session owner and bounded publication slot; UI owns its view indexes |
| Presentation | `ui.c/.h`, three keymaps, existing layout/formatting | Context/action descriptors, text-cell policy, view state and Setup operations |
| Settings document | `settings.c/.h`, parsing, validation, inert unknown records | Separate storage/path adapter; stable view IDs and versioned migration |
| Console/file/process host services | Native code in `ntop.c` and `util.c` | Cohesive NT host modules; target-specific implementations stay explicit |
| CLI projections | `WriteSnapshot`, JSON string escaping | Serializer consumes the same immutable typed snapshot as TUI |
| Historical targets | Portable semantics and fixtures | Family/ABI host/provider files introduced only with a real proof |

Initially add a few responsibility-sized files under `source/`; avoid a mass
rename during a behavioral correction. Family directories become useful when
there are real second implementations. Do not create one copied source tree per
Windows marketing release or a large collection of empty backend folders.

The dependency direction is host/provider acquisition -> raw observation ->
shared measurement/snapshot -> command/view projection -> renderer or serializer.
Actions have a parallel request -> identity validation -> native operation ->
typed result path. Installation does not participate in ordinary sampling.

### Ownership rules in code

Public interfaces document owner, borrow duration, mutation, capacity, failure,
thread/reentrancy and cleanup. Prefer a small request/result object and an opaque
native context over pointers into the live UI model. A caller-owned buffer reports
needed/used length and incomplete state rather than silently truncating.

Use one cleanup path where several native resources are acquired; initialize
ownership explicitly and capture errors before cleanup changes native error state.
Do not cast incompatible callback functions, retain stack context asynchronously,
return borrowed pointers into a replaced snapshot, or use a timeout as evidence
that an owner can be destroyed.

For 16-bit work, verify `int`, `long`, `size_t`, pointer and allocation bounds on
the actual compiler. Keep exact counter operations behind a small verified width
interface. A native 64-bit representation and a split-word representation should
pass identical arithmetic vectors; do not put Windows FILETIME in the common ABI.
Avoid requiring one allocation larger than the profile's addressable object size.
Use bounded chunks where needed and report capacity limits. Wire format never
copies the native struct layout.

## 3. Measurements and individual action outcomes

**Existing work:** WT-W020 and WT-W030. **Contracts:** WT-010/011/012/061.

### Observation contract

Each rate input retains its exact raw counter, unit, provider/source epoch,
entity lifetime, clock domain/frequency, acquisition-before/after timestamps,
measurement state and topology/capacity scope. Each derived value records its
interval and normalization. A failed read does not become zero or erase otherwise
observable process presence.

A conceptual interface is:

```c
/* Illustrative interface; target-width types must be supplied and verified. */
wtop_result wtop_process_cpu(const wtop_cpu_sample *previous,
                             const wtop_cpu_sample *current,
                             const wtop_capacity *capacity,
                             wtop_cpu_value *result);
wtop_result wtop_system_commit(const wtop_commit_sample *sample,
                               wtop_commit_value *result);
wtop_result wtop_execute_action(wtop_native_actions *owner,
                                const wtop_action_request *request,
                                wtop_action_result *result);
```

These are design sketches, not compiled headers or a stable plugin ABI.

### System commit

For the modern NT provider, use `GetPerformanceInfo`: system committed bytes are
`CommitTotal * PageSize`, and current commit-limit bytes are
`CommitLimit * PageSize`, using checked exact multiplication. Keep physical-memory
availability independent. If this API is unavailable, report that capability or
use a separately documented provider with equivalent semantics; do not silently
substitute process-constrained `MEMORYSTATUSEX` values. [S1]

### Process and host CPU

Compute process one-core percentage from the process's own observations:

`100 * delta(kernel_time + user_time) / elapsed_monotonic_time`

Both quantities must use compatible units. Subtract exact accumulated counters
before converting the bounded delta for ratio calculation. One-core values may
legitimately exceed 100. Machine-capacity percentage divides by the declared
active logical-CPU capacity over the same interval. Initially invalidate the
comparison on topology/capacity change; integrating changing capacity can be a
later explicit feature. Do not confuse this with affinity-relative or
frequency-weighted utilization. [S2, S3]

Bracket the native read with a monotonic clock. A defined midpoint is an estimate
of the observation time; retain the acquisition span as uncertainty. A delayed
read must not be presented as perfectly simultaneous. Use one clock domain per
comparison. Reset on invalid ordering, source/clock epoch change, counter decrease,
lifetime change or an explicitly disallowed gap. Do not multiply large absolute
tick counts before subtraction. [S3]

Host accounted CPU is a separate metric:

`busy = delta(kernel) + delta(user) - delta(idle)`

`total = delta(kernel) + delta(user)`

`GetSystemTimes` kernel already includes idle. Validate component deltas and
scope before division. Above 64 logical processors the API covers the caller's
primary processor group, so an all-machine CPU count cannot repair its sampling
scope. Report the group scope or use a separately qualified aggregate. [S4]

Retain a persistent previous-sample map keyed by process lifetime once snapshot
ownership is extracted. Sample once per cadence instead of doing a fresh
read/sleep/read sequence on every update. New entities show warming rates. A
one-shot snapshot can deliberately acquire two observations under the same
collector rather than implementing a second formula.

### Additional process-age correction

Current `PollProcessList` uses nonzero `lpExitTime` as evidence that a process
exited. Microsoft defines that output as undefined for a running process. Use
observed current time for a live process; only interpret exit time after an
independent established exit state. This new source/API finding is separate
from the earlier fourteen-item audit and is not a native reproduction. [S2]

### Action results

Keep the existing PID/creation identity check and operate through the same opened
handle used to validate identity. An old selection may not target a new lifetime.
Capture a bounded list of intended identities and reserve result capacity before
starting a batch. Return one result per target, in request order.

Results distinguish applied, requested, confirmed-exited, already-exited,
identity-changed, identity-unavailable, denied, unsupported, safe-limit,
not-attempted/cancelled, failed and indeterminate completion where relevant.
Native error, action, requested priority, previous priority when observed, target
identity and timing belong in the result. The UI renders summary/details; CLI
serializes the same result set. A later success cannot overwrite an earlier error.
Cancellation before dispatch can mean not attempted. Cancellation or timeout
after dispatch retains requested/indeterminate state unless completion is observed;
it must not imply that a native mutation could not have happened.

A successful `TerminateProcess` on another process initiates asynchronous
termination. Record requested; record confirmed-exited only after observing the
process handle signalled. Waiting requires appropriate rights and an explicit
budget; those extra rights need not block a termination request itself. Do not
automatically retry an ambiguous mutation. Explicitly route an action on Wtop
itself through its local shutdown policy so a batch does not destroy its own
unreported results. Preserve the existing exclusion of REALTIME priority. [S5]

### Independent regression cases

- 3 GiB / 8 GiB system commit is 37.5% regardless of a process-specific allowance;
  multiplication overflow, zero limit and provider failure remain explicit.
- Process observations 170 ms apart with 170 ms CPU yield 100% one-core, or 50%
  of two-CPU capacity. An unrelated 200 ms host window must not yield 42.5%.
- Large absolute counters above 2^53 with a small exact delta; 0%, 100%, 200%;
  first sample, reset, denied read, topology change, zero time and counter wrap.
- A live process with arbitrary nonzero exit-time output does not acquire a false
  exit time or age.
- Mixed successful, denied, exited and reused-PID actions preserve every result;
  failed result allocation prevents an invisible partial batch.
- A successful asynchronous termination remains requested until exit observation.

Use a separate arbitrary-precision reference calculation to generate fixed
vectors, then run those vectors against both native and split-word arithmetic.
These are proposed cases, not tests executed by this design review.

## 4. Freshness, bounded work and shutdown

**Existing work:** WT-W030. **Contracts:** WT-011/012/061.

### Immutable sample transfer

Use one collector owner and one bounded publication slot first. The collector
builds privately, transfers a complete immutable sample under a short lock, and
never mutates it after transfer. The UI takes ownership, builds view indexes and
renders outside that lock. A superseded unconsumed publication can be coalesced
with explicit accounting and correct destruction; never queue samples without a
limit. One consumer does not need a general reference-counted object graph.

When a logger becomes a real second consumer, specify a bounded lease or copy
policy before sharing samples. Rendering, saving, native account lookup and
process actions never execute while holding the publication lock. UI selection,
filter/sort indexes, overlays and hit regions are UI-owned.

### Freshness is part of the data

Record collection start/end, per-field observation time, publication time,
sequence, source epoch, completeness, last success and provider outcome.
Publication is not the measurement time: a slow acquisition can publish old data.
Repainting cannot reset age. An owner-name update cannot refresh a CPU observation.

Use monotonic age with an explicit provider freshness policy. Keep a current
unavailable/stale state and optional retained last-good value/time distinct.
The display can dim retained values and show age; machine output must not present
them as a valid current zero or valid fresh value. Successful empty enumeration
is a fresh empty set, not a collector error.

Decouple collection cadence from input/paint cadence. At most one baseline sample
is in flight; coalesce missed ticks instead of launching catch-up workers. Report
skipped deadlines and overruns. Set profile/provider budgets from measured needs;
this document assigns no universal latency or memory threshold.

### Remove blocking enrichment from the basic path

`LookupAccountSid` can involve domain-controller queries. Obtain stable owner
identity separately and resolve names through a bounded SID-keyed cache with
positive/negative expiry. Unknown owner is not SYSTEM. Denied optional enrichment
must not suppress basic process records. WMI, command lines and services follow
separate on-demand or slower collection policies. [S6]

Each provider has explicit maximum rows, bytes, string size, in-flight requests,
queue slots, retry policy and retirement behavior. Keep APIs compiled-in with a
small typed open/sample/stop/completion/close interface; introduce no dynamic
plugin platform merely for optional columns.

### Shutdown state machine

The intended states are running -> stop requested -> admission closed -> work
retiring -> terminal restored/resources released -> exited. Preserve the distinct
emergency-exit path until a replacement is qualified; never relabel it graceful.
Terminal restoration and provider destruction have different prerequisites:
once UI writes stop, restore owned console state promptly where safe, independently
of a stalled collector. Keep provider context, callbacks and synchronization
alive until their actual users retire; restoration does not prove retirement.

A cancellation request does not prove native work stopped. `CancelSynchronousIo`
does not wait for completion and is not a universal cancellation mechanism.
Timeout cannot authorize freeing live context or creating endless replacement
workers. Routine `TerminateThread` is excluded. [S7]

For an optional uncooperative provider, an owned child invocation of the same
`wtop.exe` can establish a separate address-space boundary on qualified targets.
Use a private versioned bounded protocol, inherited handles restricted to the
intended channels, absolute executable identity, no shell invocation, no higher
privilege, and explicit parent/child completion. Do not inherit the interactive
console handles. Qualify job/parent-death behavior per OS; if ownership cannot be
established, disable that optional provider instead of creating an unmanaged one.

A termination request is still not proof of immediate kernel cleanup. Bound the
parent's response and refusal to admit more work; qualify actual child retirement.
Blocking console output and filesystems are separate failure cases. Do not promise
universal restoration after power loss, host kill or an uncooperative kernel call.

Native control callbacks use the exact ABI, stable lifetime context and minimal
signalling. They do not render, save, acquire the publication lock or free the
model. Document callback thread, reentrancy and retirement. Registration removal
alone is not a proof that an already-running callback has returned. Win16/CE hosts
may use cooperative pumps instead of forcing modern threads onto them. [S8]

### Tests

Use a virtual clock and scripted provider states for missed ticks, slow collection,
partial samples, failure/recovery, interval changes, restarts and cancellation.
Native fault cases then cover blocked owner resolution/output/save, close/Ctrl+C,
callback races, process churn, child failure, repeated launch/exit, and multiple
instances. Count owned handles/allocations to detect trends with identical setup;
RSS alone is not proof of a leak or its absence. A timeout fixture must not pass
by silently killing the test subject before required restoration is observed.

## 5. Contextual UI, text cells and Setup

**Existing work:** WT-W040/050. **Contracts:** WT-031/040.

### One action definition per context

Extend `ui.c` with a context model for process view, Setup, search/filter editor,
sort chooser, action review and help. An action descriptor supplies action ID,
label, enabled state/reason and applicable bindings. Keyboard, mouse and the
function bar call the same action dispatcher. Do not maintain a second mouse
implementation of process actions.

The default last row remains a visible control bar. Add compact adjacent status
for active view/filter/sort, dirty settings, sample age and degraded providers.
Critical prompts/errors remain accessible when the bar is hidden. On narrow or
short screens condense lower-priority labels, reduce optional meters and protect
the process selection/prompt area; never mutate the saved layout just to fit one
window. Tiny unsupported dimensions receive a usable diagnostic and exit/recovery
keys instead of invalid coordinates.

Contextual labels must describe the operation actually dispatched. Define F10 and
Escape behavior per context deliberately; preserve tested default process-view
quit behavior and common recovery routes. Reuse current htop/vi/Emacs mappings;
full arbitrary rebinding can remain an explicitly bounded later feature.
Opening a destructive confirmation never confirms it through key auto-repeat:
F9 opens review, while a distinct affirmative action confirms captured identities.

### Unicode and hit testing

Use UTF-8 for portable strings with checked native decoding at the boundary.
Treat byte length, Unicode scalar values, grapheme clusters and terminal cells as
different concepts. Never cut a UTF-16 surrogate pair or UTF-8 sequence. Apply one
text-measure/clip policy to render and hit-test geometry.
An unpaired native surrogate or invalid byte sequence needs an explicit encoding
error/loss status and safe display representation. If original native units are
retained for inspection, keep their encoding tagged and bounded; do not emit
invalid UTF-8 or claim a replacement character preserved the original text.

A small cell-run interface should expose text span, occupied cells, role and any
continuation cells. Pin Unicode data/version and licensing if importing tables or
a small library. Grapheme boundaries and East Asian Width are useful inputs, but
Unicode explicitly requires terminal-specific width tailoring. There is no
universal `strlen` or `wcwidth` solution for every console/font/emoji sequence. [S9]

Provide a conservative substitution/ASCII profile for unqualified hosts, explicit
ambiguous-width policy where useful, and a tested richer profile. Preserve original
text in the model. Escape or visibly substitute terminal controls from process
names, arguments and configuration. Human readability needs actual host review.

Layout returns cell rectangles for text, rows, headers, tabs and actions; mouse
uses those same rectangles. Resize discards old geometry before the next event is
dispatched. Do not trust a stale screen row as a process identity; carry frame and
process lifetime identity through selection/action confirmation.

### Complete the bounded Setup scope

For 0.4.0, recommend completing create/duplicate/rename/delete/reorder/hide of the
existing maximum eight views, existing meter/column ordering, sort/tree/filter,
colors, keymap, mouse and bar settings. Keep at least one visible view and one
process column. Duplicate creates a new identity. Delete confirmation identifies
the exact view; active-view fallback is deterministic. Bounds remain visible.

Make stable view IDs independent of names and array order before this work.
Current unknown `TabN.*` records are preserved by index while known tab structures
are reordered, which can attach an unknown field to a different view. A versioned
migration must preserve scope, not just unknown bytes. This is a new static
observation, not a native reproduction. If an unknown legacy construct cannot be
safely rebound, retain the original and refuse destructive rewriting rather than
claim semantic preservation.

Keep preview changes explicit and disk persistence separate. A failed save leaves
the in-memory edit and dirty marker intact. Document cancel/close behavior for
each editor; do not unexpectedly revert unrelated earlier changes. Manual column
widths and arbitrary key rebinding need separate requirements if added later.

### Tests

Drive each context through every keymap and mouse path; assert enabled actions
match labels. Cover hidden-bar recovery, clicks on stale frames, resizing between
input events, empty lists, changing identities, very small and ordinary screens.
Text fixtures include CJK, combining marks, surrogate pairs, emoji sequences,
malformed encodings and controls. Test tab edits with unknown scoped settings,
max counts, duplicate names/IDs, deletion of active/last-visible views, failed saves
and round trips. Reuse the existing owned-console native harness; portable cell
tests alone do not qualify a terminal host.
Include held/repeated F9 input: opening the review must never confirm termination.

## 6. Settings ownership, concurrency and migrations

**Existing work:** WT-W050/060. **Contracts:** WT-040/050/061.

Separate three things: effective settings values, the editable document (including
inert unknown records), and the native store/owner. The store returns canonical
path, selected ownership mode, writability, loaded revision, metadata policy and
save outcome. The model does not guess paths from file writability.

### Paths and precedence

Retain current behavior until an explicit migration. The proposed managed policy
uses explicit `--config`, explicit portable root/marker, installed user settings,
optional machine defaults and compiled defaults, with command-line overrides.
For 0.4.0, recommend one selected document applied over compiled defaults, followed
by CLI overrides. Machine defaults are selected only when no higher-priority
document exists; they are not implicitly merged with a user document. Saving a
session based on machine defaults writes an explicitly disclosed per-user copy,
never the machine source. Multi-document layering is separate future behavior
requiring provenance per setting. `--no-config` suppresses all file sources.

An installed application uses resolved known folders: roaming Wtop preferences,
local Wtop cache/logs and machine Wtop defaults read-only to ordinary users.
Portable data uses an explicit selected root. The current working directory is
never an implicit search root. Match actual installation registration to module
path; a WinGet directory name or a writable executable folder does not establish
ownership. Unregistered bare EXEs keep functioning without registration. [S10]

Expose resolved paths and owner through diagnostics before writing. A read-only
portable destination remains read-only; do not silently write into another user's
profile. WinPE can use an explicit external writable path or in-memory settings.
Older profiles need explicit path adapters and may not provide real per-user
isolation. Era-compatible filenames may differ while schema meaning is shared.

### Cooperative save transaction

1. Serialize and validate a complete candidate in memory within existing limits.
2. Acquire a bounded per-destination cooperative lock; re-read the current file
   under that lock and compare its revision with the loaded document.
3. If another writer changed it, preserve both states and report conflict. Offer
   reload or explicit export; do not silently last-writer-win or auto-merge unknown
   fields.
4. Create an exclusive same-directory temporary file; check write/flush/close.
5. Use the target-qualified replacement primitive and backup/recovery policy.
   Preserve required ACLs/metadata or fail visibly. Creation of a previously
   absent destination must also detect a concurrent creation.
6. Interpret replacement error states, identify surviving original/new/backup
   paths, and recover or report a precise recoverable state. Do not delete the
   only recoverable copy during generic cleanup.
7. Re-read and verify that installed content matches the exact serialized candidate
   before releasing the lock. A different external edit is a conflict/uncertain
   result, not a new baseline to adopt without parsing/applying it. Retain the
   committed candidate's revision. Clear dirty state only if the live settings
   revision still equals the saved revision; edits made during a save stay dirty.

Return distinct unchanged-destination failure, committed-but-unverified and
recovery-required outcomes where applicable. A generic save error must not imply
that replacement never changed the destination.

A content digest/revision check is not a filesystem atomic compare-and-swap.
The lock serializes cooperating Wtop writers, including supported path aliases;
external editors may ignore it. Document remaining external-editor race limits,
filesystem/remote-share semantics and power-loss durability. Do not promise a
stronger primitive than the target provides.

`ReplaceFileW` preserves several original metadata classes but has documented
partial-failure name states. Its write-through flag is unsupported. Replacing
`MoveFileEx` with `ReplaceFileW` alone does not guarantee that every failure leaves
the original path untouched. Test the actual error/recovery state machine. [S11]

### Migration policy

Introduce configuration version 2 when stable view identities require it. Read
version 1 and legacy NTop into a validated in-memory form, retaining original
bytes. Migrate on explicit save/export, preserve a named original and record the
schema transition. Unknown future schema versions remain read-only/unsupported;
never pretend to understand their semantics.

Known scoped legacy records must follow their original view during reorder or
migration. If scope cannot be proven, retain the original and report the limit.
Downgrade/export to an older schema should report information loss explicitly.
Installation upgrades do not rewrite preferences merely by replacing the binary.

### Tests

Two cooperating writers, external edits, aliases/case variants, stale versions,
read-only directories, full disk, denied ACL preservation, locked files, network
loss and every replacement failure state. Place foreign sentinel files/registry
values around the owned target. Test legacy import, future-version refusal,
unknown-field association, interrupted migration, failed save retention and old
binary behavior on new configuration. No retry may select another writable path.

## 7. CLI compatibility and typed system output

**Existing work:** WT-W010/020/035. **Contracts:** WT-030/011/040.

Continue using `spec/catalog/commands.json` as the command authority. Add checked
mappings to implementation/help; do not independently maintain four option lists.
A development-time generator is optional only if duplication actually warrants
it; it must not become an installed runtime dependency.

### Freeze these compatibility choices before 0.4.0

| Surface | Recommendation |
| --- | --- |
| Default and `--tui` | TUI only with suitable interactive input/output; redirected default stays an explicit diagnostic |
| `-d`, `--snapshot`, `--dump` | Preserve snapshot semantics; document the htop short-option collision |
| `-n` | Preserve name filtering and add `--name`; use descriptive long forms for any iteration-count option |
| `--interval` | Preserve bare-integer milliseconds; retain checked bounds |
| `--delay` if added | Explicit htop-compatible deciseconds, converted with checked multiplication by 100 ms |
| `-p/-u/-s` | Add `--pid/--user/--sort-key` without silently changing match or optional-argument behavior |
| Color/bar aliases | Add `--no-color/--no-colour/--no-function-bar`; retain existing spellings |
| `--status-bar` | Preserve its current function-bar meaning; add unambiguous naming for distinct live status |
| Version/mouse aliases | Add nonconflicting `-V` and `-M` while retaining existing forms |
| Keymaps | Change bindings only; never change CLI meaning |
| Parsing | Support deliberate `--name=value`, option placement and `--` boundaries with conflict tests |
| Read-only mode | If added, enforce in the common action/launch dispatcher, including vi `:exec`/`:kill` |
| Information commands | Help/version/build capabilities stay collector-free; runtime probes require explicit bounded action |

Stable semantic metric IDs should be distinct from display headings such as
`CPU%` or `MEM`. Preserve heading aliases deliberately. The CLI shares process
selectors, sorting, field definitions and actions with TUI, while every selection
rule states whether it acts on presentation or collection.

### Output schema transition

Current schema 1 uses states such as `ok`, `warming` and `discontinuity`; the
canonical metric model uses a richer vocabulary. Introduce an explicit snapshot
schema 2 for corrected normalization, typed system summary and canonical states.
Document the migration from development schema 1. Do not maintain two collectors
or the old incorrect calculation. Any deliberate legacy projection translates
names/structure only and still reports truthful measurement semantics.

Suggested shape (illustrative, not the implemented wire contract):

```json
{
  "schema": "wtop.snapshot/2",
  "sequence": "42",
  "sample": {"state": "valid", "complete": true},
  "system": {
    "commit_bytes": {"state": "valid", "value": "3221225472", "unit": "byte"},
    "commit_limit_bytes": {"state": "valid", "value": "8589934592", "unit": "byte"}
  },
  "processes": []
}
```

The actual schema also defines source/provider identity, observation intervals,
monotonic/wall time roles, raw counters, CPU normalization/scope, completeness and
errors. Exact potentially large integers remain decimal strings. A failed current
metric has null value; retained last-good values have separate time/state.
Distinguish observed process count, selected rows and truncated enumeration.

JSON goes to stdout, diagnostics to stderr; no console escape sequences or log
preambles enter machine output. Define exit categories for success, usage,
partial data, unsupported operation, runtime failure and cancellation before
freezing them. Preserve existing exit codes unless migration explicitly changes
them. Partial observation is distinct from failed process mutation.

The future `query`, `inspect` and `log` commands reuse this model. Logs need
explicit duration/count/size/rotation bounds and slow/broken-consumer behavior;
queueing cannot grow indefinitely. Command-line argument export is opt-in.

### Tests

One fixed snapshot drives text, JSON and TUI assertions for equivalent units and
values. Include precise 64-bit integers, null/error states, malicious text,
partial counts, locale-independent numeric encoding, duplicate/conflicting flags,
`--` and `--name=value`, legacy aliases, readonly bypass routes, redirected
invocation, output failure and a consumer that closes its pipe.

## 8. Exact payloads and delivery

**Existing work:** WT-W060/070/130. **Contracts:** WT-050/060/062/063.

For each qualified modern CPU/ABI profile, build one final `wtop.exe`; reuse it
inside a portable ZIP and a dual-purpose MSI. User/machine installation changes
placement/ownership, not application behavior or the compiled payload. An EXE
bootstrapper is optional future packaging. MSI 5.0 supports dual-purpose packages
on its supported systems; that mechanism does not cover every historical OS. [S12]

WinGet should initially install that same MSI. This avoids implementing a second
WinGet-portable ownership path while core correctness is still being established.
Microsoft currently documents WinGet for Windows 10/11 and Server 2025, with the
Windows 10 command-line floor at version 1809. It is not the legacy installation
mechanism. Direct GitHub acquisition downloads a pinned version/profile asset to
staging, verifies provenance/integrity, then uses the same installer or portable
payload. A digest is integrity evidence, not proof of publisher authenticity by
itself. Do not invent an available WinGet package ID. [S13]

### Resolve the MSI preservation boundary

The existing Wtop contract permits removal only of owned unchanged files and
integration. Standard MSI RemoveFiles is component/state-based; it is not a
hash-preservation policy. Keep mutable data outside MSI File/RemoveFile tables,
avoid broad deletion patterns, and implement/test a preflight/preservation or
refusal path for modified owned payloads if retaining this blanket guarantee.
The recommendation here retains the guarantee and gates installer promotion on
it. A future narrower policy requires an explicit contract change. [S14]
Enforce preservation/refusal inside all MSI maintenance entry points, including
direct msiexec, Apps & Features, WinGet, upgrade and repair. A preflight implemented
only in an optional bootstrapper is bypassable and does not meet the contract.

Owner diagnostics distinguish unmanaged, Wtop-managed, MSI, other package manager
and inconsistent/unknown registration. Maintenance goes through the actual owner.
No installer or Wtop command silently adopts another manager's files. PATH edits
are optional/owned, token-aware and reversible without deleting preexisting
entries. Ordinary launch creates no service, scheduled task, updater or startup
entry. Uninstall preserves user configuration by default; explicitly requested
data removal names its scope and protects foreign files.

### Build and promotion sequence

1. Resolve canonical target ID and exact checkout/tree, including dirty status.
2. Record compiler/linker/SDK/CRT, flags and relevant generated inputs.
3. Build once; inspect CPU machine type, executable/subsystem headers, imports,
   manifest and resources. Import/CRT inspection is necessary but insufficient.
4. Sign final application bytes where an authorized route exists; record signed
   and reproducible unsigned identities separately when relevant.
5. Run required tests on the final application payload; record its digest.
6. Construct ZIP/MSI from those bytes; sign wrappers separately if used.
7. Extract/install and compare the application hash; test scope/update/removal.
8. Publish only after qualification and explicit release work. Acquire bytes
   from the actual GitHub/package channel and verify identities again.

No rebuilding or relinking during packaging after qualifying another binary.
A changing CI runner label does not define support; record actual OS image/build,
architecture, locale, console, filesystem, privilege and optional components.
MSI numeric versions need a collision-free mapping: ProductVersion uses three
numeric fields and ignores a fourth. Keep this separate from SemVer prerelease
labels; portable prereleases can precede a qualified MSI version scheme. [S15]

### Delivery tests

Use isolated owned VMs/snapshots. Test standard-user and machine installation,
portable read-only/removable/non-ASCII/space-containing paths, side-by-side scopes,
PATH ownership, foreign sentinel preservation, interrupted update, running-image
locks, changed user data, modified payloads, failed rollback and actual acquisition.
Verify identical application hashes across channels. Qualify every explicitly
advertised OS/profile; pairwise tests can reduce secondary host/locale combinations
but cannot substitute for a promised target's native execution.

## 9. Command-line/services and per-core providers

**Existing work:** WT-W035/030 and appropriate WT-W110 scope. **Contracts:** WT-011/012/061.

Reuse fork ideas and existing small helpers only after file-specific licensing
and source review. Do not merge an entire historical branch just for one feature.

| Provider | Native acquisition policy | Shared contract |
| --- | --- | --- |
| Owner name | SID identity first; bounded separate name resolution/cache | Owner unavailable stays explicit; no SYSTEM substitution |
| Executable path | Qualified native query with least necessary rights | Distinct from display name/command line; partial/access state |
| Command line | Explicit bitness/provider policy; optional rights; on-demand or slow cadence | Lifetime key, bounded UTF-8, truncation/freshness, terminal-safe presentation |
| Services | Qualified service enumeration/association; optional WMI where needed | Structured service list; process lifetime validation and association age |
| Per-core CPU | Reusable PDH or another qualified provider; real processor/group identity | User/kernel/idle/total semantics, topology generation, independent cadence |

Enumeration and association are not atomic: do not attach service metadata to a
reused PID without lifecycle/time validation. Collectors retain private native
handles; snapshots contain values. Protected/denied data must not cause privilege
escalation or loss of accessible basic fields.

If WMI is chosen, initialize process-wide security once under explicit policy,
own COM apartment/lifetime correctly, avoid holding publication/UI locks during
queries, and retain correct completion before releasing cache/context. An
`RPC_E_TOO_LATE` result must be interpreted according to who initialized security;
reinitializing it for every refresh is incorrect. [S16]

For per-core PDH, resolve localization, enumerate stable group/processor instances,
reuse the query, preserve warm-up/reset behavior, and aggregate raw weighted
numerators/denominators. Never add privileged time to a total that already includes
it. Exclude `_Total` from re-aggregation. CPU instance names are not a sufficient
process-lifetime identity; continue direct process sampling for process rows.
Cadence is provider-specific; faster mouse repaint does not imply high-frequency
PDH polling. [S17]

Tests cover x86 observer/x64 target where claimed, process exit/reuse, protected
processes, long/Unicode/control-containing arguments, service changes, denied
WMI, stalled queries, cancellation, bounded cache eviction, CPU groups, odd core
counts, localized counters, topology changes and multiple independent instances.
Synthetic tests establish semantics; actual hardware/OS evidence establishes the
claimed topology and provider support.

## 10. Historical backends within one source tree

**Existing work:** WT-W080/090/100. **Contracts:** WT-020/010/012/060.

Start an inexpensive real old-compiler/loader spike alongside modern correction.
It should expose width/memory/CRT assumptions before they spread, but it need not
implement the entire monitor to be useful. Use the existing target catalog;
select source groups by CPU/ABI/provider/host, not by duplicated product folders.

Each profile proceeds through toolchain availability -> minimal loader -> input/
cell-output/time/files/cleanup -> truthful entity enumeration -> shared core ->
meaningful actions -> era-appropriate delivery -> exact native qualification.
Failure has an explicit stage and cause. Passing compilation never skips native
startup and behavior gates.

| Family | First concrete proof and boundary |
| --- | --- |
| Modern NT x64/x86/ARM64 | Corrected model and native execution; qualify ARM64 and true 32-bit Windows independently |
| Server, WinPE, ReactOS | Reuse suitable NT code but qualify exact image/components; PE portability and ReactOS behavior are independent |
| Earlier NT | Explicit import/CRT/CPU floors, optional API resolution, enumeration and clocks on real named images |
| Win9x | Native/ANSI and collection differences; shared PE32 feasibility remains a tested hypothesis |
| DOS2+/FreeDOS | MZ/BIOS/DOS host; honest memory/task/TSR semantics rather than invented NT process metrics |
| DOS1.x | Separate loader/CRT/filesystem proof; do not assume DOS2 handles/directories or one OEM proves every 1.0 variant |
| Win16 3.x then 1.x/2.x | Appropriate NE/toolchain, cooperative pump, task model and text-grid host |
| OS/2 32-bit then 16-bit | Native VIO/collection plus LX/NE memory and calling-convention proof |
| CE/Mobile | Named SDK, OEM image and CPU/ISA; no generic single CE32 binary |
| Stock RT | Retain explicit deployment-policy restriction; desktop ARM64 Windows is a separate target |
| Historic NT Alpha/MIPS/PowerPC | Separate work only with usable toolchain and exact image evidence |

Windows CE's documented processor families include x86, MIPS, SH4 and ARM; a
32-bit word size is not a shared instruction set. [S18] Open Watcom documents
relevant DOS/Win16/OS2 targets and is useful for a toolchain spike, but its target
list is not evidence that the current program/CRT runs at every historical floor.
[S19] WinPE lacks ordinary MSI support; use its exact supported image/component
profile, including any specifically qualified emulation component rather than a
universal architecture assumption. [S20]

The GUI remains deferred. On an OS without the needed console host, a minimal
native window presenting the same character grid can be a TUI host adapter; do
not use that necessity to expand into a second rich GUI product. It still needs
that OS's message-loop/callback/lifetime tests.

Old machines can receive verified binaries copied from a modern host. Embedding
modern TLS/download dependencies in an 8086/NT3.1 monitor is unnecessary for
supporting CLI-driven deployment.

## 11. Testing, documentation and review discipline

### Test the contract at the cheapest sufficient layer

| Layer | What it establishes | Typical cases |
| --- | --- | --- |
| Pure C / fixed vectors | Arithmetic, parsing, settings, view transformations | Overflow, reset, identity, clipping, unknown-field scoping |
| Scripted adapters / virtual clock | Publication, actions, scheduling, failure state machine | Slow/stuck work, partial actions, stale data, cancellation ordering |
| Portable fuzz/property runs | Bounded handling of broad malformed input | Config, CLI, encoding and helper protocol |
| Native integration | Windows rights, APIs, console and filesystem behavior | Owned processes, actual input/cells, replacement/ACLs, shutdown |
| Installed/acquired artifact | Exact payload and ownership behavior | Install/update/remove/rollback, PATH and foreign sentinels |
| Native historical/hardware | Actual compatibility and topology | Loader/CRT/CPU/SDK/provider-specific behavior |
| Human visual/use review | Readability and interaction quality | Dense/narrow layout, key discovery, glyph/host differences |

Keep the fast deterministic suite in every relevant PR. Run native tests for
changed adapters and lifecycle boundaries; run target-specific qualification for
claims that changed. Use sanitizer/static analysis where supported; a clean run
is scoped evidence, not a universal memory-safety proof. Do not rerun an entire
legacy/hardware matrix for a documentation-only change.

Prefer independent oracles that reject plausible wrong implementations. A
Task Manager comparison is diagnostic, not the sole CPU oracle, because sampling
and utilization definitions may differ. Do not write tests that merely search
source for the function name or repeat its arithmetic expression.

Performance work starts from representative measured baselines: enabled fields,
process/thread counts, cadence, viewport, transport and host; CPU time, peak
allocations/handles, input/sample latency and bytes written. Test normal operation
and a bounded failure/churn scenario. Profile before indexing/differential redraw
optimization; never widen a failed threshold just to pass. No unsupported ETA,
CPU percentage, memory budget or completion percentage is assigned here.

### Documentation stays small and authoritative

- Canonical behavior: update the relevant OKF concept and existing command/metric/
  target/acceptance registries when a decision is implemented.
- Public headers: ownership, units, time domain, capacity, failure and threading.
- Source comments: explain non-obvious invariants, API caveats and compatibility
  boundaries; avoid narrating assignments or copying entire specifications.
- Tests: explain the independently expected outcome and historical failure mode.
- Decision records: only consequential choices (CPU normalization, alias collision,
  schema migration, isolation and installation ownership), linked from concepts.
- README/help: observed user behavior; planned examples clearly labelled.
- Evidence: actual source/artifact identity, command, environment, oracle, results,
  failures and limits. No document validator creates a human/native pass.

Adopt the already inspected SysPane measurement/owner contracts, ScreenSave's
portable/native boundaries, and DiskEd/AIDE's provenance/catalog discipline as
small source-bound lessons. They are not runtime dependencies. FacMan/Dominium/
Eureka components need a pinned relevant review before a code-reuse claim.

## 12. Suggested integration slices

The work catalog currently projects WT-W000 active and no ready follow-on unit.
Reconcile exact existing implementation/evidence first; split overly broad
acceptance/work boundaries where necessary. Do not reset working code or mark
large units done merely to make scheduling look orderly. The following slices
map to existing work IDs and are proposals, not an alternative status ledger.

| Suggested PR | Existing work | Reviewable result and stop condition |
| --- | --- | --- |
| Baseline/evidence reconciliation | W000/010 | Preserve audit records, map actual narrow evidence, identify unsatisfied scope; no invented passes |
| Metric arithmetic and mapping | W020 | Correct commit, per-process timing and age; independent vectors; explicit CPU/schema migration |
| Per-target action outcomes | W030/040 | No lost batch results; checked-handle identity; requested/confirmed termination distinction |
| Snapshot transfer and freshness | W020/030 | Immutable bounded transfer, timestamps and stale states; UI-owned indexes, short publication lock |
| Stateful cadence and blocking metadata | W030 | One observation per cadence, warming startup, bounded owner-name enrichment; measured changes |
| Shutdown fault qualification | W030 | Owned callback/worker retirement; explicit emergency limits; isolated optional work only where justified |
| Contextual UI and text cells | W040 | Shared action labels/dispatch/hit regions, conservative Unicode and native host evidence |
| Stable views and storage | W050 | Config identity/version migration, scoped unknown preservation, save/recovery/concurrency contract |
| Complete bounded Setup | W040/050 | View CRUD and existing customization limits editable, explicit save and error behavior |
| CLI aliases and system summary | W010/020/035 | Catalog-consistent grammar, typed system data, frozen schema/exit compatibility and pipe tests |
| Payload/install qualification | W060/070/130 | Same final bytes across channels, owned modes/update/removal, exact advertised target evidence |
| Rich optional providers | W035/030/110 | Command-line/services then per-core, with access/freshness/cadence/lifecycle evidence |
| Historical capability slices | W080/090/100 | Independent loader/host/provider/core/action/distribution progression on exact target images |

For each slice, the implementer receives: current source revision; selected work,
requirement and acceptance IDs; exact behavior/compatibility delta; file ownership;
public interface change; independent failing cases; required native/profile checks;
what will be removed or retained; and the bounded completion criterion.

Parallel work is useful after interfaces are fixed: one writer can own pure
measurement code, another cell-layout fixtures, another a legacy loader proof.
Do not let several writers concurrently rearrange `ntop.c` or invent incompatible
snapshot/settings types. Integrate one bounded change into canonical `master`,
run relevant checks, then refresh dependent branches. Keep `ntop-baseline` fixed.

0.4.0 closes the agreed modern correctness/TUI/settings/snapshot/delivery scope.
Later 0.x adds rich inspection and independently qualified historical families.
1.0 requires closure of the actually agreed TUI/CLI/platform contract; incomplete
original targets cannot become complete through an unexplained scope reduction.

## 13. Primary references consulted

The repository links above ground source observations. External references below
were consulted on 17 November 2026; current documentation minimum-support labels
alone are not a complete history of when an old API first existed.

- [S1 — PERFORMANCE_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-performance_information)
- [S2 — GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes)
- [S3 — High-resolution timestamps](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)
- [S4 — GetSystemTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getsystemtimes)
- [S5 — TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess)
- [S6 — LookupAccountSidW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-lookupaccountsidw)
- [S7 — CancelSynchronousIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio)
- [S8 — Console HandlerRoutine](https://learn.microsoft.com/en-us/windows/console/handlerroutine)
- [S9 — Unicode East Asian Width](https://www.unicode.org/reports/tr11/)
- [S10 — Known Folder IDs](https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid)
- [S11 — ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew)
- [S12 — MSI single-package authoring](https://learn.microsoft.com/en-us/windows/win32/msi/single-package-authoring)
- [S13 — WinGet](https://learn.microsoft.com/en-us/windows/package-manager/winget/)
- [S14 — MSI RemoveFiles](https://learn.microsoft.com/en-us/windows/win32/msi/removefiles-action)
- [S15 — MSI ProductVersion](https://learn.microsoft.com/en-us/windows/win32/msi/productversion)
- [S16 — CoInitializeSecurity](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializesecurity)
- [S17 — Collecting Performance Data](https://learn.microsoft.com/en-us/windows/win32/perfctrs/collecting-performance-data)
- [S18 — Windows CE processor families](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms903994(v=msdn.10))
- [S19 — Open Watcom target documentation](https://open-watcom.github.io/open-watcom-v2-wikidocs/c_readme.html)
- [S20 — WinPE introduction](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/winpe-intro?view=windows-11) and [optional components](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/winpe-add-packages--optional-components-reference?view=windows-11)
