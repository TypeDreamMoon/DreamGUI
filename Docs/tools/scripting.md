---
title: Scripting and code entry points
description: The Blueprint and C++ entry points for creating, showing and placing UI in the world (UDreamUIBPLibrary), the K2 nodes module, and editor Python that creates a Dream Widget Blueprint, points it at a .dui and compiles it.
---

## Blueprint and C++ entry points

The run-time entry points are gathered in [`UDreamUIBPLibrary`](../Reference/DreamUIBPLibrary.md) (module `DreamGUI`,
`DreamUIBPLibrary.h`), named after UMG's on purpose:

| To do this | UMG | DreamGUI |
| --- | --- | --- |
| Instantiate a widget class without putting it anywhere | the Create Widget node / `CreateWidget` | the Create Dream Widget node / `CreateDreamWidgetOfClass` |
| Create it and put it on the viewport | `CreateWidget` + `AddToViewport` | Create Dream Widget + `AddToViewport`, or `AddWidgetOfClassToViewport` |
| Take it off the viewport | `RemoveFromParent` | `RemoveFromViewport` |
| Construct a bare widget | `WidgetTree::ConstructWidget<T>()` | `ConstructWidget` |
| Lay out now (a size needed this frame) | `ForceLayoutPrepass` | `ForceLayoutPrepass` |
| Mark for the next layout pass | `InvalidateLayoutAndVolatility` | `InvalidateLayout` |
| Put it in the world | `WidgetComponent` | `AttachWidgetToSceneComponent`, or place an `ADreamWorldWidgetActor` |

Details worth knowing:

- **The Create Dream Widget node.** UMG's Create Widget node: picking a class gives it a pin for each Expose on Spawn
  property and an Owning Player pin. The pins are assigned before the widget initializes, so Pre Construct, On
  Initialized and every binding's first value see them. It compiles to `BeginDeferredCreateDreamWidget`, the
  assignments, then `FinishDeferredCreateDreamWidget`; C++ has the same two halves as `BeginCreateDreamWidget` /
  `FinishCreateDreamWidget` (`Core/DreamUserWidget.h`). `CreateDreamWidgetOfClass` is therefore BlueprintInternalUseOnly,
  as UMG marks `Create`: it is not in the Blueprint menu, graphs that already call it keep compiling, and **it is gone from
  Python** — an editor script that needs an instance uses `add_widget_of_class_to_viewport`, or a function of its own
  exposed from C++.
- **The return pin follows the class.** The return of Create Dream Widget, `AddWidgetOfClassToViewport` and
  `CreateDreamWidgetOfClass` takes the shape of the class, so a Blueprint subclass's own variables and functions are
  reachable without a Cast, as with UMG's Create Widget node.
- **The screen root is made on demand.** The first time something goes on the viewport, the shared screen-space root
  (`GetOrCreateScreenSpaceUIRoot`), the raycaster and the event system are created; nothing needs configuring.
  `AddWidgetOfClassToScreen` is the variant with a before-alive callback and a sort order; `IsInViewport` asks.
- **The parked state.** A widget made by `ConstructWidget`, `CreateDreamWidgetOfClass` or Create Dream Widget exists but is not on screen: it
  draws nothing and its behaviours do not run until you hand it to `AddChild` or put it on the viewport. Meanwhile the UI
  manager holds it, so it is not collected out from under you while you configure it. `IsWidgetParked` asks, and the
  console command `dreamgui.ListPendingWidgets` lists every parked widget.
- **Into the world.** `AttachWidgetToSceneComponent(Root, Component)` is the world-space counterpart of AddToViewport:
  the root must carry a DreamCanvas (the canvas decides the render mode and owns the meshes) and have no parent; it is
  attached to the scene component, which gives it its world transform. Otherwise it returns false and changes nothing.
  More often you place an `ADreamWorldWidgetActor`, or add a `UDreamWorldWidgetComponent` to your own actor and
  `SetWidgetClass` — see [World space](../concepts/world-space.md).

