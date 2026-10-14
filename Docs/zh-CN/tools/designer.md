---
title: "控件设计器"
description: "对照 UMG 设计器重建的 DreamUI Designer：按矩形拾取、缩放手柄与锚点徽章、框选与拖放重挂、调色板、细节面板、组件、撤销、源文件菜单、事件路由、内容浏览器里的菜单和缩略图，以及对 .dui 支撑的类它能改什么、不能改什么。"
---

打开一个 Dream Widget Blueprint，就进入 **DreamUI Designer**。它有两个模式：*Designer*（画布、调色板、
层级、细节面板、动画）和 *Graph*（Blueprint 事件图）。设计器编辑的是这个类的一个**预览实例**，改动直接
写回类本身，所以没有"应用"这一步，也没有每个实例各自的覆盖列表要对账。

**注意：**

  **设计器是被测试验证过的，不是被眼睛验证过的。** 它的手势有自动化测试（`Designer`
  预设）覆盖，但没有人手动走过每一处角落。预计会有粗糙的地方，遇到了请报告。

<a id="gestures-on-the-canvas"></a>

## 画布上的手势

设计器是对照 UMG 的 widget 设计器审过一遍的。相对上游新增的有：

- **按矩形拾取**。点击按 widget 的 *rect* 命中，而不是按渲染出来的三角形。只做布局的面板没有网格，
  射线永远打不中，上游里面板因此点不中、也放不进东西——按矩形拾取解决的就是这个。
- **悬停反馈**：指针下的 widget 会被描出来。
- **按轴的缩放手柄**，以及**锚点徽章**（anchor medallion），在画布上显示选中 widget 的锚点。
- **框选**：在空白处拖出一个框，选中框里的 widget。
- **在画布上拖动重挂**：把 widget 拖到另一个容器里。
- **从内容浏览器拖放**：把资产直接拖到设计画布上。
- **撤销**：每个设计器手势是一步撤销，创建、粘贴和拖放都在撤销覆盖之内。
- **吸附与网格**：工具栏的 *Snap* 开关和网格尺寸；拖动时有吸附参考线。
- **对齐与分布**：*Arrange* 菜单把选中的兄弟 widget 沿某条边或中心对齐（至少两个），或均分它们之间的
  间距（至少三个）。

层级面板的右键菜单有 *Wrap With...*（用一个新容器把所有选中项包起来，插入到它们的位置；widget 保持
原来的布局）、*Unwrap*（把一个 widget 从父子之间拿掉，子节点按原顺序落在它的位置）、*Replace With...*（换掉排列子节点的面板——和 UMG 不同，widget 本身的名字、位置、槽、组件和子节点都不动，只换布局容器）、
*Show / Hide in Designer*、*Lock / Unlock*、*Find References*、*Focus Selection*（等同于 F 键），以及
*Reveal in VS Code*。

<a id="the-palette"></a>

## 调色板

调色板列出可以放进来的东西，分组和 UMG 的调色板一样：**Basic**（Widget、Text、Image、RectBlock）、Panels、
Common、Input、Lists、Scrolling、Containers、Primitive、Shapes、Effects、Components、Modifiers、Advanced、
**User Created**（项目里的 widget Blueprint），最后是 **Legacy**。

- 每组里的条目按名字排序；面板去掉了 "UMG " 前缀。
- **Legacy** 放的是控件库取代掉的东西——LGUI 时代的行为、Blueprint 预设，以及 Tile View 和 Scroll Box
  两个行为——留着是为了让建在它们上面的 widget 还能看懂。它总在最后，默认折叠；打开过一次，设计器会记住。
- User Created 不列插件自己的预设目录。
- 层级面板的 *Create* 菜单用的是同一套分组和顺序。
- **收藏**：常用的条目可以收藏，收藏组排在最前面。注册键没变，所以以前的收藏都还在。
- **搜索**：搜索框过滤条目，过滤时所有分组展开。

