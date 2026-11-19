# Exact-payload Windows 11 beta acceptance

Run the reviewed candidate as an **ordinary user** on Windows 11 x64, not a
Server runner or an emulated ARM64/x86 substitute. Test tooling needs Python 3.10+
(the Wtop application does not). Extract the candidate's portable archive, then:

```powershell
python tools/desktop_acceptance.py C:\Test\wtop-beta --terminal "Windows Terminal <version>" --output desktop-receipt.json
```

The script verifies the payload hash/source/version, records native workstation
identity, runs read-only smoke queries, and launches the TUI against disposable
processes. All settings live in its temporary directory. It requests separate
human confirmations for readability/resize, keymaps/mouse, readonly controls,
settings, identity-safe termination and console restoration. It provides no
assume-yes path and never uploads data. Review 40x25, 80x25, 80x50 and the tiny
fallback; test normal F10 and Ctrl+C. Human assertions are attestation, not an
automatic proof; review the receipt and attach only that reviewed file.

A receipt binds the exact EXE hash, version and source revision. A source or
payload change requires a new receipt. Server CI and input injection remain
separate evidence. A blocked receipt is useful diagnostics but cannot authorize
publication. Keep original failed receipts rather than editing them to passed.

The owned-console CI matrix also tests 40x25, 80x25, 40x10 and 39x9 recovery.
80x50 is tested there only if the native host permits it; a host-limited result
does not replace the required desktop human review. No configuration is saved
implicitly by resizing or quitting.

## Additional Beta 3 desktop cases

Use the exact Beta 3 EXE that the receipt hashes. In addition to the script's
prompts, record these observations with the terminal version, font, theme and
actual display dimensions. Do not fill human answers from CI or source review.

1. Start with a fresh configuration, then with explicitly saved views. Both must
   restore display preferences without restoring a process action target.
2. Hover and navigate to obtain grey focus. Click or Enter confirms dark cyan;
   move focus to another row and verify the confirmed identity remains clear.
   Escape unwinds any editor/panel first, then confirmation to focus, then none.
   With no target, mutation controls must not act on a hidden current row.
3. Repeat with conventional dark yellow and an explicitly black selection
   background. Test readable text on both light and dark host palettes. Check
   `BackgroundMode terminal` with host transparency enabled where available;
   Wtop must not change the host palette or opacity. Record classic-console
   palette fallback separately from modern terminal-default behaviour.
4. Open details, threads and affinity for an owned disposable process. Check
   observation age, F5 refresh, scrolling, denied/unsupported fields, readonly
   refusal and the separate affinity confirmation/outcome. Collapse a subtree,
   sort/filter and let its process exit; no replacement lifetime may inherit it.
5. Enter combining accents, CJK, a regional-indicator flag, an Indic conjunct and
   an emoji ZWJ sequence in a tab name. Inspect clipping and caret/delete/backspace
   at the left/right edges and after resize. Verify saved raw text independently.
   Record font/shaping/bidi/width limitations instead of treating the 766-case
   segmentation corpus as visual certification. Repeat with ASCII fallback.
6. Exercise Ctrl+C in the main view, each panel, Setup, editors and confirmations
   with htop, Vi/Vim and Emacs maps. Verify process retirement and restoration of
   the invoking console. Record any emergency own-process exit distinctly.

The restricted-medium-token resource fixture supplements this review with
bounded automated observations. It does not complete these visual/usability
cases or fresh-account, UAC and installation acceptance.
