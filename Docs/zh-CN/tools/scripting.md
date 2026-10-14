---
title: 脚本与代码入口
description: 从 Blueprint 和 C++ 创建、显示、放进世界的入口（UDreamUIBPLibrary）、K2 节点模块，以及用编辑器 Python 创建 Dream Widget Blueprint、把它指向一个 .dui 并编译。
---

<a id="blueprint-与-c-的入口"></a>

## Blueprint 与 C++ 的入口

运行时的入口集中在 [`UDreamUIBPLibrary`](../Reference/DreamUIBPLibrary.md)（`DreamGUI` 模块，`DreamUIBPLibrary.h`）。
名字刻意对着 UMG：

| 想做的事 | UMG | DreamGUI |
| --- | --- | --- |
| 实例化一个 widget 类，先不放到任何地方 | Create Widget 节点 / `CreateWidget` | Create Dream Widget 节点 / `CreateDreamWidgetOfClass` |
| 创建并放上视口 | `CreateWidget` + `AddToViewport` | Create Dream Widget + `AddToViewport`，或者 `AddWidgetOfClassToViewport` |
| 从视口拿掉 | `RemoveFromParent` | `RemoveFromViewport` |
| 构造一个裸 widget | `WidgetTree::ConstructWidget<T>()` | `ConstructWidget` |
| 立刻布局（同一帧要尺寸时） | `ForceLayoutPrepass` | `ForceLayoutPrepass` |
| 标记下一次布局 | `InvalidateLayoutAndVolatility` | `InvalidateLayout` |
| 放进世界 | `WidgetComponent` | `AttachWidgetToSceneComponent`，或放一个 `ADreamWorldWidgetActor` |

几个细节：

- **Create Dream Widget 节点**。和 UMG 的 Create Widget 节点一样：选了类之后为它的每个 Expose on Spawn 属性开一个引脚，另有
  Owning Player 引脚。引脚在 widget 初始化之前赋值，Pre Construct、On Initialized 和每个绑定的第一次取值都看得到。它编译成
  `BeginDeferredCreateDreamWidget`、各个赋值、`FinishDeferredCreateDreamWidget`；C++ 里对应的是 `BeginCreateDreamWidget` /
  `FinishCreateDreamWidget`（`Core/DreamUserWidget.h`）。`CreateDreamWidgetOfClass` 因此和 UMG 的 `Create` 一样标成了
  BlueprintInternalUseOnly：蓝图菜单里没有它，已经调用它的图照常编译，**Python 里也没有它了**——编辑器脚本要创建实例，
  就用 `add_widget_of_class_to_viewport`，或者从 C++ 暴露自己的函数。
- **返回引脚跟着类走**。Create Dream Widget、`AddWidgetOfClassToViewport` 和 `CreateDreamWidgetOfClass` 的返回值形状由类决定，
  Blueprint 子类自己的变量和函数不用 Cast 就能用，和 UMG 的 Create Widget 节点一样。
- **屏幕根是按需创建的**。第一次放上视口时，共享的屏幕空间根（`GetOrCreateScreenSpaceUIRoot`）、射线检测器和事件系统
  都会自动建好，不需要任何配置。`AddWidgetOfClassToScreen` 是带"变活之前回调"和排序参数的版本，`IsInViewport` 查询。
- **"停放"状态**。`ConstructWidget`、`CreateDreamWidgetOfClass` 和 Create Dream Widget 创建的 widget 存在、但不在屏幕上：不画、行为不运行，直到
  你把它交给 `AddChild` 或放上视口。这期间它被 UI 管理器持有，不会在你配置它时被回收。`IsWidgetParked` 查询这个状态，
  控制台命令 `dreamgui.ListPendingWidgets` 列出所有停放的 widget。
- **放进世界**。`AttachWidgetToSceneComponent(Root, Component)` 是世界空间版的 AddToViewport：根必须带一个 DreamCanvas（画布决定
  渲染模式、拥有网格），而且还没有父节点；它挂到场景组件上，由组件给它世界变换。不满足时返回 false、什么都不改。
  更常用的是一个 `ADreamWorldWidgetActor`，或者在自己的 actor 上加 `UDreamWorldWidgetComponent` 并 `SetWidgetClass`——
  见[世界空间](../concepts/world-space.md)。

<a id="c一个-dui-屏幕的原生基类"></a>

### C++：一个 .dui 屏幕的原生基类

`.dui` 里的节点 id 会成为**生成的类**的成员变量，C++ 基类在编译时叫不出它们的名字，所以按变量名去找。路由（`OnClicked -> HandleApply`）
指向的处理函数写成 `UFUNCTION()`：

