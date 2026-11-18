# Wtop 0.4.0 Beta 2 — implementation and release plan

**Status:** Proposed implementation plan, not completed work or a support claim.
**Planning date:** 18 November 2026, Melbourne (AEDT).
**Wtop baseline:** `f0d02559a5795361c28170d2f75417ff2d73096a` on `master`.
**Universal Setup inspected:** `d0a2bcbf1aff6beaac46b5a778fbc315ad8cf683` on `main`.
**Next product version:** `0.4.0-beta.2`.
**Next tag:** `v0.4.0-beta.2`.
**Release title:** `Wtop 0.4.0 Beta 2`.
**Scope exclusions:** Rich native GUI; new non-MSI EXE installer implementation before Beta 3.

## 1. Decisions carried forward

| Decision | Contract |
| --- | --- |
| One shared application source | Share metric, identity, settings, command, layout and action semantics; isolate OS mechanisms. |
| Htop-like presentation | Preserve the dense process table, htop colors, default-visible contextual footer and optional vi/Emacs controls. |
| CLI and TUI | `wtop` defaults to interactive TUI; explicit query, inspection, dump and finite logging remain available. |
| Alpha/beta publication | Agents may publish these as GitHub prereleases after automated safety, provenance and artifact gates. |
| Stable publication | Keep the release in draft until a human approves trials of the exact final candidate bytes. |
| Existing evidence | Removing mandatory human review from beta does not turn missing review into a pass. |
| Packaging | Prefer ZIP and MSI; defer a new EXE setup/repair/uninstaller engine to Beta 3. |
| Portable payload | Reuse the same finalized application executable within its profile's standalone, ZIP and MSI carriers. |
| Compatibility | Publish tested profile capabilities; do not claim universal support from compilation alone. |
| Published bytes | Never replace released binaries or move published tags to different source. Editorial metadata is separate. |

The release API confirms Beta 1 is public, `prerelease=true`, titled **Wtop 0.4.0 Beta 1**, and tied to the baseline commit.
Publication occurred at `2026-11-17T22:59:46Z`, or **18 November 2026, 09:59:46 AEDT**. [S1]
The inspected repository still has Issues and Discussions disabled. Establish a usable public feedback channel before Beta 2. [S2]

## 2. Release presentation specification

### 2.1 Separate machine identity from reader-facing text

| Field | Example |
| --- | --- |
| Semantic version | `0.4.0-beta.2` |
| Git tag | `v0.4.0-beta.2` |
| Release title | `Wtop 0.4.0 Beta 2` |
| Artifact filename | `wtop-0.4.0-beta.2-win-nt-x64-portable.zip` |
| Asset display label | `Wtop 0.4.0 Beta 2 for Windows NT (x64, 64-bit) Portable` |
| Download-table text | `Windows NT — x64 Portable (ZIP)` |

Generate these from one metadata model. Do not split the version string in independent workflows.
GitHub release assets have a `label` field; filenames can stay URL-safe and stable. [S3]
Use labels on uploaded assets and explicit friendly Markdown link text in the release body.
Verify the actual rendered release page; do not assume every GitHub client presents labels identically.

### 2.2 Release body rules

- Do not repeat the release title as the body's first heading.
- Use `Downloads`, `Changes`, `Known limitations`, and `Verification`.
- Prefer a small download table and short single-purpose bullets.
- Keep normal sentences and bullet text within roughly 80–160 characters when practical; shorter is acceptable.
- Do not insert source line breaks in the middle of sentences.
- Exempt URLs, code and necessary table structure from the sentence-length lint.
- Link long audit records rather than embedding their full prose.
- Report implementation, verified behavior and remaining limitations separately.
- Do not include a download link for a file that was not built and uploaded.

Proposed lint tests: version/title mapping, duplicate opening heading, long rendered sentences, asset-label completeness, supported-target consistency, correct link destinations and absence of draft-only instructions in a public prerelease.

### 2.3 Suggested asset labels

These are naming templates, not a claim those builds already exist.

