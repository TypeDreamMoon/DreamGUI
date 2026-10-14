---
title: 项目里的布局
description: 运行时模块的分层与各自装什么、C++ 要在 Build.cs 里加哪个模块、.dui 文件放在哪里、插件目录里每个文件夹是什么，以及日志类别。
---

<a id="模块分层"></a>

## 模块分层

运行时按层拆成几个模块。一个模块只依赖它下面各层的模块，从不依赖同层的兄弟；反方向的 include 会让
`Tools/Tests/static_checks.py` 失败（规则 `layering`）。UI 的六个运行时模块都在 `PostConfigInit` 加载，
所以它们的类型和 `.dui` 标签在任何东西编译之前就已就位。

```text
L4   DreamGUISamples
L3   DreamGUIControls      DreamGUIExtensions
L2   DreamGUIInput
L1   DreamGUI (core)
L0   DreamGUIRenderer      DreamTween
     ------------------------------------------------------------
     DreamGUIEditor, DreamGUIK2Nodes (uncooked only), DreamGUITests (editor only)
```

| 模块 | 层 | 装什么 |
| --- | --- | --- |
| `DreamGUIRenderer` | L0，核心之下 | 绘制 DreamUI 的 view extension 和绘制渲染目标画布的 render command、它们的着色器、顶点与索引格式、画布用来回答材质参数的材质代理、后处理代理以及各效果共用的屏幕读写、`DreamUI.Stats` 背后的阶段计时。它对控件一无所知：它要的东西由核心注册给它 |
| `DreamGUI` | L1，核心 | 控件、visual、画布合批、布局、文本与 `.dui`、动画、事件契约、为渲染器保存画布分段的 render root，以及 PNG 截图 |
| `DreamGUIInput` | L2，核心之上 | 输入系统：事件系统及其预设 Actor、射线检测器与输入模块、动作路由、导航、拖放、提示框与模态、控件库所基于的 selectable 基类，以及游戏视口客户端 |
| `DreamGUIControls` | L3，输入之上 | 控件库：`Dream*` 控件（按钮、开关、滑条、列表、对话框、标签页……）、构成它们的 `UI*` 行为、动作栏、样式表，以及与 UMG 的互操作 |
| `DreamGUIExtensions` | L3，输入之上 | 2D 线、多边形和环，静态网格 visual，retainer box 与渲染目标辅助，歌词，具体的网格修改器，以及背景模糊、像素化和像素排序效果 |
| `DreamGUISamples` | L4，控件之上 | 展示场景和控件画廊 |
| `DreamTween` | L0，独立 | 补间动画；加载阶段是 `Default` |
| `DreamGUIEditor`、`DreamGUIK2Nodes` | 只在未烹饪环境 | 设计器和资产工具；蓝图节点。凡是运行未烹饪内容的进程都会加载它们，从编辑器启动的游戏（`-game`、Standalone Game）也算 —— 编辑器构建加载时会丢掉蓝图保存的字节码、从蓝图重建，而控件蓝图类和它的编译器就在这里。编辑器之外只启动编译器 |
| `DreamGUITests` | 编辑器 | 自动化测试套件 |

<a id="c-要加哪个模块"></a>

### C++ 要加哪个模块

用到哪个拆出去的模块里的类型，就把那个模块加进你的 `Build.cs`。大致的对应是：

| 你在用 | 模块 |
| --- | --- |
| 绘制、效果的渲染代理 | `DreamGUIRenderer` |
| 控件、visual、画布、`UDreamUIBPLibrary` | `DreamGUI` |
| 事件系统、射线检测器、导航、视口客户端、`DreamUITextInputRouter` | `DreamGUIInput` |
| `Dream*` 控件和 `UI*` 行为 | `DreamGUIControls` |
| 线、环、效果、渲染目标辅助 | `DreamGUIExtensions` |

```cpp
// MyGame.Build.cs
PublicDependencyModuleNames.AddRange(new string[] { "DreamGUI", "DreamGUIInput", "DreamGUIControls" });
```

