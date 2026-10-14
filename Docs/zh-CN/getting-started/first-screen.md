---
title: "你的第一个界面"
description: "把插件的 HelloDreamGUI.dui 从文本带到屏幕上，逐行读懂这个文件，再把同一个类立进关卡里。"
---

插件自带一个样例，`Content/Samples/HelloDreamGUI.dui`。它是仍然算作一个真实界面的最小 `.dui`——一个锚定的根、一个 Overlay、一张卡片、一列竖排的文字。下面这四步就是从文本到屏幕上 UI 的全部路径，其他什么都不用配置：屏幕根、射线检测器和事件系统都会按需创建。

<a id="copy-the-file-into-the-project-first"></a>

## 先把文件拷进项目

把它拷到项目的 `DUI/` 目录下——比如 `<Project>/DUI/HelloDreamGUI.dui`——然后只改这一份。两个原因：

- 插件目录里那份是参考，插件更新会覆盖它；
- 选文件时，DreamGUI 会把路径存成相对某个 `DUI/` 根的形式——项目里的存成 `HelloDreamGUI.dui`，插件里的存成 `Plugin.X:…`。不在任何 `DUI/` 根下的文件只能存成绝对路径，这只在某一台机器上有效。

`.dui` 是源码，不是资产，所以不放在 `Content/` 下。详见[项目布局](project-layout.md)。

<a id="four-steps-to-the-screen"></a>

## 四步上屏

<a id="make-a-widget-blueprint"></a>

### 建一个控件蓝图

内容浏览器的 **Add** 菜单（或在空白处右键）→ **DreamGUI Widget**，位于最上面一组，和引擎的 Blueprint Class、Material 排在一起；**DreamGUI** 子菜单里它也是第一个。会先后弹出两个选择框：

- **父类**：选 **DreamUI Text User Widget**（`UDreamTextUserWidget`）。只有以它为父类的控件蓝图才从 `.dui` 构建自己的树；父类是别的时，设计器上根本不会显示源文件按钮。
- **根布局**：随便选，选 None 也行——编译时文件里的树会替换掉整个层级。

放在 `/Game/UI`，命名为 `WBP_HelloDreamGUI`。文件第一行 `class /Game/UI/WBP_HelloDreamGUI` 说的就是这个位置；两边对不上时编译会给出警告 [DUI6003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6003)——要么挪资产，要么改那一行。

<a id="point-it-at-the-file"></a>

### 把它指向这个文件

打开蓝图。设计器工具栏上有一个源文件按钮，还没指定文件时它显示 **No Source File**。打开它的菜单 → **Set Source File...** → 选中刚才拷进来的文件。之后按钮上显示文件名，悬停提示是解析后的完整路径。

同一个菜单里还有 **Create Source File...**（类还没有指定文件时才出现：写一个带起始层级的新 `.dui` 并让类指向它）、**Open in Default Editor**、**Show in Explorer**，装了 VS Code 扩展时还有 **Reveal in VS Code**。

<a id="compile"></a>

### 编译

层级面板里现在就是文件描述的树：`Root`、`Card`、`Backdrop`、`Column`、`Heading`、`Subheading`。设计器编辑的是同一个类，你在设计器里改的东西会写回这个文件（见[写回](../language/write-back.md)）；反过来，在外部编辑器里保存这个文件会重新编译所有以它为源、已加载的类。

<a id="show-it"></a>

### 显示出来

蓝图里——比如关卡蓝图的 BeginPlay——放一个 **Create Dream Widget** 节点，Class 选 `WBP_HelloDreamGUI`，再把它的返回值接到 **Add to Viewport**，和 UMG 的 Create Widget 一样。选好类之后，节点上会为它的每个 Expose on Spawn 属性开一个引脚（`.dui` 的每个 `props` 和 `viewmodels` 条目都是一个），另有一个 Owning Player 引脚；这些引脚在 widget 初始化**之前**赋值，所以 On Initialized 和每个绑定的第一次取值都能看到它们。Add to Viewport 的 ZOrder 决定它和别的屏幕页的前后，拿下来用 **Remove from Parent**。

C++ 里一个调用就够，模块依赖 `DreamGUI`：

```cpp
#include "DreamUIBPLibrary.h"

// HelloClass is a TSubclassOf<UDreamUserWidget>, e.g. a UPROPERTY set to WBP_HelloDreamGUI.
UDreamUIBPLibrary::AddWidgetOfClassToViewport(this, HelloClass);
```

它返回加到屏幕上的根控件；可选的第三个参数 `SortOrder` 决定它和别的屏幕页的前后。拿下来用 `UDreamUIBPLibrary::RemoveFromViewport`。

<a id="reading-the-file"></a>

