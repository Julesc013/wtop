# Six-patch beta verification — 18 November 2026 AEDT

This is a source-bound implementation checkpoint, not public-beta acceptance.
The UTC workflow date is 17 November 2026. Product version is **0.4.0-beta.1**;
only x64 portable distribution is in the public-beta scope. The GUI, managed
installer, WinGet and historical ports remain separate work.

## Exact reviewed source and observations

- PR: [#4](https://github.com/Julesc013/wtop/pull/4).
- Reviewed head: `795ea2125ecc0dddb75c409430d0fc1e2c26100d`.
- Reviewed source tree: `69ab8208ab0a02ab4c966ab040d4995fd8e9c86b`.
- Actions PR checkout: `511912ecee9b4c68eea1ae9b28423f0332455c65`, same tree.
- [Normal checks 44095005351](https://github.com/Julesc013/wtop/actions/runs/44095005351):
  all four jobs passed. x86/x64 native tests and candidate checks ran; ARM64
  execution was explicitly skipped. The existing MSI lane remains development
  evidence and does not place an MSI in the portable beta.
- [Beta checks 44095005420](https://github.com/Julesc013/wtop/actions/runs/44095005420):
  build, all 18 native CTest entries, embedded version/signature acquisition,
  source-tree reconstruction and complete beta assembly/verification passed.
  Draft staging and public publication were correctly skipped on the PR branch.

The native execution host was Windows Server 2025, build 26100, elevated. Compiler
MSVC `19.51.36260.0`; observed selected SDK `10.0.26100.0` from the generated project.
FileVersion, ProductVersion and executed `--version` all reported
`0.4.0-beta.1`. Authenticode status was **NotSigned**, not an inferred signature
or a failed query treated as unsigned.

Host Python discovery ran 91 cases: 58 passed, 33 explicitly skipped because
native executables or optional emulator dependencies were absent in that phase.
Native cases ran separately in CTest; do not count skips as passes or add nested
cases to the 18 CTest entries as independent suites. Seven portable C90 suites
also passed under strict warnings and ASan/UBSan on the local Linux host.

The TUI receipt contains 14 scenario groups. The added native sizes were 40x25,
80x25, 40x10, 39x9 and recovery to 80x25. The runner could not supply 80x50, recorded
as `host-limited; desktop review required`. Human visual acceptance is false.

## Independently acquired bytes

Downloaded Actions archives were checked against their reported SHA-256 values:

| Artifact | ID | Archive SHA-256 |
| --- | --- | --- |
| Beta bundle | 14332878626 | `f75f06a88843a9cf8b9d6712028c270612a8c330adc473d37a8b6b9c6f40a62a` |
| Native diagnostics | 14332339487 | `c01f2e28a8b84eb04d7fa324707c5d789f60ef82b2f4762f3e48077de9bae822` |

`python tools/beta_release.py verify <downloaded-directory>` passed independently.
It checks the exact EXE, matching ZIP member, source Git tree, source/version/license
correspondence, selected SDK, PE inventory, JUnit and native receipt hashes.

| Reviewed asset | Bytes | SHA-256 |
| --- | ---: | --- |
| x64 EXE | 356352 | `4892d99af6d9274ba9414f99034efe52309bd8e9d1608015663983826807a9fb` |
| x64 portable ZIP | 196850 | `c49c9a968fe967548028a014b6ae081a67d912fa2bbcf29560c7c91123e96d61` |
| Corresponding source archive | 427118 | `2268ca89721c954d66f8da6ad8641b74df9cf8d6884e353254de94ad9fa96d51` |

These are the PR checkpoint bytes. A later master build has its own source and
asset manifests and needs its own acquisition verification; these hashes are not
silently reassigned to another build. Byte identity is not a proof of reproducible
builds, trusted authorship, human usability or support for every Windows version.

## Failures preserved and corrected

1. The initial Windows beta run
   [44092485674](https://github.com/Julesc013/wtop/actions/runs/44092485674)
   failed source-tree reconstruction. Windows Git archive line-ending conversion
   produced a different tree. This was independently reproduced by toggling
   `core.autocrlf` on the same source. Checkout/archive LF policy and binary
   checksum output now preserve the exact Git bytes; the strict tree check remains.
2. Runs [44093767083](https://github.com/Julesc013/wtop/actions/runs/44093767083)
   and [44094373188](https://github.com/Julesc013/wtop/actions/runs/44094373188)
   passed all 18 native tests but failed metadata acquisition. Retaining stderr
   exposed Windows PowerShell loading incompatible PowerShell 7 module paths
   inherited through Python. The metadata child now uses only its own PSHOME
   modules. No execution policy, trust setting, caller environment or registry is
   changed. Failed signature queries still fail the receipt.
   Microsoft documents this intermediate-process behavior in
   [about_PSModulePath](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_psmodulepath).
3. Independent regression tests retain wrong-version/hash/profile, missing or
   skipped native cases, absent human review, disabled feedback, malformed JSON,
   source mismatch and invalid signature refusal. No gate was bypassed to obtain
   the successful packaging run.

## What remains before publication

**Actual ordinary-user Windows 11 x64 review has not occurred.** Follow
[desktop acceptance](desktop-acceptance.md) against the final staged EXE and retain
its exact-hash receipt. A synthetic fixture, Server runner or edited PASS response
cannot supply this evidence.

**GitHub Issues was still disabled in the live repository check.** Issue templates
are committed, but the available connector exposes no repository-settings write.
An owner must enable Issues through repository Settings; the publication job
checks the live setting again. No guessed package ID or public-release URL is
advertised.

After checked integration, master CI may stage a verified **draft**. Public
promotion requires a separate explicit workflow dispatch, complete exact desktop
receipt and live feedback. [Publication instructions](beta-publication.md) describe
reacquisition and post-publication byte verification. Those later outcomes must
be observed, not assumed from this checkpoint.

Existing broad acceptance cases and target profiles are not blanket-promoted.
This narrow beta does not close the full 0.4.0 stable or 1.0 product scope.
