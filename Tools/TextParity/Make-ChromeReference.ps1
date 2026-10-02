#Requires -Version 7.2
<#
.SYNOPSIS
    Draws every case of the text parity corpus with headless Chrome (or Edge) and writes the pictures and the
    numbers DreamGUI.RHI.TextParity compares DreamGUI and Slate against.

.DESCRIPTION
    For each case of Source/DreamGUITests/Resources/TextParity/corpus.json (variants included, by the same rules the
    test applies) the script writes an HTML page: one @font-face per font file of the case's font key (file:/// URLs to
    the engine's own fonts; a fallback's unicode-range and size-adjust where the fonts table gives them), the family
    list in the order DreamGUI's face resolver tries the faces -- with a :lang() list for every language a face is meant
    for -- the text at (padding, padding) in a box of the case's width, white-space: pre-wrap (pre when the box is not to
    wrap), the case's line height, letter spacing, tab size, lang, dir, alignment (justification included) and drop
    shadow, black on white or white on black, drawn at the case's device scale. Rich cases are DreamGUI markup turned
    into HTML: <size=N> becomes a font-size span, <a=id> a link, <lang=xx> a span with that lang, <b> <i> <u> <s> <sup>
    <sub> stay what they are. A case held to Slate (reference 'slate': Chrome cannot draw it) is skipped, and so is a
    case whose optional font key names a file this machine does not have.

    Chrome runs twice per page. The first run takes the screenshot (<case>.png). The second dumps the DOM, into which
    the page's own script has written, once document.fonts.ready has resolved: the caret x and y for every UTF-16
    offset of the text (collapsed ranges), each character's box, the offsets where lines start, the first line's
    baseline (from a zero-size inline-block at the start of a hidden copy of the paragraph), the line pitch, and the
    primary font's ascent and descent (canvas measureText). That goes to <case>.json, with the browser's version.

    Nothing outside -OutDir and -WorkDir is written. The browser runs with a profile of its own under -WorkDir, so an
    open browser is not disturbed.

.PARAMETER Corpus
    The corpus; Source/DreamGUITests/Resources/TextParity/corpus.json of this plugin by default.
.PARAMETER OutDir
    Where <case>.png and <case>.json go; Source/DreamGUITests/Resources/TextParity/Chrome by default.
.PARAMETER EngineDir
    The engine's Engine directory, which $(EngineDir) in the fonts table stands for.
.PARAMETER Browser
    chrome.exe or msedge.exe to use; found under Program Files, Program Files (x86) and LocalAppData otherwise.
.PARAMETER WorkDir
    Where the pages and the browser profile go; Saved/TextParity of this plugin by default.
.PARAMETER Case
    Case ids to draw, wildcards allowed, several separated by commas; every case by default.
.PARAMETER TimeBudgetMs
    The virtual time each page is given to load its fonts and run its script.
.PARAMETER TimeoutSeconds
    How long one browser run may take before it is killed.

.EXAMPLE
    pwsh -NoProfile -File Tools\TextParity\Make-ChromeReference.ps1
.EXAMPLE
    pwsh -NoProfile -File Tools\TextParity\Make-ChromeReference.ps1 -Case 'Cjk_*','Corners_*'
#>
[CmdletBinding()]
param(
    [string]$Corpus,
    [string]$OutDir,
    [string]$EngineDir = 'C:\Program Files\Epic Games\UE_5.8\Engine',
    [string]$Browser,
    [string]$WorkDir,
    [string[]]$Case = @('*'),
    [int]$TimeBudgetMs = 5000,
    [int]$TimeoutSeconds = 90
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

# Under `pwsh -File` a list such as -Case 'Cjk_*','Corners_*' arrives as one string, quotes and comma included.
$Case = @($Case | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().Trim("'", '"') } | Where-Object { $_ })
if ($Case.Count -eq 0) { $Case = @('*') }

$PluginDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Corpus) { $Corpus = Join-Path $PluginDir 'Source\DreamGUITests\Resources\TextParity\corpus.json' }
if (-not $OutDir) { $OutDir = Join-Path $PluginDir 'Source\DreamGUITests\Resources\TextParity\Chrome' }
if (-not $WorkDir) { $WorkDir = Join-Path $PluginDir 'Saved\TextParity' }

# ------------------------------------------------------------------------------------------------
# helpers
# ------------------------------------------------------------------------------------------------

function Get-Prop($Object, [string]$Name, $Default) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) { return $Default }
    return $property.Value
}

