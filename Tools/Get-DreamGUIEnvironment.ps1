#Requires -Version 5.1
<#
.SYNOPSIS
    Prints the facts a DreamGUI bug report needs: plugin version and commit, engine version and
    kind, operating system, UI language and shell.

.DESCRIPTION
    The output is plain text with fixed keys, meant to be pasted into an issue as it is. Nothing is
    started: no editor, no build, no network. A value that cannot be found is printed as `unknown`;
    the script never fails on a missing value, and it never prints a path.

    It works without git (a zip download) and in Windows PowerShell 5.1 as well as PowerShell 7.

    The plugin is the one this script sits in. The project is -Project, else DREAMGUI_TEST_PROJECT,
    else the .uproject two or three folders above the plugin (<project>/Plugins/DreamGUI or
    <project>/Plugins/<group>/DreamGUI), else the .uproject in the current folder or the nearest
    folder above it. The engine is -Engine, else DREAMGUI_ENGINE, else the
    engine the plugin sits inside, else the engine the project's EngineAssociation names in the
    registry (a launcher install by version, a source build by its id).

.PARAMETER Project
    The .uproject whose engine to report. The same parameter, and the same environment variable, as
    Tools/Tests/Invoke-DreamGUITests.ps1.
.PARAMETER Engine
    The engine root (the folder that holds Engine/). The same parameter, and the same environment
    variable, as Tools/Tests/Invoke-DreamGUITests.ps1.

.EXAMPLE
    pwsh -NoProfile -File Plugins/DreamGUI/Tools/Get-DreamGUIEnvironment.ps1
.EXAMPLE
    pwsh -NoProfile -File Tools/Get-DreamGUIEnvironment.ps1 -Project <path to>/MyGame.uproject
#>
[CmdletBinding()]
param(
    [string]$Project,
    [string]$Engine
)

$ErrorActionPreference = 'Stop'
$Unknown = 'unknown'

$PluginDir = Split-Path -Parent $PSScriptRoot

function Get-Safe([scriptblock]$Block) {
    try {
        $Value = & $Block
        if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) { return $Unknown }
        return [string]$Value
    } catch {
        return $Unknown
    }
}

function Get-Ancestor([string]$Path, [int]$Levels) {
    $Current = $Path
    for ($i = 0; $i -lt $Levels; $i++) {
        if (-not $Current) { return $null }
        $Current = Split-Path -Parent $Current
    }
    return $Current
}

function Read-Json([string]$Path) {
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
}

# --- the plugin -------------------------------------------------------------------------------

$PluginVersion = Get-Safe { (Read-Json (Join-Path $PluginDir 'DreamGUI.uplugin')).VersionName }

$PluginCommit = Get-Safe {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { return $null }
    # Windows PowerShell turns a native command's stderr into an error record; that is not a failure here.
    $ErrorActionPreference = 'Continue'
    # Only the plugin's own repository counts: a plugin copied into a game's repository is not it.
    $Top = git -C $PluginDir rev-parse --show-toplevel 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $Top) { return $null }
    $Same = [string]::Equals(
        [System.IO.Path]::GetFullPath($Top).TrimEnd('\', '/'),
        [System.IO.Path]::GetFullPath($PluginDir).TrimEnd('\', '/'),
        [System.StringComparison]::OrdinalIgnoreCase)
    if (-not $Same) { return $null }
    $Hash = git -C $PluginDir rev-parse --short HEAD 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $Hash) { return $null }
    $Branch = git -C $PluginDir symbolic-ref --short -q HEAD 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $Branch) { $Branch = 'detached' }
    $Changes = git -C $PluginDir status --porcelain --untracked-files=no 2>$null
    $Dirty = if ($Changes) { ' dirty' } else { '' }
    return "$Hash ($Branch)$Dirty"
}

# --- the project and the engine ---------------------------------------------------------------

