# Draft staging and deliberate public promotion

The dedicated `Wtop portable beta` workflow builds x64, runs actual tests, records
native resources/version/SDK and assembles one exact portable beta. Work branches
and pull requests build and test but never stage or publish. On canonical `master`,
a passing package may be staged as a **draft prerelease**. This does not assert
that the public beta gates passed. The release script verifies uploaded bytes by
acquiring the draft assets again, and reports remaining gates.

A version is not overwritten. A pre-existing public release or different draft
source is refused. A retry may supply missing assets only after existing bytes
match. No `--clobber`, silent signing, tag move, rebuild or replacement is used.

Public promotion requires a maintainer to obtain the exact draft portable package,
run the [desktop acceptance procedure](desktop-acceptance.md), review the resulting
receipt and attach it to the draft as `desktop-receipt.json`. Do not synthesize
answers. The executable hash/version/source, Windows 11 workstation x64 ordinary
user, all human questions, and terminal identity must match. Enable GitHub Issues
through repository administration; adding templates does not change this setting.
No admin token is installed in Actions and the workflow does not change protections.

Then explicitly run `Wtop portable beta` from master with operation `publish` and
the existing beta tag. No build occurs in that path. It checks live feedback and
all exact-payload evidence, keeps prerelease status, does not set Latest, then
acquires unauthenticated public download URLs and verifies all bytes again.
A publication/acquisition failure is an incident, not permission to replace assets
under the same version. Investigate/revoke as appropriate and issue a new version.

The script accepts only the fixed repository and branch. Tokens are scoped by
job: builds read content, draft/publication jobs write content, artifact download
only additionally reads Actions. No pull-request-target or privileged workflow-run
path consumes arbitrary fork code. Action references are pinned.

Primary implementation references:
- https://cli.github.com/manual/gh_release_create
- https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository
- https://docs.github.com/en/actions/concepts/security/github_token

A draft may be staged while desktop/human/feedback gates are blocked. That is the
maximum authorized publication state until those real conditions are satisfied.
