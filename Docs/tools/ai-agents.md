---
title: Working with AI agents
description: What an AI agent has to work with when it writes .dui — AGENTS.md, the language reference, the symbols file, the headless DreamUICompileWidgets commandlet and the diagnostics file — and the loop that gets a file from a request to a compiled widget Blueprint.
---

`.dui` is not in any model's training data. An agent that writes it from general knowledge invents node types and
properties that do not exist. What it needs is the reference, the list of names the compiler accepts, and a way to
compile and read the errors. All three are in the plugin. There are no agent skills and no MCP server for DreamGUI.

## What there is

| Piece | Where | What it gives the agent |
| --- | --- | --- |
| The agent guide | `AGENTS.md` at the plugin's root; `CLAUDE.md` imports it | The loop below, the rules agents get wrong, and the rules for changing the plugin |
| The language reference | `Docs/language/`, starting at [the overview](../language/overview.md); a [worked example](../language/example.md) | Every construct, one page per topic |
| Samples | `Content/Samples/HelloDreamGUI.dui`, `Content/Samples/NiagaraEffects.dui` | Small complete files to copy from; the test suite compiles both and fails on any error. The `.dui` files under `Source/DreamGUITests/` are test input, not examples |
| The symbols file | `<Project>/DUI/.dui-symbols.json` | Every node type, property, enum value, event and view model class the compiler accepts ([the symbol file](vscode.md#the-symbol-file-duidui-symbolsjson)) |
| The headless compiler | the `DreamUICompileWidgets` commandlet | Creates, compiles and saves widget Blueprints from `.dui` without an editor window ([compiling from the command line](scripting.md#compiling-from-the-command-line)) |
| The diagnostics file | `<Project>/DUI/.dui-diagnostics.json` | What the running editor's last compile of each file said |
| The diagnostic catalogue | [Diagnostics](../diagnostics/index.md) | What each `DUInnnn` code means and how to fix it |

## Giving the guide to an agent in a game project

Agents read the instructions at the root of the folder they work in, which is your game project, not the plugin.
Add one line to your project's own instructions file:

```text
Before writing or fixing any .dui file, read Plugins/DreamGUI/AGENTS.md and follow its Authoring section.
```

In Claude Code, a line `@Plugins/DreamGUI/AGENTS.md` in the project's `CLAUDE.md` imports the guide.

## The loop

1. **Read** the overview, the worked example and the `.dui` section of [Common pitfalls](../guides/pitfalls.md).
2. **Look up** every node type and property in `DUI/.dui-symbols.json` before using it. The editor writes the file on
   startup, and again on `DreamUI.ExportSymbols` at its console, but only when the project's `DUI/` folder exists.
3. **Write** the file under `<Project>/DUI/`. Its `class` line names the widget Blueprint, such as
   `class /Game/UI/WBP_Hello`.
4. **Compile.** With the editor closed, list the widgets in a manifest, `<Project>/DUI/widgets.json`:

   ```json
   { "widgets": [ { "asset": "/Game/UI/WBP_Hello", "source": "Hello.dui" } ] }
   ```

   and run:

   ```text
   "<Engine>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<Project>/<Project>.uproject" -run=DreamUICompileWidgets -Manifest=DUI/widgets.json
   ```

   The manifest path is relative to the project folder, `source` to `DUI/`. A component comes before the screens that
   place it. The exit code is non-zero when anything failed, and the run ends with
   `DreamUICompileWidgets: N compiled, M failed, of K.`
5. **Read the errors.** Each is printed as `File.dui(line,col): error DUI3001: …`. Look the code up in the
   [catalogue](../diagnostics/index.md), fix the line, and compile again until every widget says
   `compiled and saved`.

### With the editor open

The commandlet saves the Blueprints it compiles, so do not run it while an editor has the project open. With the
editor open, use it instead:

- Saving a `.dui` recompiles the **loaded** classes built from it ([save to recompile](../getting-started/workflow.md#save-to-recompile)).
  *Tools ▸ Rebuild DUI* recompiles every text-backed widget Blueprint.
- After each compile the editor rewrites `DUI/.dui-diagnostics.json`: one entry per file, each with a list of
  `{ "code", "severity", "line", "column", "message" }`. `"code": 3001` is `DUI3001`. An empty list means the file
  compiled clean. An agent can read this file instead of the log.

## What agents get wrong

- Names from other UI systems: a type or property that is not in the symbols file is an error (`DUI3003`,
  `DUI4001`), never a no-op.
- `Style.X = v` with a style sheet in the project: it compiles and changes nothing until `StyleSource = Inline`
  ([styles](../controls/styles.md#in-dui-and-at-run-time)).
- A `.dui` under `Content/`. Source files live under `DUI/`.
- A manifest `asset` that differs from the file's `class` line.
- Editing a `.dui`-backed Blueprint's hierarchy in the designer. The file owns the hierarchy; the designer refuses
  structural edits.

## When the agent still gets it wrong

Open an issue with the [*AI wrote it wrong* form](https://github.com/TypeDreamMoon/DreamGUI/issues/new/choose). Give
the tool and model, your request as you wrote it, and the `.dui` it produced. Those reports are how the guide and the
examples improve. Contributing to the plugin itself with an agent: see [Contributing](../contributing/index.md).
