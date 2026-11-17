# Wtop TODO

This is the contributor checklist for the complete currently agreed product scope.
The [work registry](spec/work/units.json) owns work-unit status; the
[acceptance registry](spec/catalog/acceptance.json) owns qualification results.
This file explains concrete remaining work without creating a second authority.
Read [ROADMAP.md](ROADMAP.md) for sequencing and [spec/START-HERE.md](spec/START-HERE.md)
for the normative contracts.

A checked implementation or passing smoke test is not full acceptance. The initial
source/CLI foundation has recorded modern build and narrow native results in
[bootstrap verification](docs/bootstrap-verification.md). The current htop-style
presentation/settings increment has separate [native observations](docs/presentation-verification.md).
Historical observations
remain attached to the revision they describe. Unchecked items below are closure
criteria; some already have candidate code and need evidence rather than rewriting.

## 0. Current implementation and first-release gates

The current application is **0.4.0-dev.1**. The earlier
[17 November source audit](docs/wtop-release-readiness-2026-11-17.md) describes
`1ca153b77e7df203b9f6d1f8cfb2ff472318bc27`; the
[current implementation record](docs/implementation-verification-2026-11-17.md)
describes its follow-up. Checkmarks here mean candidate implementation, not full
acceptance or a supported release.

- [x] Source relocation, root TODO/ROADMAP, pinned NTop ancestry and canonical master.
- [x] Audit the named NTop forks and retain per-change reuse/rejection decisions.
- [x] **WT-W020:** exact process CPU deltas with measured intervals and named
  normalization; system commit from system totals; typed system summary/schema 2.
- [x] **WT-W030:** retained per-target action outcomes, lifetime revalidation,
  sample timestamps/freshness, private collection and single-slot publication.
- [x] **WT-W040:** htop defaults, vi/Emacs alternatives, contextual visible/hidden
  function bar, adjacent live status, graceful mouse and scalar-safe ASCII fallback.
- [x] **WT-W040/050:** Setup tab create/duplicate/rename/confirmed-delete/reorder,
  stable IDs and bounded saved columns/rows/filters/sort/colors.
- [x] **WT-W050:** version-2 settings, explicit owner/path resolution, byte-baseline
  conflict checks, cooperative lock, replacement verification and migration recovery.
- [x] **WT-W010/035:** checked CLI catalog/help, inherited `-d` compatibility,
  explicit delay units, query/inspect and finite complete-record JSONL logging.
- [x] **WT-W035/110:** isolated command-line/service/account/per-core providers,
  bounded protocol/capacity/deadlines and explicit retirement/admission state.
- [x] **WT-W060:** PE inspection, unchanged EXE/ZIP packaging, candidate MSI
  wrappers and direct ownership/refusal fixtures; promotion remains blocked.
- [x] **WT-W080/090:** shared historical probe/report code, DOS1 COM and DOS2+ MZ
  artifacts, NT probe and Win16/OS2 entrypoints. These are not full monitor ports.

Remaining first-release blockers and evidence work:

- [x] Close the observed native CI regressions and record exact passing
  source/artifact scope in the implementation record (run 44069558700).
- [ ] Qualify advertised Windows OS/architecture/console/token combinations;
  native ARM64, stock RT, WinPE, ReactOS and historical guests remain unverified.
- [ ] Qualify real CPU groups/hotplug, protected processes, Unicode/console hosts,
  hostile filesystem/path races, actual blocked work and resource/performance bounds.
- [ ] Close MSI preflight-to-operation races, upgrades/rollback/interruption,
  unelevated scope behavior and optional exactly owned PATH integration.
- [ ] Establish release version mapping, signing/source correspondence, actual
  download verification and WinGet publication only after payload qualification.
- [ ] Implement remaining CLI mutations/detail/thread/affinity/provider scope and
  historical monitoring capabilities toward the complete 1.0 goal.
- [ ] Map full acceptance cases to complete native evidence before promoting the
  work/target registries. A narrower passing fixture does not close a broad case.

GUI work stays deferred. The following sections retain the complete closure
criteria; many now have candidate code and need qualification, not a rewrite.

## 1. Source, specification and contributor baseline — WT-W000

- [x] Review the complete source move: application C/headers, compatibility config,
  build entrypoint and inherited package/site inputs reside in `source/`; root
  CMake, instructions, license, version and navigation remain discoverable.
