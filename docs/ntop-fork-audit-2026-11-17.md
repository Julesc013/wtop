# NTop forks: source audit and Wtop adoption decisions

**Observed:** 17 November 2026. **Upstream baseline:**
`gsass1/NTop@326ccd80b41842868f4247d1e8978e854658a710`.
**Wtop audited:** `1ca153b77e7df203b9f6d1f8cfb2ff472318bc27`, development version
`0.1.0-dev.2`. Wtop **0.4.0 and 1.0.0 are planned releases**, not released or
qualified products.

The useful unmerged ideas are command-line/executable-path/service inspection,
system summaries in CLI output, explicit CPU/unit presentation choices, per-core
collection research, legacy build qualification, and Windows executable metadata.
Several forks contain concrete defects alongside those ideas. Other apparently
active forks are unchanged mirrors or retain contributions already in Wtop's
upstream history. Bulk-merging the fork network would undo fixes and introduce
incompatible ownership and build assumptions.

## Scope, evidence and limits

The [machine-readable inventory](ntop-fork-inventory-2026-11-17.json) records full
commits, trees, branches, tags and topology. The API enumeration returned **87
entries including Wtop**. Git histories were reachable for **86 including Wtop**:
all 24 external forks named in the request and 61 additional external forks.
`betsymikaodi/NTop` was unavailable to Git and returned API 404; its source status
is **unknown**, not presumed unchanged or deleted.

Advertised branches and tags were inspected, including nondefault branches and
detached tag histories. Meaningful changes were diffed against their merge base,
then compared with current Wtop. Ahead/behind counts below are against the pinned
NTop baseline. They measure ancestry, not quality or useful feature counts.
Repository update dates, stars, branch names and larger version tags were not
treated as implementation evidence. Shared copies of upstream's `multiple-cpus`
experiment were counted once.

The inventory contains **155 branch refs, 433 tag refs and 66 distinct referenced
commits** across the reachable forks including Wtop. These are references at the
observed tips, not a count of all historical commits. Default tips comprise 17
exact upstream matches, 59 upstream ancestors, ten with commits not in upstream
(including Wtop), and the one inaccessible entry. Nondefault branches are why
default-tip counts alone would miss useful work.

This is a **static code/history audit**. No fork build script, application or
downloaded binary was executed, and no fork source was imported into Wtop.
Findings derived from source and an API contract are identified as such; they are
not claimed native reproductions. Wtop implementation, native-test evidence and
release gaps are assessed separately in
[release readiness](wtop-release-readiness-2026-11-17.md).

## All requested external forks

`+ / −` means default-branch commits ahead of / behind the baseline. All defaults
are `master` except AkarinVS's `mod`. Short SHAs identify the full records in the
inventory; substantive findings below link immutable source.

