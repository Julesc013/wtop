#!/usr/bin/env python3
"""Exact-byte portable beta assembly and fail-closed evidence validation.

No credentials, network or publication in this module. Hashes establish byte
identity, not authorship. Human receipts are attestations reviewed by a maintainer,
not cryptographic proof that somebody actually performed the checks.
"""
from __future__ import annotations
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tarfile
import zipfile
import xml.etree.ElementTree as ET
from desktop_acceptance import CHECKS, supported_host
from package_candidate import PackageError, json_bytes, sha256, verify as verify_candidate
from inspect_pe import inspect_bytes

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.compile(r"0\.4\.0-beta\.[1-9][0-9]*\Z")
SHA = re.compile(r"[0-9a-f]{64}\Z")
GIT = re.compile(r"[0-9a-f]{40}\Z")
MAX_FILE = 64 * 1024 * 1024
MAX_TOTAL = 256 * 1024 * 1024
LEGACY_REQUIRED_TESTS = frozenset((
    'portable_core','portable_settings','portable_ui','portable_text','portable_metrics',
    'portable_actions','portable_legacy_probe','native_settings_store','native_providers',
    'native_output','native_tree','cli_version','cli_capabilities','cli_gui_reserved',
    'cli_snapshot','native_cli_behavior','native_provider_behavior','native_tui_interaction',
))
BETA2_REQUIRED_TESTS = LEGACY_REQUIRED_TESTS | {'portable_frame', 'native_console_frame', 'native_input'}
REQUIRED_TESTS = BETA2_REQUIRED_TESTS | {'portable_grapheme', 'native_features', 'native_tui_inspection', 'native_resource_behavior'}


def required_tests(version: str):
    if version == '0.4.0-beta.1':
        return LEGACY_REQUIRED_TESTS
    if version == '0.4.0-beta.2':
        return BETA2_REQUIRED_TESTS
    return REQUIRED_TESTS

class BetaError(ValueError):
    pass


def checked_file(path: Path, limit=MAX_FILE) -> bytes:
    if path.is_symlink() or not path.is_file() or path.stat().st_size > limit:
        raise BetaError('missing, linked or oversized input: ' + path.name)
    return path.read_bytes()


def load_json(path: Path) -> dict:
    def unique(pairs):
        value = {}
        for key, item in pairs:
            if key in value:
                raise BetaError('duplicate JSON field: ' + key)
            value[key] = item
        return value
    result = json.loads(checked_file(path, 2 * 1024 * 1024), object_pairs_hook=unique,
                        parse_constant=lambda _: (_ for _ in ()).throw(BetaError('nonfinite JSON')))
    if not isinstance(result, dict):
        raise BetaError('expected JSON object')
    return result


def native_tests(path: Path, required=REQUIRED_TESTS) -> dict:
    """Read actual CTest JUnit. Missing/failed/skipped mandatory tests all block."""
    data = checked_file(path, 16 * 1024 * 1024)
    if b'<!DOCTYPE' in data.upper() or b'<!ENTITY' in data.upper():
        raise BetaError('DTD/entity input is not a JUnit receipt')
    root = ET.fromstring(data)
    cases = list(root.iter('testcase'))
    names = [case.get('name', '') for case in cases]
    if len(names) != len(set(names)) or not required.issubset(names):
        raise BetaError('missing or duplicate mandatory native test')
    for case in cases:
        if any(case.find(tag) is not None for tag in ('failure', 'error', 'skipped')):
            raise BetaError('native suite contains a failed/skipped case: ' + case.get('name', ''))
        if case.get('status', 'run') not in ('run', 'passed'):
            raise BetaError('native test did not run')
    return {'status': 'passed', 'names': sorted(names), 'junit_sha256': sha256(data)}


def validate_native(receipt: dict, payload: dict) -> None:
    exact = {'schema':'wtop.beta-native/1', 'result':'passed', 'version':payload['version'],
             'profile':payload['profile'], 'source_revision':payload['source']['revision'],
             'executable_sha256':payload['executable']['sha256']}
    if any(receipt.get(key) != value for key, value in exact.items()):
        raise BetaError('native receipt does not bind the exact beta payload')
    tests = receipt.get('tests', {})
    required = required_tests(payload['version'])
    if tests.get('status') != 'passed' or not required.issubset(tests.get('names', [])):
        raise BetaError('native receipt does not cover required tests')
    if not SHA.fullmatch(str(tests.get('junit_sha256', ''))):
        raise BetaError('native receipt missing JUnit digest')
    versions = receipt.get('embedded_version', {})
    if versions.get('file') != payload['version'] or versions.get('product') != payload['version']:
        raise BetaError('embedded file/product version mismatch')
    if receipt.get('version_command') != payload['version']:
        raise BetaError('executed version differs from resource/provenance')
    if receipt.get('host', {}).get('system') != 'Windows':
        raise BetaError('native execution host is not Windows')
    if receipt.get('authenticode') not in ('NotSigned', 'Valid'):
        raise BetaError('invalid or unknown Authenticode status')


