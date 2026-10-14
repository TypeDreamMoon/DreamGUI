---
title: "完整示例"
description: "一个由一个行组件、一个库和一个界面三个文件组成的设置界面，逐段讲解每一部分依赖语言的哪条规则。"
---

<a id="a-worked-example"></a>

## 完整示例

一个用同一个行组件搭起来的设置界面。三个文件：组件本身；一个给它起名、并给这一族组件定样式的库；以及界面。

行的左边是标签，右边是一个带左右箭头的值。它声明宿主能设什么、能听到什么：

```
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

库放这一族组件的外观和名字。它没有根节点，所以既能被普通使用，也能放进命名空间；它的 `use … as` 行会随它一起走：

```
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

界面放了三行，把每一行的 `Changed` 路由到各自的处理函数，只在有未保存改动时显示一条提示，并按类的数据列出按键提示：

```
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

- `use … as ui` 让库的名字和界面自己的名字分开：`: ui.Caption`、`@ui.Panel`，行组件则是 `ui.Row`——库自己那行
  `use "SettingsRow.dui" as Row` 随库一起进了命名空间。
- `ui.Row Audio { … }` 是行组件的一个实例：`Label`、`Value` 和 `Index` 设的是它的 props——只是初始值，之后处理函数通过行的变量
  `Audio` 去改 `Value`；`Changed ->` 路由的是行从箭头按钮里用 `emit` 抛出的事件。
- `VerticalBox Page` 和那个无名的 `HorizontalBox` 都是把容器写成节点类型；它们的 `Spacing` 和
  `Padding` 是容器的属性。
- `Widget Rows : ui.Column` 从样式拿到它的竖排盒子和 Fill。
- `if` 只在 `HasUnsavedChanges()` 为真时显示提示；它翻转时没有东西被创建或销毁。
- `for` 为类的 `KeyHints` 数组（一个 FieldNotify 的对象变量数组，元素有 `Label`
  成员）每一项生成一个文字，排在两个按钮之后，`Text <- Hint.Label` 把每个副本绑到它那一项上。
- 界面的类需要提供 `HasUnsavedChanges`、`KeyHints`、三个 `Handle…Changed(Index, Step)` 处理函数、
  `HandleApply` 和 `HandleBack`，写在 C++ 里或 Blueprint 的图表里都行。
