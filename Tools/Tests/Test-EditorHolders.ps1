#Requires -Version 7.2
# Run with: pwsh -NoProfile -File Tools\Tests\Test-EditorHolders.ps1. Needs no engine and no project.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

. (Join-Path $PSScriptRoot 'EditorHolders.ps1')

$Temp = [System.IO.Path]::GetTempPath()
$Root = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($Temp, 'DreamGUITestHost'))
$UProject = [System.IO.Path]::Combine($Root, 'DreamGUITestHost.uproject')
# A second host whose project has the same file name, as DreamGUITestHostV beside DreamGUITestHost.
$Twin = $Root + 'V'
$TwinProject = [System.IO.Path]::Combine($Twin, 'DreamGUITestHost.uproject')
$Plugin = [System.IO.Path]::Combine($Root, 'Plugins', 'DreamGUI', 'Binaries', 'Win64', 'UnrealEditor-DreamGUI.dll')
$TwinPlugin = [System.IO.Path]::Combine($Twin, 'Plugins', 'DreamGUI', 'Binaries', 'Win64', 'UnrealEditor-DreamGUI.dll')
$EngineModule = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($Temp, 'UE', 'Engine', 'Binaries', 'Win64', 'UnrealEditor-Core.dll'))

$NoCim = { throw [System.TypeInitializationException]::new('Microsoft.Management.Infrastructure.Native.ApplicationMethods', $null) }
$None = { @() }
function New-Cim([int]$Id, [string]$CommandLine) { [pscustomobject]@{ ProcessId = $Id; CommandLine = $CommandLine } }
function New-Editor([int]$Id, [string[]]$Files) {
    [pscustomobject]@{ Id = $Id; Modules = @($Files | ForEach-Object { [pscustomobject]@{ FileName = $_ } }) }
}
function New-Hidden([int]$Id) {
    $Process = [pscustomobject]@{ Id = $Id }
    $Process | Add-Member -MemberType ScriptProperty -Name Modules -Value { throw [System.ComponentModel.Win32Exception]::new(5) }
    $Process
}

# Scriptblocks resolve the fixtures above when the function invokes them.
$Cases = @(
    @{ Name = 'CIM finds the editor by its full project path'; Cim = { @((New-Cim 10 "UnrealEditor.exe `"$UProject`" -log"), (New-Cim 11 'UnrealEditor.exe Other.uproject')) }
       Processes = $None; Method = 'CIM'; Holders = @(10); Unchecked = @() }
    @{ Name = 'CIM matches forward slashes and another case'; Cim = { @(New-Cim 12 ("UnrealEditor-Cmd.exe " + $UProject.Replace('\', '/').ToUpperInvariant())) }
       Processes = $None; Method = 'CIM'; Holders = @(12); Unchecked = @() }
    @{ Name = 'CIM does not take a same-named project elsewhere'; Cim = { @(New-Cim 13 "UnrealEditor.exe `"$TwinProject`"") }
       Processes = $None; Method = 'CIM'; Holders = @(); Unchecked = @() }
    @{ Name = 'CIM editor without a readable command line'; Cim = { @(New-Cim 14 $null) }
       Processes = $None; Method = 'CIM'; Holders = @(); Unchecked = @() }
    @{ Name = 'without CIM, an editor holding a project DLL'; Cim = $NoCim; Processes = { @(New-Editor 20 @($EngineModule, $Plugin)) }
       Method = 'Modules'; Holders = @(20); Unchecked = @() }
    @{ Name = 'without CIM, an editor on another project'; Cim = $NoCim; Processes = { @(New-Editor 21 @($EngineModule)) }
       Method = 'Modules'; Holders = @(); Unchecked = @() }
    @{ Name = 'without CIM, the twin host whose directory shares the prefix'; Cim = $NoCim; Processes = { @(New-Editor 22 @($TwinPlugin)) }
       Method = 'Modules'; Holders = @(); Unchecked = @() }
    @{ Name = 'without CIM, modules that cannot be read'; Cim = $NoCim; Processes = { @((New-Hidden 23), (New-Editor 24 @($Plugin))) }
       Method = 'Modules'; Holders = @(24); Unchecked = @(23) }
    @{ Name = 'without CIM or editors'; Cim = $NoCim; Processes = $None
       Method = 'Modules'; Holders = @(); Unchecked = @() }
)
foreach ($Case in $Cases) {
    $Found = Get-DreamGUIEditorHolders -UProject $UProject -QueryCim $Case.Cim -QueryProcesses $Case.Processes
    $Same = $Found.Method -eq $Case.Method -and (@($Found.Holders) -join ',') -eq (@($Case.Holders) -join ',') -and
        (@($Found.Unchecked) -join ',') -eq (@($Case.Unchecked) -join ',')
    if (-not $Same) { throw "$($Case.Name): unexpected result $($Found | ConvertTo-Json -Compress)" }
    if ($Case.Method -eq 'Modules' -and -not $Found.CimError) { throw "$($Case.Name): the CIM failure must be kept" }
    Write-Output "PASS $($Case.Name)"
}

# The real queries, whichever this machine can use, answer without throwing.
$Real = Get-DreamGUIEditorHolders -UProject $UProject
if ($Real.Method -notin @('CIM', 'Modules')) { throw "the real query answered $($Real | ConvertTo-Json -Compress)" }
Write-Output "PASS the real query answers by $($Real.Method)"
Write-Output "$($Cases.Count + 1) editor-holder regressions passed."
