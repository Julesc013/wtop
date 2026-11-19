# Wtop continuation checkpoint

Updated: **19 November 2026 (AEDT)**. This is a concise continuation record,
not a release qualification receipt. Recheck live Git refs and workflow results
before resuming; preserve earlier failures in the linked verification report.

## Integration and publication

- Canonical branch: `master`, observed at
  `3524c5b54e87af31f1801466434dcdd9e277de86` (Beta 2).
- Active integration: [PR #6](https://github.com/Julesc013/wtop/pull/6),
  branch `work/beta3-20261118`. This continuation started from
  `256a5157970997640710db49fc6e49a8200cbbf0` (Beta 3).
- Beta 1 is public; Beta 2 is an unpublished draft. Beta 3 is not yet merged or
  released. Source integration and public release are separate operations.
- WinGet, Scoop and Chocolatey remain offline preparation only.

## Current blocker

The Beta 3 application passed 25 CTest entries on each of Win32, x64 and ARM64
in the recorded candidate runs. Both workflows still fail at ARM64 fresh
standard-user MSI qualification. The first refusal probe cannot acquire Windows
Installer execution/logging mutexes (system error 5, exit 1603), before evaluating
the Wtop package. Moving the fresh-account sequence before parent-side MSI
initialization did not resolve it.

Exact failing candidate runs:

- [Checks 44278311905](https://github.com/Julesc013/wtop/actions/runs/44278311905).
- [Prerelease 44278312085](https://github.com/Julesc013/wtop/actions/runs/44278312085).

The continuation at `1b02c3333c20504cc7b313f4b2a0a5da07aa0e12` added bounded
diagnostics and repaired a reproducible surrogate/combining-mark editor bug.
[Checks 44463603797](https://github.com/Julesc013/wtop/actions/runs/44463603797)
passed all 25 application entries on every architecture, but the ARM64 fresh
account could not create even a new random mutex in either namespace. The
parent could, and saw no existing MSI mutex. See the dated
[evidence section](beta3-verification.md#19-november-continuation-editor-repair-and-installer-namespace-evidence).

Next action: qualify normal inherited-desktop secondary logon for the fresh
account, including successful namespace probes and the complete MSI lifecycle.
Keep the genuine standard-user requirement, restoration checks, cleanup evidence
and existing machine policy intact. An access-denied result is not successful
installer qualification.

## Delivery sequence

1. Push each diagnostic or repair increment promptly to the active task branch.
2. Review the bounded change and run its focused tests plus required spec/build
   checks. Read the native workflow results at the actual submitted revision.
3. Merge PR #6 when its applicable checks and review pass; verify `master` and
   the draft artifacts produced from that merged source.
4. Record remaining desktop, real upgrade/interruption, performance and package
   client acceptance separately. Public promotion still has the release policy's
   feedback and exact-artifact gates; do not turn a development merge into a
   broader support claim.

## Where to resume

- [Beta 3 implementation, failures and evidence](beta3-verification.md).
- [Beta 3 changes](beta3-changes.md).
- [Build and verification commands](development.md).
- [Remaining acceptance](../TODO.md) and [work registry](../spec/work/units.json).

Keep this file short. Put detailed source-bound outcomes in the relevant
verification report and link them here. Do not commit large generated binaries,
raw logs, credentials or unrelated project material.