| Fork | Default tip; + / − | Meaningful other refs or result |
| --- | --- | --- |
| Alex313031/NTop | `4249098e`; 9 / 0 | Legacy build/resource changes; shared `multiple-cpus`. |
| AkarinVS/NTop | `mod@87b2d8ed`; 3 / 37 | CPU default/static CRT/CI; master `d7d274bf` is an ancestor. |
| rbleattler/NTop | `326ccd80`; 0 / 0 | Identical upstream master. |
| dddmaster/NTop | `326ccd80`; 0 / 0 | `feature/bottom-menu-bar` is also exactly this commit: no bar implementation. |
| pa-0/NTop | `326ccd80`; 0 / 0 | Identical master; shared `multiple-cpus`. |
| mamyun/NTop | `954aa7c9`; 2 / 47 | OneCore-oriented build and Unicode corrections; shared `multiple-cpus`. |
| z-skywalker/NTop | `326ccd80`; 0 / 0 | `mod@d3f9e58e`: +2/−32; case-insensitive filter, Unicode, text sort. |
| x1y9/NTop | `f7e3ca48`; 2 / 20 | Dump-mode system summary; VS2019 x64 build path. |
| serjan-nasredin/NTop | `46a6be46`; 0 / 35 | Ancestor-only default; inherited CI and `multiple-cpus`. |
| r-darwish/zNTop | `df49002e`; 1 / 0 | `args@6bf797a9`: +2/−0; Zig wrapper and parser experiment. |
| patrikolausson/NTop | `e6b94e33`; 1 / 55 | Relational numeric comparator; shared `multiple-cpus`. |
| mymirai-nikki/NTop | `2c85f347`; 1 / 0 | Full command-line display and horizontal scroll; custom `v0.3.5` same tip. |
| MojtabaTajik/NTop | `326ccd80`; 0 / 0 | GetNativeSystemInfo contribution already merged upstream. |
| MathiasMagnus/NTop | `d3c73f9c`; 0 / 47 | `per-core@3111a778`: +7/−47; unfinished WMI/C++ prototype. |
| luisriverag/NTop | `326ccd80`; 0 / 0 | Identical master; shared `multiple-cpus`; tags contain upstream commits. |
| knightmare2600/NTop | `98c9061a`; 9 / 0 | Workflow-only Win32/x64/ARM64 build/archive changes. |
| KarlSchu/NTop | `666d54c0`; 0 / 30 | Two filter branches at `3f0a693b`: +2/−28; obsolete fix/documentation. |
| j-williams15/NTop | `4c741d58`; 0 / 32 | Former WinGet README contribution already merged. |
| happyharryh/NTop | `e3cf5ecf`; 0 / 2 | `fix/add_processname_filter@a7e134ae` already upstream; divergent debug tags. |
| glebm/NTop | `06906a82`; 0 / 64 | M/P/F10/q contribution already upstream; shared `multiple-cpus`. |
| fghzxm/NTop | `4c741d58`; 0 / 32 | Unicode corrections already upstream. |
| CRy386i/NTop | `326ccd80`; 0 / 0 | Identical upstream master. |
| cobrce/NTop | `6c3630f6`; 0 / 22 | Smart-case search tip is already an upstream ancestor. |
| ChGen/NTop | `8f70bdc6`; 11 / 0 | WMI command lines/services, CPU scaling, units, fixes; `wmi_cmd@1c0df3cf` contained by master. |

