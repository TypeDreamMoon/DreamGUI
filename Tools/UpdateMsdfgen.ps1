#Requires -Version 5.1
<#
.SYNOPSIS
    Regenerates ThirdParty/msdfgen-single-file from the msdfgen submodule.

.DESCRIPTION
    Upstream does not commit its single-file distribution: all-in-one/generate.py concatenates core/
    and ext/ into msdfgen.h + msdfgen.cpp, and all-in-one/.gitignore ignores both. This repository
    commits the pair anyway, so that a plain clone or a zip download builds without Python. This
    script runs the upstream generator inside the submodule and copies the result over, and reports
    what changed. ThirdParty/README.md explains the layout and the pin.

    The submodule has to be checked out: git submodule update --init ThirdParty/msdfgen. Without
    -Ref the pinned revision is used, which is the one the committed pair was generated from.

.PARAMETER Ref
    Upstream branch, tag or commit to check out in the submodule before generating. A ref other than
    the pinned one leaves the submodule moved, so the pin has to be committed with the pair.

.PARAMETER Python
    Python 3 interpreter to run generate.py with. Default: python3/python on PATH, else the engine's
    bundled Python3. A candidate that cannot report a Python 3 version is skipped, so the Windows
    Store's python.exe stub does not count as an interpreter.

.PARAMETER Check
    Generate as usual -- inside the submodule, where upstream's .gitignore keeps the pair out of the
    submodule's own history -- and compare it with the committed copy, writing nothing into the
    plugin. Line endings are normalised before comparing, since generate.py writes CRLF on Windows
    and LF elsewhere. Exits 1 when the committed pair is stale.

.PARAMETER Force
    Write the pair even when it is byte-identical to what is committed.

.EXAMPLE
    pwsh -NoProfile -File Tools\UpdateMsdfgen.ps1 -Check
.EXAMPLE
    pwsh -NoProfile -File Tools\UpdateMsdfgen.ps1
