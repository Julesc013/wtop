# Draft staging and deliberate public promotion

The supplied [Beta 2 plan](beta2-plan.md) authorizes automatic alpha/beta gates and
retains human approval for stable releases. Pull requests and master pushes
validate and retain source-bound artifacts. Draft staging and public publication
each require an explicit workflow dispatch. Beta 3 may be publicly promoted when its separately checked
prerelease gates pass; user authorization does not substitute for those results.
Stable versions are refused by prerelease tooling.

## Build and draft

The dedicated workflow builds x86/x64 on the named Windows Server runner and
ARM64 on `windows-11-arm`. Every included executable runs the required native
suite and produces exact-byte source/compiler/SDK/version/test/security receipts.
Pull requests and master pushes run these checks and retain the verified aggregate
and offline channel preparation as Actions artifacts identified by source SHA.
Work-branch pushes use their open pull request's validation, avoiding duplicate
push runs. Branches without a pull request can be checked by workflow dispatch.

To stage a release, explicitly dispatch the prerelease workflow from canonical
`master` with operation `stage`. It rebuilds and qualifies that exact master
snapshot, verifies the aggregate, uploads a draft, then reacquires and hashes the
actual assets. Work branches and pull requests cannot stage or publish. The
release title, asset labels and numeric resources derive from one version model.

Superseded automatic validation runs can be cancelled within the same PR/ref.
Explicit stage/publish runs share a separate delivery concurrency group, so
automatic validation cannot cancel an in-progress release operation. Submit one
delivery operation at a time; GitHub's default concurrency permits one pending
run per group. The built source must still be current master when the staging
job checks it.

A version is never overwritten. An existing public release or different-source
draft is refused. A retry can add a missing asset only after every existing asset
matches. There is no `--clobber`, silent signing, tag move, rebuild or replacement.
Beta 1's schema-1 evidence and published bytes remain readable and immutable.
After a draft exists, changed source requires a new version before another draft
can be staged. This includes documentation changes. Ordinary source integration
continues to produce SHA-specific artifacts without attempting to replace drafts.

Alpha/beta human visual review is optional. Automated receipts explicitly say
that they are automated, identify the actual OS/architecture/token and never
assert a human observation. Missing required tests, invalid signatures, source
mismatches, altered bytes and detected security threats block assembly. An
unavailable scanner may be reported in a draft but blocks public promotion.
MSI and historical profiles remain omitted while their independent gates are open.

## Public alpha/beta promotion

Verify the final-byte security scan, exact source and asset inventory, live
feedback route and truthful limitations. Then explicitly dispatch the prerelease
workflow from master with operation `publish` and the existing draft tag. This
path does not rebuild. It refuses stable versions, verifies every gate and asset,
keeps prerelease status, does not set Latest, then reacquires public URLs and
checks the actual downloaded bytes. A failure after publication is a release
incident, not permission to replace bytes under an existing version.

GitHub Issues must actually be enabled; issue templates alone do not change the
repository setting. No automatic workflow installs administrator credentials,
changes protections or enables feedback on the maintainer's behalf.

## Stable releases

Stable publication remains outside this prerelease tool. It needs exact advertised
OS/architecture/install-scope closure, real ordinary-user desktop execution,
explicit human review of the final bytes and release approval. The existing
[desktop procedure](desktop-acceptance.md) is available for those observations.
Never synthesize PASS answers. A beta policy change does not close an installer,
historical target or full product acceptance case.

## Credential boundary

Builds use read-only content permission. Only canonical master draft/publication
jobs can write release contents; artifact acquisition additionally reads Actions.
No pull-request-target or privileged workflow-run path executes arbitrary fork
code with release credentials. Action references are pinned. Tokens remain in
workflow environment, not release artifacts, source or reports.