def desktop_blockers(receipt: dict | None, payload: dict) -> list[str]:
    if receipt is None:
        return ['desktop_human_receipt_missing']
    blockers = []
    exact = {'schema':'wtop.desktop-acceptance/1', 'result':'passed', 'version':payload['version'],
             'profile':'win-nt-x64', 'source_revision':payload['source']['revision'],
             'executable_sha256':payload['executable']['sha256'], 'automated_smoke':'passed'}
    if any(receipt.get(k) != v for k,v in exact.items()):
        blockers.append('desktop_receipt_identity_or_result_mismatch')
    if not supported_host(receipt.get('host', {})):
        blockers.append('ordinary_user_windows11_x64_evidence_missing')
    human = receipt.get('human_review', {})
    checks = human.get('checks', [])
    if (human.get('method') != 'interactive-attestation' or not isinstance(checks, list)
            or len(checks) != len(CHECKS) or any(not isinstance(x, dict) for x in checks)
            or {x.get('check') for x in checks} != set(CHECKS)
            or any(x.get('passed') is not True for x in checks)):
        blockers.append('complete_human_review_missing')
    terminal = receipt.get('terminal')
    if not isinstance(terminal, str) or not terminal.strip() or len(terminal) > 128:
        blockers.append('terminal_identity_missing')
    return blockers


def public_blockers(manifest: dict, desktop: dict | None, repository: dict) -> list[str]:
    # Old Beta 1 keeps its recorded human gate. Schema 2 records the user's
    # channel-aware policy: alpha/beta optional human review, stable mandatory.
    if manifest.get('schema') == 'wtop.beta/2':
        from release_metadata import metadata
        channel = metadata(manifest['version'])['channel']
        blockers = [] if channel in ('alpha', 'beta') else desktop_blockers(desktop, manifest['payload'])
        for profile in manifest.get('profiles', []):
            if profile.get('security_scan', {}).get('status') != 'clean':
                blockers.append('security_scan_not_clean:' + profile['payload']['profile'])
        if desktop is not None:
            blockers += desktop_blockers(desktop, manifest['payload'])
    else:
        blockers = desktop_blockers(desktop, manifest['payload'])
    if repository.get('full_name') != 'Julesc013/wtop' or repository.get('has_issues') is not True:
        blockers.append('live_public_issue_feedback_unavailable')
    if repository.get('private') is not False or repository.get('archived') is not False:
        blockers.append('repository_not_public_writable')
    return sorted(set(blockers))


def source_archive(root: Path, revision: str) -> bytes:
    tar = subprocess.run(['git','-c','core.autocrlf=false','-c','core.eol=lf','-C',str(root),'archive','--format=tar','--prefix=wtop/',revision],
                         check=True, capture_output=True).stdout
    if len(tar) > MAX_TOTAL:
        raise BetaError('source archive exceeds bound')
    return gzip.compress(tar, mtime=0)


def archive_tree(data: bytes) -> tuple[str, dict[str, bytes]]:
    """Reconstruct Git SHA-1 tree from archive members, without extracting paths."""
    entries = {}
    tree = {}
    total = 0
    def object_hash(kind, body):
        return hashlib.sha1(kind + b' ' + str(len(body)).encode() + b'\0' + body).digest()
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        for index, member in enumerate(archive):
            if index > 20000 or member.size > MAX_FILE:
                raise BetaError('excessive source archive')
            if member.isdir():
                continue
            if not member.isfile() and not member.issym():
                raise BetaError('unsupported source member type')
            parts = PurePosixPath(member.name).parts
            if not parts or parts[0] != 'wtop' or len(parts) < 2 or any(x in ('..','.') for x in parts):
                raise BetaError('unsafe source path')
            key = '/'.join(parts[1:])
            if key in entries:
                raise BetaError('duplicate source member')
            total += member.size
            if total > MAX_TOTAL:
                raise BetaError('expanded source exceeds bound')
            body = member.linkname.encode() if member.issym() else archive.extractfile(member).read()
            entries[key] = body
            node = tree
            for part in parts[1:-1]:
                node = node.setdefault(part, {})
                if not isinstance(node, dict):
                    raise BetaError('source file/directory collision')
            name = parts[-1]
            if name in node:
                raise BetaError('source member collision')
            node[name] = ('120000' if member.issym() else ('100755' if member.mode & 0o111 else '100644'),
                          object_hash(b'blob', body))
    def digest(node):
        output = bytearray()
        for name,value in sorted(node.items(), key=lambda pair:(pair[0] + ('/' if isinstance(pair[1],dict) else '')).encode()):
            mode, sha = ('40000', digest(value)) if isinstance(value,dict) else value
            output += mode.encode() + b' ' + name.encode() + b'\0' + sha
        return object_hash(b'tree', bytes(output))
    return digest(tree).hex(), entries


