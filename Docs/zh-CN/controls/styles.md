---
title: 样式与样式表
description: 每个控件的 F…Style 样式结构体、StyleSource 决定外观从哪来、项目样式表 UDreamUIStyleSheet 和它的命名变体，以及控件的面刷子、状态面和图片刷子。
---

控件的树是代码搭的，没人能打开改，所以外观上的每个决定都必须是一个旋钮，否则就只能 fork 控件。
这些旋钮集中在每个控件的**样式结构体**里——Slate 的 `FButtonStyle` 那种形状，存在的理由也一样。

分工是固定的：**外观在样式里，行为在控件上**。按钮的颜色、圆角、内边距是 `FDreamButtonStyle` 的字段；
它什么时候算点击（`ClickMethod`）、接受哪些鼠标键（`AcceptedMouseButtons`）是 `UDreamButton` 自己的属性。
所以有几个 UMG 属性在这里低了一层：列表条目之间的间距是外观，于是 UMG 的 `EntrySpacing` 在这里是
`Style.RowSpacing`（见[从 UMG 过来](umg-parity.md)）。

**提示：**

这里说的是控件的样式结构体。`.dui` 文件里的 `style Name { … }` 块是另一回事——一组按名字套到节点上的
  属性行，见[样式与资源](../language/styles-and-resources.md)。

<a id="每个控件的样式结构体"></a>

## 每个控件的样式结构体

每个控件有一个 `Style` 属性，类型是它这一族的结构体；项目样式表里每一族有一个默认值和一张命名变体表。

| 控件 | 样式结构体 | 样式表里的字段 |
| --- | --- | --- |
| `UDreamButton` | [`FDreamButtonStyle`](../Reference/DreamButtonStyle.md) | `Button`、`ButtonVariants` |
| `UDreamToggle` | [`FDreamToggleStyle`](../Reference/DreamToggleStyle.md) | `Toggle`、`ToggleVariants` |
| `UDreamRadioButton` | [`FDreamRadioButtonStyle`](../Reference/DreamRadioButtonStyle.md) | `RadioButton`、`RadioButtonVariants` |
| `UDreamInputKeySelector` | [`FDreamInputKeySelectorStyle`](../Reference/DreamInputKeySelectorStyle.md) | `InputKeySelector`、`InputKeySelectorVariants` |
| `UDreamSlider` | [`FDreamSliderStyle`](../Reference/DreamSliderStyle.md) | `Slider`、`SliderVariants` |
| `UDreamSpinBox` | [`FDreamSpinBoxStyle`](../Reference/DreamSpinBoxStyle.md) | `SpinBox`、`SpinBoxVariants` |
| `UDreamProgressBar` | [`FDreamProgressBarStyle`](../Reference/DreamProgressBarStyle.md) | `ProgressBar`、`ProgressBarVariants` |
| `UDreamThrobber` | [`FDreamThrobberStyle`](../Reference/DreamThrobberStyle.md) | `Throbber`、`ThrobberVariants` |
| `UDreamTextInput`、`UDreamEditableText`、`UDreamMultiLineEditableText` | [`FDreamTextInputStyle`](../Reference/DreamTextInputStyle.md) | `TextInput`、`TextInputVariants` |
| `UDreamRichTextBlock` | [`FDreamRichTextStyle`](../Reference/DreamRichTextStyle.md) | `RichText`、`RichTextVariants` |
| `UDreamListView` | [`FDreamListStyle`](../Reference/DreamListStyle.md) | `List`、`ListVariants` |
| `UDreamTileView` | [`FDreamTileViewStyle`](../Reference/DreamTileViewStyle.md) | `TileView`、`TileViewVariants` |
| `UDreamTreeView` | [`FDreamTreeViewStyle`](../Reference/DreamTreeViewStyle.md) | `TreeView`、`TreeViewVariants` |
| `UDreamScrollBox` | [`FDreamScrollBoxStyle`](../Reference/DreamScrollBoxStyle.md) | `ScrollBox`、`ScrollBoxVariants` |
| `UDreamScrollBar` | [`FDreamScrollBarStyle`](../Reference/DreamScrollBarStyle.md) | `ScrollBar`、`ScrollBarVariants` |
| `UDreamBorder` | [`FDreamBorderStyle`](../Reference/DreamBorderStyle.md) | `Border`、`BorderVariants` |
| `UDreamExpandableArea` | [`FDreamExpandableAreaStyle`](../Reference/DreamExpandableAreaStyle.md) | `ExpandableArea`、`ExpandableAreaVariants` |
| `UDreamTabView` | [`FDreamTabViewStyle`](../Reference/DreamTabViewStyle.md) | `TabView`、`TabViewVariants` |
| `UDreamDialog` | [`FDreamDialogStyle`](../Reference/DreamDialogStyle.md) | `Dialog`、`DialogVariants` |
| `UDreamMenuAnchor` | [`FDreamMenuAnchorStyle`](../Reference/DreamMenuAnchorStyle.md) | `MenuAnchor`、`MenuAnchorVariants` |
| `UDreamDropdown` | [`FDreamDropdownStyle`](../Reference/DreamDropdownStyle.md) | `Dropdown`、`DropdownVariants` |
| `UDreamRingMenu` | [`FDreamRingMenuStyle`](../Reference/DreamRingMenuStyle.md) | `RingMenu`、`RingMenuVariants` |