### C++: a native base for a .dui screen

A `.dui`'s node ids become members of the **generated** class, which a C++ base cannot name at compile time, so it looks
them up by variable name. The handlers routes name (`OnClicked -> HandleApply`) are `UFUNCTION()`s:

```cpp
// MySettingsScreen.h -- add "DreamGUI" and "DreamGUIControls" to Build.cs
#pragma once

#include "Core/DreamTextUserWidget.h"
#include "MySettingsScreen.generated.h"

UCLASS(Abstract)
class UMySettingsScreen : public UDreamTextUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;

	/** `OnClicked -> HandleApply` in the .dui lands here. */
	UFUNCTION()
	void HandleApply();
};
```

```cpp
// MySettingsScreen.cpp
#include "MySettingsScreen.h"
#include "Core/DreamWidgetTree.h"
#include "Controls/DreamDropdown.h"

void UMySettingsScreen::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// The .dui declares the node; the compiler made it a member of the generated class.
	if (UDreamDropdown* Quality = Cast<UDreamDropdown>(GetWidgetTree()->FindWidgetByVariableName(TEXT("Quality"))))
	{
		Quality->SetOptions({ FText::FromString(TEXT("Low")), FText::FromString(TEXT("High")) });
	}
}

void UMySettingsScreen::HandleApply()
{
}
```

Showing it:

```cpp
#include "DreamUIBPLibrary.h"

// SettingsClass: a TSubclassOf<UDreamUserWidget> naming the Blueprint made from the .dui
UDreamWidget* Page = UDreamUIBPLibrary::AddWidgetOfClassToViewport(this, SettingsClass);
// ...
UDreamUIBPLibrary::RemoveFromViewport(this, Page);
```

### Animations and input modes

