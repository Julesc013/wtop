# Public beta: six-patch execution contract

The first beta is `0.4.0-beta.1`, portable x64 only. This is an explicit subset
of stable 0.4.0, not abandonment of per-user/system installation or historical
1.0 goals. Normative gates are [WT-063](../spec/development/release.md).

| Patch | Result | Completion evidence |
| --- | --- | --- |
| 1 | Reconciled scope/catalogs, feedback templates and evidence map | Spec/help validation and live feedback check; no blanket acceptance promotion |
| 2 | Measurement/action hardening and NTop regression vectors | Portable arithmetic/action and native comparator/permission fixtures |
| 3 | Terminal/settings acceptance and reproducible desktop receipt | Owned-console matrix, settings failures, actual Windows 11 ordinary-user human review |
| 4 | Exact-byte portable beta packaging | PE/embedded version/provenance/source/license/ZIP checks and tamper tests |
| 5 | Draft and gated GitHub prerelease workflow | Passing build, staged manifest, negative promotion cases and live gate check |
| 6 | Tester/diagnostic guidance, release notes and acquisition | Privacy tests, tagged candidate receipts and final public asset hashes |

## Beta blockers versus later scope

Public-beta blockers are known unsafe process actions or misleading measurements,
settings corruption, unusable/unstored console state, missing exact-byte evidence,
missing desktop/human acceptance and absent feedback access. A failed mandatory
fixture is not converted into a skip or forgiven by a beta label.

Not beta blockers: complete htop parity, MSI/WinGet, native ARM64, historical OS
ports, full Unicode grapheme rendering, a native GUI, advanced tracing and GPUs.
These remain open and must be disclosed. The conservative renderer substitutes
non-ASCII glyphs while preserving raw text in JSON/configuration.

## Evidence reconciliation

The implementation record under `docs/evidence/implementation-2026-11-17.json`
is real narrow evidence: portable tests, Server 2025 x86/x64 execution, candidate
packaging and elevated installer fixtures. It is not Windows 11 workstation,
ordinary-user, human visual, all-architecture or historical evidence. Existing
51 broad acceptance cases remain unpromoted unless their complete oracle is met.

Modern build candidates are documented by those observation receipts. The formal
target catalog remains unpromoted until target-bound evidence records are admitted.
`process.io.read_bytes` and `write_bytes` remain planned public metric fields:
collecting their values internally to derive aggregate I/O is not the same as
exposing stable independently selected raw counters. Do not mark them implemented
merely to make counts appear complete.

Feedback templates do not enable GitHub Issues. Check the actual repository
setting before public promotion. No administrative credential is embedded in CI.

## Development and publication policy

Use six reviewable commits with focused tests, then integrate checked changes to
`master`. Preparing a draft is authorized by this work unit. Public promotion is
only authorized after its required client/human/feedback evidence exists.
Do not rename an old development executable; build and test the embedded beta
version. Do not rebuild between accepted candidate and publication.

## Implementation checkpoint

All six patches have source implementations in this branch. Portable tests and
negative gate fixtures are run locally. Windows native CI, actual desktop review,
feedback enabling, and the publication state are separate observations; this
paragraph does not mark them passed. `tools/publish_beta.py` may stage only a draft
on master after exact native packaging succeeds. It cannot auto-publish on push.

Current source enhancements map to WT-W020/030/040/050/060/070/130. The existing
acceptance registry retains broad-case scope; beta-specific requirements are
WT-REQ-063-03 through -06. Do not mark all earlier cases passed from this checkpoint.
