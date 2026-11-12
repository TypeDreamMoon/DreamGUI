#Requires -Version 7.0
<#
.SYNOPSIS
    Builds .skill/reference/ from Docs/: the pages an agent reads before writing a .dui, each one standing alone.

.DESCRIPTION
    An agent reads the references through DreamMCP (the manifest's `reference` list) or straight from disk, and either way it
    cannot follow a relative link. So each reference page is made from the manual's pages with every link that would need
    another file turned into something that does not:

      dui.md        the language: Docs/language/*.md in reading order, as one page. A link to a section of another
                    language page becomes a link within this page.
      example.md    Docs/language/example.md
      pitfalls.md   Docs/guides/pitfalls.md
      controls.md   Docs/controls/index.md
      diagnostics.md Docs/diagnostics/index.md: every DUInnnn code

    Every other relative link points at the published manual (https://gui.toolchain.64hz.cn/en/docs/...), which an agent
    that can browse may open and one that cannot does not need. The front matter goes; a note at the top says the page
    stands alone. Docs/ stays the source: edit it, then run this again. -Check reports drift and exits 1.

.EXAMPLE
    pwsh -NoProfile -File .skill/build-reference.ps1
.EXAMPLE
    pwsh -NoProfile -File .skill/build-reference.ps1 -Check
#>
[CmdletBinding()]
param([switch]$Check)

$ErrorActionPreference = 'Stop'
$PluginDir = Split-Path -Parent $PSScriptRoot
$Docs = Join-Path $PluginDir 'Docs'
$Out = Join-Path $PSScriptRoot 'reference'
$Site = 'https://gui.toolchain.64hz.cn/en/docs'
$Repository = 'https://github.com/TypeDreamMoon/DreamGUI/blob/main'

$LanguageOrder = @('overview', 'nodes-and-values', 'slots-and-components', 'styles-and-resources', 'use', 'components',
    'viewmodels', 'bindings', 'control-flow', 'rows', 'timelines', 'write-back')

# Docs/<folder>/<page>.md -> its address on the published manual (the site files some folders under other names).
function Get-ManualUrl([string]$DocsRelative, [string]$Anchor) {
    $Path = $DocsRelative -replace '\\', '/' -replace '\.md$', ''
    $Parts = $Path.Split('/')
    $Folder = if ($Parts.Count -gt 1) { $Parts[0] } else { '' }
    $Page = $Parts[-1]
    $Mapped = switch ($Folder) {
        'language' { "dui/$Page" }
        'getting-started' { "start/$Page" }
        'guides' { if ($Page -in @('pitfalls', 'property-meta', 'layered-document-import')) { "plugin/guides/$Page" } else { "guides/$Page" } }
        'Reference' { "reference/$($Page.ToLowerInvariant())" }
        '' { '' }
        default { "$Folder/$Page" }
    }
    $Mapped = $Mapped -replace '/index$', ''
    $Url = if ($Mapped) { "$Site/$Mapped/" } else { "$Site/" }
    if ($Anchor) { $Url += $Anchor }
    return $Url
}

# The text of a Docs page with its front matter removed and its links rewritten. $InPage: the language pages merged into this
# page, whose sections a link may point to directly.
function Convert-Page([string]$DocsRelative, [string[]]$InPage) {
    $Text = Get-Content -LiteralPath (Join-Path $Docs $DocsRelative) -Raw -Encoding UTF8
    $Text = $Text -replace "`r`n", "`n"
    $Text = $Text -replace '(?s)^---\n.*?\n---\n+', ''
    $Here = Split-Path -Parent $DocsRelative
    $Text = [regex]::Replace($Text, '\]\((?!https?://|#|mailto:)([^)\s#]+\.md)(#[^)\s]*)?\)', {
        param($M)
        $Target = $M.Groups[1].Value
        $Anchor = $M.Groups[2].Value
        $Resolved = [System.IO.Path]::GetFullPath((Join-Path (Join-Path $Docs $Here) $Target))
        $Relative = [System.IO.Path]::GetRelativePath($Docs, $Resolved) -replace '\\', '/'
        if ($InPage -contains $Relative) {
            if ($Anchor) { return "]($Anchor)" }
            $Slug = ([System.IO.Path]::GetFileNameWithoutExtension($Relative))
            return "](#$Slug)"
        }
        return "]($(Get-ManualUrl $Relative $Anchor))"
    })
    # A link to a file of the repository that is not a page (a header, a sample): the file on GitHub.
    $Text = [regex]::Replace($Text, '\]\((?!https?://|#|mailto:)([^)\s#]+)(#[^)\s]*)?\)', {
        param($M)
        $Resolved = [System.IO.Path]::GetFullPath((Join-Path (Join-Path $Docs $Here) $M.Groups[1].Value))
        $Relative = [System.IO.Path]::GetRelativePath($PluginDir, $Resolved) -replace '\\', '/'
        return "]($Repository/$Relative$($M.Groups[2].Value))"
    })
    return $Text.TrimEnd() + "`n"
}

function Get-Note([string]$Title, [string]$Source) {
    return @"
# $Title

> This page stands on its own: what an author needs is here, and an agent that cannot open its links loses nothing it
> needs. The links lead to the online manual for depth. Look names up (DreamMCP's ``lookup``, or
> ``DUI/.dui-symbols.json``) instead of guessing them. Built from ``$Source`` by ``.skill/build-reference.ps1``.

"@
}

$Pages = [ordered]@{}

$LanguageFiles = @($LanguageOrder | ForEach-Object { "language/$_.md" })
$Body = Get-Note 'The .dui language' 'Docs/language/*.md'
foreach ($File in $LanguageFiles) {
    $Body += "`n" + (Convert-Page $File $LanguageFiles)
}
$Pages['dui.md'] = $Body
$Pages['example.md'] = (Get-Note 'A worked example' 'Docs/language/example.md') + (Convert-Page 'language/example.md' @())
$Pages['pitfalls.md'] = (Get-Note 'Common pitfalls' 'Docs/guides/pitfalls.md') + (Convert-Page 'guides/pitfalls.md' @())
$Pages['controls.md'] = (Get-Note 'Controls' 'Docs/controls/index.md') + (Convert-Page 'controls/index.md' @())
$Pages['diagnostics.md'] = (Get-Note 'Diagnostics' 'Docs/diagnostics/index.md') + (Convert-Page 'diagnostics/index.md' @())

$Drift = 0
if (-not $Check) { New-Item -ItemType Directory -Force -Path $Out | Out-Null }
foreach ($Name in $Pages.Keys) {
    $Path = Join-Path $Out $Name
    # The notes come from here-strings, whose line ends follow this script's; the pages are LF by now. CRLF for all.
    $Wanted = ($Pages[$Name] -replace "`r`n", "`n") -replace "`n", "`r`n"
    $Current = if (Test-Path -LiteralPath $Path) { [System.IO.File]::ReadAllText($Path) } else { $null }
    if ($Current -ceq $Wanted) {
        Write-Host "  = reference/$Name" -ForegroundColor DarkGray
        continue
    }
    $Drift++
    if ($Check) {
        Write-Host "  ! reference/$Name  [$(if ($null -eq $Current) { 'missing' } else { 'stale' })]" -ForegroundColor Yellow
        continue
    }
    [System.IO.File]::WriteAllText($Path, $Wanted, [System.Text.UTF8Encoding]::new($false))
    Write-Host "  + reference/$Name" -ForegroundColor Green
}
if ($Check) {
    if ($Drift -eq 0) { Write-Host 'build-reference: up to date' -ForegroundColor Green; exit 0 }
    Write-Host "build-reference: $Drift page(s) out of date; run .skill/build-reference.ps1" -ForegroundColor Yellow
    exit 1
}
Write-Host "build-reference: $($Pages.Count) page(s), $Drift written" -ForegroundColor Green
exit 0
