#!/usr/bin/env python3
"""Assemble and independently verify a source-bound multiarchitecture prerelease.

Every included profile must carry its own actual Windows test receipt. A missing
profile is an explicit omission, not inferred support. No network, credentials,
signing or publication takes place here. The schema-1 Beta 1 reader remains intact.
"""
from __future__ import annotations
import argparse
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile
from beta_release import (BetaError, GIT, MAX_FILE, ROOT, SHA, archive_tree,
                          checked_file, load_json, native_tests, source_archive,
                          validate_native, verify as verify_legacy, required_tests)
from inspect_pe import inspect_bytes
from package_candidate import json_bytes, sha256, verify as verify_candidate, third_party_notices_required
from release_metadata import PROFILES, asset_label, metadata

ARCHITECTURES = {'win-nt-x86':'x86', 'win-nt-x64':'x64', 'win-nt-arm64':'arm64'}
POLICY = {'alpha_beta_human_review':'optional', 'stable_human_review':'required',
          'publication':'explicit_dispatch_only', 'latest':False}
OMITTED = ['MSI/per-user/system installation: lifecycle qualification remains open',
           'WinGet/Scoop/Chocolatey publication: no community package acceptance claimed',
           'DOS, Win16, Win9x, older NT, RT, CE, OS/2 and ReactOS: not qualified by these profiles',
           'GUI and a new setup EXE: outside Beta 2 scope']


def omitted_deliverables(version):
    if not third_party_notices_required(version):
        return OMITTED
    return ['MSI/per-user/system installation: hardened candidate and native lifecycle fixtures remain separate from release qualification',
            'WinGet/Scoop/Chocolatey publication: offline preparation only; no listing or channel acceptance claimed',
            'DOS, Win16, Win9x, older NT, RT, CE, OS/2 and ReactOS: not qualified by these profiles',
            'GUI and a new setup EXE: outside this portable prerelease scope']


def profile_names(version, profile):
    stem = f'wtop-{version}-{profile}'
    return {'exe':stem + '.exe', 'portable':stem + '-portable.zip',
            'payload':'payload-' + profile + '.json',
            'native':'native-receipt-' + profile + '.json',
            'junit':'native-tests-' + profile + '.xml',
            'scan':'security-scan-' + profile + '.json'}


def validate_payload(payload):
    version = metadata(payload['version'])
    if version['channel'] not in ('alpha', 'beta') or payload['profile'] not in ARCHITECTURES:
        raise BetaError('assembler accepts only implemented modern NT alpha/beta profiles')
    source = payload['source']; pe = payload['pe']
    if source.get('dirty') is not False or not GIT.fullmatch(source.get('revision','')) or not GIT.fullmatch(source.get('tree','')):
        raise BetaError('clean exact source commit/tree required')
    if not re.fullmatch(r'\d+\.\d+\.\d+\.\d+', str(payload['build'].get('sdk',''))):
        raise BetaError('exact observed Windows SDK required')
    if (pe.get('architecture') != ARCHITECTURES[payload['profile']] or pe.get('subsystem') != 3
            or pe.get('dll') or pe.get('resources',{}).get('present') is not True
            or pe.get('runtime_dlls')):
        raise BetaError('profile, console resource or external runtime contract mismatch')


def validate_execution(receipt, payload):
    validate_native(receipt, payload)
    host = receipt.get('host', {})
    native = host.get('native_architecture')
    arch = ARCHITECTURES[payload['profile']]
    # Win32 on the x64 runner is explicitly WoW64 coverage, never native x86 OS
    # qualification. ARM64 cannot be admitted from an x64 compile-only runner.
    if native not in ({'x86','x64'} if arch == 'x86' else {arch}):
        raise BetaError('test host does not cover this binary architecture')
    if (type(host.get('build')) is not int or type(host.get('elevated')) is not bool
            or host.get('product_type') not in (1, 2, 3)):
        raise BetaError('actual Windows build, product type and token elevation required')
    if receipt.get('embedded_numeric_version') != metadata(payload['version'])['native_version']:
        raise BetaError('embedded numeric version differs from the shared ledger')
    if receipt.get('human_visual_acceptance') is not False:
        raise BetaError('automated receipt must not claim a human review')


