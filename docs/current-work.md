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
- The independent `work/continuity-20261119` branch starts from master and
  integrates checkpoint/CI policy without importing Beta 3 application changes.
  Its success cannot qualify this branch's failed MSI lifecycle.

## Current blocker

The latest tested application/installer source is
`644c0a7766b0b12a40b27e4ec2d4b85cb57853c0`, tested as PR merge
`11ebca55f51dcd723ac117063ab8720ad0b1472d` with identical Git tree
`e22e914707f304529132058a558651faa4fef9db`.
[Checks 44464607563](https://github.com/Julesc013/wtop/actions/runs/44464607563)
and [prerelease 44464607595](https://github.com/Julesc013/wtop/actions/runs/44464607595)
passed all 25 application CTest entries on each of x86, x64 and ARM64. Both
overall workflows failed the ARM64 standard-user MSI lifecycle.

The surrogate/combining-mark editor repair passed native tests. Normal inherited
desktop secondary logon also repaired the previous pre-package namespace denial:
the genuine non-elevated medium account created/reopened owned Global/Local
mutexes and reached real installation. Its rollback-disabled refusal, initial
installation and same-version refusal passed.

The injected post-registration upgrade failure then exposed registry access-denied
errors during rollback. The oracle rejected skipped rollback operations; cleanup
also failed, so the account/profile/staging were retained. The complete
standard-user lifecycle is **not qualified**. See the exact artifact identity,
counts, inference limits and upstream comparison in the
[follow-up evidence](beta3-verification.md#19-november-follow-up-launch-repaired-real-rollback-still-fails).

Next: isolate this post-registration rollback failure in a minimal per-user MSI
comparison and establish a supported recovery fix before repeating the complete
fresh-account lifecycle. Preserve the token, machine policy, registration checks
and cleanup assertions. No supported fix is established at this checkpoint.

## Delivery sequence

1. Push each diagnostic or repair increment promptly to the active task branch.
2. Review the bounded change and run its focused tests plus required spec/build
   checks. Read the native workflow results at the actual submitted revision.
3. Merge independent checked increments promptly. Keep PR #6 draft until its
   applicable checks and review pass. Validate source integration separately
   from any explicitly dispatched immutable versioned draft staging.
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
