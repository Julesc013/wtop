# Exact-payload Windows 11 beta acceptance

Run the reviewed candidate as an **ordinary user** on Windows 11 x64, not a
Server runner or an emulated ARM64/x86 substitute. Test tooling needs Python 3.10+
(the Wtop application does not). Extract the candidate's portable archive, then:

```powershell
python tools/desktop_acceptance.py C:\Test\wtop-beta --terminal "Windows Terminal <version>" --output desktop-receipt.json
```

The script verifies the payload hash/source/version, records native workstation
identity, runs read-only smoke queries, and launches the TUI against disposable
processes. All settings live in its temporary directory. It requests separate
human confirmations for readability/resize, keymaps/mouse, readonly controls,
settings, identity-safe termination and console restoration. It provides no
assume-yes path and never uploads data. Review 40x25, 80x25, 80x50 and the tiny
fallback; test normal F10 and Ctrl+C. Human assertions are attestation, not an
automatic proof; review the receipt and attach only that reviewed file.

A receipt binds the exact EXE hash, version and source revision. A source or
payload change requires a new receipt. Server CI and input injection remain
separate evidence. A blocked receipt is useful diagnostics but cannot authorize
publication. Keep original failed receipts rather than editing them to passed.

The owned-console CI matrix also tests 40x25, 80x25, 40x10 and 39x9 recovery.
80x50 is tested there only if the native host permits it; a host-limited result
does not replace the required desktop human review. No configuration is saved
implicitly by resizing or quitting.
