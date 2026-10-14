---
title: "将项目迁移到 DreamGUI 1.0"
description: "将项目迁移到 DreamGUI 1.0"
---

<a id="moving-a-project-to-dreamgui-10"></a>

# 将项目迁移到 DreamGUI 1.0

1.0.0 是首个公开发行版。在此之前，本分叉的开发版被编号为 1.x、2.0 和 2.1 —— 见
[CHANGELOG](../../../CHANGELOG.md) —— 本指南沿用这一编号方式：这里的 2.1 指的是开发版，早于 1.0.0。

针对五种项目：

- 位于 **1.0.0** 上的项目，迁移到其之后的版本 —— [从 1.0.0 到下一个发行版](#from-100-to-the-next-release)，
  就在下面；
- 位于**开发版 2.1 或 2.0** 上的项目 —— [从 2.1 到 1.0.0](#from-21-to-100)，而位于 2.0 的项目
  要先经过 [从 2.0 到 2.1](#from-20-to-21)；
- 带有**按 LGUI 或 LexUI 保存的资产**的项目 —— 也就是这个分叉起步的上游；
- 位于**本分叉的早期构建**（单模块的 1.x 构建，非 1.0.0）上的项目 —— 在模块拆分、输入重做和渲染器重做之前；
- 带有**自己针对插件写的 C++** 的项目 —— 子类、自定义 visual、自己的视口客户端。

带编号的章节讲的是从 LGUI、LexUI 或 1.x 构建迁移到 2.0 线；完成了这一步的项目之后再读
[从 2.0 到 2.1](#from-20-to-21) 和 [从 2.1 到 1.0.0](#from-21-to-100)。这里说的一切都是为了保住你已有的东西。插件是什么、
如何从零开始，见 [README](../../../README.md)；每个版本新增了什么，见 [CHANGELOG](../../../CHANGELOG.md)。

<a id="from-100-to-the-next-release"></a>

## 从 1.0.0 到下一个发行版

面向位于 1.0.0 上的项目。没有任何东西被改名，打开项目之前也无事可做。有几个控件的输入方式现在与 UMG 相同，
分屏会像 UMG 那样排布各玩家的屏幕，菜单和下拉列表会放行落在其外部的按压，围绕旧方式构建的界面会感觉不同：

- **Multi 模式下的列表、瓦片或树形视图像 SListView 那样选择行。** 以前普通点击选中该行并保留已选中的每一行，
  再次点击则取消选中；现在普通点击只选中该行。添加或取消一行用 Ctrl+点击（Mac 上是 Cmd），Shift+点击添加从锚点到
  被点击行之间的所有行，在有焦点的行上按 Shift 或 Ctrl 加方向键会从该行扩展选择，Ctrl+A 选中每一行。手指轻点
  仍会像以前那样添加其所在行，但再次轻点已选中的行不再取消选中。手柄在某一行上的确认等同于普通点击，因此一个靠
  逐一确认来让玩家多选行的手柄界面现在只能选中一行；这样的界面需要自己维护集合 —— 在 `OnItemClicked` 中切换，
  用 `SetSelectedIndices` 恢复 —— 或者用自己的手柄按键提供 Ctrl 的含义。`SetItemSelection` 及其他选择调用未变，
  因此构建选择的代码不受影响。
- **获得焦点的旋转框保留方向键。** 上、右增加，下、左减少，每次按压都是一次提交
  （`OnValueCommitted`）。以前方向键会把焦点移到旁边的控件上；现在玩家像在 UMG 中一样用 Tab、十字键或摇杆
  离开旋转框。旋转框会在任何动作绑定之前收到方向键，因此当旋转框获得焦点时，绑定在方向键上的动作不再触发。
  StepSize 为零时方向键仍然步进（按 SSpinBox 的默认步长）；步进面则和以前一样不步进。
- **手指永远不会调出工具提示。** 以前手指落在、按住或拖到带工具提示的控件上时可能显示其气泡；
  想要长按显示工具提示的纯触屏游戏可以在该处理函数中用 `UDreamUITooltipSubsystem::ShowTooltipFor` 来显示。
- **在分屏中，玩家的屏幕就是该玩家在视口中的部分**，与 UMG 的 Add to Player Screen 一样。
  以前 `UDreamScreenUISubsystem` 为本地玩家创建的屏幕各自占据整个视口，以整视口的像素来排布和命中，并被绘制进
  每个玩家的视图、压缩进其矩形中；现在每块屏幕的大小就是其玩家部分的大小，锚点和 ScaleWithScreenSize 缩放器
  基于该尺寸工作，并且只绘制在该玩家的视图中。为整个视口制作的分屏布局 —— 锚定到其边缘，或以像素定位 —— 会
  落入玩家的部分中；锚定并相对于其屏幕设定大小的布局则无需改动。单人游戏，以及关闭分屏的游戏中每位玩家的屏幕，
  仍占整个视口。放置在关卡中或手工创建的屏幕空间画布不属于任何玩家，保持原样；用
  `UDreamCanvas::SetViewportPlayerIndex` 给它指定一个玩家即可使其归属某个玩家。自定义画布缩放器（`UDreamCanvasCustomScale`）会收到
  从所属画布部分的角落量起的位置，对于没有玩家的画布来说就是视口的角落，与以前相同。
- **手指轻点滑块的轨道不再移动它。** 手指移动超过拖拽距离后值才会移动，`OnMouseCaptureBegin` 也在那时触发，
  而不是手指落下的那一刻；手指没有移动就抬起则什么也不改变、什么也不提示。希望靠轻点目标位置来设置滑块的
  触屏设备现在需要让玩家像在 UMG 中那样拖动滑块手柄。鼠标保持不变。
- **手指沿可拖拽行的列表拖动会滚动列表**（列表、瓦片或树形视图上的 `bAllowDragging`）。
  以前它会拾取该行；鼠标仍然如此。靠手指拖拽行的触屏设备把列表的
  `FingerDrag` 设为 `PickUpRow`，这正是手指的旧行为。
- **在打开的菜单之外的按压会传递到它落到的对象。** Dream Menu Anchor 的菜单和菜单锚点面板会像以前一样
  在按压时关闭，随后这次按压会继续传递：它下面的按钮会被按下并点击，它下面的字段会开始编辑。依赖菜单外
  第一次点击什么都不做的界面 —— 例如菜单悬浮在按钮上、菜单弹出期间按钮不能被点到的场景 —— 可以在项目设置
  （Dream GUI、Input）中打开 `bMenusConsumeOutsideClick`，这会为所有菜单恢复旧行为。用点击打开菜单锚点的按钮在
  调用 `Open` 之前会先询问 `UDreamMenuAnchor::ShouldOpenDueToClick`，与 UMG 一样；无条件调用 `Open` 或
  `ToggleOpen` 的处理函数现在会重新打开其自身点击刚刚关闭的菜单，需要改为先询问。
- **在下拉列表打开时于其外部的按压会传递到它落到的对象，与菜单一样。** 列表像以前一样在按压时关闭，
  不选择任何项；随后这次按压会继续传递。`bMenusConsumeOutsideClick` 会为所有列表连同所有菜单恢复旧行为。
  `UUIDropdown::bUseInteractionBlock` 的默认值和含义都变了：它以前默认开启且单独决定；现在默认关闭，把决定权留给项目设置，
  开启则无论项目如何设置都为该下拉列表保留这次按压。以旧默认值保存的下拉列表会以新默认值加载；必须保留
  这次按压的下拉列表 —— 例如列表悬浮在控件上、列表打开期间控件不能被点到的场景 —— 要重新打开它。
- **被按住时被移动的控件会丢失这次按压。** 在指针按钮按下的情况下被移到另一个父级下、从父级移除或被销毁时，
  它会在下一帧收到自己的释放（以及拖拽的结束），而该按钮自身的释放之后不会点击任何东西，就像 Slate 控件
  丢失鼠标捕获一样。把被按住的控件移到别处 —— 比如拖拽时移到绘制在其他内容之上的图层 —— 的游戏代码若没有
  使用拖放，就会丢失它所移动的那次按压；这样的代码应改为移动一个副本，或启动拖放（`UDreamDragDropOperation`），
  移动其源不会结束拖放。
- **在禁用控件上的按压会传给其周围的控件。** 以前它哪儿也不去：禁用的按钮会整个吞掉按压、悬停和点击。
  现在与 Slate 一样，禁用控件及其内部的一切都被从路径中剔除，其上方最近的可接收输入的控件会收到悬停、按压和
  点击 —— 禁用按钮周围卡片上的触发器、阻挡器或点击处理函数会收到这次按压，按压打开的列表中某个禁用条目则
菜单、下拉列表或弹出窗口内部的按下不会关闭任何东西。如果一个界面依靠禁用控件来阻止按下穿透到持有它的容器——即一个面板上的点击处理器不能因灰显按钮而触发——那么就该给该面板的处理器加上所需检查，或者把 `UUIEventBlocker` 放在控件周围一个启用的控件上。
  禁用控件背后绘制的内容仍然不可达，而整棵树都被禁用的控件仍然像以前一样隐藏其画布的响应。现在鼠标停在禁用控件上会弹出其工具提示。
- **按键和方向键会停在链接上。** 在游戏中，带有 `UUITextHyperlink` 的文本的每个链接都是 Tab、方向键和摇杆的停靠点，就像 UMG 的超链接一样，确认操作会宣告它。一个界面的 Tab 或方向键顺序以前会越过一段链接，现在会停在每一个链接上；对该文本关闭 `bLinksAreFocusStops` 可以恢复旧顺序。
  这些停靠点是文本控件的子控件，在运行时创建且从不保存：在游戏中统计文本控件子项数量的代码会看到它们。
- **当操作栏条目的条目类带有按钮时，提示就是一个按钮。** 条目类是 `UDreamButton`，或预置件中包含 `UUIButton` 时，现在点击、轻触或确认会运行其操作。一个自己接线了点击处理器来运行操作的项目会执行两次；它应移除自己的处理器。
- **在关卡中面板上打开的菜单和下拉列表会留在面板上。** 从世界空间面板打开的弹出窗口，或从被网格显示的渲染目标中绘制的画布打开的弹出窗口，会出现在该面板自身根画布之上，而不是玩家的屏幕上，就像 UMG 控件组件的菜单留在它的窗口中一样。它会被面板裁剪并按面板的透视绘制，并且在任何外部按下（包括屏幕）时关闭。一个依赖此类列表平铺绘制在 HUD 上的游戏应从它自己的屏幕空间控件显示它。在屏幕根下查找已打开弹出窗口的代码（`UDreamScreenUISubsystem`）会找到在面板根控件下打开的那个；`UDreamUIPopupLayer::IsOpen` 和 `GetOpenPopups` 对两者都作出回答。根控件会布局其子项的面板（如盒子、网格）也会布局放在它上面的弹出窗口；这样的面板会把布局保留在根的子项上。
- **`UDreamUIActionTrigger::SetAction` 会在活动控件上立即绑定。** 一个在控件已存在后才给触发器的操作命名的界面，并通过隐藏和显示控件让按键生效，可以去掉这一步；其他什么都不变。一个提前在活动按钮上命名操作、意味着按键只在之后生效的界面，应在按键该开始生效时设置该操作。

`UDreamSpinBox` 或列表族的 C++ 子类若重写 `NativeOnKeyDown`，应调用 `Super` 来保留这些按键，就像基类对任何按键作出回答一样。

<a id="from-21-to-100"></a>

## 从 2.1 到 1.0.0

适用于使用 2.1.0 开发版构建的项目。两者之间没有任何重命名，所以 2.1 保存的、命名当前类型的资产会按原样加载，打开项目之前无需做任何事——只有一个例外，见下文。
1.0.0 为 `.dui` 新增的内容——用 `.dui`、`use … as`、`if`、`for`、`rows`、
`@slot { … }` 和 `@fill` 编写的组件——是旧语法旁边的新语法，2.1 的文件会像以前一样编译。有三个地方之后可能看起来不同，每一个都是项目可能曾经绕过的一个 bug 的终结：

- **唯一画布参数为 `DreamUI_IsRenderByDreamUIRenderer` 的笔刷材质会被告知渲染器。** 这样的材质曾被 DreamGUI 的渲染器预先乘了两次，因此在边缘绘制得更暗。对此做过补偿的材质现在会绘制得过亮：去掉补偿。
- **赋给图像的材质实例会显示在其首次绘制之后设置在其上的参数。** 一个为了绕过这一点而每帧重新创建的实例可以保留下来并直接设置。
- **材质绘制的图像之后的文本和块会再次绘制**在屏幕和渲染目标画布上；在此类图像之后把文本排除在批次外的布局不再需要这样做。

焦点环现在会适配它所标记的控件，并且可以按控件拒绝（`UUISelectable::bUseFocusRing`）；你自己带有蓝图处理器的环类会保留自己的布局。`DreamTween.MaxStepSeconds` 是新增项且默认关闭。

**插件不再携带 CoreRedirects。** 2.1 在 `Config/DefaultDreamGUI.ini` 中提供了 763 条；1.0.0 不提供任何，并且该文件已删除。一个仅通过其中某条 redirect 才能加载的资产——按 LGUI 或 LexUI 保存、由早期 1.x 构建保存、在模块拆分之前保存，或在某个控件或事件重命名之前保存——必须在用 1.0.0 打开之前重新保存，否则会丢失命名旧类型的任何内容。这些 redirect 在 1.0.0 下可原样使用，因为自 2.1 以来没有任何重命名，所以项目可以在重新保存所需的时间内借用它们：

1. 把 [2.1.0 的 `Config/DefaultDreamGUI.ini`](https://github.com/TypeDreamMoon/DreamGUI/blob/3049561a1742ab59d83a404ee267d2f580e53589/Config/DefaultDreamGUI.ini)（提交 `3049561a`）中的 `[CoreRedirects]` 段复制到项目的
   `Config/DefaultEngine.ini` 中。
2. 用 1.0.0 打开项目，并重新保存每个使用 DreamGUI 的资产——打开它们后 *File > Save All*，或者对内容运行
   `ResavePackages` commandlet。
3. 再次从 `Config/DefaultEngine.ini` 中删除该段。

从未有过此类资产的项目——在 2.1 上启动的，或此后已重新保存过的——跳过全部三步。下面的第 1 至 3 节说明了这些 redirect 覆盖什么。

<a id="from-20-to-21"></a>

## 从 2.0 到 2.1

适用于已经在 2.0 上的项目。没有任何重命名，因此不需要 redirect，打开项目之前也无需做任何事；下文说明之后可能看起来或表现不同的地方，以及你自己的 C++ 必须改动的地方。[CHANGELOG](../../../CHANGELOG.md) 列出了新增的内容。

<a id="before-you-upgrade"></a>

### 升级之前

- **由 2.1 重新保存的资产无法在 2.0 中打开。** 2.0 会拒绝用更新的 `FDreamGUIObjectVersion` 保存的包。请像对待任何升级一样，把项目保留在版本控制下。
- **没有添加任何 redirect**；仍然是 763 条，且 2.0 保存的每个名字仍然可以加载。
- **`UDreamDialog::DimmerBehaviour` 已移除**，没有 redirect。它对蓝图是只读的，而读取它的蓝图会编译失败：调光器不再是按钮，`bShowDimmer` 和 `bCloseOnDimmerClick` 负责配置它。

<a id="assets-that-change-as-they-load"></a>

### 在加载时发生变化的资产

- 字体的回退列表（`FallbackFontArray`）变为符认默认设置的 `Fallbacks` 条目：每个码点、任何语言、缩放为 1、不优先于字体自身的字形。
- 表情符号数据键会获得其完整序列。
- 在 `SdfSource` 存在之前保存的距离场字体，会加载到轮廓多通道场，而不再被强制为单通道场：更锐利的拐角、BGRA 图集，以及大约四倍的图集内存。把 `SdfSource` 设置回去可保留旧外观。
- 颜色等于其 `BrushColor` 的边框视觉，在注册时会被重置为白色。
- `UDreamMeshModifierPositionAsUV` 的 UV 通道自 2.0.0 起默认为 0，通道 1（画布自身的通道）会被拒绝并给出警告。一个在 2.0 之前制作、依赖旧默认值 1 的资产，必须设置该通道。

<a id="defaults-that-changed"></a>

### 已更改的默认值

一个让属性保持默认值从未保存它的资产会采用新的默认值，而 2.1 新增的属性从默认值开始：

- **默认字体**为 Roboto（含真正的粗体和斜体字面）以及用于 CJK 的 DroidSansFallback，位于多通道
  字段上：所有使用它的文本都会重新排版，宽度也随之改变。
- **小号文本**（`UDreamGUISettings`）。2.0 从距离字段绘制所有文本。小屏幕文本现在从设备像素网格上的
  带 hinting 的覆盖字形绘制（`bSmallTextCoverage` 开启，最多 `SmallTextMaxPixelSize` 20，
  `SmallTextContrast` 1），而 `bSmallTextCorrection` 开启后，会使那些尺寸下仍从字段绘制的内容变暗。带有
  描边、发光或衬底时，其字面从覆盖字形绘制——在描边细于 2 个设备像素时带 hinting，或不带 hinting（`SmallTextEffectFace` Auto）——而这些效果从字段绘制，并被移动以与其对齐。在
  渲染层中，一旦该层静止保持 3 帧，它就从覆盖绘制，而在该层移动期间从字段绘制。字面柔和度和膨胀会让文本保持在字段上。`DreamGUI.Text.SmallTextCoverage 0` 会在
  所有地方关闭覆盖，包括设置为 On 的字体。
- **文本**：连字开启（`bLigatures`），`TabSize` 8，`bPreferColorEmoji` 开启。
- **渲染**：`RenderLayer` Auto；`bShowEffectWhenDisabled` 开启，因此禁用的 Border 的背景以 45 % 绘制。
- **滚动**：`UUIScrollView::CanScrollInSmallSize` 关闭，因此短列表不再有橡皮筋效果；
  `bAllowRightClickDragScrolling` 和 `bEnableTouchScrolling` 开启。滚动条不再是焦点目标
  （`UUIScrollbar` 的 `bCanNavigateHere` 关闭；将其开启可把滚动条放回导航），其箭头永远不是，而
  列表或滚动框为自身生成的滚动条永远不是 Tab 停靠点。
- **列表、瓦片和树**是一个 Tab 停靠点（`TabNavigation` Once）：Tab 在可能时从选中的行进入，
  并将其滚动到可见位置，否则从第一行进入，Shift+Tab 则从最后一行进入；下一次 Tab 会离开该列表。
- **玩家索引**：`UDreamUINavigationScope::UserIndex`、`UDreamUIActionTrigger::UserIndex` 和
  `UDreamUIActionBar::UserIndex` 默认值为 -1，即控件所属的玩家；它们以前是 0。手动设置的索引仍然
  优先，而 `GetUserIndex()` 会返回当前生效的索引。在单个玩家下没有任何变化；在分屏下，第二个
  玩家的屏幕会响应该玩家。
- **Tab 和焦点**（Project Settings > Plugins > Dream GUI）：`bTabNavigation` 开启，`TabOrder` Hierarchy，
  `bTabWrapsAtScreenEnd` 开启，`bTabStartsTextEdit` 开启；`bFocusVisibleOnlyFromKeys` 开启，`bHideCursorOnGamepad` 开启，
  `bUsePlatformAcceptBack` 开启，`InputModeWithoutScope` All。在下拉菜单上（`UUIDropdown`、`UDreamDropdown`），
  `bTabCommitsHighlightedRow` 开启。
- **按键**（同一页面）：`ConfirmKeys`、`BackKeys`、`DirectionKeys`、`PageKeys` 和 `ExtentKeys` 保留 2.0 的表，
  并将触发器添加到 `PageKeys`（LT 和 RT 翻页）；`PreviousTabKeys` 和 `NextTabKeys` 是肩键
  （LB 和 RB 切换标签页）。

<a id="switches-that-put-20s-behaviour-back-or-measure-the-new-one"></a>

### 把 2.0 的行为放回去、或测量新行为的开关

- `TabOrder = LegacyGeometric`（项目设置）：在本版本中，Tab 和 Shift+Tab 像 2.0 那样按几何方式移动。
- `bSmallTextCoverage` 关闭（项目设置），或 `DreamGUI.Text.SmallTextCoverage 0`：所有文本都像
  2.0 那样从字段绘制。`SmallTextEffectFace = Field` 只保留带有描边、发光或衬底的完全在字段上的小号文本。
- `DreamGUI.Text.SmallTextCoverage`（-1 跟随项目设置；0 为所有字体关闭，包括设置为 On 的字体；1 为
  所有交由项目决定的字体开启）和 `DreamGUI.Text.SmallTextMaxPixelSize`（大于 0 时会为所有没有自身限制的字体替换该设置）
  会作用于每个字体，并且变更会重绘所有文本；
  `SmallTextRepaintBudgetPerFrame`（512）限制一个世界在一帧内将多少文本重绘到覆盖字形上。
- `DreamGUI.Text.IncrementalParse`、`DreamGUI.Text.IncrementalMeasure` 和 `DreamGUI.Text.InPlaceDisplayList` 分别关闭
  增量排版的一部分，回到对整个文本进行排版；`DreamGUI.Text.VerifyIncremental`（不在
  Shipping 构建中）会将增量排版与全新排版进行对比，并指出它们的不同之处。

- 增量排版与同一文本的全新排版是相同的，逐元素相同；这些开关只交换
  时间。`ElementsParsed` 统计一次编辑实际拼接进来的元素，而不是被拒绝的窗口丢弃的读取。

- 2.1 新增且默认关闭，待测量后决定：`bFieldTextCorrection`（超过
  `SmallTextMaxPixelSize` 的字段文本会获得小号文本的对比度和线性光混合，因此其粗细不会在
  截止点处跳变）、`DreamGUI.Text.SmallTextOnMove`（小号文本在移出像素网格时的行为：0 是每次移动都从覆盖重绘，1 保留其覆盖四边形，2 从字段绘制；1 和 2 会在静止保持 3 帧后从覆盖重绘）以及 `DreamGUI.Scroll.SnapToDevicePixels`（2D 画布上的滚动视图以
  整个设备像素为单位移动其内容）。

<a id="text"></a>

### 文本

- **换行遵循游戏的文化**：行和词迭代器按当前文化创建，并在文化改变时重新创建。2.0 遵循操作系统的语言。带有 CJK 文本的游戏必须随其打包
  ICU 数据（EFIGSCJK 预设，对于泰语及其邻近语言则用 All）：见
  [FontsAndPackaging.md](fonts-and-packaging.md#4-icu-data-package-efigscjk-or-all)。
- **效果随文本淡出，而不是随其颜色淡出。** 描边、发光或衬底随渲染不透明度、
  内容色调的 alpha 以及 TextAnimation 的 alpha 淡出，而不再随文本自身颜色的 alpha 淡出。通过其色调或 TextAnimation 淡出的文本现在也会淡出其描边；空心文本——透明颜色加描边——
  会保留其描边。
- **绘制。** `FDreamTextStyle` 具有 `FacePaint`、`OutlinePaint`、`OverlayPaint`、`OverlayBlend`、`PaintBoxHorizontal`
  和 `PaintBoxVertical`，全部关闭，因此在设置其中一个之前没有任何绘制会不同。字面绘制会取代
  字面上的文本自身颜色，正如 CSS 的 `background-clip: text` 所做的那样；在已绘制文本内部的 `<color>` 运行是实心的，
  而自定义样式的 Multiply 会为渐变着色。富文本在新过滤标志
  `Gradient` 之后有一个 `<gradient=Name>` 标签：一个其 `RichTextTagFilterFlags` 在没有该标志时被保存为掩码的文本会将标签显示为文本。富文本
  自定义样式条目具有一个 `paintType`（KeepOrigin、Set、None）和一个 `paint`。渐变的 CSS 会按 Chrome 读取它的方式读取，
  但有两点除外：`to <corner>` 无论盒子形状如何都取正方形的角度（45deg、135deg、...），而颜色
  名称是 CSS 的（`green` 是 #008000，而 `<color=green>` 是 #00FF00）。编辑绘制从不会对文本进行排版——
  另一个渐变只会写入其行，打开或关闭绘制会重绘——除了撤销，它会对其进行排版并
  再次绘制。
- **Tab 停靠点**按其空间在文本中被测量的方式测量其空间——在缩放画布下按设备尺寸——因此
  `TabSize` 个空格会正好结束在一个停靠点上。

- **渐变盒子**：文本块横向跨越内容盒，并从第一行的顶部延伸到最后一个
  *可见* 行的底部（被 clamp 隐藏的行会被排除）；一个其字母全部为实心的富文本运行不会获得
  `Run` 盒子的任何片段。渐变运行内部的 `<color>` 是实心的；其内部自定义样式的 Multiply 会为渐变着色。
- **编辑器中按文化换行**：ICU 在 Windows 上是静态库，因此编辑器会给 DreamGUI 一份
  其自身的副本；2.1 将该副本指向引擎的 ICU 数据（`Engine/Content/Internationalization`），因此编辑器会像打包后的游戏那样按游戏的文化
  换行。如果无法读取该数据，日志会说明一次，而
  引擎的默认文化迭代器会代替它。

- **你自己的材质**如果不通过 `MF_DreamUI_Shade` 着色，就不会获得覆盖字形，它的 emoji 来自
  `EmojiData` 或单色字形，而不是像字段那样采样彩色位图，并且通过顶点
  颜色绘制渐变。DreamGUI 自带的材质带有一个 `DreamUI_ShadeMarker` 参数，说明它们通过它着色。
- **来自 2.0 的改动**，针对跳过了其说明的项目：行高百分比的 CSS 半行距；"Left"
  在从右到左段落中表示起始；双向文本重排行（L2）；字母间距不应用于连写
  文字，并且在行尾计入，与 Chrome 一致；粗体每个簇推进一次，且仅针对合成粗体；
  下划线和删除线使用字体的度量并按 run 绘制；描边和硬阴影为斜接；
  效果颜色按 sRGB 解码，绘制得更暗；emoji 优先级改变；`<lang>` 标签会被解析（在
  Language 过滤标志关闭时它们是文本）；缩放的回退会增长其行盒；制表符推进到制表位。

<a id="fonts"></a>

### 字体

- 嵌入的字体文件（`CustomFontFile`，而非外部）在游戏 cook 时会再次读取，如果 cook 找不到
  任何可嵌入的内容，会报错，而不是让字体在发布时没有字形。
- 只要有文本仍从字体的覆盖字形图集单元绘制，字体就保留这些单元；隐藏、折叠或
  非活动的文本会让出它们，如果在此期间它们被收回，则会在被绘制前重新绘制。超出两倍
  单元预算（`MaxCoverageCells`，4，在 Project Settings > Plugins > DreamUI 中）后，新的小文本字形会等待，
  同时从字段绘制，每个字体给出一次警告。
- 字体的图集纹理及其文本列表不再记录在撤销事务中；位图字体在撤销
  和重做时重新加载。

<a id="layout"></a>

### 布局

自 2.0 起：面板内的文本按面板将给予它的宽度测量；折叠的子项测量为 0；
`OnDimensionChanged` 触发一次而不是两次；网格跨度、居中的非对称内边距、wrap box 的填充、scale
box 的模式以及声明的缩放遵循 UMG；scroll box 有填充槽以及前后内边距，在没有滚动内容时
不过度滚动，折叠其内置条并隐藏空闲轨道。

<a id="controls-focus-and-navigation"></a>

### 控件、焦点和导航

- **Tab** 是 Next，Shift+Tab 是 Prev，不按住 Ctrl、Alt 或 Cmd；按住其中之一时 Tab 只
  进入按键绑定，并且绑定首先获得每个键。顺序是层级的顺序，同级按 `TabIndex`；
  可选项或控件上的显式 Next 或 Prev 链接仍然优先，除非其目标无法接受焦点或位于 Tab
  被保持的范围之外。`TabOrder = LegacyGeometric` 在此版本中恢复 2.0 的几何顺序。Tab 停留在顶层弹出窗口内，
  然后停留在模态或调光器已升起的对话框内，否则停留在玩家的屏幕上——并且当焦点位于绘制
  在打开的弹出窗口前面的层上时，则停留在该层上。共享屏幕上另一个玩家的弹出窗口永远不会成为停靠点。列表、瓦片和
  树是一个停靠点；视图外的停靠点会滚动进入视图；工具提示、弹出面板和滚动条永远不会成为停靠点；
  被设为不可聚焦的控件不会成为 Tab、方向键或每控件规则的落点。只要玩家有可去的 Tab 停靠点，Tab 就算作已处理，
  即使是在不循环的屏幕末端。
- **文本字段**：Tab 提交编辑——如同离开字段一样，通过 `bSubmitWhenDeactivate`——并继续移动，
  而通过 Tab 到达则开始编辑；多行字段只有通过 `bTabTypesTabCharacter` 才会键入制表符（此时 Ctrl+Tab 离开
  它）；在 IME 组合期间，Tab 留给 IME；Alt+Tab 和 Cmd+Tab 永远不属于字段，Ctrl+Tab 也不属于，除非它
  键入制表符。字段仅在其使用 Tab 时才接受 Tab，所以 `IgnoreKeys` 不再需要其中的 Tab 来防止 Tab 被
  吞掉。手柄在正在编辑的字段上按下接受键会提交它；开始编辑的那次按下不计算在内。
- **下拉框和菜单**：打开的的下拉框中的 Tab 提交高亮行（在
  `UUIDropdown` 和 `UDreamDropdown` 上的 `bTabCommitsHighlightedRow`，默认开启），关闭它并移动到下一个控件，Shift+Tab 到上一个；
  菜单中的 Tab 关闭整个菜单链并继续移动（弹出 `TabBehavior` CloseAndContinue）。其他弹出窗口将 Tab 保持在
  其内部（Cycle）。
- **焦点外观**——Focused 状态和焦点环——仅在焦点来自按键或手柄，或
  来自脚本且最后一次输入是按键或手柄时显示。鼠标点击不再显示它，鼠标悬停也不再夺走
  焦点环。只要焦点被绘制，焦点环就显示在每个屏幕页面上，包括没有 presenter 的页面，以及
  作用域在其屏幕打开时给予的焦点。scroll box 的 `OnFocusReceived` 和 `OnFocusLost` 跟随真实焦点，
  而非其 Focused 外观。
- **导航作用域有输入模式**（`InputMode`：All、Menu、Game）。当该作用域是活动作用域时，Game 会为该玩家关闭 DreamGUI 的内置导航、
  确认、Back、分页和标签切换；绑定、指针和打字仍然有效。
  在没有作用域也没有焦点时，导航按下只查看玩家自己的屏幕空间
  画布，永远不查看世界空间 UI 或另一个玩家的屏幕。
- **弹出窗口和对话框**：显示模态会先关闭该玩家打开的弹出窗口，模态关闭时焦点回到其
  打开者。在绘制于打开的弹出窗口前面的层上按下会关闭该弹出窗口，并且仍然到达
  该层——它不再被吞掉——而 Back 会先到这样的层，再到弹出窗口。独立对话框
  恰好在其调光器升起时限制导航，无论 Close on Back 如何说：未调光的对话框若 Close on Back
  开启，则不再限制它。菜单锚点的 `OnMenuOpenChanged(false)` 仅在其宣布的打开之后触发。被抬起的
  面板菜单保持其槽的内边距、微调和尺寸边界，并且当它返回原位时其槽完好恢复；弹出窗口打开时
  被给予的画布此时会被收回，或者在其已经拥有时，恢复其排序顺序、覆盖和追踪
  通道。独立对话框的作用域属于其所属玩家。
- **滚动**：分页、Home、End 和右摇杆从聚焦的控件本身开始，所以聚焦的 scroll box
  滚动自身；触发器分页。
- **肩键**切换活动标签视图的标签，跳过禁用的并绕过两端，
  操作栏为它们显示 "Previous tab" 和 "Next tab" 提示。
- **每控件导航规则**（`UDreamWidgetNavigation`）保持在限制性对话框和画布内，Next 或 Prev 上的 Wrap 规则
  会被遵守，而规则未设置的方向由同一控件上的可选项回答。
- **来自 2.0 的改动**：弹出层取代点击阻挡器；弹出窗口外部的点击被消费；Back 只关闭
  顶层弹出窗口；焦点返回给打开者；下拉框在其选中行上打开；对话框推入 Back 作用域；
  确认不再把焦点拉回；禁用的开关、标签和下拉框忽略点击；滑块和下拉框只接受
  左键；文本字段按 UTF-16 偏移编辑。

<a id="input"></a>

### 输入

- **按键和手柄作用在焦点上。** Enter、Space 和手柄的确认键按下的是获得焦点的控件，不是悬停的那个；
  没有焦点时确认什么也不按——以前会按默认控件——并且不动鼠标的悬停。鼠标真的移动之后，悬停才移动导航高亮，
  且一个新的指针从视口外开始。
- `UDreamEventSystem::ActivateNavigationInput(PointerId, DefaultWidget)` 和 `SetHighlightedComponentForNavigation` 不
  再说按键从哪里开始：按键从玩家的焦点开始。用它们来设置起点的代码改为设置焦点
  （`UDreamWidget::SetFocus`）。
- 按键表每次变化时都会重新读取，所以运行时的重映射从下一个按键起生效。平台的
  确认和返回键在运行时读取（Switch 会交换它们），平台反过来用的某个面键
  会被排除在另一张表之外。
- 在 DreamGUI 的 UI-only 输入模式（`UDreamUIInputModeLibrary::SetInputModeUIOnly`）下，玩家
  用手柄时隐藏光标，用鼠标或键盘时恢复，且玩家用手柄时该模式启动即隐藏光标。
  其他地方 DreamGUI 不碰光标。
- 当 Slate 的焦点在裸的游戏视口上、而玩家有 DreamGUI 焦点或一个 Tab 停靠点时，Slate 自己的
  导航——Tab、方向键——归 DreamGUI 管，所以这些都不会把焦点移出视口进入 UMG。
- 手柄型号按最近使用的设备检测，每次设备变化都会检测；变化时动作栏会重建。
- 没有玩家的玩家索引不会创建输入用户。
- **来自 2.0 的变化**：一次点击需要在被按下的控件上释放；释放会送给接管按下的人；
  指针不再停在 (0, 0)；Slate 输入源会检查 UMG 的遮挡和键盘焦点。

<a id="rendering"></a>

### 渲染

- **场文本以其图层预乘合成**：下面什么都没有的地方保持自己的颜色。从场中绘制、alpha 低于 1 的文本
  —— 半透明颜色、render opacity、一次淡入、一个渐变色标 —— 不再向黑色变暗；
  发光在其外缘更亮；在视口中，深色背景上的浅色文本边缘和 Slate 绘制的一样完整（以前更细），
  而在渲染目标中它们像深色文本一直以来的那样在线性光中混合。黑色文本和不透明文本叠在不透明轮廓上看起来和以前一样。
  用 2.0 截取的彩色、淡出或发光文本的截图会在边缘处不同。
- **多重采样画布在其目标是 sRGB 时按 sRGB 混合**：一个 MSAA 画布的边缘和半透明重叠
  会略有不同，更接近同一画布没有 MSAA 时的样子。
- **DreamUI 顶点是 64 字节**（以前是 56），带第五个纹理坐标 `UV4`，顶点属性 8，在
  `TangentZ` 之后；其他每个字段都保持了自己的偏移和属性编号，且 `LEXUI_VERTEX_TEXCOORDINATE_COUNT` 仍是 4
  （`LEXUI_VERTEX_UV_CHANNEL_COUNT`，5，把 UV4 算进去）。材质把它读作 `TexCoord(4)`。文本为渐变
  绘制写入它；其他一切写入 (0, 0)。用构造函数、`SetNum` 或整结构体拷贝创建的顶点是正确的；
  代码若未经初始化就增长一个顶点数组并逐字段填充，必须写入 `UV4`。由
  UE 渲染器绘制的世界空间画布带有五个纹理坐标，而以前是四个。
- **文本四边形在 `UV2.x` 中带一个绘制槽**：`UV2.x += 128 * slot`（`DreamTextQuadCode::SlotStride`），未绘制文本的槽为 0，
  其顶点就是 2.0 写入的内容。解码 `UV2.x` 的着色器或材质先剥掉该槽
  （在 `DreamUIText.ush` 中的 `DreamUIText_UnpackPaintSlot`）；重写文本四边形 `UV2.x` 的网格修改器保留它
  （`DreamTextQuadCode::AddSlot(NewCode, DreamTextQuadCode::GetSlot(OldCode))`）。`DreamUI_ShadePixel` 以一个不绘制任何 paint 的重载保留
  其旧签名。
- **阴影、轮廓和长阴影修改器对彩色 emoji 的副本**以副本的颜色绘制其剪影
  （它们曾绘制第二个全彩 emoji），且副本从不被绘制。若某网格的阴影副本会使其超出
  顶点预算，则绘制时不带阴影，并给出一条警告；以前它会整体被丢弃。
- `MF_DreamUI_Shade` 把 paint 行读作 `DreamUI_PaintDataTexture`。
- 渲染统计中的上传字节每个顶点增加 8。
- **来自 2.0 的变化**：变换变化每帧公告一次（`r.DreamUI.DeferTransformNotifications`）；Auto
  渲染层不做像素吸附；新增控制台变量（`DreamUI.Animation.LitePlayer`、`DirectEvaluation`、
  `DreamGUI.Text.ShapeCache`、`IncrementalLayout`、`r.DreamUI.*`）。

<a id="animation-and-tweens"></a>

### 动画与补间

- **完成和被杀死的补间会退场。** 退场的补间被标记为垃圾，所以其句柄读取为无效，持有它的 UPROPERTY
  在下一次垃圾回收时被置空。`Kill`、`ForceComplete` 和 `Goto` 在被
  杀死或已退场的补间上什么都不做，而 `Restart` 在已退场的补间上什么都不做，只记录一条警告。要在一个补间
  完成后重启它，在它开始前 `SetAutoKill(false)`，或者从它自己的 `OnComplete` 中 `Restart` 它，这样它会继续运行。
- `Kill` 触发一次 `OnKill` 和 `OnComplete`。对尚未开始的补间调用 `ForceComplete` 会先启动它：它读取
  其起始值并触发 `OnStart` 和 `OnCycleStart`。`Goto` 到结尾完成一次，`Goto(0)` 应用
  起始值；一个向后的 yoyo 周期结束于进度 0；计数为 0 的 `RepeatCall` 不调用任何东西；一个空的运行时
  曲线线性缓动，并给出警告；时间缩放、延迟和 delta 都会被检查。
- 一个把自己的序列 seek 进自身的回调在嵌套 16 次这样的 seek 后被拒绝，日志中有一条错误。
- 颜色补间和 RectBlock 的 `*AlphaTo` 补间以 float 计算、取整并钳制：过冲的缓动不再
  回绕，补间中途的值可能与 2.0 相差 1。RectBlock alpha 的结束值也可能如此，2.0 截断了它：
  `BodyAlphaTo(0.5)` 结束于 128，而以前结束于 127。普通颜色补间仍精确结束于其结束值。
- 弹簧默认保持静止：`SetTarget` 唤醒一个；杀死它，或在它开始前 `SetAutoKill(true)`，让它
  在静止时退场。
- 第二个补间 tick 辅助函数不再使补间速度加倍。其所有者已消失的补间被丢弃，不调用回调。
  布局动画和嵌入式 UMG 控件遵循与其他所有控件
  补间相同的暂停和时间膨胀规则。

<a id="extensions"></a>

### 扩展

- retainer box 在应用其设置期间把画布切换到渲染目标模式，之后恢复旧模式，
  在编辑器世界中什么都不做，并在它没有 `DisplayVisual` 来显示其所保留内容时给出一条警告。在一个根
  画布上那就是整个画布：此时它只能通过别处的 `DisplayVisual` 被看到。
- static mesh visual 拒绝顶点数超过一个 section 所能容纳的网格，并给出错误（它以前会截断其
  索引），且其边界就是画布绘制它的地方。
- StaticMesh 几何模式下的渲染目标表面（`UDreamUIRenderTargetGeometrySource`），在
  `bOverrideStaticMeshMaterial` 开启时，仅在 play 中把其材质实例放入 static mesh 组件的槽 0。在
  编辑器中它不再这样做：该实例是瞬态的，所以保存关卡时会把 null 写到该槽自己的材质上，
  而没有任何东西把它放回去。因此编辑器不再在那个网格上显示该目标。
- 弧度为 0 的圆柱渲染目标表面按 1 度弯曲，负弧度的边界在其
  弯曲所向的一侧。
- 背景像素化像模糊和像素排序那样把其渲染目标交给其代理；像素排序的 `SetMaxSortPasses`
  把计数保持在 1 到 512。
- 歌词的 TTML 读取器在 256 层嵌套处停止，并给出错误。
- 运行时设置的 TextAnimation 属性会启动，它们替换的那些会停止；一个属性会越过一个
  没有顶点的字符。

<a id="servers"></a>

### 服务器

UI 管理器不会在专用服务器的世界中启动，包含 play-in-editor 服务器
（`UDreamUIManagerWorldSubsystem::ShouldRunForNetMode`）：服务器像 commandlet 一样，在没有管理器的情况下构建其关卡所持有的控件树，
不绘制任何内容，也不产生渲染器行或绘制行。你在那里请求管理器的自有代码
会得到 null，正如每个 DreamGUI 调用方所预期的那样。

<a id="c-of-your-own"></a>

### 你自己的 C++

- **顶点和着色器**：64 字节的顶点和 `UV4`，即 `UV2.x` 中的绘制槽（见渲染）。
- **`FRichTextParser` 和自定义样式**：`FDreamUIRichTextCustomStyleItemData::ApplyToRichTextParseResult` 接受一个标签
  顺序和一个样式名，两者都有默认值，因此调用方无需改动即可编译；`FRichTextParseResult` 具有 `PaintName`、
  `PaintOrder` 和 `bPaintRemoved`。
- **玩家索引**：导航作用域、动作触发器和动作栏上的 `GetUserIndex()` 不再是内联
  的，而是返回解析后的索引；请读取它，而不要读取 `UserIndex` 属性，后者可能为 -1。
- **弹出窗口**：`UDreamUIPopupLayer::Push` 对在其自身打开聚焦期间关闭的弹出窗口返回 false；在推入之后
  检查它或弹出窗口的状态。`FDreamPopupParams` 有一个 `TabBehavior`（Cycle、CloseAndContinue）和一个
  `OnClosing` 回调，在弹出窗口开始关闭而焦点仍在其内部时被调用。`EDreamPopupDismissReason`
  新增了 `Tab`，追加在末尾，供根据原因进行分支的代码使用。该层具有 `DismissAll(UserIndex, Reason)`，模态窗口
  会为其玩家调用它，以及 `GetHomeSlot(Widget)`，即被提起的控件在家时所拥有的面板槽的副本。
- **导航**：你自己的容器如果带有 `TabNavigation` Once，则通过重写
  `UDreamWidget::ResolveTabEntry` 来指定 Tab 进入它的位置；你自己的控件通过实现
  `IDreamUITabSwitchTarget`（`DreamGUIInput`、`Interaction/DreamUITabSwitchTarget.h`）来响应肩键；`UDreamUIActionBar::GetPrompts`
  列出该栏显示的内容。
- **TextAnimation**：`UDreamMeshModifierTextAnimation_Property::ApplyProperty` 不再是纯虚函数。它转发到
  新的 `ApplyPropertyToCharacters`，由内置属性重写它；你自己对 `ApplyProperty` 的重写
  仍然有效。
- **UI 管理器**：`ShouldRunForNetMode(ENetMode)`；绘制行（`GetPaintRowsTexture`、`AcquirePaintTextRow`、
  ...）；`GetPaintRowsMemoryInfo` 用于内存报告。
- **来自 2.0 的变更**：`IDreamUIRendererPrimitive::DreamUI_CollectRenderData` 接受一个 `FDreamUIPrimitiveDataArray&`；
  `UDreamWidget::MarkTransformChanged` 和 `UpdateObjectToWorldTransform` 已移除；`UUIDropdown::CreateBlocker`（一个
  虚函数）已移除；`RestoreFocusForTopScope` 已移除；`SetBrush_Material` 已被 `SetBrushFromMaterial` 弃用；
  移动顶点或 UV 的自定义网格修改器必须通过 `ModifierWillChangeVertexData` 声明这一点，否则覆盖文本
  会被绘制到错误的位置。

<a id="packaging"></a>

### 打包

带彩色 emoji 的游戏会自带一个 emoji 字体，带 CJK 文本的游戏会打包 EFIGSCJK 预设（泰语、
老挝语、高棉语或缅甸语则用 All）：[FontsAndPackaging.md](fonts-and-packaging.md)。测试宿主的已打包文本冒烟测试
将已打包游戏的文本与在未烘焙内容上运行的同一构建进行比对（`Tools/TestHost/README.md`）。

<a id="1-before-you-open-the-project"></a>

## 1. 打开项目之前

1. **引擎 5.8。** 插件仅针对 5.8 构建；没有 5.7 分支。
2. **先把项目纳入源代码管理，** 或者复制一份。用新插件打开它无害 ——
   重定向在内存中重写名称，而不是在磁盘上 —— 但资产的第一次保存会把新名称写入
   其中，而那次保存之后无法回到旧插件。
3. **移除旧插件。** DreamGUI 放入之前，必须先移除 `Plugins/` 下的 LGUI 或 LexUI 文件夹：其
  类会以重定向映射*来源*的名称处于活动状态，而重定向永远不会应用于仍然能够解析的名称。
4. **将重定向块放入你的 `Config/DefaultEngine.ini`。** 1.0.0 不附带任何重定向（见
   [从 2.1 到 1.0.0](#from-21-to-100)）：将 [2.1.0 的 `Config/DefaultDreamGUI.ini`](https://github.com/TypeDreamMoon/DreamGUI/blob/3049561a1742ab59d83a404ee267d2f580e53589/Config/DefaultDreamGUI.ini)
   的 `[CoreRedirects]` 部分
   （提交 `3049561a`）复制到那里，复制一次即可。如果那里已经有一份较旧的块副本，请先移除它 —— 同一个
  旧名称对应两个不同新名称的重定向是一种错误，最先注册的生效。

<a id="2-install"></a>

## 2. 安装

克隆到项目的 `Plugins/` 目录，重新生成工程文件，构建：

```bash
git clone https://github.com/TypeDreamMoon/DreamGUI.git Plugins/DreamGUI
```

插件需要 `EnhancedInput`，它会自行启用。项目不需要其他任何东西：不需要引擎
源代码构建，不需要引擎私有头文件，不需要复制任何设置。

<a id="3-what-the-redirects-carry"></a>

## 3. 重定向携带了什么

来自 2.1.0 的块一旦放入项目的 `Config/DefaultEngine.ini`，就会由引擎在
第一个包加载之前应用。它包含 763 条重定向：

| 种类 | 条目 | 覆盖内容 |
| --- | --- | --- |
| Package | 4 | 从 `/LGUI/` 内容挂载点到 `/DreamGUI/`，以及 `/Script/LGUI`、`/Script/LGUIEditor` 和 `/Script/LTween` 模块到它们的 DreamGUI 名称 |
| Class | 395 | 每个 LGUI/LexUI 类；被类模型取代的 prefab 词汇；控件重命名（`UIButtonComponent` → `UIButton` 及其十三个同类）；模块拆分从核心移出的每个类，从其旧的 `/Script/DreamGUI` 名称到其模块 |
| Struct、Enum | 96、124 | 同上，针对数据 |
| Function | 74 | 蓝图调用或重写的、被重命名或移动的函数 —— `UDreamUIBehaviour` 的 `Update` → `Tick`、`UDreamUINavigationScope` 失去 `On` 的事件 |
| Object | 70 | 随类一起被模块拆分移动的委托签名 |

**然后重新保存，并移除该块。** 每次加载需要它的资产时都会应用一次重定向。一旦
项目打开且 UI 看起来正常，就重新保存使用 DreamGUI 的资产（打开它们后 *File > Save All*，或
对内容运行 `ResavePackages` commandlet），使它们自行命名当前类型，然后从 `Config/DefaultEngine.ini` 中删除该块：
之后不再需要它，而且 1.0.0 不会保留自己的副本。

<a id="4-what-no-longer-exists"></a>

## 4. 已不存在的内容

这些没有重定向，因为已经没有可供指向的目标。仍然命名其中之一的资产
会在加载时丢失该部分，引擎会记录一条警告并指明它。

- **根蓝图** `WorldSpaceRoot_DreamRenderer`、`WorldSpaceRoot_UERenderer`、`ScreenSpaceRoot` 和
  `DreamWorldSpaceRaycasterSource_Mouse`。放置了其中一个的关卡会丢弃该 actor：将控件蓝图
  再次拖入关卡，作为你自己 actor 上的 `UDreamWorldWidgetComponent`，或通过关卡
  编辑器的拖放。
- **`UDreamWidgetPresenterComponent`。** 抽象基类 `UDreamWidgetPresenterComponentBase` 仍然保留；
  你要放置的是 `UDreamWorldWidgetComponent`。
- **`UDreamWorldSpaceRaycasterBase` / `ForWorldTrigger` / `Source` 系列** —— `UDreamWorldSpaceRaycaster`
  吸收了其全部。
- **插件设置** `ScreenSpaceRootClass`、`WorldSpaceRootClass`、`WorldSpaceUERendererRootClass`、
  `WorldSpaceRaycasterSourceClass` 和 `bLegacyTouchPointerIds`（见第 5 节）。
- **Lex 布局系列** —— `ULexLayoutContainerFlexBox`、`ULexLayoutContainerGrid`、`ULexLayoutSelfFlexBox`、
  `ULexLayoutSelfGrid` 和 `ELexUILayoutMode` 开关。UMG 形状的面板取代了它们；由其中之一
  排布的树需要手工重新排布。
- **prefab 机制**及其类型，在上述任何内容之前就被控件类模型取代了。
- **两个恢复早期渲染器行为的控制台变量** —— `r.DreamUI.MaterialWrappers` 和
  `r.DreamUI.RTDrawer` —— 以及它们所恢复的行为（见第 5 节）。

<a id="5-behaviour-to-check"></a>

## 5. 需要检查的行为

资产的表现可能与以前不同，即使没有任何重命名。任何运行过
早期版本的项目都值得一看：

<a id="input-1"></a>

### 输入

- **指针 id 有了区间。** 鼠标是指针 0，手指是 100 加它的序号，脚本造出来的 id 从 1000 开始。以前一根手指就是它自己的序号，于是第一根手指和鼠标是同一个指针，抢悬停和选中。按手指序号读触摸指针的代码改问 `DreamUIPointerIds::ForTouch`；找回旧 id 的那个设置没有了。
- **Enhanced Input 预设按原样绑定它拿到的动作。** 以前它玩的是这些动作的运行时副本，每帧改写副本的 `bTriggerWhenPaused`。现在暂停按事件判断：游戏暂停且 `UDreamUISettings::bScreenSpaceUIAffectByGamePause` 开着时，预设丢掉到达的输入。要让暂停那一帧的点击到得了，它的动作必须在暂停时也触发 —— 插件自带的 `IA_*` 动作都是；预设绑定的你自己的动作要设 `bTriggerWhenPaused`。
- **焦点和文本输入属于某个玩家。** 分屏的第二个玩家自己聚焦、自己打字。`UUITextInput::GetActiveTextInputForPlayer` 按玩家回答；`GetActiveTextInput` 跨世界回答。
- **字符通过游戏视口客户端到达文本框。** 用 `UDreamGameViewportClient`，或者在你自己视口客户端的 `InputChar` 里、控制台之后基类之前调用 `DreamUITextInputRouter::RouteViewportCharacter`（文档站上的[安装](https://gui.toolchain.64hz.cn/en/docs/start/installation)两种方式都有）。
- **Slate 输入源是可选的，默认关。** *Project Settings > Plugins > Dream GUI > Input > Use Slate Input Source* 在视口之前听每一个指针、按键和摇杆，让 UI 在引擎自己的 UI-only 输入模式下也能工作；开着时预设 Actor 让位，`SlateInputConsumePolicy` 决定它从游戏那里扣下什么。

<a id="rendering-1"></a>

### 渲染

- **画布不再自己造材质实例。** 它给材质的参数 —— 主纹理、字体纹理、它的数据纹理 —— 通过这个材质在渲染线程上的一个代理来回答。你交给控件的材质实例也这样回答，而且从不被写入，所以你在它上面设的参数始终是你的。画布绘制用的东西没有一个是关卡里的对象，也不会被拷进一次 play session 或一次粘贴。
- **渲染目标画布不管有没有东西在渲染它的世界，都会绘制。** 它的分段改变后，由自己的 render command 和一张自己的渲染图绘制，而不是在它世界的某个视图里。
- **背景模糊、像素化和像素排序**看起来和以前一样，修了两处：多重采样画布上的全尺寸模糊现在能显示（以前被画布的 resolve 吞掉了），输出到目标的全尺寸模糊现在模糊的是屏幕（以前模糊的是一张空纹理）。

<a id="seeing-it"></a>

### 看清楚

`DreamUI.Capture` 把视口和每块渲染目标画布写成 PNG，`DreamUI.Stats` 逐阶段打印 UI 的开销 —— 足够拿迁移后的界面和以前对比。有东西画错或没画出来，开着 `r.DreamUI.VerifyPartialPrepare 1` 再跑，把它触发的 ensure 附进报告。

<a id="6-c-of-your-own"></a>

## 6. 你自己的 C++

**模块。** 运行时现在是几个模块。你用到哪些模块的类型，就把哪些加进 `Build.cs`：`DreamGUIRenderer`（绘制、效果的渲染代理）、`DreamGUI`（控件、visual、画布）、`DreamGUIInput`（事件系统、射线检测器、导航、视口客户端）、`DreamGUIControls`（`Dream*` 控件和 `UI*` 行为）、`DreamGUIExtensions`（线、环、效果、渲染目标辅助）。什么在哪里，见 README 的[模块表](../../../README.md#modules)。

**include。** 除 README 的[include 表](https://gui.toolchain.64hz.cn/en/docs/guides/migration)里那些之外，每个头文件都保留了原来的路径。

**调用。** README 的调用表列出了随模块拆分挪走的东西。自那以后：

| 原来 | 现在 |
| --- | --- |
| `UDreamVisual::OnMaterialInstanceDynamicCreated(UMaterialInstanceDynamic*)` | `UDreamVisual::AddMaterialParameters(FDreamUIMaterialParameters&)`：给出你的 visual 的材质需要的参数，画布替它的代理回答 |
| `DreamUITextInputRouter::RouteCharacter(TCHAR)` | 视口客户端里用 `RouteViewportCharacter`，或 `RouteCharacter(WorldContext, UserIndex, Character)` |
| `UDreamGUISettings::bLegacyTouchPointerIds` | 没了；`DreamUIPointerIds::ForTouch` 给出一根手指的指针 id |
| `ADreamEnhancedInputEventSystemActor::GetOriginalAction` | 没了：Actor 持有的动作就是它拿到的那些 |
| `FDreamUIRenderer::IsRenderTargetDrawerEnabled` | 没了：渲染目标画布总是由 `DrawRenderTarget_GameThread` 绘制 |
| `UDreamUIMeshComponent::OnSceneProxyCreated` | `OnRenderRootCreated`，带着网格的 render root 广播。root 持有画布的分段，比为它创建的 scene proxy 活得长 |
| `FDreamVisualPostProcessRenderProxy::RenderMeshOnScreen_RenderThread` 接受 RHI 纹理 | 改接受渲染图的纹理（`FRDGTextureRef`）。你自己的效果用 `ReadScreen_RenderThread` 和 `GrabRegion_RenderThread` 读屏幕，在 `CreateWorkTexture` 给的纹理里干活，用 `WriteBack_RenderThread` 写回 |

**代码里的设置。** `UDreamGUISettings` 是 *Project Settings > Plugins > Dream GUI*；`UDreamUISettings` 是 *Project Settings > Plugins > DreamUI*。两者都在用到的地方读，所以运行时改一个值，从下一帧起生效。

<a id="7-when-something-does-not-come-across"></a>

## 7. 有东西没过来时

插件自带的自动化测试会加载插件保存的资产，并拿它们和保存时的内容对照（`DreamGUI.Compatibility.*`、`DreamGUI.Assets.*`）；redirect 段本身由 2.1.0 的测试检查过：每一条都到达了引擎、没有旧名字仍是活着的类型、没有一条跳进另一条 redirect、每个新名字都存在。放着这一段时你的某个资产仍加载失败，值得带着引擎为它打的那条警告报告上来：DreamGUI 改了名却没给 redirect 的名字，是一个 bug。
