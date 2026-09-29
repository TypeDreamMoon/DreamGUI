# Moving a project to DreamGUI 2.0

For three kinds of project, in the order they usually come:

- one with **assets saved against LGUI or LexUI** — the upstream this fork started from;
- one on an **earlier build of this fork** (1.x) — the single-module plugin, before the module split, the
  input rework and the renderer rework;
- one with **C++ of its own against the plugin** — subclasses, custom visuals, a viewport client of its own.

Everything below is about keeping what you have. What the plugin is and how to start from nothing is in the
[README](../README.md).

## 1. Before you open the project

1. **Engine 5.8.** The plugin builds against 5.8 only; there is no 5.7 branch of it.
2. **Put the project under source control first,** or copy it. Opening it with the new plugin is harmless —
   redirects rewrite names in memory, not on disk — but the first save of an asset writes the new names into
   it, and there is no way back from that save to the old plugin.
3. **Take the old plugin out.** An LGUI or LexUI folder under `Plugins/` has to go before DreamGUI goes in: its
   classes would be live under the names DreamGUI redirects *from*, and a redirect never applies to a name that
   still resolves.
4. **Delete any copy of the redirect block from your `Config/DefaultEngine.ini`.** Earlier versions shipped the
   redirects as a template to copy there. The plugin carries them in its own config now (see 3), and two
   redirects for one old name with different new names are an error: the first one registered wins, and the
   copy is the older of the two.

## 2. Install

Clone into the project's `Plugins/` directory, regenerate project files, build:

```bash
git clone https://github.com/TypeDreamMoon/DreamGUI.git Plugins/DreamGUI
```

The plugin needs `EnhancedInput`, which it enables itself. Nothing else is required of the project: no engine
source build, no private engine headers, no settings to copy.

## 3. What loads by itself

`Config/DefaultDreamGUI.ini` is mounted as the plugin's own config branch, and the engine applies its
`[CoreRedirects]` before the first package loads. It holds 763 redirects:

| Kind | Entries | What they cover |
| --- | --- | --- |
| Package | 4 | The `/LGUI/` content mount to `/DreamGUI/`, and the `/Script/LGUI`, `/Script/LGUIEditor` and `/Script/LTween` modules to their DreamGUI names |
| Class | 395 | Every LGUI/LexUI class; the prefab vocabulary the class model replaced; the control renames (`UIButtonComponent` → `UIButton` and its thirteen siblings); every class the module split moved out of the core, from its old `/Script/DreamGUI` name to its module |
| Struct, enum | 96, 124 | The same, for data |
| Function | 74 | Functions a Blueprint calls or overrides that were renamed or moved — `UDreamUIBehaviour`'s `Update` → `Tick`, `UDreamUINavigationScope`'s events that lost their `On` |
| Object | 70 | The delegate signatures the module split moved with their classes |

**Then resave.** A redirect is applied every time an asset that needs it loads. Once the project opens and the
UI looks right, resave the assets that use DreamGUI (*File > Save All* after opening them, or the
`ResavePackages` commandlet over your content), so they name the current types themselves. The redirects stay in
the plugin either way.

## 4. What no longer exists

These have no redirect, because there is nothing left for one to point at. An asset that still names one of them
loses that part on load, and the engine logs a warning naming it.

- **The root Blueprints** `WorldSpaceRoot_DreamRenderer`, `WorldSpaceRoot_UERenderer`, `ScreenSpaceRoot` and
  `DreamWorldSpaceRaycasterSource_Mouse`. A level that places one drops that actor: drag the widget Blueprint
  into the level again, as a `UDreamWorldWidgetComponent` on an actor of your own or through the level
  editor's drop.
- **`UDreamWidgetPresenterComponent`.** The abstract base `UDreamWidgetPresenterComponentBase` stays;
  `UDreamWorldWidgetComponent` is what you place.
- **The `UDreamWorldSpaceRaycasterBase` / `ForWorldTrigger` / `Source` family** — `UDreamWorldSpaceRaycaster`
  absorbed all of it.
- **The plugin settings** `ScreenSpaceRootClass`, `WorldSpaceRootClass`, `WorldSpaceUERendererRootClass`,
  `WorldSpaceRaycasterSourceClass`, and `bLegacyTouchPointerIds` (see 5).
- **The Lex layout family** — `ULexLayoutContainerFlexBox`, `ULexLayoutContainerGrid`, `ULexLayoutSelfFlexBox`,
  `ULexLayoutSelfGrid` and the `ELexUILayoutMode` switch. The UMG-shaped panels replace them; a tree laid out
  by one of these is laid out again by hand.
- **The prefab machinery** and its types, which the widget class model replaced before any of the above.
- **Two console variables that put earlier renderer behaviour back** — `r.DreamUI.MaterialWrappers` and
  `r.DreamUI.RTDrawer` — together with the behaviour they put back (see 5).

## 5. Behaviour to check

What an asset does can differ from what it did, with nothing renamed. Worth a look in any project that has run
an earlier version:

### Input

- **Pointer ids have ranges.** The mouse is pointer 0, a finger is 100 plus its index, and ids a script makes up
  start at 1000. A finger used to be its own index, so the first finger and the mouse were the same pointer and
  fought over hover and selection. Code that reads touch pointers by finger index asks
  `DreamUIPointerIds::ForTouch` instead; the setting that restored the old ids is gone.