`UDreamUserWidget::PlayAnimation` is the entry the compiler's generated animation variables feed: drag the variable in and
drop it on Animation. Beside it are `PlayAnimationByName`, `PlayAnimationForward` / `PlayAnimationReverse` (the "slides out on
click, slides back on the next click" idiom), `PlayAnimationTimeRange`, and `QueuePlayAnimation`, which is safe from inside an
animation's own callbacks. They return an `FDreamUIAnimationHandle`; [`UDreamUIAnimationLibrary`](../Reference/DreamUIAnimationLibrary.md)
has the Blueprint functions for handles.

Input modes are [`UDreamUIInputModeLibrary`](../Reference/DreamUIInputModeLibrary.md): `SetInputModeUIOnly`,
`SetInputModeGameAndUI` and `SetInputModeGameOnly`, the same three states as UMG's `SetInputMode_*`, with the focus given to
a DreamGUI widget.

### The K2 nodes module

`DreamGUIK2Nodes` (loaded only by processes that run uncooked content) adds three Blueprint nodes:

| Node | What it does |
| --- | --- |
| Play Animation with Finished event | The async form of `PlayAnimation`, with a Finished exec pin |
| Play Animation Time Range with Finished event | The async form of `PlayAnimationTimeRange` |
| Get Component for DreamGUIComponentReference | The component an `FDreamUIComponentReference` names, its output cast to the referenced component type |

## Editor Python: a Widget Blueprint from a .dui

Making assets in bulk — a family of components, a set of screens — need not be one *Set Source File...* click at a time.
What that button does is **set the class default `SourceFile` and compile**, and Python can do it too. Four steps:

### Create it with DreamGUI's factory
`unreal.DreamWidgetBlueprintFactory` makes a `UDreamWidgetBlueprint`, compiled by DreamGUI's own compiler, which an engine
Blueprint factory would not. With its picker skipped, the parent class is the plain `UDreamUserWidget`.

### Reparent to DreamTextUserWidget
A class can name a `.dui` exactly when it derives from `UDreamTextUserWidget` (or your own subclass of it). Reparent with
`BlueprintEditorLibrary.reparent_blueprint`.

### Set SourceFile
Set `SourceFile` on the generated class's default object. A relative path is rooted at a `DUI/` source directory.

### Compile and save
`compile_blueprint` reads the file and builds the tree; then save the package.

```python
# make_settings_assets.py -- run inside the editor; see below for how.
import unreal

PACKAGE_PATH = "/Game/UI"
NAME = "WBP_Settings"
SOURCE = "UI/Settings.dui"   # relative to the project's DUI/ directory


def make_widget():
    unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    blueprint = unreal.load_asset(PACKAGE_PATH + "/" + NAME)
    if blueprint is None:
        # DreamGUI's factory makes a UDreamWidgetBlueprint; its parent is the plain user widget until reparented below.
        blueprint = asset_tools.create_asset(NAME, PACKAGE_PATH, unreal.DreamWidgetBlueprint,
                                             unreal.DreamWidgetBlueprintFactory())
    if blueprint is None:
        raise RuntimeError("cannot create " + PACKAGE_PATH + "/" + NAME)

    if not hasattr(unreal, "BlueprintEditorLibrary"):
        # Python wraps the modules loaded so far; in a commandlet nothing may have loaded this one yet.
        unreal.load_module("BlueprintEditorLibrary")
    library = unreal.BlueprintEditorLibrary

    defaults = unreal.get_default_object(library.generated_class(blueprint))
    if not isinstance(defaults, unreal.DreamTextUserWidget):
        # A class is text-authored when it derives from UDreamTextUserWidget.
        library.reparent_blueprint(blueprint, unreal.DreamTextUserWidget.static_class())
        defaults = unreal.get_default_object(library.generated_class(blueprint))

    # What the designer's "Set Source File..." sets: the class default the compile reads the hierarchy from.
    defaults.set_editor_property("SourceFile", unreal.FilePath(file_path=SOURCE))
    if not library.compile_blueprint(blueprint):
        raise RuntimeError(NAME + " did not compile from " + SOURCE + "; see the log")

    if not unreal.EditorLoadingAndSavingUtils.save_packages([blueprint.get_outermost()], False):
        raise RuntimeError("saving " + NAME + " failed")
    unreal.log("made " + blueprint.get_path_name() + " <- " + SOURCE)


make_widget()
```

The script is safe to run again: an existing asset is kept, pointed at its source again and recompiled, so a changed `.dui`
is picked up. It uses only what the core editor exposes to Python (the asset registry, AssetTools,
`EditorLoadingAndSavingUtils`, `BlueprintEditorLibrary`), not the Editor Scripting Utilities plugin, which is off by default.
Properties are named as C++ names them (`"SourceFile"`), which Python resolves too.

To use a native base of your own (the `UMySettingsScreen` above, say), reparent to
`unreal.load_class(None, "/Script/MyGame.MySettingsScreen")` instead, and test for a subclass of it.

**Order matters when making several**: compiling a screen loads the classes of the components its `.dui` places with
`use … as`, so compile the components first and the screens that use them after.

### Running it

Headless, as a commandlet:

```powershell
UnrealEditor-Cmd.exe <project>.uproject -EnablePlugins=PythonScriptPlugin -run=pythonscript -script=<path>\make_settings_assets.py -unattended -nullrhi
```

or inside the editor, at startup:

```powershell
UnrealEditor.exe <project>.uproject -ExecutePythonScript=<path>\make_settings_assets.py
```

A script that also **imports files** (fonts or textures through an `AssetImportTask`) has to run in the editor: importing
needs Slate, which a `-run=pythonscript` commandlet does not have. A script that only makes Blueprints, sets properties,
compiles and saves runs fine as a commandlet.

Once a class names its source file, saving that `.dui` in the editor rebuilds the class, and the editor's *Rebuild DUI*
recompiles every text-backed class at once.
