# Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
<#
.SYNOPSIS
    The nightly run: the test host moved to a commit, built, and a list of presets run, with a summary of what turned
    red -- or green again -- since the run before.

.DESCRIPTION
    Meant to run unattended, on a machine that keeps a test host (Tools/TestHost) whose Plugins/DreamGUI is a git
    worktree of this repository. It:

      1. refuses to start while an editor of the host project is open (exit 2);
      2. fetches, and moves the host's plugin worktree to -Ref (checkout -f --detach, clean -fd -- never -x: the build
         products stay);
      3. builds the host's editor target in unity (Build.bat), unless -NoBuild;
      4. runs each preset through Invoke-DreamGUITests.ps1 -NoBuild;
      5. writes nightly-<stamp>.md beside the run's logs, and compares it with the newest summary before it.

    Exit codes follow Invoke-DreamGUITests.ps1: 0 green, 1 tests red, 2 something could not be run or judged.

    It does not register itself anywhere. To run it every night, a Windows scheduled task is one way (an example is in
    README.md); setting one up is the machine owner's call.

.EXAMPLE
    pwsh -File Tools/Tests/Invoke-DreamGUINightly.ps1 -Ref origin/main
    pwsh -File Tools/Tests/Invoke-DreamGUINightly.ps1 -Ref eng/round1 -NoFetch -Presets Quick