| Profile | Friendly label |
| --- | --- |
| `win-nt-x86` | Wtop 0.4.0 Beta 2 for Windows NT (x86, 32-bit) Portable |
| `win-nt-x64` | Wtop 0.4.0 Beta 2 for Windows NT (x64, 64-bit) Portable |
| `win-nt-arm64` | Wtop 0.4.0 Beta 2 for Windows NT (ARM64, 64-bit) Portable |
| `win9x-x86` | Wtop 0.4.0 Beta 2 for Windows 95–Me (x86, 32-bit) Portable |
| `win16-3-x86` | Wtop 0.4.0 Beta 2 for Windows 3.x (16-bit x86, DOS-based) Portable |
| Corresponding MSI | Same product/platform label followed by `Setup (MSI)` |
| Source | Wtop 0.4.0 Beta 2 — Source code |
| Evidence | Wtop 0.4.0 Beta 2 — Build and test evidence |
| Checksums | Wtop 0.4.0 Beta 2 — SHA-256 checksums |

Different NT compatibility floors must be distinguishable in labels when more than one NT binary exists.
Do not call x64 “Intel-only”; it is the x86-64/AMD64 architecture.
Do not call a command-line EXE a setup EXE.

## 3. Publication policy

Replace beta-only hardcoded policy with channel-aware, independently tested policy.

| Condition | Alpha/beta | Stable |
| --- | --- | --- |
| Agent publication | Authorized after required checks | Stage draft only until human approval |
| Mandatory human trial | No | Yes, bound to final artifact identities |
| Automated safety/correctness tests | Required for included functionality | Required |
| Runtime coverage | Disclose actual tested and experimental scopes | Complete advertised support scope |
| Hash/source/license checks | Required | Required |
| Known destructive defect or invalid signature | Blocks affected artifact | Blocks release |
| GitHub prerelease flag | True | False only on approved promotion |
| Latest release flag | False | Explicit stable policy |
| Signing status | Honest, verified, profile-specific | Approved stable signing policy |
| Feedback | Working public channel | Working public channel |

Update `tools/beta_release.py`, `tools/publish_beta.py`, `.github/workflows/beta.yml`, their tests, release specification and desktop-acceptance documentation together.
Current source hardcodes x64 beta receipts and requires a desktop receipt before any public beta. [S4]
Retain the human-review tool for optional beta feedback and mandatory stable acceptance.
Do not synthesize `desktop-receipt.json` to satisfy an obsolete gate.

Use the exact selected candidate revision, not “whatever master contains when publish starts.”
Permit publication only from trusted canonical workflows; untrusted PRs cannot access signing credentials.
Do not weaken repository protections or signing-service policies to implement agent prereleases.

## 4. Compatibility and binary profiles

### 4.1 Clarify the requested families

“Windows DOS 1.0–3.11” is interpreted as **Windows 1.x–3.11 running on DOS**, not the separate MS-DOS 1.x–8.x family.
Retain a separate DOS/FreeDOS target for actual DOS operation.

A Windows NT family label does not imply every architecture existed on every NT release.
Modern ARM64 desktop Windows, Windows RT ARM32 and Windows CE ARM32 require different target contracts.
An MSI carrier cannot make an incompatible application binary run.

### 4.2 Proposed Beta 2 matrix

