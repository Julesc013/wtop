# Wtop

Wtop is a native terminal process monitor derived from
[NTop](https://github.com/gsass1/NTop). It keeps a compact process table and meters,
with **htop-style controls and colors by default**, optional Vi/Vim and Emacs
bindings, and explicit command-line output and settings operations.

**Canonical development branch: `master`.** The `ntop-baseline` branch preserves
the unmodified NTop starting revision. Develop Wtop on short-lived branches from
`master`; see [branch and upstream policy](docs/development.md#branches-and-upstream).

**Status: development candidate, version 0.1.0-dev.2.** The current application
backend is Windows NT. Historical DOS, Win16, Win9x, early NT, CE and OS/2 ports
are specified work, not implemented support. Compiling for a CPU architecture
does not qualify an operating system. The GUI is deferred; `--gui` reports that
it is unavailable. This branch publishes no supported release, WinGet package or
installer. Automated checks and native acceptance are separate evidence.

**Next release target: 0.4.0.** The
[17 November release audit](docs/wtop-release-readiness-2026-11-17.md) separates
implemented features from remaining correctness, lifecycle, presentation,
packaging and native-support gates. It identifies open system-commit/CPU-sampling
semantics and bulk-result/freshness gaps; current CI does not close those cases.
The [NTop fork audit](docs/ntop-fork-audit-2026-11-17.md) records useful external
ideas, already inherited fixes and patches that should not be imported unchanged.
Neither audit changes the application version or announces a release.

## Start here

- [TODO](TODO.md): practical closure tasks and remaining risks, linked to work IDs.
- [ROADMAP](ROADMAP.md): implementation sequence, dependencies and completion gates.
- [Specification entry point](spec/START-HERE.md) and [OKF index](spec/index.md).
- [Platform and binary policy](spec/platform/targets.md).
- [Build and verification](docs/development.md).
- [Current native TUI/settings evidence](docs/presentation-verification.md).
- [0.4.0 / 1.0.0 release readiness](docs/wtop-release-readiness-2026-11-17.md) and
  [NTop fork source audit](docs/ntop-fork-audit-2026-11-17.md).
- [Implementation design](docs/implementation-plan-2026-11-17.md): proposed
  interfaces, reuse boundaries, tests and integration slices for the remaining work.
- [Distribution, paths and removal](docs/installation.md).
- [Codex workflow](docs/codex.md), [contribution guide](CONTRIBUTING.md) and
  [repository instructions](AGENTS.md).

The specifications use upstream Open Knowledge Format 0.2 with a documented Wtop
writer profile. Requirement, acceptance, target, command and work-unit registries
carry stable identifiers. `python tools/specctl.py validate` checks their structure
and references; it does not certify that a product requirement passes. The root
TODO and roadmap are readable views of those contracts, not competing registries.

## Current command line

Run these commands against a locally built candidate:

```powershell
wtop
wtop --tui
wtop --keymap vi --theme ntop
wtop --keymap emacs --mouse auto --status-bar
wtop --no-mouse --no-status-bar
wtop --help
wtop --version
wtop --capabilities --format json
wtop --snapshot --format json --interval 1000 --no-config
wtop --snapshot --format text -n explorer -s CPU%
wtop --snapshot --format json -p 1234 > snapshot.json
wtop --no-config --write-config .\wtop.conf
wtop --config .\wtop.conf
```

`wtop` defaults to the TUI and requires console input and output. Redirected
invocation without an explicit snapshot mode returns a diagnostic instead of
emitting terminal control sequences. A snapshot samples twice over the requested
interval so rate fields have a measured denominator; access or sampling failures
remain explicit. `--capabilities` reports build metadata without starting a
collector; it is not a full runtime probe.

| Option | Current candidate behavior |
| --- | --- |
| `--tui` | Interactive process table, also the default. |
| `--snapshot`, `--dump`, `-d` | Collect a bounded snapshot and print text or JSON. |
| `--format text` or `--format json` | Select output format; alone selects snapshot mode. |
| `--interval MILLISECONDS` | Sampling/redraw interval, 50–60000 ms; default 1000 ms. |
| `--capabilities` | Build features and explicit unavailable areas. |
| `--help`, `-h`; `--version`, `-v` | Print metadata and exit before collection. |
| `--config PATH`; `--no-config` | Read an explicit configuration, or disable file settings and implicit TUI save destination. These options conflict with each other. |
| `--write-config PATH` | Explicitly write effective settings to the named file and exit without process collection. Can be combined with `--no-config` to export defaults and CLI overrides. |
| `--keymap htop\|vi\|vim\|emacs` | Select controls; `vim` aliases `vi`. Default `htop`. |
| `--theme htop\|ntop\|monochrome` | Select a built-in palette; default `htop`. Selecting a theme resets its color roles. |
| `-C` | Force monochrome presentation after theme selection. |
| `--mouse auto\|on\|off`; `--no-mouse` | Request native mouse reporting with keyboard fallback; default `auto`. `--no-mouse` means `off`. |
| `--status-bar`; `--no-status-bar` | Show/hide the last-row function bar; shown by default. Prompts and error messages remain available. |
| `-p PID,PID` | Filter process IDs. |
| `-n NAME,NAME` | Filter names by case-sensitive substring. |
| `-u USER` | Filter by exact owner account name, case-insensitively. |
| `-s COLUMN` | Sort by ID, USER, PRI, CPU%, MEM, THRD, I/O, AGE or PROCESS. |
| `--gui` | Reserved; returns an explicit unsupported-mode error. |

CLI settings override file settings. `--write-config` is a separate action and
cannot be combined with TUI/snapshot/GUI mode selection, output format, another information action or the unsaved `-p`/`-n`/`-u`
process selectors. It does accept presentation preferences to be saved. Help,
version and capability output do not write configuration. Ordinary startup and
exit also make no settings write.

The JSON snapshot is an initial versioned schema. A field carries its availability
state, and unavailable values are `null`, never an invented zero. Exact 64-bit
integers, including process creation FILETIME, working-set bytes and age in
milliseconds, use decimal strings so ordinary JSON consumers retain precision.
CPU is the process share of the system sampling denominator; the I/O counter is
process read/write transfer activity, not a claim of physical disk bandwidth.
Commit is committed memory, not page-file residency. The TUI keeps decimal byte
units in compact values such as `1.2GB` and `123MB/s`; elapsed age uses explicit
units such as `2m05s`, `2h03m` or `2d03h`. Narrow fields must not silently turn a
clipped number into a different measurement. See
[metric semantics](spec/architecture/model-metrics.md).

Dedicated `query`, `inspect`, continuous `log`, install/uninstall commands, richer
runtime negotiation and provider backends are roadmap items. Do not infer them
from planned command-catalog entries.

## Interactive controls

The function keys provide a common route through all three keymaps. Process
mutations require native permissions and a revalidated process creation identity;
PID reuse or unavailable identity must not turn a selection into another process.

| Key | Action |
| --- | --- |
| F1 | Help. |
| F2 | Setup: general preferences, header meters, columns, tabs and colors. |
| F3 | Search without changing the process collection. |
| F4 | Live process-name filter for the active tab. |
| F5 | Toggle tree view. |
| F6 | Choose sort column. |
| F7 / F8 | Raise/lower Windows priority class for selected/tagged processes. REALTIME is excluded; these are not Unix nice values. |
| F9 | Review selected/tagged process identities before termination; `y` confirms, `n` or Escape cancels. |
| F10 | Quit. |
| Arrows, Page Up/Down, Home/End | Navigate the process view. |
| Tab / Shift+Tab | Next/previous visible tab. |
| Space / `U` | Tag selected process / clear tags. |
| `F` / `I` | Follow selected process / reverse sort direction. |
| `P` / `M` | Sort by CPU / memory. |
| Ctrl+Left / Ctrl+Right | Previous/next sort column. |
| `B` | Hide/show the function bar. |
| `/`, `\`, `:` | Enter search, filter or explicit command input. |
| `q`, Ctrl+C | Quit. |

Map-specific keys are intentional: **`k` opens termination in htop mode and moves
up in Vi mode**. Vi/Vim mode adds `j`/`k`, `g`/`G`, `n`/`N` and `K` for navigation,
search traversal and termination. Htop mode adds `N` for PID sorting, `h`/`?` for
help and `S`/`C` for setup. Emacs mode provides Ctrl+N/P, Ctrl+V and Alt+V, Alt+`<`
and Alt+`>`, Ctrl+S search, Ctrl+R previous match, Ctrl+G cancel and Alt+X command
input. Ctrl+X Ctrl+S explicitly saves settings. These are interaction presets,
not full Vim or Emacs emulation; terminal hosts may intercept some chords.

The inherited explicit command surface includes `:tree`, `:sort COLUMN`,
`:search PATTERN`, `:kill PID`, `:exec COMMAND`, `:q` and `:quit`; `:save` explicitly
saves the current settings. `:kill` is an explicit native termination request;
`:exec` is an explicit command launch. Neither is interpreted from settings or
process text. Full htop feature parity, affinity controls, thread/detail views
and arbitrary plugins remain planned.

### Setup, status and mouse

In F2 Setup, Left/Right selects **General, Meters, Columns, Tabs or Colors**;
Up/Down selects an item. Space/Enter toggles or cycles it. `+`/`-` reorders enabled
meters/columns/tabs or changes a color attribute. `s` saves explicitly; Escape
closes the panel. Column settings belong to the active tab; meters, colors,
keymap, mouse, interval and bar visibility are global.

The function bar is shown on the final row by default. Hiding it returns that
row to the view; active prompts and errors are still displayed. Its current
F1–F10 cells remain common navigation across panels; fully contextual overlay
labels are a remaining roadmap task. The view starts
with **Main** and **I/O** tabs. Each tab has its own columns, order, filter, sort
direction and tree preference. Setup changes visibility/order; creating,
renaming or removing tabs currently requires editing the configuration file.

Mouse input uses native console reporting when available. Click process rows to
select, headers to sort, tabs to switch, or function cells to invoke their
keyboard action. The wheel scrolls. Disabled or unavailable mouse reporting keeps
keyboard operation usable; Wtop restores the caller's input mode on normal exit.
While a text editor is active, other mouse controls are ignored; function keys
and function-bar actions cancel that editor before changing context. Cancelling
a live filter restores its prior value. Console-host behavior, resize races and
visual usability require native acceptance, not just portable key-map tests.
CJK/combining-character display-cell width remains an open renderer qualification;
valid UTF-8 settings alone do not establish full Unicode layout support.

### Color roles

The built-in htop scheme uses white text on black, green column headers, cyan
selection/function backgrounds and meter labels, green total CPU/used-memory
bars, and yellow tag/follow emphasis. Windows commit has its own cyan meter.
Actual RGB colors depend on the host console palette. The scheme follows
[htop's pinned default color roles](https://github.com/htop-dev/htop/blob/21bbc80c7811149ee33f449748addf23740c7b9f/CRT.c);
it does not invent uncollected CPU categories to reproduce a screenshot.

Choose `--theme ntop` for the inherited appearance or `--theme monochrome` for
monochrome. Individual role overrides are saved as Windows console attributes
from 0 through 255; F2 Colors exposes them. Choosing another built-in theme resets
those overrides. Current palettes are not 24-bit RGB theme files.

## Configuration

Generate an editable configuration without starting a collector:

```powershell
wtop --no-config --write-config .\wtop.conf
wtop --config .\wtop.conf --keymap vi
```

A small versioned text file holds presentation settings. The complete generated
current defaults are in [source/wtop.conf](source/wtop.conf);
[source/ntop.conf](source/ntop.conf) remains a legacy import example. A shortened
current example follows:

```text
ConfigVersion 1
Theme htop
Keymap htop
Mouse auto
FunctionBar 1
ShowTabs 1
RedrawInterval 1000
HeaderRows title,cpu,memory,commit,summary
TabCount 2
ActiveTab 0
Tab0.Name Main
Tab0.Visible 1
Tab0.Sort CPU%
Tab0.Descending 1
Tab0.Tree 0
Tab0.Columns ID,USER,PRI,CPU%,MEM,THRD,I/O,AGE,PROCESS
Tab0.Filter ""
Tab1.Name I/O
Tab1.Visible 1
Tab1.Sort I/O
Tab1.Descending 1
Tab1.Tree 0
Tab1.Columns ID,USER,I/O,MEM,CPU%,THRD,PROCESS
Tab1.Filter ""
```

Header rows and columns appear in listed order; omitted items are hidden.
`HeaderRows none` hides all header meters. At least one process column and one
visible tab must remain. Tab keys are zero-based; up to eight tabs are supported.
Names are limited to 31 UTF-8 bytes and filters to 127. Configuration input is
limited to 32768 bytes, with at most 1023 bytes before each line feed. Invalid
known values, duplicate keys/columns, unsupported versions, invalid UTF-8 and
oversized input fail before applying a partial configuration. Comments and
unknown inert keys are preserved by supported saves within those bounds.

The color keys are `FGColor`, `BGColor`, `FGHighlightColor`, `BGHighlightColor`,
`MenuBarColor`, `ProcessListHeaderColor`, `CPUBarColor`, `MemoryBarColor`,
`PageMemoryBarColor`, `ErrorColor`, `TaggedColor`, `FollowColor`, `MeterLabelColor`
and `FunctionKeyColor`. The inherited `PageMemoryBarColor` name controls the
commit meter; it does not change that metric's meaning. Decimal or `0x` attributes
are accepted. Explicit role values override the file's theme; command-line theme
selection overrides those file values.

Configuration discovery and explicit save destinations are described in
[installation and storage](docs/installation.md) and the
[configuration contract](spec/configuration/settings.md). No settings are saved
simply by quitting. F2 `s`, `:save` and `--write-config PATH` are the explicit write
routes; a failure is reported and must preserve the previous file. Full managed
installation ownership and migration remain separate work.

## Source layout and build

| Location | Responsibility |
| --- | --- |
| `source/ntop.c`, `source/ntop.h` | NTop-derived native process collection, console adapter and application lifecycle; extraction remains incremental. |
| `source/util.c/.h`, `source/vi.c/.h` | Native helpers and explicit command/editor handling. |
| `source/core.c/.h`, `source/settings.c/.h`, `source/ui.c/.h` | Portable option/format helpers, settings model/parser and key/action/layout policy. |
| `source/wtop.conf` | Complete current default configuration generated from the settings model. |
| `source/ntop.conf` | Legacy NTop configuration retained as an import example. |
| `source/build.bat` | Convenience wrapper around the root CMake build. |
| `source/_config.yml` | Retained upstream Jekyll input; unrelated to runtime settings. |
| `source/ntop.nuspec` | Retained NTop packaging metadata; not a publishable Wtop manifest. |
| `CMakeLists.txt`, `tests/`, `tools/` | Build entry point, focused verification and specification tooling. |
| `spec/`, `docs/`, `TODO.md`, `ROADMAP.md` | Contracts, evidence interpretation and implementation plan. |

With a suitable Windows C compiler and CMake 3.16+:

```powershell
cmake -S . -B out/windows-x64 -A x64
cmake --build out/windows-x64 --config Release --parallel 2
ctest --test-dir out/windows-x64 -C Release --output-on-failure
```

The portable core/settings/UI policy can be tested on a development host. Python
is used by repository checks, not by the application. See
[development instructions](docs/development.md) for host tests, target choices,
native verification and CI limitations. CI builds candidates and never creates a
public release from a source push. Historical
[bootstrap observations](docs/bootstrap-verification.md) retain their original
revision, paths and limitations; they do not qualify this iteration.

## Provenance and license

Wtop retains NTop's GPL-3.0-only licensing and upstream notices. The starting
revision is `326ccd80b41842868f4247d1e8978e854658a710`. See [LICENSE](LICENSE).
Use the same license for contributions and record the origin and compatibility of
any reused code. A design inspired by another monitor does not grant permission
to copy incompatible implementation code.
