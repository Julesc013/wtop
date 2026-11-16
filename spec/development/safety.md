---
type: "Wtop Specification"
title: "Process actions and trust boundaries"
description: "Observe with least access and make mutations explicit, identity-bound and independently reportable."
resource: "urn:wtop:spec:wt-061"
tags: ["wtop", "development"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T10:52:40Z"}
wtop: {"id": "WT-061", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-011", "WT-012", "WT-030"], "requirements": ["WT-REQ-061-01", "WT-REQ-061-02", "WT-REQ-061-03"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}, {"id": "process-rights", "resource": "../references/sources.json#process-rights"}, {"id": "nt-native", "resource": "../references/sources.json#nt-native"}]
---

# Process actions and trust boundaries

Basic collection requests the minimum rights required for each field. A denied
field does not make the whole process disappear. Protected processes and versioned
access rights remain real boundaries; administrator status is not universal access.[^process-rights]
No baseline operation enables debug privilege, installs a driver, changes a service,
modifies policy or bypasses platform signing rules automatically.

Process actions use the command/model layer. Before termination, priority,
affinity, suspend/resume or other controls, resolve the selected lifetime identity
again, obtain only the needed rights, and return a typed result. Distinguish
already-exited, identity-changed, denied, unsupported and action-failed.
Bulk actions enumerate a frozen set of intended identities and report each result.
Do not map a Unix signal or nice value to Windows behavior without an explicit
native semantic definition.

Inherited :exec is a deliberate shell command entered by the user. Do not
construct it from process names, settings, network data or theme fields. New
CLI arguments are parsed as data; unknown tokens never execute. Process command
lines, environment and full paths can contain sensitive data; display/record only
fields requested by the chosen view/export and keep logs opt-in.

Optional native NT queries have isolated, version-aware parsers with minimum
length, alignment, offset and overflow checks. Microsoft's native-system query
contract warns that structures may change.[^nt-native] Do not scatter native
layouts throughout the model. A malformed or changing buffer returns error or
retry-within-budget, never unchecked pointer traversal.

The initial project has no plugin execution, remote-control server or mandatory
service. Future drivers/helpers/event tracing require their own threat, resource,
licensing and lifetime analysis. This is a capability boundary, not a reason to
block ordinary authorized source edits or read-only development work.

[^process-rights]: Microsoft process rights and protected-process restrictions.
[^nt-native]: Microsoft cautions for native system-query structures.

## Requirements and acceptance

### WT-REQ-061-01

Collection MUST use least access and return denied/unsupported states without automatic privilege changes.

Acceptance: WT-AC-061-01 — Run as an ordinary user against own and protected processes; inspect requested rights and verify no policy/service change.

### WT-REQ-061-02

Every destructive action MUST revalidate lifetime identity and return per-target outcomes without shell interpolation.

Acceptance: WT-AC-061-02 — PID-reuse, exit-race, denied-right and hostile-name fixtures cannot affect the wrong process or launch an unintended command.

### WT-REQ-061-03

Native buffers and external strings MUST be length/offset/encoding checked before traversal or rendering.

Acceptance: WT-AC-061-03 — Malformed variable-record, offset overflow and unterminated-string fixtures fail safely within declared bounds.