| Family/profile | Development objective | Portable | MSI | Release condition |
| --- | --- | --- | --- | --- |
| Modern NT x64 | Primary Windows 10/11 and named Server lane | Yes | Yes, after lifecycle gates | Native x64, ordinary-user, MSI and signed-byte tests |
| Modern NT x86 | Windows 10 x86 plus documented WoW64 environments; older floors separately tested | Yes | Yes, after lifecycle gates | Actual x86 OS claims need x86 OS execution |
| Modern NT ARM64 | Native Windows 10/11 ARM64 lane | Yes | Yes, after native validation | Use actual ARM64 runner, not compilation alone |
| Legacy NT x86 | Work backward through XP/2000/NT4, then NT3.x | Goal | Runtime-specific | Each minimum-version claim independently demonstrated |
| Legacy NT x64 | XP x64/Server 2003 x64 research | Goal | Runtime-specific | Separate ABI/import/CRT testing |
| Win9x x86 | Actual 95/98/Me process/task collector and classic console host | Goal | Legacy MSI profile possible | Tested on the named 9x images, no modern-only imports |
| Windows 3.x Win16 | Real task enumeration and minimal character-grid host | Goal | No native MSI | Actual NE startup, cooperative UI and bounded task behavior |
| Windows 1.x/2.x | Earlier ABI/host feasibility and useful inspector | Research, promote when working | No native MSI | Dedicated loader/API/memory-model evidence |
| DOS/FreeDOS | Native memory/task/TSR inspection with truthful limits | Separate goal | No native MSI | Actual DOS image execution, appropriate file/startup APIs |
| WinPE | Matching-architecture portable rescue operation | Yes when tested | Not supported by WinPE | Exact image/components and persistence tests |
| ReactOS | Test suitable NT profile against pinned builds | Best effort | Separate test | Record real API gaps rather than infer Windows equivalence |
| OS/2 | Native VIO/ABI research and later usable monitor | Goal | No native Windows MSI engine | Separate LX/NE application and delivery backend |
| CE/Mobile | Exact CPU/SDK/OEM-image research | Goal | Not desktop MSI | Do not conflate CE ARM32 with desktop ARM64 |
| Stock Windows RT | Preserve supported deployment restriction | Not a normal third-party desktop lane | Do not promise | No invented supported desktop-install route |

The current Wtop CMake configuration targets Windows 7-era Win32 headers, and the existing full monitor has one NT adapter.
The legacy directory contains probes, not full old-OS monitors. [S5]
Open Watcom supports relevant 16/32-bit target families, but a compiler target is not application qualification. [S6]

**Recommended delivery boundary:** finish three modern architecture lanes and their eligible MSI carriers first.
Attempt the named legacy slices in parallel; publish only those that actually operate, clearly describing reduced capability.
Do not publish a probe as “Wtop Portable,” or claim NT 3.1–11 because NT-derived versions share a name.
Requiring the entire historical matrix in Beta 2 makes it a major porting milestone, not just a polishing release.

### 4.3 How to consolidate binaries safely

Start with family/ABI profiles, then merge profiles only after the same bytes pass both endpoints and boundary-version tests.
Audit startup imports, delay imports, CRT requirements, instruction baseline, subsystem version, encoding and loader behavior.
Resolve optional APIs inside a centralized platform adapter.
A feature can degrade only after the executable has loaded: an unsupported static import can prevent all negotiation.

Modern improvements remain in shared metric/action/layout code.
Legacy adapters return `unsupported`, `denied`, `warming_up` or other defined states rather than fabricated values.
On older targets, do not promise modern process identity, CPU or I/O semantics that the kernel cannot supply.

## 5. Application hardening and usability

### 5.1 Required hardening

| Area | Work and acceptance |
| --- | --- |
| Process identity | Recheck lifetime at action boundaries; preserve mixed per-target outcomes and prevent stale selection retargeting. |
| Metrics | Independent counter vectors; commit/physical separation; CPU normalization across views; stale and denied sorting. |
| Providers | Single-owner lifecycle, bounded queues/caches, explicit retirement; no restart while a prior helper remains unretired. |
| Shutdown | Close/Ctrl+C/error/timeout tests; restored terminal ownership; no surviving owned helper after confirmed retirement. |
| Settings | Preserve Beta 1 files, stable view IDs, unknown safe records, external edits, failed saves and backup/recovery paths. |
| Output | Complete JSON/JSONL records; broken/slow pipe behavior; cancellation; stderr-only diagnostics. |
| Text/layout | Shared measure/clip/hit-test geometry; invalid/control text; narrow/short/rapid-resize recovery. |
| Multiple instances | Isolated workers/handles, settings conflict behavior, bounded total resource use; no permanent daemon. |
| Support reports | Minimal metadata by default; no process arguments, environment or private paths without explicit selection. |
| NTop regressions | Large-memory sorting, non-ASCII accounts, unresolved owners, access denial, remote sessions and long names. |