#>
[CmdletBinding()]
param(
    [string]$Ref,
    [string]$Python,
    [switch]$Check,
    [switch]$Force
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$PluginRoot = Split-Path -Parent $PSScriptRoot
$SubmoduleDir = Join-Path $PluginRoot 'ThirdParty/msdfgen'
$Generator = Join-Path $SubmoduleDir 'all-in-one/generate.py'
$OutputDir = Join-Path $PluginRoot 'ThirdParty/msdfgen-single-file'
$OutputNames = @('msdfgen.h', 'msdfgen.cpp')

if ($Check -and $Ref) {
    throw '-Check and -Ref are mutually exclusive: -Check reports on the revision that is checked out.'
}

if (-not (Test-Path $Generator)) {
    throw "The msdfgen submodule is not checked out ($Generator is missing).`nRun: git submodule update --init ThirdParty/msdfgen"
}

function Test-Python3 {
    param([string]$Exe)

    try {
        $Version = & $Exe --version 2>&1
    }
    catch {
        return $false
    }
    return ($LASTEXITCODE -eq 0 -and "$Version" -match 'Python 3\.')
}

function Resolve-Python {
    param([string]$Requested)

    if ($Requested) {
        if (-not (Test-Python3 -Exe $Requested)) {
            throw "Not a usable Python 3 interpreter: $Requested"
        }
        return $Requested
    }

    $Candidates = @()
    foreach ($Name in @('python3', 'python')) {
        $Found = Get-Command $Name -ErrorAction SilentlyContinue
        if ($Found) {
            $Candidates += $Found.Source
        }
    }

    # An installed engine carries its own Python3 (it is what the Python Editor Script Plugin uses).
    foreach ($Key in @('HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8', 'HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\5.8')) {
        if (Test-Path $Key) {
            $EngineRoot = (Get-ItemProperty $Key -ErrorAction SilentlyContinue).InstalledDirectory
            if ($EngineRoot) {
                $Candidates += (Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/Python3/Win64/python.exe')
            }
        }
    }
    $Candidates += 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe'

    foreach ($Candidate in $Candidates) {
        if ((Test-Path $Candidate) -and (Test-Python3 -Exe $Candidate)) {
            return $Candidate
        }
    }

    throw 'No Python 3 interpreter found. Pass one with -Python.'
}

function Get-NormalizedHash {
    param([string]$Path)

    # generate.py writes in text mode, so the pair comes out CRLF on Windows and LF everywhere else.
    # Line endings are the one difference that says nothing about the revision behind the pair, so
    # they are normalised away before comparing -- the same way git's core.autocrlf stores them.
    $Encoding = [System.Text.Encoding]::GetEncoding(28591)
    $Text = $Encoding.GetString([System.IO.File]::ReadAllBytes($Path)) -replace "`r`n", "`n"
    $Bytes = $Encoding.GetBytes($Text)
    $Sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $Hash = $Sha.ComputeHash($Bytes)
    }
    finally {
        $Sha.Dispose()
    }
    return [pscustomobject]@{
        Hash  = ([System.BitConverter]::ToString($Hash) -replace '-', '')
        Bytes = $Bytes.Length
    }
}

$Python = Resolve-Python -Requested $Python
Write-Host "msdfgen submodule : $((git -C $SubmoduleDir log -1 --pretty='%h %ad %s' --date=short))"

if ($Ref) {
    git -C $SubmoduleDir checkout --quiet $Ref
    if ($LASTEXITCODE -ne 0) {
        throw "git checkout $Ref failed in the submodule."
    }
    Write-Host "checked out       : $((git -C $SubmoduleDir log -1 --pretty='%h %ad %s' --date=short))"
}

# generate.py resolves its inputs relative to itself and writes msdfgen.h + msdfgen.cpp next to it,
# inside the submodule, where upstream's .gitignore keeps them out of the submodule's own history.
& $Python $Generator
if ($LASTEXITCODE -ne 0) {
    throw "generate.py failed with exit code $LASTEXITCODE."
}

$GeneratedDir = Join-Path $SubmoduleDir 'all-in-one'
$Stale = @()
foreach ($Name in $OutputNames) {
    $Generated = Join-Path $GeneratedDir $Name
    if (-not (Test-Path $Generated)) {
        throw "generate.py did not produce $Name."
    }
    $Committed = Join-Path $OutputDir $Name
    $GeneratedInfo = Get-NormalizedHash -Path $Generated
    $CommittedInfo = if (Test-Path $Committed) { Get-NormalizedHash -Path $Committed } else { $null }
    $State = if (-not $CommittedInfo) { 'missing' } elseif ($CommittedInfo.Hash -eq $GeneratedInfo.Hash) { 'unchanged' } else { 'changed' }
    if ($State -ne 'unchanged') {
        $Stale += $Name
    }
    Write-Host ("{0,-12}: {1,-9} {2,8} bytes  sha256 {3}" -f $Name, $State, $GeneratedInfo.Bytes, $GeneratedInfo.Hash.Substring(0, 16))
}

if ($Check) {
    if ($Stale.Count -gt 0) {
        Write-Host "ThirdParty/msdfgen-single-file is stale ($($Stale -join ', ')). Regenerate without -Check." -ForegroundColor Yellow
        exit 1
    }
    Write-Host 'ThirdParty/msdfgen-single-file matches the submodule.'
    exit 0
}

if ($Stale.Count -eq 0 -and -not $Force) {
    Write-Host 'ThirdParty/msdfgen-single-file is already current; nothing written.'
    exit 0
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
foreach ($Name in $OutputNames) {
    Copy-Item (Join-Path $GeneratedDir $Name) (Join-Path $OutputDir $Name) -Force
}
Write-Host "Wrote $($OutputNames -join ', ') to ThirdParty/msdfgen-single-file."
Write-Host 'Commit the pair together with the submodule pin (ThirdParty/msdfgen) if it moved.'
