#Requires -Version 7.0
<#
.SYNOPSIS
    Regenerates the knowledge files an agent reads first: the diagnostics JSON and the cheat sheet.

.DESCRIPTION
    Writes two things, none of them by hand:

    Docs/diagnostics/codes.json
        Every live DUInnnn as data: code, name, severity, stage, title, meaning, fix and doc (the code's
        anchor on the documentation site). It is read from the "Every code" table of
        Docs/diagnostics/index.md (code, name, severity, meaning) and its families table (the stage); the
        fix of a layout code comes from the table in .skill/dream-gui-diagnose/SKILL.md. Before writing, the
        run checks that the table, Docs/diagnostics/codes.md and EDreamUIDiagnosticCode
        (Source/DreamGUI/Public/Text/DreamUIDiagnostics.h) list the same codes, and that each family's count
        is right: a code added to one and not the others stops it. Retired codes are left out.
        DreamMCP lists the file as a reference document and the documentation site publishes it.

    Docs/cheatsheet.md
        One page of syntax and common forms, expanded from .skill/cheatsheet/cheatsheet.src.md. The
        source is ordinary Markdown with three directives, each on a line of its own:

          <!-- block: <path> #<heading anchor> [N] -->   the Nth fenced block (default 1) under that
                                                          heading of a page in the repository
          <!-- names: <path> #<heading anchor> -->        the first column of the first table under
                                                          that heading, as one line of `names`
          <!-- codes: DUInnnn DUInnnn ... -->              a table of those codes and what they mean

        .skill/build-reference.ps1 copies it into .skill/reference/cheatsheet.md, where
        DreamGUI.MCP.Reference.* compiles and lays out every block whose first line is
        `// DUI/<path>.dui`, like the other reference pages.

    Run it after changing the cheat sheet source, a page it quotes, or a diagnostic. -Check writes nothing
    and exits 1 when either is out of date; CI runs it that way.

.EXAMPLE
    ./gen-knowledge.ps1

.EXAMPLE
    ./gen-knowledge.ps1 -Check
#>
[CmdletBinding()]
param(
    # Report drift and exit non-zero instead of writing.
    [switch]$Check
)

$ErrorActionPreference = 'Stop'

$plugin = 'DreamGUI'
$prefix = 'DUI'

# ---------------------------------------------------------------- shared

$pluginRoot = Split-Path -Parent $PSScriptRoot
$docsRoot = Join-Path $pluginRoot 'Docs'
$diagnosticsRoot = Join-Path $docsRoot 'diagnostics'
$cheatSource = Join-Path $PSScriptRoot 'cheatsheet/cheatsheet.src.md'

function Convert-ToLf([string]$Text) { $Text -replace "`r`n", "`n" }

function Read-Text([string]$Path) { Convert-ToLf ([System.IO.File]::ReadAllText($Path)) }

$drift = [System.Collections.Generic.List[string]]::new()

function Write-Output-File {
    <#
        Compares with line endings normalized and writes in the ending the existing file has (LF for a new
        one), so a checkout with either ending is not drift forever.
    #>
    param([string]$Path, [string]$Text)
    $label = [System.IO.Path]::GetRelativePath($pluginRoot, $Path) -replace '\\', '/'
    $lf = Convert-ToLf $Text
    $existing = if (Test-Path -LiteralPath $Path) { [System.IO.File]::ReadAllText($Path) } else { $null }
    if ($null -ne $existing -and (Convert-ToLf $existing) -ceq $lf) { return }
    $drift.Add($label)
    if ($Check) { return }
    $eol = if ($null -ne $existing -and $existing.Contains("`r`n")) { "`r`n" } else { "`n" }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    [System.IO.File]::WriteAllText($Path, ($lf -replace "`n", $eol), [System.Text.UTF8Encoding]::new($false))
    Write-Host "  wrote $label" -ForegroundColor Gray
}

function Get-Cells([string]$Row) {
    # A Markdown table row's cells, with `\|` kept inside a cell.
    $inner = $Row.Trim() -replace '^\|', '' -replace '\|$', ''
    return @([regex]::Split($inner, '(?<!\\)\|') | ForEach-Object { $_.Trim() -replace '\\\|', '|' })
}

# ---------------------------------------------------------------- diagnostics

$index = Read-Text (Join-Path $diagnosticsRoot 'index.md')