#>
[CmdletBinding()]
param(
    [string]$Ref = 'origin/main',
    [string]$Project = $env:DREAMGUI_TEST_PROJECT,
    [string]$Engine = $env:DREAMGUI_ENGINE,
    [string[]]$Presets = @('All', 'Validate', 'Exit', 'Perf'),
    # Default: <host>/Saved/DreamGUINightly.
    [string]$ReportRoot = '',
    # Compilers at once. 0 leaves it to UBT; a number is capped by the memory free now, at 1.5 GB a compiler, because an
    # explicit count skips UBT's own memory check.
    [int]$MaxParallel = 0,
    [switch]$NoFetch,
    [switch]$NoBuild
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
trap {
    Write-Host "!!  Unexpected error: $($_.Exception.Message)" -ForegroundColor Yellow
    exit 2
}

if (-not $Project) { throw 'No host project: pass -Project or set DREAMGUI_TEST_PROJECT.' }
if (-not $Engine) { throw 'No engine: pass -Engine or set DREAMGUI_ENGINE.' }
$HostDir = Split-Path -Parent $Project
$HostName = [IO.Path]::GetFileNameWithoutExtension($Project)
$PluginDir = Join-Path $HostDir 'Plugins\DreamGUI'
$Runner = Join-Path $PluginDir 'Tools\Tests\Invoke-DreamGUITests.ps1'
if (-not $ReportRoot) { $ReportRoot = Join-Path $HostDir 'Saved\DreamGUINightly' }
$Stamp = Get-Date -Format 'yyyyMMdd-HHmm'
$RunDir = Join-Path $ReportRoot $Stamp
New-Item -ItemType Directory -Force -Path $RunDir | Out-Null
$Summary = Join-Path $ReportRoot "nightly-$Stamp.md"
$Lines = [System.Collections.Generic.List[string]]::new()
function Note([string]$Line) { $Lines.Add($Line); Write-Host $Line }
function Finish([int]$Code) {
    $Lines | Out-File -LiteralPath $Summary -Encoding utf8
    Write-Host "summary: $Summary"
    exit $Code
}

Note "# DreamGUI nightly $Stamp"
Note ''

# 1. Nobody else's editor on the host.
if (Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" | Where-Object { $_.CommandLine -like "*$HostName.uproject*" }) {
    Note "- An editor of $HostName is open; nothing was run."
    Finish 2
}

# 2. The commit.
if (-not $NoFetch) {
    & git -C $PluginDir fetch --quiet origin
    if ($LASTEXITCODE -ne 0) { Note '- git fetch failed.'; Finish 2 }
}
$Sha = (& git -C $PluginDir rev-parse --verify "$Ref^{commit}").Trim()
if ($LASTEXITCODE -ne 0 -or -not $Sha) { Note "- $Ref does not name a commit."; Finish 2 }
& git -C $PluginDir checkout -f --quiet --detach $Sha
if ($LASTEXITCODE -ne 0) { Note "- Could not check out $Sha in $PluginDir."; Finish 2 }
& git -C $PluginDir clean -fdq
$Subject = (& git -C $PluginDir log -1 --format=%s $Sha).Trim()
Note "- Commit: ``$($Sha.Substring(0, 8))`` $Subject ($Ref)"

# 3. The build.
if (-not $NoBuild) {
    $BuildBat = Join-Path $Engine 'Engine\Build\BatchFiles\Build.bat'
    # A still-valid makefile of the other unity mode is reused whatever the command line says; this run is unity.
    $Makefile = Join-Path $HostDir "Intermediate\Build\Win64\x64\$($HostName)Editor\Development\Makefile.bin"
    Remove-Item -LiteralPath $Makefile -Force -ErrorAction SilentlyContinue
    $BuildArgs = @("$($HostName)Editor", 'Win64', 'Development', "-Project=$Project", '-WaitMutex', '-NoHotReloadFromIDE',
        '-NoEngineChanges', '-DisableAdaptiveUnity')
    if ($MaxParallel -gt 0) {
        $FreeGB = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB
        $BuildArgs += "-MaxParallelActions=$([math]::Max(1, [math]::Min($MaxParallel, [math]::Floor($FreeGB / 1.5))))"
    }
    $BuildLog = Join-Path $RunDir 'build.log'
    $Started = Get-Date
    & $BuildBat @BuildArgs *> $BuildLog
    $BuildCode = $LASTEXITCODE
    $Minutes = [math]::Round(((Get-Date) - $Started).TotalMinutes, 1)
    if ($BuildCode -ne 0) {
        Note "- Build: **failed** after $Minutes min (exit $BuildCode); see ``build.log``."
        Get-Content -LiteralPath $BuildLog | Where-Object { $_ -match 'error ' } | Select-Object -First 10 | ForEach-Object { Note "    $_" }
        Finish 2
    }
    Note "- Build: succeeded in $Minutes min."
}
Note ''

# 4. The presets.
$Red = [System.Collections.Generic.List[string]]::new()
$Worst = 0
Note '| Preset | Result |'
Note '|---|---|'
foreach ($Preset in $Presets) {
    $PresetLog = Join-Path $RunDir "$($Preset.ToLowerInvariant()).log"
    & pwsh -NoProfile -File $Runner -Preset $Preset -NoBuild -AllowSystemDrive -Engine $Engine -Project $Project *> $PresetLog
    $Code = $LASTEXITCODE
    $Worst = [math]::Max($Worst, $Code)
    $Result = (Select-String -LiteralPath $PresetLog -Pattern '^RESULT ' | Select-Object -Last 1)
    Note "| $Preset | $(if ($Result) { $Result.Line.Trim() } else { "no result (exit $Code)" }) |"
    foreach ($Fail in (Select-String -LiteralPath $PresetLog -Pattern '^\s+FAIL (\S+)')) {
        $Red.Add("$Preset $($Fail.Matches[0].Groups[1].Value)")
    }
}
Note ''

# 5. Against the night before.
$Previous = Get-ChildItem -LiteralPath $ReportRoot -Filter 'nightly-*.md' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -ne $Summary } | Sort-Object Name | Select-Object -Last 1
$WasRed = @()
if ($Previous) {
    $WasRed = @(Get-Content -LiteralPath $Previous.FullName | Where-Object { $_ -match '^- red: ' } | ForEach-Object { $_.Substring(7) })
}
$NewRed = @($Red | Where-Object { $WasRed -notcontains $_ })
$Fixed = @($WasRed | Where-Object { $Red -notcontains $_ })
Note "## Red ($($Red.Count))"
foreach ($Test in $Red) { Note "- red: $Test" }
Note ''
Note "## Since $(if ($Previous) { $Previous.Name } else { 'nothing: this is the first' })"
Note "- New red: $(if ($NewRed.Count) { $NewRed -join ', ' } else { 'none' })"
Note "- Green again: $(if ($Fixed.Count) { $Fixed -join ', ' } else { 'none' })"
Finish $Worst
