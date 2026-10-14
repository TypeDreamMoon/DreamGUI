---
title: "样式与样式表"
description: "每个控件的 F…Style 结构体、StyleSource 与外观的来源、项目样式表 UDreamUIStyleSheet 及其命名变体，以及面刷子、状态面和图片刷子。"
---

控件的树由代码构建，没人能打开它，所以每个外观决定都必须是一个旋钮，否则就只能 fork。
这些旋钮集中存在于每个控件的**样式结构体**中——Slate 的
`FButtonStyle` 那种形状，存在的理由也一样。

分工是固定的：**外观在样式里，行为在控件上**。按钮的颜色、圆角
和内边距是 `FDreamButtonStyle` 的字段；它什么时候算点击（`ClickMethod`）、接受哪些
鼠标键（`AcceptedMouseButtons`）是 `UDreamButton` 自己的属性。这就是为什么有几个
UMG 属性在这里低了一层：列表条目之间的间距是外观，于是 UMG 的 `EntrySpacing`
是 `Style.RowSpacing`（见[从 UMG 过来](umg-parity.md)）。

**提示：**

  这里说的是控件的样式结构体。`.dui` 里的 `style Name { … }` 块是另一回事——
  一组按名字套到节点上的属性行，见[样式与资源](../language/styles-and-resources.md)。

<a id="each-controls-style-struct"></a>

## 每个控件的样式结构体

每个控件有一个 `Style` 属性，类型是它这一族的结构体；项目样式表里每一族有一个默认值和
一张命名变体表。

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

`UDreamNativeWidgetHost` 没有样式：它唯一的旋钮 `ResolutionScale` 是保真度与内存的取舍，
针对一个托管的控件，样式表对它没什么可说的。

几条贯穿所有样式的约定：

- **颜色是 `FColor`**，整个样式家族都是。
- **按钮没有文字样式**。按钮自己不画字；标签是宿主放进洞里的东西，
  所以 `FDreamButtonStyle` 没有 `LabelColor` 或 `FontSize`——一个无处可写的旋钮比没有
  旋钮更糟。需要由控件提供标签的地方，由那个控件自己的样式说（`FDreamDialogStyle::ButtonLabelColor`）。
- **禁用色和聚焦色也是样式状态**。推一个聚焦色就是打开 selectable 的
  聚焦视觉，所以手柄用户的导航总会落在有反应的东西上。

<a id="where-the-look-comes-from-stylesource"></a>

## 外观从哪里来：StyleSource

每个控件上都有两个属性决定用哪份样式：

| 属性 | 含义 |
| --- | --- |
| `StyleSource` | `ProjectStyleSheet`（默认）：从项目样式表解析；`ProjectStyleSheetOverride`：样式表的值，再写上这个实例**勾选了的**字段；`Inline`：整个用这个实例自己的 `Style` |
| `StyleVariant` | 样式表里的命名条目（"Danger"、"Compact"）；为空表示这一族的默认值。只在从样式表解析时有意义 |

默认是样式表，因为那就是有样式表的意义：一个代码搭的控件没有树可以打开重新上色，没有项目级的答案，
每个实例都是一份手调的 fork。

**项目里没有样式表时**，实例自己的 `Style` 就是生效的外观——不管 `StyleSource`
写的是什么。这也是 `Style` 一直可以编辑、没有被 `StyleSource` 置灰的原因。

<a id="per-field-override-bits"></a>

### 逐字段的覆盖位

每个样式结构体的每个字段旁边都有一个 `bOverride_<Field>` 位——UE 自己的习惯，就是
`FPostProcessSettings` 用的那个：细节面板上是值旁边的一个勾，没勾的值显示为灰色（但仍然显示——那是
这个实例勾上的话会推什么）。合并只认这些位：

- **变体**：这一族的默认值，再写上变体勾选了的字段。一个字段不勾，
  就是"跟这一族的默认值走"——没有 Parent 指针、也没有环要检查的继承。
- **`ProjectStyleSheetOverride`**：样式表解析出的样式，再写上这个实例勾选了的字段。

**提示：**

  所有位**默认都是勾上的**，这样既有的东西一个字节都不变：每个位都勾着的变体就是它一直以来那份完整的
  fork。反过来，切到 `ProjectStyleSheetOverride` 却什么都没取消勾选，效果和 `Inline` 一样。
  要跟随样式表的字段，得把它的勾去掉。

