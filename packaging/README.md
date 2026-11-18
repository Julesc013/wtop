# Exact payload delivery candidates

This directory contains executable packaging work, **not a qualified installer
release**. Packaging never rebuilds, signs or relinks `wtop.exe`. All modern
profiles come from `spec/catalog/targets.json`; their operating-system floors
still require exact native evidence. The application remains one payload for
portable, user and machine placement.

## Portable candidate

Record the actual build and package its final bytes:

```powershell
python packaging/record_build.py out/windows out/build-provenance.json
python tools/package_candidate.py create --exe out/windows/Release/wtop.exe --output out/candidate --profile win-nt-x64 --provenance out/build-provenance.json
python tools/package_candidate.py verify out/candidate
```

The directory contains the unchanged EXE, a deterministic ZIP, license,
third-party notices (required since Beta 3),
`wtop.conf.example`, portable instructions, `payload.json`, `candidate.json` and
`SHA256SUMS`. The ZIP's payload record is separate from wrapper records, avoiding
a self-referential hash. The verifier independently reparses the final PE and
compares the ZIP's executable bytes. The inspector records ordinary/delayed
imports, architecture, subsystem, header versions, resources/manifests and
certificate presence. It does not establish API availability or trust an
Authenticode signature merely because a certificate table exists.

`python tools/package_candidate.py promote out/candidate` deliberately fails.
The local build, packaging-time dirty flag and reported compiler inputs are not
proof that a particular commit reproducibly produced those bytes. Signing,
corresponding-source delivery, host qualification and channel acquisition remain
separate gates. Do not advertise these archives as supported releases.

## Modern MSI candidate

WiX is pinned to **4.0.6**. The script verifies its exact version and neither
upgrades it nor accepts a license agreement. On a Windows development host with
MSVC and CMake:

```powershell
dotnet tool install wix --tool-path out/wix --version 4.0.6
$env:DOTNET_ROLL_FORWARD = 'Major'
python packaging/build_msi.py --candidate out/candidate --stage out/msi-stage --wix out/wix/wix.exe --msi-version 0.0.2
```

The numeric MSI version uses the shared `wtop.version/1` ledger by default:
`0.4.0-beta.2` maps to `0.4.402`, and `0.4.0-beta.3` to `0.4.403`. SemVer suffixes
are never silently dropped, and four numeric fields/out-of-range fields are
rejected. The optional command-line version override remains candidate-only and
is recorded as a promotion blocker; CI does not derive identity from run numbers.
The package defaults to user scope. MSI 5.0
single-package authoring redirects its program-files and HKMU registrations:

```powershell
msiexec /i PATH-TO-CANDIDATE.msi ALLUSERS=2 MSIINSTALLPERUSER=1
msiexec /i PATH-TO-CANDIDATE.msi ALLUSERS=1
```

The second command explicitly requests machine scope and may require elevation.
With `ALLUSERS=1`, Windows Installer ignores the per-user selector authored as
the package's default. This also avoids shell-dependent empty-value quoting.
The candidate does not add PATH tokens, startup entries, services, scheduled tasks or
an updater. `ADDTOPATH=1` is explicitly refused instead of being silently ignored.
Use the full executable path. Optional owned PATH/command-alias integration remains
unimplemented in MSI. Package-manager preparation uses each manager's owned shim
or portable alias. MSI is not a delivery mechanism for DOS, Win16, CE or ordinary
WinPE images. A Windows Installer mechanism floor is not Wtop's qualified floor.

The owned record is
`HKCU|HKLM\Software\Wtop\Installations\<profile>` in the matching registry
view. `SchemaVersion` is DWORD `1`; `InstallPath` is an absolute REG_SZ payload
directory. `Owner`, `ProductCode` and `PayloadSha256` identify MSI ownership.
Settings remain under the user's configuration directory outside the payload;
the MSI carries no active `wtop.conf` and deletes no user data.

### Preservation and its current boundary

An embedded, synchronous native DLL checks the actual destination and exact
hashes inside the MSI execution sequence before `InstallInitialize`. It also
passes bounded immutable context to deferred read-only checks immediately before
the relevant `RemoveFiles`/`InstallFiles` operations. It runs
for direct installation, repair and uninstall, including callers that bypass
the acquisition script. It refuses existing payload-name collisions, missing or
changed ownership records, changed owned EXE/license files, inaccessible files,
hard links and reparse destinations. Missing owned files are permitted so MSI
can repair or remove an otherwise verified installation. Unknown filenames and
foreign registry values are never put into deletion tables. No recursive folder
deletion or wildcard RemoveFile action is authored.

Machine destinations must lie under the actual Program Files folder for that
architecture, independent of caller-supplied MSI directory properties. Existing
directories must have a trusted owner (SYSTEM, Administrators, or TrustedInstaller)
and no nonprivileged write ACL. Reparse parents and unsupported ACL forms are
refused. Wtop never changes an existing directory's owner/ACL to make it pass.
Restart Manager checks for live users of each payload file. Maintenance refuses
when a file is in use or cannot be checked; it requests neither shutdown nor
restart. Windows Installer's automatic Restart Manager closure is disabled.

Major upgrades now use `RemoveExistingProducts` **after `InstallInitialize`**, so
old-owner removal participates in rollback. A new package accepts exactly one
older registered owner with its cached MSI present, and that old MSI's embedded
guard must authorize removal of its own hashes. Downgrades and another product
claiming the same version are refused. Upgrades must remain in the same install
context. Windows Installer rollback cannot be disabled for this candidate.

