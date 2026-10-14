---
title: 时间线
description: 文件拥有的动画——轨道、关键帧、缓动名、事件轨道、循环方式，以及交给 Sequencer 编辑的 external。
---

`timeline` 块是一个由文件拥有的动画：

```dui
class /Game/UI/WBP_Reward

timeline Pulse {
    duration = 0.6
    loop     = PingPong

    Icon.RenderScale            : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1) ease InOutQuad, 0.6 = (1, 1, 1)
    Row/Title.RenderTranslation : 0.0 = (-40, 0, 0), 0.2 = (0, 0, 0) ease OutCubic
    @0.3 -> Landed
}

timeline Celebrate external

Widget Root {
    + Overlay { }
    Image Icon
    Widget Row {
        Text Title { Text = "Reward" }
    }
}
```

它编译成根节点动画组件里的一个 `UDreamWidgetAnimation`，像别的动画一样得到一个类成员变量，也通过同样的入口播放（见[动画](../concepts/animation.md)）。
因为它归**文件**所有，每次编译都会重建它，动画编辑器打开它时是只读的：要改就改 `.dui`，或者用 `external` 把它永久交给 Sequencer。

<a id="块的设置"></a>

## 块的设置

| 设置 | 取值 | 默认 |
| --- | --- | --- |
| `duration` | 秒 | 最后一个关键帧的时间 |
| `loop` | `Once`、`Loop`、`PingPong` | `Once` |

<a id="轨道"></a>

## 轨道

每条轨道一行：一条节点 id 组成的路径（`Row/Title`，从动画所在的 widget 往下一级级写到被驱动的那个节点），它驱动的属性，然后是关键帧
`时间 = 值`，每个关键帧后面可以跟一个 `ease`。时间单位是秒。没有路径的行驱动的是动画所在的那个 widget：

```dui
timeline Grow {
    Width      : 0.0 = 200, 0.4 = 400 ease OutBack
    Icon.Color : 0.0 = #FFFFFF00, 0.4 = #FFFFFFFF
}

Widget Root {
    + Overlay { }
    Image Icon
}
```

路径就是动画绑定本来就用来解析的那条显示名路径——节点 id 就是它的显示名，所以语言不需要另一套路径模型。值的写法和别处完全一样。

**轨道能驱动的，正好是动画编辑器提供的那些**：标了 `Interp` 的属性，可以用它自己的名字（`RenderScale`、`Color`），也可以用它那一行的标签
（`Width`、`Height`——这些是 `Animatable*` 镜像属性，因为结构体没有属性轨道，锚定数据要靠它们才能打关键帧）。只有一份清单，所以一行能编译，
就是一条你在动画编辑器里看得见的轨道。

注意这里和赋值的不对称是有意的：`Width = 400` 作为赋值仍然是 [DUI4001](../diagnostics/codes.md#dui4001)，指向 `AnchorData.SizeDelta`。
赋值写的是属性，时间线驱动的是轨道。

时间线驱动的是数字、2/3/4 分量的向量和颜色；别的类型属于 `external` 时间线。

| 代码 | 情况 |
| --- | --- |
| [DUI2014](../diagnostics/codes.md#dui2014) | `timeline` 块或其中一行不合文法 |
| [DUI3016](../diagnostics/codes.md#dui3016) | 两个同名的时间线 |
| [DUI5015](../diagnostics/codes.md#dui5015) | 轨道路径找不到节点 |
| [DUI5016](../diagnostics/codes.md#dui5016) | 属性不存在于节点、它的视觉或行为上，没有标 `Interp`，或者类型不是轨道能驱动的 |
| [DUI4003](../diagnostics/codes.md#dui4003) | 某个关键帧的值读不成这个属性的值 |

<a id="缓动"></a>

## 缓动

缓动是一个**名字**，取自 `EDreamTweenEase`——整个插件只有这一份词表——而不是一组切线数字：切线是存储下来的数据，能表达切线的文本写法
要么是没人能手写的四个数，要么是来回一趟会丢东西。`ease` 写在一个关键帧上，管的是从这个关键帧到**下一个**关键帧的那一段；不写就是 `Linear`。
带缓动的段会在编译时被采样成多个关键帧。

可用的名字：

`Linear`、`InQuad`、`OutQuad`、`InOutQuad`、`InCubic`、`OutCubic`、`InOutCubic`、`InQuart`、`OutQuart`、`InOutQuart`、
`InSine`、`OutSine`、`InOutSine`、`InExpo`、`OutExpo`、`InOutExpo`、`InCirc`、`OutCirc`、`InOutCirc`、`InElastic`、`OutElastic`、
`InOutElastic`、`InBack`、`OutBack`、`InOutBack`、`InBounce`、`OutBounce`、`InOutBounce`。

枚举里还有一个 `CurveFloat`，但它指向一个曲线资产，关键帧上没有地方放资产，所以按名字拒绝。不认识的名字是
[DUI5017](../diagnostics/codes.md#dui5017)。

<a id="事件"></a>

## 事件

`@时间 -> Name` 是块的事件轨道上的一个关键帧：播放经过这个时间时，广播这个名字，经由动画组件的 `OnAnimationEvent` 发出。

```dui
timeline Intro {
    duration = 1.0
    Panel.RenderTranslation : 0.0 = (0, -60, 0), 0.5 = (0, 0, 0) ease OutCubic
    @0.5 -> Arrived
    @1.0 -> Finished
}

Widget Root {
    + Overlay { }
    Widget Panel
}
```

<a id="external"></a>

## `external`

```dui
timeline Celebrate external
timeline Outro external

Widget Root { }
```

`external` 不建任何东西。它是一条清单记录：动画住在资产里，Sequencer 可以随意编辑它，而文件仍然列着它——这样“这个类有哪些动画”
只读文件就能回答。材质参数轨道、手调的曲线，以及别的这一层表达不了的东西，按设计就留在 `external`。

编译会两头检查这份清单，都是警告：资产里有一个文件没提到的动画（它照样能用），或者文件列了一个类里不存在的动画。

把一个由文件拥有的时间线交给 Sequencer：在原来那个块的位置写 `timeline Pulse external`。上一次编译建出来的关键帧会被带到那个动画上，
从此它归动画编辑器管。

时间线不随 `use` 合并：一个时间线是这个类树上的一个对象和一个类成员变量，导入它就等于两个类拥有同一个动画。

VS Code 扩展会高亮时间线块、在大纲里列出它，但不对轨道行下判断：一行轨道要对照 widget 树和引擎的反射才能解析，单个文件的字符看不到那些，
所以关于轨道的结论都是编译器的。
