# Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
<#
.SYNOPSIS
    Several benchmark sessions in a row, with the CSV medians of each, into one report.

.DESCRIPTION
    Each run is "mode:tag" (screen:s1, world:w1). A tag that starts with "warm" is run and not summarized: the first
    launch after a build compiles shaders in the background and its numbers are not the code's. Runs are always made
    with -Trace 0 and -Csv 1; a trace costs frame time and is a separate run (bench_launch.ps1 -Trace 1).

.EXAMPLE
    pwsh -File Tools/Bench/bench_series.ps1 -Runs world:warm,world:w1,world:w2,screen:s1,screen:s2 -Report series1
#>
param(
    [string[]]$Runs = @('world:warm', 'world:w1', 'world:w2', 'screen:s1', 'screen:s2'),
    [string]$Report = 'series',
    [string]$Columns = 'FrameTime,GameThreadTime,RenderThreadTime,RHIThreadTime,GPUTime,RHI/DrawCalls',
    [string]$Project = $env:DREAMGUI_TEST_PROJECT,
    [string]$Engine = $env:DREAMGUI_ENGINE,
    [switch]$Game
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
if (-not $Project) { throw 'No host project: pass -Project or set DREAMGUI_TEST_PROJECT.' }
$ProjectDir = Split-Path -Parent $Project
$Out = Join-Path $ProjectDir 'Saved\DreamGUIBench'
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$ReportPath = Join-Path $Out "$Report.txt"
$CsvDir = Join-Path $ProjectDir 'Saved\Profiling\CSV'

"series $Report started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') game=$([bool]$Game)" | Out-File -LiteralPath $ReportPath -Encoding utf8
foreach ($Run in $Runs) {
    $Mode, $Tag = $Run.Split(':')
    $Launch = Join-Path $Out "$($Tag)_launch.txt"
    $LaunchArgs = @{ Mode = $Mode; Tag = $Tag; Trace = 0; Csv = 1; Cursor = 'away'; Project = $Project; Engine = $Engine; Game = $Game }
    & (Join-Path $PSScriptRoot 'bench_launch.ps1') @LaunchArgs *> $Launch
    "== $Mode $Tag" | Out-File -Append -LiteralPath $ReportPath -Encoding utf8
    Get-Content -LiteralPath $Launch | Where-Object { $_ -match ' (idle|anim A):' } | Out-File -Append -LiteralPath $ReportPath -Encoding utf8
    if (-not $Tag.StartsWith('warm')) {
        & python (Join-Path $PSScriptRoot 'csv_summary.py') "--dir=$CsvDir" "--columns=$Columns" *>&1 | Out-File -Append -LiteralPath $ReportPath -Encoding utf8
    }
}
"series $Report done $(Get-Date -Format 'HH:mm:ss')" | Out-File -Append -LiteralPath $ReportPath -Encoding utf8
Get-Content -LiteralPath $ReportPath
