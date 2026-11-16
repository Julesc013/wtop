# Working on Wtop

Wtop continues gsass1/NTop at `326ccd80b41842868f4247d1e8978e854658a710`.
Preserve upstream history, authorship and GPL-3.0-only notices. The current
product is a terminal process monitor; a native GUI is deferred.

Application sources, headers and inherited build/configuration/packaging files
live in `source/`. Root `CMakeLists.txt` remains the build entrypoint. Root
`TODO.md` and `ROADMAP.md` are practical views of the canonical specification;
they must distinguish implemented code from native acceptance and release support.

## Start here

Read `spec/START-HERE.md`, `spec/index.md`, `README.md` and the work unit selected
by `python tools/specctl.py next`. The specification records intended behavior;
the implementation/evidence status records what actually exists. A declared
target, passing validator or successful build is not a native support claim.

## Work discipline

- Use a short-lived branch from the repository's current integration branch.
  Inspect the worktree and current upstream revision before changing anything.
- Keep one bounded work unit active per writer. Parallel writers must have
  disjoint files and an agreed interface; integrate and review their changes.
- Preserve the working NTop-derived TUI while extracting responsibility-sized
  modules. Do not perform a speculative complete rewrite or clone an application
  directory for every Windows release.
- Keep product semantics, collectors, native host services and presentation
  separate. Share equivalent behavior; retain explicit platform differences.
- Use the single command and target catalogs in `spec/catalog/`. Do not create
  competing inventories in code, generated docs or CI without a checked mapping.
- Do not copy code from another project until its specific license, ownership
  and dependency requirements have been checked.

## Implementation rules

- Portable code uses the documented C90 profile, checked numeric widths, bounded
  buffers and explicit ownership. C90 syntax alone does not prove 16-bit support.
- Native handles, callbacks, synchronization and API structures stay in their
  owning adapters. State the callback signature, thread, lifetime and retirement.
- Every failed observation retains its status. Unknown ownership is not SYSTEM;
  unavailable measurements are not zero. Derive rates using measured intervals.
- Bind actions and selection to process identity; account for PID reuse.
- No background service, startup entry, updater, scheduled task or persistent
  collector is installed by ordinary use. No-argument invocation requests TUI;
  help, version, metadata and one-shot CLI commands must not initialize it.
- Never silently turn redirected output into an interactive console. Keep
  machine output deterministic and diagnostics separate.
- Keep optional APIs optional at the loader boundary. A missing load-time import
  can prevent the program from reaching capability negotiation.
- A cancellation request is not evidence that native work ended. Stop, drain,
  join or retire owned work before freeing state. Do not add routine forced
  thread termination or an unbounded worker/queue as a shortcut.
- Changes to defaults, aliases, output schemas, settings, paths or install
  ownership require explicit compatibility treatment and focused regression
  coverage. Do not silently reinterpret existing units or fields.

## Verification and delivery

Run `python tools/specctl.py validate`, the focused tests for changed behavior,
and the applicable CMake/CTest build. Follow `docs/development.md` for commands.
Use independent expected results for arithmetic, parsing, identity and lifecycle
cases. Test failure paths, empty process sets, denied queries and cancellation.
Do not introduce tests which merely search source text for the implementation.

Record exact source/artifact identity, compiler, target, commands, outcomes and
remaining limits. Keep large generated binaries and raw logs outside the source
tree. Preserve failures; never widen a performance threshold to make a failed
run appear successful without a separately explained requirement change.

Open a reviewable pull request with the problem, resulting behavior, tests and
limits. Do not force-push, change repository protections, publish a release or
submit a package-manager listing as an incidental consequence of a source edit.
Release publishing is an explicit work unit after target/artifact qualification.
Human review or runtime evidence must never be marked complete by a document
validator. Wtop must build and run without AIDE or another agent runtime.
