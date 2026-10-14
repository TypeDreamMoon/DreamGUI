---
title: 从开发版升级
description: 项目在 2.1 或 2.0 开发版上时，升到 1.0.0 要知道什么：哪些东西看起来会不一样、哪些默认值变了、哪些开关能找回旧行为，以及自己的 C++ 要改什么；还有从 1.0.0 跟到 main 时要留意的几处。
---

1.0.0 是第一个公开版本。在它之前，这个分叉的开发版编号是单模块的 1.x、然后 2.0.0 和 2.1.0（`main` 上的提交 `2416e3f8`、`3049561a`）。
公开编号从 1.0.0 重新开始，所以这一页里的 2.0、2.1 都是开发版，**比 1.0.0 旧**。描述文件里的整数 `Version` 一直在涨 ——
2.1.0 是 3，1.0.0 是 100 —— 所以引擎永远不会把 1.0.0 当成两者中较旧的那个。

- 在 **1.0.0** 上、要跟 `main`：读[从 1.0.0 到下一个版本](#从-100-到下一个版本)。
- 在 **2.1** 上：读[从 2.1 到 1.0.0](#从-21-到-100)。
- 在 **2.0** 上：先读[从 2.0 到 2.1](#从-20-到-21)，再读从 2.1 到 1.0.0。
- 从 LGUI、LexUI 或单模块 1.x 构建过来：先走[迁移](migration.md)，再把这一页从头读到尾。

每个版本新增了什么在[更新日志](../changelog/index.md)。

<a id="从-100-到下一个版本"></a>

## 从 1.0.0 到下一个版本

`main` 上 1.0.0 之后的改动（还没发布）什么都没改名：1.0.0 保存的资产照原样加载，`.dui` 文件照旧编译——`viewmodels`、成员路径、
`+=` 和 `=` 都是和旧语法并存的新语法。要留意的有这几处：

- **只读 FieldNotify 变量的绑定不再每帧读。** 一个 Blueprint 重新编译之后，它的这类绑定改为订阅变量，变量宣布变化时才更新。C++ 里给 FieldNotify
  变量赋值却不广播的代码，以前靠每帧轮询看起来也能工作，现在显示的值不会再跟着变。改用 `DREAM_VM_SET` 或 `BroadcastFieldValueChanged`；找不出是哪一个时，
  `DreamUI.Binding.ForcePoll 1` 把轮询找回来，`DreamUI.Binding.Dump` 列出每个绑定是怎么驱动的（见[绑定什么时候更新](../language/bindings.md#绑定什么时候更新)）。
- **Create Dream Widget Of Class 不在蓝图菜单里，也不在 Python 里了。** 已经调用它的图照常编译，新图用 Create Dream Widget 节点。调用
  `create_dream_widget_of_class` 的编辑器脚本要换成别的调用，比如 `add_widget_of_class_to_viewport`，或者自己从 C++ 暴露的函数。
- **调色板的分类改了名。** 扩展代码里拿 `FDreamUIControlDescriptor::Category` 和 "Controls"、"Post Process"、"Extensions" 或旧分类名比较的，
  改用 `DreamUIPaletteCategory` 里的名字。注册键没变，收藏不受影响。
- **Add Component 会拒绝放不上去的组件。** 以前加得上、但什么都不做的组件，现在选择器里看不到，粘贴和拖放也会被拒绝。项目自己的组件有要求时，
  在编辑器模块里用 `FDreamUIComponentSupport` 注册规则（见[组件](../tools/designer.md#组件)）。
- **缩略图。** 资产里已经存着的线框缩略图，要等资产下次修改并保存才会换成界面本身。

<a id="从-21-到-100"></a>

## 从 2.1 到 1.0.0

两者之间什么都没改名，所以 2.1 按现在的类型名保存的资产照原样加载，打开项目之前也不用做任何事 —— 只有一个例外，见下面。1.0.0 给 `.dui` 加的东西 —— 纯 `.dui` 写的组件、`use … as`、
`if`、`for`、`rows`、`@slot { … }` 和 `@fill` —— 都是和旧语法并存的新语法，一个 2.1 的文件照旧编译。

有三处会看起来不一样，每一处都是一个项目可能绕过的 bug 被修掉了：

- **一个刷子材质，如果它用到的画布参数只有 `DreamUI_IsRenderByDreamUIRenderer`，现在会被告知渲染器。** 以前这样的材质会被 DreamGUI 的渲染器
  预乘两次，边缘画得偏暗。为此做了补偿的材质现在会画得偏亮：把补偿去掉。
- **交给图片的材质实例，在第一次绘制之后设的参数现在会显示出来。** 为了绕开这一点而每帧重建的实例，可以留着改为直接设参数。
- **在屏幕和渲染目标画布上，跟在一个用材质绘制的图片后面的文本和矩形块，现在会重新正常绘制。** 为此把文本挪出那张图片之后合批的布局不再需要了。

另外：焦点框现在贴合它标出的控件，并且可以按控件拒绝（`UUISelectable::bUseFocusRing`，默认开；运行时用 `SetUseFocusRing`）；
有自己蓝图处理逻辑的焦点框类保留它自己的布局。`NavigationSelectionClass` 不设时就是哪里都没有焦点框，不再每次焦点变化都打警告。
`DreamTween.MaxStepSeconds` 是新的控制台变量，默认关（0）：一帧比它长时，世界驱动的补间只前进这个上限，手动 tick 不受限。

**插件不再带 CoreRedirects。** 2.1 在 `Config/DefaultDreamGUI.ini` 里带着 763 条；1.0.0 一条都不带，这个文件也没了。
只靠其中某一条才能加载的资产 —— 按 LGUI 或 LexUI 保存的、早期 1.x 构建保存的、模块拆分之前保存的、控件或事件改名之前保存的 ——
必须重存一次，否则单用 1.0.0 打开时，它会丢掉所有引用旧类型的部分。2.1 之后什么都没改名，所以这些 redirect 原样适用于 1.0.0，
项目只在重存期间把它们借来：

1. 把 [2.1.0 的 `Config/DefaultDreamGUI.ini`](https://github.com/TypeDreamMoon/DreamGUI/blob/3049561a1742ab59d83a404ee267d2f580e53589/Config/DefaultDreamGUI.ini)（提交 `3049561a`）里的 `[CoreRedirects]` 段拷进项目的 `Config/DefaultEngine.ini`。
2. 用 1.0.0 打开项目，把每个用到 DreamGUI 的资产重存一遍 —— 打开它们后 *File ▸ Save All*，或者对内容跑 `ResavePackages` commandlet。
3. 再从 `Config/DefaultEngine.ini` 里删掉这一段。

从没有过这种资产的项目 —— 在 2.1 上起步的，或者之后已经重存过的 —— 三步都可以跳过。这些 redirect 覆盖什么，
见[迁移](migration.md)的第 1 到 3 节。

<a id="从-20-到-21"></a>

## 从 2.0 到 2.1

什么都没改名，所以不需要 redirect，打开项目之前也不用做任何事。下面是之后可能看起来或表现得不同的地方，以及你自己的 C++ 要改什么。

<a id="升级之前"></a>

### 升级之前

- **被 2.1 重存过的资产在 2.0 里打不开。** 2.0 拒绝用更新的 `FDreamGUIObjectVersion` 保存的包。和任何升级一样，把项目放在版本控制下。
- **没有新增 redirect**，仍然是 763 条，2.0 保存的每个名字都能加载。
- **`UDreamDialog::DimmerBehaviour` 没了**，也没有 redirect。它对蓝图只读，读过它的蓝图会编译失败：遮罩不再是一个按钮，
  由 `bShowDimmer` 和 `bCloseOnDimmerClick` 配置。

<a id="加载时会改变的资产"></a>

### 加载时会改变的资产

- 字体的后备列表（`FallbackFontArray`）变成默认设置的 `Fallbacks` 条目：所有码位、任何语言、缩放 1、不优先于字体自己的字面。
- emoji 数据的 key 补上了完整序列。
- 在 `SdfSource` 出现之前保存的距离场字体，改为加载到轮廓多通道距离场上，而不是被强制放到单通道距离场：拐角更锐利、图集是 BGRA、
  图集内存大约四倍。要保留旧的样子，把 `SdfSource` 设回去。
- 颜色等于自己 `BrushColor` 的 Border visual，注册时被重置为白色。
- `UDreamMeshModifierPositionAsUV` 的 UV 通道从 2.0.0 起默认是 0，通道 1（画布自己的）会被拒绝并打警告。2.0 之前做的、依赖旧默认值 1 的资产要自己设通道。

<a id="默认值变了"></a>

### 默认值变了

一个把属性留在默认值的资产从没保存过它，所以它会拿到新的默认值；2.1 新增的属性从默认值开始。

- **默认字体**是 Roboto（带真实的粗体和斜体字面），DroidSansFallback 负责 CJK，在多通道距离场上：每段用它的文本都会重新排版，宽度会变。
- **小字号文本**（`UDreamGUISettings`）。2.0 的所有文本都从距离场画。现在小的屏幕文本从设备像素网格上的 hinted 覆盖率字形画
  （`bSmallTextCoverage` 开，最大到 `SmallTextMaxPixelSize` 20，`SmallTextContrast` 1），而 `bSmallTextCorrection`（开）会加深在这些尺寸下
  仍从距离场画的部分。带描边、发光或底衬时，字面从覆盖率字形画 —— 描边细于 2 个设备像素时不 hint（`SmallTextEffectFace` 为 Auto）——
  这些效果仍从距离场画，并挪到和字面对齐。在渲染层里，层静止 3 帧之后从覆盖率画，层在动时从距离场画。字面的柔化和膨胀会让文本留在距离场上。
  `DreamGUI.Text.SmallTextCoverage 0` 在所有地方关掉覆盖率，包括设成 On 的字体。
- **文本**：连字开（`bLigatures`），`TabSize` 8，`bPreferColorEmoji` 开。
- **渲染**：`RenderLayer` 为 Auto；`bShowEffectWhenDisabled` 开，所以禁用的 Border 的背景以 45 % 绘制。
- **滚动**：`UUIScrollView::CanScrollInSmallSize` 关，短列表不再回弹；`bAllowRightClickDragScrolling` 和 `bEnableTouchScrolling` 开。
  滚动条不再是焦点目标（`UUIScrollbar` 的 `bCanNavigateHere` 关；打开它就能把滚动条放回导航），它的箭头永远不是；列表或滚动框给自己建的滚动条永远不是 Tab 停靠点。
- **列表、平铺和树**是一个 Tab 停靠点（`TabNavigation` 为 Once）：Tab 能进就从选中的行进入并把它滚进视野，否则从第一行进入（Shift+Tab 从最后一行）；再按 Tab 离开列表。
- **玩家索引**：`UDreamUINavigationScope::UserIndex`、`UDreamUIActionTrigger::UserIndex` 和 `UDreamUIActionBar::UserIndex` 默认是 -1，
  即控件所属的玩家；以前是 0。手设的索引仍然优先，`GetUserIndex()` 回答实际生效的索引。单人时什么都不变；分屏时第二个玩家的界面听第二个玩家的。
- **Tab 与焦点**（*Project Settings ▸ Plugins ▸ Dream GUI*）：

| 设置 | 默认 |
| --- | --- |
| `bTabNavigation` | 开 |
| `TabOrder` | Hierarchy |
| `bTabWrapsAtScreenEnd` | 开 |
| `bTabStartsTextEdit` | 开 |
| `bFocusVisibleOnlyFromKeys` | 开 |
| `bHideCursorOnGamepad` | 开 |
| `bUsePlatformAcceptBack` | 开 |
| `InputModeWithoutScope` | All |
| 下拉框（`UUIDropdown`、`UDreamDropdown`）的 `bTabCommitsHighlightedRow` | 开 |

- **按键**（同一页）：`ConfirmKeys`、`BackKeys`、`DirectionKeys`、`PageKeys` 和 `ExtentKeys` 装的是 2.0 的表，`PageKeys` 加上了扳机键
  （LT 和 RT 翻页）；`PreviousTabKeys` 和 `NextTabKeys` 是肩键（LB 和 RB 切标签）。

<a id="找回-20-行为或度量新行为的开关"></a>

### 找回 2.0 行为、或度量新行为的开关

- `TabOrder = LegacyGeometric`（项目设置）：Tab 和 Shift+Tab 像 2.0 一样按几何位置移动，只在这一个版本里提供。
- `bSmallTextCoverage` 关（项目设置），或 `DreamGUI.Text.SmallTextCoverage 0`：所有文本都从距离场画，同 2.0。`SmallTextEffectFace = Field`
  只让带描边、发光或底衬的小字号文本完全留在距离场上。
- `DreamGUI.Text.SmallTextCoverage`（-1 跟随项目设置；0 对每个字体关，包括设成 On 的；1 对每个交给项目决定的字体开）和
  `DreamGUI.Text.SmallTextMaxPixelSize`（大于 0 时，对每个没有自己上限的字体替换项目设置）作用于每个字体，改了会重绘每段文本；
  `SmallTextRepaintBudgetPerFrame`（512）限制一个世界一帧内重绘到覆盖率字形上的文本数。
- `DreamGUI.Text.IncrementalParse`、`DreamGUI.Text.IncrementalMeasure` 和 `DreamGUI.Text.InPlaceDisplayList` 各自关掉增量布局的一部分，
  退回整段排版；`DreamGUI.Text.VerifyIncremental`（Shipping 构建里没有）拿增量布局和全新布局比对，指出哪里不同。
  同一段文本的增量布局和全新布局逐元素相同，这些开关只拿时间做交换。`ElementsParsed` 计的是一次编辑真正拼进去的元素，不算被拒绝的窗口扔掉的读取。
- 2.1 新增、默认关、待测量后再定：`bFieldTextCorrection`（`SmallTextMaxPixelSize` 以上的距离场文本也拿到小字号文本的对比度和线性光混合，
  让字重在分界处不跳）、`DreamGUI.Text.SmallTextOnMove`（小字号文本离开像素网格移动时怎么办：0 每次移动都从覆盖率重绘，1 保留覆盖率四边形，
  2 从距离场画；1 和 2 在静止 3 帧后从覆盖率重绘），以及 `DreamGUI.Scroll.SnapToDevicePixels`（2D 画布上的滚动视图按整设备像素移动内容）。

<a id="文本"></a>

### 文本

- **断行跟随游戏的文化**：行与词的迭代器按当前文化创建，文化变化时重建。2.0 跟的是操作系统的语言。有 CJK 文本的游戏必须带对应的 ICU 数据打包
  （EFIGSCJK 预设，泰文及其邻近文字用 All）：见[字体与打包](fonts-and-packaging.md)。
- **效果随文本淡出，而不是随它的颜色。** 描边、发光、底衬随渲染不透明度、内容着色的 alpha 和 TextAnimation 的 alpha 淡出，不再随文本自身颜色的 alpha。
  被着色或 TextAnimation 淡出的文本现在连描边一起淡出；空心字 —— 透明颜色加描边 —— 保留描边。
- **绘制（paint）。** `FDreamTextStyle` 有 `FacePaint`、`OutlinePaint`、`OverlayPaint`、`OverlayBlend`、`PaintBoxHorizontal` 和
  `PaintBoxVertical`，全部关，所以设置之前什么都不会画得不同。字面绘制取代文本自己颜色在字面上的位置，就像 CSS 的 `background-clip: text`；
  被绘制的文本里的 `<color>` 段是纯色，自定义样式的 Multiply 给渐变着色。富文本有一个 `<gradient=Name>` 标签，受新过滤标志 `Gradient` 控制：
  `RichTextTagFilterFlags` 被保存成不含它的掩码的文本，会把这个标签当文字显示。富文本自定义样式条目有 `paintType`（KeepOrigin、Set、None）
  和 `paint`。渐变的 CSS 按 Chrome 的方式读，只有两点例外：`to <corner>` 不管盒子形状都取正方形的角度（45deg、135deg……）；颜色名用 CSS 的
  （`green` 是 #008000，而 `<color=green>` 是 #00FF00）。编辑一个 paint 永远不会让文本重新排版 —— 换渐变只写它的行，开关 paint 会重绘 ——
  撤销除外，撤销会重新排版并重绘。
- **制表位**按文本里空格的测量方式测量自己的空间 —— 在缩放的画布下按设备尺寸 —— 所以 `TabSize` 个空格正好落在一个停靠点上。
- **渐变盒子**：文本块横向跨越内容盒，纵向从第一行顶部到最后一个**可见**行的底部（被截断隐藏的行不算）；字母全是纯色的富文本段不分到 `Run` 盒子的一份。
  渐变段里的 `<color>` 是纯色；渐变段里自定义样式的 Multiply 给渐变着色。
- **编辑器里按文化断行**：ICU 在 Windows 上是静态库，所以编辑器给 DreamGUI 一份自己的拷贝；2.1 让这份拷贝指向引擎的 ICU 数据
  （`Engine/Content/Internationalization`），于是编辑器像打包游戏一样按游戏的文化断行。读不到数据时，日志说一次，由引擎的默认文化迭代器顶上。
- **不经 `MF_DreamUI_Shade` 着色的自己的材质**拿不到覆盖率字形，emoji 从 `EmojiData` 或单色字面画，而不是把彩色位图当距离场采样，渐变按顶点色绘制。
  DreamGUI 自己的材质带一个 `DreamUI_ShadeMarker` 参数，表明它们经过这个函数着色。
- **2.0 已有的变化**（给跳过了它的说明的项目）：行高百分比用 CSS 的半行距；右到左段落里 "Left" 意思是起始端；bidi 重排行（L2）；字间距不加在
  连写文字内部，且像 Chrome 一样计入行尾；粗体每个字簇前进一次，只对合成粗体；下划线和删除线用字体的度量、按段绘制；描边和硬阴影是斜接的；
  效果颜色按 sRGB 解码、画得更暗；emoji 优先级变了；`<lang>` 标签会被解析（Language 过滤标志关时它们是文字）；缩放过的后备字体会撑高行盒；
  制表符前进到停靠点。

<a id="字体"></a>

### 字体

- 嵌入的字体文件（`CustomFontFile`，非外部）在烹饪时会重新读取，烹饪时找不到可嵌入的东西是错误，而不是发出一个没有字面的字体。
- 只要还有文本从覆盖率字形画，字体就保留这些字形的图集格子；隐藏、折叠或未激活的文本会放手，被收回的话在绘制前重画。超过格子预算
  （`MaxCoverageCells`，4，在 *Project Settings ▸ Plugins ▸ DreamUI*）的两倍时，新的小字号字形会等待，期间从距离场画，每个字体一条警告。
- 字体的图集纹理和它的文本列表不再记入撤销事务；位图字体在撤销和重做时重新加载。

<a id="布局"></a>

### 布局

自 2.0 起：面板里的文本按面板将要给它的宽度测量；折叠的子节点测量为 0；`OnDimensionChanged` 触发一次而不是两次；网格跨越、居中的不对称内边距、
wrap box 的填充、scale box 的模式和声明的缩放都跟 UMG 一致；滚动框有填充插槽和前后内边距，没东西可滚时不过度滚动，折叠内置滚动条并隐藏空闲轨道。

<a id="控件焦点与导航"></a>

### 控件、焦点与导航

- **Tab** 是下一个、Shift+Tab 是上一个，都要求没按 Ctrl、Alt 或 Cmd；按着其中之一时，Tab 只交给按键绑定，而绑定总是先拿到每个键。
  顺序按层级，兄弟之间按 `TabIndex`；selectable 或控件上显式的 Next / Prev 链接仍然优先，除非它的目标拿不了焦点或位于 Tab 被限制的范围之外。
  `TabOrder = LegacyGeometric` 在这个版本里找回 2.0 的几何顺序。Tab 停留在最上层弹出框里，否则在遮罩开着的模态或对话框里，否则在玩家的屏幕上 ——
  焦点在一个画在打开的弹出框前面的层上时，停在那个层上。共享屏幕上另一个玩家的弹出框永远不是停靠点。列表、平铺和树是一个停靠点；看不见的停靠点会被滚进视野；
  提示框、弹出面板和滚动条永远不是停靠点；被设为不可聚焦的控件不是 Tab、方向键或逐控件规则的落点。只要玩家有一个 Tab 停靠点可去，Tab 就算被处理了，
  即使在不回绕的屏幕尽头。
- **文本框**：Tab 提交编辑 —— 和离开文本框时按 `bSubmitWhenDeactivate` 提交一样 —— 然后移到下一个，由 Tab 到达则开始编辑；多行文本框只在
  `bTabTypesTabCharacter` 开时输入制表符（此时 Ctrl+Tab 离开它）；IME 组字期间 Tab 归 IME；Alt+Tab 和 Cmd+Tab 永远不归文本框，Ctrl+Tab
  也不归，除非它输入制表符。文本框只在用到 Tab 时才拿它，所以 `IgnoreKeys` 不再需要包含 Tab 来防止 Tab 被吞。正在编辑的文本框上，手柄的确认键提交它；开始编辑的那一下不算。
- **下拉框与菜单**：打开的下拉框里按 Tab，提交高亮的行（`UUIDropdown` 和 `UDreamDropdown` 上的 `bTabCommitsHighlightedRow`，默认开），
  关闭它并移到下一个控件，Shift+Tab 到上一个；菜单里按 Tab 关闭整条菜单链并继续（弹出框的 `TabBehavior` 为 CloseAndContinue）。其它弹出框把 Tab 留在里面（Cycle）。
- **焦点外观** —— Focused 状态和焦点框 —— 只在焦点来自按键或手柄时显示，或者来自脚本、而上一次输入是按键或手柄时。鼠标点击不再显示它，
  鼠标悬停也不再把焦点框拿走。只要焦点在画，焦点框就在每个屏幕页上显示，没有 presenter 的页也一样，作用域在屏幕打开时给出的焦点也一样。
  滚动框的 `OnFocusReceived` 和 `OnFocusLost` 跟随真正的焦点，而不是它的 Focused 外观。
- **导航作用域有了输入模式**（`InputMode`：All、Menu、Game）。Game 在这个作用域处于活动时，关掉这个玩家的 DreamGUI 内置导航、确认、返回、翻页和切标签；
  绑定、指针和打字仍然工作。没有作用域也没有焦点时，导航键只在玩家自己的屏幕空间画布上找落点，从不看世界空间 UI 或别的玩家的屏幕。
- **弹出框与对话框**：显示一个模态时先关掉这个玩家打开的弹出框，模态关闭时焦点回到它们的打开者。落在一个画在打开的弹出框前面的层上的按下，
  会关闭弹出框并且仍然到达那个层 —— 不再被吞掉 —— 返回键先给这样的层，再给弹出框。独立对话框恰好在遮罩开着时限制导航，不管 Close on Back 怎么设：
  不带遮罩、开着 Close on Back 的对话框不再限制导航。菜单锚点的 `OnMenuOpenChanged(false)` 只在它宣布过的一次打开之后触发。被提起的面板菜单保留它插槽的
  内边距、nudge 和尺寸约束，回家时插槽完整回来；弹出框打开时被给的画布，在那时被拿掉，或者在它原本就有画布时还回原来的排序、覆盖和追踪通道。
  独立对话框的作用域属于它所属的玩家。
- **滚动**：翻页、Home、End 和右摇杆从聚焦的控件本身开始，所以聚焦的滚动框滚动它自己；扳机键翻页。
- **肩键**切换活动标签视图的标签，跳过禁用的、在两端回绕，动作栏为它们显示 "Previous tab" 和 "Next tab" 提示。
- **逐控件导航规则**（`UDreamWidgetNavigation`）留在限制导航的对话框和画布之内，Next 或 Prev 上的 Wrap 规则被遵守，规则没设的方向由同一控件上的 selectable 回答。
- **2.0 已有的变化**：弹出层取代了点击拦截器；在弹出框外的点击被消耗；返回键只关最上层的弹出框；焦点回到打开者；下拉框在选中行上打开；
  对话框压入一个返回作用域；确认不再把焦点拉回去；禁用的开关、标签和下拉框忽略点击；滑条和下拉框只认左键；文本框按 UTF-16 偏移编辑。

<a id="输入"></a>

### 输入

- **按键和手柄作用于焦点。** Enter、空格和手柄的确认键按下聚焦的控件，而不是悬停的；什么都没聚焦时确认什么都不按 —— 以前会按默认控件 ——
  也不动鼠标的悬停。鼠标只有真的移动之后，悬停才移动导航高亮；新指针从视口外开始。
- `UDreamEventSystem::ActivateNavigationInput(PointerId, DefaultWidget)` 和 `SetHighlightedComponentForNavigation` 不再决定按键从哪里开始：
  按键从玩家的焦点开始。用它们来放起点的代码改为设置焦点（`UDreamWidget::SetFocus`）。
- 按键表一变就重新读取，所以运行时的重映射从下一个键起生效。平台的确认和返回按钮在运行时读取（Switch 会交换它们），平台用法相反的面键从另一张表里去掉。
- 在 DreamGUI 的 UI-only 输入模式（`UDreamUIInputModeLibrary::SetInputModeUIOnly`）下，玩家用手柄时光标隐藏，用鼠标或键盘时回来，
  玩家在手柄上时这个模式一开始就隐藏光标。其它情况下 DreamGUI 不碰光标。
- 当 Slate 的焦点在空的游戏视口上、而玩家有一个 DreamGUI 焦点或 Tab 停靠点时，Slate 自己的导航 —— Tab、方向键 —— 归 DreamGUI，所以它们都不会把焦点从视口带进 UMG。
- 手柄型号按最后使用的设备检测，每次设备变化都检测；型号变化时动作栏重建。
- 没有对应玩家的玩家索引不创建输入用户。
- **2.0 已有的变化**：一次点击需要在按下的控件上松开；松开交给接住按下的那一方；指针不再停在 (0, 0)；Slate 输入源会检查 UMG 的遮挡和键盘焦点。

<a id="渲染"></a>

### 渲染

- **距离场文本以预乘方式合成它的图层**：下面什么都没有的地方保持自己的颜色。alpha 低于 1 的距离场文本 —— 半透明颜色、渲染不透明度、淡出、
  渐变色标 —— 不再朝黑色变暗；发光朝外缘更亮；视口里深色底上浅色文本的边缘和 Slate 画的一样饱满（以前更细），渲染目标里它们像深色文本一直以来那样在线性光下混合。
  黑色文本和不透明描边上的不透明文本看起来和以前一样。用 2.0 截的彩色、淡出或发光文本的图在边缘会有差别。
- **目标是 sRGB 时，多重采样画布在 sRGB 下混合**：MSAA 画布的边缘和半透明重叠略有不同，更接近同一画布不开 MSAA 的样子。
- **DreamUI 顶点是 64 字节**（以前 56），多了第五个纹理坐标 `UV4`，顶点属性 8，在 `TangentZ` 之后；其它字段的偏移和属性号都不变，
  `LEXUI_VERTEX_TEXCOORDINATE_COUNT` 仍是 4（`LEXUI_VERTEX_UV_CHANNEL_COUNT` 是 5，把 UV4 算在内）。材质用 `TexCoord(4)` 读它。
  文本为渐变绘制写它；其余一律写 (0, 0)。用构造函数、`SetNum` 或整个结构体拷贝得到的顶点是对的；未初始化地扩大顶点数组再逐字段填写的代码必须写 `UV4`。
  由 UE 渲染器绘制的世界空间画布带五个纹理坐标，以前是四个。
- **文本四边形在 `UV2.x` 里带一个绘制槽**：`UV2.x += 128 * slot`（`DreamTextQuadCode::SlotStride`），没有绘制的文本是槽 0，它的顶点和 2.0 写的一样。
  解码 `UV2.x` 的着色器或材质要先剥掉槽（`DreamUIText.ush` 里的 `DreamUIText_UnpackPaintSlot`）；改写文本四边形 `UV2.x` 的网格修改器要保留它
  （`DreamTextQuadCode::AddSlot(NewCode, DreamTextQuadCode::GetSlot(OldCode))`）。`DreamUI_ShadePixel` 保留旧签名，作为一个不画绘制的重载。
- **阴影、描边和长阴影修改器对彩色 emoji 的拷贝**用拷贝的颜色画它的剪影（以前画第二个全彩 emoji），拷贝永远不被绘制。阴影拷贝会让网格超出顶点预算时，
  网格不带阴影绘制，打一条警告；以前整个网格被丢掉。
- `MF_DreamUI_Shade` 以 `DreamUI_PaintDataTexture` 接收绘制行。
- 渲染统计里的上传字节每个顶点多 8。
- **2.0 已有的变化**：变换变化每帧通知一次（`r.DreamUI.DeferTransformNotifications`）；Auto 渲染层不做像素对齐；新的控制台变量
  （`DreamUI.Animation.LitePlayer`、`DirectEvaluation`、`DreamGUI.Text.ShapeCache`、`IncrementalLayout`、`r.DreamUI.*`）。

<a id="动画与补间"></a>

### 动画与补间

- **结束和被杀掉的补间会退役。** 退役的补间被标为垃圾，所以它的句柄读出来无效，持有它的 UPROPERTY 在下次垃圾回收时被置空。
  `Kill`、`ForceComplete` 和 `Goto` 对被杀掉或已退役的补间什么都不做，`Restart` 对已退役的什么都不做、只打一条警告。要在结束后重启一个补间，
  在它开始前 `SetAutoKill(false)`，或者在它自己的 `OnComplete` 里 `Restart` 它，这样它会继续运行。
- `Kill` 触发 `OnKill` 和 `OnComplete` 各一次。对还没开始的补间 `ForceComplete` 会先启动它：读取起始值，触发 `OnStart` 和 `OnCycleStart`。
  `Goto` 到结尾只完成一次，`Goto(0)` 应用起始值；反向的 yoyo 循环结束在进度 0；次数为 0 的 `RepeatCall` 什么都不调用；空的运行时曲线按线性缓动并打警告；
  时间缩放、延迟和增量都会被检查。
- 一个把自己的序列 seek 进自己的回调，嵌套到 16 层时被拒绝，日志里有一条错误。
- 颜色补间和 RectBlock 的 `*AlphaTo` 补间用浮点计算、四舍五入并钳制：过冲的缓动不再回绕，中途的值可能和 2.0 差 1。RectBlock alpha 的终值也可能差 1，
  2.0 是截断的：`BodyAlphaTo(0.5)` 结束在 128，以前是 127。普通颜色补间仍然正好结束在终值。
- 弹簧默认在静止时保持：`SetTarget` 唤醒它；要让它在静止时退役，杀掉它，或者在开始前 `SetAutoKill(true)`。
- 第二个补间 tick 辅助对象不再让补间速度翻倍。所有者没了的补间直接丢掉，不触发回调。布局动画和嵌入的 UMG 控件遵循和其它控件补间相同的暂停与时间膨胀规则。

<a id="扩展"></a>

### 扩展

- retainer box 在应用设置期间把它的画布切到渲染目标模式，之后放回原来的模式，在编辑器世界里什么都不做，没有 `DisplayVisual` 来显示它保留的内容时警告一次。
  在根画布上那就是整个画布：此后只能通过别处的一个 `DisplayVisual` 看到它。
- 静态网格 visual 拒绝顶点数超过一个分段能容纳的网格，报错（以前它截断索引），它的包围盒就在画布绘制它的地方。
- 渲染目标面（`UDreamUIRenderTargetGeometrySource`）在 StaticMesh 几何模式下、开着 `bOverrideStaticMeshMaterial` 时，只在游戏运行时把它的材质实例
  放进静态网格组件的槽 0。编辑器里不再这样做：那个实例是瞬态的，保存关卡会把 null 写进槽原本的材质，而且没有东西把它放回去。所以编辑器不再在那个网格上显示目标。
- 弧度为 0 的圆柱渲染目标面弯 1 度，负弧度的包围盒在它弯向的那一侧。
- 背景像素化把它的渲染目标交给代理，和模糊、像素排序一样；像素排序的 `SetMaxSortPasses` 把次数限制在 1 到 512。
- 歌词的 TTML 读取器在嵌套 256 层时停下并报错。
- 运行时设置的 TextAnimation 属性会开始，被它们取代的会停止；属性会跳过没有顶点的字符。

<a id="服务器"></a>

### 服务器

UI 管理器不在专用服务器的世界里启动，PIE 服务器也一样（`UDreamUIManagerWorldSubsystem::ShouldRunForNetMode`）：服务器像 commandlet 那样
在没有管理器的情况下构建它的关卡里的控件树，什么都不画，也不创建渲染器或绘制行。你自己的代码在那里请求管理器会拿到 null，所有 DreamGUI 调用方本来就这样预期。

<a id="你自己的-c"></a>

### 你自己的 C++

- **顶点与着色器**：64 字节的顶点和 `UV4`，`UV2.x` 里的绘制槽（见上面的渲染）。
- **`FRichTextParser` 与自定义样式**：`FDreamUIRichTextCustomStyleItemData::ApplyToRichTextParseResult` 接受一个标签顺序和一个样式名，都有默认值，
  所以调用方不改也能编译；`FRichTextParseResult` 有 `PaintName`、`PaintOrder` 和 `bPaintRemoved`。
- **玩家索引**：导航作用域、动作触发器和动作栏上的 `GetUserIndex()` 不再是 inline 的，回答解析后的索引；读它，而不是可能为 -1 的 `UserIndex` 属性。
- **弹出框**：对一个在自己的打开时聚焦期间就关闭了的弹出框，`UDreamUIPopupLayer::Push` 返回 false；压入后检查它，或检查弹出框的状态。
  `FDreamPopupParams` 有一个 `TabBehavior`（Cycle、CloseAndContinue）和一个 `OnClosing` 回调，在弹出框开始关闭、焦点仍在其中时被调用。
  `EDreamPopupDismissReason` 增加了 `Tab`，追加在末尾，给按原因 switch 的代码。弹出层有 `DismissAll(UserIndex, Reason)`（模态会为它的玩家调用）和
  `GetHomeSlot(Widget)`（被提起的控件在家时的面板插槽副本）。
- **导航**：`TabNavigation` 为 Once 的自定义容器通过重写 `UDreamWidget::ResolveTabEntry` 说明 Tab 从哪里进入；自定义控件通过实现
  `IDreamUITabSwitchTarget`（`DreamGUIInput`，`Interaction/DreamUITabSwitchTarget.h`）响应肩键；`UDreamUIActionBar::GetPrompts` 列出动作栏显示的内容。
- **TextAnimation**：`UDreamMeshModifierTextAnimation_Property::ApplyProperty` 不再是纯虚函数。它转发给新的 `ApplyPropertyToCharacters`，
  内置属性重写的是后者；你自己对 `ApplyProperty` 的重写仍然有效。
- **UI 管理器**：`ShouldRunForNetMode(ENetMode)`；绘制行（`GetPaintRowsTexture`、`AcquirePaintTextRow`……）；内存报告用的 `GetPaintRowsMemoryInfo`。
- **2.0 已有的变化**：`IDreamUIRendererPrimitive::DreamUI_CollectRenderData` 接受一个 `FDreamUIPrimitiveDataArray&`；`UDreamWidget::MarkTransformChanged`
  和 `UpdateObjectToWorldTransform` 没了；`UUIDropdown::CreateBlocker`（一个虚函数）没了；`RestoreFocusForTopScope` 没了；`SetBrush_Material`
  弃用，改用 `SetBrushFromMaterial`；移动顶点或 UV 的自定义网格修改器必须通过 `ModifierWillChangeVertexData` 说出来，否则覆盖率文本会画错位置。

<a id="打包"></a>

### 打包

带彩色 emoji 的游戏要自己发一个 emoji 字体，有 CJK 文本的游戏要用 EFIGSCJK 预设打包（泰文、老挝文、高棉文或缅甸文用 All）：
见[字体与打包](fonts-and-packaging.md)。测试宿主的打包文本冒烟测试拿打包游戏的文本和同一构建在未烹饪内容上的运行比对（`Tools/TestHost/README.md`）。
