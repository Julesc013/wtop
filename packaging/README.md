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

The numeric MSI version is an explicit candidate-only identity. SemVer suffixes
are never silently dropped, and four numeric fields/out-of-range fields are
rejected. CI uses its bounded run number in `0.0.N`; a production collision-free
mapping is still a release gate. The package defaults to user scope. MSI 5.0
single-package authoring redirects its program-files and HKMU registrations:

```powershell
msiexec /i PATH-TO-CANDIDATE.msi ALLUSERS=2 MSIINSTALLPERUSER=1
msiexec /i PATH-TO-CANDIDATE.msi ALLUSERS=2 MSIINSTALLPERUSER=""
```

The second command requests machine scope and may require elevation. The
candidate does not add PATH tokens, startup entries, services, scheduled tasks or
an updater. Use the full executable path. Optional owned PATH integration remains
unimplemented. MSI is not a delivery mechanism for DOS, Win16, CE or ordinary
WinPE images. A Windows Installer mechanism floor is not Wtop's qualified floor.

The owned record is
`HKCU|HKLM\Software\Wtop\Installations\<profile>` in the matching registry
view. `SchemaVersion` is DWORD `1`; `InstallPath` is an absolute REG_SZ payload
directory. `Owner`, `ProductCode` and `PayloadSha256` identify MSI ownership.
Settings remain under the user's configuration directory outside the payload;
the MSI carries no active `wtop.conf` and deletes no user data.

### Preservation and its current boundary

An embedded, synchronous native DLL checks the actual destination and exact
hashes inside the MSI execution sequence before `InstallInitialize`. It runs
for direct installation, repair and uninstall, including callers that bypass
the acquisition script. It refuses existing payload-name collisions, missing or
changed ownership records, changed owned EXE/license files, inaccessible files,
hard links and reparse destinations. Missing owned files are permitted so MSI
can repair or remove an otherwise verified installation. Unknown filenames and
foreign registry values are never put into deletion tables. No recursive folder
deletion or wildcard RemoveFile action is authored.

**The guard is a preflight, not atomic compare-and-delete/replace.** Another
actor can change a path after validation and before MSI operates. Running-image
locks, hostile path replacement, interruption/rollback, unelevated users,
side-by-side scopes and the native target matrix are still qualification work.
Consequently MSI metadata always says `qualified: false`, and promotion fails
even if the basic install fixtures pass. A bootstrapper-only check would not fix
this boundary. Automatic upgrades are explicitly refused; a safe transaction is
not implemented. Restore/preserve changed files and resolve the conflict before
requesting maintenance again; the installer does not delete them to unblock
itself.

The guard is embedded in the MSI Binary table, not installed beside the app. Its
build may use extra build-time tools, while the installed payload remains the
same single EXE. The guard's callback, ownership and retirement rules are in
`msi/guard.c`; its generated hashes are tied to the candidate directory.

## Native tests and acquisition

Only on a disposable Windows VM/runner with no existing profile registration:

```powershell
python tests/test_native_install.py out/candidate --disposable-host --machine
```

This directly exercises both scopes, exact payload hashes, registration, a
related-upgrade refusal, changed-payload repair/removal refusal, unchanged
removal, foreign-file/registry retention, fresh collision refusal, and user-data
retention. It records the actual token/OS and preserves installer logs. An
elevated CI runner is not evidence of a standard-user UAC test. Unittest discovery
does not run this mutating harness.

`acquire.ps1` accepts an exact GitHub release asset URL and independently supplied
SHA256, stages it, verifies bytes, and optionally invokes that same MSI. It does
not invent a release URL or WinGet package identifier. A supplied digest verifies
integrity relative to that digest; obtain it through a trusted channel. No
release, GitHub tag, WinGet submission, signing key operation or public upload is
performed by these scripts.

## Primary references checked 2026-11-17

- [Microsoft: single-package authoring and HKMU redirection](https://learn.microsoft.com/en-us/windows/win32/msi/single-package-authoring)
- [Microsoft: custom-action sequencing](https://learn.microsoft.com/en-us/windows/win32/msi/sequencing-custom-actions)
- [Microsoft: file versioning rules](https://learn.microsoft.com/en-us/windows/win32/msi/file-versioning-rules)
- [Microsoft: rollback custom actions](https://learn.microsoft.com/en-us/windows/win32/msi/rollback-custom-actions)
- [Microsoft: ProductVersion limits](https://learn.microsoft.com/en-us/windows/win32/msi/productversion)
- [WiX: package scope](https://docs.firegiant.com/wix/schema/wxs/packagescopetype/)
- [WiX: embedded custom actions](https://docs.firegiant.com/wix/schema/wxs/customaction/)
