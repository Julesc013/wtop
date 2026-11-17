# Wtop roadmap

Public beta work: [six-patch contract](docs/beta-plan.md). A draft is not a public release; client/human/feedback gates remain mandatory.

Wtop aims to provide a dense htop-style native TUI and a unified command-line
inspection/control interface from one maintainable source tree. Compatibility is
qualified per CPU, executable ABI, loader/runtime floor and actual environment.
The native GUI is deferred.

This roadmap explains sequencing and exit conditions. The sole machine-readable
work queue is [spec/work/units.json](spec/work/units.json); normative requirements,
acceptance and target status are in [spec/catalog/](spec/catalog/). [TODO.md](TODO.md)
contains concrete closure tasks. No dates or percentage-complete claims are
assigned without measured capacity and evidence.

## Current position

The application is **0.4.0-dev.1**, developed on canonical `master`. Application,
helper and inherited configuration/build files remain under `source/`; root CMake
is the build entrypoint. GPL history and the fixed NTop baseline remain intact.

The eight requested implementation streams now have candidate code: measurement
and action modules, timestamps/freshness, contextual htop-style presentation,
version-2 settings/store transactions, query/inspect/log and typed system output,
isolated optional providers, exact-payload packaging and historical probes.
See the [current implementation record](docs/implementation-verification-2026-11-17.md)
for boundaries, actual checks and remaining failures. The earlier
[audit](docs/wtop-release-readiness-2026-11-17.md) and
[design](docs/implementation-plan-2026-11-17.md) retain their pinned context.

This is implementation progress, not a stable release or a claim that every
formal acceptance case has passed. Native console fixtures, current CI-host
execution, ARM64 compilation, scripted DOS instruction tests and historical
OS execution are distinct evidence. MSI wrappers are explicitly unqualified and
release promotion is blocked. WinGet publication and the native GUI are absent.

## Named release milestones

| Milestone | Intended outcome | Evidence boundary |
| --- | --- | --- |
| **0.4.0** — first Wtop release | Qualified modern Windows TUI, explicit settings, bounded CLI snapshots and requested portable/per-user/system delivery ownership | Correct known metric/action/lifecycle gaps; qualify every advertised OS/CPU/host and installation channel; historical targets remain explicitly pending |
| **Subsequent 0.x** | Extend and qualify the implemented query/inspection/logging/providers, complete compatibility and historical backend slices | Promote each implemented capability independently; preserve source, fixture and runtime evidence |
| **1.0.0** — agreed TUI/CLI product complete | Stable public CLI/output/settings contracts and closure of the agreed capability/platform scope | An experimental or unimplemented historical port does not become completed scope by relabeling it; a narrower 1.0 would require an explicit scope decision |

The current application is `0.4.0-dev.1`; **stable 0.4.0 remains a release goal**,
following NTop 0.3.x. Candidate names use `0.4.0-dev.N` and
`0.4.0-rc.N`, with stable `0.4.0`. The version number is independent of Actions
run numbers. `0.4.0-stable.N` would still be a SemVer prerelease and is not the
recommended stable naming scheme. The [release contract](spec/development/release.md)
owns promotion rules; the audit's release table maps closure work to existing IDs.

The original platform and installation ambitions remain visible. A portable-only
first release, or a modern-only 1.0, is a possible narrower milestone rather than
an implicit change to the agreed product goal. The native GUI remains deferred.

## Delivery principles

1. Preserve useful behavior while extracting responsibility-sized modules.
   Share equivalent semantics, not an artificial uniform API for every kernel.
2. Distinguish intended, implemented, tested and supported. Compilation and an
   import audit are useful but narrower than native execution and interaction.
3. Use one model for identity, units, selectors, actions and settings. Keyboard,
   mouse, CLI and later GUI consume those definitions.
4. Keep ordinary operation foreground, bounded and unprivileged where possible.
   Unsupported/denied observations remain visible instead of becoming zeros.
