---
title: 更新日志
description: DreamGUI 每个版本改了什么：main 上还没发布的改动，首个公开版本 1.0.0，一直往前到 2.1.0 和 2.0.0 两个开发版。
---

DreamGUI 每个版本改了什么，按它对一个项目意味着什么来分组。把项目从一个版本搬到下一个版本——哪些东西加载起来不一样了、
哪些默认值变了、自己的 C++ 要改什么——见[升级](../guides/upgrading.md)；从 LGUI、LexUI 或者早期的 1.x 构建迁过来的项目，
从[迁移](../guides/migration.md)开始。本页与仓库的 `CHANGELOG.md` 同源：
https://github.com/TypeDreamMoon/DreamGUI/blob/main/CHANGELOG.md

**提示：**

**1.0.0 是首个公开版本。** 在它之前的构建是这个分支的开发线：先是单模块的 1.x 构建，然后是 2.0.0 和 2.1.0（`main` 上的提交 `2416e3f8` 和 `3049561a`）。公开版本号从 1.0.0 重新开始，所以下面的 2.0、2.1 都是开发版，而且比 1.0.0 更旧。插件描述文件里的
整数 `Version` 一直在涨——2.1.0 是 3，1.0.0 是 100——所以引擎永远不会把 1.0.0 当成两者中较旧的那个。

<a id="未发布"></a>

## 未发布

`main` 上、1.0.0 之后的改动，会随下一个版本发布。

<a id="新增"></a>

### 新增

- **视图模型。** `.dui` 文件用 `viewmodels { … }` 块声明它的类持有的视图模型——写明类，以及每个从哪来：宿主给（创建节点上的引脚、
  `SetViewModel`、宿主 `.dui` 里的一行）、`= new`、从新的 `UDreamViewModelSubsystem` 里 `= global` / `= global "Name"`，或者从最近一层外层
  widget `= parent`。绑定、路由和循环通过成员路径到达它们：`Text <- Player.Name`、`Value <-> Settings.MasterVolume`、
  `OnClicked += Settings.Apply()`、`OnValueChanged += Settings.SetVolume(Value)`、`for Item in Inventory.Items`。任何 UObject 类都可以当视图模型；
  `UDreamViewModel` 是方便用的基类（C++ 的 setter 用 `DREAM_VM_SET`），任何实现了 `INotifyFieldValueChanged` 的东西——包括 UE 的
  `UMVVMViewModelBase`——都会被订阅，而不依赖 ModelViewViewModel 插件。路径上的对象没有都设好时不求值，属性保留原来的值。见[视图模型](../language/viewmodels.md)。
