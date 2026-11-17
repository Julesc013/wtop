# Distribution, settings paths and removal

The current tree implements **delivery candidates**, version-2 settings and a
native NT settings owner. It does not establish a supported release, a published
Wtop WinGet listing, or support for every target in the catalog. The application
has no self-installer. Candidate archives and MSI authoring are described in
[packaging/README.md](../packaging/README.md); exact native qualification remains
a separate release gate.

## One payload, several delivery methods

A built CPU/ABI/API profile produces `wtop.exe`. The candidate packager copies
those final bytes into a standalone payload and a deterministic
`wtop-<version>-<profile>-portable.zip`; an MSI candidate carries that same EXE.
Packaging does not rebuild, relink or sign the application. The verifier compares
the ZIP payload, recorded hashes and actual PE architecture, imports, subsystem
and other loader metadata. Passing these checks proves neither runtime support
nor signature trust.

The ZIP contains `wtop.exe`, `LICENSE`, `payload.json`, `PORTABLE.txt` and
`wtop.conf.example`. The example is not an active configuration file, and the
current ZIP does not contain a `wtop.portable` marker. Select portable ownership
with `--portable`, an explicit `--portable-root`, or a marker you deliberately
place beside the executable. Running a bare EXE is also supported by the current
candidate without installation; its settings ownership rules differ below.

A product version, target profile and package role are separate identities.
Candidate MSI generation requires an explicit numeric version; it does not strip
SemVer suffixes or silently truncate fields. Production version mapping, signing,
corresponding-source delivery, exact-host qualification and channel acquisition
are unfinished release gates. Candidate promotion deliberately fails. MSI is not
a delivery route for DOS, Win16, CE or ordinary WinPE images.

## Choose and inspect settings ownership

Normal startup and exit do not write settings. Wtop chooses one document, applies
it over compiled defaults, then applies CLI preferences. It does not merge all
files found on the machine. A malformed or inaccessible selected file is an
error, not a reason to try a different profile. An explicit relative path is
relative to the caller's working directory; that directory is never an implicit
search root. Executable-relative paths come from the actual loaded module.

| Selection | Read source | Explicit TUI save destination |
| --- | --- | --- |
| `--no-config` | Compiled defaults; suppress configuration and installation-owner discovery. | None. Explicit `--write-config PATH` remains possible. |
| `--config PATH` | Exactly the named existing document. | The same resolved path. |
| `--portable-root DIR` | `DIR\wtop.conf`, then legacy `DIR\ntop.conf`, otherwise defaults. | `DIR\wtop.conf`. |
| `--portable` or adjacent `wtop.portable` file | The same portable search under the actual executable directory. | Adjacent `wtop.conf`. |
| Matching per-user or machine installation record | User roaming `Wtop\wtop.conf`, then machine application-data `Wtop\wtop.conf`, otherwise defaults. | The current user's roaming `Wtop\wtop.conf`. |
| Unregistered EXE | Adjacent `wtop.conf`, adjacent `ntop.conf`, user roaming `Wtop\wtop.conf`, otherwise defaults. | Selected adjacent file; legacy adjacent input saves to sibling `wtop.conf`; otherwise the user file. |

Explicit `--config` conflicts with `--no-config` and with explicit portable
selection. `--no-config` suppresses implicit persistence even if `--portable` or
`--portable-root` is also supplied. A marker must be a file, not a directory.
Writability, a familiar folder name, and a package-manager-looking directory do
not establish installation ownership. Explicit portable selection takes
precedence over registration.

The settings adapter reads at most 128 child records under
`HKCU\Software\Wtop\Installations` and then the corresponding HKLM key in the
executable's registry view. A matching child requires `SchemaVersion` as DWORD
`1` and `InstallPath` as an absolute REG_SZ directory matching the canonical
module directory. HKCU wins when both scopes match. The application only reads
these records; ordinary use does not register itself. Registration is a settings
routing hint, not evidence that an installer is trusted or a binary is qualified.

The NT adapter resolves roaming and common application-data folders through
optional `SHGetFolderPathW`, without its create flag. `%APPDATA%` and
`%PROGRAMDATA%` describe those folder roles; they are not concatenated environment
strings in the resolver. If user-folder resolution is unavailable, operation can
remain in memory with no implicit save destination. A read-only portable root
never falls back to another writable folder. An all-users install still saves
each user's preferences separately and does not edit its machine default.

Inspect the current resolution before changing anything:

```powershell
.\wtop.exe --paths
.\wtop.exe --paths --format=json --portable
.\wtop.exe --paths --format=json --no-config
```

