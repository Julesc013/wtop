#!/usr/bin/env python3
"""Record an actual bounded Defender scan of final portable candidate bytes.

Unavailable protection is reported as unavailable and blocks public promotion.
This script never changes Defender configuration, exclusions or execution policy.
"""
from __future__ import annotations
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from beta_release import BetaError
from package_candidate import verify
from release_bundle import validate_scan

SCAN_SCRIPT = r"""
$ErrorActionPreference='Stop'
$env:PSModulePath=$PSHOME+'\Modules'
if (-not (Get-Command Get-MpComputerStatus -ErrorAction SilentlyContinue)) {
    @{status='unavailable';reason='Microsoft Defender module unavailable'} | ConvertTo-Json -Compress
    exit 0
}
$before=Get-MpComputerStatus
if (-not $before.AMServiceEnabled -or -not $before.AntivirusEnabled) {
    @{status='unavailable';reason='Microsoft Defender service or antivirus disabled on this runner'} | ConvertTo-Json -Compress
    exit 0
}
$root=[IO.Path]::GetFullPath($env:WTOP_SCAN_ROOT)
$platform=Join-Path $env:ProgramData 'Microsoft\Windows Defender\Platform'
$candidates=@()
if (Test-Path -LiteralPath $platform) {
    $candidates=@(Get-ChildItem -LiteralPath $platform -Directory |
        Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'MpCmdRun.exe' })
}
$candidates+=Join-Path $env:ProgramFiles 'Windows Defender\MpCmdRun.exe'
$command=$candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $command) {
    @{status='unavailable';reason='Microsoft Defender scan command unavailable'} | ConvertTo-Json -Compress
    exit 0
}
$started=[DateTime]::UtcNow
# This option applies to this custom scan only: exclusions are ignored,
# archives are scanned, and no remediation is performed. It never disables
# Defender protection or changes a preference. Microsoft documents zero as
# no threat or successfully remediated; remediation is disabled for this scan.
$scanOutput=& $command -Scan -ScanType 3 -File $root -DisableRemediation 2>&1
$scanExit=$LASTEXITCODE
$after=Get-MpComputerStatus
$status=if ($scanExit -eq 0) {'clean'} else {'failed'}
@{status=$status;engine='Microsoft Defender';signature_version=$after.AntivirusSignatureVersion;
  engine_version=$after.AMEngineVersion;detected_threats=$(if ($scanExit -eq 0) {0} else {$null});
  scan_exit_code=$scanExit;method='custom-no-remediation-ignore-exclusions';
  scan_started_at=$started.ToString('o');scan_completed_at=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Compress
"""


def scan(candidate):
    environment = os.environ.copy(); environment['WTOP_SCAN_ROOT'] = str(candidate)
    result = subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',SCAN_SCRIPT],
                            env=environment,capture_output=True,text=True,check=True,timeout=600)
    return json.loads(result.stdout)


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(argv)
    try:
        if os.name!='nt':raise BetaError('scan receipt requires actual Windows execution')
        candidate=args.candidate.resolve();payload=verify(candidate)['payload']
        observation=scan(candidate)
        receipt={'schema':'wtop.security-scan/1','version':payload['version'],'profile':payload['profile'],
                 'source_revision':payload['source']['revision'],'executable_sha256':payload['executable']['sha256'],
                 'recorded_at':dt.datetime.now(dt.timezone.utc).isoformat(), **observation}
        # Preserve an adverse observation before refusing the package. A failed
        # invocation is not rewritten as unavailable or a clean result.
        with args.output.open('x',encoding='utf-8') as stream:
            json.dump(receipt,stream,indent=2);stream.write('\n')
        validate_scan(receipt,payload)
        verify(candidate)  # A scanner may quarantine/change a file: fail closed.
        return 0
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error:
        print('Security scan failed: '+str(error),file=sys.stderr);return 2


if __name__=='__main__':raise SystemExit(main())