- **绑定在它读的东西变化时更新。** 编译器记下每个 `<-` 表达式读到的变量和成员；它们全都会宣布变化（FieldNotify）时，绑定就订阅它们，
  变化之间不花任何代价。FieldNotify 变量上的 `Text <- Title`、`Shown <- Count > 0` 以前每帧读一次。`DreamUI.Binding.Dump` 列出哪些绑定每帧读、
  为什么；`DreamUI.Binding.ForcePoll 1` 让它们重新全部每帧读。见[绑定什么时候更新](../language/bindings.md#绑定什么时候更新)。
- **行各自更新。** 在 `for` 或 `each` 里，一项宣布它的某个被副本显示的成员变了时，只更新那一个副本，不刷新整个列表；`Event -> Item.Func()`
  把每个副本的事件路由到它自己那一项——列表视图回收单元格时跟着挪过去。源是有类型的（`TArray<UItemVM*>`）时，编译器检查 `Item.Member`
  （DUI6021）和 `Item.Func`（DUI6022）。
- **C++ 的 `UDreamUserWidget` 子类可以声明 FieldNotify 成员。** `DreamUserWidget.h` 现在包含 `FieldNotificationDeclaration.h`——UHT 为它们
  生成的代码需要它，UMG 的 `Widget.h` 也是这样；以前这样的类编译不过。`.dui` 里绑定到这种成员会通过它的描述符订阅，`DREAM_VM_SET` 就是它的 setter。
- **事件上的 `+=` 和 `=`**，和 `->` 并列：`+=` 加一个监听者，只用于多播事件；`=` 是单播委托唯一的监听者——路由现在终于能指向单播委托了——
  也只用于它；用错是 DUI5025。`->` 两种都接受。
- **Create Dream Widget、Add to Viewport 和 Add to Player Screen**——UMG 的 Create Widget 节点，以及和它配套的两个 user widget 调用。在节点上
  选一个类，它就为每个 Expose on Spawn 属性开一个引脚——.dui 文件的每个 `props` 条目都是一个——另有一个 Owning Player 引脚，并把返回值的类型
  设成那个类。引脚在 widget 初始化之前赋值，所以 Pre Construct、On Initialized、On Construct 和每个绑定的第一次取值都已经能看到它们；UMG 在
  Create 之后才赋值，对 On Initialized 来说太晚了。Add to Viewport 把 widget 放上它拥有者玩家的屏幕，并且和 UMG 一样，拒绝已经有父节点的 widget；
  Add to Player Screen 还会拒绝没有拥有者玩家的 widget。Remove from Parent 本来就有。C++ 拿到节点的两半：`BeginCreateDreamWidget` 和
  `FinishCreateDreamWidget`。
- **Add Component 只列出选中 widget 支持的组件**（`FDreamUIComponentSupport`）。网格修饰器要一个会建网格的视觉，精灵播放器要 Sprite，精灵图集
  播放器要 Texture，超链接和文字动画要 Text，UMG 交互要 UMG Widget 视觉；Retainer Box 和 Invalidation Box 要 widget 先有画布；画布、导航、动画组件、
  具名槽位、内容 widget 每个 widget 一个，一个 widget 只能是一种可选择对象、一种滚动视图，有多个子节点的 widget 不提供内容 widget。这些以前哪里都
  能加，加了之后什么都不做。粘贴、复制和拖到面板上的组件资产遵守同样的规则，被拒绝的拖放会说出原因。项目在自己的编辑器模块里为自己的组件加规则。
  见[组件](../tools/designer.md#组件)。

<a id="变更"></a>

### 变更

- **只读 FieldNotify 变量的绑定不再每帧读**——在它的 Blueprint 重新编译之后，等变量宣布变化时才更新。C++ 给这样的变量赋值却不广播——FieldNotify
  从来不允许——以前被轮询掩盖，现在不会了；`DreamUI.Binding.ForcePoll 1` 能把它找出来。
- **Create Dream Widget Of Class 不在蓝图菜单里了**：Create Dream Widget 节点代表它，就像 UMG 的 Create Widget 节点代表
  `UWidgetBlueprintLibrary::Create`。已经调用它的图照常编译。它是 BlueprintInternalUseOnly，所以 Python 里也没有了。
- **内容浏览器的添加菜单在最上面提供 DreamGUI Widget**，和引擎的 Blueprint Class、Material 在一起，不再只在一级子菜单里。子菜单本身现在叫
  DreamGUI，分节——Basic（DreamGUI Widget、Widget Animation）、Fonts、Graphics、Rich Text——每项一个短名字和一行提示，而不是一整列按类型名排序、
  widget 排在最后、提示是类的代码注释的列表。不再开发的位图字体显示为 Legacy Bitmap Font，并指向距离场字体。资产的类型名——磁贴上、过滤器里的——不变。
- **设计器的调色板按 UMG 的方式分组。** Basic、Panels、Common、Input、Lists、Scrolling、Containers、Primitive、Shapes、Effects、Components、
  Modifiers、Advanced、User Created，最后是默认折叠的 Legacy——以前是一个三十多行、什么都有的 Controls 分类，旁边是十来个只差一个词的小分类。
  条目按名字排序，面板去掉了 "UMG " 前缀，Tile View 和 Scroll Box 两个行为挪进 Legacy，和取代它们的控件放在一起。User Created 不再列插件自己的
  预设目录。层级面板的 Create 菜单用同样的分组。注册键没变，收藏都还在；拿 `FDreamUIControlDescriptor::Category` 去和 "Controls"、"Post Process"、
  "Extensions" 或旧分类名比较的项目扩展，改从 `DreamUIPaletteCategory` 读名字。见[调色板](../tools/designer.md#调色板)。
- **细节面板把 widget 是什么放在它放在哪的正下面。** Slot、Layout，然后 Visual、Panel、Self Layout，然后 Appearance 和 Behavior，然后 Render
  Transform 和默认折叠的 Perspective、Navigation、Accessibility、Localization；以前视觉排在 Behavior、Appearance、Panel、Self Layout 之后。Text 的
  行以 Text、Font、Size、Style、Color 和对齐开头，换行、排版、渲染设置放在默认折叠的组里；Sprite 和 Texture 的颜色在它的 sprite / 纹理下面。底部
  重复 Layout 行的 Transform 和锚点数据分类去掉了，Is Enabled 进了 Behavior。见[细节面板](../tools/designer.md#细节面板)。

<a id="修复"></a>

### 修复

- **创建一个抽象类的 widget 会记一条错误**，而不是停在 NewObject 的断言上。从列表里选的类不会是抽象的，但由变量喂给类引脚或 Create Dream Widget
  Of Class 调用的类可能是。
- **DreamUI Widget Blueprint 的缩略图就是它编写的那个界面。** 内容浏览器的磁贴以前是编写时锚点矩形的线框，不知道布局把东西排到了哪里，所以大多数
  界面画出来是一个灰框加一两条线。缩略图现在像设计器的预览一样建出 widget，在设计画布上排好、画出来，和 UMG 画 widget 缩略图一样；没有编译出类的
  Blueprint 仍然画线框。它在资产没有干净的缩略图时和保存时画，不会在指针停在上面的每一帧都画，所以大界面不会卡住浏览器。已经存进资产的缩略图会一直
  显示，直到资产下一次被修改并保存。见[内容浏览器](../tools/designer.md#内容浏览器)。

<a id="100"></a>

## 1.0.0

包含 2.1.0 开发版的全部内容，外加长成了能用来写界面的 `.dui` 语言：只用 `.dui` 写的组件、`use … as`、`if`、`for`、`rows`、
更短的布局写法；覆盖这一切的语言参考；以及最早两个展示界面发现的问题的修复。2.1.0 上的项目打开后不需要任何改动（见
[升级](../guides/upgrading.md)里的“从 2.1 到 1.0.0”），只有仍靠重定向才能加载的资产例外：插件不再带任何重定向。

<a id="移除"></a>

### 移除

- **CoreRedirects。** `Config/DefaultDreamGUI.ini` 和它的 763 条重定向——来自 LGUI 和 LexUI 的、来自 prefab 词汇的、来自控件和
  事件改名的、来自模块拆分的——都没了，引擎不再对挂载这个插件的项目应用其中任何一条。插件自己的资产和测试夹具都写着现在的类型名。
  按旧名字保存的资产，要把 2.1.0 的那一段借进项目的 `Config/DefaultEngine.ini`，重存一次（见[升级](../guides/upgrading.md#从-21-到-100)）。
  守着这个重定向文件的四个 `DreamGUI.Packaging` 测试随它一起去掉了，`DreamGUI.Packaging.ThePluginShipsNoCoreRedirects`
  则保证 `[CoreRedirects]` 段不会悄悄回来。
- **模块拆分之前保存的旧资产夹具。** 它们只靠重定向才能加载；测试宿主的 `DreamGUIFixtures` 现在由 1.0.0 保存，是之后的版本都要能继续加载的基线。
- **`Tools/ModuleSplit/generate_split_redirects.py`**，它负责把一次拆分的重定向追加进那个文件。现在它叫 `moved_types.py`：列出一次拆分挪走的类型，
  加 `--redirects` 时打印一段给项目自己的配置用的重定向，不往插件的配置里写任何东西。`retarget_script_paths.py` 从它拿类型名。

<a id="新增-1"></a>

### 新增

- **`rows`：`.dui` 里的实例表。** `rows Row : ListRow (Label, Description) { "City Ruins", "…" … }` 把类型、样式和属性名
  只写一次，然后每个实例一行——同一个组件出现 N 次、只差几个值，设置页或菜单项列表就是这样构成的。表被读成它的每一行
  所代表的普通未命名子节点，所以下游什么都没变。每一行按它的第一个值命名（`Page_0__Row_City_Ruins`），因此插入行或调整行的
  顺序不会动到其他行的 id 和本地化键。一行可以以一个块结尾，写这一行在列之外还需要的东西。设计器会把一列的值写回它的
  单元格，而一行写不出来的东西会被拒绝并说明原因（[DUI7004](../diagnostics/codes.md#dui7004)）。新增诊断码
  [DUI2020](../diagnostics/codes.md#dui2020) MalformedRows 和 [DUI3023](../diagnostics/codes.md#dui3023)
  DuplicateRowKey（警告）。见 [`rows`](../language/rows.md)。
- **在 `.dui` 里用短名字引用组件。** `@Row Row1 { }` 是一个节点，它的类型由某个 `resources` 块里的 `Asset` 条目 `Row`
  指定——本文件的，或者 `use` 带进来的——这样一族组件只在给它们定样式的库里命名一次，每个界面写 `@Row`，而不必每一行都写
  资产路径。条目缺失、不是 `Asset`、或者指向的不是用户控件，都会如实报出。
- **`.dui` 里的 `use … as`。** `use "Components/Row.dui" as Row` 指名一个组件文件编译成的类——它的 `class` 行写的那个，
  或者把它当 Source File 的那个 Blueprint——`use /Game/UI/WBP_Row as Row` 则指名一个没有 `.dui` 的类；之后
  `Row Row1 { }` 就是一个实例。对库写 `use "Lib.dui" as nier`，会把它的样式、资源和组件名放到 `nier.` 下面
  （`: nier.Label`、`@nier.Ink`、`nier.Row`），这样两个库可以并排使用；库里的 `use … as` 行会随库一起带过来。`@Row`
  照样可用。见 [`use`](../language/use.md)。
- **只用 `.dui` 写的组件。** 一个文件声明它的类向宿主提供什么：`props { Text Label }`（Blueprint 变量，可带默认值，宿主
  可以设置它，也可以绑定它——`Label <- GetName()`——尽管它们没有 setter；C++ 父类上的同名属性会被直接使用）、
  `events { Picked(Number Index) }`（事件分发器，用 `OnClick -> emit Picked(Index)` 引发，宿主像路由任何事件一样路由它），
  以及带布局和样式、可以标为 `default` 的 slot——没写 slot 名而嵌套进来的内容会进入它，取代对 `GetDefaultSlotName` 的重写。
  宿主在实例里用 `slot Detail { … }` 填充一个命名 slot。见[组件](../language/components.md)。
- **`.dui` 里更短的布局写法。** 布局容器可以直接作为节点类型（`VerticalBox Column { Spacing = 29 }`），没人引用的节点不需要
  id（`HorizontalBox { … }`），slot 行可以归进 `@slot { … }`，`@fill` / `@fill 2` 代表最常写的那两种，样式可以携带
  `+ Component` 块和 slot 行，于是一类列只需要一个名字。
- **`.dui` 里的 `if` 和 `for`。** `if Cond { } else if … { } else { }` 每次只显示一个分支的控件（每个控件都绑定一个代表它所在
  分支的 `Shown`；隐藏的分支保留自己的状态）；`Shown <- Expr` 是每个控件都有的、用是或否表示的可见性
  （`UDreamWidget::Shown`）；`for Item in Items { … }` 在任何面板里为每一项复制一份模板，而 `each` 继续负责填充虚拟化的
  列表视图。见[控制流](../language/control-flow.md)。
- **语言参考。** `Docs/DuiLanguage.md`（在本站就是 [`.dui` 一节](../language/overview.md)）描述了 `.dui` 的全部内容：文件结构、
  类型、值、样式、`use`、组件、绑定、控制语句、时间轴，并附有每一个诊断码的表（[诊断码](../diagnostics/index.md)）。编辑器的
  符号导出（`DUI/.dui-symbols.json`）列出关键字、容器类型及其属性，供编辑器做补全。
- **设计器能写回新语法。** 一次编辑会落到持有那个值的行上，不管节点的类型是怎么写的；有 `@slot { … }` 块时写进块里；
  没有 id 的节点按为它生成的 id 定位。在设计器里新建的节点，用文件为它的类起的别名来写，或者写成它的容器类型；拖进组件
  命名 slot 的内容，写进那个 slot 的填充里。没有哪一行写着的东西，会说明而不是硬写：由 `if` 决定的可见性、承接不了这次改动的
  简写是 [DUI7004](../diagnostics/codes.md#dui7004)，来自样式组件的值是 [DUI7005](../diagnostics/codes.md#dui7005)。
  见[写回](../language/write-back.md)。
- **渲染变换补间。** `UDreamWidget::RenderTranslationTo`、`RenderOffsetTo`（在画布平面上，保留深度）、`RenderScaleTo`
  和 `RenderAngleTo` 移动、缩放、旋转的是画出来的东西，从不动布局：它们是给由面板摆放的控件用的补间，这类控件的锚定位置和
  尺寸会在面板下一次排布时被写回。
- **控件可以拒绝焦点环。** `UUISelectable::bUseFocusRing`（默认开启；运行时用 `SetUseFocusRing`）为那些自身外观已经能标示
  焦点的控件关掉项目的焦点环。焦点移到这样的控件上时，焦点环会被收起，而不是留在焦点离开的那个控件上；一个界面上所有控件
  都拒绝它时，焦点环根本不会被创建。`NavigationSelectionClass` 没有设置时，现在表示处处都没有焦点环，并且不会在每次焦点
  变化时报一条警告。
- **限制一帧推进补间的幅度。** `DreamTween.MaxStepSeconds`（控制台变量；0 即关闭，也是默认值）：比它更长的一帧——界面在
  加载资产、新文字的第一次绘制——只把世界驱动的补间推进这个上限，于是在这种卡顿之前刚开始的入场动画会从停下的地方接着播，
  而不是直接跳到结尾。手动推进从不受限制。

<a id="修复-1"></a>

### 修复

- **焦点环贴合它标示的控件。** 插件的焦点环把边框画在一个子节点上，这个子节点以固定的 100x100 制作在焦点环根节点的中心，
  所以无论是整行宽的列表行还是一个小图标，边框都是 100x100，只有看不见的根节点跟随了控件的尺寸。没有自己的 Blueprint
  处理函数的焦点环类，现在会把每个覆盖其根节点的子节点（中心锚定、不小于根节点）改为随根节点拉伸，保留制作时的边距；更小的
  或者锚在角上的子节点保持自己的尺寸。
- **刚出现的焦点环不再停在透明状态。** 移动焦点环会杀掉它正在运行的补间，其中包括刚刚显示的焦点环的淡入，于是四分之一秒内
  的第二次按键——或者同一次焦点先以选中、再以导航进入的形式到达——会让焦点环停在淡入已经到达的透明度上。现在移动会先把淡入
  完成。
- **代码移动焦点后会离开原来的控件。** `SetFocus` 会随焦点一起移动导航光标，但被离开的控件直到下一次导航步进才听到“退出”，
  于是它在拿到焦点的控件旁边继续把自己画成 Focused——对 `GetCurrentSelectionState` 也回答 Focused——无论是界面打开子列表
  或对话框，还是它被隐藏再显示。现在一次取消选中就结束导航对它的持有。
- **在经由材质绘制的图片之后，内置着色器画的东西重新由内置着色器绘制。** 在屏幕画布或渲染目标画布上，经由材质的绘制绑定了
  材质的管线，却让这一遍的内置管线缓存仍然以为内置管线还绑定着，于是接下来的文字或块用材质的着色器去画：材质绘制的图片之后
  的文字变成空白或方块，RHI 还会因为给一个没有绑定的顶点着色器设置参数而触发 ensure。世界空间画布本来就会重新设置内置管线，
  现在屏幕画布也会。
- **画布参数里只用到渲染器标志的笔刷材质，会被告知渲染器。** 画布只向材质询问它的贴图参数，所以标量参数
  `DreamUI_IsRenderByDreamUIRenderer` 从来不算数：一个按它做预乘的程序化材质被原样绘制、标志停在默认值，
  于是又被 DreamGUI 的渲染器预乘了第二次。
- **交给图片的材质实例会显示它的拥有者在它上面设置的参数。** 画布通过自己的一个代理来绘制材质，这个代理缓存了材质的
  uniform 表达式，而且只在 DreamGUI 自己的参数变化时才重建缓存：在首次绘制之后对实例调用 `SetScalarParameterValue`，结果
  永远到不了屏幕上，于是动画材质一动不动。现在只要实例自己的渲染代理重建过，这个代理就在收集绘制时重建自己的缓存。

<a id="210开发版"></a>

## 2.1.0（开发版）

2.0 之后的五轮工程：把一帧的开销压到能承载成千上万个动画控件，审查了每一个控件和编辑器路径，把文字与 Chrome 和 Slate
对照测量、在有差异的地方重写，渐变文字，键盘和手柄导航，以及发布所需要的一切。`DreamGUI.uplugin` 的版本号是 3。2.1 保存的
资产无法在 2.0 中打开。

<a id="新增-2"></a>

### 新增

- **渐变文字。** 文字的字面、描边和覆盖在字面上的叠加层，都可以各自用渐变来绘制：线性、径向、锥形、菱形或四角渐变，最多
  16 个色标，填充、重复或反射，在 sRGB、线性光或 Oklab 中混合，按整个文本块、内容框、每一行、每一个字形或每一段富文本来
  度量。渐变按 CSS 的写法书写（`linear-gradient(180deg, #FFF3B0, #E8B64A 55%, #9C6A12)`）——在细节面板里、在 `.dui` 里，
  或者作为一段富文本（`<gradient=Name>...</gradient>`，名字可以是自定义样式、项目预设或 CSS 本身）。预设是
  `UDreamGradientAsset` 资产，或者项目设置里的 CSS 字符串（`GradientPresets`）；使用同一个渐变的文字在 GPU 上共享它的那一行。
  绘制的相位、角度、中心和缩放可以做动画，而不需要重新排版或重新绘制文字——可以在 Sequencer 里打关键帧（`FacePaintPhase`、
  `OutlinePaintPhase`、`OverlayPaintPhase`、`PaintAngleOffset`），也可以用 `UDreamTextPaintLibrary` 做补间
  （`PaintPhaseTo`、`PaintAngleTo`、`PlayShimmer`）。不通过 `MF_DreamUI_Shade` 着色的材质改为按顶点色绘制。
- **编辑器里的渐变。** 渐变属性显示一条可以拖动色标的渐变条、一个带复制和粘贴的 CSS 框，以及一个预设菜单：列出项目的预设
  和渐变资产，并能把当前渐变保存为其中任一种。渐变资产有缩略图和 *Copy as CSS*。文字的绘制字段在细节面板里归为一个 Paint 组。
- **Tab 导航。** 在没有按住 Ctrl、Alt 或 Cmd 时，Tab 和 Shift+Tab 按层级顺序移动焦点，兄弟节点按新的 `TabIndex` 排序；
  `bIsTabStop` 和 `TabNavigation`（Continue、Cycle、Contained、Once、None）决定遍历的方式；列表、平铺视图和树是一个停靠点，
  从它们选中的那一行进入；滚出视野的停靠点会被滚动到视野内。模态框、带遮罩的对话框和会循环的弹出层把 Tab 留在自己内部，
  其他玩家的弹出层永远不是停靠点。Tab 会提交文本框的编辑并继续前进，通过 Tab 到达文本框则开始编辑；下拉框在 Tab 时提交
  高亮的那一行，菜单则关闭它的整条链。当玩家拥有 DreamGUI 的焦点或 Tab 停靠点时，Slate 自己从裸游戏视口出发的导航——Tab、
  方向键——永远不会把焦点移进 UMG。`TabOrder = LegacyGeometric` 在这个版本里保留 2.0 的几何顺序。
- **手柄与输入模式。** 导航作用域说明它的界面接收菜单输入还是游戏输入（`InputMode`：All、Menu、Game；Game 会为那个玩家
  关闭内置的导航、确认和返回）。按键和手柄作用于焦点：Enter、空格和手柄的确认键按下的是有焦点的控件，从不是悬停的控件，
  没有焦点时什么也不按。焦点外观只在焦点是由按键或手柄移动时才显示，就像 CSS 的 `:focus-visible`
  （`bFocusVisibleOnlyFromKeys`）；在 DreamGUI 的纯 UI 输入模式下，使用手柄时光标会隐藏（`bHideCursorOnGamepad`）。确认、
  返回、方向、翻页、首尾和切换标签页的按键都是项目设置（`ConfirmKeys`、`BackKeys`、`DirectionKeys`、`PageKeys`、
  `ExtentKeys`、`PreviousTabKeys`、`NextTabKeys`），设置变化时会重新读取；平台的确认和返回按钮在运行时读取
  （`bUsePlatformAcceptBack`）；扳机键翻页，肩键切换当前标签视图的标签页，并在操作栏里显示提示。分屏：作用域、动作触发器和
  操作栏属于它们所在控件的拥有者玩家，除非另外指定了索引。
- **文字**，与 Chrome 和 Slate 对照测量并向它们看齐：字素簇和双向文本层级，带连字和 CSS 行框；按 Unicode 范围、文化和
  呈现方式选择的回退字体，带真正的粗体和斜体字体；彩色 emoji（CBDT/CBLC、sbix、COLRv0），它们的阴影、描边和长阴影副本是
  剪影；两端对齐、制表位和中间省略；长文本只在编辑触及的地方重新排版；按游戏的文化断行；富文本的 `<lang=xx>` 标签；富文本
  里的图片有尺寸和对齐。
- **小字号文字**——不超过 `SmallTextMaxPixelSize`，默认 20 px——用对齐到设备像素网格的、经过 hinting 的覆盖率字形绘制，
  和 Slate 一样清晰。带描边、发光或衬底的文字，字面这样绘制，效果从距离场绘制（`SmallTextEffectFace`）；渲染层里的文字在
  该层静止 3 帧之后也这样绘制。只要还有文字在用某个字体的覆盖率字形，这个字体就保留它们在图集里的格子；超过
  `MaxCoverageCells` 的两倍后，新的字形要排队，期间从距离场绘制，每个字体只报一条警告。
- **默认字体**改为 Slate 的：Roboto，带真正的常规、粗体、斜体和粗斜体字体，CJK 用 DroidSansFallback，使用描边（多通道）
  距离场。
- **面板**追上 UMG：滚动框的填充 slot 和首尾内边距、换行框的填充、缩放框的模式和指定缩放、网格的跨行跨列、拖动开关、
  边框的着色。
- **弹出层**：每个玩家一个弹出层，取代了点击拦截器，把焦点还给打开弹出层的那一方，Back 只关闭最上面的一个。弹出层会说明
  Tab 在它里面做什么（`TabBehavior`：Cycle，下拉列表和菜单则是 CloseAndContinue）。在打开的弹出层上显示的模态框会关闭它，
  并接住第一次点击和第一次 Back；在一个画在打开的弹出层前面的层上按下，会关闭弹出层并且仍然传到那个层，Back 也先交给那个层。
  被自己的“打开时获取焦点”关掉的弹出层最终保持关闭，抬起的面板菜单保留它的 slot 的内边距、微调和尺寸上下限。
- **`DreamGUI.Memory`**，一个控制台命令：每个字体的字形图集（切片、GPU 字节数和 CPU 副本、格子、字形、字体数据字节数）、
  精灵图集页，以及每个世界的画布网格段和绘制行；`DreamGUI.Memory Json` 以 JSON 输出，`File=<path>` 写入文件。
- **按需检查**：`r.DreamUI.VerifyKeptPointers` 重新查找 DreamGUI 在帧与帧之间保存的每一个对象——控件的画布、画布的渲染层、
  UI 管理器的画布、动画属性所属的对象——并指出两者不一致的地方，就像 `r.DreamUI.VerifyPartialPrepare` 对画布的 prepare
  做的那样。
- **用来测量的开关。** `DreamGUI.Text.SmallTextCoverage` 和 `DreamGUI.Text.SmallTextMaxPixelSize` 为所有字体覆盖小字号
  设置，改动后重绘所有文字。`DreamGUI.Text.SmallTextOnMove`、`DreamGUI.Scroll.SnapToDevicePixels` 和 `bFieldTextCorrection`
  设置尝试用其他方式绘制移动中、滚动中和大号的文字，在测量之前保持关闭。`DreamGUI.Text.IncrementalParse`、
  `IncrementalMeasure` 和 `InPlaceDisplayList` 各自关闭增量排版的一部分，`DreamGUI.Text.VerifyIncremental` 则让它与从零
  开始的排版结果相互核对。

<a id="性能"></a>

### 性能

- 渲染变换不断变化的控件会变成**渲染层**，由它们的画布在 GPU 上移动，任意多个渲染层可以共享一次绘制调用。
- 世界变换在被读取时才合成，移动每帧只通知一次。
- 只由普通属性轨道组成的控件动画，由 DreamGUI 自己的播放器播放，而不是 Sequencer 的实体系统。
- 世界空间画布在共享的渲染图 pass 里绘制，每个面板的工作移到了工作线程上。两个 5000 个按钮的基准关卡都跑在 60 FPS 的上限。
- 画布只遍历请求变化的控件，就地修补几何发生变化的网格段，控件只是移动时保留原有的绘制调用。
- 在长段落里的一次编辑只读取和测量它周围的一个窗口，文字的显示列表在原处修改，而不是重新写一遍。
- 一个世界每帧最多把 `SmallTextRepaintBudgetPerFrame`（512）段文字重绘到覆盖率字形上；一段在旋转、缩放或移动时从距离场
  绘制的小字号文字，会在它静止之后再被检查一次，而不是每次移动都检查。
- 增量排版让每一条退回路径都保持精确：在一个以标点、数字或 emoji 开头的长段落的开头附近编辑，或者紧跟在内联图片之后编辑，
  得到的字形排布与整段一起排布时完全一样。
- 在编辑器里，DreamGUI 自带的 ICU 副本会拿到引擎的 ICU 数据，所以那里的断行也按游戏的文化进行，和打包后的游戏一样；没有
  这些数据时，它退回到引擎的迭代器，并且只提示一次。

<a id="修复-2"></a>

### 修复

- **距离场文字按预乘的方式合成各层**：距离场字形的衬底、发光、描边和字面被当作非预乘颜色混合，所以在什么都没有的背景上，
  一层的颜色会被拉向黑色，最终的透明度变成了它的平方。半透明或正在淡出的文字会变暗，渐变的透明色标会发灰，发光衰减得太快，
  浅色文字的边缘比 Slate 在视口里画得更细。现在每一层都叠加在它下面那一层之上。
- **游戏中的视口截取**：`UDreamUICaptureLibrary::SaveViewportToPng` 和 `ReadViewportPixels` 读取游戏的视口，而游戏视口
  是直接画进窗口的，帧与帧之间没有贴图：这会触发 ensure 并得到一张黑图。现在它们在这种情况下返回 false，并提示改用引擎的
  截图请求。
- **打包游戏中的中文回退**：游戏烘焙时使用的 ICU 数据（English 或 EFIGSCJK）没有 likely subtags，所以引擎把 `zh-CN` 展开成
  `zh-CN, zh`，没有 `zh-Hans`；为 `"zh-Hans"` 或 `"zh-Hant"` 写的回退条目在编辑器里能匹配上，在包里却永远匹配不上。
  DreamGUI 现在会为中文文化名补回书写系统。
- **渲染线程上的崩溃**：渲染器在它的渲染命令还在排队时就被释放（那个时有时无的 all-preset 崩溃）；渲染根和渲染图 pass 原先
  对可能先被销毁的对象持有裸指针，现在改为按引用持有。
- **扩展**：TextAnimation 的属性会跳过没有顶点的字符，而不是越界读取；画布没有渲染目标、或者自身没有顶点的圆柱渲染目标表面、
  在控件离开画布之后才设置的静态网格、子节点数量发生变化的 2D 线条，以及发往一个已被替换的画布的渲染目标输入，都不再崩溃
  或者送错地方；圆柱的弧度为 0 时按 1 度处理，负弧度的包围盒在它弯曲的那一侧。保留框（retainer box）真正做到了保留：它把
  自己的画布设为渲染目标模式，用完后恢复，不碰编辑器世界，没有 `DisplayVisual` 时只警告一次。静态网格视觉对象会拒绝超出
  顶点预算的网格并报错，而不是截断它的索引，它的包围盒也就在画布绘制它的地方。StaticMesh 模式下的渲染目标表面
  （`UDreamUIRenderTargetGeometrySource`）只在运行中才把它的材质放进静态网格的 0 号槽：在编辑器里，它放在那里的瞬态实例
  会让保存关卡时把网格自己的材质写成 null。Pixelate 把它的目标交给它的代理；pixel sort 把 pass 数限制在 1 到 512 之间；歌词的
  TTML 读取器在 256 层嵌套处停止；运行时设置的 TextAnimation 属性会开始生效，被它们替换掉的那些会停止。
- **DreamTween**：序列在回收器带走它的子项之后依然存活；重启或跳转自身序列的回调不再陷入循环，嵌套 16 层的跳转会被拒绝并
  报错；`Kill` 只触发一次回调；已完成和被杀掉的补间会退役，它们的句柄读作无效——`Kill`、`ForceComplete` 和 `Goto` 对已被
  杀掉或已退役的补间不做任何事，对已退役补间调用 `Restart` 只会警告，在补间自己的 `OnComplete` 里调用 `Restart` 会让它继续
  运行；对尚未开始的补间调用 `ForceComplete` 会先启动它；`Goto` 到终点只完成一次，`Goto(0)` 会应用起始值；空曲线回退为线性
  并给出警告，而不是卡住；颜色和透明度补间在越界的缓动下取整并钳制，而不是回绕；InExpo 精确地停在 1；反向的 yoyo 循环停在 0；
  静止的弹簧会在 `SetTarget` 时醒来；次数为 0 的 `RepeatCall` 什么也不调用；时间缩放、延迟和增量都会被检查；第二个 tick 辅助
  对象不再让补间速度翻倍；拥有者已经不在的补间会被直接丢弃，不触发回调；布局动画和嵌入的 UMG 控件遵循与其他控件补间相同的
  暂停和时间膨胀规则。
- **控件**：一次审查发现的大约四十个问题，每个都配了测试——条目变化期间的列表和滚动视图行、文本框缓冲区之前的一次堆写入、
  持有裸指针的下拉框、按下时被关闭的按钮和选择、源对象被销毁的拖拽、按键选择器、在 IME 下按 UTF-16 偏移编辑的文本框。
- **文字**：打包后的文字样式值如果产生了非规格化浮点数——一对值中第一个为零，比如膨胀旁边的字面柔和度为 0，或者效果透明度
  恰好是 128——会被微调到最近的规格化值，这样会把非规格化数刷成 0 的 GPU 不再把它们变成 0；长到 `FName` 装不下（1024 个字符
  或更多）的富文本标签名会作为文字显示，而不是触发断言。
- **渲染**：如果加上阴影副本会让网格超出顶点预算，网格会不带阴影绘制并给出一条警告，而不是完全不画。
- **字体**：位图字体在撤销和重做时重新加载；嵌入的字体文件在烘焙时会重新读取它的字节，空文件是一个烘焙错误；字体的图集贴图
  和它的文字列表不再被记录进撤销事务。
- **输入**：没有对应玩家的玩家索引不再被分配一个输入用户。
- **控件 Blueprint 在未烘焙内容上运行游戏时会执行它们的图表**（`-game`，Standalone）：`DreamGUIEditor` 改为 UncookedOnly。
- **编辑器**：设计器里的一次手势是一个撤销步骤；设计器把编辑写进资产指向的那个 `.dui`，并且从不覆盖在磁盘上被改过的文件；
  重新编译以及设计器自己重建预览，不再记录进一个打开着的事务。

<a id="平台与服务器"></a>

### 平台与服务器

- 只有 Win64 被构建和运行过：编辑器和自动化测试套件，以及在发布门运行过测试宿主的打包文字冒烟测试之后的 Development
  打包游戏。Mac、Linux、iOS 和 Android 列在插件描述文件里，但从未被编译过（README 的 Platforms 一节如今这样说明，而不是写
  “Builds: yes”；见[平台](../guides/platforms.md)）。
- UI 管理器不再在专用服务器的世界里启动（包括编辑器内运行的服务器）：没有画布，没有渲染器，没有绘制行。

<a id="文档与工具"></a>

### 文档与工具

- `Docs/FontsAndPackaging.md`（[字体与打包](../guides/fonts-and-packaging.md)）：彩色 emoji 字体、打包字体文件的三种方式、
  ICU 打包预设，以及没有它时会退化的东西。
- `Docs/Migration.md` 新增了从 2.0 迁到 2.1 的一节（[升级](../guides/upgrading.md)）。
- msdfgen 的 MIT 许可证随插件一起发布，放在它的源码旁边。
- 测试宿主有一个打包文字冒烟测试，是发布门的一部分：它的游戏模块里的一个探针记录打包游戏画出了什么，
  `Tools/Tests/compare_text_smoke.py` 把它与在未烘焙内容上运行的同一构建对照。
- `Tools/ModuleSplit/check_game_includes.py` 还能找出：文件用到了却没有包含其定义的引擎宏和全局变量、游戏会编译的代码里的
  编辑器头文件，以及依赖编辑器模块的运行时 `Build.cs` 文件。
- 渲染基准测试在七种运动下分别测量开启和关闭覆盖率时的小字号文字；文字对照运行测量笔画边缘、线性光下的墨量以及相对 Slate
  的灰度，并把渐变填充与 Chrome 的 `background-clip: text` 并排报告。
- `Tools/Bench` 存放基准测试和读取它们性能数据的脚本（[基准测试](../tools/bench.md)）；测试运行器有预设、一个夜间脚本，以及
  一张说明每个控件用哪种输入测试过的覆盖表（[测试](../tools/tests.md)）。

<a id="需要留意的变化"></a>

### 需要留意的变化

从 2.0 升级会改变一些默认值和行为：默认字体（文字会重新排版）、小字号文字的渲染、Tab、按键和手柄作用于焦点而不是悬停、
焦点外观、玩家索引、滚动条和列表作为 Tab 停靠点、淡出的描边文字的效果透明度、距离场文字合成各层的方式、MSAA 混合、按游戏
文化断行，以及对使用自定义着色器或网格修改器的人来说，带有新 `UV4` 通道的 64 字节顶点。它们都列在[升级](../guides/upgrading.md)
的“从 2.0 到 2.1”里。

<a id="200开发版"></a>

## 2.0.0（开发版）

模块化的版本（2026-09-29）。

- **模块。** 单一的运行时模块按层拆开：`DreamGUIRenderer`（视图扩展、着色器和代理）、`DreamGUI`（控件、视觉对象、画布、
  布局、文字、`.dui`）、`DreamGUIInput`（事件系统、射线检测器、导航、视口客户端）、`DreamGUIControls`、
  `DreamGUIExtensions`、`DreamGUISamples` 和 `DreamTween`，`DreamGUIEditor` 和 `DreamGUIK2Nodes` 为 uncooked-only，
  `DreamGUITests` 仅限编辑器。较低的层从不包含较高的层。
- **重定向随插件一起发布**：`Config/DefaultDreamGUI.ini` 带有全部重定向——来自 LGUI 和 LexUI 的、来自 prefab 词汇的、来自
  控件改名的、来自模块拆分的（共 763 条）。
- **按玩家区分的输入**：每个本地玩家有自己的指针、焦点和文字输入目标；指针 id 有各自的范围（鼠标 0，手指 100 起，脚本 1000
  起）；一个可选的 Slate 输入源在每种输入模式下都能收到输入；字符通过游戏视口客户端送达文本框。
- **渲染**：画布不创建材质实例，通过渲染线程代理回答它的材质的参数；渲染目标画布由它自己的一个渲染命令绘制；屏幕效果通过
  渲染图贴图上的同一条路径读写屏幕。
- **控件 Blueprint** 取代了 prefab 资产模型；设计器参照 UMG 的重建；控件的层级可以用文本（`.dui`）编写，带时间轴、路由和
  样式。
- **布局**沿着 Blink 和 Yoga 的思路实现，配有 UMG 形状的面板；Lex 布局族被移除。
- **移除**：根 Blueprint、`UDreamWidgetPresenterComponent`、世界空间射线检测源一族，以及那些恢复旧渲染器行为的设置和控制台
  变量。见[迁移](../guides/migration.md)里的“已经不存在的”一节。