def validate_payload(payload: dict) -> None:
    if not VERSION.fullmatch(payload['version']) or payload['profile'] != 'win-nt-x64':
        raise BetaError('public beta assembler accepts only declared x64 beta versions')
    source = payload['source']
    if source.get('dirty') is not False or not GIT.fullmatch(source.get('revision','')) or not GIT.fullmatch(source.get('tree','')):
        raise BetaError('clean exact source commit/tree required')
    if not re.fullmatch(r'\d+\.\d+\.\d+\.\d+', str(payload['build'].get('sdk',''))):
        raise BetaError('exact observed Windows SDK required')
    if not payload['pe']['resources']['present'] or payload['pe']['runtime_dlls']:
        raise BetaError('resource missing or external runtime floor unqualified')


def assemble(candidate: Path, receipt_path: Path, junit_path: Path, notes: Path, output: Path,
             root: Path = ROOT) -> dict:
    original = verify_candidate(candidate)
    payload = original['payload']
    validate_payload(payload)
    source = payload['source']
    native = load_json(receipt_path)
    validate_native(native, payload)
    tests = native_tests(junit_path)
    if native['tests'] != tests:
        raise BetaError('JUnit bytes differ from the native receipt')
    current = subprocess.run(['git','-C',str(root),'rev-parse','HEAD'],check=True,capture_output=True,text=True).stdout.strip()
    if current != source['revision']:
        raise BetaError('source archive checkout differs from built payload')
    data = source_archive(root, current)
    tree, entries = archive_tree(data)
    if tree != source['tree'] or entries.get('VERSION',b'').decode().strip() != payload['version']:
        raise BetaError('corresponding source tree/version mismatch')
    if entries.get('LICENSE') != checked_file(candidate/'LICENSE'):
        raise BetaError('license differs from corresponding source')
    for required in ('CMakeLists.txt','source/ntop.c','tools/beta_release.py','source/version.rc.in'):
        if required not in entries:
            raise BetaError('corresponding source incomplete')
    if output.exists() and any(output.iterdir()):
        raise BetaError('beta output must be empty; no artifact is overwritten')
    output.mkdir(parents=True, exist_ok=True)
    portable = next(item['name'] for item in original['artifacts'] if item['kind']=='portable-zip')
    version=payload['version']
    assets={f'wtop-{version}-win-nt-x64.exe':checked_file(candidate/'wtop.exe'),
            portable:checked_file(candidate/portable),
            f'wtop-{version}-source.tar.gz':data,
            'payload.json':json_bytes(payload), 'native-receipt.json':json_bytes(native),
            'native-tests.xml':checked_file(junit_path,16*1024*1024),
            'LICENSE':checked_file(candidate/'LICENSE'), 'RELEASE-NOTES.md':checked_file(notes,1024*1024)}
    manifest={'schema':'wtop.beta/1','product':'wtop','version':version,'tag':'v'+version,
              'payload':payload,'signing':native['authenticode'],
              'channel':'prerelease','stable_supported':False,
              'assets':[{'name':name,'size':len(content),'sha256':sha256(content)} for name,content in sorted(assets.items())],
              'public_requirements':['exact_native_tests','ordinary_user_windows11_x64','human_tui_review','live_public_feedback'],
              'scope':'x64 portable beta; no MSI, WinGet, ARM64 or historical support'}
    assets['beta-manifest.json']=json_bytes(manifest)
    for name,content in assets.items(): (output/name).write_bytes(content)
    (output/'SHA256SUMS').write_bytes(''.join(f'{sha256(value)}  {name}\n' for name,value in sorted(assets.items())).encode('ascii'))
    verify(output)
    return manifest