**The checks are not atomic compare-and-delete/replace.** A hostile same-user
actor can still change a path after a check and before a standard MSI file action.
Machine ACL restrictions and a deferred recheck narrow the exposed cases; they
do not prove hostile-race safety. Power loss/restart recovery, prior-release
application-version upgrades, independent scope coexistence and the native target
matrix remain qualification work. Consequently MSI metadata says `qualified:
false`, and promotion fails even when fixture transactions pass. Preserve changed
files and resolve conflicts before maintenance; the installer does not delete
them to unblock itself. MSI is retained as a CI candidate, not included among the
portable prerelease's supported download assets.

The guard is embedded in the MSI Binary table, not installed beside the app. Its
build may use extra build-time tools, while the installed payload remains the
same single EXE. The guard's callback, ownership and retirement rules are in
`msi/guard.c`; its generated hashes are tied to the candidate directory.

## Native tests and acquisition

Only on a disposable Windows VM/runner with no existing profile registration:

```powershell
python tests/test_native_install.py out/candidate --disposable-host --machine --restricted-user
```

This harness exercises both scopes, exact application/legal-file hashes,
registration, rollback-disabled refusal, protected machine destinations,
missing-file repair, changed-payload repair/removal refusal, running-image refusal,
major upgrade, downgrade/same-version refusal, rollback after actual script
execution, foreign-file/registry retention, fresh collision refusal and user-data
retention. It checks both raw user/machine PATH values remain byte-for-byte unchanged.

Upgrade and rollback probes are explicitly marked local MSI copies with changed
product/version identities and unchanged application bytes; they are never
uploaded. They isolate wrapper transaction behavior, and cannot stand in for a
real Beta 2-to-Beta 3 file-version/OS matrix. Failure injection is authored only
into the disposable copy, never into the distributable MSI.

`--restricted-user` repeats the per-user lifecycle under a medium-integrity
primary token with Administrators disabled and privileges removed. The child
asserts it has no administrator membership and writes its own receipt. This
creates no account/service/task and is ordinary-token automation, not a new-user
profile test, UAC interaction or human desktop review. Failed commands/logs are
retained. Unittest discovery does not run this mutating harness.

## Offline package-manager preparation

After assembling a verified portable bundle:

```powershell
python tools/prepare_distribution.py prepare --bundle out/beta --output out/distribution-preparation
python tools/prepare_distribution.py verify --bundle out/beta --output out/distribution-preparation
```

This emits three WinGet manifests, a Scoop manifest, a new Wtop Chocolatey nuspec
and checksum-pinned download script, instructions and a source/hash-bound receipt.
It has no network or publication operation. Architecture and URL inventories come
only from the verified release profiles; no missing architecture is invented.
The original `source/ntop.nuspec` remains historical attribution, not a Wtop feed
identity. Proposed identities are `Julesc013.Wtop`, `wtop`, and `wtop.portable`.
They are not reserved or accepted listings.

The explicit Chocolatey package version maps `0.4.0-beta.3` to
`0.4.0-beta0003` because the community repository still documents a dotted
prerelease compatibility restriction. The receipt retains the original SemVer.
Native client schema checks, public acquisition, upgrade/uninstall preservation,
scope and alias collision review remain required before submission. Generated
preparation is retained separately in CI, not uploaded into a community channel.

`acquire.ps1` accepts an exact GitHub release asset URL and independently supplied
SHA256, stages it, verifies bytes, and optionally invokes that same MSI. It does
not invent a release URL or WinGet package identifier. A supplied digest verifies
integrity relative to that digest; obtain it through a trusted channel. No
release, GitHub tag, WinGet submission, signing key operation or public upload is
performed by these scripts.

## Primary references checked 2026-11-18

- [Microsoft: single-package authoring and HKMU redirection](https://learn.microsoft.com/en-us/windows/win32/msi/single-package-authoring)
- [Microsoft: MSIINSTALLPERUSER and explicit ALLUSERS selection](https://learn.microsoft.com/en-us/windows/win32/msi/msiinstallperuser)
- [Microsoft: custom-action sequencing](https://learn.microsoft.com/en-us/windows/win32/msi/sequencing-custom-actions)
- [Microsoft: file versioning rules](https://learn.microsoft.com/en-us/windows/win32/msi/file-versioning-rules)
- [Microsoft: rollback custom actions](https://learn.microsoft.com/en-us/windows/win32/msi/rollback-custom-actions)
- [Microsoft: major upgrades and installation context](https://learn.microsoft.com/en-us/windows/win32/msi/major-upgrades)
- [Microsoft: deferred execution custom actions](https://learn.microsoft.com/en-us/windows/win32/msi/deferred-execution-custom-actions)
- [Microsoft: automatic rollback installation](https://learn.microsoft.com/en-us/windows/win32/msi/rollback-installation)
- [Microsoft: ProductVersion limits](https://learn.microsoft.com/en-us/windows/win32/msi/productversion)
- [WiX: package scope](https://docs.firegiant.com/wix/schema/wxs/packagescopetype/)
- [WiX: embedded custom actions](https://docs.firegiant.com/wix/schema/wxs/customaction/)
- [Microsoft: WinGet manifests](https://learn.microsoft.com/en-us/windows/package-manager/package/manifest)
- [Scoop: app manifests](https://github.com/ScoopInstaller/Scoop/wiki/App-Manifests)
- [Chocolatey: checked ZIP downloads](https://docs.chocolatey.org/en-us/create/functions/install-chocolateyzippackage/)
- [Chocolatey: SemVer 2 and community repository limitation](https://docs.chocolatey.org/en-us/guides/upgrading-to-chocolatey-v2-v6/)
