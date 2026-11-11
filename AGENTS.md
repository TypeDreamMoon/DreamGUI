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
UMG or CSS habits.

1. Read: `Plugins/DreamGUI/Docs/language/overview.md` (the reference, one page per topic in that folder),
   `Plugins/DreamGUI/Docs/language/example.md` (a worked example), and the `.dui` section of
   `Plugins/DreamGUI/Docs/guides/pitfalls.md`. Small complete files: `Plugins/DreamGUI/Content/Samples/HelloDreamGUI.dui`
   and `Plugins/DreamGUI/Content/Samples/NiagaraEffects.dui` (the test suite compiles both and fails on any error).
2. Look up what exists: `<Project>/DUI/.dui-symbols.json` lists every node type, property, enum value, event and view
   model class the compiler accepts. The editor writes it on startup, or at its console with `DreamUI.ExportSymbols`,
   and only when `<Project>/DUI/` exists. Classes: `Plugins/DreamGUI/Docs/Reference/` (for example `DreamButton.md`).
   Controls: `Plugins/DreamGUI/Docs/controls/index.md`.
3. Write the source under `<Project>/DUI/` (or `<Plugin>/DUI/`), never under `Content/`. The file's `class` line
   names the Blueprint it is the source of, for example `class /Game/UI/WBP_Hello`.
4. Build it headlessly. List the widgets in a manifest, for example `<Project>/DUI/widgets.json`:

   ```json
   { "widgets": [ { "asset": "/Game/UI/WBP_Hello", "source": "Hello.dui" } ] }
   ```

   Then run, with every editor of that project closed:

   ```text
   "<Engine>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<Project>/<Project>.uproject" -run=DreamUICompileWidgets -Manifest=DUI/widgets.json
   ```

   A missing Blueprint is created, then every entry is compiled and saved. `-Manifest` is relative to the project
   folder; `source` is relative to `DUI/`. `-Only=WBP_A,WBP_B` limits the run. `-All [-Path=/Game/UI]` recompiles the
   text-backed Blueprints that already exist. The exit code is non-zero when anything failed. The last line reads
   `DreamUICompileWidgets: N compiled, M failed, of K.`
5. Read the diagnostics: each one is printed as `File.dui(line,col): error DUI3001: …`. Look the code up in
   `Plugins/DreamGUI/Docs/diagnostics/index.md` (or <https://gui.toolchain.64hz.cn/en/docs/diagnostics>). Fix, and build again until
   every entry says `compiled and saved`.

There are no agent skills and no MCP server for DreamGUI. The commandlet above is the headless entry point.

### Rules

- Copy only from `Plugins/DreamGUI/Docs/` and `Plugins/DreamGUI/Content/Samples/`. Test `.dui` files are not examples.
- A manifest entry's `asset` must be the path in the file's `class` line.
- A manifest lists components before the screens that place them. Order matters.
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
- `Docs/Reference/` is generated. Do not edit it. Regenerate with
  `UnrealEditor-Cmd.exe "<Project>/<Project>.uproject" -run=DreamGUIReferenceDocs`.
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
- Agents: `Docs/tools/ai-agents.md`; scripting and the commandlet: `Docs/tools/scripting.md`
- The published manual: <https://gui.toolchain.64hz.cn>

## Don't

- Don't build or run the suite while an editor has that project open. Use the test host.
- Don't build with `-Module=`.
- Don't hand-edit `Docs/Reference/`, `Docs/zh-CN/.align/` or the editor-written files in `DUI/`.
- Don't renumber diagnostic codes or lower a preset floor to make a run pass.
- Don't open a pull request without the `RESULT` line of the `Quick` run.
