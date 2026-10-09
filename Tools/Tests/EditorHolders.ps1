#Requires -Version 7.2
# The Unreal editors that have a project open: a build cannot replace the DLLs they hold (LNK1104), and a second editor
# on the project shares its Saved directory. Dot-sourced by the test runner, the nightly and the bench launcher.
#
# Win32_Process gives each editor's command line, which names the .uproject by its full path; a project of the same name
# elsewhere -- a second test host -- is not this one. CIM is not always there: in a shell that cannot load WMI's native
# client, Get-CimInstance throws while its type is initialized, which no -ErrorAction catches. Without it, an editor
# holds the project when it has loaded a module from inside the project's directory, the very DLLs a build replaces. A
# process whose modules cannot be read is reported as unchecked rather than taken for either.

function Get-DreamGUIEditorHolders {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$UProject,

        # What CIM and the process list return; the tests give their own.
        [scriptblock]$QueryCim = { Get-CimInstance -ClassName Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction Stop },
        [scriptblock]$QueryProcesses = { Get-Process -Name 'UnrealEditor*' -ErrorAction SilentlyContinue }
    )

    $FullProject = [System.IO.Path]::GetFullPath($UProject)
    $Needle = $FullProject.Replace('/', '\').ToLowerInvariant()
    $Holders = [System.Collections.Generic.List[int]]::new()
    $Unchecked = [System.Collections.Generic.List[int]]::new()
    try {
        foreach ($Process in @(& $QueryCim)) {
            $Line = [string]$Process.CommandLine
            if ($Line -and $Line.Replace('/', '\').ToLowerInvariant().Contains($Needle)) { $Holders.Add([int]$Process.ProcessId) }
        }
        return [pscustomobject]@{ Method = 'CIM'; Holders = $Holders.ToArray(); Unchecked = $Unchecked.ToArray(); CimError = $null }
    }
    catch {
        $CimError = $_.Exception.Message
        $Holders.Clear()
    }

    # The directory with its separator, so a sibling such as <Host>V does not count as inside <Host>.
    $Directory = [System.IO.Path]::TrimEndingDirectorySeparator([System.IO.Path]::GetDirectoryName($FullProject)) + [System.IO.Path]::DirectorySeparatorChar
    foreach ($Process in @(& $QueryProcesses)) {
        $Files = $null
        try {
            $Files = @($Process.Modules | ForEach-Object { $_.FileName })
        }
        catch {
            $Unchecked.Add([int]$Process.Id)
            continue
        }
        if (@($Files | Where-Object { $_ -and ([string]$_).StartsWith($Directory, [System.StringComparison]::OrdinalIgnoreCase) }).Count -gt 0) {
            $Holders.Add([int]$Process.Id)
        }
    }
    [pscustomobject]@{ Method = 'Modules'; Holders = $Holders.ToArray(); Unchecked = $Unchecked.ToArray(); CimError = $CimError }
}
