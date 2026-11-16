# Htop-style presentation and settings verification

Observed on **2026-11-16 UTC**, for development candidate **0.1.0-dev.2**.
Implementation revision:
[`73eb8af3fb1d3e55db6b3c347d7bde7431b80587`](https://github.com/Julesc013/wtop/commit/73eb8af3fb1d3e55db6b3c347d7bde7431b80587).
The earlier [bootstrap observations](bootstrap-verification.md) describe an older
revision and remain unchanged. This record does not promote product acceptance
or historical target support.

## Result and exact scope

[GitHub Actions run 43871820392](https://github.com/Julesc013/wtop/actions/runs/43871820392)
passed all four jobs for the implementation revision above.

| Environment | Observed result |
| --- | --- |
| Windows Server 2025 x64, MSVC 19.51.36260.0, Win32 application | 9/9 CTest entries passed, including native CLI and isolated native-console interaction |
| Same host/toolchain, x64 application | 9/9 CTest entries passed; the moved `source/build.bat -release` also configured, built and tested from the runner's temporary directory |
| Same host/toolchain, ARM64 application | Built successfully; execution was deliberately not attempted on the x64 runner |
| Ubuntu 26.04, GCC 15.2.0 | Specification/validator tests and 3/3 portable C tests with AddressSanitizer/UndefinedBehaviorSanitizer passed |
| Local Linux, GCC 14.2.0 | 3/3 portable tests passed under strict C90 with warnings treated as errors; 24 validator tests passed |
| Local Linux with ASan/UBSan | 3/3 passed with leak detection disabled because LeakSanitizer cannot initialize under the traced workspace; this is not a local leak-test pass |
| Local Zig 0.17.1 / Clang 22.1.8 | x86, x64 and ARM64 Windows cross-builds passed; these are build observations only |

The Windows runner identifies kernel `10.0.26100.0`, image `20261103.298.1`.
Win32 execution here means an x86 application on that x64 OS. It does not
establish execution on a 32-bit Windows installation. The successful final MSVC
job logs contain no C compiler warning lines.

The nine CTest entries comprise three portable suites, four direct CLI checks,
the eleven-case native CLI suite, and the native TUI harness. Counts at these
different levels must not be added as though each were an individual user
requirement.

## Native console coverage

The harness creates its own hidden Windows console. It sends actual Win32 input
records, reads the active screen buffer and attributes, and checks console modes.
It never attaches to or injects input into a user's existing console. A parent
job object contains the worker and its descendants before any fixture launches;
closing that job bounds cleanup if the test times out.

Both Win32 and x64 observations record these successful checks:

1. Default htop function bar, raw line/echo mode and enabled native mouse input.
2. Mouse invocation of Setup and keyboard Help, with normal overlay cancellation.
3. F9 confirmation and Escape cancellation leave owned target processes alive.
4. Clicking Help cancels an unfinished command editor; tab clicks while filtering
   do not transfer the edit or its cancellation baseline to another tab.
5. Explicit save preserves a comment and an unknown inert key; a hidden function
   bar survives restart. Normal exits restore original input mode and buffer.
6. External file edits survive a refused stale save and ordinary exit.
7. Vi `j` and `k` move the highlighted owned process in a stable PID sort; `k`
   does not open the htop termination action.
8. Emacs Ctrl+S enters search, Ctrl+G cancels, and `--no-mouse` disables reporting.
9. A queued `q`, `k`, `y` batch exits without acting on subsequent keys.

The native CLI suite separately covers early metadata routing, bounded snapshots,
output structure, explicit configuration export/reload, Unicode paths, invalid
option conflicts, missing-parent/read-only write failures, preserved destination
bytes and temporary-file cleanup. Portable tests cover settings bounds and
transactionality, independent expected metric strings, keymap distinctions,
layout/hitbox boundaries and integer-overflow cases.

These checks do not qualify every mouse gesture, resize geometry, terminal host,
Unicode grapheme, permission combination or adverse process-lifetime schedule.
Human visual acceptance remains false in the artifact metadata.

## Failure found by native execution

The preceding implementation revision
[`8cde3fd874f17f79262d9d7321455ba9936f56b0`](https://github.com/Julesc013/wtop/commit/8cde3fd874f17f79262d9d7321455ba9936f56b0)
failed the Emacs step in
[run 43871361123](https://github.com/Julesc013/wtop/actions/runs/43871361123).
All eight preceding CTest entries passed. The native console consumed the
Ctrl+S key-down event because Wtop had retained `ENABLE_LINE_INPUT`.

Microsoft's [console-host input implementation](https://github.com/microsoft/terminal/blob/main/src/host/inputBuffer.cpp)
explicitly intercepts Ctrl+S as pause when that mode is enabled. Wtop now clears
line and echo input during its TUI, preserves its processed Ctrl+C policy, and
restores the caller's original mode. The regression remained in the suite and
passed after the application fix; the assertion was not weakened.

A separate source review found that quit could leave later records in the same
already-read event batch dispatchable. The implementation now stops dispatch and
skips drawing after shutdown is requested. The final native queued-input case
exercises that boundary with only owned fixture processes as possible targets.

An initial local ARM64 link attempt also failed because stale generated build
rules retained the host GNU archiver. A fresh build directory configured with
Zig's archiver/ranlib built successfully. The independent MSVC ARM64 CI build
passed; this local toolchain repair did not alter application source.

## Acquired artifact identity

Five archives from the successful run were downloaded and their SHA-256 values
matched the Actions API digests. Each of the three acquired application EXEs
also matched its accompanying `candidate.json` hash and expected PE machine type.

| Candidate | EXE bytes | PE machine | Observed imports |
| --- | ---: | --- | --- |
| Win32 | 227328 | `0x014c` | ADVAPI32, PSAPI, KERNEL32 |
| x64 | 266752 | `0x8664` | ADVAPI32, PSAPI, KERNEL32 |
| ARM64 | 272896 | `0xaa64` | ADVAPI32, PSAPI, KERNEL32 |

All use the console subsystem. The two x86-family headers report subsystem 6.0;
ARM64 reports 6.2. These header fields do not establish minimum usable Windows
versions. Optional shell-folder resolution dynamically loads the system's
shell32 library and is not represented in the static import list. Local Zig
artifacts have different runtime imports and are not interchangeable evidence
for the MSVC payloads.

Full artifact IDs, archive/EXE hashes, source-file hashes, selected job log lines,
native check summaries and exact qualification flags are in
[presentation-observations-2026-11-16.json](presentation-observations-2026-11-16.json).
The downloadable CI candidates are retained for fourteen days; this document
and its hashes are durable evidence, not a promise that those temporary download
links remain available. No supported release, installer or package listing was
created by this run.

## Remaining limits

- The common F1–F10 bar remains visible inside overlays; fully contextual bar
  labels are still work. Custom tab creation/renaming uses the configuration file.
- CJK/combining-character display-cell widths are not fully modeled. The
  sixteen color indices follow the host console palette rather than fixed RGB.
- Existing-file external-edit detection is best effort, not atomic multi-writer
  compare-and-swap. Replacement does not preserve customized destination ACLs
  or all file metadata. Managed-install ownership/migration remains unfinished.
- There is no measured guarantee of zero leaks, zero crashes or bounded cost
  under every provider failure. Normal console restoration passed; the documented
  emergency shutdown path for blocked native work can still bypass cleanup.
- ARM64 execution, historical platforms, ConPTY/SSH behavior, broad process
  churn, exhaustive failure injection, and human readability require separate
  evidence. All 51 broad product acceptance entries remain unpromoted.

Use [TODO](../TODO.md), [ROADMAP](../ROADMAP.md) and the canonical acceptance
registry for the remaining work. Do not infer unsupported ports from this run.
