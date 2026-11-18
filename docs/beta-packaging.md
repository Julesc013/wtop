# Exact portable prerelease assembly

`VERSION` is the product identity. `tools/release_metadata.py` supplies release
title/tag, friendly asset labels, the four-field native version and a bounded
MSI mapping. Native resources order dev, alpha, beta, rc and stable within a patch;
MSI mapping is separate because its field widths differ. Out-of-range versions
are refused, never truncated or derived from a CI run number. This defines
identity only: it does not qualify MSI upgrades or installation.

`packaging/record_build.py` records the SDK selected by the actual generated
CMake/VS project. A merely installed or newest SDK is not sufficient. Each modern
profile is built with a static application runtime, inspected and executed in its
own lane. x86-on-WoW64 and the actual ARM64 host are recorded explicitly.

`tools/release_bundle.py assemble` consumes one to three exact candidate profiles,
actual native receipts, required-test JUnit and final-byte security receipts. It
never compiles, signs or uploads. All included profiles must bind the same clean
source commit/tree/version/license. Mandatory tests cannot be failed, skipped or
missing. Numeric resource metadata must match the shared ledger. An invalid
signature or detected/failed security scan blocks; an unavailable scanner is an
honest draft limitation and public-promotion blocker.

The bundle includes byte-identical standalone/ZIP executables per profile, the
corresponding full source archive, LICENSE, payload/native/security receipts,
JUnit, generated release notes and SHA256SUMS. Source verification reconstructs
the Git tree without extracting untrusted paths. Verification checks architecture,
resources, source/version/license, exact archive contents, hashes and inventory.
Checksums alone are not authentication. No MSI, setup EXE, historical binary or
community package listing is silently added to the portable beta inventory.

The publisher checks draft/tag/source identity and acquires the uploaded bytes
again. Public alpha/beta eligibility additionally checks live feedback and final
security evidence. Human review is optional for these channels and remains a
stable gate. An automated receipt cannot masquerade as human review.

The original schema-1 Beta 1 verifier remains available. Candidate stable
promotion is still refused. See [publication](beta-publication.md) for the channel
boundary and [Beta 2 verification](beta2-verification.md) for test limitations.