def validate_scan(receipt, payload):
    exact = {'schema':'wtop.security-scan/1', 'version':payload['version'],
             'profile':payload['profile'], 'source_revision':payload['source']['revision'],
             'executable_sha256':payload['executable']['sha256']}
    if any(receipt.get(key) != value for key, value in exact.items()):
        raise BetaError('scan receipt does not bind the final executable')
    if receipt.get('status') not in ('clean', 'unavailable'):
        raise BetaError('failed or detected-threat security scan blocks assembly')
    if receipt['status'] == 'clean':
        if (receipt.get('engine') != 'Microsoft Defender' or not receipt.get('signature_version')
                or type(receipt.get('detected_threats')) is not int or receipt['detected_threats'] != 0
                or type(receipt.get('scan_exit_code')) is not int or receipt['scan_exit_code'] != 0
                or receipt.get('method') != 'custom-no-remediation-ignore-exclusions'
                or not receipt.get('scan_completed_at')):
            raise BetaError('clean scan lacks actual engine/completion evidence')
    elif not receipt.get('reason'):
        raise BetaError('unavailable scanner requires an explicit reason')


def release_notes(changes: str, version: str, profiles: list[dict]) -> bytes:
    title = metadata(version)['title']
    # Authored changes are source-controlled. Download and qualification facts
    # are derived from the actual profile set, rather than promised filenames.
    lines = ['## Downloads', '', 'These are portable prerelease binaries. Choose the architecture of the Windows environment you will test.', '']
    for entry in profiles:
        payload = entry['payload']; profile = payload['profile']
        for kind, suffix in (('portable','Portable ZIP'), ('exe','Executable')):
            name = profile_names(version, profile)[kind]
            lines.append(f'- [{asset_label(version, profile, suffix)}](https://github.com/Julesc013/wtop/releases/download/v{version}/{name})')
    lines += ['', f'- [Corresponding source](https://github.com/Julesc013/wtop/releases/download/v{version}/wtop-{version}-source.tar.gz)',
              '', '## Changes', '']
    authored = changes.strip().splitlines()
    if authored and authored[0].lstrip('# ').strip() in (title, 'Wtop ' + version):
        authored = authored[1:]
    lines += authored
    lines += ['', '## Known limitations', '']
    lines += ['- ' + item + '.' for item in omitted_deliverables(version)]
    lines += ['- Human desktop acceptance and ordinary-user coverage are separate from these automated receipts.',
              '- Unsigned binaries are identified as such. A digest proves byte identity and does not identify a trusted publisher.',
              '- Report reproducible problems at https://github.com/Julesc013/wtop/issues; never include private process command lines.',
              '', '## Verification', '']
    for entry in profiles:
        payload = entry['payload']; native = entry['native']; host = native['host']
        lines.append(f"- `{payload['profile']}`: Windows build {host['build']}, product type {host['product_type']}, "
                     f"native host {host['native_architecture']}, elevated token {str(host['elevated']).lower()}; "
                     f"{len(native['tests']['names'])} native tests passed; Authenticode `{native['authenticode']}`; "
                     f"security scan `{entry['security_scan']['status']}`.")
    lines += ['', 'Standalone and ZIP executables are byte-identical for each profile. The manifest binds all assets to the source commit, native test receipts and SHA256SUMS.',
              'An unavailable security scanner remains an explicit public-promotion blocker; it is never recorded as a clean scan.', '']
    return '\n'.join(lines).encode('utf-8')


