---
title: "布局"
description: "布局引擎如何工作——测量是 const 的并与布置分离，布置产生一块不可变片段并一次写入提交，期望尺寸在一轮遍历中会被记忆，失效带有原因——以及 UMG 形状的面板、面板槽、已删除的 Lex 布局族，以及为什么填充槽中的换行文本需要 WrapTextAt。"
---

一个控件自身的 `AnchorData` 决定它的矩形——直到它的父级带有布局容器。带有布局容器的控件就是一个
**面板**：它测量子级、决定它们的矩形，并把结果写回它们的 `AnchoredPosition` 和
`SizeDelta`。这里的面板不是单独的类，而是控件的子对象（`LayoutContainer`），因此一个控件可以按一种方式
布局、按另一种方式绘制。子级上的 `bIgnoreLayout` 会使其脱离父级的布局。

<a id="rebuilt-along-the-lines-of-blink-and-yoga"></a>

## 按 Blink 和 Yoga 的思路重建

布局按照 Blink 和 Yoga 的方式重建，基于四条原则：

1. **测量是 `const`，并与布置分离。** 面板的 `MeasureLayout(WidthSpec, HeightSpec)` 是一个 `const`
   方法：它回答“在这个约束内有多大”，不写入任何内容。约束是一个 `FDreamMeasureSpec`，其含义与 Android 的 MeasureSpec 和 Yoga 的 measure mode 一致：`Undefined`（无约束，报告自然尺寸）、
   `AtMost`（自然尺寸，但不大于给定值）和 `Exactly`（给定值，无论内容如何）。约束
   从真实数值唯一存在的地方——面板的布置，它知道父级给它的矩形——向下流动，每个面板
   都会按自己即将花费的空间（`ForChild`：内边距、间距、兄弟项的固定范围）缩小收到的约束，
   然后再传递下去。
2. **布置产生一块不可变片段，一次提交。** 面板的 `ArrangeChildren` 记录到一个
   `FDreamFragment` 中——它自己的尺寸和每个子级一个矩形——不写入任何子级几何信息；基类随后
   在一次遍历中把它提交到控件上（`CommitFragment`）。过去布局结果除了控件本身无处存放，
   所以布置*就是*写入，而每次写入都会在引发它的那一轮遍历仍在运行时重新进入失效。
   Blink 撞上了同一堵墙，并以同样的方式解决：它的布局算法返回不可变片段，而不是
   原地修改布局树。
3. **期望尺寸会在一轮遍历中被记忆。** 测量一个面板要询问其容器，容器测量每个子级，每个子级
   又可能是面板——一个问题耗费 O(子级^深度)。在一次布置或测量内，期望尺寸会被记住
   （`FDesiredSizeMemoScope`），并在最外层作用域关闭时全部遗忘。这之所以可靠，只是因为布置
   不再写入。
4. **失效带有原因。** `EDreamLayoutInvalidation` 有两个值：`Measure`（这个控件的期望尺寸可能
   变了，因此测量它的每个祖先都会重新运行）和 `Arrange`（只有它在父级中的位置变了——
   面板按期望尺寸测量子级，从不按位置测量，因此父级以上的任何内容都不会产生不同的
   结果；父级重新布置，遍历到此为止）。移动一个控件不再重新测量整条祖先链。

有一条规则贯穿始终：**布局输出永远不会回馈到测量中。** 当面板询问一个子级
想要多大时（`UDreamPanelLayoutBase::GetDesiredSize`），它依次查看适配器、容器的首选尺寸、
视觉固有尺寸、内容子级和编写的矩形——从不查看面板遍历写入的矩形。

<a id="driving-layout-from-code"></a>

### 从代码驱动布局

| 调用 | 作用 |
| --- | --- |
| `GetDesiredSize` | 这个控件**想要**多大，由其父级面板回答；没有测量父级的控件报告它现有的尺寸 |
| `ForceLayoutPrepass` | 立即布局该树，而不是等到下一次遍历——当你刚改了内容并需要在同一帧获得尺寸时 |
| `InvalidateLayoutAndVolatility` / `UDreamUIBPLibrary::InvalidateLayout` | 请求下一帧进行布局遍历（“volatility”那一半在这里没有对应物） |
| `UDreamPanelSlot::NotifySlotChanged(Reason)` | 槽发生了变化；对齐、尺寸规则、填充权重和 z 序传递 `Arrange`，而内边距、网格坐标和 `bAutoSize` 保持默认的 `Measure` |

