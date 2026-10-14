---
title: 概览
description: .dui 文件是什么、放在哪、由哪些语句组成，以及它怎样成为一个 widget Blueprint 的层级。
---

`.dui` 文件是一个 DreamGUI widget Blueprint 的源码：widget 树、每个节点的属性、绑定和动画，全部写成文本。
Blueprint 把这个文件记作它的 *Source File*；编译 Blueprint 时读这个文件、建出层级；在设计器里改了什么，
设计器再把改动写回这个文件。

文本替换的是**创作**这一步，不是运行时。从 `.dui` 编译出来的类和在设计器里拖出来的类是同一种类：同样的生成类、
每个 widget 一个成员变量、同样的属性绑定、同样的设计器。文件只在**编译时**读——只有那一刻能为每个 widget
声明成员变量、对照类上的函数解析绑定、把整棵树检查一遍。所以一个类只对应一个文件：`SourceFile` 是类默认值
（`EditDefaultsOnly`），不能每个实例各指一个文件。

（自 1.0.0 起）

<a id="文件放在哪"></a>

## 文件放在哪

放在 `DUI/` 目录下：项目自己的 `<Project>/DUI/`，或者某个已启用插件的 `<Plugin>/DUI/`。**不放**进 `Content/`：
`.dui` 是源码，不是资产。放进 `Content/`，cooker 就会去遍历它，内容浏览器里会多出一个打不开的文件，
插件要分发 widget 类还得把源码塞进 cook 后的内容里才能被找到。

```text
MyProject/
  Content/
    UI/WBP_Settings.uasset
  DUI/
    UI/Library.dui
    UI/Settings.dui
    UI/Components/Row.dui
  Plugins/
    MyPlugin/
      DUI/
        Panels/Settings.dui
```

指向另一个文件的路径——Blueprint 的 Source File 里，或者 `use` 后面——有三种写法：

| 写法 | 含义 |
| --- | --- |
| `Panels/Settings.dui` | 相对某个 `DUI/` 根：先找项目的，再按插件管理器的顺序找每个插件的，取第一个**存在**这个文件的。 |
| `Plugin.MyPlugin:Panels/Settings.dui` | 指定插件 `MyPlugin` 的 `DUI/` 目录。 |
| `D:/Work/Proj/DUI/Panels/Settings.dui` | 绝对路径，照写的用。 |

第一种方便，但两个根下有同一个相对路径时就有歧义了——第二种写法就是为此存在的。用 *Set Source File...*
选文件时，存下来的是可移植的写法：在项目根下存相对路径，在插件根下存 `Plugin.X:…`，只有文件不在任何根下时才存绝对路径
（那样的路径只在这一台机器上有效）。Source File 是编辑器专用数据，cook 时会被剥掉：层级早已烘进生成类里了。

<a id="文件结构"></a>

## 文件结构

一个文件是一串语句。语句在行尾或 `;` 处结束。注释是 `// …`（到行尾）和 `/* … */`。

顶层可以出现的语句：

| 语句 | 作用 | 详见 |
| --- | --- | --- |
| `class /Game/UI/WBP_Settings` | 这个文件是哪个 Blueprint 的源码。最多一个。 | 本页下文 |
| `use …` | 让另一个文件或类在本文件里可用。 | [`use`](use.md) |
| `resources { … }` | 具名常量。 | [样式与资源](styles-and-resources.md) |
| `props { … }` | 本文件的类声明的属性。 | [编写组件](components.md) |
| `events { … }` | 本文件的类会发出的事件。 | [编写组件](components.md) |
| `viewmodels { … }` | 本文件的类持有的视图模型。 | [视图模型](viewmodels.md) |
| `style Name { … }` | 一组具名的行。 | [样式与资源](styles-and-resources.md) |
| `timeline Name { … }` | 一个动画。 | [时间线](timelines.md) |
| 一个节点 | 树的根。 | [节点与值](nodes-and-values.md) |

