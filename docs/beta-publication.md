# Draft staging and deliberate public promotion

The supplied [Beta 2 plan](beta2-plan.md) authorizes automatic alpha/beta gates and
retains human approval for stable releases. This task requests a **draft**, so the
master-push path creates only a draft prerelease. Public publication always needs
an explicit dispatch operation. Stable versions are refused by prerelease tooling.

## Build and draft

The dedicated workflow builds x86/x64 on the named Windows Server runner and
ARM64 on `windows-11-arm`. Every included executable runs the required native
suite and produces exact-byte source/compiler/SDK/version/test/security receipts.
Work branches and pull requests cannot stage or publish. The canonical master
job verifies the aggregate, uploads it as a draft, then reacquires and hashes the
actual assets. The release title, asset labels and numeric resources derive from
one version model.

A version is never overwritten. An existing public release or different-source
draft is refused. A retry can add a missing asset only after every existing asset
matches. There is no `--clobber`, silent signing, tag move, rebuild or replacement.
Beta 1's schema-1 evidence and published bytes remain readable and immutable.

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
