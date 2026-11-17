---
type: "Wtop Specification"
title: "Configuration, themes and storage paths"
description: "Separate installed user preferences, portable data and executable identity without hidden writes."
resource: "urn:wtop:spec:wt-040"
tags: ["wtop", "configuration"]
status: "draft"
generated: {"by": "chatgpt/gpt-6-astra-pro", "at": "2026-11-16T12:38:25Z"}
wtop: {"id": "WT-040", "profile": "wtop-spec/1", "version": "0.1.0", "authority": "proposed-normative", "review": "pending", "depends_on": ["WT-030", "WT-020"], "requirements": ["WT-REQ-040-01", "WT-REQ-040-02", "WT-REQ-040-03", "WT-REQ-040-04"]}
sources: [{"id": "user", "resource": "../references/sources.json#user"}, {"id": "known-folders", "resource": "../references/sources.json#known-folders"}, {"id": "winpe", "resource": "../references/sources.json#winpe"}]
---

# Configuration, themes and storage paths

This is the settings contract for the current version-2 implementation candidate.
Implemented source and host-side tests are not proof of historical Windows
support, crash durability, installation qualification or a stable release. The
native NT adapter, portable data model and delivery candidates have separate
responsibilities; an old target needs its own qualified storage adapter.

## Model and precedence

One selected configuration document is applied over compiled defaults, followed
by CLI preference overrides. There is no implicit merge of adjacent, user and
machine files. Loading, ordinary startup and exit are read-only. An existing
selected file that is malformed, denied or unsupported is reported as an error;
Wtop must not silently bypass it by selecting a different profile.

The resolver applies this order:

1. `--no-config` suppresses file-derived settings, installation-owner discovery
   and the implicit TUI save destination. The actual module path is still
   resolved. An explicit `--write-config PATH` remains allowed unless readonly
   policy forbids writes.
2. `--config PATH` selects that exact document and save destination. It conflicts
   with `--no-config` and explicit portable selection.
3. `--portable-root DIR`, `--portable`, or an executable-adjacent `wtop.portable`
   file selects portable ownership. An explicit root is independent of the
   executable's placement; otherwise the actual module directory is used. Read
   `wtop.conf`, then legacy `ntop.conf`, then defaults. Save to `wtop.conf` under
   that same root. Do not fall through to an installed user profile.
4. A registered owner matched to the actual module directory selects installed
   user or machine ownership. Read the current user's roaming `Wtop/wtop.conf`,
   then machine application-data `Wtop/wtop.conf`, then defaults. Explicit saves
   target the current user's file. A machine default is never edited in place.
5. An unregistered executable retains compatibility discovery: adjacent
   `wtop.conf`, adjacent legacy `ntop.conf`, user roaming `Wtop/wtop.conf`, then
   defaults. An adjacent file owns its selected save path; legacy input saves to
   sibling `wtop.conf`. Otherwise the resolved user file is the save target.

The current directory is never an implicit search root. An explicitly supplied
relative config, export or portable-root path is resolved against the caller's
current directory once because that path was requested. Module-relative lookup
uses the actual module path, not `argv[0]`. A writable directory is not proof of
portable ownership or managed installation. Explicit portable selection precedes
registration. `--no-config` overrides an accompanying portable selection.

## Ownership and native path resolution

The native candidate reads up to 128 child records from
`HKCU\Software\Wtop\Installations`, then HKLM in the executable's registry
view. A child matches when `SchemaVersion` is REG_DWORD `1` and `InstallPath` is
an absolute REG_SZ payload directory that canonicalizes to the module directory.
HKCU wins a dual match. Lookup writes no registry values. Registration guides
settings ownership; it is not authentication of an installation or verification
of a binary's provenance.

The candidate MSI uses a canonical target profile as the child key and adds
string values `Owner=msi`, `ProductCode` and `PayloadSha256`. Its authoring and
ownership guard remain unqualified delivery work. A future package-manager path
must either supply an agreed owner record or explicitly select suitable storage;
Wtop does not infer WinGet ownership from directory names or generic uninstall
metadata. See [delivery](../distribution/packaging.md) and
[the current packaging implementation](../../packaging/README.md).