<a id="self-layout-four-variants"></a>

## 自身布局：四种变体

面板负责它的**子级**；一个控件自己希望多大由其**自身布局**
（`LayoutSelf`）回答——这是一个与面板无关的子对象：

| 类 | 说明 |
| --- | --- |
| `UDreamLayoutSelf` | 默认的自身布局 |
| `UDreamLayoutSelfAspectRatio` | 将自身约束为某个宽高比 |
| `UDreamLayoutSelfAuthoredSurface` | 使用编写的表面 |
| `UDreamLayoutSelfSpacer` | 占用剩余空间的占位符 |

<a id="the-panels"></a>

## 面板

每个面板都是 `UDreamPanelLayoutBase` 的子类，命名和行为都仿照其 UMG 对应物。在 `.dui` 中，
容器名称本身就是一个节点类型（`VerticalBox Column { Spacing = 29 }` 就是 `Widget Column { + VerticalBox { Spacing = 29 } }`）。

| `.dui` 类型 | 类 | 主要属性 | 说明 |
| --- | --- | --- | --- |
| `HorizontalBox` / `VerticalBox` / `StackBox` | [`UDreamLayoutContainerHorizontalBox`](../Reference/DreamLayoutContainerHorizontalBox.md) / [`…VerticalBox`](../Reference/DreamLayoutContainerVerticalBox.md) / [`…StackBox`](../Reference/DreamLayoutContainerStackBox.md) | `Padding`、`Spacing`、`Orientation`（StackBox） | 两个盒子都是 StackBox 的子类 |
| `Overlay` | [`UDreamLayoutContainerOverlay`](../Reference/DreamLayoutContainerOverlay.md) | `Padding` | 按声明顺序堆叠子级；第一个在最下面 |
| `CanvasPanel` | [`UDreamLayoutContainerCanvasPanel`](../Reference/DreamLayoutContainerCanvasPanel.md) | `bSortChildrenByZOrder` | 子级按自身锚点定位；不为从右到左做镜像（Slate 的 `SConstraintCanvas` 也不做） |
| `GridPanel` | [`UDreamLayoutContainerGridPanel`](../Reference/DreamLayoutContainerGridPanel.md) | `Padding`、`Spacing`、`ColumnFill`、`RowFill` | 子级由槽的 `Row`、`Column`、`RowSpan`、`ColumnSpan` 放置 |
| `UniformGridPanel` | [`UDreamLayoutContainerUniformGridPanel`](../Reference/DreamLayoutContainerUniformGridPanel.md) | `SlotPadding`、`MinDesiredSlotWidth`、`MinDesiredSlotHeight` | 等大格子 |
| `WrapBox` | [`UDreamLayoutContainerWrapBox`](../Reference/DreamLayoutContainerWrapBox.md) | `WrapSize`、`bExplicitWrapSize`、`Orientation`、`HorizontalAlignment` | 它自己有三个槽属性，见下 |
| `ScrollBox` | [`UDreamLayoutContainerScrollBox`](../Reference/DreamLayoutContainerScrollBox.md) | `ScrollOffset`、`WheelScrollMultiplier`、`bFrontPadScrolling`、`bBackPadScrolling`、`ScrollWhenFocusChanges` | 沿其滚动轴不参与测量——内容的范围绝不能到达父级 |
| `SizeBox` | [`UDreamLayoutContainerSizeBox`](../Reference/DreamLayoutContainerSizeBox.md) | `WidthOverride` / `HeightOverride`（带有 `bOverride*`）、`MinDesiredSize`、`MaxDesiredSize`、`MinAspectRatio`、`MaxAspectRatio` | |
| `ScaleBox` | [`UDreamLayoutContainerScaleBox`](../Reference/DreamLayoutContainerScaleBox.md) | `Stretch`、`UserSpecifiedScale`、`StretchDirection`（`Both`、`DownOnly`、`UpOnly`） | 唯一会缩放其所布置内容的面板 |
| `SafeZone` | [`UDreamLayoutContainerSafeZone`](../Reference/DreamLayoutContainerSafeZone.md) | `bUsePlatformSafeZone`、四个 `bPad*`、`SafePadding` | |
| `WidgetSwitcher` | [`UDreamLayoutContainerWidgetSwitcher`](../Reference/DreamLayoutContainerWidgetSwitcher.md) | `ActiveWidgetIndex` | |
| `Border` | [`UDreamLayoutContainerBorder`](../Reference/DreamLayoutContainerBorder.md) | `Padding`、`HorizontalAlignment`、`VerticalAlignment`、`BrushColor`、`ContentColorAndOpacity` | 与 UMG 一样，对齐属于边框，而不是槽 |
| `MenuAnchor` | [`UDreamLayoutContainerMenuAnchor`](../Reference/DreamLayoutContainerMenuAnchor.md) | `Placement`、`MenuClass`、`bIsOpen`、`bFitInWindow` | 打开的菜单会经过弹出层；见[输入](input.md) |

