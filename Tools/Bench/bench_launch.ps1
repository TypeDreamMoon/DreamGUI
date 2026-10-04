# Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
<#
.SYNOPSIS
    One benchmark session: an editor (PIE) or a -game process on the test host, driven by bench_run.py.

.DESCRIPTION
    Starts UnrealEditor.exe on the host project with bench_run.py, waits for it to finish, and prints the [DreamPerf]
    lines it logged. Only ever stops the process it started, and refuses to start while an editor of the same project
    is running. See README.md for the two benchmark walls and what the numbers mean.

.EXAMPLE
    pwsh -File Tools/Bench/bench_launch.ps1 -Mode world -Tag w1 -Csv 1
    pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Tag s1 -Game -Csv 1
    pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Tag cov1 -Game -Csv 1 -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0'
#>
param(
    # The host project; DREAMGUI_TEST_PROJECT when not given.
    [string]$Project = $env:DREAMGUI_TEST_PROJECT,
    # The engine; DREAMGUI_ENGINE when not given.
    [string]$Engine = $env:DREAMGUI_ENGINE,
    [ValidateSet('screen', 'world')]
    [string]$Mode = 'screen',
    # A name for this run: the trace, the samples and the copied log are named after it.
    [string]$Tag = 'bench',
    # Where traces, samples and logs go. Default: <project>/Saved/DreamGUIBench.
    [string]$Out = '',
    # The map. Default: an empty map for the screen wall, the world wall's level for the world wall.
    [string]$Map = '',
    # The screen wall's widget class (screen mode).
    [string]$ScreenWidget = '/Game/UI/DW_Benchmark_Button_SW.DW_Benchmark_Button_SW_C',
    # A -game process on the editor binary instead of PIE: no editor UI in the frame.
    [switch]$Game,
    [int]$Trace = 0,
    [string]$TraceChannels = '',
    [int]$Csv = 0,
    # Console commands run at the start and end of the animated windows (';'-separated), e.g. 'DreamUI.Stats'.
    [string]$StatsCmds = '',
    # Console commands for an A/B run: side A measured first, then these, then side B (';'-separated).
    [string]$AbCmds = '',
    # Console commands for the whole session, both sides alike, run as the game starts (';'-separated), e.g.
    # 'r.DreamUI.RenderLayers 0' to measure an A/B with render layers off.
    [string]$SetupCmds = '',
    [double]$Warmup = 20,
    [double]$Window = 8,
    [int]$Rounds = 4,
    # Engine stat scopes as named events in a trace (-statnamedevents): more detail, more cost.
    [int]$NamedEvents = 0,
    # Sample the game thread with StackSampler.cs over the chosen phase (bench_run.py's SAMPLE_PHASE).
    [int]$Sample = 0,
    [ValidateSet('window', 'all', 'edges', 'ends')]
    [string]$SamplePhase = 'window',
    [double]$SampleSeconds = 6.0,
    [string]$SampleThread = '',
    # Where the pointer rests: "away" (top-left corner, no ray into the wall), "center", or "keep".
    [ValidateSet('away', 'center', 'keep')]
    [string]$Cursor = 'away',
    [int]$TimeoutMinutes = 20,
    [int]$ResX = 1920,
    [int]$ResY = 1080
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

if (-not $Project) { throw 'No host project: pass -Project or set DREAMGUI_TEST_PROJECT.' }
if (-not $Engine) { throw 'No engine: pass -Engine or set DREAMGUI_ENGINE.' }
$Exe = Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor.exe'
$ProjectDir = Split-Path -Parent $Project
$ProjectName = [IO.Path]::GetFileNameWithoutExtension($Project)
$Log = Join-Path $ProjectDir "Saved\Logs\$ProjectName.log"
if (-not $Out) { $Out = Join-Path $ProjectDir 'Saved\DreamGUIBench' }
New-Item -ItemType Directory -Force -Path $Out | Out-Null

if (Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" | Where-Object { $_.CommandLine -like "*$ProjectName.uproject*" }) {
    'An editor of this project is running; not starting.'
    exit 1
}

$env:DREAMBENCH_OUT = $Out
$env:DREAMBENCH_MODE = $Mode
$env:DREAMBENCH_TAG = $Tag
$env:DREAMBENCH_GAME = if ($Game) { '1' } else { '0' }
$env:DREAMBENCH_SCREEN_WIDGET = $ScreenWidget
$env:DREAMBENCH_TRACE = "$Trace"
$env:DREAMBENCH_TRACE_CHANNELS = $TraceChannels
$env:DREAMBENCH_CSV = "$Csv"
$env:DREAMBENCH_STATS_CMDS = $StatsCmds
$env:DREAMBENCH_AB_CMDS = $AbCmds
$env:DREAMBENCH_SETUP_CMDS = $SetupCmds
$env:DREAMBENCH_WARMUP = "$Warmup"
$env:DREAMBENCH_WINDOW = "$Window"
$env:DREAMBENCH_ROUNDS = "$Rounds"
$SampleFlag = Join-Path $Out "$($Tag)_$($Mode)_sampling"
Remove-Item -LiteralPath $SampleFlag, "$SampleFlag.done", "$SampleFlag.resolved" -Force -ErrorAction SilentlyContinue
$env:DREAMBENCH_SAMPLE_FLAG = if ($Sample -ne 0) { $SampleFlag } else { '' }
$env:DREAMBENCH_SAMPLE_PHASE = $SamplePhase

if (-not $Map) { $Map = if ($Mode -eq 'world') { '/Game/Maps/Lvl_Test_UI_BenchMark_Workdspace_5000Button' } else { '/Engine/Maps/Entry' } }
$TracePath = Join-Path $Out "$($Tag)_$($Mode).utrace"
Remove-Item -LiteralPath $TracePath -Force -ErrorAction SilentlyContinue

# The editor's own 1 ms timer request is not honoured while its window is covered (Windows 11), and every sleep-based
# wait in a frame then rounds up to the 15.6 ms tick: held here for the whole run, so that frames measure the work.
if (-not ('DreamBench.Timer' -as [type])) {
    Add-Type -Namespace DreamBench -Name Timer -MemberDefinition '[DllImport("winmm.dll")] public static extern uint timeBeginPeriod(uint p); [DllImport("winmm.dll")] public static extern uint timeEndPeriod(uint p);'
}
[DreamBench.Timer]::timeBeginPeriod(1) | Out-Null

$Script = (Join-Path $PSScriptRoot 'bench_run.py').Replace('\', '/')
$LaunchArgs = @("`"$Project`"", $Map, "-ExecCmds=`"py $Script`"", '-nosplash')
if ($Game) { $LaunchArgs += @('-game', '-windowed', "-resx=$ResX", "-resy=$ResY", '-unattended') }
if ($NamedEvents -ne 0) { $LaunchArgs += '-statnamedevents' }

# Where the pointer is decides whether a ray is traced into the wall every frame, and whether buttons turning under it
# change their hover state: put it in the same place for every run.
Add-Type -AssemblyName System.Windows.Forms
$Bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
if ($Cursor -eq 'away') { [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(0, 0) }
elseif ($Cursor -eq 'center') { [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point([int]($Bounds.Width / 2), [int]($Bounds.Height / 2)) }

$Process = Start-Process -FilePath $Exe -ArgumentList $LaunchArgs -PassThru
"started $($Process.Id) at $(Get-Date -Format HH:mm:ss) mode=$Mode game=$([bool]$Game) map=$Map cursor=$Cursor"
if ($Sample -ne 0) {
    $Report = Join-Path $Out "$($Tag)_$($Mode)_samples.txt"
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'run_sampler.ps1') -TargetPid $Process.Id -Flag $SampleFlag -Report $Report `
        -Seconds $SampleSeconds -Thread $SampleThread -Symbols "$(Join-Path $ProjectDir 'Plugins\DreamGUI\Binaries\Win64');$(Join-Path $Engine 'Engine\Binaries\Win64')"
}
if (-not $Process.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    "timed out after $TimeoutMinutes min; stopping $($Process.Id)"
    Stop-Process -Id $Process.Id -Force
}
"exited at $(Get-Date -Format HH:mm:ss)"
[DreamBench.Timer]::timeEndPeriod(1) | Out-Null
if (Test-Path -LiteralPath $Log) {
    Copy-Item -LiteralPath $Log -Destination (Join-Path $Out "$($Tag)_$($Mode).log") -Force
    Get-Content -LiteralPath $Log | Where-Object { $_ -match '\[DreamPerf\]' -or $_ -match 'Error:.*(DreamGUI|Python)' } |
        ForEach-Object { $_ -replace '^.*\[DreamPerf\] ', '' } | Select-Object -Last 40
}