def assemble(inputs: list[Path], notes: Path, output: Path, root=ROOT):
    if not inputs or len(inputs) > len(ARCHITECTURES):
        raise BetaError('supply one to three distinct profile input directories')
    profiles = []; assets = {}; source = None; version = None; license_data = None
    for candidate in inputs:
        original = verify_candidate(candidate)
        payload = original['payload']; validate_payload(payload)
        profile = payload['profile']
        if any(p['payload']['profile'] == profile for p in profiles):
            raise BetaError('duplicate profile')
        if source is None:
            source, version, license_data = payload['source'], payload['version'], checked_file(candidate/'LICENSE')
        if payload['source'] != source or payload['version'] != version or checked_file(candidate/'LICENSE') != license_data:
            raise BetaError('all profiles must use the same exact source/version/license')
        native = load_json(candidate/'native-receipt.json'); validate_execution(native, payload)
        if native['tests'] != native_tests(candidate/'native-tests.xml', required_tests(payload['version'])):
            raise BetaError('JUnit bytes differ from the native receipt')
        scan = load_json(candidate/'security-scan.json'); validate_scan(scan, payload)
        entry = {'payload':payload, 'native':native, 'security_scan':scan}
        profiles.append(entry)
        names = profile_names(version, profile)
        portable = next(item['name'] for item in original['artifacts'] if item['kind'] == 'portable-zip')
        assets.update({names['exe']:checked_file(candidate/'wtop.exe'), names['portable']:checked_file(candidate/portable),
                       names['payload']:json_bytes(payload), names['native']:json_bytes(native),
                       names['junit']:checked_file(candidate/'native-tests.xml'), names['scan']:json_bytes(scan)})
    profiles.sort(key=lambda item:item['payload']['profile'])
    if third_party_notices_required(version):
        notices = checked_file(inputs[0] / 'THIRD-PARTY-NOTICES.md')
        if any(checked_file(candidate / 'THIRD-PARTY-NOTICES.md') != notices for candidate in inputs):
            raise BetaError('third-party notices differ between profiles')
        assets['THIRD-PARTY-NOTICES.md'] = notices
    current = subprocess.run(['git','-C',str(root),'rev-parse','HEAD'],check=True,capture_output=True,text=True).stdout.strip()
    if current != source['revision']:
        raise BetaError('source checkout differs from native payload source')
    archive = source_archive(root, current)
    tree, entries = archive_tree(archive)
    if tree != source['tree'] or entries.get('VERSION',b'').decode().strip() != version or entries.get('LICENSE') != license_data:
        raise BetaError('corresponding source/license/version mismatch')
    if third_party_notices_required(version) and entries.get('THIRD-PARTY-NOTICES.md') != assets['THIRD-PARTY-NOTICES.md']:
        raise BetaError('corresponding third-party notices differ')
    assets.update({f'wtop-{version}-source.tar.gz':archive, 'LICENSE':license_data,
                   'RELEASE-NOTES.md':release_notes(checked_file(notes,1024*1024).decode('utf-8'), version, profiles)})
    primary = next((entry['payload'] for entry in profiles if entry['payload']['profile'] == 'win-nt-x64'), profiles[0]['payload'])
    manifest = {'schema':'wtop.beta/2', 'product':'wtop', 'version':version, 'tag':'v'+version,
                'metadata':metadata(version), 'channel':'prerelease', 'stable_supported':False,
                'payload':primary, 'profiles':profiles, 'policy':POLICY,
                'omitted_profiles':sorted(set(ARCHITECTURES)-{p['payload']['profile'] for p in profiles}),
                'omitted_deliverables':omitted_deliverables(version),
                'assets':[{'name':name,'size':len(body),'sha256':sha256(body)} for name,body in sorted(assets.items())]}
    if output.exists() and any(output.iterdir()):
        raise BetaError('release output must be empty; existing artifacts are immutable')
    output.mkdir(parents=True, exist_ok=True)
    assets['beta-manifest.json'] = json_bytes(manifest)
    for name, body in assets.items():
        (output/name).write_bytes(body)
    (output/'SHA256SUMS').write_bytes(''.join(f'{sha256(body)}  {name}\n' for name,body in sorted(assets.items())).encode('ascii'))
    return verify(output)


