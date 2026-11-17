# Explicit download, digest verification and optional MSI invocation.
# SPDX-License-Identifier: GPL-3.0-only
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][uri]$Uri,
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$Sha256,
    [Parameter(Mandatory=$true)][string]$Destination,
    [switch]$Install,
    [ValidateSet('User','Machine')][string]$Scope = 'User'
)
$ErrorActionPreference = 'Stop'
if ($Uri.Scheme -ne 'https' -or $Uri.Host -ne 'github.com' -or
    $Uri.UserInfo -or $Uri.Query -or $Uri.Fragment -or
    $Uri.AbsolutePath -notmatch '^/[Jj]ulesc013/wtop/releases/download/[^/]+/wtop-[^/]+\.(msi|zip|exe)$' -or
    $Uri.AbsolutePath -match '/latest/') {
    throw 'Use an exact HTTPS GitHub release tag and Wtop asset URL; latest links and other origins are refused.'
}
$target = [IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $target) { throw 'Destination already exists; no downloaded file is overwritten.' }
$parent = [IO.Path]::GetDirectoryName($target)
if (-not (Test-Path -LiteralPath $parent -PathType Container)) { throw 'Choose an existing staging directory.' }
if ($Install -and ([IO.Path]::GetExtension($Uri.AbsolutePath) -ne '.msi' -or [IO.Path]::GetExtension($target) -ne '.msi')) {
    throw 'Automatic installation is restricted to the verified MSI. EXE/ZIP downloads are not executed.'
}
$temporary = Join-Path $parent ('.wtop-download-' + [guid]::NewGuid().ToString('N') + '.tmp')
try {
    Invoke-WebRequest -Uri $Uri -OutFile $temporary -TimeoutSec 90 -UseBasicParsing
    $actual = (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash
    if ($actual -ne $Sha256) { throw 'SHA256 mismatch; the downloaded artifact will not be used.' }
    [IO.File]::Move($temporary, $target)
    Write-Output "Verified artifact: $target (SHA256 $($actual.ToLowerInvariant()))"
    if ($Install) {
        $perUser = if ($Scope -eq 'User') { '1' } else { '""' }
        # Native Windows Installer remains the owner. This script does not edit
        # PATH, registration, application data or installer-owned payloads.
        $arguments = '/i "' + $target + '" ALLUSERS=2 MSIINSTALLPERUSER=' + $perUser + ' /norestart'
        $process = Start-Process -FilePath 'msiexec.exe' -ArgumentList $arguments -Wait -PassThru
        if ($process.ExitCode -ne 0 -and $process.ExitCode -ne 3010) {
            throw "Windows Installer returned $($process.ExitCode)."
        }
        if ($process.ExitCode -eq 3010) { Write-Output 'Windows Installer requests a restart; none was initiated.' }
    }
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
}