No “zero bugs” guarantee. Closure requires evidence for the changed behavior and a visible list of untested conditions.
Do not hide a failing test by changing its expected result to match the implementation.

### 5.2 Beta 2 feature completion

Prioritize coherent features that reuse the existing model.

1. Pause/resume the displayed snapshot, with explicit frozen age and identity-safe actions.
2. Collapse/expand process subtrees and retain collapse state by process lifetime.
3. Add a wrapped details view for full executable path, command line and associated services.
4. Add interactive user selection and shared name/command filtering with explicit field/case semantics.
5. Add process appearance/exit highlighting with bounded expiry and no confused PID reuse.
6. Extend a shared field registry for column labels, units, sorting, help and CLI field discovery.
7. Keep htop defaults; test vi/Emacs editor boundaries and context-sensitive footer/hitboxes.
8. Improve Unicode progressively: preserve raw data, support proven display cells and retain a safe fallback.

Thread enumeration and group-aware affinity are explicit additional increments, not automatic consequences of those features.
They should not be rushed into a correct release just to claim complete htop parity.
GPU, tracing, stacks, event-history subsystems and the rich GUI remain later work.

## 6. MSI strategy

### 6.1 Build on the existing implementation

Wtop already has WiX authoring, an ownership guard, a build script and disposable-host installation tests.
The package currently requires Windows Installer 5.0, refuses upgrades and has incomplete ownership/rollback qualification. [S7]
Reuse this work; do not copy a second installer engine from an unqualified fork.

The inspected NTop `ntop.nuspec` is a Chocolatey package for `ntop.portable`, not a complete MSI or EXE installer implementation. [S8]
It can inform metadata and package behavior, not provide missing repair/uninstall machinery.

### 6.2 MSI cannot cover all requested systems

Microsoft documents Windows Installer 2.0 for Windows 95/98/Me, NT4 SP6 and Windows 2000. [S9]
This makes a separately authored legacy MSI possible, not the current MSI 5.0 package compatible with 9x.
MSI 5.0 dual-purpose per-user/per-machine redirection is not an older-engine capability. [S10]
Windows 1.x–3.x and NT3.x need portable copying now; custom non-MSI setup remains Beta 3.
WinPE explicitly does not support MSI installation. [S11]
A Windows-hosted transfer tool for another OS is not a native installer for that OS.

### 6.3 Modern MSI acceptance

- Exact signed EXE bytes match the portable carrier.
- Per-user installation succeeds without elevation on an ordinary account.
- Machine installation requests required rights and writes only its owned locations.
- Upgrade, downgrade refusal, repair, uninstall and interrupted-operation recovery are deterministic.
- Preserve preferences, unrelated files, foreign registrations and modified owned files according to a documented policy.
- Close validation-to-operation races or use a protection strategy demonstrated to preserve the promised boundaries.
- Revalidate from direct `msiexec`, not only through a wrapper.
- Handle an already-running Wtop without silently killing unrelated instances.
- Test spaces, non-ASCII paths, long paths, denied paths and explicit installation directories.
- Roll back owned PATH changes; never rewrite or truncate unrelated PATH entries.
- Test both architecture coexistence and beta/stable coexistence deliberately.
- Avoid a product-specific permanent service/updater for installing a small monitor.

MSI `ProductVersion` has three numeric fields; a fourth is ignored. [S12]
Define one version ledger for semantic version, native numeric file version, MSI version, product code and upgrade family.
Do not use the same MSI version for Beta 1, Beta 2 and stable or silently strip the suffix.
Choose a bounded monotonic mapping or persisted release ordinal; validate transitions and overflow in tests.
The displayed product version remains the requested semantic/human version.

Review the pinned WiX 4.0.6 toolchain before reuse; WiX 4 is outside community support.
A controlled upgrade needs package-behavior regression tests. A newer build tool does not imply a higher target OS floor. [S13]

## 7. Universal Setup reuse