`UDreamNativeWidgetHost` 没有样式：它唯一的旋钮 `ResolutionScale` 是保真度和内存的取舍，样式表对它
没什么可说的。

几条贯穿所有样式的约定：

- **颜色是 `FColor`**，整个样式家族都是；
- **按钮没有文字样式**。按钮自己不画字，标签是宿主放进洞里的，所以 `FDreamButtonStyle` 里没有
  `LabelColor` 或 `FontSize`——一个无处可写的旋钮比没有旋钮更糟。需要由控件提供标签颜色的地方，
  由那个控件自己的样式说（比如 `FDreamDialogStyle::ButtonLabelColor`）；
- **禁用色和聚焦色是样式的一部分**。推一个聚焦色就会打开 selectable 的聚焦视觉，所以手柄导航
  落到的控件总有反应。

<a id="外观从哪里来stylesource"></a>

## 外观从哪里来：StyleSource

每个控件都有两个属性决定用哪份样式：

| 属性 | 含义 |
| --- | --- |
| `StyleSource` | `ProjectStyleSheet`（默认）：从项目样式表解析；`ProjectStyleSheetOverride`：样式表的值，再写上这个实例**勾选了的**字段；`Inline`：整个用这个实例自己的 `Style` |
| `StyleVariant` | 样式表里的命名条目（"Danger"、"Compact"）；为空表示这一族的默认值。只在从样式表解析时有意义 |

默认是样式表，因为那就是有样式表的意义：一个代码搭的控件没有树可以打开重新上色，没有项目级的答案，
每个实例都是一份手调的 fork。

**项目里没有样式表时**，实例自己的 `Style` 就是生效的外观——不管 `StyleSource` 写的是什么。这也是
`Style` 一直可以编辑、没有被 `StyleSource` 置灰的原因。

<a id="逐字段的覆盖位"></a>

### 逐字段的覆盖位

每个样式结构体的每个字段旁边都有一个 `bOverride_<字段名>` 位，和引擎的 `FPostProcessSettings` 一个习惯：
细节面板上是值旁边的一个勾，没勾的值显示为灰色（但仍然显示，那是"勾上的话会推什么"）。合并只认这些位：

- **变体**：这一族的默认值，再写上变体勾选了的字段。一个字段不勾，就是"跟默认值走"——没有 Parent 指针、
  也没有环要检查的继承；
- **`ProjectStyleSheetOverride`**：样式表解析出的样式，再写上这个实例勾选了的字段。

**警告：**

所有位**默认都是勾上的**，这样既有的东西一个字节都不变：每个位都勾着的变体就是它一直以来那份完整的 fork。
  反过来，切到 `ProjectStyleSheetOverride` 却什么都没取消勾选，效果和 `Inline` 一样——要跟随样式表的字段，
  得把它的勾去掉。

<a id="在-dui-和运行时里"></a>

### 在 .dui 和运行时里

