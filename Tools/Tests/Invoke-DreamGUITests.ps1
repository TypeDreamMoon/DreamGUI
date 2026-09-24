#Requires -Version 7.2
<#
.SYNOPSIS
    Builds the project that hosts DreamGUI, runs one preset of the plugin's automation tests in an
    unattended editor, and judges the run.

.DESCRIPTION
    1. Pre-flight: the engine and the project exist, the report directory is not on drive C, Python 3
       is there, and no Unreal editor has this .uproject open (unless -AllowEditorOpen).
    2. Static checks (static_checks.py) over the plugin under test.
    3. Build: Build.bat <Project>Editor Win64 Development, the WHOLE target. Never -Module=: a
       restricted build does not rewrite the plugin's module manifest, and a manifest that misses a
       module makes the whole plugin fail to load -- which used to look like "0 tests, no failures".
    4. Manifest: Binaries\Win64\UnrealEditor.modules of the plugin must carry the engine's BuildId
       and list every module DreamGUI.uplugin builds into an editor, each with a non-empty DLL.
    5. Run: UnrealEditor-Cmd.exe with -ExecCmds="Automation RunTests <filter>", -TestExit, the JSON
       report (-ReportExportPath) and the log (-abslog) in the report directory; killed on timeout.
    6. Digest (digest.py): the verdict from the engine's index.json, ensures and crashes from the
       log, the preset's floor on the number of tests, history.csv, summary.md.
    -Repeat N repeats 5 and 6, each time in a fresh editor.

    The plugin under test is the DreamGUI inside the project's Plugins directory -- not necessarily
    the checkout this script lives in; the banner says which one it is.

    Everything goes to one report directory,
    <project>\Saved\DreamGUITestReports\<yyyyMMdd-HHmmss>-<Preset> (the root moves with -ReportDir):
    filter.txt, static-checks.txt, build.log, manifest.txt, and per editor run: run-info.json,
    run.log, index.json, index.html, stdout.txt, stderr.txt, digest.json, summary.md (with -Repeat
    above 1, each run in its own repeat-<n> subdirectory). The report root keeps history.csv (one line
    per test per run) and latest.txt (the last report directory). Not Saved\DreamGUITests: that is
    the test module's own scratch root, whose subdirectories the tests delete.

.PARAMETER Preset
    A preset from presets.json: Quick (default), Interaction, Designer, Rhi, Validate, Pie, All.
.PARAMETER Project
    The .uproject. Default: $env:DREAMGUI_TEST_PROJECT, else the test host project when it exists,
    else DevTest.
.PARAMETER Engine
    The engine root. Default: $env:DREAMGUI_ENGINE, else F:\UnrealEngine\UE_Moon.
.PARAMETER NoBuild
    Skip the build; the manifest is still checked.
.PARAMETER SkipStaticChecks
    Skip static_checks.py.
.PARAMETER Filter
    An engine RunTests filter used instead of the preset's (the preset's arguments and timeout still
    apply). Terms are joined with '+'; "StartsWith:X" matches from the start, "^X$" exactly, anything
    else as a case-insensitive substring. No commas, semicolons or quotes.
.PARAMETER Repeat
    Run the tests this many times, each in a fresh editor, after one build.
.PARAMETER ReportDir
    The report root (default <project>\Saved\DreamGUITestReports). Refused on drive C.
.PARAMETER ShaderWorkingDir
    Where the editor and its shader compile workers keep their job files (-ShaderWorkingDir).
    Default <project>\Intermediate\ShaderWorkingDir: the engine's own default is under %TEMP%,
    on drive C, and a cold first start writes a lot there.
.PARAMETER MinTests
    Override the preset's floor on the number of tests that must run. With -Filter the default is 1.
.PARAMETER TimeoutMinutes
    Override the preset's time limit for one editor run.
.PARAMETER AllowEditorOpen
    Go ahead although an editor has this project open.
.PARAMETER SkipIfEditorOpen
    When an editor has this project open, say so and exit 0 instead of 2. The pre-push hook uses it:
    an open editor is not a reason to refuse a push.
.PARAMETER Python
    The Python 3 interpreter (default: python).

.EXAMPLE
    pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1
.EXAMPLE
    pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Preset Rhi -NoBuild
.EXAMPLE
    pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Filter "StartsWith:DreamGUI.Button" -Repeat 5

.NOTES
    Exit codes: 0 green (known issues aside), 1 tests red, 2 the suite could not be run or judged
    (static checks, build, manifest, crash, ensure, timeout, missing report, fewer tests than the
    floor, declared tests that did not run).