The inspected Universal Setup main README describes product-independent install/verify/repair/uninstall/rollback authority.
It exposes a versioned C ABI and a relocatable SDK, but reports failed packaged startup and owned-service retirement in its publisher qualification.
Its implementation uses C++17 and modern Windows facilities. [S14]
Its authoring specification describes a deterministic bundle compiler as engineering intent, not a qualified shipped MSI/legacy EXE generator. [S15]

Recommended integration:

| Boundary | Owner |
| --- | --- |
| Wtop version, capabilities, profiles, files and preservation policy | Wtop product descriptor |
| Generic installed-state, planning, verification and transaction semantics | Universal Setup contracts |
| MSI tables and Windows Installer lifecycle | Qualified MSI adapter |
| Scoop/WinGet/Chocolatey package metadata | Thin generated channel adapters |
| Non-MSI EXE lifecycle | Beta 3 Universal Setup consumer experiment |
| Monitoring runtime | Wtop only; no mandatory Universal Setup service |

Perform a pinned, bounded consumer proof before adopting the provider.
Do not embed a second mutation authority inside MSI custom actions and allow both to manage the same installation.
Do not convert a C ABI into a false claim that its C++17 implementation runs on Win16 or NT3.1.
Keep Universal Setup out of Wtop's monitoring execution path.
If the authoring adapter is missing, implement and test it in its generic owner rather than copying engine code into Wtop.

## 8. Installation channels, locations and settings

Locations below are proposed Wtop defaults or documented package-manager conventions, not hardcoded paths for every historical system.

| Method | Executable ownership/location | Settings | PATH |
| --- | --- | --- | --- |
| Manual portable ZIP | Chosen folder | Explicit `--portable` root or normal user settings | No automatic modification |
| Per-user MSI | User programs known folder / Wtop / profile | User AppData | Optional owned user entry |
| Machine MSI | Program Files / Wtop / profile | Per-user preferences; machine defaults separate | Optional owned machine entry |
| WinGet MSI | Delegates to MSI | Same MSI/app policy | MSI policy |
| WinGet portable | Manager-controlled package root | User AppData, outside replaceable package tree | Manager's Links facility |
| Scoop | `%USERPROFILE%\scoop\apps`, or configured/global root | User AppData or explicit Scoop persist contract | Scoop shims |
| Chocolatey portable | Manager package tree | User AppData, not versioned payload directory | Chocolatey shims |
| Chocolatey MSI | Delegates to MSI | Same MSI/app policy | One servicing owner, no duplicate shim/PATH strategy |
| Historical manual copy | User-chosen 8.3-compatible directory where required | Explicit text config | Optional instructions; never silently edit AUTOEXEC.BAT |

WinGet portable roots, Scoop roots/persistence/shims and Chocolatey shimming are documented by the respective projects. [S16–S18]
These managers distribute/invoke packages; they do not add compatibility to unsupported application binaries or exempt them from security scanning.

For Beta 2:
- Generate a first-party Scoop beta manifest against exact public ZIP hashes.
- Generate WinGet manifests with exact architecture, minimum OS, scope and silent arguments.
- Keep beta/stable selection explicit; do not cause stable users to receive beta automatically.
- Replace inherited NTop Chocolatey metadata with distinct Wtop identity; preserve attribution but do not impersonate the upstream package.
- Check namespace availability and community submission rules before advertising install commands.
- Distinguish locally validated manifest, submitted manifest and actually accepted public package.
- Preserve installed preferences during upgrade/uninstall; make removal of data an explicit, separately scoped operation.
- Extend `--paths`/diagnostics to show owner, effective settings path, profile and relevant package registration.
- Keep direct GitHub download and offline-copy instructions available without package managers.

## 9. Signing, reputation and antivirus

### 9.1 Diagnose before calling it a false positive

The screenshot attachments show release formatting, not the security alert.
No exact detection name, scanner verdict or blocked-file hash was supplied.
Therefore the cause of the user's block remains unverified.

