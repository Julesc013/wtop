# Wtop 0.4.0 Beta 3: implementation and qualification

Date: **18 November 2026**. Base: canonical `master` at
`3524c5b54e87af31f1801466434dcdd9e277de86` (Beta 2). This report distinguishes
implemented behaviour, automated observations and remaining release gates.
The release's manifest, source archive, JUnit and native/security receipts bind
the final binaries; a local cross-compile is not a Windows runtime result.

## Product decisions

The GUI remains deferred. `wtop` and `wtop --tui` open the terminal interface;
explicit query/inspect/log commands remain suitable for redirected output.
Canonical development stays on `master`, retaining NTop ancestry and the pinned
`ntop-baseline`; short work branches and reviewed merges avoid a second mainline.
Beta 1 and Beta 2 release assets are immutable inputs, not repackaging targets.

### A real distinction between focus and selection

| State | Entry | Default visual role | Process action target |
| --- | --- | --- | --- |
| None | Startup, or Escape after focus | Terminal-default background | None |
| Focused | Navigation, search or mouse movement over a visible row | Bright text on grey | None unless a separate confirmed row or tags already exist |
| Confirmed | Enter on a focused row, or a row click | Black on dark cyan | Captured PID and creation time |
| Tagged | Explicit Space action on a focused row | Tag emphasis | Explicit bounded batch of captured lifetimes |

Moving focus after confirming a row does not move the confirmed target. Both rows
may have different visual roles. The contextual status and enabled actions refer
to the confirmed identity/batch. Action dispatch still revalidates the actual
process lifetime. Sorting, collection, collapse and PID reuse cannot select a
replacement row by position.

Escape first cancels an editor/confirmation or returns from an inspection panel.
On the main view it clears confirmation/tags and leaves focus on the former
confirmed row; another Escape clears focus, search/follow state and all action
targets. Escape does not reverse an already dispatched OS action. Ctrl+C remains
global across all maps and panels, including native console events and raw ETX.
Blocked native calls retain the documented emergency own-process exit boundary.

The configured view, columns, sorting, tabs, colours and keymap can be restored.
Process identities, focus, confirmations and tags are session state and are never
saved as the next launch's action target. Settings still save only by an explicit
request. The function bar is visible by default; hiding it remains a preference.

### Backgrounds and colours

`BackgroundMode terminal` is the new compiled default. The modern presenter
negotiates Windows virtual-terminal processing and emits **SGR 49** for the
terminal-default background. It does not change the host palette, opacity,
Acrylic/Mica setting or global console defaults. That preserves the host's
background semantics; it does not create transparency on a host without it.

The legacy Win32 block-output fallback captures the inherited background palette
index. `CHAR_INFO` has no terminal-default/transparency flag, so this is explicitly
a palette fallback. Internal semantic attribute bits are removed before native
block output. Intentional focus/selection backgrounds remain explicit, including
an explicitly configured black selection background.

New independently configurable palette-index roles are `FocusForeground`,
`FocusBackground`, `SelectionForeground` and `SelectionBackground`, each 0..15.
Defaults are 15/8 and 0/3 respectively. Set `SelectionBackground 6` for dark yellow
under the conventional Windows palette. Actual RGB values belong to the host's
palette. Existing text, meter, header, footer, tag, follow and error roles remain
editable; this release does not introduce arbitrary RGB theme values.

`BackgroundMode palette` preserves an explicit configured background. A legacy
file with `BGColor` but no new background mode retains palette behaviour.
An explicit old `BGHighlightColor` supplies its foreground/background to the new
selection roles only when those new roles are absent. Additive keys remain in
settings schema 2, and older document-preservation rules still apply.

## Inspection and process controls

`d` opens a scrollable detail panel, `H` a thread panel, and `a` the affinity
editor. The details show observed process identity/parent/owner, CPU interval,
working set, transfer counters, age, full image path, explicitly requested
command line, service associations and affinity with field-specific status.
Read-only inspection can use focus; affinity changes require a confirmed target.

The panels are captured observations with explicit refresh and age/freshness.
They are not a claim that every optional field is continuously live. The TUI
worker owns one coalescing read request/result and a separate retained mutation
outcome. Closing/replacing a read invalidates its generation and requests
cancellation. A confirmed affinity action is never silently overwritten by a
new read. Native provider work is outside the render/input callback.

Thread rows are bounded to 1,024 retained records and a 65,536-entry enumeration
scan. Each includes the owning process identity, TID, thread creation time,
execution-time counters and relative priority with individual availability.
Kernel/user counters are lifetime **100 ns** units; there is no invented first
sample CPU percentage or scheduler state. Live thread sorting, sampled thread
CPU rates and mixing threads into the main process table remain further work.

