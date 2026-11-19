# Wtop continuation checkpoint

Updated: **19 November 2026 (AEDT)**. Recheck live Git refs and workflow results
before resuming. This is a continuation record, not a release qualification receipt.

## Integration and publication

- Canonical `master` is now `3f84df0d275f17a832184f3e025a3792e0c568d5`, after
  [PR #7](https://github.com/Julesc013/wtop/pull/7) merged the independent
  checkpoint/CI changes. Its application remains `0.4.0-beta.2`.
- [Beta 3 PR #6](https://github.com/Julesc013/wtop/pull/6) remains draft on
  `work/beta3-20261118`, blocked by standard-user MSI rollback recovery.
  This normal merge brings master's checked continuity policy into that branch;
  it does not integrate Beta 3 into master or qualify the failed MSI lifecycle.
- Beta 1 is public; Beta 2 is an unpublished draft. No Beta 3 release or tag has
  been created. Source integration, draft staging and public publication remain
  separate operations. WinGet, Scoop and Chocolatey remain offline preparation.

## Preserve every work unit

Commit and push each coherent increment before starting another substantial
unit. Open a draft PR early, keep failures explicit, then merge reviewed changes
when their applicable checks pass. Synchronize the local integration checkout.
Update this record and the relevant verification document at integration/handoff.

PR updates and master pushes run both workflows. Work-branch pushes use their
open PR's validation; a branch without a PR can be checked by manual dispatch.
Automatic validation retains exact-source artifacts and offline channel inputs.
Staging a versioned draft requires explicit dispatch from canonical master;
changed source cannot overwrite an existing version's draft. See the
[publication policy](beta-publication.md).

## Exact Beta 3 result to resume

The latest native-tested application/installer source is
`644c0a7766b0b12a40b27e4ec2d4b85cb57853c0`, tested as PR merge
`11ebca55f51dcd723ac117063ab8720ad0b1472d` with identical Git tree
`e22e914707f304529132058a558651faa4fef9db`.
[Checks 44464607563](https://github.com/Julesc013/wtop/actions/runs/44464607563)
and [prerelease 44464607595](https://github.com/Julesc013/wtop/actions/runs/44464607595)
passed all 25 application CTest entries on each of x86, x64 and ARM64. Both
complete workflows failed the ARM64 standard-user MSI lifecycle gate.

The editor repair passed native tests. Normal inherited-desktop secondary logon
also repaired the earlier namespace denial: the genuine non-elevated medium
account created/reopened owned Global/Local mutexes and reached installation.
Its rollback-disabled refusal, initial installation and same-version refusal passed.

The injected post-registration upgrade failure exposed registry access-denied
errors during rollback. The oracle rejected skipped rollback operations; cleanup
also failed, so account/profile/staging were retained. The complete standard-user
lifecycle is **not qualified**. Source-bound artifact identity, exact failures and
inference limits remain in the
[follow-up evidence](beta3-verification.md#19-november-follow-up-launch-repaired-real-rollback-still-fails).

Next: isolate the post-registration rollback fault with a minimal per-user MSI
comparison and establish a supported recovery fix before rerunning the complete
fresh-account lifecycle. Preserve the genuine token, machine policy, registration
checks and cleanup assertions. No supported fix is established at this checkpoint.

## Merge checkpoint limitation

This merge combines pre-merge Beta 3 head
`5bf807d1dd5c30131d4a7743cacf0e2a4e574bfa` with master
`3f84df0d275f17a832184f3e025a3792e0c568d5`. Application code, packaging,
native fixtures, VERSION and MSI qualification gates remain unchanged from the
Beta 3 parent. The CI/tooling policy was independently qualified in PR #7.
Master's subsequent [checks 44466735287](https://github.com/Julesc013/wtop/actions/runs/44466735287)
and [prerelease 44466735358](https://github.com/Julesc013/wtop/actions/runs/44466735358)
passed at `3f84df0d275f17a832184f3e025a3792e0c568d5`, including x86/x64/ARM64
and aggregate qualification. Draft staging and public publication were skipped.
The merge is recorded with `[skip ci]`; no new native receipt is claimed for its
combined source. It remains **unqualified and draft**, and PR #7's passing checks
cannot replace the failed Beta 3 installer receipt. A supported MSI repair still
requires fresh complete native qualification before PR #6 can merge.

Keep large binaries/raw logs outside Git. Preserve earlier unrelated/dirty
worktrees; do not reset or delete them merely to clean up this session.