| Observation | Meaning to investigate | Response |
| --- | --- | --- |
| Unrecognized/uncommon download | SmartScreen file/publisher reputation | Authenticode identity and reputation; distinguish from malware |
| “Unknown publisher” | Missing/untrusted signature | Verify publicly trusted signature and chain |
| Defender threat name/quarantine | Malware/PUA classification | Inspect exact artifact and submit possible false-positive report |
| Smart App Control block | Execution trust decision | Verify signing and actual device policy |
| Organizational App Control block | Admin policy | Administrator review; no application bypass |
| Hash mismatch | Wrong/corrupted/substituted artifact | Stop and investigate provenance |

Signing is not a clean-malware certificate.
Microsoft states that newly signed files can still trigger SmartScreen and EV no longer provides an automatic bypass. [S19]
Do not instruct users to disable Defender, add blanket exclusions, alter extensions, encrypt downloads to evade scans or ignore actual malware verdicts.
Do not recommend stripping download metadata as the distribution solution.

### 9.2 Signing options for this project

| Route | Suitability | Constraint |
| --- | --- | --- |
| Azure Artifact Signing Public Trust | CI-friendly modern Windows signing | Current quickstart permits Australian organizations; individual validation is US/Canada only |
| SignPath Foundation | Potential free OSS route | Approval, reputation, fork/upstream, review and signing-approver conditions; not guaranteed |
| CA-issued individual/OSS certificate | Alternative for an Australian individual | Verify country eligibility, identity validation, hardware/cloud signing and automation requirements before buying |
| Microsoft Store | Possible later distribution | Separate packaging/certification; not a replacement for the portable/legacy matrix |
| Self-signed certificate | Controlled local testing only | Not a public trust/reputation solution |

Use the Artifact Signing quickstart for the current geographic list; some overview pages have older organization lists. [S20]
SignPath explicitly imposes review and upstream-source constraints; do not assume a newly forked project is eligible. [S21]
Certum advertises OSS signing products, but purchase suitability and CI authorization need direct confirmation. [S22]
Enrollment, identity checks, billing and provider approvals are external prerequisites, not fabricated agent accomplishments.

### 9.3 Final-byte signing pipeline

1. Build from pinned source, compiler/SDK/dependencies and fixed resource metadata.
2. Run portable/native tests on the build.
3. Sign and timestamp eligible PE payloads using a restricted signing identity.
4. Verify the signature, expected signer, timestamp and chain.
5. Re-run required smoke/native tests on those signed payload bytes.
6. Build ZIP/MSI from the same signed EXE.
7. Sign the MSI separately and test extracted/installed EXE equality.
8. Scan the final artifacts with recorded engine/signature versions.
9. Generate hashes/manifests for final signed/carrier bytes.
10. Upload, reacquire and verify public artifacts without rebuilding.

A timestamped signing transformation changes hashes; preserve pre/post identities rather than asserting deterministic signed bytes.
Use a standard modern digest and verify oldest claimed OS trust compatibility separately.
Do not weaken modern signing to accommodate old images.
For formats without normal Authenticode support, use honest unsigned status plus separately verifiable signed manifests on a modern host.
Code signing private keys remain outside chat, source control and ordinary PR jobs.

For a suspected Defender false positive, submit the exact final EXE via Microsoft's software-developer submission route.
Record the SHA-256, product, detection name, definition version and submission ID.
A clean result is time-bound evidence, not a lifetime guarantee. [S23]
For new-file reputation alone, do not promise that a malware submission will create consumer SmartScreen reputation. [S19]

## 10. Implementation work packages

IDs below are proposed Beta 2 work-package labels; reconcile them into the existing OKF catalogs rather than duplicating registries.

