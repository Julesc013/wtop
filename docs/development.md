# Build and verification

## Branches and upstream

`master` is Wtop's sole canonical integration branch and the repository's default
branch. Its name is retained so existing clones, pull-request targets and CI
filters keep working. `main` would also be a valid name, but a rename provides no
compatibility benefit; maintaining both as independent Wtop trunks would make the
source of truth ambiguous.

| Ref or remote | Purpose |
| --- | --- |
| `master` | Current Wtop development; the target for reviewed changes |
| `work/<task>` | Short-lived feature or repair branch starting from current `master` |
| `ntop-baseline` | Fixed, unmodified starting point at `326ccd80b41842868f4247d1e8978e854658a710` |
| `origin` remote | This Wtop fork, `https://github.com/Julesc013/wtop.git` |
| `upstream` remote | Original NTop repository, `https://github.com/gsass1/NTop.git` |

The baseline branch is a historical reference, not a moving copy of upstream and
not another supported Wtop release line. Do not advance it. The original upstream
history and GPL notices remain in Wtop's ancestry; future upstream fixes should
be inspected and integrated selectively with their provenance intact.

Git remotes are local clone configuration. If a clone does not already have an
`upstream` remote, add it once:

```sh
git remote add upstream https://github.com/gsass1/NTop.git
git fetch upstream
```

`upstream/master` is then a remote-tracking ref; it is different from the local
Wtop `master` branch. Fetching upstream does not merge or overwrite Wtop. Inspect
relevant differences before choosing a merge or cherry-pick; do not blindly sync
the fork over its downstream changes. The inherited `multiple-cpus` branch is
also historical material, not Wtop's integration branch.

Merge only after applicable checks pass, preserve other contributors' work, and
do not force-push the canonical branch. A development merge does not publish a
release or qualify additional operating systems. Normal `master` CI produces
development artifacts; release publication remains a separate work unit.

## Current implementation and module boundaries

The application has a Windows NT backend derived from NTop. Shared option,
formatting, settings and input/layout policy also builds on a development host
for tests. That host target is not a Linux monitor or proof of a DOS/Win16 port.
The Windows build requests Unicode APIs, a Win7-era header baseline and PSAPI v1.
Final loader/CRT imports and native execution determine actual OS reach; version
macros alone do not establish historical support.

Application files live under `source/`. The root `CMakeLists.txt` remains the
build entry point. The inherited `source/ntop.c/.h`, `source/util.c/.h` and
`source/vi.c/.h` retain the working native foundation. Portable policy belongs in
`source/core.c/.h`, `source/settings.c/.h` and `source/ui.c/.h`; Windows handles,
console events and filesystem replacement stay with the native adapter.

`source/_config.yml` is inherited site configuration, not a Wtop settings file.
`source/ntop.nuspec` retains old NTop packaging metadata and is not a Wtop release
manifest. `source/wtop.conf` is generated from current compiled defaults;
`source/ntop.conf` is retained as a legacy import example. Preserve upstream
notices and source identity when moving files; historical observations retain the
paths and hashes that applied to their recorded revision.

Use [TODO](../TODO.md) to find closure tasks and [ROADMAP](../ROADMAP.md) for
sequence. The work registry and linked requirements remain authoritative.
Candidate code may implement part of a planned work unit; inspect it before
starting an extraction or replacement.

## Windows build

Use CMake 3.16+ and an appropriate Visual Studio C toolchain:

```powershell
cmake -S . -B out/windows-x64 -A x64
cmake --build out/windows-x64 --config Release --parallel 2
ctest --test-dir out/windows-x64 -C Release --output-on-failure
out/windows-x64/Release/wtop.exe --version
out/windows-x64/Release/wtop.exe --snapshot --format json --no-config
```

Choose `-A Win32` for an x86 candidate or `-A ARM64` for an ARM64 candidate when
that compiler is installed. Cross-compilation alone does not test execution.
MSVC uses a static C runtime for the current application profile. Audit the PE
imports before claiming one-file or old-OS-compatible operation.
`source/build.bat` is a convenience entry point for CMake; it resolves the
repository root from its own location and does not require copying files back to
the root. Python enables additional development tests but is not part of the
application's installed runtime.