Affinity reads retain full-width process/system masks. The editor admits only
representable, nonempty masks within the observed system mask, requires a separate
`y` confirmation, and applies through one identity-revalidated process handle.
Its outcome keeps `attempted`, `applied` and `verified` distinct. A successful
change followed by a failed readback is not reported as if nothing happened.
Readonly policy is enforced before opening a mutation handle.

Multiple processor groups and ambiguous 32-bit WOW64 affinity masks are explicitly
unsupported. This avoids Microsoft's documented upper/lower 32-bit OR folding.
CPU Sets/group-aware editing is a future capability, not a truncated current one.
Subtrees now collapse/expand with Left/Right. Collapse identities are lifetime
bound and pruned when their processes disappear; they are not persisted PIDs.

### CLI additions and output compatibility

```powershell
wtop inspect 1234 --image-path --threads --affinity --format json
wtop inspect 1234 --command-line --services --format json
wtop --readonly --no-config
wtop --paths --format json
```

The three new inspection flags are additive and require `inspect PID`.
`--affinity` only reads; no implicit CLI mutation or `--affinity=MASK` syntax is
introduced. Image-path inspection does not imply command-line collection.
Sensitive command arguments remain opt-in. Existing aliases, exit policy and
JSON snapshot schema 2 remain compatible. Thread counters/creation times are
decimal strings; affinity masks are 16-digit hexadecimal strings. Unknown or
denied values remain null with status. See [CLI output](cli-output.md).

## Unicode and presentation architecture

The modern text projection vendors unmodified **utf8proc 2.11.0**, commit
`d7bf128df773c2a1a7242eb80e51e91a769fc985`, using Unicode **17.0.0**. Its licence
and Unicode-data notices are in [third-party notices](../THIRD-PARTY-NOTICES.md).
No Unicode runtime DLL or network download is needed by the application.

Extended grapheme boundaries are shared by text clipping/wrapping, frame storage
and editor cursor/delete/backspace operations. Combining sequences, surrogate
pairs, regional-indicator flags, Indic conjuncts and emoji ZWJ sequences are not
split into independently editable code units. The official Unicode conformance
file supplies **766 boundary cases**; independent fixtures cover widths, clipping,
malformed UTF-16, controls, long clusters and changed-frame comparison.

Terminal width is a policy rather than a Unicode universal: wide/fullwidth and
emoji presentation use two cells, ambiguous characters use one, and an isolated
zero-width cluster gets a dotted-circle base. An oversized cluster is consumed
whole and substituted with an explicit capacity/loss flag. Each rendered cluster
is bounded to 128 UTF-16 units; front/back text arenas have a one-million-unit
ceiling each. Raw model/settings/JSON text is preserved. Bidi overrides and
terminal controls are inert visible substitutions.

VT colour support and grapheme width support are negotiated separately. An
accepted VT output mode preserves terminal-default backgrounds but does not
prove cluster shaping: the second native attempt found classic consoles advance
two cells for `e` plus combining acute where the layout reserves one. Before
selecting full projection, a bounded, never-activated private buffer measures
combining, CJK, supplementary emoji, ZWJ, flag, keycap and dotted-circle advances.
Any disagreement or probe failure selects the conservative glyph repertoire
while retaining VT colours. Setup reports that projection. The probe closes its
temporary handle and does not touch the caller's buffer, cursor or input queue.
UAX29 editing and raw settings/provider text remain intact in either projection;
an unsupported cluster becomes one explicit fallback cell, never split pieces.

This small probe qualifies the tested width families, not all fonts or Unicode
sequences. The external native fixture records its independent host measurements
and checks exact application caret, text-anchor and function-button coordinates
for the resulting projection. Fonts, shaping,
bidirectional text, ambiguous-width terminal preferences, accessibility and
Windows Terminal appearance require additional host/human review. Passing UAX29
segmentation tests does not certify those independent properties.

The existing core stays C90. The pinned upstream Unicode library and its narrow
adapter have separate C99 targets. Native NT providers, console presentation and
asynchronous inspection own native handles. This is a deliberate compilation
boundary; it does not establish a DOS/Win16 compiler or binary. Responsibility
remains separated among `frame`, `text/grapheme`, `ui`, `settings`, `providers_nt`,
`features_nt` and `tui_inspection_nt`, with the inherited application composition
still in `ntop.c`.

## Delivery and qualification boundaries

