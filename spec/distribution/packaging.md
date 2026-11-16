---
type: "Wtop Specification"
title: "Artifacts, installation and update ownership"
description: "Ship portable native payloads first and reuse exactly those bytes in later installation channels."
resource: "urn:wtop:spec:wt-050"
tags: ["wtop", "distribution"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-050", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-020", "WT-040"], "requirements": ["WT-REQ-050-01", "WT-REQ-050-02", "WT-REQ-050-03"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}, {"id": "ntop", "resource": "../references/sources.json#ntop"}, {"id": "winget", "resource": "../references/sources.json#winget"}, {"id": "msi-legacy", "resource": "../references/sources.json#msi-legacy"}, {"id": "winpe", "resource": "../references/sources.json#winpe"}]
---

# Artifacts, installation and update ownership

The first delivery is a standalone native program and an optional ZIP containing
that program, license/notices, example configuration and checksums. Setup, MSI,
package-manager manifests and self-install actions are planned work, not implied
by compilation. One payload per qualified target profile is the application
identity. Portable/per-user/system modes do not require separate implementations.

Installed and portable distributions reuse the same application bytes for a
given version/profile. A future native EXE setup or typed self-install command
can copy those bytes and create owned integration. MSI remains a wrapper artifact,
not a requirement that the portable app embed Windows Installer. Legacy systems
use an era-compatible setup/copy route; WinPE uses portable/image integration.
WinGet requires a modern Windows version, and historical MSI has specific
version/service-pack limits.[^winget][^msi-legacy][^winpe]

Artifact names follow wtop-VERSION-PROFILE-ARCH-FLAVOR.EXT; the installed runtime
name is wtop.exe or the profile's explicitly justified executable form. VERSION
uses X.Y.Z for stable releases and X.Y.Z-dev.N or X.Y.Z-rc.N for prereleases.
Build metadata and source revision remain separately recorded. A stable.N label,
if requested, is channel metadata; it must not falsely imply a semantic-version
stable release. The initial development version is 0.1.0-dev.1.

An installation receipt records product/version/profile, scope, exact executable
hash, owned files, created shortcuts, registry values and any optional PATH entry.
No portable invocation changes PATH, registry, associations or autostart.
Per-user installation requires no elevation when the selected path is writable.
System installation requests elevation only for the actual privileged action.
Uninstall deletes only receipt-owned unchanged files/integration; changed user
configuration and unowned files survive. Moving portable folders stays valid.

Updates stage new bytes, verify identity/integrity and preserve configuration.
No runtime auto-updater or background service is part of the baseline. A failed
install/update must leave a runnable previous installation or an explicit recoverable
state. A package manager owns its own installation record; self-install must not
silently adopt or overwrite it. System and per-user side-by-side precedence must
be visible, not determined by accidental PATH order.

In particular, a package manager's portable package type describes its deployment
mechanism, not Wtop's data-location mode. It may own placement, aliases, PATH,
Add/Remove Programs metadata and removal without invoking an application install
command. Installed Wtop data remains outside that payload root. An application
owner adapter must match the actual registered installation/canonical path; no
assumption about a default package-manager directory can replace that check.
The standalone portable EXE/ZIP never creates those machine integrations.

Sign modern release artifacts where a key and authorized signing route exist.
Record signing/time-stamp algorithms and old-loader trust limits; one modern
signature is not proof that every historical OS accepts it. Build, checksum,
signature, downloaded-byte verification and target execution are distinct records.
No publication, tag or package-manager availability is claimed by this spec.

[^winget]: Modern WinGet platform floor.
[^msi-legacy]: Versioned historical Installer requirements.
[^winpe]: WinPE excludes ordinary MSI installation.

## Requirements and acceptance

### WT-REQ-050-01

Portable and installed channels for a given profile/version MUST use the same application payload and retain upstream license/notices.

Acceptance: WT-AC-050-01 — Compare payload hashes across ZIP/setup/MSI and verify license/source-offer obligations before release.

### WT-REQ-050-02

Install/update/uninstall MUST operate only on explicitly owned integration and preserve unrelated or modified user files.

Acceptance: WT-AC-050-02 — Per-user/system/portable/side-by-side and interrupted-update fixtures preserve sentinel files and recover the previous app.

### WT-REQ-050-03

Artifact names, versions and supported-profile claims MUST derive from version/target authorities and exact delivered-byte evidence.

Acceptance: WT-AC-050-03 — Reject mismatched version/profile/hash manifests and inspect downloaded bytes before a release support claim.