On the native NT candidate, optional `SHGetFolderPathW` resolves roaming/common
application data without a create flag. The role names `%APPDATA%\Wtop` and
`%PROGRAMDATA%\Wtop` are explanatory; resolution does not simply concatenate
those environment strings. If the user folder cannot be resolved, memory-only
operation remains possible and the implicit save path is unavailable. The
application-owned user `Wtop` directory may be created during an explicit save.
Arbitrary explicit export parents and portable roots are not automatically
created. Read-only portable media never causes an undisclosed fallback.

Existing paths use handle-based `GetFinalPathNameByHandleW` when available;
absent destinations resolve through their parent where possible. Older APIs
have a disclosed lexical fallback. This canonicalization reduces ordinary
case/short-name/reparse aliases but is not a general filesystem race barrier.
Settings inputs and existing destinations must be disk files with one link;
hard-linked files and directories are refused. Native calls are synchronous and
have no hard elapsed-time bound.

An all-users installation uses each user's own mutable preferences outside the
payload. Current theme/keymap/color/view preferences are inline in `wtop.conf`;
separate theme/keymap directories and automatic cache/history storage are not
implemented. Logs are explicit, bounded telemetry outputs at the requested path.
Historical systems, WinPE and systems without a usable user-folder API can use a
qualified explicit-path adapter or in-memory operation; the modern NT source is
not evidence that those systems run it.

## Path inspection and readonly behavior

`--paths` returns resolution only, before settings parsing, collection or TUI
initialization. It may inspect module/file metadata, read owner registration and
resolve native folder roles; it creates no files or registration. A malformed or
absent explicit config can therefore be reported without claiming it will load.
The current text/JSON surfaces use one resolver.

The JSON object has schema `1`, kind `settings-paths`, and these fields:

| Field | Contract |
| --- | --- |
| `owner` | `memory`, `explicit`, `portable`, `installed-user`, `installed-machine`, or `unregistered`. |
| `source` | `compiled`, `explicit-file`, `portable-file`, `adjacent-file`, `legacy-file`, `user-file`, or `machine-defaults`. |
| `module` | Actual resolved executable path, including when `--no-config` is selected. |
| `read`, `write` | Resolved source and implicit save paths; empty means no selected source/destination. |
| `saving_disabled` | True for `--no-config` or `--readonly`; this policy flag does not establish destination writability or availability. |
| `canonical_aliases` | Whether the handle-based canonicalization API is available; not proof that every absent path or future race is resolved. |

For `--paths --no-config`, owner is `memory`, source is `compiled`, read/write are
empty and saving is disabled. Config/installation-owner discovery is skipped;
module identity is retained. `--readonly` retains normal resolution but forbids
settings writes through TUI and export. Explicit telemetry logging is allowed by
that policy. A new `--paths` invocation cannot report recovery names from a prior
process; those belong to the original transaction's diagnostics and filesystem.

## Version 2 and stable views

Configuration is a bounded inert UTF-8 document, not executable content. Current
limits are 32768 file bytes, 1023 bytes per physical line, eight views, nine
columns, five ordered header-row roles, 31 UTF-8 bytes per view name and 127 bytes
per view filter. Parsing validates the complete candidate before replacing the
live model or owned original document. Unsupported future versions, malformed
reserved keys, duplicate recognized keys, duplicate columns/names/IDs and invalid
UTF-8 fail explicitly. Comments and supported unknown inert keys survive edits
within the document limits.

`ConfigVersion 2` requires `NextTabId` and `TabN.Id` for every active view. Each
identity is an explicitly bounded positive 32-bit integer, independent of its
name or index and of the compiler's `unsigned long` width. `NextTabId` is a
monotonic allocator; zero means exhaustion, and IDs are not silently reused.
Fresh defaults have Main/I/O identities 1/2 and next identity 9. Missing version
and version 1 are legacy input with deterministic identity assignment; identity
fields in those formats are refused rather than reinterpreted.

Insert, duplicate, rename, reorder, delete and visibility edits validate a copy
before changing the model. At most eight views and at least one visible view are
retained. The active view follows its stable identity when views move. Duplicate
copies recognized presentation settings and consumes a new identity; it does not
silently duplicate unknown extension ownership.

