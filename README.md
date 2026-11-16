# Wtop

Wtop is a native process monitor derived from [NTop](https://github.com/gsass1/NTop),
with an htop-style process table and NTop's Vi-style interaction. This branch
starts the migration toward one shared source tree, explicit platform capabilities,
and a scriptable command-line interface.

**Status: development foundation, version 0.1.0-dev.1.** The current application
backend is Windows NT. Historical DOS, Win16, Win9x, early NT, CE and OS/2 ports
are specified work, not implemented support. A successful modern cross-build does
not qualify any historical operating system. The GUI is deferred; `--gui` reports
that it is unavailable. No Wtop release, WinGet package or installer is published
by this branch.

## Start here

- [Specification entry point](spec/START-HERE.md) and [OKF index](spec/index.md).
- [Platform and binary policy](spec/platform/targets.md).
- [Build and verification](docs/development.md).
- [Bootstrap results and remaining gates](docs/bootstrap-verification.md).
- [Distribution, paths and removal](docs/installation.md).
- [Codex workflow](docs/codex.md), [contribution guide](CONTRIBUTING.md) and
  [repository instructions](AGENTS.md).

The specifications use upstream Open Knowledge Format 0.2 with a documented Wtop
writer profile. Requirement, acceptance, target, command and work-unit registries
carry stable identifiers. `python tools/specctl.py validate` checks their structure
and references; it does not certify that a product requirement passes.

## Current command line

Run these commands against a locally built candidate:

```powershell
wtop
wtop --tui
wtop --help
wtop --version
wtop --capabilities --format json
wtop --snapshot --format json --interval 1000 --no-config
wtop --snapshot --format text -n explorer -s CPU%
wtop --snapshot --format json -p 1234 > snapshot.json
```

`wtop` defaults to the TUI and requires console input and output. Redirected
invocation without an explicit snapshot mode returns a diagnostic instead of
emitting terminal control sequences. A snapshot samples twice over the requested
interval so rate fields have a measured denominator; access or sampling failures
remain explicit. `--capabilities` reports build metadata without starting a
collector; it is not a full runtime probe.

| Option | Current behavior |
| --- | --- |
| `--tui` | Interactive process table, also the default. |
| `--snapshot`, `--dump`, `-d` | Collect once and print text or JSON. |
| `--format text` or `--format json` | Select output format; alone selects snapshot mode. |
| `--interval MILLISECONDS` | Sampling/redraw interval, 50–60000 ms; default 1000 ms. |
| `--capabilities` | Build features and explicit unavailable areas. |
| `--help`, `-h`; `--version`, `-v` | Print metadata and exit before collection. |
| `--config PATH`; `--no-config` | Read an explicit configuration, or disable configuration. |
| `-C` | Monochrome colors. |
| `-p PID,PID` | Filter process IDs. |
| `-n NAME,NAME` | Filter names by case-sensitive substring. |
| `-u USER` | Filter by exact owner account name, case-insensitively. |
| `-s COLUMN` | Sort by ID, USER, PRI, CPU%, MEM, THRD, I/O, AGE or PROCESS. |
| `--gui` | Reserved; returns an explicit unsupported-mode error. |

The JSON snapshot is an initial versioned schema. A field carries its availability
state, and unavailable values are `null`, never an invented zero. Exact 64-bit
integers, including process creation FILETIME, working-set bytes and age in
milliseconds, use decimal strings so ordinary JSON consumers retain precision.
CPU is the process share of the system sampling denominator; the I/O counter is
process read/write transfer activity, not a claim of physical disk bandwidth.
See [metric semantics](spec/architecture/model-metrics.md).

Dedicated `query`, `inspect`, continuous `log`, install/uninstall commands, richer
runtime negotiation and provider backends are roadmap items. Do not infer them
from the future command catalog.

## Interactive controls

The NTop table, meters, tree, search, sorting, selection and Vi commands are
retained while their collection and lifetime handling are hardened.

| Key or command | Function |
| --- | --- |
| Arrow keys, `j`, `k`, Page Up/Down | Move in the process list. |
| Ctrl+Left/Right | Change sort column. |
| `g`, `G`; `I`; `F` | Top/bottom; invert sort; follow selected process. |
| Space; `U`; `K` | Tag; clear tags; terminate tagged processes. |
| `/PATTERN`, `:search PATTERN`; `n`, `N` | Search and move between matches. |
| `:tree`; `:sort COLUMN` | Tree view; sort. |
| `:kill PID`; `:exec COMMAND` | Explicit process termination; launch command. |
| `q`, F10, Ctrl+C, `:q`, `:quit` | Exit the monitor. |

Process actions are subject to native permissions. A PID alone is not a stable
process identity; action guards use creation identity and reject unavailable or
changed identities. Full htop feature parity is a goal, not a current claim.

## Configuration and installation

The current slice reads an explicit `--config PATH`; otherwise it tries
`wtop.conf` beside the executable and then the inherited `ntop.conf`. The existing
[ntop.conf](ntop.conf) demonstrates color entries. It does not write settings,
change PATH, register an installation, create a service or install a scheduled
task. Normal configuration writes and managed installation are deferred until
ownership, migration, rollback and removal behavior have acceptance evidence.

The planned model keeps portable settings beside the payload and managed user
settings outside the package directory. A WinGet **portable installer** still
performs a managed installation and can change PATH/registration. See
[installation policy](docs/installation.md) for precise distinctions and paths.

## Build and validate

With a suitable Windows C compiler and CMake 3.16+:

```powershell
cmake -S . -B out/windows-x64 -A x64
cmake --build out/windows-x64 --config Release --parallel 2
ctest --test-dir out/windows-x64 -C Release --output-on-failure
```

The portable core can be tested on a development host. Python is used by repository
checks, not by the application. See [development instructions](docs/development.md)
for host tests, target choices, native verification and what CI does and does not
prove. CI builds candidates and never creates a public release from a source push.

## Provenance and license

Wtop retains NTop's GPL-3.0-only licensing and upstream notices. The starting
revision is `326ccd80b41842868f4247d1e8978e854658a710`. See [LICENSE](LICENSE).
Use the same license for contributions and record the origin and compatibility of
any reused code. A design inspired by another monitor does not grant permission
to copy incompatible implementation code.
