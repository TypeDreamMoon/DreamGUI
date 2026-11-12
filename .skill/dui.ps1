#Requires -Version 5.1
<#
.SYNOPSIS
    Checks, compiles or lays out a .dui from the command line, for CI and for an agent with no DreamMCP server.

.DESCRIPTION
    The verbs are the DreamMCP endpoint's own actions, asked of the engine the way a DreamMCP server asks them:

      check   <file> | -All    read a .dui as the compiler does and report its errors; builds nothing
      compile <file>           build the widget Blueprint its class line names (components it places first) and save it
      layout  <file>           compile, lay the screen out at -Width x -Height (and -Sizes), list every node and the problems
      info                     plugin, engine, OS and locale, as Tools/Get-DreamGUIEnvironment.ps1 prints them; no engine

    When an editor (or a host) of the project already answers on its DreamMCP endpoint (Saved/DreamMCP/gui.json), the
    call goes to it, and what a compile writes stays in that editor's memory. Otherwise a headless host is started for the
    call (`-run=DreamGUIHost -nullrhi -DreamMCPHost`, about ten seconds), asked, and shut down; a compile there saves.

    Output: the answer's summary, one line per diagnostic as `File.dui(line,col): severity DUInnnn: message`, and a last
    line `RESULT=OK` or `RESULT=FAILED`. Exit code 0 when the source has no error, 1 when it has, 2 when the call was
    refused or could not be made (no engine, no project, the file is missing).

    The project is -Project, else DREAMGUI_TEST_PROJECT, else the .uproject two or three folders above the plugin, else
    the one in the current folder or above it. The engine is -Engine, else DREAMGUI_ENGINE, else the engine the
    project's EngineAssociation names.

.EXAMPLE
    pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 check DUI/Menu.dui
.EXAMPLE
    pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 layout DUI/Menu.dui -Width 1280 -Height 720 -Sizes "1920x1080, 1080x1920"
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0, Mandatory = $true)][ValidateSet('check', 'compile', 'layout', 'info')][string]$Verb,
    [Parameter(Position = 1)][string]$File,
    [switch]$All,
    [switch]$Force,
    [int]$Width = 1920,
    [int]$Height = 1080,
    [string]$Sizes,
    [int]$Depth = 6,
    [string]$Project,
    [string]$Engine,
    [switch]$Json,
    [int]$TimeoutSeconds = 600
)

$ErrorActionPreference = 'Stop'
$PluginDir = Split-Path -Parent $PSScriptRoot

if ($Verb -eq 'info') {
    $Arguments = @('-NoProfile', '-File', (Join-Path $PluginDir 'Tools/Get-DreamGUIEnvironment.ps1'))
    if ($Project) { $Arguments += @('-Project', $Project) }
    if ($Engine) { $Arguments += @('-Engine', $Engine) }
    $Shell = if ($PSVersionTable.PSEdition -eq 'Core') { 'pwsh' } else { 'powershell' }
    & $Shell @Arguments
    Write-Output 'RESULT=OK'
    exit 0
}

function Fail([string]$Message) {
    Write-Output "dui: $Message"
    Write-Output 'RESULT=FAILED'
    exit 2
}

function Read-Json([string]$Path) { Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json }

function Find-ProjectIn([string]$Dir) {
    if (-not $Dir -or -not (Test-Path -LiteralPath $Dir -PathType Container)) { return $null }
    $Found = @(Get-ChildItem -LiteralPath $Dir -Filter '*.uproject' -File -ErrorAction SilentlyContinue)
    if ($Found.Count -eq 1) { return $Found[0].FullName }
    return $null
}

function Find-Project {
    if ($Project) { return (Resolve-Path -LiteralPath $Project).ProviderPath }
    if ($env:DREAMGUI_TEST_PROJECT) { return $env:DREAMGUI_TEST_PROJECT }
    $Dir = $PluginDir
    foreach ($Levels in 1..3) {
        $Dir = Split-Path -Parent $Dir
        if ($Levels -ge 2) { $Found = Find-ProjectIn $Dir; if ($Found) { return $Found } }
    }
    $Current = (Get-Location -PSProvider FileSystem).ProviderPath
    while ($Current) {
        $Found = Find-ProjectIn $Current
        if ($Found) { return $Found }
        $Parent = Split-Path -Parent $Current
        if ($Parent -eq $Current) { break }
        $Current = $Parent
    }
    return $null
}