`--paths` performs path and owner discovery, but does not parse configuration,
collect processes, initialize the TUI or create files. It can report an absent or
malformed explicit config path; this is not a declaration that loading will
succeed. JSON uses schema `1`, kind `settings-paths`, with `owner`, `source`,
`saving_disabled`, `canonical_aliases`, `module`, `read` and `write` fields.
With `--no-config`, owner is `memory`, source is `compiled`, read/write are empty,
and saving is disabled; the actual module path is still reported. `--readonly`
also marks saving disabled while retaining the selected paths. That boolean is a
policy flag, not a check that a nonempty destination is writable. The alias flag
reports availability of handle-based canonicalization; lexical fallback and
nonexistent paths do not establish protection against every path race.

## Explicit save, export and migration

F2 Setup save, `:save`, and the Emacs save binding request persistence. Changes to
theme, keymap, colors, views or ordering otherwise stay in memory. The selected
user `Wtop` directory is created only for an explicit save; arbitrary export or
portable directories must already exist. No save chooses another destination
because the requested one failed.

```powershell
# Export defaults plus selected preferences; no collector starts.
.\wtop.exe --no-config --write-config .\wtop.conf --keymap=htop --theme=htop
# Use an explicit portable data directory that already exists.
.\wtop.exe --portable-root D:\Tools\Wtop
# Inspect the same document without allowing settings or process mutation.
.\wtop.exe --config .\wtop.conf --readonly
```

`--write-config PATH` creates a separate export transaction. Export preserves the
selected document's comments and supported unknown fields without rebasing an
active document's provenance. If the destination exists, it must itself parse as
a supported configuration; malformed and future-version documents are refused.
Destination values are not merged into the export. `--no-config` permits export
of defaults plus CLI preferences; `--readonly` forbids it. Explicit telemetry
logging is separately allowed in readonly mode and uses its named output path;
there is no automatic log/cache directory.

Current output is `ConfigVersion 2`. Version-1 and unversioned legacy documents
load without being rewritten. On an explicit save, stable view IDs are assigned
and migration is recorded. If an existing destination is replaced during that
migration, its original bytes are kept in a sibling
`<destination>.migration-original-<pid>-<tick>-<sequence>`. This also applies to
`--no-config --write-config` over a version-1 destination. When legacy `ntop.conf`
is imported into a new sibling `wtop.conf`, the source itself remains untouched.
Migration originals are not temporary garbage and are not automatically removed
by subsequent saves or disposal.

### Concurrent edits and recovery

A save serializes and validates the complete bounded candidate in memory before
opening its destination for replacement. Cooperating writers use one exclusive
`<destination>.wtop-lock` creation attempt; an existing lock reports `busy`
without waiting or deleting the other file. Loaded bytes and existence are
checked again under that lock and immediately before replacement. A detected
external change reports `conflict` and retains in-memory edits.

The adapter writes an exclusive same-directory `.wtop-tmp-*` sibling, checks its
write/flush/close, then uses `ReplaceFileW` with a reserved backup for an existing
file, or a non-overwriting `MoveFileExW` for a new file. It rereads the destination
and verifies the exact candidate before adopting its saved revision. Only the
captured settings revision may clear the dirty state. Ordinary backups are
removed after verification; migration originals are retained.

| Outcome | Meaning and next step |
| --- | --- |
| `busy`, `conflict` | Another writer or changed baseline prevented a verified save. Preserve edits; inspect/reload or export elsewhere. |
| `invalid`, `unavailable`, `denied`, `unchanged-failure` | Validation, destination policy or an operation before replacement failed. Read the diagnostic; do not assume another path was used. |
| `recovery-required` | A replacement/move failed with potentially changed names. Inspect the destination and every retained candidate/backup before retrying. |
| `committed-unverified` | Replacement reported success, but the exact candidate could not be verified afterward. Inspect/reload; the application does not adopt unrelated bytes. |
| `ok` with a retained-backup diagnostic | The candidate was verified, but cleanup failed. Inspect that backup and reload before another save. |

Replacement failures cannot honestly promise that the original remains under its
original filename. The adapter preserves recoverable candidate/backup paths and
invalidates its save baseline rather than guessing. CLI export failures report
retained paths on stderr. Inspect the actual sibling files as well as the
outcome; a fresh `--paths` process does not know an earlier transaction's recovery
names. Never delete `.migration-original-*`, `.wtop-backup-*`, `.wtop-tmp-*` or a
stale lock indiscriminately. Copy and inspect recovery evidence before deciding
which file to keep. A backup reservation can still be empty if replacement never
started.

