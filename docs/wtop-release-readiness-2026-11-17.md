# Wtop release readiness — 17 November 2026

**Verdict:** the requested interaction and configuration foundations are merged,
but the complete status-bar contract, htop feature superset and stable-release
qualification are unfinished. **Wtop 0.4.0 is a release target, not an available
release.** The audited application still identifies itself as `0.1.0-dev.2`.

Reviewed canonical `master`:
[`1ca153b77e7df203b9f6d1f8cfb2ff472318bc27`](https://github.com/Julesc013/wtop/tree/1ca153b77e7df203b9f6d1f8cfb2ff472318bc27).
Inherited NTop:
[`326ccd80b41842868f4247d1e8978e854658a710`](https://github.com/gsass1/NTop/tree/326ccd80b41842868f4247d1e8978e854658a710),
tag `v0.3.21`. This audit changes documentation and planning, not application
source, the version, acceptance results, released binaries or support claims.

The [structured observations](wtop-audit-observations-2026-11-17.json) contain
source hashes, fourteen classified findings and fresh host-test receipts. The
[fork audit](ntop-fork-audit-2026-11-17.md) records external ideas and their risks.
[TODO](../TODO.md), [ROADMAP](../ROADMAP.md) and the existing work/acceptance
registries remain the route to implementation. Finding IDs below identify this
dated audit; they are not a second requirements or work-status catalog.

## 1. Is the requested presentation work done?

| Request | Implemented at the audited revision | Remaining boundary |
| --- | --- | --- |
| Htop controls by default | Yes; common F1–F10 actions, navigation, tagging, follow, search/filter, sort/tree, confirmed termination and Windows priority classes | Full htop feature parity is not implemented |
| Vi/Vim option | Yes; `--keymap vi` and `vim` alias | A selectable preset, not complete Vim emulation or arbitrary rebinding |
| Emacs option | Yes; control/meta navigation and search, cancel, command entry and C-x save/quit routes | A preset; broader host/modifier qualification remains |
| Final-row control bar | Yes; shown by default, clickable, hideable with B/Setup/CLI/config | Common F1–F10 labels are static across overlays; contextual hints remain unfinished |
| Continuous live status | Partial; keymap/dirty title, task counts, prompts and errors exist | No persistent active-filter/sort/provider/freshness summary; a stalled sample is not marked stale |
| Mouse | Yes; row selection, wheel, header sorting, tabs, bar and Setup | Native console adapter; full gesture, resize, mode-failure and terminal-host qualification remains |
| Htop-like default colors | Yes; htop/NTop/monochrome presets and fourteen role overrides | Native 0–255 console attributes; RGB depends on host palette, not a truecolor theme engine |
| Shown/hidden and ordered rows/columns | Yes; five built-in header rows and nine built-in process columns | No arbitrary metric definitions or manual column widths |
| Saved tabs and sorting | Yes; up to eight named views with per-view columns, filter, sort/direction, tree and visibility | Setup hides/reorders existing tabs; create/rename/delete requires editing the config |
| Saving customization | Yes; F2 `s`, `:save`, C-x C-s and `--write-config PATH` | Explicit save only; installed ownership, custom file metadata and fully specified multiwriter behavior remain |

Keep the function bar **shown by default**: it makes the available controls
discoverable and mouse-accessible. Hiding it should remain a persisted user
choice; prompts and errors must remain visible. Complete the adjacent status
area with compact view/filter/sort/dirty/freshness information rather than
adding decorative charts. This is a design recommendation consistent with the
[existing TUI contract](../spec/interfaces/tui.md).

The ten inherited application/build/config inputs are under `source/`. The
actual inherited site-config name was `_config.yml`, not `config.yml`. Root
TODO/ROADMAP and root CMake are present. Source relocation is complete; it does
not establish historical-platform portability.

Implementation references at the pinned source: [settings defaults/model](https://github.com/Julesc013/wtop/blob/1ca153b77e7df203b9f6d1f8cfb2ff472318bc27/source/settings.c),
[key/action policy](https://github.com/Julesc013/wtop/blob/1ca153b77e7df203b9f6d1f8cfb2ff472318bc27/source/ui.c),
[native console and save adapter](https://github.com/Julesc013/wtop/blob/1ca153b77e7df203b9f6d1f8cfb2ff472318bc27/source/ntop.c),
[native interaction tests](https://github.com/Julesc013/wtop/blob/1ca153b77e7df203b9f6d1f8cfb2ff472318bc27/tests/test_native_tui.py).

## 2. What has actually been verified?

| Evidence | Result and precise limit |
| --- | --- |
| Fresh specification validation in this audit | Passed: 17 concepts, 51 requirements and 51 acceptance records are structurally consistent |
| Fresh specification-tool tests | 24/24 passed |
| Fresh GCC 14.2 host build | Portable modules built with C90 settings and `-Wall -Wextra -Werror -pedantic` |
| Fresh portable CTest | Core/settings/UI, 3/3 passed; no Windows execution or new sanitizer result follows from this |
| Previously completed master CI, rechecked during this audit | [Run 43976010499](https://github.com/Julesc013/wtop/actions/runs/43976010499): all four jobs successful for the exact audited source |
| Native scope of that CI | Windows Server 2025 x64: x64 and Win32 applications each passed 9/9 CTest entries; Win32 means x86-on-x64, not a 32-bit Windows installation |
| ARM64 scope | Build passed; execution step skipped on the x64 runner |
| Earlier detailed presentation evidence | [Implementation observations](presentation-verification.md) retain exact source/artifact identity and the preceding Ctrl+S failure/fix |

The native harness reads actual console cells/attributes and injects input only
into an isolated owned console. It covers important bar, mouse-mode, editor,
save/reload, stale-file, vi/Emacs and queued-quit cases. It is not a human visual
review or an exhaustive terminal, permissions, Unicode, process-churn or
historical-OS qualification. CTest suite counts are not counts of completed
product requirements.

The formal registry still contains **51 `not_run` acceptance cases, zero catalog
evidence records, one active work unit and fourteen planned units**. Fifteen
target profiles are unqualified and one is policy-blocked. Real CI observations
exist separately; the catalogs need scoped reconciliation. Link exact observations
and split overly broad cases where necessary, then advance only the satisfied
scope. Do not mark all gates passed to make the work queue appear current.

## 3. Source findings that affect release readiness

The classification matters: a static proof of an API/algorithm mismatch is not
a measured failure rate; an untested risk is not an observed crash. Full source
locations, proposed closure and `native_reproduced: false` appear in the
[observation record](wtop-audit-observations-2026-11-17.json).

| ID | Finding | Required treatment |
| --- | --- | --- |
| CUR-001 | Cmt derives from process-constrained `MEMORYSTATUSEX` limit/availability | Correct system-commit collection, or explicitly expose its actual scope; close before stable system-metric claims |
| CUR-002 | Process CPU samples and the shared system denominator use different observation windows | Define consistent per-process timing/capacity; test delayed reads, counter resets and processor-group limits |
| CUR-003 | Bulk priority messages overwrite earlier results; termination keeps counts but loses individual failure detail | Retain bounded per-identity outcomes shared by TUI and future CLI |
| CUR-004 | Old samples have no visible freshness/stale state during stalled collection | Timestamp publication and show stale/degraded status; bound optional provider work |
| CUR-005 | Function bar is not fully contextual; continuous live status is incomplete | Finish context-derived hints/status while retaining hidden-bar recovery |
| CUR-006 | Layout mostly counts UTF-16 units rather than display cells | Define conservative width/substitution behavior and qualify supported hosts/encodings |
| CUR-007 | Settings replacement does not retain custom ACLs/all metadata; stale-edit checks are not atomic compare-and-swap | Specify and test metadata and concurrent-writer behavior before managed installation |
| CUR-008 | Htop CLI compatibility collides with existing aliases | Freeze an explicit compatibility table before the first stable CLI promise |
| CUR-009 | Flat view still builds parent relationships; rendering clears/redraws complete frames | Measure scaling before optimizing; no performance regression or speedup was measured in this audit |
| CUR-010 | Emergency self-exit can bypass console restoration when native work blocks | Fault-test normal/emergency shutdown, output/save/provider stalls and repeated launch/exit |
| CUR-011 | Tab create/rename/delete is config-only; widths and arbitrary keybindings are not customizable | Finish TUI editing or explicitly retain these bounded 0.4.0 limitations |
| CUR-012 | Formal work/acceptance status has not absorbed the narrow implementation evidence | Reconcile exact source-bound observations without certifying broader requirements |
| CUR-013 | Managed portable/per-user/system ownership, installers and package channels are absent | Implement and qualify the requested delivery modes; an EXE copied to a folder is only one part |
| CUR-014 | Broad CLI, htop superset and historical backends remain planned | Implement staged capability-based slices; preserve the long-term scope explicitly |

### System commit: a concrete semantic correction

`PollSystemInfo` at `source/ntop.c:1065–1077` calculates Cmt from
`ullTotalPageFile - ullAvailPageFile`. Microsoft defines the former as the
smaller system/current-process commit limit and the latter as what the current
process can still commit. Their difference is not guaranteed system committed
memory. Renaming the inherited page-memory meter was insufficient.

Use `GetPerformanceInfo`'s `CommitTotal`, `CommitLimit` and `PageSize` with checked
width conversion/multiplication and explicit availability, or label an alternate
metric with its actual scope. References:
[MEMORYSTATUSEX](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-memorystatusex),
[PERFORMANCE_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-performance_information).

### CPU timing: define the denominator, then test it

At `source/ntop.c:890–941`, each system sample occurs before its corresponding
process-reading pass. Individual process windows shift and can change length
with variable query costs, yet every CPU percentage divides by the same system
delta. This can bias results; the magnitude has not been measured.

An **illustrative independent fixture**, not a native observation: system
samples at wall times 0/200 ms on two logical CPUs give a 400 ms aggregate
capacity delta. A process sampled at 80/250 ms consuming 170 ms of CPU occupies
one CPU fully over its own 170 ms window, or 50% of two-CPU capacity. Dividing
170 by the unrelated 400 ms instead yields 42.5%. Tests should make that policy
decision observable rather than duplicate the current formula.

Above 64 logical processors,
[GetSystemTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getsystemtimes)
reports the caller's primary processor group. An explicit JSON caveat is useful,
but does not establish correct whole-machine CPU normalization on that topology.

### Ownership and responsiveness remain evidence work

The collector already builds private samples and revalidates process creation
identity for actions. Those are valuable foundations. Synchronous account lookup
can still block sampling; the UI does not show sample age. Startup sampling runs
on the main thread. Input/render/save work also shares the model lock.

The existing 3000 ms join and 3500 ms callback fallback bound some shutdown
paths by terminating Wtop itself. They do not make a native query cancellable or
prove orderly console restoration. Test blocked providers/output/save, Ctrl+C,
close, process churn, allocation/handle failures and repeated instances. Use
bounded caches/private publication or justified isolation; do not add routine
`TerminateThread` or unbounded workers.

Parent discovery scans process pairs and ancestry, including in flat view;
frames are cleared and rewritten. These are profiling candidates. Establish
CPU time, memory/handle high-water marks, sample/input latency and output bytes
across process counts, intervals, window sizes and transports before declaring
an optimization necessary or successful.

## 4. Reconcile the original creator's TODO

The [baseline README](https://github.com/gsass1/NTop/blob/326ccd80b41842868f4247d1e8978e854658a710/README.md)
is historical evidence, not a current complete Wtop checklist.

| Original entry | Inherited state | Wtop now / remaining work |
| --- | --- | --- |
| Buggy resizing | Struck through upstream | Viewport polling/layout guards improved; broad rapid-resize/host tests remain |
| Process tree | Struck through upstream | Present with revised traversal/cycle guards; branch collapse, child sorting and thread views remain |
| Searching | Struck through upstream | Present with bounded editor/pattern handling and keymap routes |
| Filtering | Still open in the short TODO, although CLI filtering was later added | Inherited CLI PID/name/user selectors plus new F4 live per-tab filtering exist |
| All htop CLI options | Open | Still incomplete and cannot be achieved by blindly copying aliases |
| Important interactive commands; follow | Partly open, follow struck through | Common function keys, setup, mouse, profiles and action safeguards added; detail/thread/affinity/per-core/pause work remains |

Upstream's latest retrieved release is
[NTop 0.3.21, published 23 December 2024](https://github.com/gsass1/NTop/releases/tag/v0.3.21).
The repository is unarchived; issues and PRs remain active, including per-core
PR #42 and architecture-build PR #82. This does not establish recent merged
development. The [fork audit](ntop-fork-audit-2026-11-17.md) maps original user
reports into useful Wtop regression scenarios without claiming them reproduced
or resolved.

## 5. Htop compatibility needs a versioned boundary

Released reference: **htop 3.5.3**,
[`20882b8ef8271fb49ea0eb74bbfd484e9b0a11d8`](https://github.com/htop-dev/htop/blob/20882b8ef8271fb49ea0eb74bbfd484e9b0a11d8/htop.1.in),
published 16 August 2026. Separately inspected development reference: **3.6.0-dev**,
[`21bbc80c7811149ee33f449748addf23740c7b9f`](https://github.com/htop-dev/htop/blob/21bbc80c7811149ee33f449748addf23740c7b9f/htop.1.in),
13 November 2026. Do not describe development-only options as stable behavior.

| Stable htop option | Current Wtop | Compatibility issue |
| --- | --- | --- |
| `-d / --delay` in deciseconds | `-d` selects snapshot; `--interval` takes milliseconds | Actual meaning/unit collision; settle before stable |
| `-C / --no-color / --no-colour` | `-C`, `--theme monochrome` | Long aliases absent |
| `-F / --filter` | F4 live filter; CLI `-n` name selectors | Different matching/scope; define before aliasing |
| `--no-function-bar` | `--no-status-bar` | Equivalent basic intent; htop spelling absent |
| `-h / --help` | Present | Basic metadata path exists |
| `-p / --pid` | `-p` | Long alias absent |
| `-s / --sort-key`, including `help` | `-s` with nine named columns | Long/list alias and some sorting semantics absent |
| `-u / --user[=name or UID]` | Required `-u USER` account name | Optional-self and long spelling absent; Unix UID is not a Windows account/SID |
| `-U / --no-unicode` | No equivalent flag | Needs explicit conservative renderer semantics |
| `-M / --no-mouse` | Long option present | Short alias absent |
| `--no-meters` | Config `HeaderRows none` | CLI alias absent |
| `--readonly` | No equivalent mode | Must cover every mutation/launch route if added, not just hide F9 |
| `-V / --version` | `-v / --version` | Uppercase alias absent; preserve existing lowercase deliberately |
| `-t / --tree[=mode]` | F5, `:tree`, saved preference | CLI tree and stable-tree modes absent |
| `-H / --highlight-changes` | Absent | New feature, not an alias |
| `--drop-capabilities` | Absent | Linux-specific behavior; no identical Windows semantics |

The parser also lacks generic `--option=value` handling. Development 3.6.0-dev
adds `-n / --max-iterations`, which would conflict with NTop's name filtering;
**stable 3.5.3 does not have that option**. Its newer keep-visible and PCP-specific
options further demonstrate why a moving upstream cannot be an undefined parity
promise.

Recommended policy: one unambiguous Wtop CLI independent of the chosen TUI
keymap; prefer descriptive long options; preserve or explicitly deprecate
inherited aliases with migration notes. Decide `-d` before the first stable
release. Do not silently change option meanings when `--keymap` changes.

## 6. Proposed release definition: Wtop 0.4.0

**Recommended scope:** a release-quality, self-contained modern Windows process
monitor with the requested htop-style presentation, explicit configuration,
bounded noninteractive snapshots, truthful capability limits and the requested
installation ownership. It is the first Wtop release following NTop 0.3.x; it
does not assert that DOS/Win16/Win9x/CE/OS2 backends are complete.

This is a proposed release boundary within the existing product scope, not an
unannounced removal of historical targets or installation modes. A portable-only
first release would be a narrower milestone and must be identified as such.

| Gate | Concrete closure | Existing work |
| --- | --- | --- |
| Semantics and CLI | Resolve aliases/units/denominators, define supported output/settings contracts, distinguish build metadata from runtime capability status | WT-W010/020/035 |
| Metric correctness | Close CUR-001/002; add independent timing, commit, large-value, denied and reset fixtures | WT-W020 |
| Safe actions | Close CUR-003; preserve identities, confirmations, mixed per-target outcomes and action availability | WT-W030/040 |
| Freshness and lifecycle | Close CUR-004/010; no old values masquerading as fresh, no orphaned owned work, explicit emergency limits | WT-W030 |
| Everyday TUI | Contextual bar/status, usable small/resize layouts, conservative Unicode, mouse/keyboard recovery, agreed tab-editing scope | WT-W040 |
| Settings | Explicit paths and saves, failed-write preservation, metadata/multiwriter policy, legacy import and supported migration | WT-W050 |
| Qualified payloads | Exact CPU/ABI/import/CRT/source identity, native behavior on each advertised OS/host, human usability and measured resource budgets | WT-W070 |
| Delivery modes | Same application bytes in portable EXE/ZIP and appropriate setup packages; per-user/machine scope, reversible owned PATH/registration, update/uninstall | WT-W060 |
| Acquisition channels | GitHub assets/digests/source/notices and tested direct CLI download/install; WinGet exact-architecture manifest and observed installation before claiming availability | WT-W060/130 |
| Release evidence | Map the precise feature/profile closure to acceptance, retain failures/limits, acquire and verify the actual published bytes | WT-W000/130 |

Full command lines, service association, per-core CPU and extensive query/logging
are valuable subsequent slices; they should not be rushed into the first stable
build by copying unqualified fork workers. A small typed system summary for
snapshots is a particularly useful candidate: Wtop's current snapshot emits
process records and sample metadata, **not system CPU/memory/commit totals**.
Add it only with consistent semantics and schema compatibility treatment.

### What the first support table can truthfully say

| Lane | Current evidence | What would qualify advertised 0.4.0 support |
| --- | --- | --- |
| Modern NT x64 | Server 2025 x64 CI build/run | Exact promised client/server images and console hosts, action/failure/resource and package checks |
| Win32 application | x86 execution on x64 Server 2025 | State x86-on-x64 scope; test actual 32-bit Windows separately before claiming it |
| ARM64 | Cross/native-toolchain build on x64 runner | Native ARM64 runtime and package acquisition checks |
| WinPE | Planned profile | Exact PE image/components, console, permissions and persistence behavior |
| ReactOS | Planned profile | Pinned image/toolchain and explicit degraded capabilities |
| Legacy NT/Win9x, DOS/FreeDOS, Win16, OS/2, CE/RT | Research/unqualified or policy-blocked | Independent loader/provider/renderer/deployment work; not inferred from modern builds |

Do not split binaries purely by Windows marketing eras. Use the fewest artifacts
supported by CPU, executable ABI, loader/API/CRT and measured compatibility.
One shared source tree and one application payload per qualified profile remain
the goal. Setup packages may wrap those same bytes; ordinary invocation must not
install itself, add startup work or change PATH.

### Version and artifact policy

Use planned development versions such as `0.4.0-dev.N`, then `0.4.0-rc.N`, and
the qualified stable product version `0.4.0` with tag `v0.4.0`. No version bump
was made by this audit. Do not derive the product version from a workflow-run
counter. Do not modify a published artifact under the same version.

Under [SemVer](https://semver.org/), `0.4.0-stable.1` is syntactically a
prerelease despite the word "stable". Prefer ordinary `0.4.0` and an explicit
channel/profile in release metadata; use `0.4.1` for a subsequent changed stable
product. Version zero can be release-quality while its public compatibility
surface is still evolving; 1.0 defines the stable contract.

Every artifact name/manifest must include its profile/architecture where needed.
MSI numeric version mapping, signed/unsigned status and release/source hashes
need deliberate rules. Do not invent a WinGet identifier or advertise an
installation command before the package exists.

## 7. What remains for Wtop 1.0.0?

Treat 1.0 as **completion of an explicitly agreed TUI/CLI product contract**, not
a claim that no defect can ever occur. The GUI remains deferred. The following
work remains after the first qualified release:

1. **Unified CLI:** shared query/inspect selectors and fields, provider/config/path
   diagnostics, real runtime capabilities, versioned output, bounded logs with
   duration/rotation, broken-pipe/slow-consumer behavior and deliberate typed
   process actions.
2. **Htop-tier process inspection:** full executable paths and command lines,
   service association, thread/detail views, supported affinity, richer process
   tree controls, pause/freeze, consistent sorting and explicit CPU/unit options.
3. **Resource model and performance:** group-aware per-core topology, optional
   measured hardware/event providers, typed snapshots and actions, bounded
   ownership and tested rendering/collection costs.
4. **Installation and maintenance:** portable/per-user/system ownership, update,
   rollback, uninstall, settings migration and stable CLI/schema compatibility
   exercised across supported releases.
5. **Historical and alternate systems:** real backends and tests for the agreed
   DOS/FreeDOS, Win16, Win9x/early NT, OS/2, CE/Mobile and environment-specific
   Server/WinPE/ReactOS profiles. Stock RT deployment constraints and CPU/ABI
   differences require explicit disposition. A DOS memory/task view cannot
   manufacture NT process telemetry.
6. **Acceptance closure:** each supported profile has reproducible source,
   toolchain, artifact and runtime evidence; every promised feature is implemented
   or has an explicitly agreed scope decision, with limitations visible to users.

If "1.0.0 when it is all done" includes every originally requested family, then
unfinished or merely experimental ports remain unfinished scope. Recording an
infeasibility or policy restriction is useful, but silently calling it completed
would change the goal. A narrower modern-only 1.0 would require a separate scope
decision. Keep the historical ambition visible throughout the 0.x series.

## 8. Suggested implementation order and regression oracles

Use small reviewed PRs against canonical `master`; keep `ntop-baseline` fixed.
Preserve working code and extract boundaries as they become necessary. The
2377-line native adapter still owns many responsibilities; separate collection,
model, actions, host services and rendering incrementally rather than creating
one copied application directory per OS.

Recommended order: metric/action correctness; freshness/lifecycle; contextual
TUI/Unicode; settings ownership; CLI compatibility and system summary; exact
payload/install qualification. Then command-line/service enrichment, per-core
and broad CLI slices, with legacy feasibility work progressing independently.
The existing work catalog owns dependencies; inspect implemented partial work
before interpreting its `planned` status as an instruction to rewrite it.

High-value **proposed tests, not tests performed by this audit**:

- System commit: independent provider values of 3 GiB committed/8 GiB system commit limit give
  37.5%, even when process-constrained fields say 512 MiB limit/128 MiB available
  (75% by the old subtraction). Follow with a native owned-job constraint case,
  measuring actual API values rather than assuming those illustrative numbers.
- CPU: the independent shifted-window example above, delayed reads, changing
  process counts, first samples, resets, zero time and processor-group changes.
- Actions: owned fixtures with success, denied, exited and reused PID lifetimes;
  assert ordered per-target results survive later successes and no replacement
  lifetime receives the action.
- Presentation: every keymap/context, hidden bar recovery, click/wheel/header/tab
  behavior, narrow/short/rapid resize, long/wide/combining text and mode failures.
- Storage and shutdown: two writers/external edits, custom ACLs, denied/full/locked
  destinations, interrupted saves, blocked providers/output, close/Ctrl+C and
  repeated launch/exit with handle/allocation accounting.

No unsupported deadline, percentage complete, leak-free guarantee or universal
performance estimate is assigned. Release readiness is the closure of these
specific behaviors on the exact advertised payloads.