function Find-Engine([string]$ProjectFile) {
    if ($Engine) { return $Engine }
    if ($env:DREAMGUI_ENGINE) { return $env:DREAMGUI_ENGINE }
    $Association = [string](Read-Json $ProjectFile).EngineAssociation
    if ($Association.StartsWith('{')) {
        $Key = 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds'
        if (Test-Path $Key) { return (Get-ItemProperty -Path $Key -Name $Association -ErrorAction SilentlyContinue).$Association }
        return $null
    }
    $Key = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Association"
    if (Test-Path $Key) {
        $Value = (Get-ItemProperty -Path $Key -Name 'InstalledDirectory' -ErrorAction SilentlyContinue).InstalledDirectory
        if ($Value) { return $Value }
    }
    if ($env:ProgramData) {
        $List = Join-Path $env:ProgramData 'Epic/UnrealEngineLauncher/LauncherInstalled.dat'
        if (Test-Path -LiteralPath $List -PathType Leaf) {
            foreach ($Item in @((Read-Json $List).InstallationList)) {
                if ($Item.AppName -eq "UE_$Association" -and $Item.InstallLocation) { return $Item.InstallLocation }
            }
        }
    }
    return $null
}

function Invoke-Endpoint([string]$Method, [string]$Path, $Body, [int]$Port, [string]$Token, [string]$Route, [int]$Timeout = 30) {
    $Headers = @{ 'X-Dream-Token' = $Token }
    $Uri = "http://127.0.0.1:$Port$Route$Path"
    $Payload = if ($Method -eq 'POST') { if ($null -eq $Body) { '{}' } else { $Body | ConvertTo-Json -Depth 8 -Compress } } else { $null }
    try {
        return Invoke-RestMethod -Method $Method -Uri $Uri -Headers $Headers -Body $Payload -ContentType 'application/json; charset=utf-8' -TimeoutSec $Timeout
    } catch {
        $Response = $_.Exception.Response
        if ($Response -and $_.ErrorDetails.Message) { return ($_.ErrorDetails.Message | ConvertFrom-Json) }
        throw
    }
}

function Test-Answers([int]$Port, [string]$Token, [string]$Route) {
    try { $Hello = Invoke-Endpoint 'GET' '/hello' $null $Port $Token $Route 3; return $Hello.id -eq 'gui' } catch { return $false }
}

# --- the arguments -----------------------------------------------------------------------------

if ($Verb -ne 'check' -and $All) { Fail '-All goes with check only.' }
if (-not $All -and -not $File) { Fail "$Verb needs a .dui, e.g. dui.ps1 $Verb DUI/Menu.dui." }
$ProjectFile = Find-Project
if (-not $ProjectFile -or -not (Test-Path -LiteralPath $ProjectFile -PathType Leaf)) { Fail 'no project: pass -Project <path>/<Name>.uproject.' }
$ProjectDir = Split-Path -Parent $ProjectFile
$Manifest = Read-Json (Join-Path $PluginDir 'Resources/DreamMCP.json')
$Route = $Manifest.route

$Args_ = @{}
if ($All) { $Args_['all'] = $true }
if ($File) {
    # Relative to the project, as the endpoint takes it; a path given from elsewhere is made so.
    $Full = if ([System.IO.Path]::IsPathRooted($File)) { $File } else { Join-Path (Get-Location -PSProvider FileSystem).ProviderPath $File }
    if (Test-Path -LiteralPath $Full -PathType Leaf) {
        $Args_['file'] = [System.IO.Path]::GetFullPath($Full)
    } else {
        $Args_['file'] = $File
    }
}
if ($Force -and $Verb -ne 'check') { $Args_['force'] = $true }
if ($Verb -eq 'layout') {
    $Args_['width'] = $Width
    $Args_['height'] = $Height
    $Args_['depth'] = $Depth
    if ($Sizes) { $Args_['sizes'] = $Sizes }
}

# --- an endpoint: the one that answers, or a host of our own ----------------------------------