- **The Enhanced Input preset binds the actions it is given, as they are.** It used to play with runtime copies of
  them, whose `bTriggerWhenPaused` it rewrote every frame. Now a paused game is decided per event: while the game
  is paused and `UDreamUISettings::bScreenSpaceUIAffectByGamePause` is set, the preset drops what arrives. For a
  paused frame's click to arrive at all, its action must trigger while paused — the shipped `IA_*` actions do; set
  `bTriggerWhenPaused` on actions of your own that the preset binds.
- **Focus and text input belong to a player.** A split screen's second player focuses and types on its own.
  `UUITextInput::GetActiveTextInputForPlayer` answers per player; `GetActiveTextInput` answers across worlds.
- **Characters reach a text field through the game viewport client.** Use `UDreamGameViewportClient`, or call
  `DreamUITextInputRouter::RouteViewportCharacter` from your own viewport client's `InputChar`, after the console
  and before the base class (the README's [keyboard section](../README.md#keyboard-layouts-give-dreamgui-the-game-viewport-client)).
- **The Slate input source is optional and off.** *Project Settings > Plugins > Dream GUI > Input >
  Use Slate Input Source* hears every pointer, key and stick before the viewport, so the UI keeps working in the
  engine's own UI-only input mode; the preset actors stand down while it is on, and
  `SlateInputConsumePolicy` decides what it keeps from the game.

### Rendering

- **A canvas makes no material instance of its own.** It answers the parameters it gives a material — the main
  and font textures, its data textures — through a render-thread proxy of that material. A material instance you
  give a widget is answered for the same way and never written to, so parameters you set on it stay yours.
  Nothing a canvas draws with is an object in the level, and nothing of it is copied into a play session or a
  paste.
- **A render-target canvas draws whether or not anything renders its world.** It is drawn by a render command
  and a graph of its own after its sections change, not inside one of its world's views.
- **Background blur, pixelate and pixel sort** look as they did, with two fixes: a full-size blur on a
  multisampled canvas shows (it was lost to the canvas's resolve), and a full-size blur into an output target
  blurs the screen (it blurred an empty texture).

### Seeing it

`DreamUI.Capture` writes a PNG of the viewport and of every render-target canvas, and `DreamUI.Stats` prints
what the UI cost stage by stage — enough to compare a migrated screen with how it looked before. If something is
drawn wrong or not drawn, run with `r.DreamUI.VerifyPartialPrepare 1` and include the ensure it raises in the
report.

## 6. C++ of your own

**Modules.** The runtime is several modules now. Add to your `Build.cs` the ones whose types you use:
`DreamGUIRenderer` (drawing, effects' render proxies), `DreamGUI` (widgets, visuals, canvases), `DreamGUIInput`
(event systems, raycasters, navigation, the viewport client), `DreamGUIControls` (the `Dream*` controls and the
`UI*` behaviours), `DreamGUIExtensions` (lines, rings, effects, render-target helpers). The README's
[module table](../README.md#modules) says what is where.

**Includes.** Every header kept its path except those in the README's
[include table](../README.md#c-written-against-the-single-module).

**Calls.** The README's call table lists what moved with the module split. Since then:

| Was | Is now |
| --- | --- |
| `UDreamVisual::OnMaterialInstanceDynamicCreated(UMaterialInstanceDynamic*)` | `UDreamVisual::AddMaterialParameters(FDreamUIMaterialParameters&)`: give the parameters your visual's material needs, and the canvas answers them for its proxy |
| `DreamUITextInputRouter::RouteCharacter(TCHAR)` | `RouteViewportCharacter` from a viewport client, or `RouteCharacter(WorldContext, UserIndex, Character)` |
| `UDreamGUISettings::bLegacyTouchPointerIds` | Gone; `DreamUIPointerIds::ForTouch` gives a finger's pointer id |
| `ADreamEnhancedInputEventSystemActor::GetOriginalAction` | Gone: the actions the actor holds are the ones it was given |
| `FDreamUIRenderer::IsRenderTargetDrawerEnabled` | Gone: a render-target canvas is always drawn by `DrawRenderTarget_GameThread` |
| `UDreamUIMeshComponent::OnSceneProxyCreated` | `OnRenderRootCreated`, broadcast with the mesh's render root. The root holds the canvas's sections and outlives the scene proxies made for it |
| `FDreamVisualPostProcessRenderProxy::RenderMeshOnScreen_RenderThread` taking RHI textures | Takes the graph's textures (`FRDGTextureRef`). An effect of your own reads the screen with `ReadScreen_RenderThread` and `GrabRegion_RenderThread`, works in textures from `CreateWorkTexture`, and writes back with `WriteBack_RenderThread` |

**Settings in code.** `UDreamGUISettings` is *Project Settings > Plugins > Dream GUI*; `UDreamUISettings` is
*Project Settings > Plugins > DreamUI*. Both are read where they are used, so a value changed at runtime applies
from the next frame.

## 7. When something does not come across

The automation suite that ships with the plugin checks the redirects themselves: that every one reaches the
engine, that no old name is still a live type, that none hops into another redirect, and that every new name in
the plugin's modules exists (`DreamGUI.Packaging.*`), and it loads assets saved by older versions of the plugin
(`DreamGUI.Compatibility.*`, `DreamGUI.Assets.*`). An asset of yours that fails to load is worth a report with
the warning the engine logged for it: a name DreamGUI renamed and did not redirect is a bug.
