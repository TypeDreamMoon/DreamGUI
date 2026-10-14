---
title: 布局
description: 布局引擎怎么工作：测量是 const 的、与布置分开，布置结果是一次写入的不可变片段，期望尺寸在一轮里记忆，失效带着原因；UMG 形状的面板、面板槽，被删除的 Lex 布局族，以及填充槽里的文本需要 WrapTextAt 的那个坑。
---

一个控件自己的 `AnchorData` 决定它的矩形 —— 直到它的父级带着一个布局容器。带着布局容器的控件就是**面板**：
面板测量孩子、决定它们的矩形，再把结果写回孩子的 `AnchoredPosition` 和 `SizeDelta`。面板在这里不是一个单独的类，
而是挂在控件上的子对象（`LayoutContainer`），所以一个控件可以按一种方式排、按另一种方式画。孩子上的 `bIgnoreLayout`
让它不受父级布局容器管辖。

<a id="沿着-blink-和-yoga-的思路重写"></a>

## 沿着 Blink 和 Yoga 的思路重写

布局按 Blink 和 Yoga 的路子重写过，四条原则：

1. **测量是 `const` 的，和布置分开。** 面板的 `MeasureLayout(WidthSpec, HeightSpec)` 是 `const` 方法，只回答"在这个约束下想要多大"，
   不写任何东西。约束是 `FDreamMeasureSpec`，借自 Android 的 MeasureSpec 和 Yoga 的 measure mode：`Undefined`（没有约束，报自然尺寸）、
   `AtMost`（报自然尺寸，但不超过给定值）、`Exactly`（不管内容，就是这个值）。约束从唯一有真实数字的地方 —— 面板的布置，
   它知道父级给了它多大的矩形 —— 向下流；每个面板用 `ForChild` 减掉自己要花掉的空间（内边距、间距、兄弟占的固定长度）再往下传。
2. **布置产出一个不可变的片段，一次提交。** 面板的 `ArrangeChildren` 只往 `FDreamFragment`（自己的尺寸 + 每个孩子一个矩形）里记录，
   不写任何孩子的几何；基类随后用 `CommitFragment` 一次写到控件上。以前布局结果除了控件本身没有地方放，所以"布置"就是"写"，
   每一次写都在引起它的那一轮还没跑完时重新触发失效。Blink 撞过同一堵墙，也是这样解决的 —— 布局算法返回不可变的片段，
   而不是原地改布局树。
3. **期望尺寸在一轮里记忆。** 测量一个面板要问它的容器，容器要测量每个孩子，孩子又可能是面板，一个问题的代价是 O(孩子数^深度)。
   一轮布置或测量里，期望尺寸被记住（`FDesiredSizeMemoScope`），最外层作用域关闭时整体忘掉。这只因为布置不再写入才是可靠的。
4. **失效带着原因。** `EDreamLayoutInvalidation` 有两个值：`Measure`（这个控件的期望尺寸可能变了，所有测量它的祖先都要重跑）和
   `Arrange`（只是它在父级里的位置变了 —— 面板按期望尺寸而不是位置测量孩子，所以父级以上的答案不会变，父级重排一下就停）。
   于是移动一个控件不再重新测量整条祖先链。

还有一条贯穿始终的规则：**布局的输出永远不回流到测量。** 面板问孩子想要多大时（`UDreamPanelLayoutBase::GetDesiredSize`），
依次看尺寸适配（fitter）、容器的首选尺寸、视觉的固有尺寸、内容孩子、写出来的矩形，绝不读一个面板写进去的矩形。

<a id="在代码里触发布局"></a>

### 在代码里触发布局

| 调用 | 做什么 |
| --- | --- |
| `GetDesiredSize` | 这个控件**想要**多大，由父级面板来回答；没有测量它的父级时报它现有的尺寸 |
| `ForceLayoutPrepass` | 立即布局这棵树，而不是等下一轮 —— 刚改了内容、同一帧就要拿到结果时用 |
| `InvalidateLayoutAndVolatility` / `UDreamUIBPLibrary::InvalidateLayout` | 请下一帧对它做一轮布局（"volatility"那一半在这里没有对应物） |
| `UDreamPanelSlot::NotifySlotChanged(Reason)` | 槽改了；对齐、尺寸规则、填充权重、z 序传 `Arrange`，内边距、网格坐标、`bAutoSize` 保持默认的 `Measure` |