```dui
class /Game/UI/WBP_DeleteSave

HorizontalBox Root {
    Spacing = 16

    Native.Button Delete {
        StyleVariant = "Danger"
        Text { Text = "删除" }
        OnClicked -> HandleDelete
    }
    Native.Toggle AutoSave {
        StyleSource = Inline
        Style.TickChecked = #FF3355
    }
}
```

运行时：`SetStyle`、`SetStyleSource`、`SetStyleVariant` 都会**立刻重推**，因为一个改了来源却不重新解析的样式，
效果在别的东西碰巧触发推送之前完全不可见。如果你在原地改了一个样式结构体，自己调一次 `ApplyStyle`。

注意 `SetStyle` 写的是实例的 `Style`：样式表正在生效时写内联样式，看不出任何变化——推送照样会跑，但赢的
仍然是 `StyleSource` 说的那一份。

<a id="项目样式表"></a>

## 项目样式表

`UDreamUIStyleSheet` 是一个数据资产，项目的控件在这里约定怎么长。每一族一个默认值加一张变体表，这就是
一个控件需要说的全部：哪一族，可选再加一个名字。

<a id="建一个样式表"></a>

### 建一个样式表
在内容浏览器里新建一个数据资产，类选 `DreamUIStyleSheet`。

<a id="指给项目"></a>

### 指给项目
*Project Settings ▸ Plugins ▸ Dream GUI* 的 Assets 分类里，把 **Default Style Sheet** 指向它。这个设置是
`UDreamGUISettings::DefaultStyleSheet`。

<a id="在控件上选变体"></a>

### 在控件上选变体
控件默认就从样式表解析；要特殊外观的实例填 `StyleVariant`。

几件值得知道的事：

- **插件不带样式表**。没有样式表是受支持的状态，不是缺东西：结构体自己的 C++ 默认值就是内置主题。
- **`DefaultStyleSheet` 的类型是 `TSoftObjectPtr<UDataAsset>`**，因为核心模块不认识控件库的类型；
  选择器只列样式表，`UDreamUIStyleSheet::GetProjectSheet()` 负责转换。它在第一次调用时加载，返回空
  表示"用内联默认值"，调用方不把它当错误。
- **解析发生在构造时**，按名字查找，和 Slate 的样式集一样。这里没有运行时换主题，也不打算有：
  它买到的是"改项目强调色就是改一个资产"，而不是逐个屏幕去找每一个放过开关的地方。
- **拼错的变体名回落到这一族的默认值**，而不是失败——屏幕上总有东西，十次里有九次是对的。

<a id="面刷子状态面和图片刷子"></a>

## 面刷子、状态面和图片刷子

<a id="fdreamuifacebrush控件面的皮肤"></a>

### FDreamUIFaceBrush：控件面的皮肤

每个控件的面都是一个程序化圆角矩形（`UDreamRectBlock`），UMG 的手感大多就在这里。
[`FDreamUIFaceBrush`](../Reference/DreamUIFaceBrush.md) 是可选的一层皮：空的时候面就是那个圆角矩形；
设了图，面在**同一个轮廓里**画这张图——圆角、边框和 selectable 的着色都照常工作，因为皮是矩形的
主体纹理，不是另一个视觉。控件的面永远不会变成别的东西。

| 字段 | 含义 |
| --- | --- |
| `Image` | 一张纹理或一个图集精灵，拖哪个进来都行 |
| `Tint` | 乘在图上，Slate 的 tint。和样式的状态色是两条通道，两者相乘 |
| `ScaleMode` | `Stretch` / `FitIn` / `Envelop`，对应 UMG 的 Stretch / ScaleToFit / ScaleToFill |
| `DrawMode` | `Image` / `Box` / `Border`，Slate 的 DrawAs。后两个是九宫格，只对普通纹理生效，精灵总按 `Image` 画 |
| `Margin` | 九宫格边距，单位是**纹理像素**：文件里八像素宽的边，在任何尺寸的矩形上都是八像素 |
| `ImageSize` | 图自己的绘制尺寸。零表示不表态；非零时覆盖带尺寸字段的部件（开关的框和勾、单选的框和点、滑条的把手）的样式尺寸 |

