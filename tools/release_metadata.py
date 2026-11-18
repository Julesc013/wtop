#!/usr/bin/env python3
"""One explicit, bounded version mapping for release names and native carriers.

The full semantic version remains the public identity. Numeric Windows versions
order channels within a patch; MSI uses a separate, documented packing because
its third field is only 16 bits. Values outside that packing are rejected, never
truncated. This defines identity only, not installer upgrade qualification.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re

SEMVER = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-(dev|alpha|beta|rc)\.([1-9][0-9]*))?\Z')
NATIVE_BASE = {'dev': 0, 'alpha': 10000, 'beta': 20000, 'rc': 30000}
MSI_BASE = {'dev': 0, 'alpha': 200, 'beta': 400, 'rc': 600}
PROFILES = {'win-nt-x86': ('x86', '32-bit'), 'win-nt-x64': ('x64', '64-bit'),
            'win-nt-arm64': ('ARM64', '64-bit')}


def metadata(version: str) -> dict:
    match = SEMVER.fullmatch(version)
    if not match:
        raise ValueError('version must be unambiguous SemVer with a supported channel')
    major, minor, patch = map(int, match.group(1, 2, 3))
    channel = match[4] or 'stable'
    ordinal = int(match[5] or 0)
    if any(value > 65535 for value in (major, minor, patch)) or ordinal > 9999:
        raise ValueError('version exceeds the native resource field ledger')
    native_build = 65535 if channel == 'stable' else NATIVE_BASE[channel] + ordinal
    suffix = '' if channel == 'stable' else ' ' + {'dev':'Development', 'alpha':'Alpha', 'beta':'Beta', 'rc':'Release Candidate'}[channel] + ' ' + str(ordinal)
    # MSI supports major/minor <=255 and build <=65535. Reserving 1000 build
    # values per patch makes beta.1 < beta.2 < stable < next patch for all
    # represented versions. Larger values require a future explicit ledger.
    msi = None
    if major <= 255 and minor <= 255 and patch <= 64 and ordinal <= 199:
        build = patch * 1000 + (999 if channel == 'stable' else MSI_BASE[channel] + ordinal)
        msi = f'{major}.{minor}.{build}'
    return {'schema': 'wtop.version/1', 'version': version, 'tag': 'v' + version,
            'channel': channel, 'ordinal': ordinal,
            'title': f'Wtop {major}.{minor}.{patch}' + suffix,
            'native_version': [major, minor, patch, native_build],
            'msi_version': msi, 'msi_qualified': False,
            'prerelease': channel != 'stable', 'make_latest': False}


def msi_version(version: str) -> str:
    result = metadata(version)['msi_version']
    if result is None:
        raise ValueError('semantic version is outside the explicit MSI ledger (major/minor <=255, patch <=64, ordinal <=199)')
    return result


def asset_label(version: str, profile: str, kind: str) -> str:
    architecture, width = PROFILES[profile]
    return f"{metadata(version)['title']} for Windows NT ({architecture}, {width}) {kind}"


def cmake_text(value: dict) -> str:
    lines = ['# Generated from VERSION by tools/release_metadata.py; do not edit.']
    for key, number in zip(('MAJOR', 'MINOR', 'PATCH', 'BUILD'), value['native_version']):
        lines.append(f'set(WTOP_VERSION_{key} "{number}")')
    lines.append('set(WTOP_VERSION_FLAGS "' + ('VS_FF_PRERELEASE' if value['prerelease'] else '0') + '")')
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--version'); source.add_argument('--version-file', type=Path)
    parser.add_argument('--cmake', type=Path)
    args = parser.parse_args(argv)
    value = metadata(args.version if args.version is not None else args.version_file.read_text(encoding='utf-8').strip())
    if args.cmake:
        args.cmake.parent.mkdir(parents=True, exist_ok=True)
        args.cmake.write_text(cmake_text(value), encoding='ascii', newline='\n')
    else:
        print(json.dumps(value, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
