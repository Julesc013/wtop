# Wtop continuation checkpoint

Updated: **19 November 2026 (AEDT)**. Recheck live Git refs and workflow results
before resuming. This is a continuation record, not a release qualification receipt.

## Integration and publication

- Canonical branch: `master`. The continuity change starts from
  `3524c5b54e87af31f1801466434dcdd9e277de86`, with version `0.4.0-beta.2`.
- Independent continuity branch: `work/continuity-20261119`. Its changes cover
  checkpoint discipline, workflow scheduling and explicit draft staging only.
  Application code, native fixtures and qualification assertions are unchanged.
- [Beta 3 PR #6](https://github.com/Julesc013/wtop/pull/6) remains a separate
  draft on `work/beta3-20261118`, blocked by standard-user MSI rollback recovery.
  Do not merge it merely because this independent continuity change passes.
- Beta 1 is public; Beta 2 is an unpublished draft. No Beta 3 release or tag has
  been created. A source merge is separate from staging or public publication.

## Preserve every work unit

Commit and push each coherent increment before starting another substantial
unit. Open a draft PR early, keep failures explicit, then merge reviewed changes
when their applicable checks pass. Synchronize the local integration checkout.
Update this record and the relevant verification document at integration/handoff.

PR updates and master pushes run both workflows. Work-branch pushes use their
open PR's validation; a branch without a PR can be checked by manual dispatch.
Automatic validation retains exact-source Actions artifacts. Staging a versioned
draft requires explicit dispatch from canonical master; changed source cannot
overwrite an existing version's draft. See [publication policy](beta-publication.md).

## Exact Beta 3 result to resume

The latest tested application/installer source is
`644c0a7766b0b12a40b27e4ec2d4b85cb57853c0`, tested as PR merge
`11ebca55f51dcd723ac117063ab8720ad0b1472d` with identical Git tree
`e22e914707f304529132058a558651faa4fef9db`.
[Checks 44464607563](https://github.com/Julesc013/wtop/actions/runs/44464607563)
and [prerelease 44464607595](https://github.com/Julesc013/wtop/actions/runs/44464607595)
passed all 25 application CTest entries on each of x86, x64 and ARM64.
The overall workflows failed the ARM64 MSI lifecycle gate.

The editor repair preserves adjacent UTF-16 surrogate input before combining-mark
normalization. The fresh-user launcher repair also passed its native namespace
preconditions: a genuine medium-integrity, non-elevated account created/reopened
its own Global/Local mutexes, then reached the package's real install sequence.

The injected post-registration upgrade failure exposed registry access-denied
errors during Installer rollback. The oracle correctly rejected skipped rollback
operations; cleanup also failed, and account/profile/staging were retained.
The complete standard-user lifecycle is **not qualified**. The similar open
[WiX issue #8574](https://github.com/wixtoolset/issues/issues/8574) is corroborating
evidence, not proof of the underlying cause or a supported workaround.

Detailed source-bound evidence, artifact identity and earlier failures are in
[the Beta 3 branch verification record](https://github.com/Julesc013/wtop/blob/work/beta3-20261118/docs/beta3-verification.md).
Next: isolate the post-registration rollback fault with a minimal per-user MSI
comparison and establish a supported recovery fix before rerunning the full
fresh-account lifecycle. Preserve the standard-user token, machine policy,
registration checks and cleanup assertions. Record failure if no fix is proven.

## Continuity change verification and limits

Before submission, the independent master-based patch passed 9 publication tests,
the full Python suite (73 passed, 33 explicit skips; 106 total), specification
validation, generated command-help checks and workflow YAML parsing. A parsed
comparison confirms the existing Beta 2 job steps, matrices, permissions,
dependencies and artifact paths are unchanged except for the deliberate staging
event condition. Native CI results belong to this PR's actual submitted revision.
They cannot qualify Beta 3 or replace its failed installer receipt.

Keep large binaries/raw logs outside Git. Preserve earlier unrelated/dirty
worktrees; do not reset or delete them merely to clean up the current session.
