---
name: dream-gui-verify
description: Check, compile and lay out DreamGUI .dui screens — one file or every one — and read back each diagnostic and layout problem with its file, line and code. Use when asked to verify, validate, check, build, compile, test or CI-gate .dui files, or to find out whether a screen still compiles and still fits the screen.
---

# dream-gui-verify `<file>` | all

The gate every other DreamGUI skill ends with. Three levels, each further than the one before:

| Level | Goes as far as | Writes | Use it |
| --- | --- | --- | --- |
| `check` | the parser and the tree builder, as the compiler reads the file | nothing | first, after every edit |
| `compile` | the widget Blueprint (and the components the file places) | the Blueprints | to prove it builds; catches what only a compile sees: `props`, `events`, view models, expression bindings (DUI6xxx) |
| `layout` | the compiled screen laid out at a viewport size | the Blueprints, as `compile` | to prove it fits: off the screen, cut text, small or stacked buttons (DUI8xxx) |

Paths are relative to the game project's root.

## With DreamMCP (the `dream_gui` tool)

1. `dream_gui { "action": "check", "args": { "file": "DUI/MainMenu.dui" } }`, or `{ "all": true }` for every `.dui`.
2. `dream_gui { "action": "compile", "args": { "file": "DUI/MainMenu.dui" } }`. `force: true` compiles even what has not
   changed since the last compile.
3. `dream_gui { "action": "layout", "args": { "file": "DUI/MainMenu.dui", "sizes": "1280x720, 2560x1440" } }`.
4. `dream_gui { "action": "diagnostics" }` reads back what the engine holds for every file, errors first, without
   compiling anything.

Each result starts with one summary line, then one line per diagnostic: severity, code, `file:line:column`, message,
hint and a link to the code's page. The outcome is `ok`, `source-error` (fix the file), or `refused` with a reason and
what to do next: do that, do not switch to the script. A file is verified when `check` and `compile` are `ok` and
`layout` has no warnings at the sizes asked for.

## Without DreamMCP

```bash
pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 check DUI/MainMenu.dui
pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 check -All
pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 compile DUI/MainMenu.dui
pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 layout DUI/MainMenu.dui -Width 1920 -Height 1080 -Sizes "1280x720"
```

Each prints the summary, one line per diagnostic as `File.dui(line,col): severity DUInnnn: message`, and a last line
`RESULT=OK` or `RESULT=FAILED`. Exit code 0: no error; 1: the source has errors; 2: the call could not be made (no
project, no engine, no such file). When an editor of the project is open and serving DreamMCP, the script uses it and a
compile stays in that editor's memory; otherwise it starts a headless engine for the call (about ten seconds) and a
compile saves. `-Json` prints the whole result instead.

For a CI gate without any engine endpoint, the commandlet compiles a manifest of widgets:
`UnrealEditor-Cmd.exe <Project>.uproject -run=DreamUICompileWidgets -Manifest=DUI/widgets.json` (see the plugin's
`Docs/tools/scripting.md`).

## Rules

- Never run a compile of everything in a user's project to "check" it: `check all` reads every file and writes nothing.
- `layout` and `preview` are not part of a compile: their DUI8xxx findings are warnings and infos, and a screen with
  them still compiles. Fix them anyway; they are what a player sees.
- A `refused` result is not the source's fault (a file that is not there, an asset path that is not one, an editor that
  holds the project): read its reason and its next step.