5. Introduce a new binary only when CPU, ABI, loader/CRT or a measured platform
   constraint requires it. Marketing-era labels do not decide binary boundaries.
6. A release gates a precise artifact and scope. Failed or unavailable historical
   lanes do not prevent independent progress on qualified current lanes.

## Phase A — Coherent source and evidence baseline

**Work:** WT-W000, WT-W010. **Contracts:** WT-001, WT-002, WT-030, WT-060.

Establish clean source ownership, reproducible current builds, early CLI dispatch,
compatibility handling and specification navigation. Keep historical artifacts and
observations attached to their source revision after the source move.

**Exit evidence:** a fresh checkout builds through documented entrypoints;
mode/argument/configuration conflicts fail predictably; help/version/metadata do
not initialize collectors or the console; structural validation and meaningful
host/native checks have recorded scope. The current broad work-unit status stays
open until its full acceptance is supported.

## Phase B — Htop-style everyday usability

**Work:** WT-W040 and the presentation portion of WT-W050. **Contracts:** WT-031,
WT-040, supported by WT-011/012/030.

Make the process table comfortable for daily use: htop default keys, optional
vi/vim and Emacs habits, common help/setup recovery, contextual last-row controls,
readable htop-derived colors, keyboard-complete mouse alternatives, real Main/I/O
views, and saved row/column/tab/sort preferences.

The interface remains table-first. A text-grid setup/help/confirmation screen is
part of the TUI, not the deferred native GUI. The function bar is shown by default
and can be hidden. Changing keymap changes input bindings and hints, not process
action semantics. Native Windows priority classes are named as such.

**Exit evidence:** independent key/action cases, bounded settings round trips,
mouse/layout hit tests, process-lifetime safety checks, and native console runs
cover navigation, prompts, resize, empty/changing lists, mouse failure, read-only
configuration, saving and normal restoration. Human readability remains a
separate observation from injected input success.

## Phase C — Shared model and reliable lifetime boundaries

**Work:** WT-W020, WT-W030. **Contracts:** WT-010, WT-011, WT-012, WT-061.

Continue extracting identity, metrics, model transformations and resource
ownership from inherited globals. Keep collection private until publication.
Define counter epochs, processor-group denominators, stale/missing states, action
revalidation and shutdown completion precisely. Repair specific blocking-query
risks through qualified providers or isolation where justified.

**Exit evidence:** independent counter/identity oracles, malformed-data and failure
fixtures, native permission/process-churn tests, and resource/lifecycle checks.
The existing emergency self-termination fallback is documented risk mitigation;
it does not prove that native calls are cancellable or that every abnormal exit
restores the console.

## Phase D — Unified query, inspection and logging

**Work:** WT-W035. **Contracts:** WT-030, WT-011, WT-012, WT-061.

Extend the existing snapshot surface with shared selectors and fields, bounded
inspection, runtime capability/provider diagnostics, and opt-in logging/export.
Noninteractive output stays versioned and parseable; error/cancellation/partial
results are explicit. TUI and CLI process controls use the same typed actions.

**Exit evidence:** independent output fixtures and actual native invocations cover
selection, permissions, Unicode, pipes, slow/broken consumers, bounded logs,
cancellation and cleanup. Document every new grammar/default/schema before it
becomes a compatibility promise.

## Phase E — Owned settings and delivery

**Work:** remaining WT-W050, WT-W060. **Contracts:** WT-040, WT-050, WT-063.

Promote the explicit reader/save mechanism into a qualified portable/per-user/
machine ownership model. Add migration and update/removal behavior only after
path precedence, foreign-file preservation and rollback are proven. Keep user
preferences outside package-manager payload directories.

Build standalone EXE and portable ZIP from the same application payload, then
add appropriate EXE/MSI setup and WinGet distribution for qualified modern
profiles. Historical copying/setup uses native facilities; there is no assumption
that MSI or WinGet can serve DOS, Win16, CE or WinPE. A self-install operation,
if added, remains explicit and respects the existing installation owner.