function Find-Project {
    if ($Project) { return $Project }
    if ($env:DREAMGUI_TEST_PROJECT) { return $env:DREAMGUI_TEST_PROJECT }
    foreach ($Levels in 2, 3) {
        $Found = Find-ProjectIn (Get-Ancestor $PluginDir $Levels)
        if ($Found) { return $Found }
    }
    # A copy of the plugin outside the project's Plugins folder: the project the shell is in.
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

function Find-ProjectIn([string]$Dir) {
    if (-not $Dir -or -not (Test-Path -LiteralPath $Dir -PathType Container)) { return $null }
    $Found = @(Get-ChildItem -LiteralPath $Dir -Filter '*.uproject' -File -ErrorAction SilentlyContinue)
    if ($Found.Count -eq 1) { return $Found[0].FullName }
    return $null
}

function Test-EngineRoot([string]$Root) {
    return $Root -and (Test-Path -LiteralPath (Join-Path $Root 'Engine/Build/Build.version') -PathType Leaf)
}

function Find-EngineAbove([string]$Path) {
    $Current = $Path
    while ($Current) {
        if (Test-EngineRoot $Current) { return $Current }
        $Parent = Split-Path -Parent $Current
        if ($Parent -eq $Current) { break }
        $Current = $Parent
    }
    return $null
}

function Find-EngineOfProject([string]$ProjectFile) {
    if (-not $ProjectFile -or -not (Test-Path -LiteralPath $ProjectFile -PathType Leaf)) { return $null }
    $Association = [string](Read-Json $ProjectFile).EngineAssociation
    if (-not $Association) {
        # No association: the project lives inside the engine's folder tree.
        return Find-EngineAbove (Split-Path -Parent $ProjectFile)
    }
    if ($Association.StartsWith('{')) {
        $Key = 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds'
        if (Test-Path $Key) {
            $Value = (Get-ItemProperty -Path $Key -Name $Association -ErrorAction SilentlyContinue).$Association
            if ($Value) { return $Value }
        }
        return $null
    }
    $Key = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Association"
    if (Test-Path $Key) {
        $Value = (Get-ItemProperty -Path $Key -Name 'InstalledDirectory' -ErrorAction SilentlyContinue).InstalledDirectory
        if ($Value) { return $Value }
    }
    # Newer launcher installs are listed only in the launcher's own install list.
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

$EngineRoot = $null
try {
    if ($Engine) { $EngineRoot = $Engine }
    elseif ($env:DREAMGUI_ENGINE) { $EngineRoot = $env:DREAMGUI_ENGINE }
    else {
        $EngineRoot = Find-EngineAbove $PluginDir
        if (-not $EngineRoot) { $EngineRoot = Find-EngineOfProject (Find-Project) }
    }
} catch {
    $EngineRoot = $null
}
if (-not (Test-EngineRoot $EngineRoot)) { $EngineRoot = $null }

$EngineVersion = Get-Safe {
    if (-not $EngineRoot) { return $null }
    $Build = Read-Json (Join-Path $EngineRoot 'Engine/Build/Build.version')
    return '{0}.{1}.{2}' -f $Build.MajorVersion, $Build.MinorVersion, $Build.PatchVersion
}

$EngineKind = Get-Safe {
    if (-not $EngineRoot) { return $null }
    if (Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Build/InstalledBuild.txt') -PathType Leaf) { return 'launcher' }
    if (Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Source/Runtime') -PathType Container) { return 'source' }
    return $null
}

# --- the machine ------------------------------------------------------------------------------

$OsName = Get-Safe {
    $Key = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    if (Test-Path $Key) {
        $Info = Get-ItemProperty -Path $Key
        $Name = [string]$Info.ProductName
        $Build = [int]$Info.CurrentBuild
        # Windows 11 still reports "Windows 10" as its product name; the build number tells them apart.
        if ($Build -ge 22000) { $Name = $Name -replace '^Windows 10', 'Windows 11' }
        $Release = [string]$Info.DisplayVersion
        $Text = $Name
        if ($Release) { $Text += " $Release" }
        $Text += " (build $Build"
        if ($null -ne $Info.UBR) { $Text += ".$($Info.UBR)" }
        return "$Text)"
    }
    return [System.Runtime.InteropServices.RuntimeInformation]::OSDescription.Trim()
}

$Locale = Get-Safe { (Get-UICulture).Name }

$Shell = Get-Safe {
    $Name = if ($PSVersionTable.PSEdition -eq 'Core') { 'pwsh' } else { 'powershell' }
    return "$Name $($PSVersionTable.PSVersion)"
}

# --- the report -------------------------------------------------------------------------------

Write-Output 'DreamGUI environment'
Write-Output "plugin.version : $PluginVersion"
Write-Output "plugin.commit  : $PluginCommit"
Write-Output "engine.version : $EngineVersion"
Write-Output "engine.kind    : $EngineKind"
Write-Output "os             : $OsName"
Write-Output "locale         : $Locale"
Write-Output "shell          : $Shell"
exit 0