function Find-Browser {
    # Chrome first, wherever it was installed; Edge, which takes the same switches, when there is no Chrome.
    $chromeRoots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:LOCALAPPDATA) | Where-Object { $_ }
    foreach ($root in $chromeRoots) {
        $candidate = Join-Path $root 'Google\Chrome\Application\chrome.exe'
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    $edgeRoots = @(${env:ProgramFiles(x86)}, $env:ProgramFiles) | Where-Object { $_ }
    foreach ($root in $edgeRoots) {
        $candidate = Join-Path $root 'Microsoft\Edge\Application\msedge.exe'
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Resolve-FontPath([string]$Spelling) {
    $path = $Spelling.Replace('$(EngineDir)', $EngineDir.TrimEnd('\', '/'))
    $path = $path.Replace('$(WindowsFonts)', (Join-Path $env:WINDIR 'Fonts'))
    return [System.IO.Path]::GetFullPath($path)
}

function ConvertTo-FileUri([string]$Path) {
    return ([System.Uri]::new($Path)).AbsoluteUri
}

# Chrome's font sanitizer (OTS) rejects some fonts FreeType draws fine -- the engine's DroidSansFallback.ttf sets a
# reserved glyph-flag bit -- and Chrome then draws those cases in a system font. Pages load a copy with that bit
# cleared (chrome_safe_font.py: same outlines and metrics), made once per file under -WorkDir; without Python the
# original file is used and such a case reports that a font did not load.
$script:ChromeFontUris = @{}
function Get-ChromeFontUri([string]$Path) {
    if ($script:ChromeFontUris.ContainsKey($Path)) { return $script:ChromeFontUris[$Path] }
    $uri = ConvertTo-FileUri $Path
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python -and $Path -match '\.(ttf|otf)$') {
        $fontsDir = Join-Path $WorkDir 'fonts'
        New-Item -ItemType Directory -Force -Path $fontsDir | Out-Null
        $copy = Join-Path $fontsDir ([System.IO.Path]::GetFileNameWithoutExtension($Path) + '.chrome' + [System.IO.Path]::GetExtension($Path))
        & $python.Source (Join-Path $PSScriptRoot 'chrome_safe_font.py') $Path $copy | Out-Null
        if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $copy)) { $uri = ConvertTo-FileUri $copy }
    }
    $script:ChromeFontUris[$Path] = $uri
    return $uri
}

function ConvertTo-HtmlText([string]$Text) {
    return $Text.Replace('&', '&amp;').Replace('<', '&lt;').Replace('>', '&gt;')
}

# DreamGUI's rich-text markup as HTML. Character references (&lt; &#x1F600;) are HTML already.
function Convert-RichMarkup([string]$Markup) {
    $pattern = '<(/?)([A-Za-z_][A-Za-z0-9_]*)(?:=([^>]*))?(/?)>'
    $evaluator = {
        param($Match)
        $closing = $Match.Groups[1].Value -eq '/'
        $name = $Match.Groups[2].Value.ToLowerInvariant()
        $value = $Match.Groups[3].Value.Trim()
        if ($Match.Groups[4].Value -eq '/') { return '' }
        switch ($name) {
            { $_ -in @('b', 'i', 'u', 's', 'sup', 'sub') } {
                if ($closing) { return "</$name>" } else { return "<$name>" }
            }
            'size' {
                if ($closing) { return '</span>' }
                if ($value.StartsWith('+') -or $value.StartsWith('-')) {
                    return "<span style=`"font-size:calc(1em $($value.Substring(0, 1)) $($value.Substring(1))px)`">"
                }
                return "<span style=`"font-size:${value}px`">"
            }
            'color' {
                if ($closing) { return '</span>' }
                return "<span style=`"color:$value`">"
            }
            'a' {
                if ($closing) { return '</a>' }
                return "<a href=`"#$value`">"
            }
            'lang' {
                if ($closing) { return '</span>' }
                return "<span lang=`"$value`">"
            }
            default {
                if ($closing) { return '</span>' }
                return "<span data-tag=`"$name`">"
            }
        }
    }
    return [regex]::Replace($Markup, $pattern, $evaluator)
}

function Get-LineHeightFactor([string]$Spelling) {
    $trimmed = $Spelling.Trim()
    if ($trimmed.EndsWith('%')) { return [double]::Parse($trimmed.TrimEnd('%'), [System.Globalization.CultureInfo]::InvariantCulture) / 100.0 }
    return 0.0
}

# The corpus's cases with their defaults filled in and their variants made, by the rules DreamTextParityCorpus.cpp applies.
function Expand-Cases($Data) {
    $padding = [int](Get-Prop $Data 'padding' 16)
    $defaultCanvas = @(Get-Prop $Data 'canvas' @(1024, 160))
    $result = [System.Collections.Generic.List[object]]::new()
    foreach ($entry in @($Data.cases)) {
        $outline = Get-Prop $entry 'outline' $null
        $shadow = Get-Prop $entry 'shadow' $null
        $item = [ordered]@{
            id            = [string]$entry.id
            base          = [string]$entry.id
            text          = [string](Get-Prop $entry 'text' '')
            font          = [string](Get-Prop $entry 'font' 'Latin')
            size          = [double](Get-Prop $entry 'size' 16)
            width         = [double](Get-Prop $entry 'width' 0)
            lineHeight    = Get-LineHeightFactor ([string](Get-Prop $entry 'lineHeight' 'normal'))
            letterSpacing = [double](Get-Prop $entry 'letterSpacing' 0)
            rich          = [bool](Get-Prop $entry 'rich' $false)
            lang          = [string](Get-Prop $entry 'lang' 'en')
            align         = [string](Get-Prop $entry 'align' 'start')
            textJustify   = [string](Get-Prop $entry 'textJustify' 'auto')
            textAlignLast = [string](Get-Prop $entry 'textAlignLast' 'auto')
            dir           = [string](Get-Prop $entry 'dir' 'ltr')
            wrap          = [string](Get-Prop $entry 'wrap' 'anywhere')
            wordBreak     = [string](Get-Prop $entry 'wordBreak' 'normal')
            overflow      = [string](Get-Prop $entry 'overflow' '')
            maxLines      = [int](Get-Prop $entry 'maxLines' 0)
            transform     = [string](Get-Prop $entry 'transform' '')
            tabSize       = [double](Get-Prop $entry 'tabSize' 8)
            outlineEm     = if ($null -ne $outline) { [double](Get-Prop $outline 'width' 0) } else { 0.0 }
            outlineColor  = if ($null -ne $outline) { [string](Get-Prop $outline 'color' '#000000') } else { '#000000' }
            shadowX       = if ($null -ne $shadow) { [double](Get-Prop $shadow 'x' 0) } else { 0.0 }
            shadowY       = if ($null -ne $shadow) { [double](Get-Prop $shadow 'y' 0) } else { 0.0 }
            shadowColor   = if ($null -ne $shadow) { [string](Get-Prop $shadow 'color' '#00000080') } else { '' }
            canvas        = @(Get-Prop $entry 'canvas' $defaultCanvas)
            scale         = [double](Get-Prop $entry 'scale' 1)
            reference     = [string](Get-Prop $entry 'reference' 'chrome')
            inverse       = $false
            flags         = @(Get-Prop $entry 'flags' @())
        }
        $result.Add([pscustomobject]$item)
        foreach ($variant in @(Get-Prop $entry 'variants' @())) {
            $copy = [ordered]@{}
            foreach ($key in $item.Keys) { $copy[$key] = $item[$key] }
            if ($variant -eq 'narrow') {
                $copy.id = "$($item.id)_1px"
                $copy.width = 1.0
                $copy.wrap = 'normal'
                $pitch = [Math]::Ceiling(1.6 * $item.size)
                $copy.canvas = @(
                    [Math]::Max(512, 2 * $padding + [Math]::Ceiling(3.0 * $item.size)),
                    [Math]::Min(4096, 2 * $padding + ($item.text.Length + 1) * $pitch))
            }
            elseif ($variant -eq 'inverse') {
                $copy.id = "$($item.id)_Inverse"
                $copy.inverse = $true
            }
            elseif ($variant -eq 'field') {
                # DreamGUI draws this one from its distance field; Chrome's picture is the case's own, under the variant's name.
                $copy.id = "$($item.id)_Field"
            }
            else {
                continue
            }
            $result.Add([pscustomobject]$copy)
        }
    }
    return $result
}

# A face of the fonts table: its file, or {file, lang, unicodeRange, scale} for a fallback meant for some languages,
# some code points or a scale (DreamTextParityCorpus.cpp reads the same object).
function ConvertTo-Face($Spec) {
    if ($Spec -is [string]) {
        return [pscustomobject]@{ File = Resolve-FontPath $Spec; Lang = ''; UnicodeRange = ''; Scale = 1.0 }
    }
    $file = [string](Get-Prop $Spec 'file' '')
    if (-not $file) { throw 'a face of the fonts table has no file' }
    return [pscustomobject]@{
        File         = Resolve-FontPath $file
        Lang         = ([string](Get-Prop $Spec 'lang' '')).Trim()
        UnicodeRange = ([string](Get-Prop $Spec 'unicodeRange' '')).Trim()
        Scale        = [double](Get-Prop $Spec 'scale' 1.0)
    }
}

function Get-FontKey($Data, [string]$Key) {
    $entry = Get-Prop $Data.fonts $Key $null
    if ($null -eq $entry) { throw "the corpus has no font key '$Key'" }
    return [pscustomobject]@{
        Faces      = @(@(Get-Prop $entry 'faces' @()) | ForEach-Object { ConvertTo-Face $_ })
        Bold       = if (Get-Prop $entry 'bold' $null) { Resolve-FontPath $entry.bold } else { $null }
        Italic     = if (Get-Prop $entry 'italic' $null) { Resolve-FontPath $entry.italic } else { $null }
        BoldItalic = if (Get-Prop $entry 'boldItalic' $null) { Resolve-FontPath $entry.boldItalic } else { $null }
        Optional   = [bool](Get-Prop $entry 'optional' $false)
    }
}

# Whether a text in $TextLang is in one of a face's languages ($FaceLangs, semicolon-separated): CSS :lang() matching,
# a face's tag being the text's or a prefix of it at a hyphen, case-insensitive. DreamTextParity::LanguageMatches.
function Test-LanguageMatch([string]$FaceLangs, [string]$TextLang) {
    $text = $TextLang.Trim().ToLowerInvariant().Replace('_', '-')
    if (-not $text) { return $false }
    foreach ($tag in ($FaceLangs -split ';')) {
        $face = $tag.Trim().ToLowerInvariant().Replace('_', '-')
        if (-not $face) { continue }
        if ($text -eq $face -or $text.StartsWith("$face-")) { return $true }
    }
    return $false
}

# The order a text in $Lang tries a key's faces in, as indices: the primary, the fallbacks meant for its language, those
# meant for any, then the rest, each in table order. DreamTextParity::GetFaceOrder, and DreamGUI's face resolver.
function Get-FaceOrder($FontKey, [string]$Lang) {
    $order = [System.Collections.Generic.List[int]]::new()
    if ($FontKey.Faces.Count -eq 0) { return @() }
    $order.Add(0)
    for ($index = 1; $index -lt $FontKey.Faces.Count; $index++) {
        if ($FontKey.Faces[$index].Lang -and (Test-LanguageMatch $FontKey.Faces[$index].Lang $Lang)) { $order.Add($index) }
    }
    for ($index = 1; $index -lt $FontKey.Faces.Count; $index++) {
        if (-not $FontKey.Faces[$index].Lang) { $order.Add($index) }
    }
    for ($index = 1; $index -lt $FontKey.Faces.Count; $index++) {
        if (-not $order.Contains($index)) { $order.Add($index) }
    }
    return $order.ToArray()
}

# The page's own script: waits for the fonts, measures, and writes what it measured into the document as JSON.
$MeasureScript = @'
(async () => {
  const out = { id: parityCase.id, ok: false };
  const round = (value) => Math.round(value * 100) / 100;
  try {
    if (document.fonts && document.fonts.ready) { await document.fonts.ready; }
    const p = document.getElementById('p');
    if (parityCase.lineHeightFactor > 0) {
      const probe = document.createElement('div');
      probe.style.cssText = 'position:absolute;left:0;top:0;visibility:hidden;white-space:pre;line-height:normal;margin:0;padding:0;';
      probe.style.fontFamily = parityCase.primaryFamily;
      probe.style.fontSize = parityCase.size + 'px';
      probe.textContent = 'Hg';
      document.body.appendChild(probe);
      const normal = probe.getBoundingClientRect().height;
      probe.remove();
      p.style.lineHeight = (normal * parityCase.lineHeightFactor) + 'px';
      out.normalLineHeight = round(normal);
    }
    const faces = [];
    let loaded = true;
    document.fonts.forEach((face) => {
      faces.push({ family: face.family, weight: face.weight, style: face.style, status: face.status });
      if (face.status === 'error') { loaded = false; }
    });
    out.fonts = faces;
    out.fontsLoaded = loaded;

    const walker = document.createTreeWalker(p, NodeFilter.SHOW_TEXT);
    const nodes = [];
    for (let node = walker.nextNode(); node; node = walker.nextNode()) { nodes.push(node); }
    let text = '';
    const starts = [];
    for (const node of nodes) { starts.push(text.length); text += node.data; }
    out.text = text;
    out.length = text.length;
    const nodeAt = (offset) => {
      for (let i = 0; i < nodes.length; i++) {
        const end = starts[i] + nodes[i].data.length;
        if (offset < end || i === nodes.length - 1) { return [nodes[i], Math.min(offset - starts[i], nodes[i].data.length)]; }
      }
      return null;
    };
    const range = document.createRange();

    const chars = [];
    for (let offset = 0; offset < text.length; offset++) {
      const [node, local] = nodeAt(offset);
      range.setStart(node, local);
      range.setEnd(node, Math.min(local + 1, node.data.length));
      const rects = Array.from(range.getClientRects()).filter((r) => r.width > 0 || r.height > 0);
      if (!rects.length) { chars.push(null); continue; }
      let l = Infinity, t = Infinity, r = -Infinity, b = -Infinity;
      for (const rect of rects) { l = Math.min(l, rect.left); t = Math.min(t, rect.top); r = Math.max(r, rect.right); b = Math.max(b, rect.bottom); }
      chars.push([round(l), round(t), round(r), round(b)]);
    }
    out.chars = chars;

    const carets = [];
    for (let offset = 0; offset <= text.length; offset++) {
      const located = nodeAt(offset);
      if (!located) { carets.push(null); continue; }
      range.setStart(located[0], located[1]);
      range.setEnd(located[0], located[1]);
      let rect = range.getClientRects()[0];
      if (!rect) {
        const box = range.getBoundingClientRect();
        if (box && (box.height > 0 || box.left !== 0 || box.top !== 0)) { rect = box; }
      }
      carets.push(rect ? [round(rect.left), round(rect.top + rect.height / 2), round(rect.height)] : null);
    }
    out.carets = carets;

    // A character starts a line when it overlaps the line so far by less than 60 % of its own height: a smaller
    // run, a superscript or a fallback face on the same line overlaps it almost wholly, the next line hardly at all.
    const lineStarts = [];
    const lineTops = [];
    let line = null;
    for (let offset = 0; offset < chars.length; offset++) {
      const c = chars[offset];
      if (!c) { continue; }
      const height = c[3] - c[1];
      if (height <= 0) { continue; }
      if (line === null) { lineStarts.push(offset); line = [c[1], c[3]]; continue; }
      const overlap = Math.min(line[1], c[3]) - Math.max(line[0], c[1]);
      if (overlap < 0.6 * height) {
        lineTops.push(line[0]);
        lineStarts.push(offset);
        line = [c[1], c[3]];
      } else {
        line[0] = Math.min(line[0], c[1]);
        line[1] = Math.max(line[1], c[3]);
      }
    }
    if (line !== null) { lineTops.push(line[0]); }
    // A line clamp only hides the lines past it; what is measured is what is shown, so the hidden lines' starts, boxes
    // and carets are dropped.
    if (parityCase.maxLines > 0 && lineStarts.length > parityCase.maxLines) {
      const firstHidden = lineStarts[parityCase.maxLines];
      lineStarts.length = parityCase.maxLines;
      lineTops.length = parityCase.maxLines;
      for (let offset = firstHidden; offset < chars.length; offset++) { chars[offset] = null; }
      for (let offset = firstHidden; offset < carets.length; offset++) { carets[offset] = null; }
    }
    out.lineStarts = lineStarts;
    out.lineTops = lineTops.map(round);
    const box = p.getBoundingClientRect();
    out.box = [round(box.left), round(box.top), round(box.width), round(box.height)];
    out.linePitch = lineTops.length >= 2
      ? round((lineTops[lineTops.length - 1] - lineTops[0]) / (lineTops.length - 1))
      : round(box.height / Math.max(1, lineStarts.length));

    // The first baseline: a zero-size inline-block sits on it. It goes into a hidden copy of the paragraph, in a span
    // with the text's first grapheme that may not break (white-space: pre, which also keeps a leading space), so that
    // in a box narrower than any glyph it is not left alone on an empty first line of its own.
    const copy = p.cloneNode(true);
    copy.removeAttribute('id');
    copy.style.visibility = 'hidden';
    const probe = document.createElement('span');
    probe.style.cssText = 'display:inline-block;width:0;height:0;margin:0;padding:0;border:0;vertical-align:baseline;';
    const holder = document.createElement('span');
    holder.style.whiteSpace = 'pre';
    holder.appendChild(probe);
    const copyWalker = document.createTreeWalker(copy, NodeFilter.SHOW_TEXT);
    let firstText = copyWalker.nextNode();
    while (firstText && firstText.data.length === 0) { firstText = copyWalker.nextNode(); }
    if (firstText) {
      let graphemeLength = /^[\uD800-\uDBFF][\uDC00-\uDFFF]/.test(firstText.data) ? 2 : 1;
      if (typeof Intl !== 'undefined' && Intl.Segmenter) {
        const first = new Intl.Segmenter(parityCase.lang, { granularity: 'grapheme' }).segment(firstText.data)[Symbol.iterator]().next();
        if (!first.done) { graphemeLength = first.value.segment.length; }
      }
      if (graphemeLength < firstText.data.length) { firstText.splitText(graphemeLength); }
      firstText.parentNode.insertBefore(holder, firstText);
      holder.appendChild(firstText);
    } else {
      copy.insertBefore(holder, copy.firstChild);
    }
    document.body.appendChild(copy);
    out.baseline = round(probe.getBoundingClientRect().bottom);
    copy.remove();

    const context = document.createElement('canvas').getContext('2d');
    context.font = parityCase.size + 'px ' + parityCase.primaryFamily;
    const metrics = context.measureText('Hg');
    out.ascent = round(metrics.fontBoundingBoxAscent);
    out.descent = round(metrics.fontBoundingBoxDescent);
    out.userAgent = navigator.userAgent;
    out.ok = true;
  } catch (error) {
    out.error = String((error && error.stack) || error);
  }
  const result = document.createElement('script');
  result.type = 'application/json';
  result.id = 'parity-result';
  result.textContent = JSON.stringify(out)
    .replace(/[\u007f-\uffff]/g, (c) => '\\u' + c.charCodeAt(0).toString(16).padStart(4, '0'))
    .replace(/</g, '\\u003c');
  document.body.appendChild(result);
})();
'@

function New-CasePage($Item, $FontKey, [int]$Padding) {
    $canvasWidth = [int]$Item.canvas[0]
    $canvasHeight = [int]$Item.canvas[1]
    $ink = if ($Item.inverse) { '#ffffff' } else { '#000000' }
    $paper = if ($Item.inverse) { '#000000' } else { '#ffffff' }
    $boxWidth = if ($Item.width -gt 0) { $Item.width } else { $canvasWidth - 2 * $Padding }

    $invariant = [System.Globalization.CultureInfo]::InvariantCulture
    $faceCss = [System.Text.StringBuilder]::new()
    $families = @()
    for ($index = 0; $index -lt $FontKey.Faces.Count; $index++) {
        $face = $FontKey.Faces[$index]
        $family = "P_$($Item.font)_$index"
        $families += "'$family'"
        # A fallback's ranges and scale, as the fallback entry DreamGUI makes of it has them; the primary takes only its file.
        $descriptors = ''
        if ($index -gt 0 -and $face.UnicodeRange) { $descriptors += " unicode-range: $($face.UnicodeRange);" }
        if ($index -gt 0 -and $face.Scale -ne 1.0) { $descriptors += " size-adjust: $((100.0 * $face.Scale).ToString($invariant))%;" }
        [void]$faceCss.AppendLine("@font-face { font-family: '$family'; src: url('$(Get-ChromeFontUri $face.File)'); font-weight: 400; font-style: normal;$descriptors }")
        if ($index -eq 0) {
            if ($FontKey.Bold) { [void]$faceCss.AppendLine("@font-face { font-family: '$family'; src: url('$(Get-ChromeFontUri $FontKey.Bold)'); font-weight: 700; font-style: normal; }") }
            if ($FontKey.Italic) { [void]$faceCss.AppendLine("@font-face { font-family: '$family'; src: url('$(Get-ChromeFontUri $FontKey.Italic)'); font-weight: 400; font-style: italic; }") }
            if ($FontKey.BoldItalic) { [void]$faceCss.AppendLine("@font-face { font-family: '$family'; src: url('$(Get-ChromeFontUri $FontKey.BoldItalic)'); font-weight: 700; font-style: italic; }") }
        }
    }
    # The family list in the order DreamGUI's resolver tries the faces for a language no face is meant for, and one more
    # list for every language a face is meant for, which :lang() gives the paragraph and any <lang=xx> span in it. The
    # shorter tags come first, so where a text matches two ("zh" and "zh-Hans") the list of the longer one wins.
    $familyList = (@(Get-FaceOrder $FontKey '') | ForEach-Object { $families[$_] }) -join ', '
    $tags = @($FontKey.Faces | ForEach-Object { $_.Lang -split ';' } | ForEach-Object { $_.Trim() } | Where-Object { $_ } |
        Sort-Object -Unique | Sort-Object { ($_ -split '-').Count })
    $langCss = [System.Text.StringBuilder]::new()
    foreach ($tag in $tags) {
        $tagList = (@(Get-FaceOrder $FontKey $tag) | ForEach-Object { $families[$_] }) -join ', '
        [void]$langCss.AppendLine(".para:lang($tag), .para :lang($tag) { font-family: $tagList; }")
    }
    $extra = [System.Collections.Generic.List[string]]::new()
    $whiteSpace = if ($Item.width -gt 0) { 'pre-wrap' } else { 'pre' }
    if ($Item.overflow -eq 'ellipsis') {
        $whiteSpace = 'pre'
        $extra.Add('overflow: hidden; text-overflow: ellipsis;')
    }
    elseif ($Item.overflow -eq 'clamp' -and $Item.maxLines -gt 0) {
        $extra.Add("display: -webkit-box; -webkit-box-orient: vertical; -webkit-line-clamp: $($Item.maxLines); overflow: hidden;")
    }
    if ($Item.transform -eq 'uppercase') { $extra.Add('text-transform: uppercase;') }
    if ($Item.outlineEm -gt 0) {
        # DreamGUI's outline lies wholly outside the face; a stroke straddles the edge, so it is twice as wide and painted under the fill.
        $stroke = (2.0 * $Item.outlineEm).ToString($invariant)
        $extra.Add("-webkit-text-stroke: ${stroke}em $($Item.outlineColor); paint-order: stroke fill;")
    }
    if ($Item.shadowColor) {
        # In em, as DreamGUI's underlay offset is: the text's own size makes them pixels. No blur, as the case's underlay has none.
        $shadowX = $Item.shadowX.ToString($invariant)
        $shadowY = $Item.shadowY.ToString($invariant)
        $extra.Add("text-shadow: ${shadowX}em ${shadowY}em 0 $($Item.shadowColor);")
    }

    $content = if ($Item.rich) { Convert-RichMarkup $Item.text } else { ConvertTo-HtmlText $Item.text }
    $caseJson = [ordered]@{
        id               = $Item.id
        size             = $Item.size
        lang             = $Item.lang
        lineHeightFactor = $Item.lineHeight
        maxLines         = if ($Item.overflow -eq 'clamp') { $Item.maxLines } else { 0 }
        primaryFamily    = $families[0]
        families         = $familyList
    } | ConvertTo-Json -Compress -Depth 4

    $size = $Item.size.ToString($invariant)
    $spacing = $Item.letterSpacing.ToString($invariant)
    $tabSize = $Item.tabSize.ToString($invariant)
    $box = ([double]$boxWidth).ToString($invariant)
    $wrap = if ($Item.wrap -eq 'normal') { 'normal' } else { 'anywhere' }
    return @"
<!doctype html>
<html lang="$($Item.lang)">
<head>
<meta charset="utf-8">
<title>$($Item.id)</title>
<style>
$($faceCss.ToString())
html, body { margin: 0; padding: 0; }
body { width: ${canvasWidth}px; height: ${canvasHeight}px; overflow: hidden; background: $paper; }
.para {
  position: absolute; left: ${Padding}px; top: ${Padding}px; width: ${box}px; margin: 0; padding: 0;
  font-family: $familyList; font-size: ${size}px; line-height: normal; letter-spacing: ${spacing}px;
  white-space: $whiteSpace; overflow-wrap: $wrap; word-break: $($Item.wordBreak); line-break: auto; tab-size: $tabSize;
  color: $ink; text-align: $($Item.align); text-justify: $($Item.textJustify); text-align-last: $($Item.textAlignLast);
  font-kerning: normal; -webkit-font-smoothing: antialiased;
  $($extra -join ' ')
}
$($langCss.ToString())
</style>
</head>
<body><div id="p" class="para" lang="$($Item.lang)" dir="$($Item.dir)">$content</div>
<script>
const parityCase = $caseJson;
$MeasureScript
</script>
</body>
</html>
"@
}

# One browser run. A run that outlives -TimeoutSeconds is killed, with everything it started, and says so in TimedOut
# rather than throwing: one page that hangs is that case's problem, not the end of the run.
function Invoke-Browser([string]$Exe, [string[]]$Arguments) {
    $info = [System.Diagnostics.ProcessStartInfo]::new($Exe)
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $info.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::Start($info)
    $standardOutput = $process.StandardOutput.ReadToEndAsync()
    $standardError = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        try { $process.Kill($true) } catch { }
        return [pscustomobject]@{ ExitCode = -1; Out = ''; Err = ''; TimedOut = $true }
    }
    $process.WaitForExit()
    return [pscustomobject]@{ ExitCode = $process.ExitCode; Out = $standardOutput.Result; Err = $standardError.Result; TimedOut = $false }
}

function Get-PngSize([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $header = [byte[]]::new(24)
        if ($stream.Read($header, 0, 24) -lt 24) { return $null }
        $width = ([int]$header[16] -shl 24) -bor ([int]$header[17] -shl 16) -bor ([int]$header[18] -shl 8) -bor [int]$header[19]
        $height = ([int]$header[20] -shl 24) -bor ([int]$header[21] -shl 16) -bor ([int]$header[22] -shl 8) -bor [int]$header[23]
        return @($width, $height)
    }
    finally {
        $stream.Dispose()
    }
}

# ------------------------------------------------------------------------------------------------
# run
# ------------------------------------------------------------------------------------------------

if (-not $Browser) { $Browser = Find-Browser }
if (-not $Browser -or -not (Test-Path -LiteralPath $Browser)) {
    Write-Host '!!  No chrome.exe or msedge.exe was found; pass -Browser.' -ForegroundColor Yellow
    exit 2
}
if (-not (Test-Path -LiteralPath $Corpus)) {
    Write-Host "!!  No corpus at $Corpus." -ForegroundColor Yellow
    exit 2
}
$Version = (Get-Item -LiteralPath $Browser).VersionInfo.ProductVersion
$BrowserName = [System.IO.Path]::GetFileNameWithoutExtension($Browser)
Write-Host "==> $BrowserName $Version" -ForegroundColor Cyan

$Data = [System.IO.File]::ReadAllText($Corpus, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
$Padding = [int](Get-Prop $Data 'padding' 16)
$Cases = @(Expand-Cases $Data | Where-Object { $id = $_.id; @($Case | Where-Object { $id -like $_ }).Count -gt 0 })
if ($Cases.Count -eq 0) {
    Write-Host "!!  No case matches $($Case -join ', ')." -ForegroundColor Yellow
    exit 2
}

$PagesDir = Join-Path $WorkDir 'pages'
$ProfileDir = Join-Path $WorkDir 'profile'
New-Item -ItemType Directory -Force -Path $OutDir, $PagesDir, $ProfileDir | Out-Null

# The device scale factor is the case's own and goes with each run.
$Common = @(
    '--headless=new', '--disable-gpu', '--disable-lcd-text', '--force-color-profile=srgb',
    '--hide-scrollbars', '--allow-file-access-from-files', '--no-first-run', '--no-default-browser-check',
    '--disable-extensions', '--disable-background-networking', '--disable-component-update', '--disable-sync',
    "--user-data-dir=$ProfileDir", "--virtual-time-budget=$TimeBudgetMs"
)
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

$Problems = 0
$Skipped = 0
foreach ($item in $Cases) {
    $notes = [System.Collections.Generic.List[string]]::new()
    $lines = '-'
    $png = Join-Path $OutDir "$($item.id).png"
    $json = Join-Path $OutDir "$($item.id).json"
    try {
        # Both of the case's old files go first: a picture or numbers left from an earlier run would be paired with
        # this run's other half.
        foreach ($stale in @($png, $json)) {
            if (Test-Path -LiteralPath $stale) { Remove-Item -LiteralPath $stale -Force }
        }
        if ($item.reference -eq 'slate') {
            # Measured against Slate: Chrome cannot draw it (a middle ellipsis), so there is no reference to make.
            $Skipped++
            Write-Host ("    {0,-36} skipped: held to Slate, Chrome cannot draw it" -f $item.id) -ForegroundColor DarkGray
            continue
        }
        $fontKey = Get-FontKey $Data $item.font
        $missing = @($fontKey.Faces | ForEach-Object { $_.File } | Where-Object { -not (Test-Path -LiteralPath $_) })
        if ($missing.Count -gt 0 -and $fontKey.Optional) {
            # A system font this machine does not have; the test skips the case here too.
            $Skipped++
            Write-Host ("    {0,-36} skipped: no {1} on this machine" -f $item.id, ($missing -join ', ')) -ForegroundColor DarkGray
            continue
        }
        if ($missing.Count -gt 0) { $notes.Add("missing font file(s): $($missing -join ', ')") }
        $page = Join-Path $PagesDir "$($item.id).html"
        [System.IO.File]::WriteAllText($page, (New-CasePage $item $fontKey $Padding), [System.Text.UTF8Encoding]::new($false))
        $url = ConvertTo-FileUri $page
        # The window in CSS pixels, the canvas; the screenshot in device pixels, the canvas times the scale.
        $windowSize = "--window-size=$([int]$item.canvas[0]),$([int]$item.canvas[1])"
        $scaleFactor = "--force-device-scale-factor=$($item.scale.ToString($Invariant))"
        $expectedWidth = [int][Math]::Round([double]$item.canvas[0] * $item.scale)
        $expectedHeight = [int][Math]::Round([double]$item.canvas[1] * $item.scale)

        $shot = Invoke-Browser $Browser ($Common + @($scaleFactor, $windowSize, "--screenshot=$png", $url))
        if ($shot.TimedOut) {
            $notes.Add("the screenshot run did not finish within $TimeoutSeconds s and was killed")
        }
        elseif (-not (Test-Path -LiteralPath $png)) {
            $notes.Add("no screenshot (exit $($shot.ExitCode)): $($shot.Err.Trim())")
        }
        else {
            $pngSize = Get-PngSize $png
            if ($null -ne $pngSize -and ($pngSize[0] -ne $expectedWidth -or $pngSize[1] -ne $expectedHeight)) {
                $notes.Add("the screenshot is $($pngSize[0]) x $($pngSize[1]), not $expectedWidth x $expectedHeight")
            }
        }

        $dump = Invoke-Browser $Browser ($Common + @($scaleFactor, $windowSize, '--dump-dom', $url))
        $match = [regex]::Match($dump.Out, '(?s)<script[^>]*id="parity-result"[^>]*>(.*?)</script>')
        if ($dump.TimedOut) {
            $notes.Add("the measuring run did not finish within $TimeoutSeconds s and was killed")
        }
        elseif (-not $match.Success) {
            $notes.Add("the page wrote no measurements (exit $($dump.ExitCode))")
        }
        else {
            $measured = $match.Groups[1].Value
            $parsed = $measured | ConvertFrom-Json
            if (-not (Get-Prop $parsed 'ok' $false)) { $notes.Add("the page's script failed: $(Get-Prop $parsed 'error' 'unknown')") }
            if (-not (Get-Prop $parsed 'fontsLoaded' $true)) { $notes.Add('a font did not load') }
            $lines = (@(Get-Prop $parsed 'lineStarts' @()) -join ' ')
            # The browser's version goes in front of what the page measured, which is kept exactly as the page wrote it.
            $header = '{"chrome":"' + $Version + '","browser":"' + $BrowserName + '","case":"' + $item.id + '",'
            [System.IO.File]::WriteAllText($json, $header + $measured.Substring(1), [System.Text.UTF8Encoding]::new($false))
        }
    }
    catch {
        $notes.Add("the case could not be drawn: $($_.Exception.Message)")
    }
    if ($notes.Count -gt 0) {
        $Problems++
        Write-Host ("!!  {0,-36} {1}" -f $item.id, ($notes -join '; ')) -ForegroundColor Yellow
    }
    else {
        Write-Host ("    {0,-36} lines start at {1}" -f $item.id, $lines)
    }
}

Write-Host "==> $($Cases.Count - $Skipped) case(s) drawn by $BrowserName $Version into $OutDir, $Skipped skipped; $Problems with a problem." -ForegroundColor Cyan
exit ($(if ($Problems -gt 0) { 1 } else { 0 }))