- [x] Build from a fresh clone and an arbitrary working directory using root CMake
  and `source/build.bat`; keep outputs outside tracked source.
- [ ] Preserve upstream GPL notices, source identity and historical observations;
  do not rewrite old hashes to match new paths.
- [ ] Convert complete baseline acceptance into source-bound records, keeping
  partial CI observations separate from product qualification.
- [ ] Keep README, TODO, ROADMAP, help and catalogs synchronized when behavior
  changes. Remove stale promises and avoid duplicate feature inventories.
- [ ] Keep one owner per parser, setting, metric, native resource and action.

References: [governance](spec/foundation/governance.md),
[build](spec/development/build.md), [OKF profile](spec/foundation/okf-profile.md).

## 2. Current htop-style interaction — WT-W040

### Keymaps and discoverability

- [ ] Qualify htop as the default keymap, with explicit vi/vim and Emacs choices.
  Test conflicting meanings, especially htop `k` versus vi `k`.
- [ ] Verify the common F1–F10 route, help/setup recovery, navigation, tagging,
  following, search/filter, tree and sorting in every map and input context.
- [ ] Qualify keyboard hints, command execution and mouse hitboxes using shared action
  identities. A hidden/unsupported action must not leave an active click target.
- [ ] Verify Emacs modifiers and text editing independently of ordinary list
  actions; retain an unambiguous cancellation path.
- [ ] Preserve compatible explicit NTop commands without treating unknown input
  as executable shell text.

### Function bar, mouse and layout

- [ ] Qualify the default-visible last-row control bar, its hide/show preference,
  context-sensitive labels, transient errors and unsaved-settings indication.
- [ ] Test 40x25, 80x25, 80x50 and very small/rapidly resized windows. Clipped labels
  and hitboxes must remain aligned with actual rendered cells.
- [ ] Verify click selection, header sorting, wheel scrolling, tab selection and
  function-bar actions, including mouse disabled/unavailable and mode failure.
- [ ] Confirm caller input/output modes and screen buffer are restored on normal
  quit, Ctrl+C and console close. Record any emergency-shutdown exception.
- [ ] Exercise zero rows, shrinking lists, process churn and scrolling boundaries.
  No input may act on an unseen replacement row.

### Views, colors and process controls

- [ ] Qualify ordered/hidden header rows, process columns, named tabs, per-view
  sorting/tree/filter settings, tab visibility and restart round trips.
- [ ] Preserve inactive views when editing another view; keep live process rows
  out of configuration. Persist display rules, not stale process snapshots.
- [ ] Verify htop-style color roles, explicit NTop/monochrome alternatives, custom
  overrides, selection/tag/follow contrast and host-palette limitations.
- [ ] Keep total CPU, used memory and commit semantics honest; do not draw
  uncollected CPU subcategories or relabel commit as swap for visual similarity.
- [ ] Verify confirmed termination and supported Windows priority-class actions
  against the exact displayed process lifetime, including denied rights and exits.
- [ ] Add remaining htop-tier capabilities as distinct changes: thread/detail
  views, supported affinity, branch collapse, richer meters and explicit pause.
  Do not advertise them merely because corresponding keys exist in upstream htop.

References: WT-AC-031-01 through WT-AC-031-07 in
[acceptance](spec/catalog/acceptance.json), [TUI contract](spec/interfaces/tui.md).

## 3. Settings, themes and ownership — WT-W050

- [ ] Qualify bounded, transactional parsing for current settings and legacy
  color files: invalid known values must not partially change live settings.
- [ ] Exercise schema version, line/file/string limits, duplicate/unknown fields,
  duplicate columns, invalid sort/active tabs and unsupported future versions.
- [ ] Preserve comments and unknown inert records within the documented format
  limits. Fail rather than discard content that cannot be preserved safely.
- [ ] Verify CLI overrides and explicit `--write-config PATH` without collection.
  Normal startup, metadata commands and ordinary exit must not write settings.
- [ ] Qualify explicit TUI save destination disclosure, checked same-directory
  temporary write/replacement, failed-write preservation and temp cleanup.
- [ ] Verify explicit/adjacent/legacy/user precedence, legacy preservation,
  unavailable user-folder lookup and user-directory creation only on save.
- [ ] Qualify implemented concurrent-edit behavior for multiple instances and external editors;
  a byte comparison before replacement is not an atomic compare-and-swap.