<a id="in-dui-and-at-run-time"></a>

### 在 .dui 和运行时里

```dui
class /Game/UI/WBP_DeleteSave

HorizontalBox Root {
    Spacing = 16

    Native.Button Delete {
        StyleVariant = "Danger"
        Text { Text = "Delete" }
        OnClicked -> HandleDelete
    }
    Native.Toggle AutoSave {
        StyleSource = Inline
        Style.TickChecked = #FF3355
    }
}
```

运行时：`SetStyle`、`SetStyleSource` 和 `SetStyleVariant` 都会**立刻重推**，因为一个改了来源
却不重新解析的样式，效果在别的东西碰巧触发推送之前完全不可见。如果你在原地改了一个样式结构体，
自己调一次 `ApplyStyle`。

注意 `SetStyle` 写的是实例的 `Style`：样式表正在生效时写内联样式，你看不到任何变化。
推送照样会跑，但赢的仍然是 `StyleSource` 说的那一份。

<a id="the-project-style-sheet"></a>

## 项目样式表

`UDreamUIStyleSheet` 是一个数据资产：项目的控件在这里约定怎么长。每一族一个默认值加一张命名变体表，
这就是一个控件需要说的全部——哪一族，可选再加一个名字。

<a id="make-a-sheet"></a>

### 建一个样式表
在内容浏览器里新建一个数据资产，类选 `DreamUIStyleSheet`。

<a id="point-the-project-at-it"></a>

### 指给项目
在 *Project Settings ▸ Plugins ▸ Dream GUI* 的 Assets 分类里，把 **Default Style Sheet** 指向它。这个设置
是 `UDreamGUISettings::DefaultStyleSheet`。

<a id="pick-variants-on-controls"></a>

### 在控件上选变体
控件默认从样式表解析；要特殊外观的实例填 `StyleVariant`。

几件值得知道的事：

- **插件不带样式表。** 没有样式表的项目是受支持的状态，不是缺东西：结构体自己的 C++ 默认值就是内置主题。
- **`DefaultStyleSheet` 是 `TSoftObjectPtr<UDataAsset>`**，因为核心模块不认识控件库的类型；选择器只列样式表，
  `UDreamUIStyleSheet::GetProjectSheet()` 负责转换。它在第一次被调用时加载样式表，没配置时返回空，调用方把它读作
  "用内联默认值"，而不是错误。
- **解析发生在构造时**，按名字查找，和 Slate 的样式集一样。这里没有运行时换主题，也不打算有：
  它换来的是——改项目的强调色只需编辑一个资产的一处，而不用走访每一个放过开关的屏幕。
- **拼错的变体名回落到这一族的默认值**，而不是失败：屏幕上总有可见的东西，十次里有九次是对的。

<a id="face-brushes-state-faces-and-image-brushes"></a>

## 面刷子、状态面和图片刷子

<a id="fdreamuifacebrush-the-skin-of-a-controls-face"></a>

### FDreamUIFaceBrush：控件面的皮肤

每个控件的面都是一个程序化圆角矩形（`UDreamRectBlock`）；UMG 的手感大多就在这里。
[`FDreamUIFaceBrush`](../Reference/DreamUIFaceBrush.md) 是可选的一层皮：为空时面保持普通矩形；给了图，面在
**同一个轮廓里**画这张图——圆角、边框和 selectable 的着色都照常工作，因为皮是矩形的
主体纹理，而不是另一个视觉。控件的面永远不会不再是矩形。

| 字段 | 含义 |
| --- | --- |
| `Image` | 一张纹理或一个图集精灵，拖哪个进来都行 |
| `Tint` | 乘在图上，Slate 的 tint。和样式的状态色是两条独立通道；两者相乘 |
| `ScaleMode` | `Stretch` / `FitIn` / `Envelop`，对应 UMG 的 Stretch / ScaleToFit / ScaleToFill |
| `DrawMode` | `Image` / `Box` / `Border`，Slate 的 DrawAs。后两个是九宫格，只对普通纹理生效；精灵总是按 `Image` 绘制 |
| `Margin` | 九宫格边距，单位是**纹理像素**：文件里八像素宽的边，在任何矩形尺寸下都保持八像素宽 |
| `ImageSize` | 图自己的绘制尺寸。零表示不表态；非零时覆盖带尺寸字段的部件（开关的框和勾、单选的框和点、滑条的把手）的样式尺寸 |

