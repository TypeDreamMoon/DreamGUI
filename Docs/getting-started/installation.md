---
title: Installation
description: Requirements, cloning the plugin into Plugins/, the two settings worth deciding right after (the game viewport client for keyboard layouts, the Slate input source), and where settings live.
---

DreamGUI installs like any project plugin: clone it into the project's `Plugins/`, regenerate project files, build.
For a fresh project that is the whole install. The two sections after it are decisions worth making straight away —
neither is required, but skip one and non-US keyboards type the wrong characters; skip the other and the UI hears
nothing in the engine's own UI-only input mode.

| | |
| --- | --- |
| Engine | Unreal Engine `5.8` (`EngineVersion` 5.8.0), a source build or an Epic launcher install |
| Platform | Win64 is the only platform built and run (see [Platforms](../guides/platforms.md)) |
| Plugin dependency | `EnhancedInput`, enabled by the `.uplugin` itself |
| Runtime modules | `DreamGUIRenderer`, `DreamGUI`, `DreamGUIInput`, `DreamGUIControls`, `DreamGUIExtensions`, `DreamGUISamples` (`PostConfigInit`); `DreamTween` (`Default`) |
| Loaded only on uncooked content | `DreamGUIEditor`, `DreamGUIK2Nodes` (`UncookedOnly`) |
| Tests | `DreamGUITests` (`Editor`, `PostEngineInit`) |

## Requirements

### Engine 5.8, a launcher install included

The plugin compiles against the engine's public headers only: the renderer reads the scene's depth through the
public scene-texture API, and the static check `engine-private-path` fails any include path into
`Runtime/Renderer/Private` or `Internal`. So no source build is needed — and there is no 5.7 branch.

### The bundled msdfgen

