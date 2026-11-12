---
title: "完整示例"
description: "一个由三个文件——一个行组件、一个库和一个界面——组成的设置界面，按原样即可编译并布局，逐段讲解每一部分依赖语言的哪条规则。"
---

<a id="a-worked-example"></a>

## 完整示例

一个用同一个行组件搭起来的设置界面。三个文件：组件本身；一个给它起名、并给这一族组件定样式的库；以及界面。按原样复制到项目的 `DUI/` 文件夹后，它们编译和布局都不会产生错误和警告：界面不需要 C++ 或蓝图图表提供任何东西。

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

    Text LabelText : Caption {
        Text <- Label
        HAlign = Left
        @fill
    }
    Native.Button Previous : Arrow {
        OnClicked -> emit Changed(Index, -1)
        Text { Text = "<" }
    }
    Text ValueText : Caption {
        Text <- Value
        @slot MinDesiredSize = (220, 0)
    }
    Native.Button Next : Arrow {
        OnClicked -> emit Changed(Index, 1)
        Text { Text = ">" }
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
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)
}
style RowSlot {
    @slot HorizontalAlignment = Fill
    @slot MinDesiredSize      = (0, 48)
}
style Arrow {
    AnchorData.SizeDelta = (40, 40)
    @slot MinDesiredSize = (40, 40)
}
style Column {
    + VerticalBox { Spacing = 8 }
    @fill
}
```

界面放了三行，把每一行的 `Changed` 变成各自的事件，事件说明是哪一行，在存在未保存改动时显示一条提示，并从它的按钮中为打开它的代码抛出 `Applied` 和 `Closed`：

```
// DUI/Settings/SettingsScreen.dui
class /Game/UI/Settings/WBP_SettingsScreen
use "Settings/SettingsLibrary.dui" as ui

props {
    Bool Unsaved = true
}
events {
    SettingChanged(Number Index)
    Applied()
    Closed()
}

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
            HAlign = Left
        }

        Widget Rows : ui.Column {
            ui.Row Audio : ui.RowSlot {
                Label = "Master volume"
                Value = "80 %"
                Changed -> emit SettingChanged(0)
            }
            ui.Row Display : ui.RowSlot {
                Label = "Display mode"
                Value = "Fullscreen"
                Index = 1
                Changed -> emit SettingChanged(1)
            }
            ui.Row Language : ui.RowSlot {
                Label = "Language"
                Value = "English"
                Index = 2
                Changed -> emit SettingChanged(2)
            }
        }

        if Unsaved {
            Text UnsavedHint : ui.Caption {
                Text = "Unsaved changes"
                Color = @ui.Muted
                HAlign = Left
            }
        }

        HorizontalBox Buttons {
            Spacing = 16
            Native.Button Apply {
                OnClicked -> emit Applied()
                Text { Text = "Apply" }
            }
            Native.Button Back {
                OnClicked -> emit Closed()
                Text { Text = "Back" }
            }
        }
    }
}
```

逐段看它依赖的规则：

- `use … as ui` 让库的名字和界面自己的名字分开：`: ui.Caption`、`@ui.Panel`，行组件则是 `ui.Row`——库自己那行 `use "SettingsRow.dui" as Row` 随库一起进了命名空间。
- `ui.Row Audio { … }` 是行组件的一个实例：`Label`、`Value` 和 `Index` 设的是它的 props——这些是初始值；之后代码通过行的变量 `Audio` 去改 `Value`；`Changed ->` 路由的是行从箭头按钮里用 `emit` 抛出的事件。这里界面又依次抛出自己的一个事件：`emit` 的参数是界面类上的表达式（事件自身的参数不在其中），所以每一行传的是自己的编号。若界面的类有对应的函数，则改为写 `Changed -> HandleVolumeChanged`（类必须有那个函数，否则编译会报 DUI6004）。
- 每一行上的 `: ui.RowSlot`，以及行的根节点上的 `RowBox`：垂直盒子给子节点的宽度是子节点自己测量的宽度，所以该实例要求填满整列并高 48，而行的根节点拉伸覆盖该实例。没有它们，行的宽度就与它的根节点一样，也就是 0，每个标签都会一个字母一行地竖排。
- `VerticalBox Page`、`HorizontalBox Buttons` 和行的 `+ HorizontalBox` 都是盒子；它们的 `Spacing` 和 `Padding` 属于盒子。`Widget Rows : ui.Column` 从样式拿到它的垂直盒子和填充。
- `if Unsaved` 只在 prop 为真时显示提示；它翻转时不会创建或销毁任何东西。条件可以是类上的任意绑定——当类有该函数时为 `if HasUnsavedChanges()`。
- 按钮本身不绘制标签：每个按钮里的 `Text` 就是它的标签。
- 对类的数组使用 `for` 会为每一项生成一个节点；参见 [`if` 和 `for`](control-flow.md)。