项目自己注册的控件，分类在 DreamGUI 的分类之后、按注册顺序排列。分类名来自 `DreamUIPaletteCategory`
（`DreamUIControlRegistry.h`）；以前拿 `FDreamUIControlDescriptor::Category` 去和 "Controls"、"Post Process"、"Extensions" 或
某个旧分类名比较的扩展代码，要改用那里的名字。

<a id="the-details-panel"></a>

## 细节面板

选中一个 widget 时，分类按一个 widget 的构成排列：先是它放在哪（Slot、Canvas、Layout），再是它是什么
（Visual、Panel、Self Layout），然后是它怎么表现（Appearance、Behavior），最后是 Render Transform，以及
默认折叠的 Perspective、Navigation、Accessibility 和 Localization。Is Enabled 在 Behavior 里。以前排在最
下面、把 Layout 的行原样重复一遍的 Transform 和锚点数据分类已经去掉了。

视觉自己的行也按用途排：

- **Text** 以 Text、Font、Font Size、Font Style、Color、对齐、Overflow Type、Auto Wrap Text 和 Rich Text
  开头；其余的——Wrapping、Typography（包括语言选择）、Text Style / Paint、Rendering（覆盖材质、预设材质、
  混合模式、网格尺寸、小字栅格化等）——放在默认折叠的组里。
- **Image** 的刷子排在最前，射线检测的行排在最后。
- **Sprite** 和 **Texture** 的颜色在 sprite / 纹理的下面。

<a id="components"></a>

## 组件

*Add Component* 只列出选中 widget 支持的组件——以前它在每个 widget 上列出所有组件类，加上去之后大多数
什么都不做：

| 组件 | 要求 |
| --- | --- |
| 网格修饰器 | 一个会建网格的视觉（`UDreamVisualBatchMesh`） |
| `UISpriteSequencePlayer` | Sprite 视觉 |
| `UISpriteSheetTexturePlayer` | Texture 视觉 |
| `UITextHyperlink`，文字动画 | Text 视觉 |
| `UDreamUMGWidgetInteraction` | UMG Widget 视觉 |
| `UDreamRetainerBox`、`UDreamInvalidationBox` | widget 上已经有画布（`UDreamCanvas`） |
| 画布、导航、动画组件、具名槽位、内容 widget、Retainer Box、Invalidation Box | 每个 widget 一个 |
| 可选择对象（`UUISelectable` 一族）、滚动视图（`UUIScrollView` 一族） | 每个 widget 只能是一种 |
| 内容 widget | 不提供给有多个子节点的 widget |

粘贴、复制，以及拖到面板上的组件资产，遵守同样的规则；被拒绝的拖放会说出原因。规则写在
`FDreamUIComponentSupport`（`DreamGUIEditor` 模块）里：为一个类注册的规则对它的每个子类都成立，包括
Blueprint 子类。项目在自己编辑器模块的 `StartupModule` 里，用 `RequireVisual`、
`RequireComponent`、`OnePerWidget` 或 `AddRule` 为自己的组件加规则。

<a id="the-viewport-toolbar"></a>

## 视口工具栏

| 项 | 作用 |
| --- | --- |
| Screen Size | 预览的设备分辨率；*Flip Orientation* 交换宽高，*Show Resolution Guides* 叠加常见分辨率 |
| Size Rule | *Custom*：画布是选定或输入的分辨率；*Fill Screen*：画布就是视口，一个设计单位一个像素；*Desired*：按根 widget 的面板测量出的尺寸设定画布，适合按自身大小制作一个 tooltip 尺寸的 widget |
| Zoom | *Zoom to Fit* 框住整个画布（F 框住选中项），*Zoom 1:1* 一个设计单位一个屏幕像素 |
| Rulers、Guides、Layout Debug | 标尺、吸附参考线、选中 widget 的测量 / 排列 / 槽 / 裁剪诊断 |
| Safe Zone | 画出平台的标题安全区（桌面平台需要 `r.DebugSafeZone.TitleRatio` 才会有） |
| Respect Locks | 关掉后可以选中、拖动锁定的 widget 而不解锁 |
| Designer Chrome | 关掉画布边界、选框、手柄、参考线等所有覆盖层，只看 widget 本身 |
| Preview DPI | 按项目的 DPI 曲线缩小画布，预览 Slate 实际会施加的应用缩放 |
| Preview Language | 用另一种文化解析预览里的文本，相当于 UMG 的本地化预览；关闭设计器时自动关掉 |
| Screen Space / Canvas Eye | 按运行时的方式把预览构建成屏幕空间叠加画布（声明的透视才会生效）；把 3D 视图的相机放到画布自己的虚拟相机上 |