一个控件只有一个布局容器：容器类型节点上的 `+ HorizontalBox` 就是 DUI5022。有些容器需要控件上带有某种行为，才能与它们所镜像的 UMG 面板匹配（例如 SizeBox 需要 ContentWidget），缺少的那些会在分配容器时被添加。容器还可以为其布局添加动画：`bUseAnimation` 配合 `AnimationHandler` 会让子项**补间**到新位置，而不是跳变。这三个类一起位于 `Core/Components/DreamLayout.h` 中：`UDreamLayoutAnimation`（`UObject` 基类，带有 `GetLayoutContainer()` 和蓝图事件 `OnApplyLayoutResults`）、`UDreamLayoutAnimation_CommonTween`（通用补间；`Duration` 默认为 **0.3**，可以换成曲线）以及 `UDreamLayoutAnimation_SlideIn`（滑入）。
⚠️ 它们是 **`UObject` 而非组件**——你不会在添加到控件的组件中找到它们。

```dui
VerticalBox Settings {
    Spacing = 12
    Padding = (24, 24, 24, 24)

    Text Title {
        Text = "Settings"
        FontSize = 28
        @slot HorizontalAlignment = Center
    }

    HorizontalBox VolumeRow {
        Spacing = 16

        Text VolumeLabel {
            Text = "Volume"
            @slot VerticalAlignment = Center
        }
        // Takes whatever width the row has left
        Native.Slider Volume {
            @fill
            @slot VerticalAlignment = Center
        }
    }

    Text Hint {
        Text = "Changes apply at once."
        @slot { SizeRule = Auto  Padding = (0, 8, 0, 0) }
    }
}
```

<a id="slots"></a>

## 槽

UMG 为每种面板有一个槽类；这里只有一个 `UDreamPanelSlot`，由**子项**拥有，所以没有什么需要转型：
`AddChild` 返回它，而 `UDreamUILayoutLibrary::SlotAsPanelSlot` 就是 UMG 整个 `SlotAs*` 家族在一个节点中的对应物。

| 属性 | 说明 |
| --- | --- |
| `Padding` | 子项四周的外边距 |
| `HorizontalAlignment` / `VerticalAlignment` | `Fill`、`Left` / `Top`、`Center`、`Right` / `Bottom`；槽自身的默认值是 `Fill`。Border 的对齐在 Border 上 |
| `SizeRule` / `FillWeight` | 在盒子中：`Auto` 取期望尺寸，`Fill` 按权重分配剩余空间；`@fill` 和 `@fill 2` 是 `.dui` 的简写 |
| `Row` / `Column` / `RowSpan` / `ColumnSpan` | 网格中的坐标和跨度 |
| `ZOrder` | 兄弟之间的绘制顺序，作为稳定重排。Overlay 和 GridPanel 总是读取它，CanvasPanel 在 `bSortChildrenByZOrder` 开启时读取；其他所有面板严格按兄弟顺序排列。UMG 网格槽的 `Layer` 就是它 |
| `bAutoSize` | 按内容确定尺寸，UMG 画布槽同名属性 |
| `MinDesiredSize` / `MaxDesiredSize` | 对子项测量结果按每个轴设置的下限和上限；零表示没有意见 |
| `bFillEmptySpace` / `FillSpanWhenLessThan` / `bForceNewLine` | 仅 WrapBox：行上的最后一个子项占据剩余空间；当换行宽度低于此值时独占一行；从此处开始新的一行 |
| `Nudge` | 在面板放置子项的位置上添加的固定偏移，位于面板的内容空间（x 向右，y 向下，与 `Padding` 方向相同） |

