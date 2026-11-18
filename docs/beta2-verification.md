# Beta 2 implementation and verification

Date: 18 November 2026. This record describes the Beta 2 source changes and the
meaning of their evidence. The release's `beta-manifest.json`, per-profile native
receipts and JUnit files identify its exact revision, binaries and actual checks.
No broad target or acceptance case is promoted by this document.

## Source and observed regression

The work continues canonical `master` from `f0d02559a5795361c28170d2f75417ff2d73096a`
and the renderer branch at `440b28959fc2b5d3c74dfd67012e0177a7ce44dc`, reviewed in
[PR 5](https://github.com/Julesc013/wtop/pull/5). Beta 1's public tag and assets
remain unchanged. The complete supplied plan is [retained here](beta2-plan.md).

[Run 44204901746](https://github.com/Julesc013/wtop/actions/runs/44204901746)
executed merge revision `29f606e`. Its x64 CTest result was **19/20**, with
`native_console_frame` passing and `native_tui_interaction` failing at
“mouse help cancels command editor”. The earlier description of two remaining
failures was stale. The viewport precheck was already present at `440b289`.

## Changes and independent oracles

| Area | Intended behavior | Regression oracle |
| --- | --- | --- |
| Startup | Restore display rules; no selected, tagged or followed process | Native launch observes no selected row; process actions require deliberate selection |
| Selection | Bind a selected process to PID plus creation time | Reorder preserves the same lifetime; removal, filtering and PID reuse clear it without selecting a neighbour |
| Escape | Cancel editor/confirmation, return from panels, clear main-view selection/tags/follow | Editing rollback and main-state checks exercise actual native handlers |
| Ctrl+C | Global quit regardless of keymap/context | Shared resolver matrix, raw key injection and separate native control-event delivery |
| Mouse ordering | Displayed model and accepted hit targets refer to the same frame | Sample adoption waits for a due paint; native command-editor footer click changes to Help |
| Renderer | Changed runs preserve characters and attributes; stale viewport geometry is rejected before any unchanged-frame shortcut | Native private-buffer resize/movement, no stale writes, cache invalidation/recovery, attribute-only changes and bottom-right cell checks |
| Pause | Stop collection, keep the last sample and show its age | Native pause/resume interaction checks |
| Delivery | Per-profile final-byte native results and same-byte ZIP/EXE | JUnit required-test inventory, PE/resource/version checks, source-tree reconstruction, hashes and reacquisition |

Portable modules remain strict C90. Windows integration tests compile the actual
native routines, using an entry-point rename where needed; they do not duplicate
the implementation under test. Native TUI tests use an owned console and owned
fixture processes. They do not send events to the user's unrelated console.

## htop comparison

The reference review pins htop to `21bbc80c7811149ee33f449748addf23740c7b9f`.
Its [Panel](https://github.com/htop-dev/htop/blob/21bbc80c7811149ee33f449748addf23740c7b9f/Panel.c)
starts with selected index zero, and
[CommandLine](https://github.com/htop-dev/htop/blob/21bbc80c7811149ee33f449748addf23740c7b9f/CommandLine.c)
initializes `hideSelection` to false. Its
[MainPanel](https://github.com/htop-dev/htop/blob/21bbc80c7811149ee33f449748addf23740c7b9f/MainPanel.c)
intercepts Escape before the general screen manager: it hides selection and stops
following. It retains the underlying selected row and tags, and a subsequent
normal key unhides selection. Reading only ScreenManager's Escape branch would
therefore give the wrong main-view behavior.

Wtop follows the requested stronger contract: no initial action target, and main
Escape clears the actual target, tags and follow state. It does not merely hide a
highlight that another action can silently reuse. This is an intentional product
difference while keeping htop's compact presentation and discoverable controls.

## Evidence boundaries

- Linux policy/sanitizer execution tests portable logic, not Windows handles or
  historical loaders. In this local container LeakSanitizer cannot execute under
  the host's tracing environment; an explicitly disabled leak detector is not
  leak qualification. Hosted Linux CI runs its own sanitizer configuration.
- x86 executes under WoW64 on a Windows x64 runner. It is not evidence for an
  actual 32-bit Windows kernel or old Windows versions.
- x64 uses the named Windows Server runner. It is not Windows 11 desktop review.
- ARM64 uses the `windows-11-arm` runner and must execute the full required native
  inventory; a successful cross-compilation cannot replace that receipt.
- Automated input/console-buffer checks do not establish human visual quality,
  every terminal host, arbitrary Unicode grapheme layout or accessibility.
- A cooperative quit and emergency fallback are distinct outcomes. Existing
  baseline OS/output calls are not generally preemptible. Cleanup has per-worker
  grace periods and an own-process emergency fallback; this is not a universal
  promise of normal exit and console restoration within one fixed deadline.

## Beta 2 versus remaining product work

The deliverable is an exact-byte modern portable prerelease draft. Native ARM64
and x86 may be included only when their own release lane passes. A failure omits
or blocks that profile; it must never appear as a fabricated success.

The supplied plan explicitly separates alpha/beta automatic gates from stable
human approval. The current request asks for a draft. Stable qualification,
ordinary-user desktop/human review, MSI upgrade/repair/rollback/elevation and PATH
ownership, public package-manager acceptance, real old Windows/DOS/Win16/CE/OS2
execution, and the remaining htop feature scope stay open. No probe is advertised
as a full historical monitor. GUI and a new setup EXE are deferred.

Thread rows, affinity, collapsible subtrees, a full TUI process-detail view,
fielded command-line filtering, richer device telemetry and grapheme-aware layout
remain separately scoped additions. Existing CLI `inspect --command-line
--services`, typed system summaries and optional per-core providers are retained.
None of these open features is represented as completed by a keybinding or a
passing unrelated fixture.
