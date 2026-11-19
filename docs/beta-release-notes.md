# Wtop 0.4.0 Beta 3

Experimental modern Windows portable prerelease. The exact release manifest and
native receipts identify delivered profiles. A draft remains visible to repository
collaborators until public-promotion gates pass. This is not stable 0.4.0 or a
universal historical Windows binary.

## Changes

- **Three row states:** startup has no target; navigation/search/hover gives grey
  focus; Enter/click confirms a lifetime in dark cyan. Moving focus cannot silently
  retarget a confirmed process. Escape unwinds context, then confirmation to focus,
  then focus to none. Ctrl+C remains global.
- **Terminal-default backgrounds:** modern VT output uses SGR 49; legacy consoles
  inherit a palette background. Wtop does not alter opacity, Mica/Acrylic settings
  or host palettes. Focus and selection colours are separately configurable;
  existing explicit background settings migrate.
- **Unicode 17 graphemes:** pinned utf8proc 2.11.0, cluster-safe editing/wrapping/
  clipping and retained wide-cell text. Full projection requires a separate
  bounded native cluster-width probe; VT colour support alone is insufficient.
  Incompatible hosts keep conservative one-cell substitutions and VT colours,
  with raw text and whole-cluster editing preserved. Terminal font, shaping,
  bidi and width limitations remain explicit.
- **Details and threads:** scrollable captured process details, full image path,
  command line, services and thread lifetime counters with status, age and manual
  F5 refresh. Optional reads run in bounded owned child providers outside input.
- **Affinity and collapse:** confirmed, identity-checked single-group affinity
  editing with independent applied/verified outcomes and readonly enforcement;
  Left/Right collapses/expands lifetime-bound subtrees.
- **CLI inspection:** additive --image-path, --threads and --affinity flags for
  inspect PID; exact counters/masks in JSON. Affinity is read-only in the CLI.
  Command-line arguments remain separately opt-in.
- **Delivery preparation:** stronger MSI upgrade/repair/rollback/refusal fixtures,
  restricted-token automation and bounded concurrent-monitor resource tests.
  Offline WinGet/Scoop/Chocolatey manifests are a separate preparation artifact;
  no public package channel is submitted or hosted by this work.

The dense htop-style table, optional Vi/Emacs maps, configurable columns/meters/
tabs/colours, one default footer, pause, sample freshness and action outcomes remain.
Settings save only on explicit request; no selected lifetime is persisted.

## Try the candidate

```powershell
.\wtop.exe --readonly --no-config
.\wtop.exe --paths --format json
.\wtop.exe inspect 1234 --image-path --threads --affinity --format json
.\wtop.exe inspect 1234 --command-line --services --format json
.\wtop.exe --portable
```

Use Up/Down to focus, Enter to confirm, d for details, H for thread rows and a for
affinity. F5 refreshes an inspection panel. F1/F2 expose controls/Setup; F10 and
Ctrl+C exit. With only focus, mutation buttons explain that confirmation is needed.
Termination and affinity review require y to confirm.

BackgroundMode terminal, FocusForeground 15, FocusBackground 8,
SelectionForeground 0 and SelectionBackground 3 are the compiled defaults.
SelectionBackground 6 selects conventional dark yellow. Actual RGB values belong
to the host palette. Setup can change/save these roles. --paths discloses the
configuration owner/destination. Ordinary startup and exit write no settings.

## Exact downloads and verification

Each delivered x86/x64/ARM64 profile has an EXE and ZIP with identical application
bytes. Inspect the actual asset set and beta-manifest; architecture names alone
are not runtime evidence. Corresponding source, GPL/third-party notices, native/
security receipts, JUnit and SHA256SUMS accompany the release. The workflow
reacquires uploaded assets and checks their bytes.

Receipts name host, architecture/token, compiler/SDK, imports and source/payload
identity. x86-on-WOW64 and native ARM64 are distinct. Hashes establish byte
correspondence, not publisher trust. Signing/scanner outcomes remain explicit;
unavailable security evidence blocks public promotion. No security bypass is
recommended.

## Remaining qualification

MSI assets are separate **experimental workflow candidates**, not supported
installers in this portable release. Major-upgrade fixtures do not replace real
prior-release, fresh-account/UAC, interruption/reboot and adversarial-path tests.
MSI PATH integration is unimplemented: PATH remains unchanged and ADDTOPATH=1 is
refused. There is no setup EXE or published package-manager installation.

Restricted medium-token automation is not a human ordinary-user desktop review.
Resource observations have stated durations/growth bounds, not a guarantee of no
leaks forever. Stable release still requires real human/host acceptance.

Thread rows currently expose captured lifetime counters, not sampled CPU rates or
mixed live process/thread sorting. Multi-group/CPU-Set affinity and folded WOW64
masks remain unsupported. All 766 official Unicode boundary cases are tested;
terminal width/shaping/bidi/accessibility still need host review.

DOS, Win16, Win9x, early NT, CE, RT, OS/2, ReactOS and WinPE remain independently
unqualified. Historical probes are not complete monitors. GPU/sensor parity,
complete CLI mutations and the broader 1.0 scope remain work. GUI is deferred.
Some OS/output calls have no universal cancellation deadline; cooperative exit130
and emergency own-process exit124 have different guarantees. A confirmed action
may have applied before shutdown; applied and verified remain separate outcomes.

See the [complete implementation/qualification report](beta3-verification.md),
[source-grounded interaction comparison](interaction-comparison.md),
[testing guide](beta-testing.md) and [release policy](beta-publication.md).
Earlier beta assets remain unchanged. No human acceptance or public availability
is inferred merely from this source-side document.
