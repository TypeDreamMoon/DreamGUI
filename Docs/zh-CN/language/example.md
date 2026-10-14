---
title: 完整示例
description: 一个由一个行组件、一个库和一个界面三个文件组成的设置界面，逐段讲解每一部分依赖语言的哪条规则。
---

一个用同一个行组件搭起来的设置界面。三个文件：组件本身；一个给它起名、并给这一族组件定样式的库；以及界面。

```text
MyProject/
  Content/
    UI/
      Settings/
        WBP_SettingsRow.uasset
        WBP_SettingsScreen.uasset
  DUI/
    Settings/
      SettingsRow.dui
      SettingsLibrary.dui
      SettingsScreen.dui
```

两个 Blueprint 都以 `UDreamTextUserWidget` 为父类，Source File 分别指向 `Settings/SettingsRow.dui` 和 `Settings/SettingsScreen.dui`。
库没有根节点，不对应任何 Blueprint。

<a id="组件一行设置"></a>

## 组件：一行设置

行的左边是标签，右边是一个带左右箭头的值。它声明宿主能设什么、能听到什么：

```dui
// DUI/Settings/SettingsRow.dui
class /Game/UI/Settings/WBP_SettingsRow
use "Settings/SettingsLibrary.dui"

props {
    Text   Label
    Text   Value
    Number Index = 0
}
events {
    Changed(Number Index, Number Step)
}

Widget Root : RowBox {
    + HorizontalBox { Padding = (16, 0, 16, 0)  Spacing = 12 }
    + UIButton { TransitionType = None }

    Text LabelText : Caption {
        Text <- Label
        @fill
    }
    Native.Button Previous : Arrow {
        OnClicked -> emit Changed(Index, -1)
        Text { Text = "◀" }
    }
    Text ValueText : Caption {
        Text <- Value
        HAlign = Center
        @slot MinDesiredSize = (220, 0)
    }
    Native.Button Next : Arrow {
        OnClicked -> emit Changed(Index, 1)
        Text { Text = "▶" }
    }
}
```

- `class` 行写明了它编译进 `WBP_SettingsRow`。库里的 `use … as Row` 就是从这一行读出类的，所以不依赖编辑器去找 Blueprint。
- `use "Settings/SettingsLibrary.dui"` 让这一行拿到库里的样式（`RowBox`、`Caption`、`Arrow`）。库反过来也 `use` 了这个文件——
  这不是循环：`use … as` 只读组件文件的头部，不跟它的 `use` 行（见 [`use`](use.md)）。
- `props` 声明了三个 Blueprint 变量 `Label`、`Value`、`Index`，`events` 声明了一个带两个参数的事件分发器 `Changed`。
- 根节点用两个 `+` 块挂了一个横排盒子和一个按钮行为；`RowBox` 样式给它高度。
- `LabelText` 把文字绑到 prop `Label` 上，`@fill` 让它占掉横排里剩下的宽度。`ValueText` 绑 `Value`，并用槽位行要求至少 220 宽。
- 两个箭头按钮各自 `emit Changed`，参数是绑定表达式：`Index` 是 prop，`-1` 和 `1` 是步长。按钮里的 `Text` 没写 id，它们的 id 是
  `Previous__Text0` 和 `Next__Text0`。

<a id="库名字和样式"></a>

## 库：名字和样式

库放这一族组件的外观和名字。它没有根节点，所以既能被普通 `use`，也能放进命名空间；它的 `use … as` 行会随它一起走：

```dui
// DUI/Settings/SettingsLibrary.dui
use "Settings/SettingsRow.dui" as Row

resources {
    Color Ink   = #E6E9F0
    Color Muted = #8C93A6
    Color Panel = #1B1E26
}

style Caption {
    FontSize = 18
    Color    = @Ink
}
style RowBox {
    AnchorData.SizeDelta = (0, 48)
}
style Arrow {
    AnchorData.SizeDelta = (40, 40)
}
style Column {
    + VerticalBox { Spacing = 8 }
    @fill
}
```

- `use … as Row` 给行组件起名。每个用这个库的界面都会得到这个名字：普通 `use` 后是 `Row`，`use … as ui` 后是 `ui.Row`。
- 三个颜色资源会成为每个导入它的类的变量。`Caption` 里的 `@Ink` 永远指这个库的 `Ink`，即使它是在命名空间下被用到的。
- `Column` 样式带着一个竖排盒子和一个 `@fill`，于是“一列”就是一个名字（见[样式与资源](styles-and-resources.md)）。

<a id="界面"></a>

## 界面

