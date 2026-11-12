# DreamGUI — agent guide

DreamGUI is a UI plugin for Unreal Engine 5.8: widgets in a tree, batched by a canvas, on the screen or in the level.
Its text language is `.dui`. A `.dui` file is the source of a widget Blueprint (`UDreamWidgetBlueprint`): compiling
the Blueprint reads the file and builds its widget tree.

Two kinds of work happen here. Pick the section that matches yours.

- **Authoring `.dui` in a game project** that has this plugin installed → [Authoring](#authoring).
- **Changing the plugin itself** (C++, tools, docs) → [Changing the plugin](#changing-the-plugin).

## Authoring

Paths in this section are relative to the game project's root. The plugin is in `Plugins/DreamGUI/`.

`.dui` is not in your training data. Read the reference and copy from the examples. Do not guess from QML, XAML,
UMG or CSS habits. A screen that compiles can still be laid out wrong: it is done when it also fits.

**If you have the `dream_gui` tool, a DreamMCP server is connected: use it for every step.** It answers in the user's
editor or in an engine it keeps running, in a fraction of a second. `help` gives each action's arguments.

1. Read: the `reference` action lists the pages (the language, a worked example, pitfalls, controls, diagnostics).
   Without the tool: `Plugins/DreamGUI/.skill/reference/dui.md`, `example.md` and `pitfalls.md`.
2. Look up every node type and property before writing it: `lookup` (`Text`, `Text.FontSize`, `VerticalBox`, `Widget`,
   `slot`). Without the tool: `<Project>/DUI/.dui-symbols.json`, which the editor writes when `<Project>/DUI/` exists.
3. Write the source under `<Project>/DUI/` (or `<Plugin>/DUI/`), never under `Content/`. The file's `class` line
   names the Blueprint it is the source of, for example `class /Game/UI/WBP_Hello`.
4. `check` the file (it writes nothing), fix every error at its line, and check again.
5. `layout` it at the sizes that matter (`sizes: "1280x720, 2560x1440"`): it compiles the screen (components it places
   first), lists every node's rect and text, and reports what is off the screen, cut, too small or on top of another
   (DUI8xxx). Fix those and run it again. `preview` draws the screen; its figures speak for the picture.

Without the tool, the same verbs from the command line (each run starts an engine; write everything first):
`pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 check|compile|layout <file>`, `check -All` for every file.
The skills `dream-gui-create`, `-verify` and `-diagnose` in `Plugins/DreamGUI/.skill/` walk these steps;
`pwsh -NoProfile -File Plugins/DreamGUI/.skill/sync-skills.ps1` publishes them into `.claude/skills/`. A diagnostic reads
`File.dui(line,col): error DUI3001: …`; every code is in `Plugins/DreamGUI/Docs/diagnostics/index.md`. More:
`Plugins/DreamGUI/Docs/tools/dreammcp.md`. The commandlet `-run=DreamUICompileWidgets -Manifest=DUI/widgets.json`
(`Docs/tools/scripting.md`) compiles a list of widgets with no endpoint, components listed first.

### Rules

- Copy only from the reference, `Plugins/DreamGUI/Docs/` and `Plugins/DreamGUI/Content/Samples/`. Test `.dui` files
  are not examples.
- `parent` must be `UDreamTextUserWidget` or a subclass. Leave it out to get `UDreamTextUserWidget`.
- Use only names that appear in `.dui-symbols.json` or the class reference. An unknown name is an error, not a no-op.
- `Style.X = v` compiles but changes nothing while the project has a style sheet and `StyleSource` is the default.
  Write `StyleSource = Inline` first (`Plugins/DreamGUI/Docs/controls/styles.md`).
- Colour literals such as `#8C8C94` are sRGB. Read the pitfalls page before you debug a "property that did not take".
- Once a Blueprint names its `.dui`, the file owns the hierarchy. Change the file; the designer refuses structural edits.
- Do not edit `DUI/.dui-symbols.json` or `DUI/.dui-diagnostics.json`. The editor writes them.

## Changing the plugin

Paths in this section are relative to the root of this repository (the plugin folder).

### Layout

| Module | Type | What it holds |
| --- | --- | --- |
| `DreamGUIRenderer` | Runtime | The view extension, shaders, vertex formats and render passes. Knows nothing of widgets. |
| `DreamGUI` | Runtime | The core: widgets, canvases, the `.dui` syntax tree, tree builder and diagnostics. |
| `DreamGUIInput` | Runtime | Event systems, raycasters, navigation, drag and drop, tooltips, modals. |
| `DreamGUIControls` | Runtime | The `Dream*` controls and the `UI*` behaviours they are built from. |
| `DreamGUIExtensions` | Runtime | Lines, polygons, rings, mesh visuals, retainer and render-target widgets. |
| `DreamGUISamples` | Runtime | The showcase and the controls gallery. |
| `DreamTween` | Runtime | Tween animation. |
| `DreamGUINiagara` | Runtime | Niagara particles drawn by DreamGUI. |
| `DreamGUIEditor` | UncookedOnly | The designer, the Blueprint compiler, write-back, the commandlets. |
| `DreamGUIK2Nodes` | UncookedOnly | Blueprint nodes. |
| `DreamGUIPsd` | UncookedOnly | The layered-document importer. |
| `DreamGUINiagaraEditor` | UncookedOnly | The editor side of the Niagara visual. |
| `DreamGUITests` | Editor | The automation suite. |

Module layers and what each depends on: `Docs/getting-started/project-layout.md`.

### Build

Build inside a host project that has this checkout as `Plugins/DreamGUI`. Build the whole editor target, never
`-Module=` (a restricted build leaves the plugin's module manifest stale):

```text
"<Engine>/Engine/Build/BatchFiles/Build.bat" <Project>Editor Win64 Development -Project="<Project>/<Project>.uproject" -WaitMutex
```

`Tools/TestHost/New-DreamGUITestHost.ps1` makes a minimal host project whose plugin is a git worktree of this
repository. Its defaults are the maintainer's machine: pass `-Root`, `-RepoPath`, `-Branch` and `-EngineRoot`.
See `Tools/TestHost/README.md`.

### Test

Run this before every pull request. It runs the static checks, builds, and runs the `Quick` preset headless
(the editor run is capped at 20 minutes):

```text
pwsh -NoProfile -File Tools/Tests/Invoke-DreamGUITests.ps1 -Project "<Project>/<Project>.uproject" -Engine "<Engine>"
```

Exit 0 is green, 1 is red, 2 is "not judged" (static checks, build, crash, too few tests). It prints one line
that starts with `RESULT`; that line goes into the pull request. The full report is
`<Project>/Saved/DreamGUITestReports/<stamp>-Quick/summary.md`. Close every editor of that project first.
Without an engine you can still run `python Tools/Tests/static_checks.py`. Presets and the rest:
`Tools/Tests/README.md`.

### Rules

- New property meta tags start with `DreamGUI_` (`DreamGUI_Section`, `DreamGUI_Handle`, …). See
  `Docs/guides/property-meta.md`.
- Diagnostic codes are stable. Never renumber or reuse one. A new code goes into `EDreamUIDiagnosticCode`
  (`Source/DreamGUI/Public/Text/DreamUIDiagnostics.h`) and into `Docs/diagnostics/index.md` and
  `Docs/diagnostics/codes.md`.
- `.skill/reference/` is built from `Docs/` (the language, the worked example, pitfalls, controls, diagnostics). After
  editing one of those pages run `pwsh -File .skill/build-reference.ps1`; `-Check` reports drift. A code block whose
  first line is `// DUI/<path>.dui` is a whole file that agents copy: `DreamGUI.MCP.Reference.*` compiles and lays out
  every one and fails on any error or warning, so keep them self-contained (no handler the class must supply).
- `Docs/Reference/` is generated. Do not edit it. Regenerate with
  `UnrealEditor-Cmd.exe "<Project>/<Project>.uproject" -run=DreamGUIReferenceDocs -culture=en` (without `-culture=en`
  an editor in another language writes its translated tooltips).
- A runtime file includes only its own module's and lower layers' headers. A new or moved file gets its row in
  `Tools/Tests/module-owners.csv`. The `layering` static check enforces it.
- Test names are `DreamGUI.<Area>.<Sentence>`. A test that reads pixels carries `NonNullRHI`. Preset floors in
  `Tools/Tests/presets.json` only go up.
- Unreal Engine 5.8 only; Win64 is the only platform built and run. There are no engine-version macros.
- English under `Docs/` is the source. `Docs/zh-CN/` is translated by CI; never edit `Docs/zh-CN/.align/`.
- A change a project can notice gets a line in `CHANGELOG.md` under `## Unreleased` (`### New`, `### Changed`,
  `### Fixed`). Behaviour changes update the page in `Docs/` that describes them.
- Commits: one topic each, `type(scope): sentence`, for example `fix(text): …`. No planning labels, no absolute
  local paths, no personal data.

### Where things are documented

- Contributing: `CONTRIBUTING.md`, `Docs/contributing/index.md`
- Tests and the test host: `Docs/tools/tests.md`, `Tools/Tests/README.md`, `Tools/TestHost/README.md`
- The language: `Docs/language/overview.md`; diagnostics: `Docs/diagnostics/index.md`
- Agents: `Docs/tools/ai-agents.md`, `Docs/tools/dreammcp.md`, `.skill/README.md`; scripting: `Docs/tools/scripting.md`
- The published manual: <https://gui.toolchain.64hz.cn>

## Don't

- Don't build or run the suite while an editor has that project open. Use the test host.
- Don't build with `-Module=`.
- Don't hand-edit `Docs/Reference/`, `Docs/zh-CN/.align/` or the editor-written files in `DUI/`.
- Don't renumber diagnostic codes or lower a preset floor to make a run pass.
- Don't open a pull request without the `RESULT` line of the `Quick` run.
