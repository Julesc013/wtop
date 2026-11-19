#!/usr/bin/env python3
"""Prepare offline, hash-bound community-channel inputs; never publish/install.

The caller supplies a fully verified release bundle. The generated URLs describe
that release's exact assets; this tool does not assert they are publicly served,
reserve a package identity, contact a feed or submit a manifest. Channel-native
validation/acquisition and community acceptance are independent pending gates.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

from package_candidate import json_bytes, sha256
from release_bundle import profile_names, verify as verify_release
from release_metadata import metadata

REPOSITORY = 'https://github.com/Julesc013/wtop'
WINGET_ID = 'Julesc013.Wtop'
WINGET_SCHEMA = '1.12.0'
SCOOP_ARCH = {'win-nt-x86': '32bit', 'win-nt-x64': '64bit', 'win-nt-arm64': 'arm64'}


def chocolatey_version(version: str) -> str:
    value = metadata(version)
    if value['channel'] == 'stable':
        return version
    # CCR still documents SemVer-1 compatibility. Preserve the full semantic
    # source version in the receipt, with an explicit sortable package mapping.
    return version.split('-')[0] + '-' + value['channel'] + f"{value['ordinal']:04d}"


def yaml_document(document: dict, kind: str) -> bytes:
    # JSON flow syntax is YAML 1.2, so strings and URLs need no ad-hoc escaping.
    return (f'# yaml-language-server: $schema=https://aka.ms/winget-manifest.{kind}.{WINGET_SCHEMA}.schema.json\n'
            + json.dumps(document, indent=2, ensure_ascii=True) + '\n').encode('utf-8')


def documents(manifest: dict) -> dict[str, bytes]:
    if manifest.get('schema') != 'wtop.beta/2':
        raise ValueError('channel preparation requires a multi-profile source-bound release bundle')
    version = manifest['version']; revision = manifest['payload']['source']['revision']
    info = metadata(version)
    assets = {item['name']: item for item in manifest['assets']}
    payloads = sorted((entry['payload'] for entry in manifest['profiles']), key=lambda p: p['profile'])
    entries = []
    for payload in payloads:
        profile = payload['profile']; name = profile_names(version, profile)['portable']
        item = assets[name]
        entries.append({'profile': profile, 'architecture': payload['pe']['architecture'],
                        'url': f'{REPOSITORY}/releases/download/{info["tag"]}/{name}',
                        'sha256': item['sha256'], 'size': item['size'],
                        'executable_sha256': payload['executable']['sha256']})
    common = {'PackageIdentifier': WINGET_ID, 'PackageVersion': version}
    install = dict(common, InstallerType='zip', NestedInstallerType='portable',
                   NestedInstallerFiles=[{'RelativeFilePath': 'wtop.exe', 'PortableCommandAlias': 'wtop'}],
                   Commands=['wtop'], UpgradeBehavior='uninstallPrevious',
                   Installers=[{'Architecture': e['architecture'], 'Scope': scope,
                                'InstallerUrl': e['url'], 'InstallerSha256': e['sha256'].upper()}
                               for e in entries for scope in ('user', 'machine')],
                   ManifestType='installer', ManifestVersion=WINGET_SCHEMA)
    locale = dict(common, PackageLocale='en-US', Publisher='Wtop contributors',
                  PublisherUrl=REPOSITORY, PackageName='Wtop', PackageUrl=REPOSITORY,
                  License='GPL-3.0-only', LicenseUrl=f'{REPOSITORY}/blob/{revision}/LICENSE',
                  ShortDescription='Native Windows terminal process monitor derived from NTop.',
                  Description='Wtop continues Gian Sass\'s NTop with htop-style controls and structured inspection.',
                  ReleaseNotesUrl=f'{REPOSITORY}/releases/tag/{info["tag"]}',
                  ManifestType='defaultLocale', ManifestVersion=WINGET_SCHEMA)
    version_doc = dict(common, DefaultLocale='en-US', ManifestType='version', ManifestVersion=WINGET_SCHEMA)
    scoop = {'version': version, 'description': locale['ShortDescription'], 'homepage': REPOSITORY,
             'license': {'identifier': 'GPL-3.0-only', 'url': locale['LicenseUrl']},
             'architecture': {SCOOP_ARCH[e['profile']]: {'url': e['url'], 'hash': e['sha256']} for e in entries},
             'bin': 'wtop.exe',
             'notes': 'Prepared package; native channel acceptance is pending. User settings remain in %APPDATA%\\Wtop. No updater or service is installed.'}
    package = ET.Element('package'); package_metadata = ET.SubElement(package, 'metadata')
    for key, value in (
        ('id', 'wtop.portable'), ('version', chocolatey_version(version)), ('title', 'Wtop Portable'),
        ('authors', 'Wtop contributors; Gian Sass (NTop)'), ('owners', 'Julesc013'),
        ('summary', locale['ShortDescription']), ('description', locale['Description']),
        ('projectUrl', REPOSITORY), ('projectSourceUrl', REPOSITORY),
        ('licenseUrl', locale['LicenseUrl']), ('requireLicenseAcceptance', 'false'),
        ('releaseNotes', f'Application version {version}; {locale["ReleaseNotesUrl"]}'),
        ('tags', 'wtop ntop htop process-monitor terminal native portable'),
    ):
        ET.SubElement(package_metadata, key).text = value
    ET.SubElement(ET.SubElement(package, 'files'), 'file', src='tools\\**', target='tools')
    urls = {e['architecture']: e for e in entries}
    cases = []
    for architecture, windows in (('x86', 'x86'), ('x64', 'AMD64'), ('arm64', 'ARM64')):
        if architecture in urls:
            e = urls[architecture]
            cases.append(f"    '{windows}' {{ $url = '{e['url']}'; $checksum = '{e['sha256']}' }}")
    script = '''# SPDX-License-Identifier: GPL-3.0-only
# Prepared offline; do not submit/host before channel acquisition acceptance.
$ErrorActionPreference = 'Stop'
$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$nativeArchitecture = $env:PROCESSOR_ARCHITEW6432
if (-not $nativeArchitecture) { $nativeArchitecture = $env:PROCESSOR_ARCHITECTURE }
if ($env:ChocolateyForceX86 -eq 'true') { $nativeArchitecture = 'x86' }
switch ($nativeArchitecture) {
''' + '\n'.join(cases) + '''
    default { throw 'No qualified Wtop archive for the native architecture.' }
}
Install-ChocolateyZipPackage -PackageName 'wtop.portable' -Url $url -UnzipLocation $toolsDir -Checksum $checksum -ChecksumType 'sha256'
# Chocolatey owns its generated command shim. Wtop edits no PATH value.
'''
    result = {
        f'winget/{WINGET_ID}.installer.yaml': yaml_document(install, 'installer'),
        f'winget/{WINGET_ID}.locale.en-US.yaml': yaml_document(locale, 'defaultLocale'),
        f'winget/{WINGET_ID}.yaml': yaml_document(version_doc, 'version'),
        'scoop/wtop.json': json_bytes(scoop),
        'chocolatey/wtop.portable.nuspec': ET.tostring(package, encoding='utf-8', xml_declaration=True) + b'\n',
        'chocolatey/tools/chocolateyinstall.ps1': script.encode('utf-8'),
        'README.md': (f'''# Wtop {version}: offline distribution preparation

These files are prepared inputs, not published listings or accepted packages.
No package ID is reserved. No bucket, tap, feed, listing or public download was
created by this operation. URLs point only to the exact intended GitHub release;
a draft release does not make them publicly downloadable.

## Ownership and lifecycle

WinGet uses ZIP/portable installers and owns the `wtop` command alias under its
user or machine scope. Scoop owns its `wtop.exe` shim. Chocolatey owns its generated
shim and package directory. None of these manifests writes a Wtop registry owner,
adds a raw PATH token, installs a service, or creates an active adjacent config.
Wtop therefore saves explicit preferences under `%APPDATA%\\Wtop`; package-manager
updates do not own that directory. Do not add `wtop.portable` or active `wtop.conf`
beside a managed EXE unless you intentionally accept separate portable settings.

MSI is omitted: its transaction qualification is independent. Simultaneous channel
installs can provide competing `wtop` commands; inspect `where.exe wtop` before
choosing one. No manifest silently removes NTop or another channel's installation.

Chocolatey package version `{chocolatey_version(version)}` explicitly maps to
application SemVer `{version}`; it avoids CCR's documented dotted-prerelease
compatibility limitation. The inherited `ntop.portable` package identity is untouched.

## Remaining gates (no automatic execution)

1. Review namespace ownership, license attribution, native minimum OS and channel policy.
2. Validate using the target clients: `winget validate --manifest winget`, Scoop's
   manifest/schema checks, and `choco pack chocolatey/wtop.portable.nuspec`.
3. In disposable ordinary-user and machine fixtures acquire public release bytes,
   verify their hashes and test install, update, command resolution and uninstall.
4. Confirm modified/foreign-file and user-settings preservation under each manager.
5. Obtain explicit publication authorization before submitting any channel listing.

The preparation verifier checks exact release hashes, source/version/architecture,
safe filenames, generated input consistency and all file bytes. It is not a
replacement for client-native schema or lifecycle tests.
''').encode('utf-8'),
    }
    receipt = {'schema': 'wtop.distribution-preparation/1', 'application_version': version,
               'source_revision': revision, 'release_tag': info['tag'], 'publication': 'not_requested_not_performed',
               'proposed_identities': {'winget': WINGET_ID, 'scoop': 'wtop', 'chocolatey': 'wtop.portable'},
               'chocolatey_package_version': chocolatey_version(version), 'entries': entries,
               'channel_validation': 'pending_native_clients', 'channel_lifecycle': 'pending',
               'files': [{'name': name, 'size': len(body), 'sha256': sha256(body)} for name, body in sorted(result.items())]}
    result['preparation.json'] = json_bytes(receipt)
    return result


def prepare(bundle: Path, output: Path) -> dict:
    manifest = verify_release(bundle)
    expected = documents(manifest)
    if output.exists() and any(output.iterdir()):
        raise ValueError('preparation output must be empty; never overwrite reviewed inputs')
    output.mkdir(parents=True, exist_ok=True)
    for name, body in expected.items():
        path = output / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(body)
    return verify(bundle, output)


def verify(bundle: Path, output: Path) -> dict:
    expected = documents(verify_release(bundle))
    paths = list(output.rglob('*'))
    if any(path.is_symlink() for path in paths):
        raise ValueError('preparation contains a symbolic link')
    actual = {str(path.relative_to(output)).replace('\\', '/'): path.read_bytes() for path in paths if path.is_file()}
    if actual != expected:
        raise ValueError('preparation differs from the exact verified release inputs')
    return json.loads(expected['preparation.json'])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('prepare', 'verify'))
    parser.add_argument('--bundle', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        receipt = prepare(args.bundle, args.output) if args.command == 'prepare' else verify(args.bundle, args.output)
        print(json.dumps({'verified': True, 'application_version': receipt['application_version'],
                          'publication': receipt['publication'], 'channel_validation': receipt['channel_validation']}, indent=2))
        return 0
    except (OSError, ValueError, KeyError) as error:
        print('Distribution preparation failed: ' + str(error), file=sys.stderr); return 2


if __name__ == '__main__':
    raise SystemExit(main())
