---
type: "Wtop Guide"
title: "Start here"
description: "A task-sized route through product contracts, current evidence and dependency-ready work."
resource: "urn:wtop:spec:wt-000"
tags: ["wtop", "onboarding"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-000", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "informative", "review": "pending", "depends_on": ["WT-001", "WT-070"], "requirements": []}
sources: [{"id": "user", "resource": "references/sources.json#user"}]
---

# Start here

Wtop is an authorized development fork of NTop. This bundle specifies the desired
product and the boundaries of the first implementation; it does not announce a
release or certify any operating system. Start with the repository's root
instructions, then this guide and [governance](foundation/governance.md).

The [bundle index](index.md) lists the concepts. For a coding task read only the
work unit's context and direct dependencies, inspect current code, then run:

    python tools/specctl.py validate
    python tools/specctl.py status
    python tools/specctl.py next
    python -m unittest discover -s tests -p test_specctl.py -v

Run these commands from the repository root. Python is development tooling,
not an application runtime dependency. The tool does not execute work-unit
commands, acquire privileges, call services or publish anything.

The [work queue](work/units.json) is dependency-ordered. The first work establishes
the fork, specification and inherited-code repair/build baseline. The following
work establishes invocation and measured snapshot semantics before broad TUI
feature work. Source changes authorized by the user can proceed through this
sequence; a proposed document's review field is not a new permission hurdle.

Read the [invocation contract](interfaces/invocation.md) before using candidate
flags. The command registry distinguishes inherited code, candidate behavior,
planned commands and an explicitly unavailable GUI. The target registry distinguishes
a build candidate from runtime qualification. Current results belong to
[evidence records](catalog/evidence.json), not to optimistic prose.

To continue a partial task: preserve its actual failures, examine the current
working tree and evidence, choose the smallest dependency-ready increment, run
its relevant acceptance case and record exact scope. Never reset unfinished work
or manufacture a passing receipt to make the queue advance.
