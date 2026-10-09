<p align="center">
  <img alt="DreamGUI banner" src="./Images/banner.png" />
</p>

<table>
  <tr>
    <td width="64%" valign="top">
      <h1>DreamGUI</h1>
      <p><strong>A 3D UI system for Unreal Engine 5.8.</strong></p>
      <p>
        A widget is a <code>UObject</code> with a rect, anchors and a pivot, arranged in a tree and batched by a
        canvas into as few draw calls as it can — on the screen, or standing in the level at any angle with correct
        hit testing either way. A UI tree is a widget Blueprint you subclass, written as <code>.dui</code> text or in
        a UMG-style designer that edits the same class. A control library, tweens, per-player input and gamepad
        navigation come with it.
      </p>
      <p>
        <img alt="Unreal Engine 5.8" src="https://img.shields.io/badge/Unreal%20Engine-5.8-313131" />
        <img alt="Version 1.0.0" src="https://img.shields.io/badge/version-1.0.0-blue" />
        <img alt="License MIT" src="https://img.shields.io/badge/license-MIT-green" />
        <img alt="Automation tests 2400+" src="https://img.shields.io/badge/automation%20tests-2400%2B-2f6bff" />
      </p>
      <p>
        <a href="README.zh-CN.md">中文文档</a> &nbsp;·&nbsp;
        <a href="https://gui.toolchain.64hz.cn/en/docs">Documentation</a> &nbsp;·&nbsp;
        <a href="https://gui.toolchain.64hz.cn/en/docs/start/installation">Getting started</a> &nbsp;·&nbsp;
        <a href="Docs/DuiLanguage.md">.dui language</a> &nbsp;·&nbsp;
        <a href="Docs/Reference/index.md">Class reference</a> &nbsp;·&nbsp;
        <a href="CHANGELOG.md">Changelog</a>
      </p>
      <p>
        <a href="https://github.com/TypeDreamMoon/DreamGUI/issues">
          <img alt="Issues" src="https://img.shields.io/github/issues/TypeDreamMoon/DreamGUI" />
        </a>
        <a href="https://github.com/TypeDreamMoon/dreamui-language-support">
          <img alt="VSCode Extension" src="https://img.shields.io/badge/VSCode-DreamUI-007ACC" />
        </a>
        <a href="https://github.com/TypeDreamMoon/DreamShader">
          <img alt="Sister project DreamShader" src="https://img.shields.io/badge/sister%20project-DreamShader-181717" />
        </a>
        <a href="https://github.com/TypeDreamMoon/DreamFX">
          <img alt="Sister project DreamFX" src="https://img.shields.io/badge/sister%20project-DreamFX-181717" />
        </a>
      </p>
    </td>
    <td width="36%" align="center" valign="middle">
      <img src="./Images/character.png" width="260" alt="DreamGUI character" />
    </td>
  </tr>
</table>

> [!TIP]
> Keep every `.dui` file in version control next to the widget Blueprint it is the source of. The Blueprint's
> hierarchy is compiled from the file, and a property changed in the designer is written back into it, so the text
> is what a review, a diff and a merge read.

