# Historical loader probes and capability evidence

These probes are independent experiments inside the shared source tree. They do
not implement a DOS, Win16, OS/2, WinPE or ReactOS process monitor, and they do not
change a target's support status. The canonical target inventory remains
[spec/catalog/targets.json](../spec/catalog/targets.json). An entrypoint, compiler
result or export address is a different level of evidence from a working provider
or a qualified installed application.

## Implemented scope

| Canonical profile | Probe source and output | Evidence available in this work unit | Still required |
| --- | --- | --- | --- |
| `dos1-x86` | `dos_probe.s`, no CRT, 8086 COM. Prints BIOS INT 12 conventional-memory KiB; explicitly does not query a DOS version. Uses DOS INT 21/AH=09 output and INT 20 termination. | GNU binutils 2.44 assembled/linked a 451-byte COM. Scripted 16-bit instruction execution checked bounded output, 0/640/65535 KiB conversion, DOS1 API isolation and return via the PSP. | Actual named MS-DOS/PC DOS/OEM image, boot/loader/BIOS behavior and capture; DOS1.25 source is not a proof for every DOS1.0/OEM variant. |
| `dos2plus-x86` | Same assembly source, MZ wrapper, no relocations/CRT, separate 256-byte stack. Adds raw INT 21/AH=30 major/minor/OEM observations. | GNU binutils 2.44 assembled/linked a 681-byte MZ. Scripted execution checked three version/OEM vectors and DOS2 termination. | Actual pinned DOS/FreeDOS image, command processor/redirection and OEM behavior; no MCB/TSR/task enumeration yet. |
| `win16-3-x86` | Native `WinMain`, raw `GetVersion`, C widths and already-loaded Toolhelp/export presence. Writes only an explicit `--output PATH`; creates no window. | Source entrypoint against reviewed Win16 API signatures; shared C90 report tests. | Open Watcom/SDK/CRT build, NE/import/segment audit, native loader execution. This is not the future cooperative text-grid host. |
| `os2-16-x86` | OS/2 `DosGetVersion`, C widths, VIO rows/columns with separate API outcomes. | Source entrypoint and shared report tests. | Real 16-bit OS/2 toolkit/compiler, NE/ABI audit and OS/2 1.x image execution. |
| `os2-32-x86` | OS/2 `DosQuerySysInfo` raw version fields, C widths and VIO observations. | Source entrypoint and shared report tests. | Real toolkit/compiler, LX/import audit and named OS/2 image execution. |
| `reactos-x86` and matching NT/WinPE profiles | `nt_probe.c`: actual module path, raw NT version, ProductName/BuildLabEx registry observations, MiniNT marker, console result and optional export presence. | x86 Windows cross-build and existing PE inspector; no NT/ReactOS/WinPE execution in this work unit. | Exact image/components and artifact execution. A Windows/ReactOS-reported version or MiniNT marker never grants compatibility. |
| Earlier Win16, Win9x/early NT, historic NT CPUs, CE and stock RT | No new loader entrypoint. | Harness records `source-unavailable`, rather than fabricating a provider. | Independently scoped toolchain/SDK/loader work; stock RT deployment policy remains a separate restriction. |

The assembly test uses **Unicorn 2.1.4 with scripted interrupts**, not a booted DOS
image. It executes the emitted instructions and validates our calling assumptions
against an independent fixture. It cannot validate a DOS kernel, OEM BIOS,
filesystem, loader implementation, Ctrl+C behavior or hardware timing. Actual
historical runtime qualification remains pending for every row.

BIOS conventional-memory size is not DOS free memory. Reported DOS versions can
be virtualized or altered; the probe retains their raw values and OEM byte without
inferring “FreeDOS support.” Export presence means an address could be resolved;
the probe does not execute an arbitrary function with a guessed signature. The
NT version call has an explicitly typed, synchronous `WINAPI` signature; no
callback or background work survives program exit.

## Source boundaries

