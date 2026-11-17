#!/usr/bin/env python3
"""Bind actual Windows test results and version resources to an exact candidate."""
from __future__ import annotations
import argparse
import ctypes
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from beta_release import checked_file, native_tests, sha256, validate_native, BetaError
from package_candidate import verify

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate',type=Path);parser.add_argument('--junit',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    try:
        if os.name!='nt':raise BetaError('native receipt requires actual Windows execution')
        candidate=args.candidate.resolve();payload=verify(candidate)['payload'];exe=candidate/'wtop.exe'
        # Keep the path out of executable PowerShell text; environment is data.
        environment=os.environ.copy();environment['WTOP_RECEIPT_EXE']=str(exe)
        command="$p=$env:WTOP_RECEIPT_EXE; $v=[System.Diagnostics.FileVersionInfo]::GetVersionInfo($p); @{ file=$v.FileVersion; product=$v.ProductVersion; authenticode=(Get-AuthenticodeSignature -LiteralPath $p).Status.ToString() } | ConvertTo-Json -Compress"
        result=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',command],capture_output=True,text=True,check=True,timeout=30,env=environment)
        versions=json.loads(result.stdout)
        result=subprocess.run([str(exe),'--no-config','--version'],capture_output=True,text=True,check=True,timeout=10)
        if payload['version'] not in result.stdout.strip().split():raise BetaError('version command did not report exact version')
        osversion=sys.getwindowsversion()
        receipt={'schema':'wtop.beta-native/1','result':'passed','recorded_at':dt.datetime.now(dt.timezone.utc).isoformat(),
                 'version':payload['version'],'profile':payload['profile'],'source_revision':payload['source']['revision'],
                 'executable_sha256':sha256(checked_file(exe)),'version_command':payload['version'],
                 'embedded_version':{'file':versions['file'],'product':versions['product']},'authenticode':versions['authenticode'],
                 'host':{'system':'Windows','major':osversion.major,'minor':osversion.minor,'build':osversion.build,
                         'product_type':osversion.product_type,'elevated':bool(ctypes.windll.shell32.IsUserAnAdmin())},
                 'tests':native_tests(args.junit),'human_visual_acceptance':False}
        validate_native(receipt,payload)
        with args.output.open('x',encoding='utf-8') as stream:json.dump(receipt,stream,indent=2);stream.write('\n')
        return 0
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error:
        print('Native receipt failed: '+str(error),file=sys.stderr);return 2
if __name__=='__main__':raise SystemExit(main())
