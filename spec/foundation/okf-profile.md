---
type: "Wtop Specification"
title: "OKF authoring profile"
description: "Use actual Open Knowledge Format 0.2 with a separately versioned Wtop producer profile."
resource: "urn:wtop:spec:wt-002"
tags: ["wtop", "foundation"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-002", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-001"], "requirements": ["WT-REQ-002-01", "WT-REQ-002-02", "WT-REQ-002-03"]}
sources: [{"id": "okf-02", "resource": "../references/sources.json#okf-02"}, {"id": "aide-okf", "resource": "../references/sources.json#aide-okf"}, {"id": "disked-okf", "resource": "../references/sources.json#disked-okf"}]
---

# OKF authoring profile

The upstream Open Knowledge Format 0.2 is UTF-8 Markdown with YAML frontmatter.
Its only always-required concept field is a nonempty type; its reserved index.md
and log.md files are navigation and history, not concepts. The root index may
declare okf_version; concept identity is its bundle-relative path without .md.[^okf-02]

Wtop adds the independent wtop-spec/1 producer profile. A concept declares type,
title, description, resource, tags, status, generated, wtop and sources. The wtop
object carries the stable WT identifier, profile/document version, authority,
review, dependencies and requirement IDs. Stable Wtop identity survives a move;
OKF path identity does not. Status uses upstream draft/stable/deprecated.
No human verification is asserted by this generated initial bundle.

This project's writer subset uses one top-level key followed by a JSON value on
one line. JSON strings, arrays and flow objects are valid YAML 1.2 values.
The standard-library tool parses exactly that bounded subset with duplicate-key,
nonfinite-number, size, depth and path checks. It is deliberately not a general
YAML parser or a universal OKF consumer. Ordinary upstream YAML outside this
subset may be valid OKF but is not editable/validatable by this writer tool.
Unknown safe extension keys remain in the loaded metadata and are not silently
rewritten. There is no executable YAML, alias expansion or network resolver.

Wtop's stricter local checks require resolvable repository links, stable IDs,
schema-valid catalogs and complete acceptance mappings. Upstream OKF consumers
must tolerate broken links and unknown metadata; failing a Wtop authoring check
does not mean such a document is malformed upstream OKF. These distinctions follow
the inspected AIDE and DiskEd profiles without importing their entire framework.[^aide-okf][^disked-okf]

Per-claim external citations use footnotes joined to sources[].id. Source registry
anchors are stable source IDs. The tool checks local anchors, requirement references
and contained paths; it does not treat a URL's presence as verification of the URL.
Root index generation is deterministic. log.md records dated changes and stays
human-readable. Actual run records use the Wtop evidence catalog, not fabricated
OKF verified stamps.

[^okf-02]: Upstream SPEC.md, observed blob c06e3eede0c910d0ecf12524c34204156f8795ac.
[^aide-okf]: AIDE's proposed engineering extension, inspected at its pinned revision.
[^disked-okf]: DiskEd's authoring and traceability patterns, inspected at its pinned revision.

## Requirements and acceptance

### WT-REQ-002-01

Every concept MUST satisfy the local frontmatter schema, keep a unique stable ID and appear in the generated index.

Acceptance: WT-AC-002-01 — Invalid metadata, duplicate IDs, missing concepts and stale index fixtures fail validation.

### WT-REQ-002-02

The validator MUST reject unresolved evidence-critical IDs, escaping paths and unsupported schema keywords rather than imply broad validation.

Acceptance: WT-AC-002-02 — Malformed catalog, traversal, symlink escape and unsupported-keyword fixtures fail with precise errors.

### WT-REQ-002-03

A passed acceptance or completed work unit MUST reference source-bound execution evidence; structural validation MUST NOT create a passing result.

Acceptance: WT-AC-002-03 — A synthetic passed-without-evidence entry is rejected and an empty evidence catalog leaves all cases not_run.