- `source/legacy/probe.c/.h` owns bounded typed observations and JSON output. It
  reuses `source/text.c` for UTF-8 validation. It has no host handles, plugin
  loading, process actions or target-support policy.
- `dos_probe.s` isolates the no-CRT 8086 startup/API experiment. DOS1 and DOS2 use
  one source with a checked assembly selector. No DOS2 handles, directory APIs or
  CRT startup are smuggled into the DOS1 probe.
- `win16_probe.c`, `os2_probe.c` and `nt_probe.c` own their ABI entrypoints and
  native observations. Their absence of a provider is reported as `unsupported`,
  rather than a fabricated empty process list or a zero utilization value.
- `tools/legacy_probe.py` maps source groups to canonical catalog IDs, builds,
  inspects artifacts and binds supplied output to exact hashes. It does not
  maintain a competing target inventory or update the catalog's qualification.

Each shared report has schema `wtop.loader-probe/1`, a canonical profile, actual
executable format and up to 24 observations. Each observation has an ID, state,
native return code and bounded UTF-8 value. `observed`, `present`, `unavailable`,
`unsupported`, `denied` and `failed` are distinct. A failed query cannot become a
successful numeric zero. Every report explicitly says
`"qualification":"not-established"`.

The report object is bounded below a 16-bit segment; JSON serialization has a
16 KiB ceiling and fails instead of silently truncating. This size bound and C90
syntax still require actual old-compiler/memory-model verification. Win16's
source spike uses static buffers and a large-model build recipe. It has no
interactive message loop because it exits after the one-shot observation; it is
not evidence for the future TUI host's lifecycle.

## Reproduce the available build stages

Run from the repository root. Keep generated artifacts outside the source tree:

```sh
python tools/legacy_probe.py list
python tools/legacy_probe.py build dos1-x86 --output /tmp/wtop-dos1-probe
python tools/legacy_probe.py build dos2plus-x86 --output /tmp/wtop-dos2-probe
python tools/legacy_probe.py inspect dos2plus-x86 /tmp/wtop-dos2-probe/WTOPPROB.EXE
```

The DOS recipe requires GNU `as` and `ld` supporting `.code16` and `.arch i8086`.
The assembler's architecture check rejects later instruction forms. The COM link
origin is `0x100`. The MZ wrapper has a 32-byte header, zero relocations, CS:IP
`0:0`, and a separate stack above the rounded code/data image. The harness checks
header lengths, entrypoint, relocation bounds and minimum stack allocation; this
is structural validation, not DOS-loader qualification.

An available Windows cross-compiler can build an NT probe, for example:

```sh
python tools/legacy_probe.py build reactos-x86 \
  --cc /absolute/path/to/x86-windows-compiler \
  --output /tmp/wtop-reactos-probe
```

The compiler path must be one executable, such as a wrapper that already supplies
its target. Arguments are passed as an array with no shell interpolation. The PE
inspector checks the resulting architecture against a specific catalog
architecture and retains its imports, subsystem/header versions and CRT imports.
A modern compiler's successful PE output does not establish an old loader/CRT
floor.

With real Open Watcom and target SDKs installed, the harness has explicit Win3.x
large-model and OS/2 16/32-bit recipes:

```sh
python tools/legacy_probe.py build win16-3-x86 --cc wcl --output out/probe-win3
python tools/legacy_probe.py build os2-16-x86 --cc wcl --output out/probe-os2-16
python tools/legacy_probe.py build os2-32-x86 --cc wcl386 --output out/probe-os2-32
```

Those recipes were **not compiled here** because the required compilers/SDKs were
unavailable. A missing tool is recorded as `toolchain-unavailable`; an actual
compiler failure is retained as `build-failed`. Neither is converted into a pass.
The entrypoints and recipes may need changes when tested against a named toolkit.