<a id="自身布局四个变体"></a>

## 自身布局：四个变体

面板管**孩子**；控件自己「想要多大」由**自身布局**（`LayoutSelf`）回答 —— 它是个子对象，和面板互不相干：

| 类 | 说明 |
| --- | --- |
| `UDreamLayoutSelf` | 默认的自身布局 |
| `UDreamLayoutSelfAspectRatio` | 按宽高比约束自己 |
| `UDreamLayoutSelfAuthoredSurface` | 用自绘表面 |
| `UDreamLayoutSelfSpacer` | 占位、把剩余空间吃掉 |

<a id="面板"></a>

## 面板

面板都是 `UDreamPanelLayoutBase` 的子类，名字和行为对照 UMG。`.dui` 里容器名本身就是节点类型
（`VerticalBox Column { Spacing = 29 }` 等于 `Widget Column { + VerticalBox { Spacing = 29 } }`）。

| `.dui` 类型 | 类 | 主要属性 | 说明 |
| --- | --- | --- | --- |
| `HorizontalBox` / `VerticalBox` / `StackBox` | [`UDreamLayoutContainerHorizontalBox`](../Reference/DreamLayoutContainerHorizontalBox.md) / [`…VerticalBox`](../Reference/DreamLayoutContainerVerticalBox.md) / [`…StackBox`](../Reference/DreamLayoutContainerStackBox.md) | `Padding`、`Spacing`、`Orientation`（StackBox） | 横竖两个盒子都是 StackBox 的子类 |
| `Overlay` | [`UDreamLayoutContainerOverlay`](../Reference/DreamLayoutContainerOverlay.md) | `Padding` | 按声明顺序叠放，先声明的在后面 |
| `CanvasPanel` | [`UDreamLayoutContainerCanvasPanel`](../Reference/DreamLayoutContainerCanvasPanel.md) | `bSortChildrenByZOrder` | 孩子用自己的锚点定位；不做从右到左镜像（Slate 的 `SConstraintCanvas` 也不做） |
| `GridPanel` | [`UDreamLayoutContainerGridPanel`](../Reference/DreamLayoutContainerGridPanel.md) | `Padding`、`Spacing`、`ColumnFill`、`RowFill` | 孩子由槽的 `Row`、`Column`、`RowSpan`、`ColumnSpan` 定位 |
| `UniformGridPanel` | [`UDreamLayoutContainerUniformGridPanel`](../Reference/DreamLayoutContainerUniformGridPanel.md) | `SlotPadding`、`MinDesiredSlotWidth`、`MinDesiredSlotHeight` | 等大格子 |
| `WrapBox` | [`UDreamLayoutContainerWrapBox`](../Reference/DreamLayoutContainerWrapBox.md) | `WrapSize`、`bExplicitWrapSize`、`Orientation`、`HorizontalAlignment` | 槽上有 WrapBox 专用的三项，见下 |
| `ScrollBox` | [`UDreamLayoutContainerScrollBox`](../Reference/DreamLayoutContainerScrollBox.md) | `ScrollOffset`、`WheelScrollMultiplier`、`bFrontPadScrolling`、`bBackPadScrolling`、`ScrollWhenFocusChanges` | 滚动轴上不向父级报尺寸 —— 内容的长度绝不能传到父级 |
| `SizeBox` | [`UDreamLayoutContainerSizeBox`](../Reference/DreamLayoutContainerSizeBox.md) | `WidthOverride` / `HeightOverride`（配 `bOverride*`）、`MinDesiredSize`、`MaxDesiredSize`、`MinAspectRatio`、`MaxAspectRatio` | |
| `ScaleBox` | [`UDreamLayoutContainerScaleBox`](../Reference/DreamLayoutContainerScaleBox.md) | `Stretch`、`UserSpecifiedScale`、`StretchDirection`（`Both`、`DownOnly`、`UpOnly`） | 唯一会缩放它所排内容的面板 |
| `SafeZone` | [`UDreamLayoutContainerSafeZone`](../Reference/DreamLayoutContainerSafeZone.md) | `bUsePlatformSafeZone`、`bPadLeft` 等四项、`SafePadding` | |
| `WidgetSwitcher` | [`UDreamLayoutContainerWidgetSwitcher`](../Reference/DreamLayoutContainerWidgetSwitcher.md) | `ActiveWidgetIndex` | |
| `Border` | [`UDreamLayoutContainerBorder`](../Reference/DreamLayoutContainerBorder.md) | `Padding`、`HorizontalAlignment`、`VerticalAlignment`、`BrushColor`、`ContentColorAndOpacity` | 和 UMG 一样，对齐属于 Border 而不是槽 |
| `MenuAnchor` | [`UDreamLayoutContainerMenuAnchor`](../Reference/DreamLayoutContainerMenuAnchor.md) | `Placement`、`MenuClass`、`bIsOpen`、`bFitInWindow` | 打开的菜单走弹出层，见[输入](input.md) |