槽默认是 `Fill`，每个 `.dui` 文件和每个自行构建树的控件都是针对它编写的。只有当子项是在设计器中**手动添加**时，才会把面板特定的默认值写入槽中，就像 UMG 的每种槽类所做的那样（overlay 将内容按自身尺寸堆叠在它的角上，box 给每个子项一条带，scale box 居中）。UMG 的画布槽家族——`SetPosition`、`SetSize`、`SetOffsets`、`SetAnchors`、`SetAlignment`、`SetLayout`——读写子项自身的 `AnchorData`；`GetAlignment` 就是本插件的 `Pivot`。

<a id="right-to-left"></a>

## 从右到左

`FlowDirectionPreference`（`Inherit`、`Culture`、`LeftToRight`、`RightToLeft`）表示控件的布局朝哪个方向流动。
`Inherit` 是默认值，它从最近一个声明了方向的祖先处取得答案，所以在根节点上设置一次 `Culture` 就会镜像整个屏幕。镜像只在一个地方完成：面板围绕自身的宽度反射已完成的矩形。这单次反射就是镜像水平布局的全部含义——它反转任何沿 x 方向排列的顺序，交换 `Left` 和 `Right` 对齐，并交换面板和槽的内边距的左右两半；垂直方向没有任何移动，测量也不受影响。画布面板的子项通过其锚点自行放置，从不经过这一步，所以画布不会被镜像。

<a id="the-lex-layout-family-is-gone"></a>

## Lex 布局家族已被删除

上游的 FlexBox + Grid 家族——`ULexLayoutContainerFlexBox`、`ULexLayoutContainerGrid`、`ULexLayoutSelfFlexBox`、
`ULexLayoutSelfGrid` 以及 `ELexUILayoutMode` 开关——已在此处删除，只留下 UMG 形状的面板。没有
重定向，因为没有什么可供它指向：由它们之一布局的树需要手动重新布局。
上游 2026-08-08 对 `ULexLayoutContainerFlexBox` 中无限循环的修复在这里没有意义，因为该类
已不存在。参见[迁移](../guides/migration.md)。

<a id="a-wrapping-text-in-a-fill-slot"></a>

## 填充槽中的换行文本

一个在其盒子处换行的文本（`OverflowType` 为 `VerticalOverflow`，或 `bAutoWrapText` 开启）的高度取决于它
获得的宽度。面板总是在给子项宽度**之前**先询问它想要多大，所以返回的结果取决于面板在询问时是否
提供了宽度：

- 提供了宽度——垂直盒子测量其子项时提供自己的宽度——文本回答它在该宽度下的高度。垂直盒子的 Fill 槽中的段落
  按槽的宽度测量，而不是按控件恰好具有的宽度。
- 未提供——水平盒子沿自身轴测量时——文本回答该段落单行的宽度，
  并在被排列得比这更窄时换行。

因此，给水平排列中的换行文本一个 `WrapTextAt`：它便会在该宽度处断行，并按该宽度测量，
无论面板提供了什么。随附示例 `HelloDreamGUI.dui` 也为其每个文本提供了 `WrapTextAt`，因此它们
按设计宽度断行。

```dui
VerticalBox Column {
    Padding = (24, 28, 24, 28)

    // A horizontal box offers no width along its own axis: give the text a width to break at
    HorizontalBox Row {
        Image Icon { Brush.ImageSize = (32, 32) }
        Text Description {
            Text = "A description long enough to wrap."
            WrapTextAt = 320
            @fill
        }
    }
}
```

`WrapTextAt` 也有其自身的用途：盒子也是文本**对齐**的地方，所以比盒子更窄的换行宽度
会得到一列狭窄的文本，却仍然在整个控件上居中。其余文本规则见
[文本](text.md)。