界面放了三行，把每一行的 `Changed` 路由到各自的处理函数，只在有未保存改动时显示一条提示，并按类的数据列出按键提示：

```dui
// DUI/Settings/SettingsScreen.dui
class /Game/UI/Settings/WBP_SettingsScreen
use "Settings/SettingsLibrary.dui" as ui

Widget Root {
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)
    + Overlay { }

    Image Backdrop {
        Brush.TintColor = @ui.Panel
        @slot { HorizontalAlignment = Fill  VerticalAlignment = Fill }
    }

    VerticalBox Page {
        Spacing = 24
        Padding = (64, 48, 64, 48)
        @slot { HorizontalAlignment = Fill  VerticalAlignment = Fill }

        Text Heading : ui.Caption {
            Text = "Settings"
            FontSize = 32
        }

        Widget Rows : ui.Column {
            ui.Row Audio {
                Label = "Master volume"
                Value = "80 %"
                Changed -> HandleVolumeChanged
            }
            ui.Row Display {
                Label = "Display mode"
                Value = "Fullscreen"
                Index = 1
                Changed -> HandleDisplayModeChanged
            }
            ui.Row Language {
                Label = "Language"
                Value = "English"
                Index = 2
                Changed -> HandleLanguageChanged
            }
        }

        if HasUnsavedChanges() {
            Text UnsavedHint : ui.Caption {
                Text = "Unsaved changes"
                Color = @ui.Muted
            }
        }

        HorizontalBox {
            Spacing = 16
            Native.Button Apply { OnClicked -> HandleApply }
            Native.Button Back  { OnClicked -> HandleBack }
            for Hint in KeyHints {
                Text : ui.Caption { Text <- Hint.Label }
            }
        }
    }
}
```

逐段看它依赖的规则：

- **命名空间。** `use … as ui` 让库的名字和界面自己的名字分开：`: ui.Caption`、`@ui.Panel`，行组件则是 `ui.Row`——库自己那行
  `use "Settings/SettingsRow.dui" as Row` 随库一起进了命名空间。见 [`use`](use.md)。
- **组件实例。** `ui.Row Audio { … }` 是行组件的一个实例：`Label`、`Value`、`Index` 设的是它的 props——只是初始值，之后处理函数通过行的变量
  `Audio` 去改 `Value`；`Changed ->` 路由的是行从箭头按钮里 `emit` 出来的事件。见[编写组件](components.md)。
- **容器作为类型。** `VerticalBox Page` 和那个无名的 `HorizontalBox` 都是把容器写成节点类型，它们的 `Spacing` 和 `Padding` 是容器的属性。
  无名的那个 id 是 `Page__HorizontalBox0`。见[节点与值](nodes-and-values.md)。
- **样式带组件。** `Widget Rows : ui.Column` 从样式拿到它的竖排盒子和 Fill 槽位。
- **`if`。** 提示只在 `HasUnsavedChanges()` 为真时显示；它翻转时没有东西被创建或销毁，只是可见性变了。见 [if 与 for](control-flow.md)。
- **`for`。** 类的 `KeyHints`（一个 FieldNotify 的对象数组变量，元素有 `Label` 成员）每一项一个文字，排在两个按钮之后；
  `Text <- Hint.Label` 把每个副本绑到它那一项上。
- **背景和铺满。** 根节点四角锚定、零内缩，挂一个 `Overlay`；`Backdrop` 和 `Page` 都用 `@slot { … }` 在两个方向上 Fill。

<a id="类要提供什么"></a>

## 类要提供什么

界面的类需要提供这些成员，写在 C++ 里或 Blueprint 的图表里都行：

| 成员 | 用在 | 要求 |
| --- | --- | --- |
| `HasUnsavedChanges` | `if` 的条件 | 无参数函数，返回布尔值 |
| `KeyHints` | `for` 的源 | FieldNotify 的对象数组变量，元素有 `Label`（[DUI6006](../diagnostics/codes.md#dui6006)、[DUI6007](../diagnostics/codes.md#dui6007)） |
| `HandleVolumeChanged`、`HandleDisplayModeChanged`、`HandleLanguageChanged` | `Changed ->` | 参数和事件一样：`(Index, Step)`（[DUI6004](../diagnostics/codes.md#dui6004)、[DUI6005](../diagnostics/codes.md#dui6005)） |
| `HandleApply`、`HandleBack` | `OnClicked ->` | 参数和按钮的 `OnClicked` 一样 |

行组件的类则什么都不用额外写：它的 props 和事件都由 `.dui` 声明，编译时生成。