## 逐行读这个文件

下面是样例去掉文件头注释和一条分隔线后的样子——仍然是一个完整、能编译的文件：

```dui
class /Game/UI/WBP_HelloDreamGUI

style Title {
    Font     = /DreamGUI/DefaultFont_DistanceField
    FontSize = 28
    Color    = #E6E9F0
    HAlign   = Center
}

style Body {
    Font     = /DreamGUI/DefaultFont_DistanceField
    FontSize = 15
    Color    = #8C93A6
    HAlign   = Center
}

Widget Root {
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)

    + Overlay {}

    Widget Card {
        AnchorData.SizeDelta = (420, 220)
        @slot HorizontalAlignment = Center
        @slot VerticalAlignment   = Center

        + Overlay {}

        Image Backdrop {
            Brush.TintColor = #1B1E26FF
            @slot HorizontalAlignment = Fill
            @slot VerticalAlignment   = Fill
        }

        Widget Column {
            @slot HorizontalAlignment = Fill
            @slot VerticalAlignment   = Fill

            + VerticalBox {
                Spacing = 10
                Padding = (24, 28, 24, 28)
            }

            Text Heading : Title {
                AnchorData.SizeDelta = (0, 34)
                Text = "Hello, DreamGUI"
                WrapTextAt = 372
                @slot HorizontalAlignment = Fill
                @slot SizeRule = Auto
            }

            Text Subheading : Body {
                AnchorData.SizeDelta = (0, 46)
                Text = "This screen is a text file. Edit it, compile, and the class changes with it."
                WrapTextAt = 360
                @slot HorizontalAlignment = Fill
                @slot SizeRule = Auto
            }
        }
    }
}
```

<a id="class"></a>

### `class`

这个文件是哪个控件蓝图的源码；一个文件最多一行。它的第二个作用是做本地化命名空间：文件里每个本地化字符串的 key 都挂在这个路径下，所以文件改名时 key 不变。没有这一行时，用文件自己的名字做命名空间。

<a id="style"></a>

### `style`

一组有名字的属性行，用 `Node : StyleName` 套到节点上（`Text Heading : Title`）。节点自己写的行优先于样式里的。顺序随意：样式可以声明在用它的节点下面。

<a id="widget"></a>

### `Widget`

一个没有自己可视部分的节点——一个只为了装别的东西而存在的矩形。节点由类型、id 和一个可选的块组成。id（`Root`、`Card`……）就是这个控件的身份：控件的名字、蓝图图表里读到的成员变量、绑定解析时用的 key、本地化 key 都是它。它必须是合法的 C++ 标识符，并且在文件内不区分大小写地唯一（[DUI3001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3001)）。

<a id="-overlay-"></a>

### `+ Overlay {}`

挂在节点上的一个组件。布局住在组件里（`Overlay`、`VerticalBox`），而不是住在控件类型里——这就是为什么一个节点可以按一种方式排布、按另一种方式绘制。一个节点只有一个布局容器。Overlay 按声明顺序叠放子节点，所以 `Backdrop` 写在前面，就画在后面。

<a id="image--text"></a>

### `Image` / `Text`

带一个会画东西的 visual 的节点。属性名可以是点分路径，伸进结构体属性里（`Brush.TintColor`）；颜色是 sRGB 十六进制，3、4、6、8 位都行；`(24, 28, 24, 28)` 这样的元组是什么结构体由属性决定。

<a id="slot-versus-anchordatasizedelta"></a>

### `@slot` 和 `AnchorData.SizeDelta`

整个文件里最值得读两遍的就是这一处区别：

- `AnchorData.*` 是**节点自己的矩形**。`Root` 锚在 (0, 0)–(1, 1)、`SizeDelta` 为 (0, 0)，意思是"四边贴住父级、不留边"：在任何分辨率下都铺满它被加进去的东西。`Card` 的 `SizeDelta` (420, 220) 就是它的尺寸。
- `@slot …` 是**这个节点对它上层那个面板提的要求**，也就是父级怎么安排它：`HorizontalAlignment`、`VerticalAlignment`、`SizeRule`。父级没有布局面板时写 slot 行是 [DUI5003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5003)。

两段文字上的 `WrapTextAt` 也和这个区别有关。一段文本有 `WrapTextAt` 时在那里折行，没有时按自己的宽度折行；而竖排的列在给子节点宽度**之前**先问它想要多高。所以放在 Fill 插槽里的文本要自己报一个折行宽度：这里取卡片预期的最大宽度 420，减去列两侧各 24，得 372。不写的话，标题会按它自己的宽度 0 被测量，变成一行一个字。

