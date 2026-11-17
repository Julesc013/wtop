#!/usr/bin/env python3
"""Write a deliberately small, opt-in diagnostic report. Never upload it.

Only early build metadata is queried; no process collection, configuration read,
settings path discovery, command lines, environment or user identity is requested.
Unknown fields are discarded rather than recursively copied/redacted.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

BOOL_FIELDS=('tui','snapshot','query','inspect','log','gui','install','gpu','native_gui')
PROVIDERS={'command_line','services','per_core','account_name'}
METRICS={'cpu_percent','working_set_bytes','io_read_write_bytes_per_second','age_ms','owner'}
PROFILES={'win-nt-x86','win-nt-x64','win-nt-arm64'}


def safe_capabilities(value: dict) -> dict:
    if not isinstance(value,dict) or value.get('kind')!='build-capabilities' or value.get('runtime_probe') is not False:
        raise ValueError('expected collector-free build-capability metadata')
    version=value.get('version','');profile=value.get('profile','')
    if not isinstance(version,str) or not re.fullmatch(r'\d+\.\d+\.\d+(?:-(?:dev|beta|rc)\.[1-9]\d*)?',version) or profile not in PROFILES:
        raise ValueError('invalid product version/profile')
    result={'version':version,'profile':profile,'runtime_probe':False}
    for key in BOOL_FIELDS:
        if type(value.get(key)) is bool:result[key]=value[key]
    for key in ('schema','snapshot_schema','settings_schema'):
        if type(value.get(key)) is int and 0<value[key]<100:result[key]=value[key]
    for key,allowed in (('optional_providers',PROVIDERS),('metrics',METRICS)):
        items=value.get(key,[])
        if isinstance(items,list):result[key]=sorted({x for x in items if isinstance(x,str) and x in allowed})
    if value.get('encoding')=='utf-8':result['encoding']='utf-8'
    return result


def coarse_host() -> dict:
    if os.name!='nt':return {'family':'non-Windows; native executable not run'}
    version=sys.getwindowsversion()
    return {'family':'Windows','major':version.major,'minor':version.minor,'build':version.build,'product_type':version.product_type}


def create(exe: Path) -> dict:
    if os.name!='nt':raise ValueError('run this reporter on the actual Windows test host')
    exe=exe.resolve(strict=True)
    if not exe.is_file() or exe.stat().st_size>64*1024*1024:raise ValueError('invalid executable')
    before=hashlib.sha256(exe.read_bytes()).hexdigest()
    result=subprocess.run([str(exe),'--no-config','--capabilities','--format','json'],
                          stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=10,check=True)
    if len(result.stdout)>65536:raise ValueError('metadata exceeds diagnostic input bound')
    capabilities=safe_capabilities(json.loads(result.stdout))
    if hashlib.sha256(exe.read_bytes()).hexdigest()!=before:raise ValueError('executable changed')
    return {'schema':'wtop.support-report/1','executable_sha256':before,'host':coarse_host(),
            'build':capabilities,'runtime_health':'not_assessed',
            'privacy':'allowlisted build/host metadata only; review before manually sharing'}


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True,help='new JSON file, never overwritten')
    args=parser.parse_args(argv)
    try:
        report=create(args.exe)
        with args.output.open('x',encoding='utf-8') as stream:json.dump(report,stream,indent=2);stream.write('\n')
        print('Diagnostic report saved. Review it before manually sharing; nothing was uploaded.')
        return 0
    except (OSError,ValueError,subprocess.SubprocessError):
        # Never include native stderr, filesystem paths or arbitrary exception text.
        print('Report not written: host, executable, metadata or destination validation failed.',file=sys.stderr)
        return 2
if __name__=='__main__':raise SystemExit(main())
