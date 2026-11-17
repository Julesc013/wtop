# Exact portable beta assembly

The beta version is compiled into `--version` and native Windows FileVersion /
ProductVersion resources. Resource numeric components are not an MSI ordering
scheme. The full beta string remains authoritative.

`packaging/record_build.py` records the SDK selected by the generated CMake/VS
project, including observed sources; conflicting SDK values fail. An installed
SDK or the newest SDK on the machine is not sufficient. Beta assembly rejects an
unverified SDK and external-runtime dependency floor.

`tools/beta_release.py assemble` consumes an existing exact-byte candidate and an
actual native receipt plus the exact CTest JUnit file. It never builds or signs.
Mandatory tests cannot be missing, failed or skipped. The native receipt also
checks the executed version, the Windows version resource and Authenticode
status. Unsigned beta bytes may be staged and disclosed as `NotSigned`; an invalid
signature blocks. This does not establish reputation or bypass SmartScreen.

The package includes identical standalone/portable executable bytes, a full
source archive at the build revision, LICENSE, payload provenance, native receipt,
test results, release notes and SHA256SUMS. Source verification reconstructs the
Git tree from archive members without extracting their paths. The tree, VERSION
and license must match the payload. No MSI, ARM64 or historical artifact enters
the x64 beta inventory. Existing output is never overwritten.

Verification detects changed assets, wrong architecture, source mismatch, renamed
development binaries, missing tests and inconsistent ZIPs. Checksums alone are
not authentication: trust the repository/release provenance independently.

A valid staged package is **not yet publicly eligible**. `gate` additionally
requires a matching ordinary-user Windows 11 x64 human review receipt and live
public issue feedback. No fixture is a qualification receipt. A human attestation
is reviewable evidence, not cryptographic proof of human behavior.

The original candidate tool still refuses stable promotion. Beta policy is an
explicit separate prerelease scope, not deletion of its original blockers.