一个面板只有一个布局容器：容器类型的节点上再写 `+ HorizontalBox` 是 DUI5022。有些容器需要控件上带着特定的行为才能和它镜像的
UMG 面板一致（比如 SizeBox 需要 ContentWidget），给控件指定容器时缺的会自动加上。容器还可以带布局动画：`bUseAnimation` 加上
`AnimationHandler`，让孩子换位置时**补间**过去而不是跳过去。三个类同住 `Core/Components/DreamLayout.h`：
`UDreamLayoutAnimation`（`UObject` 基类，`GetLayoutContainer()` 加蓝图事件 `OnApplyLayoutResults`）、
`UDreamLayoutAnimation_CommonTween`（通用补间，`Duration` 默认 **0.3**、可换成曲线）、
`UDreamLayoutAnimation_SlideIn`（滑入）。
⚠️ 它们是 **`UObject` 不是 `UComponent`** —— 在「加到控件上的组件」里找不到。

```dui
VerticalBox Settings {
    Spacing = 12
    Padding = (24, 24, 24, 24)

    Text Title {
        Text = "设置"
        FontSize = 28
        @slot HorizontalAlignment = Center
    }

    HorizontalBox VolumeRow {
        Spacing = 16

        Text VolumeLabel {
            Text = "音量"
            @slot VerticalAlignment = Center
        }
        // 横向填满这一行剩下的宽度
        Native.Slider Volume {
            @fill
            @slot VerticalAlignment = Center
        }
    }

    Text Hint {
        Text = "改动立即生效。"
        @slot { SizeRule = Auto  Padding = (0, 8, 0, 0) }
    }
}
```

<a id="槽"></a>

## 槽

UMG 每种面板有自己的槽类；这里只有一个 `UDreamPanelSlot`，归**孩子**所有，所以没有什么要转型的 ——
`AddChild` 直接返回它，`UDreamUILayoutLibrary::SlotAsPanelSlot` 一个节点顶替 UMG 整个 `SlotAs*` 家族。

| 属性 | 说明 |
| --- | --- |
| `Padding` | 孩子四周的外边距 |
| `HorizontalAlignment` / `VerticalAlignment` | `Fill`、`Left` / `Top`、`Center`、`Right` / `Bottom`；槽自己的默认值是 `Fill`。Border 的对齐在 Border 自己身上 |
| `SizeRule` / `FillWeight` | 盒子里：`Auto` 按期望尺寸，`Fill` 按权重分剩下的空间；`.dui` 里 `@fill` 和 `@fill 2` 是简写 |
| `Row` / `Column` / `RowSpan` / `ColumnSpan` | 网格里的坐标与跨度 |
| `ZOrder` | 兄弟间的绘制顺序，稳定重排。Overlay 和 GridPanel 总是读它，CanvasPanel 在 `bSortChildrenByZOrder` 打开时读，别的面板严格按兄弟顺序。UMG 网格槽的 `Layer` 就是它 |
| `bAutoSize` | 按内容定尺寸，UMG 画布槽的同名项 |
| `MinDesiredSize` / `MaxDesiredSize` | 给孩子测出来的尺寸按轴加上下限，`0` 表示没意见 |
| `bFillEmptySpace` / `FillSpanWhenLessThan` / `bForceNewLine` | 只有 WrapBox 读：行末的孩子吃掉剩余空间；换行宽度小于此值时独占一行；从这里强制换行 |
| `Nudge` | 在面板决定的位置上再加一个固定偏移，单位是面板内容空间（x 向右、y 向下，和 `Padding` 同向） |

