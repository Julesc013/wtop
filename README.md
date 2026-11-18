# Wtop

Beta 3 work: [implementation, qualification and remaining work](docs/beta3-verification.md),
[interaction comparison](docs/interaction-comparison.md), and [release policy](docs/beta-publication.md).
Automated prerelease gates and stable human qualification are separate.

Wtop is a native terminal process monitor derived from
[NTop](https://github.com/gsass1/NTop). It keeps a compact process table and meters,
with **htop-style controls and colors by default**, optional Vi/Vim and Emacs
bindings, and explicit command-line output and settings operations.

**Canonical development branch: `master`.** The `ntop-baseline` branch preserves
the unmodified NTop starting revision. Develop Wtop on short-lived branches from
`master`; see [branch and upstream policy](docs/development.md#branches-and-upstream).

**Status: version 0.4.0-beta.3 (prerelease candidate).** The current application
backend is Windows NT. Earlier beta release assets remain unchanged. Beta 3
adds separate grey focus and confirmed selection, terminal-default backgrounds,
Unicode 17 grapheme layout/editing, collapsible trees, bounded detail/thread
inspection and identity-checked affinity controls. The shared implementation includes:
corrected measurements/actions, sample freshness, contextual TUI and Setup,
versioned settings ownership, query/inspect/log, bounded optional providers,
exact-byte delivery candidates and separate historical probes.

**This is not a supported 0.4.0 release.** Modern x86/x64/ARM64 builds, native
execution and packaging checks have different evidence scopes. The current
[Beta 3 verification record](docs/beta3-verification.md)
records changes, observed failures, exact checks and remaining gates. DOS/Win16/OS2 probes are not full
monitors; Win9x, early NT, CE, RT, WinPE and ReactOS support is unqualified.
There is no published WinGet package, stable installer or native GUI.

The [earlier release audit](docs/wtop-release-readiness-2026-11-17.md) and
[NTop fork audit](docs/ntop-fork-audit-2026-11-17.md) remain evidence for their
pinned source revisions. They are not descriptions of this newer implementation.

## Start here

- [Portable beta notes](docs/beta-release-notes.md), [tester guide](docs/beta-testing.md),
  [exact packaging](docs/beta-packaging.md), and [draft/publication gates](docs/beta-publication.md).
- [Privacy-aware issue templates](.github/ISSUE_TEMPLATE/bug.yml) and [security reporting](SECURITY.md).

- [TODO](TODO.md): practical closure tasks and remaining risks, linked to work IDs.
- [ROADMAP](ROADMAP.md): implementation sequence, dependencies and completion gates.
- [Specification entry point](spec/START-HERE.md) and [OKF index](spec/index.md).
- [Platform and binary policy](spec/platform/targets.md).
- [Build and verification](docs/development.md).
- [Current native TUI/settings evidence](docs/presentation-verification.md).
- [0.4.0 / 1.0.0 release readiness](docs/wtop-release-readiness-2026-11-17.md) and
  [NTop fork source audit](docs/ntop-fork-audit-2026-11-17.md).
- [Implementation design](docs/implementation-plan-2026-11-17.md) and
  [current implementation record](docs/implementation-verification-2026-11-17.md).
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
wtop --paths --format json
wtop --portable --tui
wtop query system --per-core --format json
wtop query services --format json
wtop inspect 1234 --image-path --threads --affinity --format json
wtop inspect 1234 --command-line --services --format json
wtop log --output .\observations.jsonl --samples 10 --interval 1000
wtop log --output - --duration 5000 --max-bytes 1048576
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
| `--interval MILLISECONDS`; `--delay DECISECONDS` | 50–60000 ms or 1–600 deciseconds; default 1000 ms. `-d` retains NTop's dump meaning. |
| `query processes\|system\|services`; `inspect PID` | Shared snapshot model; inspect can request `--image-path`, `--threads`, `--affinity`, `--command-line` and `--services`. |
| `--per-core` | Optional isolated PDH provider; physical collection requires at least one second. |
| `log --output PATH\|-` | JSONL only; explicit sample count or duration required. Default 16 MiB complete-record output cap; existing files are refused. |
| `--cpu-normalization core\|machine` | Default one core = 100%; machine uses the observed active logical processor capacity. |
| `--readonly` | Disable process mutations, shell execution and configuration saves; explicitly requested telemetry logging remains permitted. |
| `--paths` | Resolve module/configuration/owner paths without loading settings or starting collectors. |
| `--portable`; `--portable-root PATH` | Explicit portable ownership beside the executable or in a named directory. No implicit installation. |
| `--capabilities` | Build features and explicit unavailable areas. |
| `--help`, `-h`; `--version`, `-v`, `-V` | Print metadata and exit before collection. |
| `--config PATH`; `--no-config` | Read an explicit configuration, or disable file settings and implicit TUI save destination. These options conflict with each other. |
| `--write-config PATH` | Explicitly write effective settings to the named file and exit without process collection. Can be combined with `--no-config` to export defaults and CLI overrides. |
| `--keymap htop\|vi\|vim\|emacs` | Select controls; `vim` aliases `vi`. Default `htop`. |
| `--theme htop\|ntop\|monochrome` | Select a built-in palette; default `htop`. Selecting a theme resets its color roles. |
| `-C`, `--no-color`, `--no-colour` | Force monochrome presentation after theme selection. |
| `--mouse auto\|on\|off`; `--no-mouse`, `-M` | Request native mouse reporting with keyboard fallback; default `auto`. `--no-mouse` means `off`. |
| `--status-bar` / `--function-bar`; negative forms | Show/hide the last-row function bar; shown by default. Active prompts and errors remain visible. |
| `--footer functions\|compact\|status\|hidden` | Choose the single footer style; default `functions`. |
| `--status-position banner\|bottom\|hidden` | Place contextual status in the header by default; a second bottom row is explicit opt-in. |
| `--frame-interval MILLISECONDS` | Bound ordinary painting cadence (16–1000 ms; default 50), independently of sampling; input may redraw immediately. |
| `--character-set unicode\|ascii` | Unicode 17 grapheme editing; full projection requires VT and native width checks. Other hosts retain conservative glyph fallback. ASCII remains available. |
| `-p`, `--pid PID,PID` | Filter process IDs. |
| `-n`, `--name NAME,NAME` | Filter names by case-sensitive substring. |
| `-u`, `--user USER` | Exact SID or resolved account name. Unresolved name filtering is explicitly partial. |
| `-s`, `--sort-key COLUMN` | Sort by ID, USER, PRI, CPU%, MEM, THRD, I/O, AGE or PROCESS. |
| `--gui` | Reserved; returns an explicit unsupported-mode error. |

CLI settings override file settings. `--write-config` is a separate action and
cannot be combined with TUI/snapshot/GUI mode selection, output format, another information action or the unsaved `-p`/`-n`/`-u`
process selectors. It does accept presentation preferences to be saved. Help,
version and capability output do not write configuration. Ordinary startup and
exit also make no settings write.

### Measurements and output compatibility

Snapshot JSON **schema 2** contains a typed system summary, process records and
requested provider results. It replaces the old ambiguous `cpu_share` with
`cpu_percent`; the default is one core = 100%, so a multithreaded process can
exceed 100%. CPU uses exact execution-counter subtraction and measured QPC
observation intervals. `--cpu-normalization machine` names its active-CPU
capacity. System commit is `CommitTotal * PageSize` against `CommitLimit`,
separate from physical memory and page-file residency.

Unavailable values have status and `null`, never invented zero. Exact 64-bit
integers use decimal strings. Each observation retains its collection timestamps;
process freshness is three requested intervals, and optional per-core data has
its own observation time. The TUI shows stale/partial state. `owner.value` is a
SID; optional `account_name`, its status and observation time are separate.
Process I/O is read/write transfer activity, not physical disk bandwidth.

Information output remains schema 1. Unknown/malformed options fail with exit 2;
runtime failure is 1, explicit partial/provider/capacity results are 3,
cooperative cancellation is 130, and forced own-process shutdown is 124.
Long options accept `--name=value`; option aliases and units are independent of
keymap. Help is generated from the checked command catalog. See the
[CLI/output compatibility contract](docs/cli-output.md) for fields, statuses, units
and exit/stream semantics.

Logging stays in the foreground, creates an explicitly named new file (or writes
to stdout), and requires `--samples` and/or `--duration` in milliseconds. It stages
each complete JSON record before committing within `--max-bytes`. Known waits are
shortened to the remaining duration. Duration is a collection stopping condition,
not a hard deadline for arbitrary blocking native calls or output devices. The
same qualification applies to baseline collection and emergency shutdown; see
[provider/lifecycle limits](docs/implementation-verification-2026-11-17.md).

## Interactive controls

Startup restores the selected display configuration but leaves process selection,
tags and follow state empty. Navigation/search/hover creates grey focus. Enter
or a row click confirms a process lifetime with a separate selection colour.
Moving focus afterward does not move the confirmed action target.
A selected process that exits or is filtered out is cleared; another process at
that row or PID is never automatically made its action target. Escape is cancel
and back, not undo for an already executed process action.

The table below describes process-view keys. Help, Setup, editors, confirmations
and retained action results have their own context labels and disabled reasons,
shared by keyboard and mouse. The function keys provide a common route through all three keymaps. Process
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
| Up/Down, Page Up/Down, Home/End | Move grey focus without confirming an action target. |
| Enter / row click | Confirm the focused/clicked lifetime. |
| Left / Right | Collapse / expand the focused subtree. |
| `d` / `H` / `a` | Details / captured thread rows / affinity editor; F5 refreshes an inspection panel. |
| Tab / Shift+Tab | Next/previous visible tab. |
| Space / `U` | Tag focused process / clear tags. |
| `F` / `I` | Follow selected process / reverse sort direction. |
| `P` / `M` | Sort by CPU / memory. |
| Ctrl+Left / Ctrl+Right | Previous/next sort column. |
| `B` | Hide/show the function bar. |
| `/`, `\`, `:` | Enter search, filter or explicit command input. |
| Escape | Cancel the current editor/dialog or return from Help/Setup; on the main view clear confirmation/tags to focus, then clear focus/search/follow. |
| `Z` | Pause/resume collection; paused sample age remains visible. |
| `q`, Ctrl+C | Quit (`Ctrl+C` is global, including editors and confirmations). |

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
process text. Affinity is limited to safely representable single-group masks;
thread rows show exact lifetime counters, with sampled thread CPU and mixed main
thread rows still open. Full htop parity and arbitrary plugins remain further work.

### Setup, status and mouse

In F2 Setup, Left/Right selects **General, Meters, Columns, Tabs or Colors**;
Up/Down selects an item. Space/Enter toggles or cycles it. `+`/`-` reorders enabled
meters/columns/tabs or changes a color attribute. `s` saves explicitly; Escape
closes the panel. Column settings belong to the active tab; meters, colors,
keymap, mouse, interval and bar visibility are global.

The function bar is shown on the final row by default. Hiding it returns that
row to the view; active prompts and errors are still displayed. Labels, enabled
states and mouse regions follow the same active context. Freshness, view/filter/
sort, selections and unsaved changes appear in the header by default. `StatusPosition
bottom` explicitly opts into a second bottom row; this is not the default. Setup
also exposes footer/status placement, frame cadence, meter width/style and character
set. A frame is composed offscreen before changed console runs are presented. The view
starts with **Main** and **I/O** tabs. Each tab has its own columns, order, filter, sort
direction and tree preference. Setup supports visibility/order and bounded create, duplicate, rename and confirmed
delete operations. Stable numeric IDs follow tabs through reordering; eight tabs
are the limit, and at least one remains visible.

Mouse input uses native console reporting when available. Click process rows to
select, headers to sort, tabs to switch, or function cells to invoke their
keyboard action. The wheel scrolls. Disabled or unavailable mouse reporting keeps
keyboard operation usable; Wtop restores the caller's input mode on normal exit.
While a text editor is active, other mouse controls are ignored; function keys
and function-bar actions cancel that editor before changing context. Cancelling
a live filter restores its prior value. Console-host behavior, resize races and
visual usability require native acceptance, not just portable key-map tests.
The conservative renderer preserves printable single-cell Unicode in Unicode
mode and replaces unsupported wide/combining/emoji scalars predictably. ASCII mode
substitutes non-ASCII scalars. Raw strings are retained in settings/JSON. This makes
clipping and mouse geometry deterministic. A true CJK/combining/emoji cell renderer
remains separate qualification work.

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
ConfigVersion 2
NextTabId 3
Theme htop
Keymap htop
Mouse auto
FunctionBar 1
FooterStyle functions
StatusPosition banner
FrameInterval 50
MeterWidth 25
MeterStyle bars
CharacterSet unicode
ShowTabs 1
RedrawInterval 1000
HeaderRows title,cpu,memory,commit,summary
TabCount 2
ActiveTab 0
Tab0.Id 1
Tab0.Name Main
Tab0.Visible 1
Tab0.Sort CPU%
Tab0.Descending 1
Tab0.Tree 0
Tab0.Columns ID,USER,PRI,CPU%,MEM,THRD,I/O,AGE,PROCESS
Tab0.Filter ""
Tab1.Id 2
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
routes; a failure is reported and must preserve the previous file. Version 2 migrates older settings on an explicit save, assigns stable tab IDs and
retains a recovery backup. Native writes compare the loaded bytes, use a
cooperative zero-wait lock and verify replacement; concurrent changes are refused.
Candidate MSI ownership detection is implemented; installer qualification remains
separate work.

## Source layout and build

| Location | Responsibility |
| --- | --- |
| `source/ntop.c`, `source/ntop.h` | NTop-derived native process collection, console adapter and application lifecycle; extraction remains incremental. |
| `source/util.c/.h`, `source/vi.c/.h` | Native helpers and explicit command/editor handling. |
| `source/core.c/.h`, `source/settings.c/.h`, `source/ui.c/.h` | Portable options, settings and contextual action/layout policy. |
| `source/metrics.c/.h`, `source/actions.c/.h`, `source/text.c/.h` | Exact counters, typed action results, bounded Unicode decoding and conservative cell projection. |
| `source/settings_store_nt.c/.h`, `source/providers_nt.c/.h`, `source/output_nt.c/.h` | Native settings transactions, isolated optional collection and complete-record logging. |
| `source/legacy/` | Independently built capability probes, not historical monitor support. |
| `packaging/`, `tools/package_candidate.py` | Exact-payload ZIP/MSI candidates, PE inspection and acquisition tooling; release promotion is blocked. |
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

The portable core/settings/UI/metrics/actions/text policy can be tested on a development host. Python
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