$stages = [System.Collections.Generic.List[object]]::new()
$stageOf = @{}
$declaredCount = @{}
foreach ($m in [regex]::Matches($index, '(?m)^\| \[(DUI(\d)xxx)\]\((\S+?)\) \|.*$')) {
    $cells = Get-Cells $m.Value
    $stages.Add([ordered]@{ range = $m.Groups[1].Value; title = $cells[1]; meaning = $cells[2]; doc = $m.Groups[3].Value })
    $stageOf[$m.Groups[2].Value] = $cells[1]
    $declaredCount[$m.Groups[2].Value] = [int]$cells[3]
}
if ($stages.Count -eq 0) { throw 'Docs/diagnostics/index.md: no families table.' }

# Fixes the diagnose skill gives, by code: | DUI8001 off the screen | cause | fix |
$skillFix = @{}
$skill = Read-Text (Join-Path $PSScriptRoot 'dream-gui-diagnose/SKILL.md')
foreach ($m in [regex]::Matches($skill, '(?m)^\| (DUI\d{4})\b[^|]*\|.*$')) {
    $cells = Get-Cells $m.Value
    if ($cells.Count -ge 3) { $skillFix[$m.Groups[1].Value] = @{ cause = $cells[1]; fix = $cells[2] } }
}

$entries = [ordered]@{}
$tableCodes = [System.Collections.Generic.List[string]]::new()
foreach ($m in [regex]::Matches($index, '(?m)^\| \[(DUI(\d)\d{3})\]\((\S+?)\) \|.*$')) {
    $cells = Get-Cells $m.Value
    $code = $m.Groups[1].Value
    $tableCodes.Add($code)
    $name = $cells[1].Trim('`')
    if ($name -like '*_Retired') { continue }
    $meaning = $cells[3]
    $title = ($meaning -split '(?<=[.;:])\s', 2)[0].TrimEnd(';', ':')
    # A fifth column, Fix, when the table has one, wins over the diagnose skill's.
    $fix = if ($cells.Count -ge 5 -and $cells[4]) { $cells[4] } else { $null }
    if ($skillFix.ContainsKey($code)) {
        $meaning = "$meaning Most often: $($skillFix[$code].cause)."
        if (-not $fix) { $fix = $skillFix[$code].fix }
    }
    $entries[$code] = [ordered]@{
        code     = $code
        name     = $name
        severity = $cells[2]
        stage    = $stageOf[$m.Groups[2].Value]
        title    = $title
        meaning  = $meaning
        fix      = $fix
        doc      = $m.Groups[3].Value
    }
}

# The table, codes.md and the enum have to agree, and so do the family counts.
$enumText = Read-Text (Join-Path $pluginRoot 'Source/DreamGUI/Public/Text/DreamUIDiagnostics.h')
$enumCodes = @([regex]::Matches($enumText, '(?m)^\s+\w+ = (\d{4}),?') | ForEach-Object { "DUI$($_.Groups[1].Value)" } | Sort-Object -Unique)
$codesMd = @([regex]::Matches((Read-Text (Join-Path $diagnosticsRoot 'codes.md')), '(?m)^\| (DUI\d{4}) \|') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
$table = @($tableCodes | Sort-Object -Unique)
$problems = @()
foreach ($pair in @(@('EDreamUIDiagnosticCode', $enumCodes), @('Docs/diagnostics/codes.md', $codesMd))) {
    $missing = @($pair[1] | Where-Object { $_ -notin $table })
    $extra = @($table | Where-Object { $_ -notin $pair[1] })
    if ($missing) { $problems += "in $($pair[0]) but not in the table of Docs/diagnostics/index.md: $($missing -join ', ')" }
    if ($extra) { $problems += "in the table of Docs/diagnostics/index.md but not in $($pair[0]): $($extra -join ', ')" }
}
foreach ($digit in $declaredCount.Keys) {
    $actual = @($table | Where-Object { $_.Substring(3, 1) -eq $digit }).Count
    if ($actual -ne $declaredCount[$digit]) { $problems += "Docs/diagnostics/index.md says DUI${digit}xxx has $($declaredCount[$digit]) code(s); the table has $actual" }
}
if ($problems) {
    $problems | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    Write-Host 'The diagnostic codes disagree; add the code everywhere (AGENTS.md, Rules).' -ForegroundColor Red
    exit 1
}

$document = [ordered]@{
    schema      = 1
    plugin      = $plugin
    prefix      = $prefix
    description = 'Every DUI diagnostic DreamGUI can report. name is the EDreamUIDiagnosticCode entry; title is the first sentence of meaning, which is the documentation''s; fix is given where the documentation has one (the layout codes); doc is the code on the documentation site.'
    generatedBy = '.skill/gen-knowledge.ps1 from Docs/diagnostics/index.md and .skill/dream-gui-diagnose/SKILL.md'
    stages      = $stages
    codes       = @($entries.Values)
}
$json = ($document | ConvertTo-Json -Depth 6) + "`n"
Write-Output-File -Path (Join-Path $diagnosticsRoot 'codes.json') -Text $json
Write-Host "Found $($entries.Count) live diagnostic code(s)." -ForegroundColor DarkGray

# ---------------------------------------------------------------- cheat sheet

function Get-Anchor([string]$Heading) {
    # GitHub's and the site's slug: lower case, punctuation dropped, spaces to hyphens.
    $text = $Heading.Trim().ToLowerInvariant() -replace '`', ''
    $text = [regex]::Replace($text, '[^\p{L}\p{Nd}\s_-]', '')
    return ($text -replace '\s', '-')
}

function Get-Section([string]$RelativePath, [string]$Anchor) {
    $path = Join-Path $pluginRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) { throw "cheat sheet: '$RelativePath' does not exist." }
    $lines = (Read-Text $path) -split "`n"
    $start = -1; $level = 0; $inFence = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\s*```') { $inFence = -not $inFence; continue }
        if ($inFence) { continue }
        if ($lines[$i] -match '^(#{1,6})\s+(.*?)\s*$') {
            if ($start -ge 0 -and $Matches[1].Length -le $level) { return ($lines[$start..($i - 1)] -join "`n") }
            if ($start -lt 0 -and (Get-Anchor $Matches[2]) -eq $Anchor) { $start = $i + 1; $level = $Matches[1].Length }
        }
    }
    if ($start -lt 0) { throw "cheat sheet: '$RelativePath' has no heading #$Anchor." }
    return ($lines[$start..($lines.Count - 1)] -join "`n")
}