The lock serializes cooperating Wtop writers; it is not filesystem
compare-and-swap against external editors. Native filesystem calls can block,
and this synchronous adapter has no hard elapsed-time guarantee. Hard-linked
settings files are refused. Power loss, unusual filesystems, reparse races,
metadata/ACL retention and interruption behavior still require exact native
qualification. The [configuration specification](../spec/configuration/settings.md)
records these boundaries and acceptance cases.

## Candidate MSI and CLI acquisition

The current WiX 4.0.6 authoring defaults to per-user scope and can request machine
scope through Windows Installer. It installs only the unchanged EXE and license
under the profile's programs directory, plus owned MSI metadata. It carries no
active `wtop.conf`, changes no PATH token and installs no service, updater,
startup entry or scheduled task. Use the full executable path. Optional owned
PATH integration is unfinished.

Its settings record follows the schema above and also stores `Owner=msi`,
`ProductCode` and `PayloadSha256` as strings. An embedded synchronous guard checks
ownership, destination collisions and exact owned-file hashes before the MSI
transaction, including direct repair/uninstall. Automatic upgrades are refused.
This guard is a preflight, not atomic compare-and-delete/replace: another actor
can change paths after validation. The package remains explicitly unqualified;
passing basic install fixtures does not establish safe interruption, UAC, every
scope combination or a target support floor. See the packaging document for
build commands and disposable-host tests.

`packaging/acquire.ps1` accepts an exact tagged Wtop GitHub release asset URL, an
independently supplied SHA256 and a new destination under an existing staging
directory. It verifies the download and can explicitly invoke the same MSI in
user or machine scope. It does not execute downloaded EXE/ZIP files, invent a
release URL or install a package-manager listing. A matching digest establishes
integrity relative to that digest; obtain the digest through a trusted channel.
There is no published-release claim in this script. Older environments can
receive already verified files copied from another machine.

## WinGet and the inherited NTop package

WinGet's portable installer manages payload placement, command aliases/PATH and
uninstall registration itself; it does not require `wtop --install`. Its managed
payload directory is not a durable preferences directory. A future Wtop listing
must coordinate installer ownership and settings routing; the current settings
adapter does not infer a WinGet owner from generic WinGet uninstall metadata or a
hard-coded default path. No working `winget install` command for Wtop is claimed.

The upstream package audit checked on **2026-11-16** found `gsass1.NTop` 0.3.21
using a portable manifest for `ntop.exe`, command `ntop`, and dependency
`Microsoft.VCRedist.2015+.x64`, without `ntop.conf`. Its manifest said architecture
`neutral` although the hash-verified payload was PE32+ x86-64. That audit concerns
NTop, not Wtop installation support. Wtop manifests must describe actual artifact
architecture. Inspect the current installed record rather than guessing its
location:

```powershell
winget show --id gsass1.NTop --exact
winget list --id gsass1.NTop --exact
winget --info
```

## Ownership-aware removal

For a direct copy, stop Wtop and remove the EXE and the specific companion files
you placed there. Remove deliberately saved portable/user settings separately if
you want to discard them. Do not recursively delete an arbitrary parent folder.

For an MSI-owned candidate, use Windows Installer. The candidate preserves user
settings and unknown files/registry values; changed owned payloads are a reported
maintenance conflict. Review and preserve changed files before resolving that
conflict. For a WinGet-owned package, use that package's WinGet uninstall; Wtop
must not remove a manager's files or shared PATH location itself. Ordinary Wtop
execution has no installation registration to undo.

User settings, migration originals, recovery copies and explicit telemetry logs
remain deliberate user data. Review them separately, and preserve recovery
copies until their contents are understood. Application removal cannot promise
erasure of Windows' own event records, download history or security logs.

## Primary references

The following external references support platform behavior, not qualification
of Wtop's candidates. The inherited NTop/WinGet audit above is dated 2026-11-16;
MSI candidate implementation references were checked on 2026-11-17 in the
[packaging notes](../packaging/README.md).

- [WinGet documentation](https://learn.microsoft.com/en-us/windows/package-manager/winget/)
- [NTop manifest](https://github.com/microsoft/winget-pkgs/blob/master/manifests/g/gsass1/NTop/0.3.21/gsass1.NTop.installer.yaml)
- [WinGet portable installer](https://github.com/microsoft/winget-cli/blob/v1.29.380/src/AppInstallerCLICore/PortableInstaller.cpp)
- [Known folder roles](https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid)
- [ReplaceFileW, including partial-failure states](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew)
- [MSI single-package authoring](https://learn.microsoft.com/en-us/windows/win32/msi/single-package-authoring)
- [MSI ProductVersion](https://learn.microsoft.com/en-us/windows/win32/msi/productversion)