槽的默认对齐是 `Fill`，每个 `.dui` 文件和每个自己搭树的控件都按这个写；只有在设计器里**手动**往面板里放孩子时，
才会按面板类型写入 UMG 那样的默认值（Overlay 把东西按自身尺寸叠在角上，盒子给每个孩子一条，ScaleBox 居中）。
UMG 画布槽那一族 —— `SetPosition`、`SetSize`、`SetOffsets`、`SetAnchors`、`SetAlignment`、`SetLayout` —— 读写的就是孩子自己的
`AnchorData`；`GetAlignment` 就是这里的 `Pivot`。

<a id="从右到左"></a>

## 从右到左

`FlowDirectionPreference`（`Inherit`、`Culture`、`LeftToRight`、`RightToLeft`）决定一个控件的布局往哪边流。默认 `Inherit`
从最近一个有主张的祖先取答案，所以在根上设一次 `Culture` 就能镜像整个界面。镜像只在一个地方做：面板把排好的矩形绕自己的宽度反射一次。
这一次反射就是镜像横向布局的全部含义 —— 横向排列的顺序反过来，`Left` 和 `Right` 对调，面板和槽的内边距左右对调；纵向什么都不动，
测量也不受影响。CanvasPanel 的孩子用自己的锚点定位，不经过这一步，所以不镜像。

<a id="被删除的-lex-布局族"></a>

## 被删除的 Lex 布局族

上游的 FlexBox + Grid 布局族 —— `ULexLayoutContainerFlexBox`、`ULexLayoutContainerGrid`、`ULexLayoutSelfFlexBox`、
`ULexLayoutSelfGrid`，以及 `ELexUILayoutMode` 这个开关 —— 在这里已经删除，只剩 UMG 形状的面板。它们没有重定向，
因为没有可以指向的东西：用它们排的树要手工重新排一遍。上游 2026-08-08 对 `ULexLayoutContainerFlexBox` 无限循环的修复在这里没有意义，
那个类已经不存在了。见[迁移](../guides/migration.md)。

<a id="填充槽里会换行的文本"></a>

## 填充槽里会换行的文本

一个按盒子换行的文本（`OverflowType` 为 `VerticalOverflow`，或开了 `bAutoWrapText`），高度取决于它拿到的宽度。
面板总是先问孩子想要多大，**再**给它宽度，所以结果取决于面板问的时候给没给宽度：

- 给了 —— 竖直盒子测量孩子时给出自己的宽度 —— 文本回答它在那个宽度下的高度。一段放在竖直盒子 Fill 槽里的文字，
  得到的是槽宽下的高度，而不是控件碰巧有的宽度。
- 没给 —— 横向盒子沿自己的轴测量时 —— 文本回答它排成一行时的宽度；等真正被排得比这窄时它才换行。

所以一个放在横向排列里、需要换行的文本，给它一个 `WrapTextAt`：有了它，文本就按这个宽度断行、按这个宽度测量，不管面板问的时候给了什么。
插件自带的示例 `HelloDreamGUI.dui` 也给它的每段文字写了 `WrapTextAt`，让它们按设计好的宽度断行。

```dui
VerticalBox Column {
    Padding = (24, 28, 24, 28)

    // 横向盒子沿自己的轴测量时不给宽度：给文本一个断行宽度
    HorizontalBox Row {
        Image Icon { Brush.ImageSize = (32, 32) }
        Text Description {
            Text = "一段会换行的说明文字。"
            WrapTextAt = 320
            @fill
        }
    }
}
```

`WrapTextAt` 还有一个独立的用处：盒子同时也是文本**对齐**的地方，一个比盒子窄的断行宽度能得到一栏窄窄的文字、仍然在整个控件上居中。
文本的其余规则见[文本](text.md)。