#>
[CmdletBinding()]
param(
    [string]$Preset = 'Quick',
    [string]$Project,
    [string]$Engine,
    [switch]$NoBuild,
    [switch]$SkipStaticChecks,
    [string]$Filter,
    [ValidateRange(1, 50)]
    [int]$Repeat = 1,
    [string]$ReportDir,
    [string]$ShaderWorkingDir,
    [int]$MinTests = -1,
    [int]$TimeoutMinutes = 0,
    [switch]$AllowEditorOpen,
    [switch]$SkipIfEditorOpen,
    [string]$Python = 'python'
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

# An error nobody expected must not end the script with PowerShell's own exit code 1, which reads
# as "tests red": whatever went wrong, the suite was not judged.
trap {
    Write-Host "!!  Unexpected error: $($_.Exception.Message)" -ForegroundColor Yellow
    if ($_.InvocationInfo) { Write-Host "    $($_.InvocationInfo.PositionMessage)" }
    exit 2
}

# Where this machine keeps things; each can be overridden (see README.md).
$HostProjectDefault = 'I:\UnrealProject_Moon\DEV_58\DreamGUITestHost\DreamGUITestHost.uproject'
$DevTestProjectDefault = 'I:\UnrealProject_Moon\DEV_58\DevTest\DevTest.uproject'
$EngineDefault = 'F:\UnrealEngine\UE_Moon'

$ToolsDir = $PSScriptRoot
$PresetsFile = Join-Path $ToolsDir 'presets.json'
$KnownIssuesFile = Join-Path $ToolsDir 'known-issues.json'
# The engine copies at most 16384 characters of its command line (FCommandLine::MaxCommandLineSize)
# and says nothing about the rest; the filter has to fit with room for everything else.
$MaxFilterLength = 12000

$script:ReportPath = $null
$script:ReportRoot = $null
$script:PresetName = $Preset

# ------------------------------------------------------------------------------------------------
# small helpers
# ------------------------------------------------------------------------------------------------

function Write-Step([string]$Text) { Write-Host "==> $Text" -ForegroundColor Cyan }
function Write-Note([string]$Text) { Write-Host "    $Text" }
function Write-Problem([string]$Text) { Write-Host "!!  $Text" -ForegroundColor Yellow }

function Get-Shortened([string]$Text, [int]$Max) {
    if ($Text.Length -le $Max) { return $Text }
    return $Text.Substring(0, $Max) + " ... ($($Text.Length) characters)"
}

function Resolve-UserPath([string]$Path) {
    # Relative to PowerShell's current location (which .NET's own current directory may not be),
    # then normalized -- '..' collapsed, '/' turned into '\' -- because the path is later compared
    # with the command lines of running editors.
    return [IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path))
}

function Read-Json([string]$Path) {
    # ReadAllText drops the byte order mark UBT and the engine put in front of their JSON.
    $text = [IO.File]::ReadAllText($Path)
    return ($text | ConvertFrom-Json -AsHashtable -Depth 64)
}

function Save-Json($Object, [string]$Path) {
    $Object | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding utf8
}

function Get-Prop($Table, [string[]]$Names) {
    # The first of the keys the table has; nothing at all when it has none of them.
    if ($Table -isnot [System.Collections.IDictionary]) { return }
    foreach ($name in $Names) {
        if ($Table.Contains($name)) { return $Table[$name] }
    }
}

