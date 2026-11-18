# Wtop 0.4.0 Beta 2

Experimental modern Windows portable prerelease. This is not stable 0.4.0 or full
htop parity. A draft remains private to repository collaborators until explicit
public promotion. The release manifest and receipts define its exact profile set.

## Changes

Beta 2 keeps the dense htop-style process table, native Win32 implementation,
htop/vi/Emacs maps, configurable columns/meters/tabs/colors and explicit settings
operations. It adds complete offscreen frame composition, changed-run output,
independent paint/sample timing, one footer by default and `Z` pause/resume.

Startup selects no process. Saved views restore presentation preferences only.
Escape cancels an editor or confirmation, closes Help/Setup, or clears selection,
tags and follow state on the main screen. A cancelled live filter restores its
previous value. Ctrl+C quits from every keymap and input context. Escape does not
undo a process action that has already happened or revert committed settings.

Selection follows PID plus process creation time. A process that exits, is hidden
or has its PID reused cannot silently transfer the action target to a nearby row.
The mouse event-ordering repair keeps the displayed model and its hit targets
consistent when a new sample is waiting for the next paint.

## Downloads and verification

Each included x86, x64 or ARM64 profile has a standalone EXE and portable ZIP
containing identical executable bytes. The manifest identifies actual Windows
host, token, compiler/SDK, PE imports, source tree, final-byte tests and security
scan. x86-on-WoW64 is described as such. Native ARM64 tests must execute on ARM64.
Do not infer a profile's presence or success from this list: inspect the draft's
actual assets and `beta-manifest.json`.

The source archive is the corresponding full Git tree. LICENSE, profile-specific
payload/native/security receipts, JUnit results and SHA256SUMS accompany it. The
release workflow reacquires uploaded assets and checks their bytes. Unsigned
binaries are identified as `NotSigned`; hashes establish byte identity, not a
trusted publisher or antivirus verdict. An unavailable security scan is recorded
and blocks public promotion until resolved. Invalid signatures or detected
threats block assembly. No security bypass is required or recommended.

```powershell
.\wtop.exe --readonly --no-config
.\wtop.exe --paths --format json
.\wtop.exe query system --format json
.\wtop.exe --portable
```

Settings save only on explicit request. Normal startup and exit do not write
configuration. The chosen configuration is disclosed by `--paths`; an explicit
portable root owns its settings. Ordinary execution creates no service, scheduled
task, startup entry, updater, installation registry record or PATH change.

## Known limitations

The compatibility statement is the actual native receipt, not every OS able to
parse the PE header. Historical Windows, DOS, Win16, CE, OS/2, RT, ReactOS and
WinPE remain independently unqualified. MSI lifecycle, setup EXE delivery and
community WinGet/Scoop/Chocolatey acceptance are separate work. No installer or
package-manager installation command is advertised as available by this release.

Unicode mode keeps conservative single-cell characters. Unsupported wide,
combining and emoji text falls back predictably; raw Unicode remains in JSON and
configuration. Full grapheme layout, thread rows, affinity, branch collapse, a
full TUI detail panel, GPU/sensor telemetry and full htop parity remain open.
CLI command-line/service inspection and per-core collection are already available
through bounded optional providers.

CPU means one logical processor = 100% by default. Machine normalization is
explicit. Commit is not swap residency, and process I/O is not physical disk
bandwidth. Missing or denied metrics are not zeros. Some baseline Windows and
output calls are not generally cancellable. Normal cleanup and emergency
own-process termination have different restoration guarantees.

## Feedback

Use the repository's enabled feedback route and report only the version, digest,
host/terminal, reproduction and observed result needed to investigate. Do not
attach private process command lines, environment, usernames or full snapshots by
default. The source-side support reporter is opt-in, allowlisted and never uploads.
See [testing](beta-testing.md) and [release policy](beta-publication.md).

NTop ancestry and GPL-3.0-only notices remain intact. Beta 1's release/tag/assets
remain unchanged. Stable human desktop approval is retained; it is not fabricated
from automated Beta 2 input tests.
