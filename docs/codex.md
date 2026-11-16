# Continuing Wtop with Codex

Open the repository in Codex and start from the current integration revision.
Read `AGENTS.md`, `spec/START-HERE.md`, then the next work unit and its linked
requirements. Use root `TODO.md` for detailed closure tasks and `ROADMAP.md` for
dependency order. The canonical requirements, acceptance and work registries
remain in `spec/`; the root documents must not become competing inventories.
There is no need to load the entire specification for every edit.

```text
Read AGENTS.md and spec/START-HERE.md. Run tools/specctl.py validate and
tools/specctl.py next. Inspect the current worktree, source revision and evidence.
Take the next dependency-ready work unit whose required toolchain/target is
available. Read its TODO/ROADMAP projection and inspect existing candidate code.
State the observable result and touched modules. Preserve working behavior,
source history and inherited notices. Implement the bounded change, run meaningful
focused checks and the applicable native build, review the diff, record exact
evidence and remaining limits, and open a reviewable pull request. Do not infer
target support, human acceptance or release authority from a validator,
compilation, synthetic fixture or another target's run. Do not publish a
release/package listing as a side effect. When an environment is missing,
complete independent work and record the precise evidence still needed.
```

A planned work unit can have useful candidate implementation already present.
Its status describes unclosed requirements/evidence; it is not permission to
replace working modules. Before writing, identify the gap, the existing owner of
that behavior and the smallest compatible change. Parallel writers need disjoint
files and agreed interfaces, followed by an integrated review.

## Responsibility boundaries

All application code and inherited build/configuration/packaging inputs live in
`source/`. Root CMake, tests and specification tooling reference that location.
`source/ntop.c` still contains native responsibilities that will be extracted
incrementally; preserve a buildable, usable TUI while doing so.

Use one model for each meaning. Collectors adapt actual APIs and retain missing
data states. CLI/TUI use the same observations and action definitions. Portable
`core`, `settings` and `ui` modules hold equivalent policy; native adapters own
Windows handles, console modes and checked filesystem replacement. The future
GUI consumes those contracts after its own work unit is admitted.

Do not introduce an application copy for each Windows generation. Add a host or
collector adapter only when its ABI, loader or behavioral contract differs.
Optional APIs need optional loader boundaries; checking for an export after the
loader has already required it does not preserve an old target.

## Focused review

| Change | Evidence and questions |
| --- | --- |
| New field | Definition, unit, availability state, collection cost and independent expected result. |
| New API | Export/import floor, permissions, failure contract, ownership and cleanup. |
| New callback | Exact type, context owner, calling thread, reentrancy and retirement. |
| New action | Observed lifetime identity, native revalidation, confirmation/partial failure and visible outcome. |
| New key or mouse path | Keymap conflicts, modifiers, text-editor/overlay context, shared action dispatch, displayed hitbox and fallback. |
| New view or theme | Stable metric/column IDs, bounded state, active versus inactive tabs, small-screen layout, persisted round trip and host-palette limits. |
| New settings write | Explicit intent, disclosed resolved destination, original preservation, bounded serialization, temporary cleanup and zero implicit machine integration. |
| New target | Compiler/ABI, binary/import/CPU audit and exact boot/runtime evidence. |
| New install mode | Writes and ownership, update/removal, changed foreign files and preserved settings. |
| Optimization | Fixed workload/semantics and before/after measurements with resource units. |

The htop-style defaults deliberately change NTop's previous Vi defaults. Keep
Vi/Vim selectable, keep Emacs as a distinct map, and do not alias conflicting
keys blindly: htop `k` is a process action; Vi `k` is navigation. F1–F10 remain a
common discoverable path. Text input and confirmation contexts require their own
binding treatment. Mouse input must use the same identity guards as the keyboard.

Theme names describe semantic roles, not guaranteed RGB values. Never invent a
metric or relabel Windows commit/priority/I/O to resemble another monitor. Tabs
must display real views over the shared observations. Persist stable IDs and
user order, not transient screen indices.

Ordinary startup and exit remain read-only. The authorized explicit settings
routes are F2 save, `:save`, the Emacs save binding and `--write-config PATH`.
Full portable/managed owner detection, installer integration and migrations are
separate contracts. Do not turn the current config resolver into a hidden
installer or silently fall back to a different writable directory after failure.

Prefer small extraction from working code over replacing it with a new general
framework. Share equivalent behavior; make meaningful platform differences
explicit. Avoid global singletons and persistent collectors as shortcuts around
ordinary foreground ownership. A cancellation request is not proof that native
work stopped; drain/join/retire owned work before releasing its state.

## Tests, records and completion

Use [development](development.md) for specification, portable, native CLI and
isolated-console checks. Prefer independent expected results and failure paths.
A source-text search is not a behavioral test. A synthetic key-event test is not
human visual acceptance. Native console evidence on one modern host does not
qualify earlier Windows, WinPE, Windows RT, CE or another CPU architecture.

Record work ID, source/artifact identities, changed behavior, exact tests,
outcomes and unresolved limits. Keep planned, implemented, tested and supported
separate. Leave acceptance unpromoted until its actual case and evidence are
complete. Historical observation documents remain tied to their original source
revision; a source move does not authorize rewriting old paths or hashes.

AIDE may consume the records; Wtop must remain buildable and usable without an
agent runtime, model service or IDE plugin. Referenced sibling projects supply
reusable ideas or code only after inspecting the actual repository, contract and
license; their names alone establish no shared API or verified implementation.
