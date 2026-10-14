---
title: "脚本与代码入口"
description: "用于创建、显示和将 UI 放入世界的 Blueprint 与 C++ 入口（UDreamUIBPLibrary）、K2 节点模块，以及创建 Dream Widget Blueprint、将其指向 .dui 并编译的编辑器 Python。"
---

<a id="blueprint-and-c-entry-points"></a>

## Blueprint 与 C++ 入口

运行时入口集中在 [`UDreamUIBPLibrary`](../Reference/DreamUIBPLibrary.md)（模块 `DreamGUI`、
`DreamUIBPLibrary.h`），命名刻意对照 UMG：

| 想做的事 | UMG | DreamGUI |
| --- | --- | --- |
| 实例化一个控件类但先不放到任何地方 | Create Widget 节点 / `CreateWidget` | Create Dream Widget 节点 / `CreateDreamWidgetOfClass` |
| 创建它并放到视口上 | `CreateWidget` + `AddToViewport` | Create Dream Widget + `AddToViewport`，或 `AddWidgetOfClassToViewport` |
| 将它从视口移除 | `RemoveFromParent` | `RemoveFromViewport` |
| 构造一个裸控件 | `WidgetTree::ConstructWidget<T>()` | `ConstructWidget` |
| 立即布局（本帧就需要尺寸） | `ForceLayoutPrepass` | `ForceLayoutPrepass` |
| 标记为下一次布局 | `InvalidateLayoutAndVolatility` | `InvalidateLayout` |
| 放入世界 | `WidgetComponent` | `AttachWidgetToSceneComponent`，或放置一个 `ADreamWorldWidgetActor` |

值得了解的细节：

- **Create Dream Widget 节点。** UMG 的 Create Widget 节点：选中一个类会为每个 Expose on Spawn
  属性提供一个引脚，并提供一个 Owning Player 引脚。这些引脚在控件初始化之前赋值，因此 Pre Construct、On
  Initialized 以及每个绑定的首次取值都能看到它们。它编译为 `BeginDeferredCreateDreamWidget`、
  各项赋值，然后是 `FinishDeferredCreateDreamWidget`；C++ 中对应的两半是 `BeginCreateDreamWidget` /
  `FinishCreateDreamWidget`（`Core/DreamUserWidget.h`）。因此 `CreateDreamWidgetOfClass` 是 BlueprintInternalUseOnly，
  正如 UMG 将 `Create` 标记的那样：它不在 Blueprint 菜单中，已调用它的图表仍能编译，并且 **它已从
  Python 中消失**——需要实例的编辑器脚本可使用 `add_widget_of_class_to_viewport`，或使用从 C++ 暴露的自己的函数。
- **返回引脚跟随类。** Create Dream Widget、`AddWidgetOfClassToViewport` 和
  `CreateDreamWidgetOfClass` 的返回值取类的形状，因此 Blueprint 子类自己的变量和函数无需 Cast 即可访问，
  与 UMG 的 Create Widget 节点一样。
- **屏幕根按需创建。** 当有东西首次放到视口上时，共享的屏幕空间根
  （`GetOrCreateScreenSpaceUIRoot`）、射线检测器和事件系统会被创建；无需任何配置。
  `AddWidgetOfClassToScreen` 是带 before-alive 回调和排序顺序的变体；`IsInViewport` 用于查询。
- **停放状态。** 由 `ConstructWidget`、`CreateDreamWidgetOfClass` 或 Create Dream Widget 创建的控件存在但不在屏幕上：它
  不绘制任何内容，行为也不会运行，直到你将其交给 `AddChild` 或放到视口上。同时 UI
  管理器持有它，因此在你配置它时不会在你眼皮底下被回收。`IsWidgetParked` 用于查询，
  控制台命令 `dreamgui.ListPendingWidgets` 会列出每个停放的控件。
- **放入世界。** `AttachWidgetToSceneComponent(Root, Component)` 是 AddToViewport 的世界空间对应物：
  根必须带有 DreamCanvas（画布决定渲染模式并拥有网格）且没有父级；它会被
  附加到场景组件，由此获得世界变换。否则返回 false 且不做任何更改。
  更常见的是放置一个 `ADreamWorldWidgetActor`，或向自己的 actor 添加 `UDreamWorldWidgetComponent` 并
  `SetWidgetClass`——参见[世界空间](../concepts/world-space.md)。

<a id="c-a-native-base-for-a-dui-screen"></a>

### C++：.dui 屏幕的原生基类

`.dui` 的节点 id 会成为**生成的**类的成员，而 C++ 基类在编译时无法命名它们，因此它按变量名
查找它们。处理函数路由名称（`OnClicked -> HandleApply`）是 `UFUNCTION()`：

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

显示它：

```cpp
#include "DreamUIBPLibrary.h"

// SettingsClass: a TSubclassOf<UDreamUserWidget> naming the Blueprint made from the .dui
UDreamWidget* Page = UDreamUIBPLibrary::AddWidgetOfClassToViewport(this, SettingsClass);
// ...
UDreamUIBPLibrary::RemoveFromViewport(this, Page);
```

