# Changelog

## 0.4.0-beta.1 — unpublished candidate

This section describes the beta candidate, not a publication announcement.
The GitHub draft/public state and its attached receipts determine availability.

### Added
- Explicit portable-x64 beta contract and privacy-aware issue templates.
- Native version resources, exact SDK observation, reconstructed source-tree
  verification and unchanged-payload portable packages.
- Draft-only staging and separate fail-closed public promotion; actual ordinary-user
  Windows 11/human review and live public feedback are mandatory.
- Native viewport matrix, interactive desktop receipt collector and allowlisted
  support report tooling (no automatic upload).

### Fixed
- AGE sorting uses the CPU/age acquisition timestamp rather than a later memory
  observation timestamp.
- System-commit conversion writes outputs transactionally; late overflow cannot
  leave mixed old/new values.
- Invalid action kinds are rejected across the whole batch before any mutation.

### Retained / compatibility
- htop-default controls/colors with vi/Emacs alternatives; explicit settings saves.
- NTop `-d` dump and `-n` name-selection meanings; explicit long-option units.
- Snapshot and settings schema 2, lifetime-safe actions, bounded optional providers
  and finite foreground logs from the 0.4.0-dev.1 checkpoint.
- GPL-3.0-only source/notices and pinned upstream ancestry.

### Known limitations
- x64 portable prerelease only; no stable support, public MSI/WinGet, native ARM64,
  historical monitor or GUI claim. Full Unicode rendering and htop parity remain open.
- Installer maintenance races/upgrades, broad blocked-work behavior and resource
  soaks require additional qualification. See TODO.md, ROADMAP.md and beta notes.