For an editable source document, every scoped `TabN.*` record is bound to its
original view ID. Saving a reorder rewrites only its positional prefix so known
and unknown fields follow the same view. Explicit deletion preserves unknown
fields for the removed identity as inert comments. An unknown scoped field
outside the document's original view set has no provable owner: serialization
refuses that ambiguous rewrite rather than assigning it to another view.
Oversized preservation output also fails before filesystem mutation.

The implemented presentation model includes htop/vi/Emacs keymaps, htop/NTop/
monochrome built-in themes, color-role overrides, mouse/function-bar/tab-strip
preferences, ordered visible header rows and named views with filters, ordered
stable column IDs, sort direction and tree preference. The exact Setup exposure
and bindings are specified by [the TUI contract](../interfaces/tui.md); settings
storage is not a claim that every future UI feature is implemented.

## Explicit migration and independent export

Loading legacy/version-1 input does not migrate it on disk. An explicit save
writes version 2 and records migration. Replacing an older destination retains
its original bytes in
`<destination>.migration-original-<pid>-<tick>-<sequence>`. If source and new
destination differ, such as `ntop.conf` to absent sibling `wtop.conf`, the old
source remains untouched. Target schema is tracked independently of source
schema: exporting defaults with `--no-config` over a version-1 destination must
still retain that destination's original. Migration originals have no automatic
expiry and must not be deleted by generic cleanup or store disposal.

`--write-config PATH` owns a separate store and a deep copy of the selected
editable document. It must not rebase active source ownership, comments or dirty
revision. An existing export target is privately validated as a supported
configuration before replacement; its values are not applied to the live model.
Future-version and malformed destinations are refused. Export preserves the
selected source's supported extensions, not a merge of unrelated destination
extensions. Arbitrary existing files are not a general output-overwrite API.

## Save transaction, concurrency and recovery

Settings have a live revision. Save captures a revision and serializes the full
validated candidate before any replacement. A verified completion clears dirty
state only if the live revision still equals that captured revision. Failed or
unverified saves retain edits. Current TUI saves are synchronous; this revision
rule also prevents a later asynchronous adapter from clearing newer edits.

The native candidate performs the following explicit transaction:

1. Validate the candidate, resolve an already selected destination and ensure its
   exact loaded bytes/existence are available as the baseline.
2. Make one exclusive `CREATE_NEW` attempt for the destination's `.wtop-lock`
   sidecar, using delete-on-close for the owned handle. An existing lock returns
   `busy` without waiting or deleting a sentinel. Filesystem crashes may leave a
   stale name requiring inspection.
3. Compare exact destination bytes/existence against the baseline under the
   cooperative lock. Refuse an external change or a file that appeared after
   source selection/loading. Do not silently adopt bytes never applied to the UI.
4. Create an exclusive same-directory `.wtop-tmp-*` candidate, with at most 16
   name attempts, and check write, flush and close. Recheck the baseline before
   replacement.
5. For an existing file, reserve an owned `.wtop-backup-*` or migration-original
   sibling and call `ReplaceFileW` without flags that ignore merge/ACL failures.
   For a new file, use non-overwriting `MoveFileExW`. A racing creation is a
   conflict. Replacement metadata/durability remains a native qualification case.
6. Reopen and compare the installed destination with the exact candidate. Only
   verified completion adopts the candidate document and saved revision. Remove
   an ordinary backup after verification; retain migration originals.

Outcomes are typed: `ok`, `conflict`, `busy`, `denied`, `invalid`, `unavailable`,
`unchanged-failure`, `committed-unverified`, and `recovery-required`, with native
error and commit/revision facts. Before replacement, only owned temporary files
may be cleaned up. A failed replacement can move names or leave recoverable data
under backup/candidate paths; do not promise the old file still occupies its
original name. Preserve those paths and invalidate the save baseline on uncertain
commit. If replacement reports success but the candidate cannot be verified,
report `committed-unverified` and never adopt unrelated observed bytes.

A verified save whose ordinary backup cleanup fails reports that retained backup
and requires reload before another save. Disposal releases memory/handles but
must not delete recovery files. CLI export failure reports retained paths on
stderr; TUI keeps the failure outcome and edits. Recovery requires inspecting the
destination, candidate and backup before deciding which copy is authoritative.
An empty reservation is possible; names alone do not prove contents.