| ID | Work | Principal files/boundary | Exit evidence |
| --- | --- | --- | --- |
| B2-01 | Release policy and readable metadata | Release specification, channel policy, publisher, generated notes | Alpha/beta publish without human receipt; stable refuses without approval; label/title golden tests |
| B2-02 | Profile-driven packaging | Target/artifact catalogs, PE/NE/MZ inventory, package builder | No x64-only assumptions; no mislabeled binaries or nonexistent links |
| B2-03 | Signing and security triage | Signing workflow, metadata receipt, support tooling | Exact signature/status/scan record; no secret-bearing or untrusted signing path |
| B2-04 | Core and settings regressions | Metrics/actions/store/provider adapters | Failure injection, mixed outcomes, identity churn, Beta 1 migration and no data loss |
| B2-05 | TUI/CLI completion | Shared fields/actions/text/layout | Pause, tree collapse, details/filter behavior and consistent projections |
| B2-06 | Native architecture tests | Windows x64/x86 and `windows-11-arm` workflows | Actual matching-architecture receipts, ordinary-user fixtures, recorded limits |
| B2-07 | Legacy NT and Win9x adapters | Central host/provider abstractions, exact compiler profiles | Startup and useful process view on named images; prohibited imports rejected |
| B2-08 | Win16/DOS-based Windows | Win16 host and task provider, segmented-memory tests | Real character-grid task view; earlier versions remain explicit until tested |
| B2-09 | MSI lifecycle | Existing WiX/guard/build adapter | Install, upgrade, repair, uninstall, rollback, modified/foreign-file and token tests |
| B2-10 | Universal Setup consumer study | Pinned contracts/adapter proof | Reuse/adapt/defer decision with exact source and no mandatory monitor dependency |
| B2-11 | Distribution channels and docs | Scoop/WinGet/Chocolatey projections, acquisition, TODO/ROADMAP | Tested generated manifests, exact owner/settings/PATH behavior, truthful availability |
| B2-12 | Freeze and publish | Final-byte receipts, security checks, publication | Correct title/body/labels; prerelease flags; final public download verification |

Critical path: B2-01/02 plus B2-04/05 → B2-06 → B2-09/11 → B2-12.
Signing enrollment begins early and runs in parallel.
Legacy platform work is separately owned; failed optional profiles cannot contaminate a passed modern package.
Public release of a subset must explicitly list omitted requested targets, not imply the original request was completed.

## 11. Validation strategy

### Cheap continuous checks

Pure arithmetic, parse/config migration, field descriptors, release metadata, action transitions, binary parsers and package-manifest fixtures.
Fuzz malformed configuration, long/control text, CLI quoting and provider frames.
Sanitizers apply where the toolchain supports them; never substitute a modern run for 16-bit width testing.

### Native profiles

Use x64 Windows runners and actual ARM64 `windows-11-arm` runners.
GitHub documents native ARM64 runners; earlier compile-only evidence need not remain a permanent limitation. [S24]
Run ordinary-user fixtures under a demonstrably non-elevated token.
Use appropriate Windows client/Server images for support statements; ARM64 Windows 11 is not Windows 11 x64 evidence.

Test:
- 40x25, 80x25, 80x50, 120x40, minimum dimensions and repeated resize.
- Keyboard-only, mouse enabled/disabled, htop/vi/Emacs, redirection and remote-session behavior.
- Memory over 4 GiB, denied/protected tasks, SID/account resolution, process/thread churn and PID reuse.
- Sleeping/broken pipes, stopped providers, slow storage, interrupted save and repeated start/exit.
- Multiple Wtop instances, helper process count, handle/allocation growth and CPU/sample/output cost.
- 10-minute smoke and 60-minute soak as proposed qualification workloads, not measured guarantees.
- Current and Beta 1 configurations, unknown fields, concurrent writers and readonly destinations.

Legacy VMs/emulators need exact image/version/configuration, lawful image acquisition, CPU mode, memory, code page and expected degraded capabilities.
Keep unsupported OS test machines isolated from production networks.

### Artifact and installation checks

Inspect executable format/machine/imports/CRT and stated OS floor.
Check real signed payloads after browser-like acquisition and package-manager install.
Test user/machine scope, upgrade order, rollback, unavailable installer runtime and preservation.
No installer can ship on the strength of an EXE smoke test alone.

### Release acceptance

Publish a per-profile table: built, binary-inspected, executed, signed, scanned, package-tested, limitations.
Archive exact receipts and superseded failures.
Agent prerelease authority does not permit deleting failures, inventing human review, reusing another binary's receipt or weakening stable gates.
Keep security warnings and unavailable capabilities visible.

## 12. Beta 3 boundary