<a id="fdreamuistatefaces-one-face-drawn-five-ways"></a>

### FDreamUIStateFaces：一个面的五种画法

样式原本只能靠颜色区分五种状态，所以普通按钮和按下按钮是**两张不同的图**的项目只能 fork 控件。
[`FDreamUIStateFaces`](../Reference/DreamUIStateFaces.md) 补上这个缺口，而且不引入第二个状态机：selectable 已经
决定了控件处于哪个状态，这里只决定那个决定画成什么。

| 字段 | 含义 |
| --- | --- |
| `Normal`、`Hovered`、`Pressed`、`Disabled`、`Focused` | 每种状态一个面刷子。没有图的状态回落到 `Normal`，`Normal` 也没有图就回落到控件自己那一张刷子 |
| `bTintForeground` + 五个 `…Foreground` 颜色 | 控件**自己的**前景部件——自己的标签或图标，从不碰宿主的内容——每种状态的颜色。默认关闭 |
| `bUsePressedPadding` + `PressedPadding` | 按下时内容周围的边距，经典的"按下去"效果。静止时的边距是控件自己的 `ContentPadding` |
| `HoveredSound`、`PressedSound`、`ClickedSound` | 控件出声的三个时刻，作为样式决定：一次点击应该和这个 UI 里别的点击听起来一样 |

**每个字段都是可选的**，所以它什么都不改变：空的状态组画出来和以前逐像素完全相同。它是按钮、开关、
下拉框的面和列表行这些样式的**一个字段**，而不是基结构体——USTRUCT 继承经不住覆盖位的合并。

焦点和悬停是两个问题——焦点在指针离开后仍然存在，手柄上根本没有指针——所以 `Focused` 单独占一格。

<a id="fdreamuiimagebrush-the-image-visuals-brush"></a>

### FDreamUIImageBrush：Image 视觉的刷子

[`FDreamUIImageBrush`](../Reference/DreamUIImageBrush.md) 是核心模块 `Image` 视觉（`UDreamImage::Brush`）
的刷子，`UUIToggle`、`UUIDropdown` 和 `UDreamSelectableStyle` 也用它。它不是控件的面，能画的方式更多：

| 字段 | 含义 |
| --- | --- |
| `ResourceObject` | 一张纹理、一个精灵或一个材质 |
| `TintColor` | 颜色 |
| `DrawAs` | `None`、`Box`、`Border`、`Image`、`Tiled`（按自身尺寸平铺）、`Filled`（只显示一部分，进度条那样）、`RoundedBox`（圆角轮廓） |
| `ImageSize`、`Margin`、`UVRegion`、`PixelsPerUnitMultiplier` | 尺寸、0 到 1 的边距、UV 区域、像素密度 |
| `FillAmount`、`FillMethod`、`FillOrigin`、`FillDirectionFlip` | 仅 `Filled`：显示多少、切割形状、从哪端开始、另一个方向 |
| `CornerRadius`、`CornerSegments` | 仅 `RoundedBox`：每个角的半径（像素，从左上顺时针）和每段圆弧的点数 |

`RoundedBox` 是从几何上切出来的轮廓：角的平滑程度取决于段数，且不自带抗锯齿。要精确、与分辨率
无关、带边框或渐变的圆角矩形，用 `RectBlock`。

<a id="two-things-beside-them"></a>

## 旁边的两样东西

- **`UDreamSelectableStyle`**：输入层的数据资产，挂在行为（`UUISelectable::Style`）上——一组可复用的状态色、
  状态刷子、动画时长和声音。selectable 上挂着它时，它压过控件推过来的颜色和声音。
- **[`FDreamTextStyle`](../Reference/DreamTextStyle.md)**：文字的描边、投影和发光，整个库共用一个结构体；
  `FDreamRichTextStyle` 直接带着它，而不是自己再定义一份。

要换掉的不是颜色而是**整棵树**时，用控件的 `Template` 属性（在 Style 分类里）：给一个里面有正确
命名部件的 widget 蓝图，控件用它的树，行为和样式约定照旧。见
[一个控件是怎么搭起来的](index.md#how-a-control-assembles-itself)。