The glyph rasteriser compiles [msdfgen](https://github.com/Chlumsky/msdfgen) (Viktor Chlumsky, MIT) into a
translation unit of its own, to turn glyph outlines into multi-channel distance fields. That used to need a source
build: upstream ships no single-file distribution but generates a `msdfgen.h` / `msdfgen.cpp` pair with
`all-in-one/generate.py`, and a launcher install has only `msdfgen.tps` under `Engine/Source/ThirdParty/msdfgen/`.

The plugin now carries its own generated pair, committed, under `ThirdParty/msdfgen-single-file/`. A plain
`git clone` — and a GitHub zip download, which carries no submodule contents — builds as it is, with no Python at
build time. The `ThirdParty/msdfgen` submodule is only what `Tools/UpdateMsdfgen.ps1` regenerates that pair from;
it is never compiled or included. `Tools/UpdateMsdfgen.ps1 -Check` exits 1 when the committed pair has drifted from
the submodule.

**Note:**

  The copy is wrapped in a `DreamMsdfgen` namespace (`MSDFGEN_PARENT_NAMESPACE`). SlateCore compiles its own msdfgen,
  and in a monolithic build the two sets of symbols would collide — two copies that only look similar are worse than a
  link error, so the namespace is not optional.

## Install

```bash
git clone https://github.com/TypeDreamMoon/DreamGUI.git Plugins/DreamGUI
```

Regenerate project files and build. No engine source build, no private engine headers, no settings to copy.

1.0.0 ships no CoreRedirects. If your project has assets saved against LGUI or LexUI, or by a build before 1.0.0
under names that have changed since, resave them once with 2.1.0's redirect block borrowed: the steps are in
[Upgrading](../guides/upgrading.md#from-21-to-100), and a project coming from LGUI or LexUI starts at
[Migration](../guides/migration.md).

### Checking it is there

Once the editor is up:

- the Content Browser's **Add** menu has **DreamGUI Widget** in its top group, and a **DreamGUI** submenu below, in
  sections: Basic, Fonts, Graphics, Rich Text;
- *Project Settings ▸ Plugins* has a **Dream GUI** and a **DreamUI** page (and a **DreamGUI Editor** one);
- the level editor's *Tools* menu has **Open DreamUI Workspace (VSCode)** and **Rebuild DUI**.

## Keyboard layouts: give DreamGUI the game viewport client

A DreamGUI text field is not a Slate widget, so it is never on the keyboard focus path, and the engine's only landing
place for a platform **character** in a game is the virtual `UGameViewportClient::InputChar` — which has no delegate to
subscribe to. With nobody owning that function, `UUITextInput` falls back to its own `FKey` → character table, and that
table is only correct on **US QWERTY**: AZERTY, QWERTZ, Dvorak, Cyrillic, dead keys and AltGr all type the wrong
character. (IME users are unaffected — composition text arrives through TSF, not through the table.)

Pick whichever of these fits the project. The field logs one warning the first time it is edited if neither is in
place.

### 1. Use the plugin's viewport client

In the project's `Config/DefaultEngine.ini`:

```ini
[/Script/Engine.Engine]
GameViewportClientClassName=/Script/DreamGUIInput.DreamGameViewportClient
```

### 2. Keep your own viewport client

Either derive it from `UDreamGameViewportClient` instead of `UGameViewportClient`, or keep its base and hand the
character to DreamGUI from its `InputChar` override (module `DreamGUIInput`, header
`Interaction/DreamUITextInputTarget.h`) — **after the console, and before the base class**:

```cpp
#include "Interaction/DreamUITextInputTarget.h"

bool UMyGameViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character)
{
    FString CharacterString;
    CharacterString += Character;
    // An open console takes every character.
    if (ViewportConsole && ViewportConsole->InputChar(FInputDeviceId::CreateFromInternalId(ControllerId), CharacterString))
    {
        return true;
    }
    // Before the base class: in a play-in-editor viewport it answers true for every character, so a
    // field asked after it never sees one there.
    if (!IgnoreInput() && DreamUITextInputRouter::RouteViewportCharacter(this, ControllerId, Character))
    {
        return true;
    }
    return Super::InputChar(InViewport, ControllerId, Character);
}
```

Why that order: in a play-in-editor viewport the base class absorbs every character (it returns true) so that game
input does not reach the editor's own frame. Ask the base first and return on its answer, and the field is never
reached — PIE falls back to the table, while a packaged game, where the base answers false, gets real characters.

`DreamUITextInputRouter::RouteViewportCharacter` is the entire contract. It hands the character to the field the
typing player (the one `ControllerId` is) is editing, or, when no field takes it, to what that player has focused as a
key character, and returns whether either took it. From the first character that arrives this way, the `FKey` table
stops synthesising printable characters in that world, so the two roads never double-type. The module your code
depends on is `DreamGUIInput`.

**Note:**

  `UDreamGameViewportClient` does two more things: in DreamGUI's own UI-only input mode
  (`UDreamUIInputModeLibrary::SetInputModeUIOnly`) it routes the input the engine would ignore on to DreamGUI, and
  while DreamGUI has the focus it keeps Slate's own navigation (Tab, the arrows) from carrying the keyboard focus off
  the viewport into UMG. With a viewport client of your own, the world's input subsystem guards the latter through the
  base class's `OnNavigationOverride`.

## Input in every input mode: the Slate input source

By default DreamGUI hears input through its preset event system actor's bindings on the player controller, so it hears
nothing in the engine's own UI-only input mode: `SetInputMode(FInputModeUIOnly())` makes the game viewport ignore input,
and the controller never sees it.

Turn on *Project Settings ▸ Plugins ▸ Dream GUI ▸ Input ▸ Use Slate Input Source* (`bUseSlateInputSource`) and DreamGUI
hears the mouse, touch, keys and sticks from Slate itself instead — an input pre-processor, ahead of the game viewport —
in every input mode. The preset actors stand down while it is on, so nothing arrives twice.

**Slate Input Consume Policy** (`SlateInputConsumePolicy`) decides what the UI keeps from the game:

| Value | What the UI keeps |
| --- | --- |
| `Never` (default) | Nothing; the game hears everything, as it always has |
| `WhenOverUI` | A press, release or wheel turn over DreamGUI UI, and any key the UI took |
| `WhenHandled` | Only a press on a widget that handles presses, and a key the UI took |

A key typed into a field being edited is always kept. The same group has `SlateInputStickScrollSpeed` (1500 by
default): how fast the right stick scrolls what has focus, in canvas units a second at full tilt.

The source is off by default for now, and becomes the default in a later version.

## Where settings live

| Page | Class | Written to |
| --- | --- | --- |
| *Project Settings ▸ Plugins ▸ Dream GUI* | `UDreamGUISettings` | the project's `Config/DefaultDreamGUI.ini`, `[/Script/DreamGUI.DreamGUISettings]` |
| *Project Settings ▸ Plugins ▸ DreamUI* | `UDreamUISettings` | the project's `Config/DefaultEngine.ini` (`config=Engine`) |
| *Project Settings ▸ Plugins ▸ DreamGUI Editor* | `UDreamUIEditorSettings` | the project's `Config/DefaultEditor.ini` (`config=Editor`) |

Both runtime settings classes are read where they are used, so a value changed at runtime applies from the next frame.

**Note:**

  The plugin's `Config/Game.ini` puts `/DreamGUI` into `DirectoriesToAlwaysCook`, so a cook driven by an explicit
  package list (`-map=`, chunks) still gets the plugin's default assets.

## Next

  - [Your first screen](first-screen.md) — From a .dui file to the screen, and the same class in a level.
  - [Project layout](project-layout.md) — The module layers, where .dui files go, what is in the plugin folder.
