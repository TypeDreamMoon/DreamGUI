#Requires -Version 7.2
<#
.SYNOPSIS
    Installs, or with -Uninstall removes, the DreamGUI pre-push hook of this repository.

.DESCRIPTION
    Writes a small forwarding stub as pre-push in the repository's hooks directory. The stub runs
    Tools/Tests/hooks/pre-push from whichever checkout is pushing, so every worktree of the
    repository runs the version of the hook its own branch carries, and a checkout without the file
    pushes as before.

    The hooks directory is core.hooksPath when that is configured, else <git common dir>\hooks,
    which all worktrees of the repository share (git rev-parse --git-common-dir).

    A pre-push that is not this stub is never overwritten or removed unless -Force; -WhatIf shows
    what would happen.

    What the hook does, and how to skip it (DREAMGUI_SKIP_PREPUSH=1), is in Tools/Tests/README.md.

.PARAMETER Force
    Replace (or, with -Uninstall, remove) a pre-push hook that is not this stub.
.PARAMETER Uninstall
    Remove the stub.
.PARAMETER RepoPath
    Any directory inside the repository. Default: the checkout this script lives in.

.EXAMPLE
    pwsh -NoProfile -File Tools\Tests\Install-DreamGUIHooks.ps1 -WhatIf
.EXAMPLE
    pwsh -NoProfile -File Tools\Tests\Install-DreamGUIHooks.ps1 -Uninstall
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [switch]$Force,
    [switch]$Uninstall,
    [string]$RepoPath
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$Marker = 'dreamgui-prepush-stub'
$Stub = @"
#!/bin/sh
# $Marker -- installed by Tools/Tests/Install-DreamGUIHooks.ps1; remove it with -Uninstall.
# Runs the pre-push hook of the checkout that pushes, so each worktree runs its own branch's version.
top=`$(git rev-parse --show-toplevel 2>/dev/null) || exit 0
hook="`$top/Tools/Tests/hooks/pre-push"
if [ -f "`$hook" ]; then
	exec sh "`$hook" "`$@"
fi
exit 0
"@

function Invoke-Git([string[]]$Arguments) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & git @Arguments 2>$null
    } finally {
        $ErrorActionPreference = $saved
    }
    if ($LASTEXITCODE -ne 0) { return $null }
    return (@($out) | Select-Object -First 1)
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Error 'git is not on PATH.'
    exit 1
}
$repo = if ($RepoPath) { $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($RepoPath) } else { Join-Path $PSScriptRoot '..\..' }
$top = Invoke-Git @('-C', $repo, 'rev-parse', '--show-toplevel')
if (-not $top) {
    Write-Error "$repo is not inside a git working tree."
    exit 1
}

$configured = Invoke-Git @('-C', $top, 'config', '--get', 'core.hooksPath')
if ($configured) {
    # A relative core.hooksPath is taken from the directory hooks run in: the top of the work tree.
    $hooksDir = if ([IO.Path]::IsPathRooted($configured)) { $configured } else { Join-Path $top $configured }
    Write-Host "core.hooksPath is set; using $hooksDir"
} else {
    $common = Invoke-Git @('-C', $top, 'rev-parse', '--path-format=absolute', '--git-common-dir')
    if (-not $common) {
        Write-Error 'git rev-parse --git-common-dir failed.'
        exit 1
    }
    $hooksDir = Join-Path $common 'hooks'
}
$target = Join-Path $hooksDir 'pre-push'
$existing = if (Test-Path -LiteralPath $target -PathType Leaf) { [IO.File]::ReadAllText($target) } else { $null }
$ours = $null -ne $existing -and $existing.Contains($Marker)

if ($Uninstall) {
    if ($null -eq $existing) {
        Write-Host "No pre-push hook at $target; nothing to remove."
        exit 0
    }
    if (-not $ours -and -not $Force) {
        Write-Warning "$target is not the DreamGUI stub; left alone (pass -Force to remove it anyway)."
        exit 1
    }
    if ($PSCmdlet.ShouldProcess($target, 'Remove the pre-push hook')) {
        Remove-Item -LiteralPath $target
        Write-Host "Removed $target."
    }
    exit 0
}

if ($null -ne $existing -and -not $ours -and -not $Force) {
    Write-Warning "$target already exists and is not the DreamGUI stub; left alone. Merge it by hand, or pass -Force to replace it."
    exit 1
}
if ($PSCmdlet.ShouldProcess($target, $(if ($ours) { 'Refresh the DreamGUI pre-push stub' } else { 'Install the DreamGUI pre-push stub' }))) {
    if (-not (Test-Path -LiteralPath $hooksDir -PathType Container)) { New-Item -ItemType Directory -Force -Path $hooksDir | Out-Null }
    # sh reads a carriage return as part of the command: LF only, no byte order mark.
    [IO.File]::WriteAllText($target, $Stub.Replace("`r`n", "`n") + "`n", [Text.UTF8Encoding]::new($false))
    Write-Host "Installed $target."
    Write-Host 'Every push of a branch whose commit is checked out now runs the Quick preset first; DREAMGUI_SKIP_PREPUSH=1 skips it.'
}
exit 0