- [ ] Test paths with spaces/non-ASCII characters, read-only media, denied paths,
  missing parents, full disks and interrupted replacement. Qualify customized
  file ACL/metadata preservation separately from byte-content preservation.
- [ ] Qualify the implemented installed/portable path resolver, ownership records,
  user/system precedence and migration without guessing from directory names or
  writability. Keep managed data outside package payload directories.
- [ ] Define and test settings compatibility across stable versions and side-by-side
  copies, including implemented schema-1 migration. Keep ordinary startup/exit read-only.
- [ ] Keep themes/keymaps declarative and inert; plugins or executable callbacks
  are not needed for ordinary customization.

References: WT-AC-040-01 through WT-AC-040-04,
[settings policy](spec/configuration/settings.md).

## 4. Invocation and shared metric model — WT-W010 / WT-W020

- [ ] Complete parser/conflict/alias tests with independent expected results;
  presentation preferences must not silently alter one-shot data semantics.
- [ ] Preserve early help/version/build-capabilities paths with zero collection
  and zero console/config writes.
- [ ] Finish responsibility-sized extraction of the typed snapshot and selectors
  from inherited native globals, keeping working behavior at each step.
- [ ] Qualify documented CPU denominator, processor groups/topology, counter epoch and sample
  timestamp precisely. Test first sample, reset, wrap, missing data and zero time.
- [ ] Preserve process creation identity and per-field availability/provenance;
  do not invent SYSTEM ownership or zero utilization for failed observations.
- [ ] Verify text/JSON/TUI projections use the same measured values and units,
  with exact large integers, finite rates and inert external strings.
- [ ] Define schema evolution and partial-output/broken-pipe outcomes before
  additional machine-readable formats or consumers depend on them.

References: [invocation](spec/interfaces/invocation.md),
[model/metrics](spec/architecture/model-metrics.md), WT-AC-010/011/030 families.

## 5. Lifecycle and safe process actions — WT-W030

- [ ] Verify one bounded collection owner and no overlapping unbounded samples;
  slow providers must expose stale/missed results rather than spawn more work.
- [ ] Test start/stop/resize/close/error interleavings and all allocation/handle
  failure paths with explicit ownership and reverse-order cleanup.
- [ ] Replace or isolate potentially blocking queries only with a qualified
  design. A flag or timeout is not preemption.
- [ ] Fault-test the existing 3000 ms cleanup grace and 3500 ms control callback
  fallback. Document self-termination/console-restoration limits accurately.
- [ ] Strengthen the Toolhelp snapshot/open identity boundary where feasible;
  the wall-clock cutoff is not an atomic kernel generation guarantee.
- [ ] Revalidate selected/tagged process identities immediately before mutation;
  verify confirmations cannot silently change targets while the list refreshes.
- [ ] Qualify retained per-target outcomes for bulk actions. Preserve least privilege and
  never create a service, driver or host policy change merely to observe processes.
- [ ] Measure repeated-instance CPU, allocations, handles and cleanup. Any optional
  single-instance behavior must remain user/session scoped and not block scripts.

References: [lifecycle](spec/architecture/lifecycle.md),
[safety](spec/development/safety.md), WT-AC-012/061 families.

## 6. Query, inspection, diagnostics and logging — WT-W035

- [ ] Extend the implemented shared selector grammar with field selection for read-only query and
  process/system inspection; distinguish invalid selectors from empty results.
- [ ] Extend implemented path/provider diagnostics to effective configuration and full runtime
  capability negotiation, distinct from current build-capability metadata.
- [ ] Qualify implemented finite logging with interval, duration, destination, complete-record
  size cap, cancellation and partial outcomes; rotation remains a separate extension.
- [ ] Keep output schemas/units stable and diagnostics off stdout; do not prompt
  or initialize a TUI when input/output is redirected.
- [ ] Define supported process-control subcommands using the same typed actions
  and identity checks as the TUI, with deliberate mutation syntax.
- [ ] Test pipes, non-console execution, Unicode, slow consumers, denied data,
  output failures and termination without orphaned collectors.

References: [command catalog](spec/catalog/commands.json),
[invocation](spec/interfaces/invocation.md), [metrics](spec/catalog/metrics.json).

## 7. Distribution and installation — WT-W060

- [ ] Produce one qualified application payload per ABI/CPU/API profile and reuse
  those bytes in standalone EXE, portable ZIP and appropriate setup packages.