The additional reachable forks supplied no other unique application feature head
beyond the inherited `multiple-cpus` work. The unique nonlisted
[`bushnerd-workflow@d97ad275`](https://github.com/bushnerd/NTop/commit/d97ad2755063faddc0b1286aa6383f6d6b5aed24)
is +2/−23 and adds a tmate CI debugging action. It is not a monitor enhancement
and is not recommended for routine Wtop CI.

## Features worth adapting

### Command lines, executable paths and services

Two forks explore complementary approaches. Neither provides a safe drop-in
provider for Wtop.

**mymirai-nikki** adds `c` to toggle command lines and Left/Right to scroll. Its
[pinned collector](https://github.com/mymirai-nikki/NTop/blob/2c85f347480950e4ce1f059b6aaf67cedd7111ab/ntop.c)
at lines 581–710 dynamically resolves NtQueryInformationProcess, reads remote
PEB/process parameters, validates lengths, limits the string to 4095 WCHARs,
and replaces CR/LF/tab. Those bounds and optional API lookup are useful ideas.

The integration requests `PROCESS_VM_READ` on the **base observation handle**
(line 835). Failure to obtain optional command-line access can consequently
remove otherwise accessible CPU/owner data. It reconstructs remote structures
with the monitor's own pointer width without a target-bitness dispatch. It reads
every process's command line each poll even while display is disabled. The inline
field alone consumes 4096 bytes per ANSI record or 8192 bytes per Unicode record:
about 3.91 or 7.81 MiB per 1000-record array, before other fields. These are static
size calculations, not measured overhead. Scrolling counts TCHAR units rather
than display cells; truncation is not distinguished from complete data.

**ChGen** queries Win32_Process and Win32_Service, caches command lines and running
service names, and associates them with PIDs. This helps distinguish interpreters,
scripts and service hosts. Its
[WMI provider](https://github.com/ChGen/NTop/blob/8f70bdc651fe2d135827865e9f60120f3adecc7e/wmi.c),
[structures](https://github.com/ChGen/NTop/blob/8f70bdc651fe2d135827865e9f60120f3adecc7e/wmi.h)
and [integration](https://github.com/ChGen/NTop/blob/8f70bdc651fe2d135827865e9f60120f3adecc7e/ntop.c)
show substantial feature work, with these source-level defects:

| Finding | Consequence and required redesign |
| --- | --- |
| CoInitializeSecurity runs at initialization and again on refresh (`wmi.c:263–280,474–491`); any refresh failure returns | Microsoft specifies process-wide one-time initialization; repeated calls yield RPC_E_TOO_LATE. A successful first cache does not establish working subsequent refreshes. |
| Shutdown ignores a 2000 ms thread-wait result, then frees caches/event/critical section (`199–225`) | A timeout is not worker completion. Prove join or retirement before releasing worker-owned objects. |
| WMI enumeration holds the reader cache lock; per-record waits have no total deadline (`366–469,551–613`) | Slow enrichment can block the TUI. Build bounded private snapshots and publish briefly. |
| Cache lookup uses only PID (`703–716`) | PID reuse can attach stale metadata to another process. Include creation identity/generation and freshness. |
| ANSI output allocation uses UTF-16 unit count and ignores conversion failure (`403–416`) | Multibyte expansion may exceed the allocation and leave output unproven terminated. Size conversion output, check results and preserve availability. |
| Fixed 256-TCHAR display output and unchecked service concatenation (`wmi.h:24`, `wmi.c:625–641`) | Long data can overflow the intended presentation contract or invoke invalid-parameter handling. Use bounded fields with explicit truncation and structured service lists. |

These deductions follow the
[CoInitializeSecurity](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializesecurity),
[wait](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject)
and [conversion](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte)
contracts; they have not been reproduced natively in this audit.

**Wtop decision: adapt the capabilities through an optional detail provider.**
Keep executable name, executable path, command line and services as separate
fields. Preserve base observations when enrichment is denied, unavailable, stale
or truncated. Cache by process lifetime, sanitize terminal output, query on demand
or a separate cadence, and require explicit output-field selection before logging
command arguments. Native PEB access must have an explicit architecture policy;
Microsoft documents the
[internal API's compatibility limits](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryinformationprocess).

### System summaries and measurement presentation

[x1y9's summary change](https://github.com/x1y9/NTop/commit/f7e3ca4844a6b5771ea4383a93157bf97aa5f94f)
adds CPU, memory, page-memory and task totals to noninteractive output. This is a
real Wtop gap: the audited
[WriteSnapshot](https://github.com/Julesc013/wtop/blob/1ca153b77e7df203b9f6d1f8cfb2ff472318bc27/source/ntop.c)
emits process records, sample duration and meaning metadata, but no system totals.
Add a typed system summary and text equivalent, using explicit availability,
raw bytes, commit terminology and schema-compatibility treatment. Do not copy the
ambiguous `Pge` label or truncated integer-GB display.

ChGen also explores per-logical-CPU normalization and binary-sized memory.
However, its flat view multiplies process CPU by core count while tree view does
not (`ntop.c:1149–1185`); switching views changes the measurement's meaning.
Its RAM calculation produces GiB but labels it GB. **Adapt explicit options** for
system-share versus one-logical-CPU percentage and SI versus IEC display units;
apply each consistently across views while retaining stable raw metrics.
Its `C` CPU-sort shortcut conflicts with Wtop's htop-style Setup binding and must
not be imported without keymap-specific resolution.

### Per-core counters: two unfinished experiments

Upstream's shared
[`multiple-cpus@9d472fae`](https://github.com/gsass1/NTop/commit/9d472faecf011a8f4117123c8b18b926c3228846)
uses [PDH](https://github.com/gsass1/NTop/blob/9d472faecf011a8f4117123c8b18b926c3228846/pdh.c).
It enumerates the English `Processor` object and creates numerically named
English counter paths. It uses a named auto-reset `NTop` event, infinite waits,
and a local query handle with no retained cleanup path. Each sample allocates a
new usage array; the
[caller](https://github.com/gsass1/NTop/blob/9d472faecf011a8f4117123c8b18b926c3228846/ntop.c)
overwrites the published result without freeing the previous array and publishes
outside the process lock. It sums privileged time with total processor time,
double-counting the privileged component before clamping. Fixed layout and
odd-core handling are unfinished. Named shared events also need multi-instance
analysis; independent monitor instances should not consume one another's sample
notifications.

MathiasMagnus's
[`per-core@3111a778`](https://github.com/MathiasMagnus/NTop/commit/3111a778d1829a175ed6b3c91426716612cea95e),
submitted as [PR #42](https://github.com/gsass1/NTop/pull/42), explores a C wrapper
over C++ WMI. The first version creates WQL queries each sample and uses
WBEM_INFINITE. A later demonstration uses IWbemRefresher/IWbemHiPerfEnum and
property handles, a more reusable collection approach. That richer demonstration
is not integrated into the TUI; drawing remains unfinished/commented. It adds
C++17, C++/WinRT, WIL and Windows libraries. Exceptions can cross its C boundary,
and enumeration indexed by hardware concurrency does not establish stable
processor/group identity.

**Adapt reusable queries, actual user/privileged/total counters, separate sample
and render cadence, and scalable layouts. Reject either prototype as production
collector code.** Wtop needs localized-counter handling, explicit instance/group
identity, bounded cancellation, availability per counter, checked initialization,
ownership, and native topology fixtures before a support claim.

### Legacy builds and executable metadata

[Alex313031's head](https://github.com/Alex313031/NTop/commit/4249098e521ef9874c45545a83e2cb3553468506)
adds icon/version resources, MinGW/GN recipes, cross-build CI, XP-era
header/subsystem flags, PSAPI_VERSION=1, optional GetTickCount64 resolution, and
bounded CRT wrappers. **Adapt resource metadata and explicit import/CRT/CPU
qualification.** Wtop already moved sources, fixes Unicode name matching and
builds several CPU architectures; duplicating build-policy files has little value
without a demonstrated target need.

The fork is **not proven XP-compatible**:

- Its [process open](https://github.com/Alex313031/NTop/blob/4249098e521ef9874c45545a83e2cb3553468506/src/ntop.c)
  still requests PROCESS_QUERY_LIMITED_INFORMATION, which Microsoft marks
  [unsupported on XP/Server 2003](https://learn.microsoft.com/en-us/windows/win32/procthread/process-security-and-access-rights).
- Its [timer fallback](https://github.com/Alex313031/NTop/blob/4249098e521ef9874c45545a83e2cb3553468506/src/util.c)
  widens GetTickCount without extending its approximately
  [49.7-day rollover](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount).
- Its x86 build forces SSE2. OS flags do not qualify every CPU or the compiler's
  CRT imports. Modern MSVC plus an old subsystem field is not a backported CRT.
- Its [legacy workflow](https://github.com/Alex313031/NTop/blob/4249098e521ef9874c45545a83e2cb3553468506/.github/workflows/legacy.yml)
  cross-compiles without an XP execution lane; its PR artifact label uses the
  head SHA while default checkout can build a merge SHA. Record the actual source.

mamyun's [OneCore-oriented contribution](https://github.com/mamyun/NTop/commit/016ae4c1a7f53672ad5a663c0e90a5db1bbaf57c)
fixes TCHAR allocation/output and supplies an internal Windows `sources` recipe.
Wtop already corrects those allocation sizes. Private SDK variables and an
unbounded `_stprintf` substitution do not establish WinPE, RT or CE support.

### Build coverage versus language migration

[knightmare2600's changes](https://github.com/knightmare2600/NTop/commit/98c9061af2005d3815b0b042e90c5c71cc57d1e1)
are workflow-only: Win32/x64/ARM64 builds and ZIPs, proposed in
[PR #82](https://github.com/gsass1/NTop/pull/82). Wtop already has those candidate
architectures and native x86/x64 checks. Neither a build lane nor the fork's
`v1.0.*` workflow-counter tags establish runtime support. Keep Wtop product
versions, exact payloads, tests and release promotion independent of run numbers.

r-darwish/zNTop is a **Zig bootstrap around the original C monitor**, not a complete
rewrite. Its [build](https://github.com/r-darwish/zNTop/blob/df49002ebd23b12db8b1402c87718c442206e2cd/build.zig)
still compiles ntop.c, vi.c and util.c. The
[`args` branch](https://github.com/r-darwish/zNTop/blob/6bf797a93521226c31707b4e71e10bcbe0cd3d2f/src/main.zig)
adds zig-clap and a C argument structure, but names/help are not fully wired and
the C bridge copies arbitrary PID counts into a fixed 1024-entry array. Its
package paths omit required C files/headers/LICENSE. `std.process.exit` also
bypasses deferred cleanup; the OS reclaims the exiting process, but this is not
an embeddable lifecycle model. **Defer migration; retain the existing portable C
parser and inspect toolchain ideas independently.**

## Improvements already retained: credit without duplicate merges

| Contribution | Wtop disposition |
| --- | --- |
| [AkarinVS CPU default/static CRT](https://github.com/AkarinVS/NTop/compare/d7d274bf478c3797a1caea8f66b39282ab2f99de...87b2d8eda4300008e02a9a05651d5c696839a3d8) | Main defaults to CPU descending; CMake selects static MSVC CRT. |
| [patrikolausson relational comparison](https://github.com/patrikolausson/NTop/commit/e6b94e33a28592fb4af31739a564ff5e643cbe7c) | Current integer comparators avoid subtract-and-narrow; floating metrics have a dedicated comparator. |
| [MojtabaTajik native system information](https://github.com/gsass1/NTop/commit/401221608bfcb06b42aec56dc51de75b737d80ba) | Retained; not full processor-group or hybrid-topology support. |
| [KarlSchu name filtering](https://github.com/gsass1/NTop/commit/e8f616e6ae0aa84584ef71eda99b8ebbdcd2524e) and [final TCHAR fix](https://github.com/gsass1/NTop/commit/a7e134ae6f908b60bf8dd0e4b9a8e9240995235b) | Retained with bounded parsing; preserve original Karlheinz Schubert and submitting contributor attribution. |
| [glebm M/P/F10/q](https://github.com/gsass1/NTop/commit/fb5e228f5e3935cdeafbc0b64cd1ceb447c10f41) | Implemented through Wtop's shared action maps and shutdown path. |
| [fghzxm Unicode fixes](https://github.com/gsass1/NTop/commit/a4b92a30578815d97f67c8bffea9a570f175bacc) | Inherited and extended; full display-cell/grapheme qualification remains. |
| [cobrce smart-case search](https://github.com/gsass1/NTop/commit/6c3630f687c88149878b67e3520be7dbae0551df) | Retained with TCHAR-aware case operations and bounded storage. |
| [j-williams15 WinGet instructions](https://github.com/gsass1/NTop/commit/c201c90018f300cc17468b10171e928d26d4ba7f) | Upstream documentation, not a Wtop package listing. |
| ChGen color parsing/tag allocation fixes | Replaced by role-indexed settings and checked identity-bearing tag storage in Wtop. |

z-skywalker's nondefault filter branch illustrates why feature and safety review
must be combined: its new filter rejection skips the process-handle close.
Wtop already closes filtered-out handles. Its case-insensitive filtering/text-sort
ideas can be adopted deliberately without adding Shlwapi or silently changing CLI
semantics.

KarlSchu's later `sizeof(Token)+1` patch still uses pointer size and is obsolete.
It is patch-equivalent to happyharryh's `v0.3.6` experiment, **not** the final
merged fix. Several other happyharryh tags print debug data and return early.
A larger tag number in a fork is not a better implementation. Likewise,
dddmaster's named bottom-bar branch contains no changed code.

## NTop's unfinished work and regression lessons

The upstream repository is unarchived. The latest release record retrieved is
[v0.3.21](https://github.com/gsass1/NTop/releases/tag/v0.3.21), published
23 December 2024, pointing to the audited baseline. Later issues, open PRs and
fork commits show ecosystem activity; they do not establish continuing merged
upstream development.

The [pinned README TODO](https://github.com/gsass1/NTop/blob/326ccd80b41842868f4247d1e8978e854658a710/README.md)
marks resizing/tree/search/following as completed while leaving filtering, all
htop CLI options and other interactive commands open. It is an incomplete status
inventory: `-n` name filtering and `-d` dump mode subsequently existed. Wtop now
has F4 live filtering, htop/vi/Emacs presets, a default-visible function bar,
mouse input and saved presentation settings. That does not mean literal htop
parity, arbitrary key rebinding, complete Unicode layout, or every platform is
finished. See [release readiness](wtop-release-readiness-2026-11-17.md).

Original issues should supply reproducible acceptance cases:

| Issue references | Required treatment |
| --- | --- |
| [#74](https://github.com/gsass1/NTop/issues/74), [#62](https://github.com/gsass1/NTop/issues/62): bar/help/F5/mouse/tree | Core interactions exist; qualify host differences, resize, glyphs and contextual labels. |
| [#57](https://github.com/gsass1/NTop/issues/57), [#71](https://github.com/gsass1/NTop/issues/71): configuration/colors | Test discovery, save/reload, conflicts and installer migration separately. |
| [#79](https://github.com/gsass1/NTop/issues/79), [#34](https://github.com/gsass1/NTop/issues/34), [#68](https://github.com/gsass1/NTop/issues/68): false SYSTEM/zero metrics | Preserve unavailable/denied states; qualify service/domain/GMSA/RDS cases. |
| [#81](https://github.com/gsass1/NTop/issues/81): Cyrillic owner names | Test non-ASCII accounts, encoding, long names and display width; Unicode compilation is insufficient. |
| [#54](https://github.com/gsass1/NTop/issues/54), [#78](https://github.com/gsass1/NTop/issues/78), [#36](https://github.com/gsass1/NTop/issues/36): large-memory sorting | Exercise values above 4 GiB and integer boundaries; do not equate a comparator rewrite with exact issue reproduction. |
| [#51](https://github.com/gsass1/NTop/issues/51), [#64](https://github.com/gsass1/NTop/issues/64): CPU differences | Specify denominator and CPU-time versus frequency-scaled utility before comparing Task Manager. |
| [#60](https://github.com/gsass1/NTop/issues/60), [#76](https://github.com/gsass1/NTop/issues/76): command lines/paths | Add independently available detail fields and bounded collection. |
| [#9](https://github.com/gsass1/NTop/issues/9), [#42](https://github.com/gsass1/NTop/pull/42): per-core counters | Qualify topology, provider state, cancellation and layouts. |
| [#55](https://github.com/gsass1/NTop/issues/55), [#82](https://github.com/gsass1/NTop/pull/82): ARM64 | Native execution and release packaging remain separate from compilation. |
| [#50](https://github.com/gsass1/NTop/issues/50), [#37](https://github.com/gsass1/NTop/issues/37): remote use | Test snapshot pipes, WinRM, disconnect and encoding independently of a local TUI. |
| [#25](https://github.com/gsass1/NTop/issues/25), [#27](https://github.com/gsass1/NTop/issues/27), [#32](https://github.com/gsass1/NTop/issues/32), [#4](https://github.com/gsass1/NTop/issues/4): tree totals/GPU/thermals/network | Separate provider and aggregation contracts; capability absence must be explicit. |

## Adoption order and provenance

For the proposed **0.4.0**, prioritize correctness and qualification of the existing
modern Windows monitor: lifecycle, collection failures, numeric boundaries,
Unicode/host behavior, settings ownership, exact artifacts and installation.
System-summary output and optional inspection fields are useful bounded feature
increments if their acceptance gates can be completed. They are not reasons to
import an unsafe worker/cache wholesale. The
[release plan](wtop-release-readiness-2026-11-17.md) defines scope and gates;
this audit does not independently declare features mandatory or completed.

Per-core topology, richer inspection, explicit measurement preferences and
historical backends belong in dependency-ordered work toward the declared 1.0
contract. No fork supplies a completed DOS-to-modern-Windows universal monitor,
and no inspected patch eliminates the need for per-target runtime evidence.

All 24 named forks retain the upstream GPL license text at their inspected
defaults; Alex renames LICENSE to LICENSE.md. Its SHA-256 is
`3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`.
Inspected source retains Gian Sass's attribution and GPL version-3 wording,
consistent with Wtop's GPL-3.0-only policy. Preserve contribution provenance.
Repository-level licensing alone does not establish rights for newly added icon
assets, every standalone file or transitive dependencies such as zig-clap/WIL.
Review the exact proposed files and dependency versions before copying them.
This audit contributes evidence and adoption decisions; it imports no fork code.
