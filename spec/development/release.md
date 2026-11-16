---
type: "Wtop Specification"
title: "Release qualification and support"
description: "Promote only exact tested payloads with honest support, packaging and provenance."
resource: "urn:wtop:spec:wt-063"
tags: ["wtop", "development"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-063", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-020", "WT-050", "WT-062"], "requirements": ["WT-REQ-063-01", "WT-REQ-063-02"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}]
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

## Requirements and acceptance

### WT-REQ-063-01

A release MUST bind its exact delivered bytes to a qualified target/acceptance closure and disclose remaining unsupported capabilities.

Acceptance: WT-AC-063-01 — Inspect manifest, source/import/native records and acquired-byte hashes; a build-only package cannot pass release qualification.

### WT-REQ-063-02

Published support and installation commands MUST reflect actually observed release/package channels and must retain prerelease status.

Acceptance: WT-AC-063-02 — Compare release metadata and package identity against the real published artifact; planned channels remain unavailable.