**注意：**

  同一棵树还有更短的写法：布局容器可以直接当节点类型（`Overlay Card`、`VerticalBox Column`），slot 行可以合进 `@slot { … }` 块，`@fill` 代替 `@slot SizeRule = Fill`。样例用长写法，是为了在你学习"组件"和"插槽"时把两件事分开。语言的完整说明从 [.dui 概览](../language/overview.md)开始。

下面是卡片和标题用短写法的样子（只留了标题）：

```dui
class /Game/UI/WBP_HelloDreamGUI

Widget Root {
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)
    + Overlay {}

    Overlay Card {
        AnchorData.SizeDelta = (420, 220)
        @slot { HorizontalAlignment = Center  VerticalAlignment = Center }

        Image Backdrop {
            Brush.TintColor = #1B1E26FF
            @slot { HorizontalAlignment = Fill  VerticalAlignment = Fill }
        }

        VerticalBox Column {
            Spacing = 10
            Padding = (24, 28, 24, 28)
            @slot { HorizontalAlignment = Fill  VerticalAlignment = Fill }

            Text Heading {
                AnchorData.SizeDelta = (0, 34)
                Text       = "Hello, DreamGUI"
                FontSize   = 28
                HAlign     = Center
                WrapTextAt = 372
                @slot { HorizontalAlignment = Fill  SizeRule = Auto }
            }
        }
    }
}
```

<a id="the-same-class-in-the-world"></a>

## 同一个类，放进世界里

同一个类可以变成关卡里的一块表面，而不是屏幕上的一层。把 `WBP_HelloDreamGUI` 从内容浏览器拖进关卡，或者从 *Place Actors* 面板放置一个 **DreamUI World Widget Actor**：两种方式得到的都是一个 `ADreamWorldWidgetActor`，它的全部内容就是一个 `UDreamWorldWidgetComponent`。像任何其他场景组件一样移动、旋转和挂接它。

组件托管这棵树，但不重新定义它。蓝图自己的根画布仍然是 UI 如何构建的依据；组件只把渲染模式、排序顺序和追踪通道写到它上面，其余不动。

| 属性 | 作用 |
| --- | --- |
| `WidgetClass` | 要加载的控件蓝图类。改它是唯一会重建整棵树的编辑 |
| `Backend` | `DreamUIRenderer`：DreamGUI 自己的渲染器 —— 平面、不受光、不受后处理影响，绘制顺序精确。`UERenderer`：引擎的管线 —— 像关卡里任何网格一样受光、投影、雾和遮挡，按半透明规则排序 |
| `bUseDesignSize` / `DrawSize` | 树落地的尺寸。默认开启，沿用蓝图设计时的尺寸；关闭后用 `DrawSize`（世界单位，默认 1920 × 1080） |
| `Pivot` | Actor 原点落在这个矩形内的位置；(0.5, 0.5) 居中 |
| `SortOrder` | 世界空间画布之间的先后顺序 |
| `TraceChannel` | 这块画布应答的通道，默认 Visibility。射线检测器只看通道与自己一致的画布 |

这些属性是实时生效的：改变尺寸、轴心、后端或排序顺序会重新作用到已经加载的树上。

**交互无需配置。** 在 `BeginPlay` 时，组件为每个本地玩家索取一个事件系统和一个 `UDreamWorldSpaceRaycaster`，缺哪个就补哪个，所以按下 Play 就足以点击悬挂在世界里的按钮。射线检测器从光标或屏幕中心发出（`PointerSource`：`Mouse` 或 `ScreenCenter`）。`bOccludeByWorld` 默认开启，让实心几何体像挡住 line trace 一样挡住点击；要穿墙点击，在你自己的射线检测器上取消勾选它，或者对玩家的射线检测器调用 `SetOccludeByWorld(false)`。在任何 Actor 上放置一个用户索引相同的你自己的射线检测器，插件就不会再额外添加 —— 它检查的是已存在的，而不是这个插件创建的。

从代码中则是 `UDreamUIBPLibrary::ConstructWidget`，然后 `UDreamUIBPLibrary::AttachWidgetToSceneComponent`，后者把根挂接到某个场景组件上；根必须带有一个 `DreamCanvas`。`ADreamWorldWidgetActor` 可以直接被 Level Sequence 持有，组件上的 `WidgetOpacity`、`WidgetOffset` 和 `bWidgetVisible` 都能在 Sequencer 里打关键帧。

<a id="next"></a>

## 下一步

  - [工作流程](workflow.md) —— 写、编译、放置、交互、动画、调试 —— 一天里的形状。
  - [世界空间](../concepts/world-space.md) —— 世界空间画布、两种后端和射线检测的细节。
  - [.dui 概览](../language/overview.md) —— 文件结构、节点、值，以及语言的其余部分。
