# Continuing Wtop with Codex

Open the repository in Codex and start from the current integration revision.
Read `AGENTS.md`, `spec/START-HERE.md`, then the next work unit and its linked
requirements. There is no need to load the entire specification for every edit.

```text
Read AGENTS.md and spec/START-HERE.md. Run tools/specctl.py validate and
tools/specctl.py next. Inspect the current worktree, source revision and evidence.
Take the next dependency-ready work unit whose required toolchain/target is
available. State its observable result and touched modules. Preserve working
behavior and inherited notices. Implement the bounded change, run the meaningful
focused checks and applicable native build, review the diff, record exact evidence
and remaining limits, and open a reviewable pull request. Do not infer target
support, human acceptance or release authority from a validator, compilation,
synthetic fixture or another target's run. Do not publish a release/package listing
as a side effect. When an environment is missing, complete independent work and
record the precise evidence still needed.
```

Use one model for each meaning. Collectors adapt actual APIs and retain missing
data states. CLI/TUI use the same observations and action definitions. The future
GUI consumes those contracts after its own work unit is admitted.

## Focused review

- New field: definition, unit, state, cost and independent expected result.
- New API: export/import floor, permissions, failure contract and cleanup.
- New callback: exact type, context owner, thread, reentrancy and retirement.
- New action: identity, revalidation, partial failure and visible result.
- New target: compiler, binary audit and boot/runtime evidence.
- New install mode: writes, ownership, update/removal and preserved settings.
- Optimization: unchanged workload/semantics and before/after measurements.

Prefer small extraction from working code over replacing it with a new general
framework. Share genuinely equivalent behavior; do not hide differences behind
silent fallbacks. Avoid global singletons and persistent collectors as shortcuts
around ordinary foreground ownership.

Record work ID, source/artifact identities, changed behavior, exact tests,
outcomes and unresolved limits. Keep planned, implemented, tested and supported
separate. AIDE may consume the records; Wtop must remain buildable and usable
without an agent runtime, model service or IDE plugin.