## Host policy and specification checks

```sh
python tools/specctl.py validate
python -m unittest discover -s tests -p 'test_*.py' -v
cmake -S . -B out/host -DCMAKE_BUILD_TYPE=Debug
cmake --build out/host --parallel 2
ctest --test-dir out/host --output-on-failure
```

CMake registers separate `portable_core`, `portable_settings` and `portable_ui`
tests. Their contracts cover option conflicts/output arithmetic, bounded
transactional settings and round trips, and key/action/visible-layout decisions.
Expected results must come from the behavior contract, not another call to the
same implementation. Exercise malformed values, duplicates, overflow, unknown
inert keys, UTF-8 limits, mode conflicts and narrow widths where relevant.

Sanitizer runs apply to portable modules. They do not establish Win32 handle
ownership, operating-system loader compatibility or historical behavior. Keep
builds and raw logs outside tracked source paths.

## Native automated checks

On Windows, CMake adds CLI version, capability, reserved-GUI and snapshot checks.
When a Python interpreter is available, it also adds `native_cli_behavior` and
`native_tui_interaction`. The latter creates its own console, injects native input
and observes the console buffer/settings. It must not attach to an unrelated
interactive user's console or terminate arbitrary processes.

These checks can establish concrete behavior on the recorded CI host: parseable
output, bounded exit, selected keyboard/mouse paths and settings persistence.
They do not establish all console hosts, visual quality, accessibility, historical
target support, all privilege boundaries or full htop parity. Keep synthetic event
coverage distinct from a human reviewing a real displayed frame.

Tests must never broaden an action to arbitrary real processes. Launch a disposable
fixture, record its identity and own its cleanup. Unknown/reused identities,
failed rights, cancelled prompts and denied settings destinations need independent
negative cases. `--write-config` tests use temporary owned directories and assert
that no process collector is required. Restore console modes and close every
owned handle on success, failure and cancellation.

## Native acceptance and release gates

For each target, record executable/source hashes, compiler/runtime, OS/service
pack/build, architecture and optional components. Audit imports/subsystem/CPU
instructions before execution. Exercise selection, sorting, tagging, following,
resize, Unicode/code pages, small/empty lists, quit, Ctrl+C and console close.
Test rapid process exit/reuse and denied queries. Repeated filters and snapshots
must not grow handles/memory without a bounded reason. Compare counters with
independently specified arithmetic/native observations and matched units.

For the current TUI iteration, include all keymaps and editing/overlay contexts,
F1–F10, narrow function-bar hints/hitboxes, disabled/failed mouse reporting,
header/column/tab order and visibility, pending identity-based termination,
priority-class boundaries, settings round trip and saved versus unsaved changes.
Check read-only/denied config paths, interrupted replacement, original-file
preservation and zero startup/exit writes. A passing parser fixture cannot qualify
native save durability or mouse/console restoration.

Test redirection and noninteractive commands without a console. GUI invocation
is reserved and unavailable. Managed installs require their own ownership,
update/removal and recovery proof. DOS1, Win1/2, Win3, Win9x, early NT, CE and OS/2
require suitable toolchains and actual native/emulated-guest execution; the
default build proves none of them.

## CI and evidence

CI validates specifications, runs portable tests with sanitizers and builds
Windows Win32/x64/ARM64 candidates. Its x86 runtime is hosted x64 Windows, while
ARM64 is build-only. Neither is historical OS certification. Artifacts carry
exact identity and explicit limits. A source push never automatically creates a
release. Keep large binaries/raw logs in ignored `out/` or an external build
directory; retain concise source-bound outcomes and known failures as evidence.

The initial [bootstrap observations](bootstrap-verification.md) disclose the
original cross-build and host-test results, toolchain failures and remaining
gates. Those records do not qualify later source revisions. Record new evidence
with its own revision rather than editing old hashes or silently promoting
acceptance and release status.
