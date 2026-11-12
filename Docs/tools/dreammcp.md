---
title: DreamMCP endpoint
description: The HTTP endpoint a DreamMCP server reaches DreamGUI through — check, compile, layout, preview, diagnostics, lookup and info for .dui screens, in the user's editor or in a host the server starts — with what layout lists, the layout rules and their thresholds, and what preview measures.
---

An AI agent connected through [DreamMCP](https://github.com/TypeDreamMoon/dream-mcp) gets a `dream_gui` tool. Through it
the agent checks and compiles `.dui` files, sees where every node of a screen lands at a viewport size and the problems a
person would see at a glance (`layout`), draws the screen into a picture (`preview`), reads diagnostics and looks up the
names the compiler accepts. The calls are answered by the user's editor when it is open, and by a host process the server
starts when it is not.

The endpoint adds no compiler of its own: every action forwards to what the plugin already does — the parser and tree
builder, the widget Blueprint compiler, the diagnostics mailbox, the symbol export the [VS Code extension](vscode.md)
reads, and the designer's way of instancing a screen. What it adds is one answer shape for all of them, and the two things
a model that writes `.dui` cannot otherwise see: the laid-out screen, in words, and the picture.

| | |
| --- | --- |
| Kind | editor service, in the `DreamGUIEditor` module (`Source/DreamGUIEditor/Private/MCP/`) |
| Protocol | DreamMCP protocol `1` |
| Manifest | `<Plugin>/Resources/DreamMCP.json`: the actions, their arguments as JSON Schema, the route, the host commandlet |
| References | `.skill/reference/dui.md`, `example.md`, `pitfalls.md`, `controls.md`, named by the manifest and packaged with the plugin. Each stands alone: its links lead to this manual, and an agent that cannot open them misses nothing it needs |
| Route | `http://127.0.0.1:<port>/dream/gui`, tool name `dream_gui` |
| Port | `47800`, or `-DreamMCPPort=<n>`; a port that cannot be bound is skipped for the next, up to 16. Other Dream plugins in the same process share it, each under its own route |
| Bind address | `127.0.0.1` only, proven at startup |
| Authentication | the `X-Dream-Token` header on every request; the token is `DREAM_MCP_TOKEN` from the environment, else `-DreamMCPToken=<t>`, else random. A request with an `Origin` header is refused (403) |
| Discovery | `<Project>/Saved/DreamMCP/gui.json`, written once the endpoint has answered a hello of its own, deleted when it stops if it still names this process |
| Hosts | the editor with `-DreamMCPHost -RenderOffscreen` (draws); `-run=DreamGUIHost -nullrhi -DreamMCPHost` (does not draw, still lays out) |
| Switched off by | `-NoDreamMCP`; in the user's editor also *Project Settings ▸ Plugins ▸ Dream GUI ▸ Editor ▸ Enable DreamMCP Endpoint* (on by default, read at startup) |

## Which process serves it

| Process | Endpoint | `mode` | Draws |
| --- | --- | --- | --- |
| The user's editor | yes, once the engine has initialized | `editor` | yes |
| A render host: the editor with `-DreamMCPHost -RenderOffscreen`, no `-run=` | yes | `host` | yes |
| A headless host: `-run=DreamGUIHost -nullrhi -DreamMCPHost`, or another Dream plugin's host commandlet | yes | `host` | no |
| Any other commandlet (`-run=DreamUICompileWidgets`, a cook), an automation run, a game | no | — | — |

A host is nobody's editor: it writes no editor settings, does not auto-save, renders no realtime viewport, caps its frame
rate at 15 while no Dream endpoint in the process has a job (the console variable `DreamMCP.BusyEndpoints`, which every
Dream plugin's endpoint counts itself in and out of, is 0), accepts `POST /dream/gui/shutdown`, exits when the process that started it goes away
(`-DreamMCPParentPid=<pid>`, `0` for none) and exits with code 3 when it cannot start answering within
`-DreamMCPStartupTimeout` seconds (600 by default). In the user's editor `/shutdown` is refused (409, `not-a-host`).

Where things are written: a compile in the user's editor leaves the widget Blueprints in memory for the user to save, as
saving a `.dui` does; a compile in a host saves them. Pictures go to `<Project>/Saved/DreamMCP/artifacts/<job>.png`.

## Actions

| Action | Long | Writes | Arguments | Answers |
| --- | --- | --- | --- | --- |
| `check` | yes | no | `file`, or `all: true` | parses the file and builds its tree the way the compiler does, into a tree that is thrown away. Reports lexer, parser, meaning, value and builder errors (DUI1xxx–5xxx); what only a compile sees (props, events, view models, expression bindings: DUI6xxx) needs `compile` |
| `compile` | yes | yes | `file`, `force` | builds the widget Blueprint the file's `class` line names, creating it when it does not exist, and first every component the file places with `use "X.dui" as X`. Nothing that has not changed since this process last compiled it is compiled again unless `force` |
| `diagnostics` | no | no | `file` | what the engine holds from the last check or compile of each file, errors first, without compiling |
| `lookup` | no | no | `name`, `kind`, `limit` | an exact node type, component, enum or view model class gives its entry (properties with type, default and a one-line description from the C++ source, events), one a line in the answer's text; `Type.Property` narrows to properties starting so; otherwise every name containing the text. `Widget` lists what every node takes, `slot` what `@slot` takes |
| `layout` | yes | yes | `file`, `width`, `height`, `sizes`, `root`, `depth`, `force` | compiles the screen as `compile` does, lays it out at the viewport size without drawing it, and lists every node and the problems (below) |
| `preview` | yes | yes | `file`, `width`, `height`, `background`, `timeline`, `time`, `frames`, `duration`, `force` | compiles, draws the screen into a PNG, and measures it (below). Needs a process that draws |
| `info` | no | no | — | plugin version and commit, engine version and kind, OS, locale, mode, whether this process draws, the source folders |

Paths in arguments and answers are relative to the project root (`DUI/Menu.dui`); a path relative to a `DUI/` folder
(`Menu.dui`) is found too. `help` and `reference` are added by the server.

## What `layout` lists

The screen is put on a stage of the endpoint's own — a preview world, a root the size of the viewport, the compiled class
instanced under it the way the designer instances it — and laid out until every glyph is in and two passes agree. Nothing
of the user's level is touched. Layout reads no pixels, so the headless host lays screens out as well as the editor does.

The answer's summary is one sentence — the file, the viewport, how many nodes, how many problems and the most serious.
Its `text`, which the server shows as written after the summary, is the screen, one node a line, children indented under
their parent:

```text
Laid out DUI/Menu.dui at 1920x1080: 7 nodes, 1 problem (1 warning(s), 0 info); the most serious: ...
The screen, one node a line (x, y from the top left of the viewport, in pixels):
Widget Root  x=0 y=0 w=1920 h=1080  [Overlay]
  Widget Card  x=701 y=466 w=519 h=148  [Overlay]
    Image Backdrop  x=701 y=466 w=519 h=148
    Widget Column  x=701 y=466 w=519 h=148  [VerticalBox]
      Text Heading  x=725 y=494 w=471 h=33  text="Hello, DreamGUI" size=28
```

Each line is the node's type as the file writes it, its id, its rect in viewport pixels (top-left origin, Y down, rounded),
its text with its font size, `CUT` or `OVERFLOWS(needs WxH)` when the text does not fit, and in brackets what else matters:
`hidden` / `inactive` / `transparent`, `pressable`, its own layout box, how its anchors hold it when no box places it
(`centred`, `fills its parent`, `held at the top left`...), `partly clipped`, `turned in 3D`, `own canvas X`, and `part of
its control` for what a control builds for itself (a button's face). `depth` (6 by default) limits the levels listed, and
the list stops at 250 lines; the answer's `next` says how many nodes were left out and how to see them; `root` lists one
node and what is under it.

`data.tree` is the same tree as JSON: per node `type`, `name`, `path`, `rect`, `visibleRect` when clipped, `visible`,
`order` (draw order in its canvas), `canvas`, `pressable`, `container`, `placedBy`, `anchors`, `in3d`, `text` (`text`,
`fontSize`, `cut`, `overflows`, `neededW`, `neededH`), `file` and `line`. A node turned in 3D is given the bound of its
outline seen straight on; the picture shows its perspective.

`sizes` lays the screen out at more viewports (`"1280x720, 2560x1440, 1080x1920"`, up to five). A problem found at one of
them and not at `width` x `height` is reported as DUI8006, naming the size.

## The layout rules

They never fail a compile; they come out of `layout` (and DUI8007 out of `preview`) as warnings and infos, each with the
node's path, the line that writes it (or the control that made it) and a hint. One problem in a branch is reported once,
not again for every node inside it, and a problem inside a component placed several times is one entry that names the
other instances (`(the same at Root/CardB/Mute, ...)`); any finding repeated at the same place says `(reported N times)`.

| Code | Severity | Reported when | Threshold |
| --- | --- | --- | --- |
| [DUI8001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8001) | warning | what can show of a node (its rect cut to its clipping ancestors) reaches past an edge of the viewport, or lies wholly outside; or a text that does not fit its box spills off the screen (upward from a bottom-aligned box, both ways from a centred one) where nothing clips it | more than 1 px |
| [DUI8002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8002) | warning | a text is cut short by its overflow policy, or laid out past its box | the text's own measure (`UDreamText::MeasureFit`) |
| [DUI8003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8003) | warning (info for a plain widget with children) | a node that draws, holds children or can be pressed has no width or height; a pressable control is smaller than a pointer needs | under 1 px; under 24 x 24 px (WCAG 2.2's minimum target) |
| [DUI8004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8004) | warning | two pressable controls in the same canvas overlap on the screen, wherever they are in the tree (one inside the other aside): a press there reaches only the upper one. A small control lying at least 95 % on one at least four times its area (a button on a pressable card, a dialog on a click-away backdrop) is layered on purpose and not reported | 2 px or more on both axes |
| [DUI8005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8005) | info | siblings' left, right, top or bottom edges or centres nearly line up; a row or column of three or more has nearly even gaps | more than 0.5 and at most 3 px apart |
| [DUI8006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8006) | warning | DUI8001–8004 at one of `sizes` and not at the first viewport | as the rule it repeats |
| [DUI8007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8007) | warning / info | in `preview`: a text's colour (at its alpha) against the median of the pixels on a one-pixel ring just inside its box. A control's own part painted within 1.1 : 1 of what is behind it (the tick of a toggle that is off) is hidden on purpose and not reported | WCAG contrast under 3 : 1 a warning; under 4.5 : 1 an info for text under 24 px |

## What `preview` measures

The screen is drawn on the same stage into a render target the size of the picture, after the picture has held still for
three frames with no shader compiling and no glyph pending (two minutes at most; the answer says when it ran out). Colours
are the file's: an authored `#1B1E26` reads back as `#1B1E26`. The same frame is then drawn over black and over white:

| Figure | Definition |
| --- | --- |
| covered | a pixel the same over black and over white within 8 per channel, or between that and 247 (partly transparent); one that follows the backdrop is not covered |
| `coverage` | covered pixels over all pixels |
| `meanColor`, `meanColorName` | the mean of the covered pixels as the picture shows them, and a plain name for it (greys under saturation 0.12, tinted greys under 0.4) |
| `luminance` | Rec. 709 weights on the PNG's encoded values: min, max, mean, standard deviation |
| `nearWhite`, `nearBlack` | covered pixels with every channel at 240 or above / 12 or below |
| `flat` | luminance and every channel's standard deviation at most 0.012 |

The summary leads with the one fact that matters most: the screen draws almost nothing (under 0.5 % covered), is near black
(90 % or more), near white (30 % or more), one flat colour, or else its coverage, mean colour and luminance range; then how
many texts are hard to read (DUI8007), how many layout problems `layout` would report, and the image's path.

`background` is what shows behind the screen: `#RRGGBB` (`#1C1E26` by default) or `checker`. A `timeline` block does not
play by itself: `timeline` names one of the file's to play from its start, looping, once the screen has settled. `time`
runs the screen that many seconds before the picture, advancing the timeline, tweens and particles; `frames` (up to 16)
spread over `duration` seconds are put side by side in one PNG (8192 px wide at most), and `data.animation` says whether they
differ, by how much, and whether they zigzag (more frames or a shorter duration needed). A `RenderTranslation` key moves
along X toward or away from the viewer (X is depth, Y right, Z up): `(0, -400, 0)` to `(0, 400, 0)` slides a node across,
`(-400, 0, 0)` brings it 400 units nearer and draws it much larger.

### Screens in 3D

| What | In `preview` |
| --- | --- |
| A render transform that turns a widget out of the canvas plane (`RenderRotation`, `RenderTranslation` with depth) | drawn in perspective, through the canvas's own camera, as the game draws a screen-space canvas |
| Layer effects, blend modes, clip masks, rect blocks, text effects | drawn: they are part of the screen's own draw |
| A screen shown on a panel in a level (a world-space canvas), seen through a camera | not drawn: the stage draws the screen itself, flat, into its picture; put the screen in the level and look at it in the editor |
| Niagara particles in a screen | drawn: the stage holds each system and steps it by the preview's own time, so `time` and the frames show the particles a game would after as many seconds; a system that waits for a pointer or a click (`bAutoActivate = false`) shows nothing until something starts it |

## See also

- [Working with AI agents](ai-agents.md): the skills, the scripts, and what an agent without DreamMCP does.
- [Diagnostics](../diagnostics/index.md): every `DUInnnn` code.
- [Compiling from the command line](scripting.md#compiling-from-the-command-line): the commandlet `.skill/dui.ps1` drives.