<a id="source-files-classes-backed-by-a-dui"></a>

## 源文件：.dui 支撑的类

一个类的层级可以来自一个 `.dui` 文件。工具栏上的 *DreamUI Source* 下拉菜单管这件事：

| 菜单项 | 作用 |
| --- | --- |
| Create Source File... | 写一个起步用的 `.dui`（一个根加一个居中的标签），让类指向它并编译。只在类还没有源文件时提供 |
| Set Source File... | 选一个已有的 `.dui` 作为这个类的层级来源，然后重新编译 |
| Open in Default Editor | 用系统关联 `.dui` 文件的程序打开 |
| Show in Explorer | 在文件浏览器里选中这个文件 |
| Reveal in VS Code | 在 VS Code 里打开并定位到正确位置（层级面板的右键菜单里也有，会定位到节点所在的行） |

能指定源文件的前提是类派生自 `UDreamTextUserWidget`；源文件就是它的类默认值
`SourceFile`。从脚本做同样的事见[脚本](scripting.md)。

<a id="the-text-is-the-only-truth"></a>

### 文本是唯一的真相

一个类一旦指向了 `.dui`，**文件就是它层级的唯一真相，设计器是文件的图形前端**。换来的是：
一个地方读、一个地方 diff、一个地方 review。代价是设计器不再什么都能做——而且它会在你动手之前就告诉你，
而不是让改动存活到下次编译再悄悄消失。

**值会写回文件。** 改一个属性，写回会落在**持有这个值的那一行**上——不管节点的类型怎么写，在
`@slot { … }` 块里就写进那个块，没有 id 的节点用它生成的 id 定位。设计器从不覆盖在磁盘上被改过的文件
（DUI7002）。在画布上移动一个控件也是值的修改：它重算锚点，而 `AnchorData.*` 是会写回的。

**结构归文件管。** 创建、删除、在树里移动、重命名、复制、粘贴控件，添加、删除或重排行为，替换面板——
在由 `.dui` 支撑的类上，这些一律被拒绝，并弹出一条消息说出文件名：*结构在文本里；改文件来改它；
设计器改值。* 旋转和缩放 gizmo 也不提供，因为写回只带 `AnchorData.*`，
而语言里没有相对旋转或缩放的写法。

**写不回去的属性行是灰的**，并说明原因：

| 原因 | 例子 |
| --- | --- |
| 文件用绑定写它 | `Text <- GetTitle()`——写一个字面量会删掉绑定 |
| 由循环展开而来 | `for` 生成的 N 个副本没有逐个实例的值可写 |
| 在可写集合之外 | 写回没有地方放这个属性，改了也活不过编译 |
| 值在 `.dui` 里没有写法 | 实际上就是对象引用：字体、纹理、材质。在面板里换一个新字体，预览变了，文件没变，下次编译改动就没了 |

写回时还会明确拒绝几种情况，"说出来而不是写进去"：一个值没法写成语言读得回来的字面量
（今天只有非有限浮点数），报 DUI7003；一个由 `if` 决定的可见性，或一个无法承载这个改动的简写，
报 DUI7004；一个来自节点样式 `+ Component` 行的值，报 DUI7005——
样式是共享的，写进去会移动所有用这个样式的节点。代码含义见
[DUI7xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx)。

