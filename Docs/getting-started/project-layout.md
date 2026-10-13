---
title: Project layout
description: The runtime module layers and what each holds, which module your Build.cs adds, where .dui files live, what each plugin folder is, and the log categories.
---

## Module layers

The runtime is split into modules by layer. A module depends only on modules in the layers below its own, never on a
sibling in its layer, and `Tools/Tests/static_checks.py` fails an include that goes the other way (rule `layering`).
The UI's six runtime modules load at `PostConfigInit`, so their types and `.dui` tags are in place before anything
compiles.

```text
L4   DreamGUISamples
L3   DreamGUIControls      DreamGUIExtensions
L2   DreamGUIInput
L1   DreamGUI (core)
L0   DreamGUIRenderer      DreamTween
     ------------------------------------------------------------
     DreamGUIEditor, DreamGUIK2Nodes (uncooked only), DreamGUITests (editor only)
```

| Module | Layer | Holds |
| --- | --- | --- |
| `DreamGUIRenderer` | L0, below the core | The view extension that draws DreamUI and the render command that draws a render-target canvas, their shaders, the vertex and index formats, the material proxies a canvas answers its parameters through, the post-process proxies with the screen reads and writes the effects share, and the stage timing behind `DreamUI.Stats`. It knows nothing of widgets: the core registers what it asks for |
| `DreamGUI` | L1, the core | Widgets, visuals, canvas batching, layout, text and `.dui`, animation, the event contracts, the render root that holds a canvas's sections for the renderer, and the PNG capture |
| `DreamGUIInput` | L2, above the core | The input system: the event systems and their preset actors, the raycasters and input modules, the action router, navigation, drag and drop, tooltips and modals, the selectable base the controls are built on, and the game viewport client |
| `DreamGUIControls` | L3, above input | The control library: the `Dream*` controls (button, toggle, slider, lists, dialog, tab view, ...), the `UI*` behaviours they are built from, the action bar, style sheets and the UMG interop |
| `DreamGUIExtensions` | L3, above input | 2D lines, polygons and rings, the static-mesh visual, the retainer box and the render-target helpers, lyrics, the concrete mesh modifiers, and the background blur, pixelate and pixel sort effects |
| `DreamGUISamples` | L4, above the controls | The showcase and the controls gallery |
| `DreamTween` | L0, independent | Tweens; loads at `Default` |
| `DreamGUIEditor`, `DreamGUIK2Nodes` | uncooked only | The designer and the asset tools; the Blueprint nodes. Loaded by every process that runs uncooked content, a game started from the editor (`-game`, Standalone Game) included: an editor build drops a Blueprint's saved bytecode on load and rebuilds it from the Blueprint, and the widget Blueprint class and its compiler live here. Outside the editor only the compiler starts |
| `DreamGUITests` | editor | The automation suite |

### Which module your C++ adds

C++ that uses a type from a split-off module adds that module to its `Build.cs`. Roughly:

| You use | Module |
| --- | --- |
| Drawing, effects' render proxies | `DreamGUIRenderer` |
| Widgets, visuals, canvases, `UDreamUIBPLibrary` | `DreamGUI` |
| Event systems, raycasters, navigation, the viewport client, `DreamUITextInputRouter` | `DreamGUIInput` |
| The `Dream*` controls and the `UI*` behaviours | `DreamGUIControls` |
| Lines, rings, effects, render-target helpers | `DreamGUIExtensions` |

```cpp
// MyGame.Build.cs
PublicDependencyModuleNames.AddRange(new string[] { "DreamGUI", "DreamGUIInput", "DreamGUIControls" });
```

Assets need nothing: every type that moved still loads under its old name (see [Migration](../guides/migration.md)).

## Where .dui files live

Under a `DUI/` directory: the project's (`<Project>/DUI/`) or an enabled plugin's (`<Plugin>/DUI/`). **Not under
`Content/`** — a `.dui` is source, not an asset: under `Content/` the cooker walks it, the Content Browser shows a file it
cannot open, and a plugin shipping widget classes would have to ship its sources inside its cooked content to have
them found.

A path to another file — in a Blueprint's Source File, or after `use` — is written one of three ways:

| Spelling | Means |
| --- | --- |
| `Panels/Settings.dui` | Relative to a `DUI/` root: the project's first, then each plugin's, the first that has the file |
| `Plugin.MyPlugin:Panels/Settings.dui` | That plugin's `DUI/` directory |
| `D:/Work/Proj/DUI/Panels/Settings.dui` | Absolute, used as written |

A bare relative path means "whichever root has it" — convenient, and ambiguous the moment two roots hold the same
relative path, which is why the plugin-qualified spelling exists. When you pick a file in the designer, the absolute
path the picker hands back is turned into a portable one: relative under the project's root, `Plugin.X:…` under a
plugin's, and absolute only when the file is under no root at all (refusing it would be a picker that silently
discards your choice).

```dui
use "Settings/SettingsLibrary.dui"
use "Plugin.MyPlugin:Panels/Row.dui" as Row
use /Game/UI/WBP_Slider as Slider

VerticalBox Root {
    Spacing = 12
    Row Audio { Label = "Audio" }
    Slider Volume
}
```

The editor also writes two files into the project's `DUI/`: `.dui-symbols.json` (rewritten on start and on
`DreamUI.ExportSymbols`; the symbol table the VS Code extension completes from) and `DreamUI.code-workspace`
(rewritten from the current source roots, and opened, by *Tools ▸ Open DreamUI Workspace (VSCode)*).