$Discovery = Join-Path $ProjectDir 'Saved/DreamMCP/gui.json'
$Port = 0
$Token = $null
$HostProcess = $null
if (Test-Path -LiteralPath $Discovery -PathType Leaf) {
    try {
        $Found = Read-Json $Discovery
        if ((Get-Process -Id $Found.pid -ErrorAction SilentlyContinue) -and (Test-Answers $Found.port $Found.token $Found.route)) {
            $Port = [int]$Found.port; $Token = [string]$Found.token
            Write-Output "dui: using the $($Found.mode) that has the project open (pid $($Found.pid))."
        }
    } catch { }
}
if ($Port -eq 0) {
    $EngineRoot = Find-Engine $ProjectFile
    $Exe = if ($EngineRoot) { Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe' } else { $null }
    if (-not $Exe -or -not (Test-Path -LiteralPath $Exe -PathType Leaf)) { Fail 'no engine: pass -Engine <engine root> (the folder that holds Engine/).' }
    $Port = 47850 + (Get-Random -Maximum 50)
    $Token = -join ((1..32) | ForEach-Object { '{0:x}' -f (Get-Random -Maximum 16) })
    $Info = New-Object System.Diagnostics.ProcessStartInfo
    $Info.FileName = $Exe
    $Info.Arguments = "`"$ProjectFile`" -run=DreamGUIHost -nullrhi -DreamMCPHost -unattended -nosplash -nopause -DreamMCPPort=$Port -DreamMCPParentPid=$PID"
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.EnvironmentVariables['DREAM_MCP_TOKEN'] = $Token
    Write-Output 'dui: starting a headless host for this call...'
    $HostProcess = [System.Diagnostics.Process]::Start($Info)
    $Deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $Started = Get-Date
    $Answering = $false
    while ((Get-Date) -lt $Deadline -and -not $HostProcess.HasExited) {
        # The host may have moved to a free port: its discovery file, once it names this process, says where.
        if (Test-Path -LiteralPath $Discovery -PathType Leaf) {
            try { $Found = Read-Json $Discovery; if ($Found.pid -eq $HostProcess.Id) { $Port = [int]$Found.port } } catch { }
        }
        if (Test-Answers $Port $Token $Route) { $Answering = $true; break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $Answering) {
        if (-not $HostProcess.HasExited) { Stop-Process -Id $HostProcess.Id -Force -ErrorAction SilentlyContinue }
        Fail "the host did not answer within $TimeoutSeconds s (exit code: $(if ($HostProcess.HasExited) { $HostProcess.ExitCode } else { 'none' }))."
    }
    Write-Output ('dui: the host answered after {0:0.0} s.' -f ((Get-Date) - $Started).TotalSeconds)
}

# --- the call ----------------------------------------------------------------------------------

$Result = $null
try {
    $Reply = Invoke-Endpoint 'POST' '/call' @{ id = "dui-$PID-$(Get-Random)"; action = $Verb; args = $Args_ } $Port $Token $Route
    if ($Reply.job) {
        while ($true) {
            $Job = Invoke-Endpoint 'GET' "/jobs/$($Reply.job)" $null $Port $Token $Route
            if ($Job.state -in @('done', 'failed', 'cancelled')) { $Result = $Job.result; break }
            Start-Sleep -Milliseconds 100
        }
    } else {
        $Result = $Reply
    }
} finally {
    if ($HostProcess -and -not $HostProcess.HasExited) {
        try { Invoke-Endpoint 'POST' '/shutdown' $null $Port $Token $Route 5 | Out-Null } catch { }
        if (-not $HostProcess.WaitForExit(30000)) { Stop-Process -Id $HostProcess.Id -Force -ErrorAction SilentlyContinue }
    }
}

if ($Json) {
    $Result | ConvertTo-Json -Depth 12
} else {
    Write-Output $Result.summary
    foreach ($D in @($Result.diagnostics)) {
        if (-not $D) { continue }
        $Where = if ($D.file) { if ($D.line) { "$($D.file)($($D.line),$($D.column))" } else { $D.file } } else { 'dui' }
        $Line = "${Where}: $($D.severity) $(if ($D.code) { "$($D.code): " })$($D.message)"
        if ($D.hint) { $Line += " (hint: $($D.hint))" }
        Write-Output $Line
    }
    foreach ($W in @($Result.writes)) { if ($W) { Write-Output "wrote $($W.asset) ($($W.state))" } }
    foreach ($N in @($Result.next)) { if ($N) { Write-Output "next: $N" } }
}
switch ($Result.outcome) {
    'ok' { Write-Output 'RESULT=OK'; exit 0 }
    'source-error' { Write-Output 'RESULT=FAILED'; exit 1 }
    default { Write-Output "RESULT=FAILED ($($Result.outcome): $($Result.reason))"; exit 2 }
}