Portable EXE/ZIP, MSI candidate and package-manager preparation must all consume
the same profile payload. The release version ledger derives native/MSI/channel
versions from `VERSION`, now `0.4.0-beta.3`. Third-party notices travel with the
payload and are included in ownership/digest inventories.

MSI work now exercises per-user/system ownership, repair, major upgrades,
downgrade/refusal, rollback and running images in disposable automation. Upgrade
probes change MSI identities while retaining identical application bytes; actual
prior-release application/file-version upgrades remain a separate gate. The
fixture injects a checked deferred failure only into a disposable MSI, after
actual payload and product-registration writes within the normal installation
script. It checks restoration of the old product owner, payload and native MSI
registration, and absence of the failed new registration. The actual recorded
fixture outcomes govern its status.

MSI leaves both user and machine PATH values unchanged and explicitly refuses
`ADDTOPATH=1`; optional owned PATH integration is **not implemented**. The prepared
package-manager inputs delegate command aliases/shims to the manager instead of
editing an environment string. Alias collisions and package-manager lifecycle
still need native qualification.

The guard now checks protected machine-directory ownership/ACLs and repeats a
read-only check immediately before file operations. These checks narrow the race
window; they are not an atomic conditional delete/replace.
It remains an experimental candidate until hostile path races, interruption,
fresh-account UAC and exact advertised installation environments are qualified.
The release does not silently advertise an MSI as supported because it built.

WinGet, Scoop and Chocolatey metadata is prepared offline from exact release
identity and hashes. No public manifest pull request, bucket, community feed,
package submission or hosting is authorized by this task. External schema/client
validation and channel policies are additional publication gates.

Restricted-token automation makes administrator membership deny-only, removes
privileges and uses medium integrity. The workload confirms `IsUserAnAdmin=false`
and integrity RID 8192; the inherited `TokenElevation` flag remains true. This is
restricted same-user medium-token automation, not a separate standard account.
It is useful evidence of restricted process permissions;
it is **not** a fresh user profile, interactive UAC review, Windows Terminal
appearance review or a person's desktop acceptance. Resource fixtures are bounded
observations with recorded duration/provider load, not proof of zero leaks or
unlimited uptime. A real human stable-release receipt must remain absent until
someone performs that review.

## Verification record

During implementation, the local sanitized portable suite passed all nine
CTest entries, including the official 766-case corpus. Address/undefined-behaviour
instrumentation was enabled; leak detection was disabled only for this container's
ptrace limitation. The Linux Python run had 112 cases: 76 passed and 36 explicit
platform/dependency skips at that checkpoint. These counts may grow with delivery
and resource fixtures; final CI/JUnit is authoritative.

The x64 cross-build compiles the application and native fixtures, including real
production input/tree/inspection code. It is compile evidence only. Final native
x86/x64/ARM64 runs, installer fixtures, security scans and uploaded-byte checks
must be read from the exact release workflow; no Beta 2 receipt is reused.

### First native integration attempt