function Test-OnDriveC([string]$Path) {
    $root = [IO.Path]::GetPathRoot($Path)
    return ($root.TrimEnd('\', '/').ToUpperInvariant() -eq 'C:')
}

function Save-Latest {
    if ($script:ReportRoot -and $script:ReportPath) {
        try { Set-Content -LiteralPath (Join-Path $script:ReportRoot 'latest.txt') -Value $script:ReportPath -Encoding utf8 } catch { }
    }
}

function Stop-Run([int]$Code, [string]$Why, [string[]]$Details = @()) {
    Write-Problem $Why
    foreach ($d in $Details) { Write-Note $d }
    if ($script:ReportPath) {
        $lines = @("# DreamGUI tests: $script:PresetName -- stopped before a verdict", '', "- $Why")
        $lines += @($Details | ForEach-Object { "  - $_" })
        $lines += @('', "Exit code: $Code")
        Set-Content -LiteralPath (Join-Path $script:ReportPath 'summary.md') -Value ($lines -join "`n") -Encoding utf8
        Write-Note "report: $script:ReportPath"
        Save-Latest
    }
    exit $Code
}

function Invoke-Native {
    # A native command with $ErrorActionPreference relaxed; the caller reads $LASTEXITCODE.
    #   Merge   -- stderr lines join the output (for logs)
    #   Discard -- stdout only (for values: git warns on stderr about line endings)
    #   Pass    -- stdout is the output, stderr goes straight to the console
    param(
        [string]$Exe,
        [string[]]$Arguments,
        [ValidateSet('Merge', 'Discard', 'Pass')]
        [string]$Stderr = 'Merge'
    )
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        switch ($Stderr) {
            'Merge' { & $Exe @Arguments 2>&1 | ForEach-Object { "$_" } }
            'Discard' { & $Exe @Arguments 2>$null }
            'Pass' { & $Exe @Arguments }
        }
    } finally {
        $ErrorActionPreference = $saved
    }
}

# ------------------------------------------------------------------------------------------------
# facts about the machine and the project
# ------------------------------------------------------------------------------------------------

function Get-EditorsOnProject([string]$UProject) {
    # Every UnrealEditor*.exe (the editor, -Cmd, other configurations) whose command line names
    # this .uproject. An editor started from the project browser without it on its command line
    # is not seen; that is the one case this cannot catch.
    $needle = $UProject.Replace('/', '\').ToLowerInvariant()
    try {
        $procs = @(Get-CimInstance -ClassName Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction Stop)
    } catch {
        Write-Problem "Could not list running editors ($($_.Exception.Message)); assuming none."
        return
    }
    foreach ($p in $procs) {
        $line = [string]$p.CommandLine
        if ($line -and $line.Replace('/', '\').ToLowerInvariant().Contains($needle)) { $p }
    }
}

function Get-GitState([string]$Dir) {
    $state = [ordered]@{ commit = ''; branch = ''; dirty = $false }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { return $state }
    $sha = @(Invoke-Native git @('-C', $Dir, 'rev-parse', 'HEAD') -Stderr Discard)
    if ($LASTEXITCODE -eq 0 -and $sha.Count -gt 0) { $state.commit = ([string]$sha[0]).Trim() }
    $branch = @(Invoke-Native git @('-C', $Dir, 'rev-parse', '--abbrev-ref', 'HEAD') -Stderr Discard)
    if ($LASTEXITCODE -eq 0 -and $branch.Count -gt 0) { $state.branch = ([string]$branch[0]).Trim() }
    # What a build compiles: sources (tracked or not -- UBT globs the module directories), shaders,
    # config and the descriptor.
    $changes = @(Invoke-Native git @('-C', $Dir, 'status', '--porcelain', '--', 'Source', 'Shaders', 'Config', 'DreamGUI.uplugin') -Stderr Discard)
    if ($LASTEXITCODE -eq 0) { $state.dirty = (@($changes | Where-Object { "$_".Trim() }).Count -gt 0) }
    return $state
}

function Find-PluginUnderTest([string]$ProjectDir) {
    $plugins = Join-Path $ProjectDir 'Plugins'
    $direct = Join-Path $plugins 'DreamGUI\DreamGUI.uplugin'
    if (Test-Path -LiteralPath $direct -PathType Leaf) { return @(Get-Item -LiteralPath $direct) }
    if (-not (Test-Path -LiteralPath $plugins -PathType Container)) { return @() }
    return @(Get-ChildItem -LiteralPath $plugins -Filter 'DreamGUI.uplugin' -File -Recurse -Depth 2 -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notmatch '\\(Plan|Intermediate|Saved|Binaries)\\' })
}

function Test-BuiltForEditor($Module, [string]$TargetName) {
    # ModuleDescriptor.IsCompiledInConfiguration (UnrealBuildTool) for an Editor target on Win64
    # Development, where bBuildRequiresCookedData is false and bBuildDeveloperTools is true.
    $platform = 'Win64'
    $configuration = 'Development'
    $allow = Get-Prop $Module 'PlatformAllowList', 'WhitelistPlatforms'
    if ($null -ne $allow -and -not (@($allow) -contains $platform)) { return $false }
    $deny = Get-Prop $Module 'PlatformDenyList', 'BlacklistPlatforms'
    if ($null -ne $deny -and (@($deny) -contains $platform)) { return $false }
    $targets = Get-Prop $Module 'TargetAllowList', 'WhitelistTargets'
    if ($null -ne $targets -and @($targets).Count -gt 0 -and -not (@($targets) -contains 'Editor')) { return $false }
    $targetsDenied = Get-Prop $Module 'TargetDenyList', 'BlacklistTargets'
    if ($null -ne $targetsDenied -and (@($targetsDenied) -contains 'Editor')) { return $false }
    $configs = Get-Prop $Module 'TargetConfigurationAllowList', 'WhitelistTargetConfigurations'
    if ($null -ne $configs -and @($configs).Count -gt 0 -and -not (@($configs) -contains $configuration)) { return $false }
    $configsDenied = Get-Prop $Module 'TargetConfigurationDenyList', 'BlacklistTargetConfigurations'
    if ($null -ne $configsDenied -and (@($configsDenied) -contains $configuration)) { return $false }
    $games = Get-Prop $Module 'GameTargetAllowList'
    if ($null -ne $games -and @($games).Count -gt 0 -and -not (@($games) -contains $TargetName)) { return $false }
    $gamesDenied = Get-Prop $Module 'GameTargetDenyList'
    if ($null -ne $gamesDenied -and (@($gamesDenied) -contains $TargetName)) { return $false }
    $built = @('Runtime', 'RuntimeNoCommandlet', 'RuntimeAndProgram', 'UncookedOnly', 'Developer', 'DeveloperTool',
        'Editor', 'EditorNoCommandlet', 'EditorAndProgram', 'ServerOnly', 'ClientOnly', 'ClientOnlyNoCommandlet')
    return ($built -contains [string](Get-Prop $Module 'Type'))
}

function Test-ModuleManifest([string]$PluginDir, [string]$EngineDir, [string]$TargetName) {
    $problems = [System.Collections.Generic.List[string]]::new()
    $lines = [System.Collections.Generic.List[string]]::new()
    $ok = 0
    $uplugin = Read-Json (Join-Path $PluginDir 'DreamGUI.uplugin')
    $expected = [System.Collections.Generic.List[string]]::new()
    $supported = Get-Prop $uplugin 'SupportedTargetPlatforms'
    if ($null -eq $supported -or (@($supported) -contains 'Win64')) {
        foreach ($m in @(Get-Prop $uplugin 'Modules')) {
            if ($null -ne $m -and (Test-BuiltForEditor $m $TargetName)) { $expected.Add([string](Get-Prop $m 'Name')) }
        }
    }
    $lines.Add("DreamGUI.uplugin builds these modules into $TargetName (Win64 Development): $($expected -join ', ')")
    $manifestPath = Join-Path $PluginDir 'Binaries\Win64\UnrealEditor.modules'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        $problems.Add("$manifestPath does not exist: the plugin has not been built for the editor.")
        return @{ Problems = $problems; Lines = $lines; Ok = $ok }
    }
    $manifest = Read-Json $manifestPath
    $buildId = [string](Get-Prop $manifest 'BuildId')
    $engineManifest = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor.modules'
    if (Test-Path -LiteralPath $engineManifest -PathType Leaf) {
        $engineBuildId = [string](Get-Prop (Read-Json $engineManifest) 'BuildId')
        $lines.Add("BuildId: plugin $buildId, engine $engineBuildId")
        if ($engineBuildId -and $buildId -ne $engineBuildId) {
            $problems.Add("The plugin manifest's BuildId ($buildId) is not the engine's ($engineBuildId). The module manager skips every module of a manifest whose BuildId differs (FModuleManager::FindModulePathsInDirectory), so the plugin would fail to load. Rebuild the whole editor target.")
        }
    } else {
        $lines.Add("No engine manifest at $engineManifest; BuildId not compared.")
    }
    $listed = Get-Prop $manifest 'Modules'
    $binaries = Split-Path -Parent $manifestPath
    foreach ($name in $expected) {
        $file = $null
        if ($listed -is [System.Collections.IDictionary] -and $listed.Contains($name)) { $file = [string]$listed[$name] }
        if (-not $file) {
            $problems.Add("Module $name is not listed in $manifestPath. A build restricted with -Module= does not rewrite the manifest, and a plugin whose manifest misses a module fails to load as a whole. Rebuild the whole editor target.")
            continue
        }
        $dll = Join-Path $binaries $file
        if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) {
            $problems.Add("Module $name is listed as $file, which does not exist in $binaries.")
        } elseif ((Get-Item -LiteralPath $dll).Length -eq 0) {
            $problems.Add("$dll is zero bytes (an interrupted link) and Windows refuses to load it. Delete it and rebuild.")
        } else {
            $lines.Add("ok  $name -> $file")
            $ok++
        }
    }
    return @{ Problems = $problems; Lines = $lines; Ok = $ok }
}

function Get-CompletedCount([string]$Log) {
    if (-not (Test-Path -LiteralPath $Log -PathType Leaf)) { return 0 }
    try {
        # The editor still has the log open for writing; share it back.
        $stream = [IO.File]::Open($Log, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            $reader = [IO.StreamReader]::new($stream)
            $text = $reader.ReadToEnd()
        } finally {
            $stream.Dispose()
        }
        return ([regex]::Matches($text, 'Test Completed\. Result=')).Count
    } catch {
        return '?'
    }
}

# ------------------------------------------------------------------------------------------------
# one editor run
# ------------------------------------------------------------------------------------------------

function Invoke-EditorRun([string]$RunDir, [int]$Index) {
    $RunDir = $RunDir.TrimEnd('\', '/')   # a trailing backslash would escape the closing quote below
    $runLog = Join-Path $RunDir 'run.log'
    $stdout = Join-Path $RunDir 'stdout.txt'
    $stderr = Join-Path $RunDir 'stderr.txt'
    # No value below may end in a backslash: the C runtime reads \" as a quote inside the argument.
    $argLine = (@(
            "`"$Project`"",
            "-ExecCmds=`"Automation RunTests $ResolvedFilter`"",
            '-unattended', '-nopause', '-NoSplash', '-NoSound',
            '-TestExit="Automation Test Queue Empty"',
            "-ReportExportPath=`"$RunDir`"",
            "-abslog=`"$runLog`"",
            "-ShaderWorkingDir=`"$ShaderDir`""
        ) + @($PresetArgs)) -join ' '

    $info = [ordered]@{
        preset          = $script:PresetName
        filter          = $ResolvedFilter
        filterSource    = $FilterSource
        args            = @($PresetArgs)
        minTests        = $Floor
        timeoutMinutes  = $Timeout
        project         = $Project
        pluginRoot      = $PluginDir
        engine          = $Engine
        commit          = $Git.commit
        branch          = $Git.branch
        dirty           = $Git.dirty
        runName         = (Split-Path -Leaf $script:ReportPath) + $(if ($Repeat -gt 1) { "/repeat-$Index" } else { '' })
        repeatIndex     = $Index
        repeatCount     = $Repeat
        reportRoot      = $script:ReportRoot
        historyFile     = (Join-Path $script:ReportRoot 'history.csv')
        presetsFile     = $PresetsFile
        knownIssuesFile = $KnownIssuesFile
        shaderWorkingDir = $ShaderDir
        commandLine     = "`"$EditorCmd`" $argLine"
        startedUtc      = [DateTime]::UtcNow.ToString('o')
        elapsedSeconds  = $null
        editorExitCode  = $null
        timedOut        = $false
    }
    Save-Json $info (Join-Path $RunDir 'run-info.json')

    $label = if ($Repeat -gt 1) { "$script:PresetName, run $Index of $Repeat" } else { $script:PresetName }
    Write-Step "Running the tests ($label)"
    Write-Note "filter: $(Get-Shortened $ResolvedFilter 200)"
    Write-Note "log:    $runLog"

    $watch = [Diagnostics.Stopwatch]::StartNew()
    $proc = $null
    $timedOut = $false
    try {
        $proc = Start-Process -FilePath $EditorCmd -ArgumentList $argLine -WorkingDirectory $ProjectDir `
            -NoNewWindow -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $null = $proc.Handle   # keeps ExitCode readable once the process is gone
        $deadline = [DateTime]::UtcNow.AddMinutes($Timeout)
        $nextNote = [DateTime]::UtcNow.AddSeconds(60)
        while (-not $proc.WaitForExit(5000)) {
            if ([DateTime]::UtcNow -ge $deadline) { $timedOut = $true; break }
            if ([DateTime]::UtcNow -ge $nextNote) {
                $nextNote = [DateTime]::UtcNow.AddSeconds(60)
                Write-Note ('... {0:N0} s, {1} test(s) finished' -f $watch.Elapsed.TotalSeconds, (Get-CompletedCount $runLog))
            }
        }
        if ($timedOut) {
            Write-Problem "Still running after $Timeout minutes: killing the editor and everything it started."
            try { $proc.Kill($true) } catch { Write-Problem "Could not kill it: $($_.Exception.Message)" }
            $null = $proc.WaitForExit(60000)
        }
    } finally {
        # Ctrl+C or an error in here must not leave a headless editor running the suite.
        if ($null -ne $proc -and -not $proc.HasExited) {
            try { $proc.Kill($true) } catch { }
        }
    }
    $watch.Stop()

    $info.elapsedSeconds = [math]::Round($watch.Elapsed.TotalSeconds, 1)
    $info.timedOut = $timedOut
    $info.editorExitCode = if ($timedOut -or -not $proc.HasExited) { $null } else { $proc.ExitCode }
    Save-Json $info (Join-Path $RunDir 'run-info.json')
    $shown = if ($null -eq $info.editorExitCode) { 'none (killed)' } else { $info.editorExitCode }
    Write-Note ('editor finished after {0:N0} s, exit code {1}' -f $watch.Elapsed.TotalSeconds, $shown)

    Invoke-Native $Python @('-B', (Join-Path $ToolsDir 'digest.py'), 'tests', $RunDir) | Out-Host
    $code = $LASTEXITCODE
    if ($code -notin 0, 1, 2) {
        Write-Problem "digest.py exited with $code; the run is treated as not judged."
        $code = 2
    }
    return [int]$code
}

# ------------------------------------------------------------------------------------------------
# 1. pre-flight
# ------------------------------------------------------------------------------------------------

if (-not (Test-Path -LiteralPath $PresetsFile -PathType Leaf)) { Stop-Run 2 "presets.json is missing beside the runner ($PresetsFile)." }
$presetsData = Read-Json $PresetsFile
$presets = Get-Prop $presetsData 'presets'
if ($presets -isnot [System.Collections.IDictionary]) { Stop-Run 2 "$PresetsFile has no ""presets"" object." }
$presetKey = @($presets.Keys | Where-Object { $_ -ieq $Preset }) | Select-Object -First 1
if (-not $presetKey) { Stop-Run 2 "No preset named '$Preset'. Presets: $((@($presets.Keys) | Sort-Object) -join ', ')." }
$script:PresetName = $presetKey
$spec = $presets[$presetKey]
$PresetArgs = @(Get-Prop $spec 'args' | Where-Object { $_ })

if (-not $Engine) { $Engine = if ($env:DREAMGUI_ENGINE) { $env:DREAMGUI_ENGINE } else { $EngineDefault } }
$Engine = Resolve-UserPath $Engine
$EditorCmd = Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$BuildBat = Join-Path $Engine 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path -LiteralPath $EditorCmd -PathType Leaf)) { Stop-Run 2 "No UnrealEditor-Cmd.exe under the engine $Engine." }
if (-not $NoBuild -and -not (Test-Path -LiteralPath $BuildBat -PathType Leaf)) { Stop-Run 2 "No Build.bat under the engine $Engine." }

if (-not $Project) {
    if ($env:DREAMGUI_TEST_PROJECT) { $Project = $env:DREAMGUI_TEST_PROJECT }
    elseif (Test-Path -LiteralPath $HostProjectDefault -PathType Leaf) { $Project = $HostProjectDefault }
    else { $Project = $DevTestProjectDefault }
}
$Project = Resolve-UserPath $Project
if (-not (Test-Path -LiteralPath $Project -PathType Leaf) -or [IO.Path]::GetExtension($Project) -ne '.uproject') {
    Stop-Run 2 "The project $Project is not an existing .uproject."
}
$ProjectDir = Split-Path -Parent $Project
$ProjectName = [IO.Path]::GetFileNameWithoutExtension($Project)
$EditorTarget = "${ProjectName}Editor"

$descriptors = @(Find-PluginUnderTest $ProjectDir)
if ($descriptors.Count -eq 0) { Stop-Run 2 "The project $Project has no DreamGUI plugin under its Plugins directory." }
if ($descriptors.Count -gt 1) { Stop-Run 2 'The project holds more than one DreamGUI.uplugin; the plugin under test is ambiguous.' @($descriptors | ForEach-Object { $_.FullName }) }
$PluginDir = $descriptors[0].DirectoryName

$script:ReportRoot = if ($ReportDir) { Resolve-UserPath $ReportDir } else { Join-Path $ProjectDir 'Saved\DreamGUITestReports' }
if (Test-OnDriveC $script:ReportRoot) {
    Stop-Run 2 "The report directory $script:ReportRoot is on drive C, which is nearly full. Pass -ReportDir on another drive."
}
# The engine appends the separator itself (FPaths::CustomShaderDirArgument); one here would escape the quote.
$ShaderDir = $(if ($ShaderWorkingDir) { Resolve-UserPath $ShaderWorkingDir } else { Join-Path $ProjectDir 'Intermediate\ShaderWorkingDir' }).TrimEnd('\', '/')
if (Test-OnDriveC $ShaderDir) {
    Write-Problem "The shader working directory $ShaderDir is on drive C, which is nearly full."
}

if (-not (Get-Command $Python -ErrorAction SilentlyContinue)) {
    Stop-Run 2 "Python ($Python) is not on PATH. The static checks and the digest need Python 3; pass -Python."
}
$pythonVersion = [string](@(Invoke-Native $Python @('--version')) | Select-Object -First 1)
if ($LASTEXITCODE -ne 0 -or $pythonVersion -notmatch '^Python 3\.') {
    Stop-Run 2 "$Python does not answer as Python 3 ('$pythonVersion'). Pass -Python."
}

if (-not $AllowEditorOpen) {
    $open = @(Get-EditorsOnProject $Project)
    if ($open.Count -gt 0) {
        $why = "An Unreal editor has $Project open (process $(@($open | ForEach-Object { $_.ProcessId }) -join ', ')). Building would fail with LNK1104 on the DLLs it holds, and a second editor on the project would share its Saved directory."
        if ($SkipIfEditorOpen) {
            Write-Problem "$why Not testing (-SkipIfEditorOpen)."
            exit 0
        }
        Stop-Run 2 $why @('Close it, run against the test host project (the open editor does not use its binaries), or pass -AllowEditorOpen.')
    }
}

$Git = Get-GitState $PluginDir
$Floor = if ($MinTests -ge 0) { $MinTests } elseif ($Filter) { 1 } else { [int](Get-Prop $spec 'minTests') }
$Timeout = if ($TimeoutMinutes -gt 0) { $TimeoutMinutes } else { [int](Get-Prop $spec 'timeoutMinutes') }
if ($Timeout -le 0) { $Timeout = 30 }

# One report directory per invocation, never reused.
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$candidate = Join-Path $script:ReportRoot "$stamp-$presetKey"
$suffix = 2
while (Test-Path -LiteralPath $candidate) { $candidate = Join-Path $script:ReportRoot "$stamp-$presetKey-$suffix"; $suffix++ }
New-Item -ItemType Directory -Force -Path $candidate | Out-Null
$script:ReportPath = $candidate

$toolsPlugin = (Resolve-Path -LiteralPath (Join-Path $ToolsDir '..\..')).Path.TrimEnd('\')
$shortCommit = if ($Git.commit) { $Git.commit.Substring(0, [Math]::Min(12, $Git.commit.Length)) } else { 'no commit' }
Write-Step "DreamGUI tests: $presetKey"
Write-Note "project: $Project"
Write-Note "plugin:  $PluginDir  ($(if ($Git.branch) { $Git.branch } else { 'no branch' }) $shortCommit$(if ($Git.dirty) { ', uncommitted changes' } else { '' }))"
Write-Note "engine:  $Engine"
Write-Note "python:  $pythonVersion"
Write-Note "args:    $(@($PresetArgs) -join ' ')   floor: $Floor tests   timeout: $Timeout min"
Write-Note "shaders: $ShaderDir"
Write-Note "report:  $script:ReportPath"
if ($PluginDir.TrimEnd('\') -ne $toolsPlugin) {
    Write-Note "note:    the plugin under test is not the checkout this runner lives in ($toolsPlugin)."
}

# ------------------------------------------------------------------------------------------------
# the filter
# ------------------------------------------------------------------------------------------------

if ($Filter) {
    # -ExecCmds splits on commas (ParseExecCommands.cpp) and the Automation command on semicolons;
    # a quote would end the -ExecCmds value.
    if ($Filter -match "[,;'`"]") { Stop-Run 2 "The filter may not hold commas, semicolons or quotes: $Filter" }
    $ResolvedFilter = $Filter.Trim()
    $FilterSource = 'override'
} else {
    $resolved = @(Invoke-Native $Python @('-B', (Join-Path $ToolsDir 'sourcescan.py'), 'filter', '--preset', $presetKey,
            '--presets', $PresetsFile, '--root', $PluginDir) -Stderr Pass)
    $code = $LASTEXITCODE
    $lines = @($resolved | ForEach-Object { "$_".Trim() } | Where-Object { $_ })
    if ($code -ne 0 -or $lines.Count -eq 0) { Stop-Run 2 "The $presetKey preset could not be turned into a filter (sourcescan.py exit $code)." }
    $ResolvedFilter = $lines[-1]
    $FilterSource = 'preset'
}
if ($ResolvedFilter.Length -gt $MaxFilterLength) {
    Stop-Run 2 "The filter is $($ResolvedFilter.Length) characters; the engine keeps only 16384 characters of its whole command line. Narrow the preset."
}
Set-Content -LiteralPath (Join-Path $script:ReportPath 'filter.txt') -Value $ResolvedFilter -Encoding utf8

# ------------------------------------------------------------------------------------------------
# 2. static checks
# ------------------------------------------------------------------------------------------------

if (-not $SkipStaticChecks) {
    Write-Step 'Static checks'
    $staticLog = Join-Path $script:ReportPath 'static-checks.txt'
    Invoke-Native $Python @('-B', (Join-Path $ToolsDir 'static_checks.py'), '--root', $PluginDir) |
        Tee-Object -FilePath $staticLog | Out-Host
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        Stop-Run 2 "The static checks failed (exit $code); nothing was built or run." @("Findings: $staticLog", 'Fix them, allow them (README.md, "Static checks"), or pass -SkipStaticChecks.')
    }
}

# ------------------------------------------------------------------------------------------------
# 3. build
# ------------------------------------------------------------------------------------------------

if (-not $NoBuild) {
    Write-Step "Building $EditorTarget (Win64 Development, the whole target)"
    $buildLog = Join-Path $script:ReportPath 'build.log'
    Write-Note "log: $buildLog"
    # -NoEngineChanges: the editor target shares the engine's build products, so an out-of-date engine
    # module would otherwise be recompiled and relinked in place -- which fails with LNK1104 while any
    # editor on this engine is open, and rewrites the engine's manifests under every project on it.
    # With it UnrealBuildTool refuses before running a single action and lists the engine files it
    # would have written (BuildMode.cs, BuildOptions.NoEngineChanges).
    $buildArgs = @($EditorTarget, 'Win64', 'Development', "-Project=$Project", '-WaitMutex', '-NoHotReloadFromIDE', '-NoEngineChanges')
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        # From PowerShell, not bash: under bash, Build.bat -WaitMutex comes back with 127 after waiting.
        & $BuildBat @buildArgs *>&1 | Out-File -LiteralPath $buildLog -Encoding utf8
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved
    }
    $watch.Stop()
    if ($code -ne 0) {
        if (Select-String -LiteralPath $buildLog -SimpleMatch 'FailedDueToEngineChange' -Quiet) {
            $engineFiles = @(Get-Content -LiteralPath $buildLog | Where-Object { $_.Trim().StartsWith($Engine, [StringComparison]::OrdinalIgnoreCase) } |
                    ForEach-Object { $_.Trim() } | Select-Object -Unique)
            Stop-Run 2 "The build would have recompiled or rewritten $($engineFiles.Count) engine file(s); nothing was built." (@(
                    'An engine module is out of date for this target. Build the engine (or the working project) with every editor closed first, then run again.',
                    "Log: $buildLog") + @($engineFiles | Select-Object -First 12))
        }
        Invoke-Native $Python @('-B', (Join-Path $ToolsDir 'digest.py'), 'build', $buildLog) | Out-Host
        Stop-Run 2 "The build failed (Build.bat exit $code) after $([int]$watch.Elapsed.TotalSeconds) s." @("Log: $buildLog")
    }
    Write-Note "built in $([int]$watch.Elapsed.TotalSeconds) s"
}

# ------------------------------------------------------------------------------------------------
# 4. the module manifest
# ------------------------------------------------------------------------------------------------

Write-Step 'Module manifest'
$manifestReport = Test-ModuleManifest $PluginDir $Engine $EditorTarget
$manifestText = @($manifestReport.Lines) + @($manifestReport.Problems | ForEach-Object { "PROBLEM $_" })
Set-Content -LiteralPath (Join-Path $script:ReportPath 'manifest.txt') -Value ($manifestText -join "`n") -Encoding utf8
if ($manifestReport.Problems.Count -gt 0) {
    Stop-Run 2 'The plugin would not load as built.' @($manifestReport.Problems)
}
Write-Note "$($manifestReport.Ok) module(s) listed with their DLLs; BuildId matches the engine"

# ------------------------------------------------------------------------------------------------
# 5 and 6. run and digest
# ------------------------------------------------------------------------------------------------

$results = @()
for ($i = 1; $i -le $Repeat; $i++) {
    $runDir = if ($Repeat -eq 1) { $script:ReportPath } else { Join-Path $script:ReportPath "repeat-$i" }
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    $code = Invoke-EditorRun -RunDir $runDir -Index $i
    $results += [pscustomobject]@{ Index = $i; Code = $code; Dir = $runDir }
}

$final = [int](($results | Measure-Object -Property Code -Maximum).Maximum)
if ($Repeat -gt 1) {
    $lines = @("# DreamGUI tests: $presetKey x $Repeat", '', '| Run | Exit | Verdict | Ran | Failed |', '|---|---|---|---|---|')
    foreach ($r in $results) {
        $verdict = '?'; $ran = '?'; $failed = '?'
        $digestFile = Join-Path $r.Dir 'digest.json'
        if (Test-Path -LiteralPath $digestFile -PathType Leaf) {
            try {
                $d = Read-Json $digestFile
                $verdict = $d['verdict']; $ran = $d['counts']['ran']; $failed = $d['counts']['failed']
            } catch { }
        }
        $lines += "| [repeat-$($r.Index)](repeat-$($r.Index)/summary.md) | $($r.Code) | $verdict | $ran | $failed |"
    }
    $lines += @('', "Exit code: $final (the worst run).", '', 'A test that passed in one run and failed in another is listed as flaky in the later runs'' summaries (from history.csv, clean trees only).')
    Set-Content -LiteralPath (Join-Path $script:ReportPath 'summary.md') -Value ($lines -join "`n") -Encoding utf8
}
Save-Latest

$word = switch ($final) { 0 { 'green' } 1 { 'red' } default { 'not judged' } }
Write-Step "Done: $word (exit $final)"
Write-Note "report: $script:ReportPath"
exit $final
