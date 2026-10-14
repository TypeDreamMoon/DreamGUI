---
title: Overview
description: DreamGUI is a 3D UI plugin for Unreal Engine 5.8 — widgets are UObjects with a rect, a pivot and anchors in a tree, batched by a canvas, on the screen or standing in the level.
---

DreamGUI is a UI system for Unreal Engine 5.8. A widget here is a `UObject` with a rect, a pivot and anchors,
arranged into a tree; a canvas (`UDreamCanvas`) batches the whole tree into as few draw calls as it can. That is
Unity's uGUI shape rather than Slate's.

The shape buys three things UMG cannot do as directly:

- **UI in the world.** A canvas can render in screen space or sit on a surface in the level, at any angle, lit or
  unlit, with correct hit testing either way.
- **A tree that is a class.** A UI tree is authored in a widget Blueprint (`UDreamWidgetBlueprint`) and reused by
  subclassing and by nesting one widget class inside another tree — the way UMG's widget Blueprints work. The tree can
  also be written as text: a `.dui` file is the source of a widget Blueprint, and the designer writes its edits back
  into it.
- **Per-widget perspective.** A widget can establish a perspective its subtree is foreshortened into, the way CSS
  `perspective` works.

It costs you Slate's ecosystem: none of UMG's widgets, styles or bindings apply. DreamGUI has a control library of its
own whose names deliberately follow UMG's, and where a name could not be kept the reason is written down (see
[How it differs from UMG and from upstream](getting-started/about.md)).

| | |
| --- | --- |
| Plugin version | `1.0.0`, the first public release (the development builds before it were numbered 1.x, 2.0 and 2.1; see the [changelog](changelog/index.md)) |
| Engine | Unreal Engine `5.8`, a source build or an Epic launcher install |
| Platform | Win64 is the only platform built and run (see [Platforms](guides/platforms.md)) |
| Runtime modules | `DreamGUIRenderer`, `DreamGUI`, `DreamGUIInput`, `DreamGUIControls`, `DreamGUIExtensions`, `DreamGUISamples`, `DreamTween` |
| Editor-side modules | `DreamGUIEditor`, `DreamGUIK2Nodes` (`UncookedOnly`), `DreamGUITests` (`Editor`) |
| Plugin dependency | `EnhancedInput` (the plugin enables it itself) |
| Source files | `.dui`, under a project's or a plugin's `DUI/` directory |
| Project settings | *Project Settings ▸ Plugins ▸ Dream GUI* and *Project Settings ▸ Plugins ▸ DreamUI* |
| License | MIT |
| Origin | A fork of [LGUI / LexUI](https://github.com/liufei2008/LGUI) by Lex Liu — not a drop-in replacement for it |

## At a glance

```dui
class /Game/UI/WBP_Hello

Widget Root {
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)
    + Overlay {}

    Text Greeting {
        AnchorData.SizeDelta = (600, 60)
        Text     = "Hello, DreamGUI"
        FontSize = 32
        HAlign   = Center
        @slot HorizontalAlignment = Center
        @slot VerticalAlignment   = Center
    }
}
```

`Root` is anchored to all four corners with no inset, so it fills whatever it is added to; `+ Overlay` makes it stack
its children; `Greeting` is a 600 × 60 text, and its two `@slot` lines are what it asks of the Overlay above it:
centred both ways. Point a widget Blueprint's *Source File* at this file and compile, and it is a class you can add to
the viewport or drag into a level — step by step in [Your first screen](getting-started/first-screen.md).

## Where to start

  - [Getting started](getting-started/installation.md) — Install, your first screen, where things live in a project, the everyday authoring loop.
  - [Concepts](concepts/widgets.md) — Widgets, canvases, world space, widget Blueprints, layout, text, rendering, input, animation.
  - [The .dui language](language/overview.md) — Nodes and values, slots and components, styles, use, components, view models, bindings, if and for, rows, timelines, write-back.
  - [Controls](controls/index.md) — Buttons, toggles, sliders, lists, dialogs, tab views... plus style sheets and the member-by-member UMG comparison.
  - [Tools](tools/designer.md) — The designer, the VS Code extension, debugging commands, the test suite, benchmarks, scripting.
  - [Migration and upgrades](guides/migration.md) — Coming from LGUI, LexUI or an early build, and upgrading between development builds.
  - [Packaging and platforms](guides/fonts-and-packaging.md) — Shipping fonts, colour emoji and ICU data with a game; which platforms have actually run.
  - [Diagnostics](diagnostics/index.md) — Every .dui diagnostic is a DUInnnn code, in seven families by compile stage.
  - [API reference](Reference/index.md) — Per-class property and function reference printed from reflection, each page ending with its UMG comparison.
  - [Changelog](changelog/index.md) — What changed in each version.
