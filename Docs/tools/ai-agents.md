---
title: Working with AI agents
description: What an AI agent has to work with when it writes .dui — the DreamMCP endpoint and its layout and preview, the skills, the self-contained references, dui.ps1 and the commandlet — and the loop that gets a screen from a request to a compiled widget Blueprint that fits the screen.
---

`.dui` is not in any model's training data. An agent that writes it from general knowledge invents node types and
properties that do not exist; one that gets the names right still lays screens out badly, because it cannot see them.
What it needs is the reference, the names the compiler accepts, a fast way to compile and read the errors, and a way to
see what it made. All of it is in the plugin.

## What there is

| Piece | Where | What it gives the agent |
| --- | --- | --- |
| DreamMCP | the [endpoint](dreammcp.md), reached through a [DreamMCP server](https://github.com/TypeDreamMoon/dream-mcp) as the `dream_gui` tool | `check`, `compile`, `diagnostics`, `lookup`, `layout` (every node's rect in pixels and the layout problems, in words), `preview` (a picture and its figures), `info` — in the user's editor or a background engine, in a fraction of a second |
| The skills | `.skill/dream-gui-create`, `-verify`, `-diagnose`; published into `.claude/skills/` by `.skill/sync-skills.ps1` | Fixed steps, one command each: look names up, write, check, lay out, fix |
| The references | `.skill/reference/`: `dui.md` (the whole language on one page), `example.md`, `pitfalls.md`, `controls.md`, `diagnostics.md` | What an author needs, each page standing alone; DreamMCP serves them through `reference` |
| The agent guide | `AGENTS.md` at the plugin's root; `CLAUDE.md` imports it | The loop below and the rules agents get wrong |
| `dui.ps1` | `.skill/dui.ps1` | The endpoint's `check`, `compile` and `layout` from the command line, for CI and agents with no DreamMCP server |
| The symbols file | `<Project>/DUI/.dui-symbols.json` | Every node type, property, enum value, event and view model class ([the symbol file](vscode.md#the-symbol-file-duidui-symbolsjson)); `lookup` reads the same table |
| The commandlet | `-run=DreamUICompileWidgets` | Creates, compiles and saves widget Blueprints from a manifest ([compiling from the command line](scripting.md#compiling-from-the-command-line)) |
| The diagnostic catalogue | [Diagnostics](../diagnostics/index.md) | What each `DUInnnn` code means and how to fix it, the layout codes (DUI8xxx) included |

## Giving the guide to an agent in a game project

Agents read the instructions at the root of the folder they work in, which is your game project, not the plugin.
Add one line to your project's own instructions file:

```text
Before writing or fixing any .dui file, read Plugins/DreamGUI/AGENTS.md and follow its Authoring section.
```

In Claude Code, a line `@Plugins/DreamGUI/AGENTS.md` in the project's `CLAUDE.md` imports the guide, and
`pwsh -NoProfile -File Plugins/DreamGUI/.skill/sync-skills.ps1` publishes the skills. Run it again after updating the
plugin; `-Check` says whether the published copy is stale.

## The loop

With the `dream_gui` tool (calls are `{ "action": ..., "args": { ... } }`):

1. **Read** the references (`reference`), the language page first.
2. **Look up** every node type and property before writing it: `lookup { "name": "Text" }`, `Text.FontSize`,
   `VerticalBox`, `Widget` (what every node takes), `slot` (what `@slot` takes).
3. **Write** the file under `<Project>/DUI/`; its `class` line names the widget Blueprint, such as `class /Game/UI/WBP_Hello`.
4. **Check**: `check { "file": "DUI/Hello.dui" }` writes nothing and lists every error with its line and code.
5. **Lay out**: `layout { "file": "DUI/Hello.dui", "sizes": "1280x720, 2560x1440" }` compiles the screen (components it
   places first) and answers with the screen, one node a line — type, id, `x y w h`, its text and whether it fits —
   and the problems: off the screen, text cut, zero size or too small to press, buttons on top of each other, edges a
   pixel or two off, what breaks at another size. Fix, and lay out again until nothing is reported.
6. **Look**: `preview` draws the screen into a PNG; an agent that cannot see images reads its coverage, colours and the
   texts that are hard to read.

Without DreamMCP, `dui.ps1` does steps 4 and 5 (and `compile`) from the command line. It uses the user's editor when it
is serving the project, and otherwise starts a headless engine for the call:

```text
pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 check DUI/Hello.dui
pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 layout DUI/Hello.dui -Sizes "1280x720"
```

It prints diagnostics as `File.dui(line,col): error DUI3001: …` and ends with `RESULT=OK` or `RESULT=FAILED` (exit 0, 1,
or 2 when it could not run). Names then come from `DUI/.dui-symbols.json`, which the editor writes on startup when the
project's `DUI/` folder exists.

### With the editor open

The editor serves DreamMCP itself while *Project Settings ▸ Plugins ▸ Dream GUI ▸ Editor ▸ Enable DreamMCP Endpoint* is
on ([the endpoint](dreammcp.md)), so the agent's calls go to it, and a compile leaves the Blueprints in memory for you to save, as saving a
`.dui` does. Saving a `.dui` also recompiles the **loaded** classes built from it
([save to recompile](../getting-started/workflow.md#save-to-recompile)), and the editor rewrites
`DUI/.dui-diagnostics.json` after each compile.

## What agents get wrong

- Names from other UI systems: a type or property that is not in the symbols table is an error (`DUI3003`, `DUI4001`),
  never a no-op. `lookup` first.
- Absolute positions measured from the centre: the screen fits at one size and leaves it at another (`DUI8001`,
  `DUI8006`). Anchors to edges and boxes with `Spacing` hold at every size.
- A text in a box that gives it its width, with no `WrapTextAt`: measured as narrow as it can be (`DUI8002`).
- `Style.X = v` with a style sheet in the project: it compiles and changes nothing until `StyleSource = Inline`
  ([styles](../controls/styles.md#in-dui-and-at-run-time)).
- A `.dui` under `Content/`. Source files live under `DUI/`.
- Editing a `.dui`-backed Blueprint's hierarchy in the designer. The file owns the hierarchy; the designer refuses
  structural edits.

## When the agent still gets it wrong

Open an issue with the [*AI wrote it wrong* form](https://github.com/TypeDreamMoon/DreamGUI/issues/new/choose). Give
the tool and model, your request as you wrote it, the `.dui` it produced, and what `layout` said about it. Those reports
are how the guide, the skills and the layout rules improve. Contributing to the plugin itself with an agent: see
[Contributing](../contributing/index.md).