Source `d08d1de19ad91e5ab8a57397f3e78014905ab97c` was tested in
[Windows checks run 44262855930](https://github.com/Julesc013/wtop/actions/runs/44262855930).
All three profiles compiled and passed 22 of 25 native-suite entries, including
provider, thread/affinity, asynchronous inspection, settings, input and tree
fixtures. Three failures were retained: moved-viewport console positioning, an
external Setup fixture still expecting the old text projection, and the new
restricted-token resource fixture reaching its 180-second outer bound. These
results do not qualify that attempt for release.

The parallel packaging attempt also found a temporary-repository CRLF/LF mismatch
in the third-party-notice test. Its fixture now uses explicit LF bytes and the
production Git attributes; the production corresponding-source byte check is
unchanged. The workflow's missing `beta3-changes.md` input was supplied. Corrected
native results must come from the later exact-source workflow and final receipts;
this failure record is retained rather than retroactively labelled successful.

### Second native integration attempt

Source `9c80c8bef5c76d55a8a21656cec9f65c5e1ae96f` was tested in
[Windows checks run 44264564360](https://github.com/Julesc013/wtop/actions/runs/44264564360).
All three profiles passed 23 of 25 entries. The moved-viewport presenter and
explicit ASCII Setup contracts passed. The new full-Unicode anchor test exposed
actual classic-console width drift: combining text shifted the next glyphs,
caret and function buttons. This is a production capability-negotiation failure,
not a reason to weaken expected cell positions. The repair separates VT colour
support from the bounded native width probe described above and preserves
conservative output on incompatible hosts.

The restricted resource worker also failed before Python produced its receipt,
with process status `0xC0000142` (DLL initialization failure). Those permissions/
process-launch results remain separate from ordinary TUI and provider evidence.
The later exact-source native run must qualify both repairs; this attempt is not
release acceptance.

### Native application qualification checkpoint

Candidate head `269307921de94e2a15025a7dad4f1b7a2781ec89` was tested as PR merge
revision `9267af9cc91460d92ef0f40c415efa03920920d1` in
[Windows checks run 44267580472](https://github.com/Julesc013/wtop/actions/runs/44267580472).
All **25 native CTest entries passed on Win32, x64 and ARM64**. Win32 and ARM64
jobs completed successfully; the x64 job failed later in the separate MSI
lifecycle step. This checkpoint qualifies the observed application tests, not
the installer or a later release's bytes.

The intervening native repairs retained exact viewport/caret/button assertions,
fixed the cell-grid readback and asynchronous thread-panel fixture, and supplied
explicit current-user/SYSTEM object access for the restricted workload. Its
previous default object DACL granted full access to Administrators/SYSTEM while
Administrators was deny-only. Token owner was already the current user; changing
owner is not the demonstrated cause of the repair. The original parent token and
interactive desktop ACLs remain unchanged.

Each architecture ran 12 one-shot queries and three concurrent TUIs, measured for
approximately ten seconds after a two-second warm-up. SHA256-bound diagnostic
receipts record these bounded observations:

| Profile | CPU, percent of one logical core | Maximum private-byte growth | Maximum Ctrl-C retirement |
| --- | ---: | ---: | ---: |
| Win32 | 0.937–1.406% | 135,168 bytes | 9.79 ms |
| x64 | 1.250–1.718% | 425,984 bytes | 7.80 ms |
| ARM64 | 1.562–1.719% | 221,184 bytes | 7.19 ms |

All nine monitor processes exited zero; monitor and repeated-query harness handle
growth was zero. These short windows are not long-run leak or performance proofs.
All three native hosts selected conservative grapheme projection: full Windows
Terminal shaping and human desktop acceptance remain separate gates.

MSI reached real per-user install and same-version refusal, then exposed two
independent defects. A deferred removal guard ran after `RemoveRegistryValues`
had deleted the owner record it needed. The upgrade fixture also attempted to
SQL-update primary-key version bounds in the `Upgrade` table, which MSI rejects;
cleanup had masked that primary error. The repair moves removal validation before
owned registry removal and retains both primary and cleanup traces. In
[checks run 44268684939](https://github.com/Julesc013/wtop/actions/runs/44268684939),
candidate `05dfcaf855a980ab77288c5433c8e09197bbe713` again passed all 25 native
entries on each architecture, and uninstall cleanup succeeded. The copied MSI
fixture's attempted key replacement returned error 1627. The subsequent fixture
repair retains complete bounded native records, closes the read cursor, and
reconstructs the copied `Upgrade` table through parameterized inserts, preserving
NULLs and field types. Extended MSI errors are captured before later API calls
can replace them. A later exact-source native run must qualify the repair and
remaining lifecycle cases; no result is inferred here.

### Installer lifecycle checkpoint

Candidate `fd0fe18aff85b255369a15229adb7158d0766ac1`, tested as PR merge
`a02c98f633628c3c541305022466b7ddab54fd10` in
[checks run 44269585860](https://github.com/Julesc013/wtop/actions/runs/44269585860),
passed the complete **elevated per-user and machine** MSI fixture sequences on
Windows Server build 26100. Both receipts record exact EXE/legal-file delivery,
missing-file repair, changed-file preservation, running-image refusal,
same-payload major upgrade, downgrade/same-version refusal, actual-script rollback
restoring the old owner, uninstall, foreign-file/registry and user-data retention,
plus unchanged user/machine PATH. The numeric MSI version was `0.4.403`.

The same run's restricted-user repetition failed before Wtop's installer actions:
Windows Installer returned **1625**, with effective `DisableMSI=1` and the logged
unmanaged/non-admin policy denial. It recorded no successful restricted-user
scope. This agrees with
[Microsoft's documented policy](https://learn.microsoft.com/en-us/windows/win32/msi/disablemsi).
It is a host policy boundary, not evidence that ordinary-user installation passed.

CI verifies that Server refusal explicitly, without modifying policy, while
requiring a separate full standard-user lifecycle on the native Windows 11
ARM64 client. These are different outcomes, each retained in its receipt; an
unexpected refusal on the client remains a failing gate. MSI artifacts are named
per profile and remain separate experimental candidates. The final exact-source
receipts must establish the outcome of those newly separated host checks. Real
x64 client/fresh-account/UAC, prior-release file-version upgrades and hostile or
interrupted filesystem transactions remain unqualified.

Candidate `4e7ca9ff94a019ae9b7093b8d6ab0c3af6a0ea55` in
[release run 44271054646](https://github.com/Julesc013/wtop/actions/runs/44271054646)
passed the Server policy probe and the elevated ARM64 per-user/machine sequences.
The ARM64 restricted client then exposed a stronger rollback requirement: payload
and Wtop-owner restoration alone was insufficient, because the failed newer MSI
product remained registered. Its log retained access-denied rollback errors.
The initial test injection had added an extra `InstallExecute` and an immediate
failure, a boundary absent from the production package. The revised fixture
instead schedules the existing guard's fail-closed missing-context path as a
checked deferred action after `PublishProduct`, inside normal `InstallFinalize`.
The oracle now requires actual preceding writes, old/new native registration
states and absence of skipped failed rollback operations. That is a stronger
restoration check; a later exact-source run must establish its result. This
earlier registration failure is retained, not retroactively treated as a pass.

Candidate `8ae413e5a2b01b37ee5a36d6e9f2b4a662ba1214` in
[checks run 44272594615](https://github.com/Julesc013/wtop/actions/runs/44272594615)
still failed the restricted-client rollback oracle under that normal deferred
boundary. This rules out the extra execution phase as a sufficient explanation.
`IsUserAnAdmin=false` alone does not establish an unelevated token: the synthetic
token retained `TokenElevation=true`, and the same account had previous elevated
installer state. The new client fixture therefore requires a fresh standard
account/profile, actual non-elevated token and full lifecycle/cleanup evidence.
It does not change machine policy or relax restoration assertions. Installer
observations use schema 2 to separate enabled administrator membership, actual
elevation and integrity. Server policy refusal and restricted resource stress
coverage retain their distinct, explicit meanings. Native results must still
qualify this corrected client context; no human desktop receipt is synthesized.

## Remaining work and recommended sequence

1. **Finish exact Beta 3 qualification:** resolve actual native fixture failures,
   retain source/payload/JUnit/security receipts, and stage a draft. Public beta
   additionally needs the enabled repository feedback route required by the
   existing release policy. Do not weaken that gate to turn a draft green.
2. **Complete installation acceptance:** fresh standard-user/system accounts,
   elevation UX, real interruption/reboot/recovery, adversarial path/ACL tests,
   coexistence and package-client validation. Retain an old runnable installation
   or an explicit recovery record on every failed update.
3. **Perform desktop acceptance:** Windows Terminal and classic console, light
   and dark palettes, default backgrounds, grey/cyan/yellow focus roles, Unicode,
   high DPI, keyboard maps, mouse, accessibility, resize and cancellation.
4. **Broaden resource/performance evidence:** high process/thread counts, many
   cores/groups, churn, slow/denied providers, long sessions and several monitors.
   Measure CPU/working set/private memory/handles at stated intervals; don't
   equate a small executable or C implementation with low overhead.
5. **Complete remaining inspection/control depth:** sampled thread CPU, group-aware
   affinity/CPU Sets, richer optional subsystem providers and typed CLI mutations
   with retained per-target outcomes. Preserve readonly and lifetime checks.
6. **Reduce application composition debt:** extract CLI serialization and panel
   projection behind existing tested models, without forking identity/action/
   settings rules. Keep one owner per native resource and error outcome.
7. **Advance historical backends independently:** loader/toolchain probes,
   collection capabilities and native guest evidence per DOS/Win16/Win9x/early
   NT/CE/RT/OS2/ReactOS/WinPE profile. Current PE builds are not universal binaries.
8. **Promote stable 0.4.0 only for qualified advertised scope.** The broader
   originally agreed 1.0 platform/capability scope remains visible; completing a
   modern beta does not silently redefine it. GUI stays out of this milestone.

## Primary implementation references

- [utf8proc 2.11.0 source](https://github.com/JuliaStrings/utf8proc/tree/v2.11.0)
- [Unicode 17 grapheme corpus](https://www.unicode.org/Public/17.0.0/ucd/auxiliary/GraphemeBreakTest.txt)
- [Microsoft console VT sequences](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences)
- [Windows Terminal colour guidance](https://learn.microsoft.com/en-us/windows/terminal/troubleshooting#technical-notes)
- [Microsoft WOW64 affinity semantics](https://learn.microsoft.com/en-us/windows/win32/winprog64/processor-affinity)
- [Upstream interaction comparison](interaction-comparison.md)