<a id="fdreamuistatefaces一个面的五种画法"></a>

### FDreamUIStateFaces：一个面的五种画法

样式原本只能用颜色区分五种状态。一个项目的普通按钮和按下按钮如果是**两张不同的图**，就只能 fork 控件。
[`FDreamUIStateFaces`](../Reference/DreamUIStateFaces.md) 补上这一块，而且不引入第二个状态机——
selectable 已经决定了控件处于哪种状态，这里只决定那个决定画成什么。

| 字段 | 含义 |
| --- | --- |
| `Normal`、`Hovered`、`Pressed`、`Disabled`、`Focused` | 每种状态一个面刷子。没有图的状态回落到 `Normal`，`Normal` 也没有图就回落到控件自己那一张刷子 |
| `bTintForeground` + 五个 `…Foreground` | 控件**自己的**前景部件（自己的标签或图标，从不碰宿主放进来的内容）每种状态的颜色。开关默认关 |
| `bUsePressedPadding` + `PressedPadding` | 按下时内容周围的边距，经典的"按下去"效果。静止时的边距是控件自己的 `ContentPadding` |
| `HoveredSound`、`PressedSound`、`ClickedSound` | 三个出声的时刻，作为样式决定：一次点击应该和这个 UI 里别的点击听起来一样 |

**每个字段都是可选的**，所以它什么都不改变：什么都没写的状态面，画出来和以前逐像素相同。它是按钮、开关、
下拉框的面、列表行这些样式的**一个字段**，不是基类——USTRUCT 继承经不住覆盖位的合并。

焦点和悬停是两个问题：焦点在指针离开后仍然在，手柄上根本没有指针。所以 `Focused` 是独立的一格。

<a id="fdreamuiimagebrushimage-视觉的刷子"></a>

### FDreamUIImageBrush：Image 视觉的刷子

[`FDreamUIImageBrush`](../Reference/DreamUIImageBrush.md) 是核心模块里 `Image` 视觉（`UDreamImage::Brush`）
的刷子，`UUIToggle`、`UUIDropdown` 和 `UDreamSelectableStyle` 也用它。它不是控件的面，能画的东西更多：

| 字段 | 含义 |
| --- | --- |
| `ResourceObject` | 纹理、精灵或材质 |
| `TintColor` | 颜色 |
| `DrawAs` | `None`、`Box`、`Border`、`Image`、`Tiled`（按自身尺寸平铺）、`Filled`（只显示一部分，进度条那样）、`RoundedBox`（圆角轮廓） |
| `ImageSize`、`Margin`、`UVRegion`、`PixelsPerUnitMultiplier` | 尺寸、0 到 1 的边距、UV 区域、像素密度 |
| `FillAmount`、`FillMethod`、`FillOrigin`、`FillDirectionFlip` | `Filled` 专用：显示多少、切割形状、从哪端开始、是否反向 |
| `CornerRadius`、`CornerSegments` | `RoundedBox` 专用：每个角的半径（像素，从左上顺时针）和每段圆弧的点数 |

`RoundedBox` 是从几何上切出来的轮廓，角的平滑程度取决于段数，不自带抗锯齿。要精确、与分辨率无关、带边框
或渐变的圆角矩形，用 `RectBlock`。

<a id="旁边的两样东西"></a>

## 旁边的两样东西

- **`UDreamSelectableStyle`**：输入层的数据资产，挂在行为（`UUISelectable::Style`）上，一组可复用的状态色、
  状态刷子、动画时长和声音。selectable 上挂着它时，它压过控件推过来的颜色和声音。
- **[`FDreamTextStyle`](../Reference/DreamTextStyle.md)**：文字的描边、投影、发光，整个库共用一个结构体；
  `FDreamRichTextStyle` 直接带着它，而不是自己再定义一份。

要换掉的不是颜色而是**整棵树**时，用控件的 `Template` 属性（在 Style 分类里）：给一个里面有正确命名部件的
widget Blueprint，控件用它的树，行为和样式约定照旧。见[一个控件是怎么搭起来的](index.md#一个控件是怎么搭起来的)。