**Exit evidence:** acquired payload hashes match manifests; installation/update/
uninstall fixtures preserve sentinels, PATH and other installations; settings
survive as documented; packaging metadata describes actual architecture and
runtime dependencies. No package listing precedes these checks.

## Phase F — Qualify current targets and research historical families

**Work:** WT-W070, WT-W080, WT-W090, WT-W100. **Contracts:** WT-020, WT-060, WT-062.

Current x86, x64 and ARM64 are separate native execution lanes. Server/WinPE and
pinned ReactOS builds are environment qualifications, not automatic support
inherited from desktop compilation.

Research lanes prove narrow startup, rendering/input, enumeration, clock and
configuration prototypes before broad feature work:

| Research family | Boundary to prove |
| --- | --- |
| Legacy Win32 | Common x86 loader/import/CRT floor versus different NT/Win9x collectors |
| DOS 1.x / DOS 2+ / FreeDOS | Actual loader/file API/memory/task semantics and adapter limits |
| Early Win16 / Windows 3.x | Native text-grid host, cooperative message loop, resources and segmented memory |
| OS/2 16/32-bit | Native VIO/API/toolchain contracts and separate executable ABIs |
| CE/Mobile | Exact CPU/SDK/OEM image, available console and permitted observation/control |
| WinPE | Matching architecture/subset, optional components and explicit persistence |
| ReactOS | Pinned build behavior and precise API gaps |

This is a summary of research boundaries. Exact profiles and status remain in
[targets.json](spec/catalog/targets.json), including stock RT policy restrictions
and additional historical NT architecture research. Consolidate artifacts only
after the same bytes satisfy both target contracts.

**Exit evidence per lane:** compiler/SDK/CRT/import identity plus actual native or
explicitly qualified-emulator execution, interaction, metrics and cleanup. A
failed prototype records the limiting boundary and leaves support unclaimed.

## Phase G — Optional telemetry and measured optimization

**Work:** WT-W110. **Contracts:** WT-011, WT-012, WT-061, WT-062.

Add useful GPU, disk/network attribution, sensor/power, event and historical
providers without changing basic foreground operation into a persistent agent.
Each provider describes cost, permission, units, sampling state and actual
hardware coverage. Events/logging need bounded queues and storage.

Optimize after profiling sample/render cost across process counts, intervals,
terminal dimensions and transport. Changed-cell rendering and caching are useful
only if correctness and lifecycle remain intact. Record latency, CPU, allocation,
working set, handles and output bytes with units and dispersion.

**Exit evidence:** source-bound comparative measurements and native hardware
checks, with misses/drops/staleness and unsupported telemetry retained explicitly.

## Phase H — Release and continuing compatibility

**Work:** WT-W130. **Contracts:** WT-050, WT-062, WT-063.

Release only the qualified profiles and precise feature scope. Preserve source
and license correspondence, deterministic artifact naming, supported upgrade and
configuration paths, changelog accuracy, and acquired-byte verification. Keep
unqualified families visible as development goals without a support badge.

**Exit evidence:** the actual downloadable/installable artifact passes its import,
runtime, interaction, packaging, upgrade/removal and documentation checks. CI
artifacts and prereleases remain distinct from a supported stable release.

## Deferred native GUI

**Work:** WT-W120, explicitly deferred. A future native GUI must consume the same
model/actions/settings and respect each target's host/entrypoint constraints. It
does not justify adding another current runtime or a parallel implementation.
The current `--gui` stub remains an explicit unavailable result.

## Continuing a partial unit

Run `python tools/specctl.py validate`, `status` and `next`; inspect the active
source and evidence before editing. Choose the smallest remaining deliverable
with an available environment. Complete independent authorized work if one lane
is unavailable. Update the actual result and limitations, not an optimistic
percentage. The root documents are navigational projections; update the canonical
registry when work status genuinely changes.
