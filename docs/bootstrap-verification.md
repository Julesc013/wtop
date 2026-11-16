# Bootstrap verification — 2026-11-16

These are development observations, not product acceptance receipts or a release
qualification. [bootstrap-observations.json](bootstrap-observations.json) binds
the inspected application/test sources and locally produced binaries to SHA256
hashes. All 45 product acceptance records remain `not_run`; the formal evidence
registry is deliberately empty until complete case-scoped records are produced.

## Observed results

| Check | Result and scope |
| --- | --- |
| `python3 tools/specctl.py validate` | Passed: 17 concepts, 45 requirements, 45 acceptance designs. |
| Python unittest discovery | 24 specification-tool tests passed; 8 native Windows tests explicitly skipped on Linux. |
| Strict portable core | GCC 14.2.0, C90, `-pedantic -Wall -Wextra -Werror`; passed. |
| CMake/CTest host core | Passed in Debug with AddressSanitizer and UndefinedBehaviorSanitizer, with leak detection disabled as explained below. |
| Windows x86 cross-build | Compiled and linked application and core-test executable. No execution. |
| Windows x64 cross-build | Compiled and linked application and core-test executable. No execution. |
| Windows ARM64 cross-build | Compiled and linked application and core-test executable. No execution. |
| PE inspection | Correct x86/x64/ARM64 machine identities and console subsystem; import DLLs and hashes recorded. |
| `git diff --check` | Passed. |
| Independent source review | Focused lifetime, PID reuse, allocation failure, quoting, bounds, metric serialization and shutdown review; fixes re-reviewed. No native fault injection. |

CMake was 4.4.5. The local cross compiler was Zig 0.17.1 / Clang 22.1.8, targeting
`x86-windows-gnu`, `x86_64-windows-gnu` and `aarch64-windows-gnu`. CMake used a Windows
system cross profile and Release builds. ARM64 required Zig's `ar` and archive
indexing; the host GNU archive tools initially produced an unusable symbol index.
That toolchain mismatch was repaired without changing application source.

The cross compiler defines a Windows 10 API macro by default; the current source
sets a Win7 header selection, producing a macro-redefinition warning. Neither
macro nor the PE subsystem version establishes an OS minimum. These local GNU
cross-builds import UCRT API-set DLLs. They therefore do **not** demonstrate a
runtime-independent or historical one-file release. The separate MSVC CI profile
requests a static runtime; its actual artifacts still require import/runtime
inspection.

LeakSanitizer failed to initialize because this workspace executes under tracing
(`LeakSanitizer does not work under ptrace`). The same core test passed with
`ASAN_OPTIONS=detect_leaks=0`, retaining ASan and UBSan instrumentation. This is
not a leak-test pass. Native process/token/console handle checks and long-running
growth checks remain required.

## Concrete repairs reviewed

The bootstrap addresses invalid Toolhelp sentinel checks, leaked filtered-process
and token handles, private-to-published sample ownership, tag-array growth, empty
lists, tree cycles, long search/config/input bounds, preserved Windows command
quoting, exact JSON integers, measured rates and honest unavailable states.
It binds tags/follow/termination to PID plus creation identity and rejects unknown
identity. The review also caught and fixed a version-macro/enum collision and a
test-discovery error before final builds.

## Remaining native risks and gates

- The TUI remains inherited code with globals and a critical section. Extracting
  the future collector/model/view boundaries is not complete.
- A conservative Toolhelp snapshot-start timestamp reduces PID reuse races; it
  is not an atomic kernel identity guarantee under wall-clock rollback.
- Normal shutdown restores the console and joins owned work. A blocked provider
  can require emergency self-termination with exit 124 after the documented
  grace period. If the main thread cannot reach cleanup after TUI activation,
  the callback fallback may bypass console restoration.
- Control-event delivery is an assumption of that fallback. The provider itself
  has not become cancellable, and a command without an interrupt can still wait
  inside a blocking native query.
- Actual TUI rendering, mouse/keyboard/resize behavior, console-close behavior,
  denied access, fault-injected allocation/provider failures, PID churn, CPU
  topology, repeated-instance cost and sustained memory/handle growth need
  Windows execution and case-scoped evidence.
- Historical OS ports, installer lifecycle, configuration writes, continuous
  logging, broader query/inspection commands, GPU telemetry and complete htop
  parity are unimplemented or unqualified.

The new CI workflow is intended to run native CLI/core checks for Win32 and x64
on its hosted Windows runner and build ARM64. Its runner is not proof of a native
32-bit OS, historical Windows or ARM64 execution. Workflow results must be
inspected separately; authoring the workflow is not an execution result.