A successful build writes `loader-build.json` containing catalog/source/harness
hashes, tool launcher identity, tool version output, exact arguments, command
results, artifact SHA-256 and structural inspection. Build success is named
`probe-built-not-run`. If the launcher is a wrapper, its hash is not represented
as the underlying compiler executable's hash; retain the SDK/compiler package
identity with the qualification run.

## Tests and their limits

```sh
cc -std=c90 -Wall -Wextra -Werror -pedantic -Isource \
  source/legacy/probe.c source/text.c tests/test_legacy_probe.c \
  -o /tmp/test-legacy-probe
/tmp/test-legacy-probe
python -m unittest discover -s tests -p test_legacy_probe.py -v
```

The portable C test checks typed failures, unsupported observations, string bounds,
UTF-8, JSON escaping, duplicate observation IDs, count limits and explicit Win16
output parsing. Four Python tests check artifact bounds, catalog format checks,
unsupported qualification claims and unavailable toolchains. Two additional
instruction tests run when Unicorn and GNU binutils are available:

```sh
python -m pip install --target /tmp/wtop-probe-runtime unicorn==2.1.4
PYTHONPATH=/tmp/wtop-probe-runtime \
  python -m unittest discover -s tests -p test_legacy_probe.py -v
```

The optional dependency belongs to the development fixture. Wtop and every probe
run without Python or Unicorn on their target OS. No SDK/emulator download is
triggered by a normal build or application invocation.

## Collect actual image evidence

Run the exact artifact inside the named image. For DOS2+, OS/2 and NT, capture its
one-shot standard output using that environment's supported mechanism. DOS1
requires an explicit console/emulator capture; do not assume DOS2 redirection.
The Win16 probe requires, for example, `WTOPPROB.EXE --output C:\PROBE.JSON` and
creates or replaces that explicitly named output. It does not inherit the normal
application's settings-store guarantees.

Keep the exact guest image/build, CPU/emulator/version, boot configuration,
optional components, compiler/SDK identity, raw output, exit result and artifact
hash. To bind a supplied observation to an inspected artifact:

```sh
python tools/legacy_probe.py record dos2plus-x86 \
  /tmp/wtop-dos2-probe/WTOPPROB.EXE /tmp/guest-probe.json \
  --image-id 'exact image/build and SHA-256' \
  --host-description 'hardware or emulator/version, CPU model and components' \
  --output /tmp/dos2-loader-evidence.json
```

The tool labels this `supplied-guest-output-not-independently-attested`. It checks
schema/profile/format and content bounds, not the authenticity of the claimed
guest execution. An emulator run is scoped to that emulator and guest. A native
run is scoped to the actual hardware/image. A human or qualification workflow
must review this evidence before any catalog/support change.

Next stages remain independent: input/cell output, timer/counter units, bounded
file/cleanup behavior, truthful entity enumeration, shared model integration,
identity-bound actions, era-appropriate delivery and exact native qualification.
A loader probe is the first stage, not a shortcut around those gates.

## Primary references used for API boundaries

- [Microsoft MS-DOS 1.25 source](https://github.com/microsoft/MS-DOS/blob/main/v1.25/source/MSDOS.ASM): the DOS dispatch table includes function 9 output and the INT20 abort entry. This is source evidence for that version, not every earlier OEM variant.
- [Open Watcom Win16 API declarations](https://github.com/open-watcom/open-watcom-v2/blob/91cb061bdf3da85f6cd1f066585022784904f9da/bld/w16api/include/win16.mh): WinMain, GetVersion, module/export and explicit file-output signatures.
- [Open Watcom OS/2 16-bit API declarations](https://github.com/open-watcom/open-watcom-v2/blob/91cb061bdf3da85f6cd1f066585022784904f9da/bld/os2api/incl16/bsedos.mh) and [32-bit declarations](https://github.com/open-watcom/open-watcom-v2/blob/91cb061bdf3da85f6cd1f066585022784904f9da/bld/os2api/incl32/bsedos.mh): distinct version-query APIs/ABIs.

No implementation code from those projects was copied into the probes.
