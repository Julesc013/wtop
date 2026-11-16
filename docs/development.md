# Build and verification

## Current implementation

The application has a Windows NT backend derived from NTop. The small shared
command/formatting core also builds on a development host for tests. That host
target is not a Linux monitor or proof of a DOS/Win16 port. The Windows build
requests Unicode APIs, a Win7-era header baseline and PSAPI v1. Final loader/CRT
imports and native execution determine actual OS reach; version macros alone
do not establish historical support.

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
imports before claiming one-file or old-OS-compatible operation. `build.bat`
is a convenience entrypoint for CMake.

## Host core and specification checks

```sh
python tools/specctl.py validate
python -m unittest discover -s tests -p 'test_*.py' -v
cmake -S . -B out/host -DCMAKE_BUILD_TYPE=Debug
cmake --build out/host --parallel 2
ctest --test-dir out/host --output-on-failure
```

Python is development tooling, not an application dependency. Sanitizer runs
apply to the portable core; they do not prove Win32 handle ownership or native
historical behavior. Builds should remain outside tracked source paths.

## Native acceptance

For each target, record executable/source hashes, compiler/runtime, OS/service
pack/build, architecture and optional components. Audit imports/subsystem/CPU
instructions before execution. Exercise TUI selection, sorting, tagging,
following, resize, Unicode/code pages, small/empty lists, quit, Ctrl+C and console
close. Test rapid process exit/reuse and denied queries. Repeated filters and
snapshots must not grow handles/memory without a bounded reason. Compare counters
to independently specified arithmetic/native observations with matched units.

Test redirection and noninteractive commands without a console. GUI invocation
is reserved and unavailable. Install/configuration writes need their own lifecycle
proof. DOS1, Win1/2, Win3, Win9x, early NT, CE and OS/2 require suitable toolchains
and actual native/emulated-guest execution; the default build proves none of them.

## CI and evidence

CI validates specs, runs host core tests with sanitizers, and builds Windows
Win32/x64/ARM64 candidates. Its x86 runtime is hosted x64 Windows, while ARM64 is
build-only. Neither is historical OS certification. Artifacts carry exact identity
and explicit limits. A source push never automatically creates a release. Keep
large binaries/raw logs in ignored `out/` or an external build directory; retain
concise source-bound outcomes and known failures in project evidence.

The initial [bootstrap observations](bootstrap-verification.md) disclose the
local cross-build and host-test results, toolchain failures and remaining native
gates. They do not promote acceptance or release status.
