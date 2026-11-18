---
type: "Wtop Specification"
title: "Release qualification and support"
description: "Promote only exact tested payloads with honest support, packaging and provenance."
resource: "urn:wtop:spec:wt-063"
tags: ["wtop", "development"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-17T01:02:17Z"}
wtop: {"id": "WT-063", "profile": "wtop-spec/1", "version": "0.1.1", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-020", "WT-050", "WT-062"], "requirements": ["WT-REQ-063-01", "WT-REQ-063-02", "WT-REQ-063-03", "WT-REQ-063-04", "WT-REQ-063-05", "WT-REQ-063-06"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}, {"id": "release-intent-20261117", "resource": "../references/sources.json#release-intent-20261117"}, {"id": "wtop-audit-20261117", "resource": "../references/sources.json#wtop-audit-20261117"}, {"id": "semver-2", "resource": "../references/sources.json#semver-2"}]
---

# Release qualification and support

A development source checkpoint is not a release. Before a release, identify
the candidate source, version and target artifacts; run the required acceptance
closure; inspect binary/import/runtime dependencies; verify package contents and
license obligations; execute the target behavior; then verify the acquired bytes.
Missing a required native target means that profile is omitted or explicitly
experimental, not silently certified by a neighboring platform.

The support statement lists exact tested images and limitations separately from
the intended compatibility family. Buildable, binary-audited, runtime-tested and
qualified are different evidence levels. Stock RT remains policy-blocked. Historical
toolchains, emulators and actual machines need reproducible identities. Do not
present arbitrary future OS versions as guaranteed merely because the core is C.

Publication has a concrete payload manifest with product version, source revision,
profile/CPU, files, hashes, signing information, licenses and supported invocation.
The version file/resource and release names agree. A prerelease is visibly marked.
Only a published asset observed at its actual release URL is described as available
through that channel. Do not invent package-manager identifiers or installation
commands for an unpublished package.

Rollback and uninstall preserve user data and package-manager ownership. Release
notes explain changed behavior, fixes, migration, tests and remaining gaps rather
than listing only implementation effort. Preserve old release manifests and
compatibility evidence; corrected claims do not rewrite historical outcomes.

The future GUI is an extension of the same application/model after CLI/TUI and
target foundations. Keeping --gui reserved is not GUI completion. Likewise,
setup, sensors, event history, containers, remote operation and obscure CPU ports
are staged capabilities, not default dependencies or prerequisites for the
first useful Windows TUI release.

## Named milestones and version policy

The next intended product release is **Wtop 0.4.0**, the first Wtop-branded
release following the inherited NTop 0.3.x line. The integrated implementation reached 0.4.0-dev.1; public beta candidates use
0.4.0-beta.N and are distinct from the eventual stable release. Calling a milestone
0.4.0 does not publish it, change an acceptance result or qualify a target.

The [dated release-readiness assessment](../../docs/wtop-release-readiness-2026-11-17.md)
records current defects, partial implementation and a proposed first-release
closure. It recommends a qualified modern Windows TUI/CLI and requested install
ownership before expanding historical backends. Every advertised portable,
per-user, machine or package-manager channel needs its own acquire/install/update/
remove evidence. A narrower portable-only release is an explicit scope boundary,
not completion of all installation goals.

**1.0.0** commits to a stable public invocation/output/settings contract and the
explicitly agreed TUI/CLI feature/platform scope. Where that scope includes the
original historical families, unimplemented or experimental ports remain open.
An infeasibility or policy restriction requires a recorded scope disposition;
it cannot silently count as a working port. A modern-only 1.0 is a different
scope decision. GUI remains deferred independently of the TUI/CLI milestone.

Use explicit development/prerelease labels, for example 0.4.0-dev.N, 0.4.0-beta.N and
0.4.0-rc.N, followed by stable 0.4.0. Do not derive product versions from CI run
counts. Under SemVer, 0.4.0-stable.N is a prerelease despite its wording; use
ordinary stable versions and channel/profile metadata. A changed published
payload requires a new appropriate product version; retain source/artifact and
installation-version mappings. Version-zero release quality does not by itself
promise the stable public API associated with 1.0.

No audit checklist, static source review or workflow success automatically
promotes the canonical acceptance/evidence/target registries. Reconcile exact
observations with their oracles and split overly broad scopes where necessary.

## Requirements and acceptance

### WT-REQ-063-01

A release MUST bind its exact delivered bytes to a qualified target/acceptance closure and disclose remaining unsupported capabilities.

Acceptance: WT-AC-063-01 — Inspect manifest, source/import/native records and acquired-byte hashes; a build-only package cannot pass release qualification.

### WT-REQ-063-02

Published support and installation commands MUST reflect actually observed release/package channels and must retain prerelease status.

Acceptance: WT-AC-063-02 — Compare release metadata and package identity against the real published artifact; planned channels remain unavailable.

## Prerelease and stable boundaries

Beta 1 is the historical x64 portable release. Its published bytes and schema-1
records remain immutable. The user-authorized Beta 2 plan extends portable drafts
to independently admitted modern NT x86, x64 and ARM64 profiles. An omitted
architecture is disclosed rather than inferred from a neighbouring test lane.
The existing target identities remain authoritative; no duplicate target or full
historical support claim is created. MSI, community package publication and GUI
remain outside the Beta 2 assets until separately qualified.

The supplied [Beta 2 plan](../../docs/beta2-plan.md) explicitly permits automated
alpha/beta publication after automatic gates. Human desktop review is valuable
but is not a mandatory alpha/beta prerequisite. Stable publication still requires
ordinary-user desktop evidence, human interaction review and final-byte approval.
This change is an authorized channel policy, not an automatically synthesized
human receipt. The present task requests a **draft**; no automatic master push
may publicly publish it.

A draft may record a missing optional observation or unavailable security scanner
truthfully. Failed required tests, changed payloads, invalid signatures or a
detected threat block assembly. Public prerelease promotion additionally needs
live feedback, completed final-byte security scanning and exact-source acquisition
checks. Server CI is not Windows desktop evidence; injected input is not human
visual review. Each receipt states the actual host, token and method.

Candidate construction, native execution, draft staging, public promotion and
post-publication acquisition are separate operations. A release cannot verify
its own public download before publication: verify draft assets first, publish
only after pre-publication gates, then immediately verify the public asset URLs.
A failed post-publication check is a release incident, not permission to replace
bytes under the same version. Retain the failed manifest and publish a new version.

The narrow beta gate receipt supplements the existing broad acceptance cases.
It references their IDs and exact inputs; it does not promote full target support.
Historical audit documents retain their original revision and are not rewritten.

### WT-REQ-063-03

A public beta MUST bind its version, source revision and included portable profile payloads to
passing automated tests, observed build/SDK provenance and same-byte source/license
and archive verification; it MUST exclude unqualified install/profile artifacts.

Acceptance: WT-AC-063-03 — Reject wrong revision/version/profile, dirty source,
missing provenance, failed/skipped mandatory tests, changed EXE/ZIP/source and
unexpected artifacts; retain exact successful candidate hashes.

### WT-REQ-063-04

A stable release MUST have a receipt for each advertised desktop executable
under an ordinary user token and explicit human review of console interaction
and restoration, with host and terminal identities recorded. Automated alpha/beta
receipts MUST disclose their actual OS, CPU, elevation and synthetic-input method;
they MUST NOT assert human review or a different client host.

Acceptance: WT-AC-063-04 — Reject wrong-payload and falsely asserted host/human
evidence in every channel. Admit alpha/beta only with their declared automated
profile checks. For stable desktop qualification, reject server/elevated-only
substitutes and require actual ordinary-user interaction/restoration review.

### WT-REQ-063-05

A public beta MUST have an accessible issue-reporting route and privacy-safe
report instructions; diagnostics MUST NOT collect process names, command lines,
SIDs, usernames, environment, paths or user settings without explicit selection.

Acceptance: WT-AC-063-05 — Verify live repository Issues availability and report
templates; inspect a diagnostic report from a fixture containing secret sentinels
and ensure only the documented non-sensitive allowlist is included.

### WT-REQ-063-06

Beta staging and publication MUST be explicit, exact-source-bound operations;
publication MUST refuse missing gates and existing non-draft releases, and MUST
verify acquired asset bytes without rebuilding or silently replacing a version.

Acceptance: WT-AC-063-06 — Exercise dry-run/draft/publication state transitions,
missing gates, mismatched tag/source and existing releases; independently acquire
and hash the final assets. An offline validator cannot assert live publication.
