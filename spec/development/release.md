---
type: "Wtop Specification"
title: "Release qualification and support"
description: "Promote only exact tested payloads with honest support, packaging and provenance."
resource: "urn:wtop:spec:wt-063"
tags: ["wtop", "development"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-17T01:02:17Z"}
wtop: {"id": "WT-063", "profile": "wtop-spec/1", "version": "0.1.1", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-020", "WT-050", "WT-062"], "requirements": ["WT-REQ-063-01", "WT-REQ-063-02"]}
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
release following the inherited NTop 0.3.x line. The audited source remains
0.1.0-dev.2 until a separately tested version transition. Calling a milestone
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

Use explicit development/prerelease labels, for example 0.4.0-dev.N and
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