```cpp
// MySettingsScreen.h —— Build.cs 里加上 "DreamGUI" 和 "DreamGUIControls"
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

<a id="动画与输入模式"></a>

### 动画与输入模式

`UDreamUserWidget` 的 `PlayAnimation` 是编译器生成的动画变量所接的入口：把变量拖进来接到 Animation 上。旁边还有
`PlayAnimationByName`、`PlayAnimationForward` / `PlayAnimationReverse`（"点一下滑出、再点一下滑回"）、`PlayAnimationTimeRange`，
以及在动画自己的回调里也安全的 `QueuePlayAnimation`。它们返回 `FDreamUIAnimationHandle`，
[`UDreamUIAnimationLibrary`](../Reference/DreamUIAnimationLibrary.md) 提供处理句柄的 Blueprint 函数。

输入模式用 [`UDreamUIInputModeLibrary`](../Reference/DreamUIInputModeLibrary.md)：`SetInputModeUIOnly`、`SetInputModeGameAndUI`、
`SetInputModeGameOnly`，和 UMG 的 `SetInputMode_*` 是同样三种状态，只是焦点给的是一个 DreamGUI widget。

<a id="k2-节点模块"></a>

### K2 节点模块

`DreamGUIK2Nodes`（只在未烹饪的进程里加载）提供三个 Blueprint 节点：

| 节点 | 作用 |
| --- | --- |
| Play Animation with Finished event | `PlayAnimation` 的异步形式，多一个 Finished 执行引脚 |
| Play Animation Time Range with Finished event | `PlayAnimationTimeRange` 的异步形式 |
| Get Component for DreamGUIComponentReference | 从一个 `FDreamUIComponentReference` 取组件，输出自动转成引用的组件类型 |

<a id="编辑器-python从-dui-生成-widget-blueprint"></a>

## 编辑器 Python：从 .dui 生成 Widget Blueprint

批量建资产——一组组件、一组屏幕——不用一个个点 *Set Source File...*。设计器那个按钮做的事，就是**设置类默认值 `SourceFile` 再编译**，
Python 也能做。流程分四步：

<a id="用-dreamgui-的工厂创建"></a>

### 用 DreamGUI 的工厂创建
`unreal.DreamWidgetBlueprintFactory` 创建的是 `UDreamWidgetBlueprint`，由 DreamGUI 自己的编译器编译——引擎的 Blueprint 工厂做不到。
跳过选择器时，它的父类是普通的 `UDreamUserWidget`。

<a id="改父类为-dreamtextuserwidget"></a>

### 改父类为 DreamTextUserWidget
一个类能指向 `.dui`，当且仅当它派生自 `UDreamTextUserWidget`（或你自己的子类）。用 `BlueprintEditorLibrary.reparent_blueprint` 改。

<a id="设置-sourcefile"></a>

### 设置 SourceFile
在生成类的默认对象上设置 `SourceFile`。相对路径以 `DUI/` 源目录为根。

<a id="编译并保存"></a>

### 编译并保存
`compile_blueprint` 读文件、建树；然后保存包。

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

脚本可以重跑：已有的资产保留，重新指向源文件、重新编译，所以改过的 `.dui` 会被读进来。只用到核心编辑器暴露给 Python 的东西
（资产注册表、AssetTools、`EditorLoadingAndSavingUtils`、`BlueprintEditorLibrary`），不需要默认关闭的 Editor Scripting Utilities 插件。
属性按 C++ 的名字写（`"SourceFile"`），Python 也认。

要换成你自己的原生基类（比如上面的 `UMySettingsScreen`），把 `reparent_blueprint` 的目标换成 `unreal.load_class(None, "/Script/MyGame.MySettingsScreen")`，
判断条件改成"是不是它的子类"。

**一次建多个时注意顺序**：一个屏幕的 `.dui` 用 `use … as` 放了哪些组件，编译它时就要加载那些组件的类，所以先编译组件，再编译用它们的屏幕。

<a id="怎么运行"></a>

### 怎么运行

无头地，作为 commandlet：

```powershell
UnrealEditor-Cmd.exe <project>.uproject -EnablePlugins=PythonScriptPlugin -run=pythonscript -script=<path>\make_settings_assets.py -unattended -nullrhi
```

或者在编辑器里，启动时执行：

```powershell
UnrealEditor.exe <project>.uproject -ExecutePythonScript=<path>\make_settings_assets.py
```

脚本如果还要**导入文件**（用 `AssetImportTask` 导入字体、纹理），就得在编辑器里跑：导入需要 Slate，而 `-run=pythonscript` 的 commandlet 没有。
只建 Blueprint、设属性、编译、保存的脚本用 commandlet 就行。

类指向源文件之后，在编辑器里保存那个 `.dui` 就会重建这个类；编辑器的 *Rebuild DUI* 一次重编所有文本支撑的类。