Defer:
- A new general-purpose EXE setup/uninstaller/repair engine.
- Complete non-MSI installation on NT3.x, Win16, DOS and OS/2.
- Any unqualified Universal Setup runtime adoption.
- Full htop inspection parity, tracing/backtrace/GPU systems unless separately completed.
- Rich native GUI.

Beta 2 should deliver an improved monitor and a reliable release/distribution system.
It must not relabel capability probes, build-only artifacts or blocked installer candidates as completed downloads.

## 13. Source register

[S1] GitHub release API, inspected 2026-11-18:
https://api.github.com/repos/Julesc013/wtop/releases/tags/v0.4.0-beta.1

[S2] GitHub repository metadata:
https://api.github.com/repos/Julesc013/wtop

[S3] GitHub release-asset labels:
https://docs.github.com/en/rest/releases/assets
https://cli.github.com/manual/gh_release_create

[S4] Existing beta gate:
https://github.com/Julesc013/wtop/blob/f0d02559a5795361c28170d2f75417ff2d73096a/tools/beta_release.py
https://github.com/Julesc013/wtop/blob/f0d02559a5795361c28170d2f75417ff2d73096a/tools/publish_beta.py

[S5] Current build and legacy probes:
https://github.com/Julesc013/wtop/blob/f0d02559a5795361c28170d2f75417ff2d73096a/CMakeLists.txt
https://github.com/Julesc013/wtop/tree/f0d02559a5795361c28170d2f75417ff2d73096a/source/legacy

[S6] Open Watcom targets:
https://open-watcom.github.io/open-watcom-v2-wikidocs/c_readme.html

[S7] Current MSI:
https://github.com/Julesc013/wtop/blob/f0d02559a5795361c28170d2f75417ff2d73096a/packaging/msi/wtop.wxs
https://github.com/Julesc013/wtop/blob/f0d02559a5795361c28170d2f75417ff2d73096a/packaging/build_msi.py

[S8] NTop portable package:
https://github.com/gsass1/NTop/blob/326ccd80b41842868f4247d1e8978e854658a710/ntop.nuspec

[S9] Historical MSI runtimes:
https://learn.microsoft.com/en-us/windows/win32/msi/instmsi-exe

[S10] MSI 5.0 dual-purpose packaging:
https://learn.microsoft.com/en-us/windows/win32/msi/single-package-authoring

[S11] WinPE limitations:
https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/winpe-intro

[S12] MSI numeric versions:
https://learn.microsoft.com/en-us/windows/win32/msi/productversion

[S13] WiX lifecycle:
https://docs.firegiant.com/wix/
https://www.firegiant.com/wixtoolset/

[S14] Universal Setup inspected main:
https://github.com/Julesc013/universal-setup/blob/d0a2bcbf1aff6beaac46b5a778fbc315ad8cf683/README.md
https://github.com/Julesc013/universal-setup/blob/d0a2bcbf1aff6beaac46b5a778fbc315ad8cf683/CMakeLists.txt

[S15] Authoring contract:
https://github.com/Julesc013/universal-setup/blob/d0a2bcbf1aff6beaac46b5a778fbc315ad8cf683/spec/authoring/toolchain.md

[S16] WinGet locations:
https://learn.microsoft.com/en-us/windows/package-manager/winget/settings

[S17] Scoop locations:
https://github.com/ScoopInstaller/Scoop/wiki/Scoop-Folder-Layout

[S18] Chocolatey shims:
https://docs.chocolatey.org/en-us/faqs/
https://docs.chocolatey.org/docs/features-shim/

[S19] Microsoft SmartScreen:
https://learn.microsoft.com/windows/apps/package-and-deploy/smartscreen-reputation

[S20] Artifact Signing eligibility:
https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart

[S21] SignPath conditions:
https://signpath.org/terms

[S22] OSS CA signing:
https://www.certum.eu/en/code-signing-certificates/
https://support.certum.eu/en/code-signing-required-documents/

[S23] False-positive submission:
https://learn.microsoft.com/defender-xdr/submission-guide

[S24] Native ARM64 CI:
https://docs.github.com/en/actions/reference/runners/github-hosted-runners