def verify(directory: Path):
    manifest = load_json(directory/'beta-manifest.json')
    if manifest.get('schema') == 'wtop.beta/1':
        return verify_legacy(directory)
    version = manifest.get('version',''); info = metadata(version)
    if (manifest.get('schema') != 'wtop.beta/2' or manifest.get('metadata') != info
            or info['channel'] not in ('alpha','beta') or manifest.get('tag') != info['tag']
            or manifest.get('channel') != 'prerelease' or manifest.get('stable_supported') is not False
            or manifest.get('policy') != POLICY or manifest.get('omitted_deliverables') != omitted_deliverables(version)):
        raise BetaError('release metadata, channel or qualification policy mismatch')
    profiles = manifest.get('profiles', [])
    if not isinstance(profiles,list) or not 1 <= len(profiles) <= 3:
        raise BetaError('invalid profile count')
    identifiers = [p['payload']['profile'] for p in profiles]
    if len(identifiers) != len(set(identifiers)) or any(p not in ARCHITECTURES for p in identifiers):
        raise BetaError('unknown/duplicate profile')
    if manifest.get('omitted_profiles') != sorted(set(ARCHITECTURES)-set(identifiers)):
        raise BetaError('omitted profiles must be explicit')
    primary = next((p['payload'] for p in profiles if p['payload']['profile']=='win-nt-x64'), profiles[0]['payload'])
    if manifest.get('payload') != primary:
        raise BetaError('primary profile/source mismatch')
    expected = {f'wtop-{version}-source.tar.gz', 'LICENSE', 'RELEASE-NOTES.md'}
    if third_party_notices_required(version):
        expected.add('THIRD-PARTY-NOTICES.md')
    for profile in identifiers:
        expected.update(profile_names(version,profile).values())
    listed = manifest.get('assets',[])
    if len(listed) != len(expected) or {x.get('name') for x in listed} != expected:
        raise BetaError('unexpected/unsafe/duplicate asset inventory')
    data = {}
    for item in listed:
        body = checked_file(directory/item['name'])
        if len(body) != item['size'] or sha256(body) != item['sha256']:
            raise BetaError('asset identity mismatch: ' + item['name'])
        data[item['name']] = body
    hashes = {name:sha256(body) for name,body in data.items()}
    hashes['beta-manifest.json'] = sha256(checked_file(directory/'beta-manifest.json'))
    if checked_file(directory/'SHA256SUMS').decode('ascii') != ''.join(f'{digest}  {name}\n' for name,digest in sorted(hashes.items())):
        raise BetaError('checksum inventory mismatch')
    tree, entries = archive_tree(data[f'wtop-{version}-source.tar.gz'])
    if tree != primary['source']['tree'] or entries.get('VERSION',b'').decode().strip() != version or entries.get('LICENSE') != data['LICENSE']:
        raise BetaError('source tree/version/license differs from payload')
    if third_party_notices_required(version) and entries.get('THIRD-PARTY-NOTICES.md') != data['THIRD-PARTY-NOTICES.md']:
        raise BetaError('source third-party notices differ from release')
    for entry in profiles:
        payload = entry['payload']; validate_payload(payload)
        if payload['version'] != version or payload['source'] != primary['source']:
            raise BetaError('mixed version/source across profiles')
        names = profile_names(version,payload['profile'])
        if load_json(directory/names['payload']) != payload:
            raise BetaError('payload provenance mismatch')
        exe = data[names['exe']]
        if sha256(exe) != payload['executable']['sha256'] or len(exe) != payload['executable']['size'] or inspect_bytes(exe) != payload['pe']:
            raise BetaError('PE/provenance mismatch')
        with zipfile.ZipFile(io.BytesIO(data[names['portable']])) as archive:
            members = {'wtop.exe','LICENSE','payload.json','PORTABLE.txt','wtop.conf.example'}
            if third_party_notices_required(version):
                members.add('THIRD-PARTY-NOTICES.md')
            if (len(archive.namelist()) != len(members) or set(archive.namelist()) != members
                    or any(i.file_size > MAX_FILE for i in archive.infolist())):
                raise BetaError('unexpected portable members')
            if archive.read('wtop.exe') != exe or archive.read('LICENSE') != data['LICENSE'] or archive.read('payload.json') != json_bytes(payload):
                raise BetaError('ZIP payload differs from standalone')
            if third_party_notices_required(version):
                notices = data['THIRD-PARTY-NOTICES.md']
                if (archive.read('THIRD-PARTY-NOTICES.md') != notices or payload.get('legal_files') !=
                        [{'name': 'THIRD-PARTY-NOTICES.md', 'size': len(notices), 'sha256': sha256(notices)}]):
                    raise BetaError('ZIP third-party notices/provenance differ')
        native = load_json(directory/names['native']); validate_execution(native,payload)
        scan = load_json(directory/names['scan']); validate_scan(scan,payload)
        if native != entry['native'] or scan != entry['security_scan'] or native['tests'] != native_tests(directory/names['junit'], required_tests(version)):
            raise BetaError('native/scan/actual JUnit receipt mismatch')
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command',required=True)
    create = commands.add_parser('assemble')
    create.add_argument('--input',type=Path,action='append',required=True)
    create.add_argument('--notes',type=Path,required=True);create.add_argument('--output',type=Path,required=True)
    check = commands.add_parser('verify');check.add_argument('directory',type=Path)
    args = parser.parse_args(argv)
    try:
        manifest = assemble(args.input,args.notes,args.output) if args.command=='assemble' else verify(args.directory)
        print(json.dumps({'verified':True,'version':manifest['version'],'source':manifest['payload']['source']['revision'],
                          'profiles':[p['payload']['profile'] for p in manifest.get('profiles',[])],
                          'publication':'draft preparation only'},indent=2))
        return 0
    except (OSError,ValueError,KeyError,TypeError,subprocess.SubprocessError,zipfile.BadZipFile) as error:
        print('Release bundle failed: '+str(error),file=sys.stderr);return 2


if __name__=='__main__':raise SystemExit(main())