function Get-Fences([string]$Text) {
    [regex]::Matches($Text, '(?ms)^```[^\n]*\n.*?^```[ \t]*$') | ForEach-Object { $_.Value }
}

$messages = @{}
foreach ($entry in $entries.Values) { $messages[$entry.code] = $entry }

$expanded = foreach ($line in ((Read-Text $cheatSource) -split "`n")) {
    if ($line -match '^<!--\s*block:\s*(\S+)\s+#(\S+)(?:\s+(\d+))?\s*-->$') {
        $n = if ($Matches[3]) { [int]$Matches[3] } else { 1 }
        $fences = @(Get-Fences (Get-Section $Matches[1] $Matches[2]))
        if ($fences.Count -lt $n) { throw "cheat sheet: $($Matches[1]) #$($Matches[2]) has $($fences.Count) block(s), not $n." }
        $fences[$n - 1]
    }
    elseif ($line -match '^<!--\s*names:\s*(\S+)\s+#(\S+)\s*-->$') {
        $section = Get-Section $Matches[1] $Matches[2]
        $names = foreach ($row in ($section -split "`n")) {
            if ($row -match '^\|\s*`([^`]+)`') { $Matches[1] }
        }
        if (-not $names) { throw "cheat sheet: $($Matches[1]) #$($Matches[2]) has no table of names." }
        (@($names | Select-Object -Unique) | ForEach-Object { "``$_``" }) -join ' '
    }
    elseif ($line -match '^<!--\s*codes:\s*(.+?)\s*-->$') {
        '| Code | Severity | Message |'
        '| :-- | :-- | :-- |'
        foreach ($code in ($Matches[1] -split '\s+')) {
            if (-not $messages.ContainsKey($code)) { throw "cheat sheet: $code is not a live diagnostic." }
            $m = $messages[$code]
            "| [$code]($($m.doc)) | $($m.severity) | $(($m.title -replace '\|', '\|')) |"
        }
    }
    elseif ($line -match '^<!--\s*(block|names|codes):') { throw "cheat sheet: malformed directive: $line" }
    else { $line }
}

$banner = '<!-- Generated by .skill/gen-knowledge.ps1 from .skill/cheatsheet/cheatsheet.src.md. Edit that file, not this one. -->'
# The banner goes last: the page has to open with its title, which the documentation site lifts out.
$cheatText = (($expanded -join "`n").TrimEnd()) + "`n`n" + $banner + "`n"
Write-Output-File -Path (Join-Path $docsRoot 'cheatsheet.md') -Text $cheatText

# ---------------------------------------------------------------- verdict

if ($Check) {
    if ($drift.Count) {
        Write-Host ''
        Write-Host "knowledge files are out of date: $($drift -join ', ')" -ForegroundColor Red
        Write-Host 'Run .skill/gen-knowledge.ps1 and commit the result.' -ForegroundColor Red
        exit 1
    }
    Write-Host 'knowledge files are up to date.' -ForegroundColor Green
}
exit 0
