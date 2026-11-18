#!/usr/bin/env python3
"""Bind actual Windows test results and version resources to an exact candidate."""
from __future__ import annotations
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from beta_release import checked_file, native_tests, sha256, BetaError
from package_candidate import verify
from desktop_acceptance import host_identity
from release_bundle import validate_execution

# Windows PowerShell launched via Python under pwsh inherits PS7 module paths.
# Resolve built-in Security/Utility only from this child host's own PSHOME.
# This changes no registry, caller environment, execution policy or trust setting.
METADATA_SCRIPT = (
    "$ErrorActionPreference='Stop'; $env:PSModulePath=$PSHOME+'\\Modules'; "
    "$p=$env:WTOP_RECEIPT_EXE; $v=[System.Diagnostics.FileVersionInfo]::GetVersionInfo($p); "
    "@{ file=$v.FileVersion; product=$v.ProductVersion; "
    "numeric=@($v.FileMajorPart,$v.FileMinorPart,$v.FileBuildPart,$v.FilePrivatePart); "
    "authenticode=(Get-AuthenticodeSignature -LiteralPath $p).Status.ToString() } "
    "| ConvertTo-Json -Compress"
)

def read_windows_metadata(exe):
    environment=os.environ.copy();environment['WTOP_RECEIPT_EXE']=str(exe)
    result=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',METADATA_SCRIPT],
                          capture_output=True,text=True,check=True,timeout=30,env=environment)
    return json.loads(result.stdout)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate',type=Path);parser.add_argument('--junit',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    try:
        if os.name!='nt':raise BetaError('native receipt requires actual Windows execution')
        candidate=args.candidate.resolve();payload=verify(candidate)['payload'];exe=candidate/'wtop.exe'
        # The filename remains environment data, never PowerShell source.
        versions=read_windows_metadata(exe)
        result=subprocess.run([str(exe),'--no-config','--version'],capture_output=True,text=True,check=True,timeout=10)
        if payload['version'] not in result.stdout.strip().split():raise BetaError('version command did not report exact version')
        receipt={'schema':'wtop.beta-native/1','result':'passed','recorded_at':dt.datetime.now(dt.timezone.utc).isoformat(),
                 'version':payload['version'],'profile':payload['profile'],'source_revision':payload['source']['revision'],
                 'executable_sha256':sha256(checked_file(exe)),'version_command':payload['version'],
                 'embedded_version':{'file':versions['file'],'product':versions['product']},
                 'embedded_numeric_version':versions['numeric'],'authenticode':versions['authenticode'],
                 'host':host_identity(),
                 'tests':native_tests(args.junit),'human_visual_acceptance':False}
        validate_execution(receipt,payload)
        with args.output.open('x',encoding='utf-8') as stream:json.dump(receipt,stream,indent=2);stream.write('\n')
        return 0
    except subprocess.CalledProcessError as error:
        # Fixed metadata/version commands only: preserve the native error rather
        # than turning a failed acquisition into an unknown signing success.
        diagnostic = (error.stderr or error.stdout or 'no diagnostic output')[-4000:]
        print('Native receipt command failed: ' + diagnostic, file=sys.stderr)
        return 2
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error:
        print('Native receipt failed: '+str(error),file=sys.stderr);return 2
if __name__=='__main__':raise SystemExit(main())