这些语句的顺序随意：样式可以声明在穿它的节点下面。要编译成类的文件**恰好有一个**根节点，没有或多于一个都是
[DUI2006](../diagnostics/codes.md#dui2006)（没有根时，建树阶段还会报 [DUI5009](../diagnostics/codes.md#dui5009)，原因见解析器的那条）。
没有根节点的文件是一个**库**：它可以被 `use`，但不能被编译。

整个文件层面的几种错误：不能开始任何 token 的字符是 [DUI1001](../diagnostics/codes.md#dui1001)；`/*` 一直等不到 `*/` 是
[DUI1003](../diagnostics/codes.md#dui1003)；`{` 没有配对的 `}` 是 [DUI2002](../diagnostics/codes.md#dui2002)，`(` 没有配对的 `)` 是
[DUI2003](../diagnostics/codes.md#dui2003)；文法要别的东西的地方出现了一个 token，是 [DUI2001](../diagnostics/codes.md#dui2001)，消息里会说出两者。
块或括号嵌套超过 256 层是 [DUI2013](../diagnostics/codes.md#dui2013)：手写或生成的界面远到不了这个深度，到了只能是写坏了的文件，
报一个行号总比递归到栈溢出、把编辑器连同没保存的工作一起带走要好。

<a id="class-行"></a>

### `class` 行

`class` 行做两件事：

- 说明这个文件是哪个 Blueprint 的源码。把它编译进另一个 Blueprint 会警告
  （[DUI6003](../diagnostics/codes.md#dui6003)）。
- 它的路径是文件里每个本地化字符串的命名空间，所以文件改名后本地化键不会变。没有 `class` 行时，文件名就是命名空间。

`class` 写了两次、路径为空、或者写在节点里面，是 [DUI2007](../diagnostics/codes.md#dui2007)。

另一个文件用 `use "…" as Row` 把它当组件引用时，也是从这一行读出它的类；没有这一行，就只能由编辑器去找 Source File
是它的那个 Blueprint（见 [`use`](use.md)）。所以组件文件最好都写上 `class` 行。

<a id="接到-blueprint-上"></a>

## 接到 Blueprint 上

<a id="建一个以文本为源的-blueprint"></a>

### 建一个以文本为源的 Blueprint

在内容浏览器里用 *添加 ▸ DreamGUI Widget* 新建一个 widget Blueprint，父类选 **DreamUI Text User Widget**（`UDreamTextUserWidget`）。
只有这个类的子类才带 `SourceFile`；父类是普通的 `UDreamUserWidget` 时，设计器工具栏上不会出现 Source File 按钮。
放的位置要和文件里的 `class` 行一致，或者反过来改 `class` 行。
<a id="指定-source-file"></a>

### 指定 Source File

打开 Blueprint，设计器工具栏上有一个 Source File 下拉按钮（还没指定时显示 *No Source File*）。它的菜单里：

- **Create Source File...**：只在还没有文件时出现。弹出保存对话框（默认打开项目的 `DUI/` 目录，没有就先建一个），写一个起步用的
  `.dui`（一个根节点加一个居中的文字），把类指向它，然后编译——第一次编译就能用“出现了”来回答“成没成”。
- **Set Source File...**：选一个已有的 `.dui`，然后重新编译。
- **Open in Default Editor**、**Show in Explorer**、**Reveal in VS Code**：打开文件、在资源管理器里定位文件、
  让 VS Code 跳到选中节点所在的行。
<a id="编译"></a>

### 编译

编译读文件、建树、把树装成 Blueprint 的层级。出错时编译结果里是带 `DUInnnn` 的诊断。文件不存在或读不出来是
[DUI6001](../diagnostics/codes.md#dui6001)，文件解析了但没产生树是 [DUI6002](../diagnostics/codes.md#dui6002)。
<a id="显示出来"></a>

### 显示出来

蓝图里用 **Create Dream Widget** 接 **Add to Viewport**，和 UMG 一样——文件的 `props` 和 `viewmodels` 条目就是创建节点上的引脚；
C++ 里用 `UDreamUIBPLibrary::AddWidgetOfClassToViewport`。screen root、raycaster 和 event system 都是按需创建的，
不用另外配置。完整流程见[第一个界面](../getting-started/first-screen.md)。

此后，保存 `.dui` 会让**已加载**的、以它为源的 Blueprint 重新编译，`use` 了它的文件的类也一样。没加载的类不会自动更新，
加载一个 Blueprint 也不会重新编译它；*Tools* 菜单里的 **Rebuild DUI** 会重新编译项目里每一个以文本为源的 widget Blueprint，
没加载的先加载（会先询问）。把一个已加载的类正在用的文件删掉或改名，编辑器会当场说出来，而不是等到下次编译才报 DUI6001。

<a id="第一个文件"></a>

## 第一个文件

插件自带 `Content/Samples/HelloDreamGUI.dui`，这是还能算一个真界面的最小 `.dui`。要改它，先复制到自己项目的 `DUI/` 里：
插件更新会覆盖插件目录下的那份。下面是同样形状的一个更短的版本：

```dui
// DUI/UI/Hello.dui
class /Game/UI/WBP_Hello

style Title {
    Font     = /DreamGUI/DefaultFont_DistanceField
    FontSize = 28
    Color    = #E6E9F0
    HAlign   = Center
}

Widget Root {
    // 四角锚定、零内缩：加到哪里就铺满哪里，任何分辨率都一样
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)
    + Overlay { }

    Overlay Card {
        AnchorData.SizeDelta = (420, 120)
        @slot { HorizontalAlignment = Center  VerticalAlignment = Center }

        // 先写的先画，所以在后面：Overlay 按声明顺序叠放子节点
        Image Backdrop {
            Brush.TintColor = #1B1E26
            @slot { HorizontalAlignment = Fill  VerticalAlignment = Fill }
        }

        VerticalBox Column {
            Spacing = 10
            Padding = (24, 28, 24, 28)
            @slot { HorizontalAlignment = Fill  VerticalAlignment = Fill }

            Text Heading : Title {
                AnchorData.SizeDelta = (0, 34)
                Text = "Hello, DreamGUI"
                WrapTextAt = 372
                @slot { HorizontalAlignment = Fill  SizeRule = Auto }
            }
        }
    }
}
```

逐段看：

- `class` 说明这是 `/Game/UI/WBP_Hello` 的源码。
- `style Title` 是一组具名的属性，`Text Heading : Title` 让节点穿上它，节点自己的行再覆盖样式的。
- `Widget` 是没有视觉的矩形，专门用来装别的节点；`+ Overlay { }` 给它挂一个叠放子节点的布局容器。
  `Overlay Card` 和 `VerticalBox Column` 是同一件事的短写：类型就是容器，`Spacing`、`Padding` 设的是容器的属性。
- `Image`、`Text` 是有视觉、会画东西的节点。
- `@slot` 是这个节点向**上面那个面板**提的要求，而不是对自己的设置。`AnchorData.SizeDelta` 是节点自己的矩形，
  `@slot SizeRule` 是父面板怎么安排它——这个区别值得读两遍。
- 竖排容器先问子节点想要多高、再给它宽度，所以 Fill 槽位里的文字要用 `WrapTextAt` 说明在多宽处折行：
  这里是卡片的 420 减去两边各 24 的内边距。没有它，标题会按自己的宽度（0）被量成一字一行。

<a id="诊断"></a>

## 诊断

每条消息都有一个代码 `DUInnnn`，打印成 `File.dui(line,col): error DUI3001: …`。前端遇错会继续往下读：
一个有五处错误的文件报五条，而不是停在第一条。第一位数字说明是哪个阶段拒绝的：

| 代码 | 阶段 |
| --- | --- |
| [DUI1xxx](../diagnostics/codes.md#dui1xxx) | 词法：一串字符不成 token |
| [DUI2xxx](../diagnostics/codes.md#dui2xxx) | 语法：token 不成文法 |
| [DUI3xxx](../diagnostics/codes.md#dui3xxx) | 含义：合乎文法，但树对不上（id 重复、类型不认识） |
| [DUI4xxx](../diagnostics/codes.md#dui4xxx) | 值：值放不进它所写的属性 |
| [DUI5xxx](../diagnostics/codes.md#dui5xxx) | 建树：反射拒绝了写入，或目标对象不存在 |
| [DUI6xxx](../diagnostics/codes.md#dui6xxx) | 编译：`.dui` 接不到它的 Blueprint 上 |
| [DUI7xxx](../diagnostics/codes.md#dui7xxx) | 写回：设计器的改动放不进文本 |

VS Code 扩展在输入时就报单个文件能判定的那些，编辑器运行时还会把每次编译的结论送进 Problems 面板，见
[VS Code 扩展](../tools/vscode.md)。

<a id="其余各页"></a>

## 其余各页

- [节点与值](nodes-and-values.md) — 节点、id、不写 id 的节点、类型名的解析顺序、属性和每一种值。
  - [槽位行与组件](slots-and-components.md) — @slot 和 @fill 怎样告诉父面板摆放节点，+ Class 怎样挂行为和布局容器。
  - [样式与资源](styles-and-resources.md) — 样式的继承、样式里能带什么，以及 resources 常量块。
  - [use](use.md) — 合并库、给组件或类起短名、命名空间、转发，以及旧的 @Name 写法。
  - [编写组件](components.md) — props、events 与 emit、默认槽位和具名槽位，以及宿主怎样填槽。
  - [绑定与路由](bindings.md) — 三种箭头：驱动属性、路由事件、双向镜像，以及 Shown。
  - [if 与 for](control-flow.md) — 按条件切换分支、按数据重复模板，以及两者共同的规则。
  - [rows 表格](rows.md) — 同一个组件的多个实例写成一张表，每行按第一个值命名。
  - [时间线](timelines.md) — 文件拥有的动画：轨道、关键帧、缓动名、事件和 external。
  - [设计器写回](write-back.md) — 设计器把改动写进哪一行，哪些改动它拒绝写、为什么。
  - [完整示例](example.md) — 一个由组件、库和界面三个文件组成的设置界面，逐段讲解。
