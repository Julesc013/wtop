# Distribution, paths and removal

This document explains the intended delivery policy. The initial Wtop foundation
has no published release, WinGet listing, MSI or managed self-installer. Building
or downloading a CI candidate does not establish a supported release.

## One payload, several delivery methods

Each qualified CPU/ABI/API profile produces an application called `wtop.exe`.
The standalone file runs directly; ZIP adds notices, instructions and portable
configuration examples. Future EXE/MSI setup packages carry the same application
payload. Installer technology is selected per supported environment; MSI is not
an installation route for DOS, Win16, CE or WinPE.

Example future asset names (not current download URLs):

```text
wtop-v1.0.0-win-nt-x64.exe
wtop-v1.0.0-win-nt-x64-portable.zip
wtop-v1.0.0-win-nt-x64-setup.exe
wtop-v1.0.0-win-nt-x64-setup.msi
```

The installed application filename remains `wtop.exe`. A release has one semantic
version; channel, build identity, platform profile and packaging role are separate
metadata. `1.0.0-stable.1` has prerelease syntax in SemVer despite its name. Use
`1.0.0` for a stable release and `1.0.0-rc.1`/`0.1.0-dev.1` for prereleases. MSI
version mapping must reject unsupported ranges/collisions, not truncate fields.

## Direct portable operation

Run an absolute/relative path from any suitable folder. No persistent PATH,
registry, service, scheduled task or startup entry is needed. Portable settings
are explicitly selected and remain with the portable copy; read-only media can
run with in-memory settings. The initial implementation reads an explicit
`--config PATH`, or adjacent `wtop.conf` and then legacy `ntop.conf`; `--no-config`
disables those reads. It does not implement the full future persistence contract.

The future policy and precedence are normative in
[configuration](../spec/configuration/settings.md). Direct portable and managed
installation are distinct modes. Do not identify a managed package just because
its directory resembles a default WinGet path or happens to be writable.

## How WinGet handles a portable application

WinGet consumes a publisher/package manifest, selects the applicable architecture
and installer, downloads the declared URL, verifies its SHA256, resolves declared
dependencies and performs that installer's operation. For `portable`, WinGet
does the placement/registration itself; it does not run `wtop --install`.

Typical default locations:

| Item | Location |
|---|---|
| User payload root | `%LOCALAPPDATA%\Microsoft\WinGet\Packages` |
| User command links | `%LOCALAPPDATA%\Microsoft\WinGet\Links` |
| Machine payload root | `%ProgramFiles%\WinGet\Packages` (x86 uses the corresponding x86 program-files root) |
| Machine command links | `%ProgramFiles%\WinGet\Links` |

WinGet normally creates a package/source-specific directory and command alias,
makes its links directory available through PATH, and records uninstall metadata
in the appropriate user/machine registry. Root settings or an explicit location
can change the destination. Alias creation has a PATH fallback; inspect the
actual installed record rather than assuming a symlink always exists.

Therefore **WinGet portable installation is managed installation of a portable
payload**, not zero-registry operation. Let WinGet own its files, registration,
PATH handling and uninstall entry. Wtop must not silently self-register again.

### What NTop currently installs

As checked on 2026-11-16, `gsass1.NTop` 0.3.21 uses a portable manifest, downloads
only `ntop.exe`, declares the command `ntop` and depends on
`Microsoft.VCRedist.2015+.x64`. It does not install `ntop.conf`. The manifest says
`Architecture: neutral`, but the hash-verified published file is PE32+ x86-64;
Wtop must use the actual artifact architecture in its own future manifests.

```powershell
winget show --id gsass1.NTop --exact
winget list --id gsass1.NTop --exact
winget --info
```

Wtop's future package identifier is reserved in project planning only. Do not
advertise a working `winget install` command until the package is published and
its acquired bytes have passed qualification.

## CLI download without WinGet

A future release should document an explicit tag, exact profile/asset, digest and
source revision. A modern PowerShell example can use `Invoke-WebRequest`,
`Get-FileHash` and `Expand-Archive`; a plain EXE can use `curl.exe` plus a SHA256
check. Download, inspect and verify before running. Do not make piping a remote
script into an evaluator the installation contract. Old targets can receive
verified files copied from another machine and need no modern TLS client.

Current use is **build from source** using [development](development.md), or
inspect an explicitly labelled CI candidate. No placeholder GitHub release URL
is presented as a working installer.

## Per-user and machine installations

Future per-user setup places the application in the appropriate user programs
location; machine setup uses the corresponding program-files directory. Resolve
these with target-specific APIs. Do not copy application files into System32 or
the Windows directory. An optional PATH entry is exact, deduplicated and owned;
never replace the entire PATH value. Existing terminals may need to be reopened.

Installed preferences/themes live outside the package root, with common defaults
separate from user state. The planned `config`/`paths` inspection surface must
report the effective locations and mode. The initial implementation does not yet
write installed preferences, modify PATH or register an installation.

## Complete, ownership-aware removal

- Direct portable copy: stop the process, remove its EXE and the explicitly owned
  portable data directory. Never recursively delete an arbitrary parent folder.
- WinGet-owned copy: use WinGet's uninstall operation. It removes owned payload,
  aliases and registration; it does not understand Wtop preferences as a domain.
- MSI-owned copy: route to MSI. A self-installer must not remove MSI-owned files.
- Future Wtop-managed copy: remove only recorded owned paths/entries; preserve
  changed or foreign files and report them.

Application settings require an explicit remove-settings choice. WinGet may
remove tracked ZIP directories recursively, and portable purge can remove an
installer-created directory including adjacent data. This is why managed-mode
preferences must be outside the payload directory. Uninstall must not remove a
shared PATH directory or a foreign registration. Removing Wtop-owned artifacts
cannot promise erasure of Windows' own logs, download history or security records.

## Sources checked 2026-11-16

- [WinGet documentation](https://learn.microsoft.com/en-us/windows/package-manager/winget/)
- [Portable roots and settings](https://learn.microsoft.com/en-us/windows/package-manager/winget/settings)
- [NTop manifest](https://github.com/microsoft/winget-pkgs/blob/master/manifests/g/gsass1/NTop/0.3.21/gsass1.NTop.installer.yaml)
- [WinGet stable portable installer](https://github.com/microsoft/winget-cli/blob/v1.29.380/src/AppInstallerCLICore/PortableInstaller.cpp)
- [WinGet stable portable registration](https://github.com/microsoft/winget-cli/blob/v1.29.380/src/AppInstallerCommonCore/PortableARPEntry.cpp)
- [MSI ProductVersion](https://learn.microsoft.com/en-us/windows/win32/msi/productversion)