自己在设计器里搭层级、不指向 `.dui` 的类，以上限制都不存在。

<a id="events"></a>

## 事件

细节面板的 **Events** 一节列出选中控件能发出的事件。`+`（*Add Event Handler*）会在事件图里创建一个签名正确的
自定义事件，**以及**一条指向它的路由——和 `.dui` 里的 `OnClick -> HandleConfirm` 是同一个东西。
已经有处理函数时，同一个按钮打开它。*Remove Event Route* 去掉路由。

路由名的是**用户控件**（层级编译成的那个类）上的函数，UMG 也把事件处理放在那里。编译器把每条路由解析进
`UDreamWidgetBlueprint::EventBindings`，`UDreamUserWidget::BindEventBindings` 在初始化时接上它们。它能接到本插件的两种事件：
控件声明的 `BlueprintAssignable` 动态多播委托，和较老的 `Interaction/` 行为声明的 `FDreamUIEventDelegate` 属性。

**提示：**

`FDreamUIEventDelegate` 自己的逐实例事件列表是遗留的、只读的。存在里面的绑定照常触发，也能从面板删掉，但
  不能再在那里新建：那种绑定是用字面量参数调用任意对象上的函数，UMG 没有对应物，`.dui` 也没有语法。
  打开含有这种绑定的面板会打一条警告点名它。不会自动改写——把"带这个值调用那个行为上的 `Foo`"
  改成"调用用户控件上的处理函数"会改变游戏的行为——所以碰到时请重新写成路由。

<a id="the-animation-editor"></a>

## 动画编辑器

动画编辑器基于 Sequencer。`.dui` 里的 `timeline` 块编译成类里的一个 `UDreamWidgetAnimation`；
因为**文件拥有它**，每次编译都会重建，所以动画编辑器**只读**打开它，并在日志里说明原因，重命名它也会被拒绝
（"在 `.dui` 的 timeline 块里改名"）。

要让 Sequencer 接管一个动画，把它的块改成 `external`——下面 `Pulse` 归文件，`Celebrate` 归资产：

```dui
class /Game/UI/WBP_Results

Widget Root {
    Text Title { Text = "Victory" }
}

timeline Pulse {
    duration = 0.6
    loop     = PingPong

    Title.RenderScale : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1) ease InOutQuad, 0.6 = (1, 1, 1)
}

timeline Celebrate external
```

`external` 不构建任何东西。它是一条清单：动画活在资产里，Sequencer 随便编辑，而文件仍然列着它——
这让"这个类有哪些动画"可以靠读文件回答。材质参数轨道、手调曲线这类 timeline 写不出来的东西，属于 `external`。见
[Timelines](../language/timelines.md)。

<a id="the-content-browser"></a>

## 内容浏览器

**添加菜单。** **DreamGUI Widget** 在 *添加* 菜单最上面一组，和引擎的 Blueprint Class、Material 排在一起。下面
的 **DreamGUI** 子菜单分节：Basic（DreamGUI Widget、Widget Animation）、Fonts、Graphics、Rich Text，
每一项是一个短名字加一行提示。位图字体已经不再开发，它显示为 Legacy Bitmap Font，提示里指向距离场字体。
资产的类型名——磁贴上、过滤器里的那个——没有变。

**缩略图。** 一个控件 Blueprint 的磁贴画的是它编写的那个界面：缩略图像设计器的预览一样把控件建出来、
按设计画布的尺寸排好、画出来，和 UMG 画控件缩略图的方式一样。它在资产还没有干净的缩略图时和保存时画，
不会在指针停在磁贴上的每一帧都画，所以大界面不会卡住内容浏览器。
已经存进资产里的旧缩略图（锚点线框）会一直显示，直到资产下一次被修改并保存；还没有编译出类的 Blueprint 仍然画线框。
