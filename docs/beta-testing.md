# Testing the portable beta

Use a noncritical ordinary-user Windows 11 x64 session first. An initial read-only
run cannot change process state, launch commands or save configuration:

```powershell
Get-FileHash .\wtop.exe -Algorithm SHA256
.\wtop.exe --no-config --readonly
```

Compare the digest with the trusted release manifest. The standalone application
requires no Python, agent runtime or installation. Python is used only by the
optional source-side qualification/diagnostic tools.

## Everyday checks

Exercise htop/vi/Emacs navigation and Help/Setup/search/filter/sort, mouse on/off,
40x25/80x25/80x50, rapid resize and the tiny-window recovery screen. Confirm that
old data is marked stale and placeholders are disclosed for unsupported glyphs.
Keep raw Unicode outputs private if they expose user/process information.

Save only into a disposable directory using an explicit configuration or portable
root. Check save/reload, two writers, an external edit, a denied/read-only file,
and ordinary startup/exit without a write. Preserve reported recovery files until
you have inspected them. Do not make the first test against an important config.

For destructive process actions use only a disposable process you deliberately
created. Do not use production services, PID 0/4, security software or another
person's work. A passed portable test does not establish permission behavior on
all tokens. The [desktop evidence tool](desktop-acceptance.md) creates its own
children and asks for actual human observations; never manufacture PASS answers.

## Report only the information necessary

The supplied support reporter queries build metadata only, not a runtime process
snapshot. It does not read settings, report their paths, or enumerate environment,
users, command lines or services. It discards unknown fields and never uploads.

```powershell
python tools/support_report.py --exe C:\path\to\wtop.exe --output support.json
```

Review that file and attach it manually only where appropriate. CLI `--paths`,
`inspect --command-line`, JSONL logs, screenshots and crash dumps can contain
sensitive information and are **not** default report attachments. Sanitization
cannot guarantee that an arbitrary full dump contains no secrets.

GitHub Issues must actually be enabled; templates alone are insufficient. Security
reports follow SECURITY.md and should not expose secrets in public. Private
reporting is mentioned only when the repository actually exposes that option.

## Evidence levels

Automated Server-host console injection, a real client run, a human visual review,
source/PE inspection and resource soaks prove different things. Record exact bytes
and context. A successful normal exit does not establish every blocked-call or
forced-termination path. Historical probes do not qualify historical monitors.
