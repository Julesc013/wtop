# Contributing

Read [AGENTS.md](AGENTS.md) and the [specification entrypoint](spec/START-HERE.md).
Wtop uses the same engineering rules for human and automated contributors.

Choose one work unit, state its observable result and target profile, and keep
the patch reviewable. Existing behavior stays intact unless the change explicitly
corrects or replaces it. Add a focused regression for a real failure mode, run
the relevant build/tests and record untested environments accurately.

The GUI, native historical collectors and managed installers are separate work
units. Their presence in the roadmap is not an implementation claim. Do not add
placeholder outputs which look like measured system data.

Original contributions to this NTop-derived product are made under GPL-3.0-only.
Retain the notices attached to inherited code. Identify the source and license
of proposed third-party code before importing it. Document concepts and small
contracts may be learned from other projects without importing their runtimes.

See [development](docs/development.md) for build and validation commands, and
[Codex continuation](docs/codex.md) for the bounded implementation workflow.