def verify(directory: Path) -> dict:
    manifest=load_json(directory/'beta-manifest.json')
    if manifest.get('schema')!='wtop.beta/1' or manifest.get('channel')!='prerelease' or manifest.get('stable_supported') is not False:
        raise BetaError('not a declared beta manifest')
    version=manifest.get('version','');payload=manifest['payload']
    validate_payload(payload)
    if not VERSION.fullmatch(version) or manifest.get('tag')!='v'+version or payload['version']!=version or payload['profile']!='win-nt-x64':
        raise BetaError('beta version/profile mismatch')
    expected={f'wtop-{version}-win-nt-x64.exe',f'wtop-{version}-win-nt-x64-portable.zip',
              f'wtop-{version}-source.tar.gz','payload.json','native-receipt.json','native-tests.xml','LICENSE','RELEASE-NOTES.md'}
    listed=manifest.get('assets',[])
    if len(listed)!=len(expected) or {x.get('name') for x in listed}!=expected:
        raise BetaError('unexpected/unsafe/duplicate beta asset inventory')
    data={}
    for item in listed:
        body=checked_file(directory/item['name'])
        if len(body)!=item['size'] or sha256(body)!=item['sha256']:
            raise BetaError('asset identity mismatch: '+item['name'])
        data[item['name']]=body
    hashes={name:sha256(body) for name,body in data.items()}
    hashes['beta-manifest.json']=sha256(checked_file(directory/'beta-manifest.json'))
    expected_sums=''.join(f'{sha}  {name}\n' for name,sha in sorted(hashes.items()))
    if checked_file(directory/'SHA256SUMS').decode('ascii')!=expected_sums:
        raise BetaError('checksum inventory mismatch')
    if json.loads(data['payload.json'])!=payload:
        raise BetaError('payload provenance mismatch')
    exe=data[f'wtop-{version}-win-nt-x64.exe']
    if sha256(exe)!=payload['executable']['sha256'] or len(exe)!=payload['executable']['size'] or inspect_bytes(exe)!=payload['pe']:
        raise BetaError('PE/provenance mismatch')
    if payload['source']['dirty'] is not False or payload['pe']['architecture']!='x64' or payload['pe']['subsystem']!=3:
        raise BetaError('unclean source or non-console/non-x64 image')
    with zipfile.ZipFile(io.BytesIO(data[f'wtop-{version}-win-nt-x64-portable.zip'])) as archive:
        names=archive.namelist()
        if len(names)!=5 or set(names)!={'wtop.exe','LICENSE','payload.json','PORTABLE.txt','wtop.conf.example'}:
            raise BetaError('unexpected portable members')
        if any(x.file_size>MAX_FILE for x in archive.infolist()):raise BetaError('oversized ZIP member')
        if archive.read('wtop.exe')!=exe or archive.read('LICENSE')!=data['LICENSE'] or json.loads(archive.read('payload.json'))!=payload:
            raise BetaError('portable payload differs')
    tree,entries=archive_tree(data[f'wtop-{version}-source.tar.gz'])
    if tree!=payload['source']['tree'] or entries.get('VERSION',b'').decode().strip()!=version or entries.get('LICENSE')!=data['LICENSE']:
        raise BetaError('corresponding source does not match')
    receipt=load_json(directory/'native-receipt.json');validate_native(receipt,payload)
    required = required_tests(version)
    if receipt['tests']!=native_tests(directory/'native-tests.xml', required) or manifest['signing']!=receipt['authenticode']:
        raise BetaError('test or signing receipt mismatch')
    return manifest


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='command',required=True)
    p=commands.add_parser('assemble')
    for name in ('candidate','native','junit','notes','output'):p.add_argument('--'+name,type=Path,required=True)
    p=commands.add_parser('verify');p.add_argument('directory',type=Path)
    p=commands.add_parser('gate');p.add_argument('directory',type=Path);p.add_argument('--desktop',type=Path);p.add_argument('--repository',type=Path,required=True)
    try:
        args=parser.parse_args(argv)
        if args.command=='assemble':manifest=assemble(args.candidate,args.native,args.junit,args.notes,args.output)
        else:manifest=verify(args.directory)
        if args.command=='gate':
            blockers=public_blockers(manifest,load_json(args.desktop) if args.desktop else None,load_json(args.repository))
            print(json.dumps({'public_eligible':not blockers,'blockers':blockers},indent=2))
            return 2 if blockers else 0
        print(json.dumps({'verified':True,'version':manifest['version'],'source':manifest['payload']['source']['revision'],
                          'public_qualification':'separate desktop/human/feedback gate required'},indent=2))
        return 0
    except (OSError,ValueError,KeyError,TypeError,subprocess.SubprocessError,ET.ParseError,tarfile.TarError,zipfile.BadZipFile) as error:
        print('Beta validation failed: '+str(error),file=sys.stderr);return 2

if __name__=='__main__':raise SystemExit(main())