资产不需要做任何事：每个挪了模块的类型都仍然能用旧名字加载（见[迁移](../guides/migration.md)）。

<a id="dui-文件放在哪"></a>

## .dui 文件放在哪

放在一个 `DUI/` 目录下：项目自己的（`<Project>/DUI/`），或者某个已启用插件的（`<Plugin>/DUI/`）。
**不放在 `Content/` 下** —— `.dui` 是源码，不是资产：放进 `Content/` 意味着烹饪器要去走它，内容浏览器里多出一个打不开的文件，
而一个发布控件类的插件得把源码混进烹饪内容里才能被找到。

指向另一个文件的路径 —— 蓝图的 Source File，或者 `use` 后面那个 —— 有三种写法：

| 写法 | 含义 |
| --- | --- |
| `Panels/Settings.dui` | 相对某个 `DUI/` 根：先找项目的，再按顺序找每个插件的，取第一个存在这个文件的 |
| `Plugin.MyPlugin:Panels/Settings.dui` | 那个插件的 `DUI/` 目录 |
| `D:/Work/Proj/DUI/Panels/Settings.dui` | 绝对路径，按原样使用 |

裸的相对路径意思是"哪个根有它就是哪个"，方便，但两个根里有同一个相对路径时就有歧义 —— 这正是插件限定写法存在的原因。
在设计器里选文件时，选到的绝对路径会被转回可移植的写法：项目根下的存成相对路径，插件根下的存成 `Plugin.X:…`，
都不在的才原样存成绝对路径（拒收会让选择框悄悄丢掉你的选择）。

```dui
use "Settings/SettingsLibrary.dui"
use "Plugin.MyPlugin:Panels/Row.dui" as Row
use /Game/UI/WBP_Slider as Slider

VerticalBox Root {
    Spacing = 12
    Row Audio { Label = "Audio" }
    Slider Volume
}
```

编辑器还会往项目的 `DUI/` 里写两个文件：`.dui-symbols.json`（启动时和执行 `DreamUI.ExportSymbols` 时重写，
是 VS Code 扩展做补全用的符号表），以及 `DreamUI.code-workspace`（*Tools ▸ Open DreamUI Workspace (VSCode)* 按当前的源码根重写并打开）。

一个用了 DreamGUI 的项目大概是这样：

```text
MyProject/
  Config/
    DefaultDreamGUI.ini
    DefaultEngine.ini
  Content/
    UI/
      WBP_HelloDreamGUI.uasset
  DUI/
    HelloDreamGUI.dui
    .dui-symbols.json
    DreamUI.code-workspace
  Plugins/
    DreamGUI/
```

项目的 `Config/DefaultDreamGUI.ini` 存的是 *Project Settings ▸ Plugins ▸ Dream GUI* 的值（见[安装](installation.md)）。

<a id="插件目录里有什么"></a>

## 插件目录里有什么

```text
DreamGUI/
  Config/
    Game.ini
    FilterPlugin.ini
  Content/
  DShader/
  Docs/
  Resources/
  Shaders/
  Source/
  ThirdParty/
  Tools/
  DreamGUI.uplugin
  CHANGELOG.md
  LICENSE
  README.md
```

<a id="config"></a>

### Config