<a id="animations-and-input-modes"></a>

### 动画与输入模式

`UDreamUserWidget::PlayAnimation` 是编译器生成的动画变量所接入的入口：将变量拖入并
放到 Animation 上。它旁边是 `PlayAnimationByName`、`PlayAnimationForward` / `PlayAnimationReverse`（“点击滑出，
再次点击滑回”这一惯用法）、`PlayAnimationTimeRange`，以及 `QueuePlayAnimation`，后者在动画自身回调内部也是安全的。它们返回一个 `FDreamUIAnimationHandle`；[`UDreamUIAnimationLibrary`](../Reference/DreamUIAnimationLibrary.md)
提供用于处理句柄的 Blueprint 函数。

输入模式是 [`UDreamUIInputModeLibrary`](../Reference/DreamUIInputModeLibrary.md)：`SetInputModeUIOnly`、
`SetInputModeGameAndUI` 和 `SetInputModeGameOnly`，与 UMG 的 `SetInputMode_*` 是同样三种状态，只是焦点给予
DreamGUI 控件。

<a id="the-k2-nodes-module"></a>

### K2 节点模块

`DreamGUIK2Nodes`（仅由运行未烘焙内容的进程加载）添加三个 Blueprint 节点：

| 节点 | 作用 |
| --- | --- |
| Play Animation with Finished event | `PlayAnimation` 的异步形式，带一个 Finished 执行引脚 |
| Play Animation Time Range with Finished event | `PlayAnimationTimeRange` 的异步形式 |
| Get Component for DreamGUIComponentReference | `FDreamUIComponentReference` 所指定的组件，其输出被转换到所引用的组件类型 |

<a id="editor-python-a-widget-blueprint-from-a-dui"></a>

## 编辑器 Python：从 .dui 生成 Widget Blueprint

批量创建资产——一系列组件、一组屏幕——不必一次点击一下 *Set Source File...*。
那个按钮所做的是**设置类默认值 `SourceFile` 并编译**，Python 也能做到。共四步：

<a id="create-it-with-dreamguis-factory"></a>

### 使用 DreamGUI 的工厂创建
`unreal.DreamWidgetBlueprintFactory` 创建一个 `UDreamWidgetBlueprint`，由 DreamGUI 自己的编译器编译，而引擎的
Blueprint 工厂做不到。跳过其选择器后，父类是普通的 `UDreamUserWidget`。

<a id="reparent-to-dreamtextuserwidget"></a>

### 重新设置父类为 DreamTextUserWidget
一个类只有在派生自 `UDreamTextUserWidget`（或你自己的其子类）时才能指定 `.dui`。使用
`BlueprintEditorLibrary.reparent_blueprint` 重新设置父类。

<a id="set-sourcefile"></a>

### 设置 SourceFile
在生成类的默认对象上设置 `SourceFile`。相对路径以 `DUI/` 源目录为根。

<a id="compile-and-save"></a>

### 编译并保存
`compile_blueprint` 读取文件并构建树；然后保存包。

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

该脚本可以安全地重复运行：现有资产会被保留，重新指向其源文件并重新编译，因此更改过的 `.dui`
会被读取。它仅使用核心编辑器向 Python 暴露的内容（资产注册表、AssetTools、
`EditorLoadingAndSavingUtils`、`BlueprintEditorLibrary`），不使用默认关闭的 Editor Scripting Utilities 插件。
属性按 C++ 的命名方式命名（`"SourceFile"`），Python 也能解析。

要使用你自己的原生基类（例如上面的 `UMySettingsScreen`），改为重新设置父类到
`unreal.load_class(None, "/Script/MyGame.MySettingsScreen")`，并测试它是否为某个子类。

**创建多个时顺序很重要**：编译一个屏幕会加载其 `.dui` 通过
`use … as` 放置的组件的类，因此应先编译组件，之后再编译使用它们的屏幕。

<a id="running-it"></a>

### 运行它

无头运行，作为 commandlet：

```powershell
UnrealEditor-Cmd.exe <project>.uproject -EnablePlugins=PythonScriptPlugin -run=pythonscript -script=<path>\make_settings_assets.py -unattended -nullrhi
```

或在编辑器内，于启动时：

```powershell
UnrealEditor.exe <project>.uproject -ExecutePythonScript=<path>\make_settings_assets.py
```

一个还会**导入文件**的脚本（通过 `AssetImportTask` 导入字体或纹理）必须在编辑器中运行：导入
需要 Slate，而 `-run=pythonscript` commandlet 没有。仅创建 Blueprint、设置属性、
编译并保存的脚本作为 commandlet 运行没有问题。

一旦某个类指定了其源文件，在编辑器中保存该 `.dui` 就会重建该类，而编辑器的 *Rebuild DUI*
会一次性重新编译每个以文本为基础的类。