This protocol serializes cooperating writers, not arbitrary external editors.
It is not a filesystem compare-and-swap, a native-I/O timeout, or a guarantee of
power-loss durability. The remaining path-race window, metadata/ACL behavior,
reparse changes, unusual filesystems, partial `ReplaceFileW` failures and shutdown
while saving require independently recorded native evidence. Microsoft's
[ReplaceFileW documentation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew)
describes partial-failure states; code must retain their uncertainty instead of
claiming universal unchanged-file behavior.

## Implementation and evidence boundaries

The portable implementation is in [settings.c](../../source/settings.c) and
[settings.h](../../source/settings.h), with parsing/identity/migration tests in
[test_settings.c](../../tests/test_settings.c). The NT ownership and transaction
adapter is [settings_store_nt.c](../../source/settings_store_nt.c), exercised by
[test_settings_store_nt.c](../../tests/test_settings_store_nt.c) and the settings
cases in [test_native_cli.py](../../tests/test_native_cli.py). Fixtures exist for
exact migration originals, conflict/busy behavior, future-schema export refusal,
flush failures, replacement failures and committed-but-unverified outcomes.
These are implementation/test references, not native pass or support records;
only source/artifact-bound evidence may satisfy release acceptance.

## Requirements and acceptance

### WT-REQ-040-01

Configuration loading and ordinary startup/exit MUST remain read-only, select
one document according to the ownership precedence above, and apply CLI
preferences afterward. They MUST NOT search arbitrary current directories or
silently bypass an invalid selected file. `--paths` MUST resolve without loading
settings or starting collection; `--no-config` MUST suppress implicit persistence
while preserving module identity in path reports.

Acceptance: WT-AC-040-01 — Use explicit, portable, adjacent, legacy, registered
user/machine and unrelated-current-directory sentinels; include denied/malformed
paths and `--no-config`. Verify owner/source/read/write fields, exact no-config
path output, preference precedence and zero startup/exit/path-query writes.

### WT-REQ-040-02

Portable, per-user and machine settings MUST follow explicit ownership and MUST
NOT silently redirect or overwrite another owner's data. Registration matching
MUST use the actual module path and agreed schema; writability alone MUST NOT
establish ownership. Installed user preferences MUST remain outside the payload.

Acceptance: WT-AC-040-02 — Exercise markers, explicit roots, both registry scopes,
wrong/absent owner records, read-only media, unavailable folder resolution,
multiple users and side-by-side profiles. Preserve sentinels and disclose missing
save destinations. Candidate MSI fixtures do not promote installer support.

### WT-REQ-040-03

Settings parsing and migration MUST be bounded, inert and versioned. View IDs
MUST survive reorder/rename and preserve supported unknown scoped records with
their owner. Ambiguous ownership, future schemas, exhausted identities and
oversized preservation MUST fail without partial model application. Explicit
migration MUST retain the prior source or destination as recoverable original.

Acceptance: WT-AC-040-03 — Test malformed/oversized input, both legacy formats,
v2 identity validation, ID exhaustion, insert/duplicate/rename/reorder/hide/delete,
unknown fields on moved/deleted/unknown scopes, and exact-byte migration recovery.
Include defaults exported over a legacy destination and future-schema refusal.

### WT-REQ-040-04

Settings writes MUST require an explicit save/export action, disclose the
selected destination, retain correct dirty/revision state and report individual
transaction outcomes. Cooperating writers MUST use bounded lock acquisition and
baseline checks. Uncertain replacement MUST preserve recoverable copies and
report uncertainty without implicit redirection, integration or blind cleanup.

Acceptance: WT-AC-040-04 — Exercise existing/new destinations, readonly policy,
missing parents, foreign/stale locks, exact external edits, concurrent writers,
flush/close failures, partial replacement failures, committed-but-unverified
outcomes, retained migration originals and recovery files, restart round-trips
and independent export provenance. Qualify actual artifacts/filesystems and
interruption behavior separately; do not substitute source inspection or mocked
failure injection for native runtime evidence.