从 1.0.0 起插件不再带 CoreRedirects，按旧名字保存的资产怎么重存一次，见[从开发版升级](../guides/upgrading.md#从-21-到-100)。

- **`Game.ini`** —— 把 `/DreamGUI` 加进 `DirectoriesToAlwaysCook`。插件自己伸手去拿的资产（默认材质、字体、精灵预设……）
  是赋给原生 CDO 的软指针，不产生资产依赖；没有这一行，按显式包列表烹饪时它们一个也不会进包，整个 UI 画成空白。
  文件必须叫 `Game.ini` 才会被并入 Game 分支。
- **`FilterPlugin.ini`** —— BuildPlugin 默认只打包 `/Source`、`/Content`、`/Resources`、`/Shaders` 和
  `/Binaries/ThirdParty`；这里列出其余必须随插件走的文件：上面的 `Game.ini`、README、CHANGELOG、`Docs/` 下的文档和参考、msdfgen 的源码与许可证。

<a id="content"></a>

### Content

默认字体（`DefaultFont_DistanceField` 及其 `_Bold`、`_Italic`、`_BoldItalic`、`_CJK` 四个字面，还有 `DefaultFont_Bitmap`）、
`Materials/` 下的材质与材质函数、精灵与纹理预设、`Blueprints/` 下的事件系统 Actor、`Controls/` 下的控件蓝图
（`BP_Button`、`BP_Toggle`、`BP_Dropdown`……）、`EnhancedInput/` 下的输入动作和映射上下文（`IA_*`、`IMC_DreamUIInputContext`），
以及 `Samples/HelloDreamGUI.dui`。

<a id="dshader"></a>

### DShader

`Content/Materials` 的源码（`.dsm` / `.dsf`）。`.uasset` 是构建产物，和源码一起提交，所以 **DreamGUI 不依赖 DreamShader**：
没装 DreamShader 时，那些材质就是普通的 `UMaterial` 和 `UMaterialFunction`。只有想从文本重建它们时才需要 DreamShader。

<a id="docs"></a>

### Docs

`DuiLanguage.md`（整个 `.dui` 语言）、`Migration.md`、`FontsAndPackaging.md`，以及 `Reference/` —— 逐类的属性与函数参考，
从反射打印出来、不是手写的，所以不会落后于头文件：

```powershell
UnrealEditor-Cmd.exe <project>.uproject -run=DreamGUIReferenceDocs
```

本站的[API 参考](../Reference/index.md)就是这些页面。

<a id="resources"></a>

### Resources

插件图标、编辑器图标，以及 `UMGParity/`：每个 UMG 类一张对照表（58 张），逐个成员说明它在这里是同名同义、换了名字或位置，
还是有意不提供以及为什么。自动化测试拿这些表和 UMG 自己的反射双向核对。见[与 UMG 的区别](about.md)和[UMG 对照](../controls/umg-parity.md)。

<a id="shaderssourcethirdparty"></a>

### Shaders、Source、ThirdParty

`Shaders/Private` 是渲染器的 `.usf` / `.ush`；`Source/` 是上面那十个模块；`ThirdParty/` 是 msdfgen 的子模块和提交进来的单文件拷贝（见[安装](installation.md)）。

<a id="tools"></a>

### Tools

| 目录 | 是什么 |
| --- | --- |
| `Tools/Tests` | 测试运行器 `Invoke-DreamGUITests.ps1`、预设、静态检查、结果判读（见[测试](../tools/tests.md)） |
| `Tools/TestHost` | 只为构建插件和跑测试而存在的最小宿主项目，以及打包文本冒烟测试 |
| `Tools/Bench` | 性能工作用的两个基准：5000 个按钮的屏幕和 2688 个世界空间面板的关卡（见[基准测试](../tools/bench.md)） |
| `Tools/Fonts` | 构建默认字体的脚本 `make_default_fonts.py` |
| `Tools/TextParity` | DreamGUI、Slate、Chrome 在同一个语料上的文本对照 |
| `Tools/ModuleSplit` | 把一个运行时模块从核心拆出去时用的脚本 |
| `Tools/UpdateMsdfgen.ps1` | 从子模块重新生成 msdfgen 单文件拷贝 |

<a id="日志类别"></a>

## 日志类别

| 类别 | 谁在用 |
| --- | --- |
| `DreamGUI` | 核心，以及它之上的运行时模块（Input、Controls、Extensions、Samples），编辑器的一部分也用它 |
| `LogDreamGUIRenderer` | `DreamGUIRenderer`。它在核心之下，用不了核心声明的类别，所以有自己的 |
| `DreamTween` | `DreamTween` |
| `DreamGUIEditor` | 设计器与资产工具 |
| `DreamGUIK2Nodes` | 蓝图节点 |
| `LogDreamGUIReferenceDocs` | 生成参考文档的 commandlet |

统计组是 `stat DreamGUI`（声明在渲染器里，渲染器和它之上的每个模块都往同一个组里计数）和 `stat DreamTween`。