> [!NOTE]
> **1.0.0 is the first public release.** The fork's development builds before it were numbered 1.x, 2.0.0 and
> 2.1.0 (commits `2416e3f8`, `3049561a` on `main`); a project on one of them reads
> [Docs/Migration.md](Docs/Migration.md#from-21-to-100) — 1.0.0 carries no CoreRedirects, so assets saved by those
> builds, or against LGUI and LexUI, are resaved once before they are opened with it. DreamGUI is a fork of
> [LGUI / LexUI](https://github.com/liufei2008/LGUI) by Lex Liu, MIT licensed — not a drop-in replacement for it.

---

## What it looks like

```text
class /Game/UI/WBP_Settings
use "UI/Library.dui" as ui

VerticalBox Root : ui.Page {
    Spacing = 24

    Text Title : ui.Heading { Text = "Settings" }

    rows ui.Row : ui.Wide (Label, Value) {
        "Subtitles",  "On"
        "Difficulty", "Normal"
        "Vibration",  "Off"
    }

    Native.Button Apply {
        Text { Text = "Apply" }
        OnClicked -> HandleApply
    }

    Text Hint : ui.Note {
        Shown <- bDirty
        Text  = "Unsaved changes"
    }
}
```

A library supplies the styles and the `Row` component, `rows` writes one instance per line, the button routes its
click to a function on the user widget, and the hint shows while a variable says so. Compiled, the file is an
ordinary widget Blueprint — a `UDreamWidgetBlueprint` whose class is a `UDreamUserWidget`:

```cpp
UDreamUIBPLibrary::AddWidgetOfClassToViewport(this, WBP_Settings);   // a layer on the screen
```

or dragged into a level, an `ADreamWorldWidgetActor` — the same class as a surface in the world.

## Quick start

1. Clone into the project's `Plugins/` directory, regenerate project files and build:

   ```bash
   git clone https://github.com/TypeDreamMoon/DreamGUI.git Plugins/DreamGUI
   ```

   That is the whole install for a fresh project, on a launcher install too: the plugin compiles against the
   engine's public headers only and carries its own generated msdfgen copy under `ThirdParty/`.
2. Open [`Content/Samples/HelloDreamGUI.dui`](Content/Samples/HelloDreamGUI.dui) — the smallest file that is still
   a real screen. Make a widget Blueprint for it (*Content Browser ▸ Add ▸ DreamGUI Widget*) with
   **DreamUI Text User Widget** (`UDreamTextUserWidget`) as its parent class — only such a class shows
   *Set Source File…* in the designer toolbar — point it at the file, and compile.
3. Show it: in a graph, **Create Dream Widget** then **Add to Viewport**, as with UMG -- the file's `props` are pins
   on the create node; in C++, `UDreamUIBPLibrary::AddWidgetOfClassToViewport`. Or drag the Blueprint into a level.
   The screen root, the raycaster and the event system are created on demand; nothing else needs configuring.

Two settings are worth making early — the game viewport client for non-US keyboard layouts, and the Slate input
source for the engine's UI-only input mode. Both are on
[Installation](https://gui.toolchain.64hz.cn/en/docs/start/installation).

## What's in it

| | |
| :-- | :-- |
| **Widgets and canvases** | Rects with anchors and a pivot, visuals that draw (text, images, rect blocks, polygons, rings, lines), a canvas that walks only the widgets that changed and patches its sections in place |
| **Screen and world** | Screen space, world space at any angle (DreamGUI's own renderer, or the engine's pipeline for post process and depth), render targets on any mesh; per-widget perspective; render transforms in three dimensions |
| **Widget Blueprints** | A tree is a class: subclass it, nest it, fill its named slots, reach its children by name; the designer edits the class directly, rebuilt against UMG's |
| **`.dui`** | Nodes, styles, resources, `use … as` libraries, components written in `.dui` with `props`, `events` and slots, bindings and routes, `if` / `for` / `each`, `rows` tables, timelines — every mistake a `DUInnnn` code with its line and column |
| **Layout** | UMG-shaped panels on a layout engine rebuilt along Blink's and Yoga's lines: const measurement, immutable fragments, invalidation with a reason |
| **Text** | Distance-field glyphs that stay sharp at any scale or angle, small text as crisp as Slate's, colour emoji, fallback faces, best fit, gradient text, culture-aware line breaking |
| **Controls** | Buttons, toggles, sliders, spin boxes, text inputs, dropdowns, list, tile and tree views, dialogs, tab views, ring menus … named as UMG names them, with style structs and style sheets |
| **Input** | Per-player pointers, focus and text targets; Tab and gamepad navigation; modals, popups, tooltips, drag and drop; an optional Slate input source |
| **Animation** | Tweens, render-transform tweens for widgets inside panels, widget animations Sequencer edits, and `.dui` timelines |

## How it differs from UMG

The difference is structural rather than cosmetic, so it is worth knowing before committing to either.

| | UMG / Slate | DreamGUI |
| :-- | :-- | :-- |
| Widget | `SWidget`, retained-mode Slate | `UObject` in a component-like tree |
| Sizing | Content-sized: a widget's size **is** its desired size | Box-first: you author a rect, content is arranged inside it |
| Text | The box grows to the text | The text is aligned in the box, and may overflow it |
| Placement | Slot-relative | Anchors + pivot, resolution-independent |
| In-world | `WidgetComponent`, a rendered quad | A first-class render mode |

The vocabulary deliberately does not differ: a control answers to the name its UMG counterpart uses, and
`Resources/UMGParity` records, for 58 UMG classes and some 960 members, which names were adopted, mapped or rejected
and why — held against UMG's own reflection by the automation suite.

## Documentation

The manual is published at **<https://gui.toolchain.64hz.cn>** in Chinese and English.

| | |
| :-- | :-- |
| **[Getting started](https://gui.toolchain.64hz.cn/en/docs/start/installation)** | install, a first screen, the project layout and the authoring loop |
| **[Concepts](https://gui.toolchain.64hz.cn/en/docs/concepts/widgets)** | widgets, canvases, world space, widget Blueprints, layout, text, rendering, input, animation |
| **[The .dui language](https://gui.toolchain.64hz.cn/en/docs/dui/overview)** | every construct of the language, page by page — also in the repository as [Docs/DuiLanguage.md](Docs/DuiLanguage.md) |
| **[Controls](https://gui.toolchain.64hz.cn/en/docs/controls)** | the control library, styles and style sheets, UMG parity |
| **[Class reference](https://gui.toolchain.64hz.cn/en/docs/reference)** | every public class, printed from reflection — also as [Docs/Reference](Docs/Reference/index.md) |
| **[Diagnostics](https://gui.toolchain.64hz.cn/en/docs/diagnostics)** | every `DUInnnn` code: what it means, how it happens, how to fix it |
| **[Guides](https://gui.toolchain.64hz.cn/en/docs/guides/fonts-and-packaging)** | fonts and packaging, moving from LGUI / LexUI, upgrading, platforms — [Docs/Migration.md](Docs/Migration.md), [Docs/FontsAndPackaging.md](Docs/FontsAndPackaging.md) |
| **[Changelog](CHANGELOG.md)** | what each version changed |

## Editor and tooling

| | |
| :-- | :-- |
| **Designer** | Picking by rect, hover feedback, per-axis resize handles, a rotate handle (Shift for 15° steps), an anchor medallion, marquee selection, drag to reparent, Content Browser drops, palette favourites and search, undo; the *Events* section creates a handler and its route in one click. On a class a `.dui` is the source of, a property edit is written back onto its line in the file, and a structural edit is refused with the reason — the hierarchy is the file's |
| **[VS Code extension](https://github.com/TypeDreamMoon/dreamui-language-support)** | Highlighting, completion, hover, navigation, diagnostics and formatting for `.dui`, with the compiler's own codes |
| **`DreamUI.Capture`** | a PNG of the viewport and of every render-target canvas (`UDreamUICaptureLibrary` from Blueprint) |
| **`DreamUI.Stats`** | what the frames since the last call cost, stage by stage, with batches, vertices and bytes; every stage is a `DreamUI_*` scope in Unreal Insights |
| **`DreamGUI.Memory`** | what the fonts, atlases and canvases hold |
| **Automation suite** | `Automation RunTests DreamGUI`, or by preset with `Tools/Tests/Invoke-DreamGUITests.ps1`; static checks for module layering and engine-private includes |
| **[`Tools/Bench`](Tools/Bench/README.md)** | the benchmarks the performance work was measured with: a screen of 5000 turning buttons and a level of 2688 world-space panels |

## Modules

```text
L4   DreamGUISamples
L3   DreamGUIControls      DreamGUIExtensions
L2   DreamGUIInput
L1   DreamGUI (core)
L0   DreamGUIRenderer      DreamTween
     ------------------------------------------------------------
     DreamGUIEditor, DreamGUIK2Nodes (uncooked only), DreamGUITests (editor only)
```

A module depends only on modules in the layers below its own, and a static check fails an include that goes the
other way. C++ that uses a type from a module other than the core adds that module to its `Build.cs`.

The plugin ships no CoreRedirects. Assets saved against LGUI, LexUI or a build before 1.0.0 are resaved once with
2.1.0's redirect block borrowed into the project's config —
[Docs/Migration.md](Docs/Migration.md#from-21-to-100) has the three steps.

## Platforms

Unreal Engine **5.8**, source and launcher installs alike. What is *claimed* and what has been *run* are different
lists, so both are here:

| | Compiled | Run |
| :-- | :-- | :-- |
| **Win64** | the editor; the game target in Development and Shipping | the editor with the whole automation suite; a packaged Development game, once the release gate has run the packaged text smoke test |
| Mac, Linux | never | never — there is no machine or toolchain for them here |
| iOS, Android | never | never — no SDK here, and never on a device |
| Consoles | no | not in any module's `PlatformAllowList` |
| Dedicated server | never — no server target has been built | no |

The descriptor's `SupportedTargetPlatforms` names the five platforms the code is written for; that says what the
build tools will try, not that anyone has run it there. The release gate, the mobile code paths that have never run
and what was checked for a dedicated server are on
[Platforms](https://gui.toolchain.64hz.cn/en/docs/guides/platforms).

## Project info

| | |
| :-- | :-- |
| Version | `1.0.0` |
| Unreal Engine | `5.8` |
| Modules | `DreamGUIRenderer`, `DreamTween`, `DreamGUI`, `DreamGUIInput`, `DreamGUIControls`, `DreamGUIExtensions`, `DreamGUISamples` (Runtime), `DreamGUIEditor`, `DreamGUIK2Nodes` (uncooked), `DreamGUITests` (Editor) |
| Author | TypeDreamMoon |
| GitHub | <https://github.com/TypeDreamMoon> |
| Docs | <https://gui.toolchain.64hz.cn/> |
| Upstream | [LGUI / LexUI](https://github.com/liufei2008/LGUI) by Lex Liu |
| License | [MIT](LICENSE) |

## License

MIT — see [LICENSE](./LICENSE).

Copyright (c) 2026-present TypeDreamMoon
Copyright (c) 2019-present Lex Liu

Substantial portions of this software remain the work of Lex Liu and are used under the MIT terms of the
[original project](https://github.com/liufei2008/LGUI). The MIT notice must travel with any copy or substantial
portion of this code, including yours.

For bug reports and feature requests, open an [issue](https://github.com/TypeDreamMoon/DreamGUI/issues/new).

## Star History

<a href="https://www.star-history.com/?repos=typedreammoon%2Fdreamgui&type=date&legend=bottom-right">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=typedreammoon/dreamgui&type=date&theme=dark&legend=bottom-right" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=typedreammoon/dreamgui&type=date&legend=bottom-right" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=typedreammoon/dreamgui&type=date&legend=bottom-right" />
 </picture>
</a>