- [ ] Qualify implemented portable/per-user/machine ownership and registry changes; add
  optional exactly removable PATH integration. No implicit first-run installation.
- [ ] Test update, rollback, side-by-side versions, foreign/modified files,
  locked executables and interrupted install/uninstall.
- [ ] Keep preferences outside WinGet/MSI payload ownership; provide explicit
  remove-settings behavior without deleting arbitrary parent directories.
- [ ] Define stable/prerelease/artifact names, version-to-MSI mapping and source
  correspondence; reject collisions or unsupported version ranges.
- [ ] Create exact-architecture WinGet manifests only after the acquired payload
  is qualified. Do not repeat NTop's inspected neutral/x64 mismatch.
- [ ] Document GitHub downloads with exact profile/digest and offline-copy paths
  for historical hosts. Do not require remote-script evaluation to install.
- [ ] Retire or explicitly replace inherited `source/ntop.nuspec` before any
  Chocolatey publication; its legacy metadata is not a Wtop release manifest.

References: [packaging](spec/distribution/packaging.md),
[installation guide](docs/installation.md), WT-AC-050/063 families.

## 8. Native and historical platform qualification — WT-W070 / WT-W080 / WT-W090 / WT-W100

- [ ] Qualify each current native architecture on its actual OS/CPU. x86 under
  x64 Windows and an ARM64 cross-build are narrower evidence.
- [ ] Record exact toolchain, SDK/CRT/imports, executable machine/subsystem, OS
  build/service pack, optional components and source/artifact hashes.
- [ ] Prototype the legacy Win32 loader/API floor before splitting executables
  by marketing era; keep Win9x and NT collection semantics separate.
- [ ] Prove DOS 1.x startup/FCB/loader behavior separately from DOS 2+ and FreeDOS.
  Expose relevant task/memory information without inventing NT process concepts.
- [ ] Prove early Win16 and Windows 3.x text-grid hosts, message-loop ownership,
  segmented-memory constraints and telemetry separately.
- [ ] Prototype OS/2 16/32-bit native VIO/API contracts and CE's exact CPU/SDK/image
  combinations. Do not treat all CE/Mobile devices as one desktop Win32 target.
- [ ] Qualify WinPE subset, external persistence and optional components, plus
  Server and pinned ReactOS builds. Record stock RT deployment restrictions.
- [ ] Consider non-x86 historical NT architectures only as explicit research
  targets with available toolchains/hardware; do not imply their support.
- [ ] Establish Unicode/code-page, keyboard/mouse and configuration limits per
  adapter. Capability degradation must describe what actually remains usable.

References: [canonical targets](spec/catalog/targets.json),
[platform policy](spec/platform/targets.md), WT-AC-020/060 families.

## 9. Optional providers and performance — WT-W110

- [ ] Add GPU, disk/network attribution, sensors, power or event/history providers
  only with explicit units, cost, permissions, provenance and actual hardware tests.
- [ ] Distinguish device totals, process-attributed values and inferred values.
  Avoid a vendor/driver requirement becoming a silent baseline dependency.
- [ ] Define any ETW/event/history collection lifecycle, queue/drop limits and
  opt-in logging/storage policy; no permanent agent for ordinary TUI use.
- [ ] Measure sample/render cost, CPU time, peak memory/handles and output bytes
  across process counts, intervals, terminal sizes and transports.
- [ ] Investigate changed-cell rendering and provider caching only after profiles
  identify real bottlenecks; retain semantics and baseline measurements.
- [ ] Keep multi-group CPUs, hotplug, protected processes and unusual clocks in
  the qualification matrix; fixtures alone do not certify real hardware.

References: [verification](spec/development/verification.md),
[metric catalog](spec/catalog/metrics.json), WT-AC-062 families.

## 10. Release, maintenance and deferred GUI — WT-W130 / WT-W120

- [ ] Close the exact payload's release gates, source/notice obligations,
  acquisition verification and truthful platform support table.
- [ ] Publish reproducible artifacts and changelogs that distinguish fixes,
  compatibility changes, implemented features and unqualified targets.
- [ ] Test upgrades and configuration migrations from every supported stable
  predecessor; preserve failure records and regression fixtures.
- [ ] Keep GUI reserved until explicitly scheduled after model/lifecycle and
  settings contracts mature. Its future host must reuse existing actions.
- [ ] Audit this checklist against the canonical catalogs when scope changes;
  do not mark a work unit complete merely because its document exists.