A project using DreamGUI looks roughly like this:

```text
MyProject/
  Config/
    DefaultDreamGUI.ini
    DefaultEngine.ini
  Content/
    UI/
      WBP_HelloDreamGUI.uasset
  DUI/
    HelloDreamGUI.dui
    .dui-symbols.json
    DreamUI.code-workspace
  Plugins/
    DreamGUI/
```

The project's `Config/DefaultDreamGUI.ini` holds the values of *Project Settings ▸ Plugins ▸ Dream GUI* (see
[Installation](installation.md)).

## What is in the plugin folder

```text
DreamGUI/
  Config/
    Game.ini
    FilterPlugin.ini
  Content/
  DShader/
  Docs/
  Resources/
  Shaders/
  Source/
  ThirdParty/
  Tools/
  DreamGUI.uplugin
  CHANGELOG.md
  LICENSE
  README.md
```

### Config

Since 1.0.0 the plugin ships no CoreRedirects; how an asset saved under an old name is resaved once is in
[Upgrading](../guides/upgrading.md#from-21-to-100).

- **`Game.ini`** — puts `/DreamGUI` into `DirectoriesToAlwaysCook`. The assets the plugin reaches for by itself (the
  default material, font, sprite presets, ...) are soft pointers on a native CDO and create no asset dependency; without
  this line, a cook driven by an explicit package list ships none of them and the whole UI draws blank. The file has to
  be called `Game.ini` to be layered into the Game branch.
- **`FilterPlugin.ini`** — BuildPlugin packages only `/Source`, `/Content`, `/Resources`, `/Shaders` and
  `/Binaries/ThirdParty` by default; this lists everything else that has to travel with the plugin: `Game.ini` above,
  the README, the CHANGELOG, the documents and reference under `Docs/`, and msdfgen's source and licence.

### Content

The default fonts (`DefaultFont_DistanceField` with its `_Bold`, `_Italic`, `_BoldItalic` and `_CJK` faces, and
`DefaultFont_Bitmap`), the materials and material functions under `Materials/`, sprite and texture presets, the event
system actors under `Blueprints/`, the control Blueprints under `Controls/` (`BP_Button`, `BP_Toggle`, `BP_Dropdown`,
...), the input actions and mapping context under `EnhancedInput/` (`IA_*`, `IMC_DreamUIInputContext`), and
`Samples/HelloDreamGUI.dui`.

### DShader

The sources (`.dsm` / `.dsf`) of `Content/Materials`. The `.uasset` files are build output committed alongside them, so
**DreamGUI has no dependency on DreamShader**: without it the materials are ordinary `UMaterial` and
`UMaterialFunction` assets. DreamShader is only needed to rebuild them from text.

### Docs

`DuiLanguage.md` (the whole `.dui` language), `Migration.md`, `FontsAndPackaging.md`, and `Reference/` — the property
and function reference, one page per class, printed from reflection rather than written, so it cannot fall behind the
headers:

```powershell
UnrealEditor-Cmd.exe <project>.uproject -run=DreamGUIReferenceDocs
```

This site's [API reference](../Reference/index.md) is those pages.

### Resources

The plugin icon, the editor icons, and `UMGParity/`: one table per UMG class (58 of them), each member marked as here
under the same name and meaning, here under another name or on another type, or deliberately absent and why. The
automation suite holds the tables against UMG's own reflection, in both directions. See
[How it differs from UMG](about.md) and [UMG parity](../controls/umg-parity.md).

### Shaders, Source, ThirdParty

`Shaders/Private` holds the renderer's `.usf` / `.ush`; `Source/` the ten modules above; `ThirdParty/` the msdfgen
submodule and the committed single-file copy (see [Installation](installation.md)).

### Tools

| Folder | What it is |
| --- | --- |
| `Tools/Tests` | The test runner `Invoke-DreamGUITests.ps1`, its presets, the static checks, the result digest (see [Tests](../tools/tests.md)) |
| `Tools/TestHost` | A minimal host project that exists only to build the plugin and run its tests, and the packaged text smoke test |
| `Tools/Bench` | The two benchmarks the performance work was measured with: a screen of 5000 buttons and a level of 2688 world-space panels (see [Benchmarks](../tools/bench.md)) |
| `Tools/Fonts` | `make_default_fonts.py`, which builds the default font |
| `Tools/TextParity` | DreamGUI, Slate and Chrome on one text corpus |
| `Tools/ModuleSplit` | The scripts a runtime module is split off the core with |
| `Tools/UpdateMsdfgen.ps1` | Regenerates the single-file msdfgen copy from the submodule |

## Log categories

| Category | Used by |
| --- | --- |
| `DreamGUI` | The core and the runtime modules above it (Input, Controls, Extensions, Samples); part of the editor uses it too |
| `LogDreamGUIRenderer` | `DreamGUIRenderer`. It sits below the core and cannot use the core's category, so it has its own |
| `DreamTween` | `DreamTween` |
| `DreamGUIEditor` | The designer and the asset tools |
| `DreamGUIK2Nodes` | The Blueprint nodes |
| `LogDreamGUIReferenceDocs` | The commandlet that prints the reference |

The stat groups are `stat DreamGUI` (declared in the renderer, so the renderer and every module above it count into the
same group) and `stat DreamTween`.
