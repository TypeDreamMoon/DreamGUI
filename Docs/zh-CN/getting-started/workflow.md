---
title: 日常循环
description: 创作一个界面的完整循环：文本与设计器双向写回、保存即重编译、放到屏幕或世界里、接上交互、加动画、出问题时怎么看。
---

<a id="一份文件两个入口"></a>

## 一份文件，两个入口

一个文本创作的控件蓝图只有一份真相：它的 `.dui`。改它有两个入口，两边看到的永远是同一个东西。

- **在文本里改。** 用 VS Code 打开（*Tools ▸ Open DreamUI Workspace (VSCode)* 会按当前的源码根重写
  `DUI/DreamUI.code-workspace` 并打开它；VS Code 不可用时退回记事本）。扩展的补全来自编辑器写出的
  `DUI/.dui-symbols.json`，见 [VS Code 扩展](../tools/vscode.md)。
- **在设计器里改。** 设计器是这个文件的前端：在那里做的改动就是对文本的改动，写进存着那个值的那一行，
  或者在节点已有的行之后新起一行，文件其余部分逐字节不变。少数改动没有一行可写 —— 比如一个由 `if` 决定显隐的控件的可见性 ——
  设计器会明说（[DUI7004](../diagnostics/codes.md#dui7004)），而不是替你发明一行。见[写回](../language/write-back.md)。

<a id="保存即重编译"></a>

### 保存即重编译

在外部编辑器里保存一个 `.dui`，**已加载**的、以它为源的类会自动重新编译。编辑器一次保存往往要写好几次文件，
所以改动会先攒一小会儿（0.75 秒）再动手。

- 编译失败时弹一个提示，带 **Open the file** 链接，直接打开出错的文件。
- 一次改动超过 8 个文件（切分支、批量改写）时**不会**自动编译，而是弹提示问你：**Rebuild them now**。
  文件是留着的，不是丢掉的 —— 丢掉会让那些类悄悄过期。
- 源文件被删掉或改名时，提示会列出"现在从无构建"的那些类。
- *Tools ▸ Rebuild DUI* 重新编译项目里所有文本创作的控件蓝图，没加载的也会加载，动手之前会先问。

诊断的格式是 `File.dui(line,col): error DUI3001: …`，首位数字说明是哪个阶段拒绝的：1 词法、2 语法、3 语义、4 取值、
5 建树、6 编进蓝图、7 写回。每一条都在[诊断码](../diagnostics/index.md)里有成因和修法。

<a id="一天里的形状"></a>

## 一天里的形状

<a id="写"></a>

### 写

在 VS Code 或设计器里写树：节点、属性、样式、组件。新文件可以从设计器的 **Create Source File...** 开始，
它会写一个带起始层级的 `.dui`。复用靠组件：`use "Components/Row.dui" as Row` 之后，`Row Audio { … }`
就是那个类的一个实例（见[组件](../language/components.md)）。

<a id="编译"></a>

### 编译

保存。设计器里的预览跟着刷新。出错就看提示和诊断里的行列号。

<a id="放"></a>

### 放

屏幕上：蓝图里用 **Create Dream Widget** 接 **Add to Viewport**，C++ 里用 `UDreamUIBPLibrary::AddWidgetOfClassToViewport`。世界里：把控件蓝图拖进关卡，得到一个
`ADreamWorldWidgetActor`。同一个类，两种用法都不用改它（见[第一个界面](first-screen.md)和[世界空间](../concepts/world-space.md)）。

<a id="接上交互"></a>

### 接上交互

事件只有一种接法，到处写法相同：**路由**。`事件名 -> 处理函数` 指向**用户控件**上的一个函数 —— 也就是这棵树
编译成的那个类，UMG 也把事件处理放在这里。编译器把每条路由解析进 `UDreamWidgetBlueprint::EventBindings`，
`UDreamUserWidget::BindEventBindings` 在 Initialize 时挂上。在设计器里，同一件事是详情面板的 *Events* 一节：
点 `+` 会建一个签名正确的自定义事件，并写好指向它的路由。处理函数不存在是 [DUI6004](../diagnostics/codes.md#dui6004)，
参数对不上是 [DUI6005](../diagnostics/codes.md#dui6005)。

属性则用绑定驱动：`Text <- GetTitle()` 让属性跟着一个表达式走，`Value <-> Volume` 双向同步。见[绑定](../language/bindings.md)。

<a id="加动画"></a>

### 加动画

`timeline` 块是文件自己拥有的动画：每条轨道一行，写节点路径、它驱动的属性和关键帧。它编译成根上动画组件里的一个
`UDreamWidgetAnimation`，像别的动画一样有自己的类成员变量，用 `UDreamUserWidget::PlayAnimation` 播放。
因为归文件所有，每次编译都会重建它，**动画编辑器以只读方式打开它**；想交给 Sequencer 长期编辑，写
`timeline Name external`。在面板里动一个组件，用渲染变换的补间（`RenderOffsetTo`、`RenderScaleTo`、`RenderAngleTo`、
`RenderTranslationTo`），它们只动画出来的东西，不动布局。见[时间轴](../language/timelines.md)和[动画](../concepts/animation.md)。

<a id="看它画了什么花了多少"></a>

### 看它画了什么、花了多少

`DreamUI.Capture` 把视口和每块渲染目标画布写成 PNG；`DreamUI.Stats` 打印自上次以来每个阶段的开销；
`DreamGUI.Memory` 打印字体、图集和画布占了多少。画错或没画出来时，开 `r.DreamUI.VerifyPartialPrepare 1` 再复现。
见[调试](../tools/debugging.md)。

把交互和动画放在一起，一个小暂停菜单大概是这样：

```dui
class /Game/UI/WBP_Pause

Widget Root {
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)
    + Overlay {}

    Native.Button Resume {
        AnchorData.SizeDelta = (240, 56)
        @slot HorizontalAlignment = Center
        @slot VerticalAlignment   = Center
        OnClicked -> HandleResume

        Text Label { Text = "Resume" }
    }
}

timeline Appear {
    duration = 0.3
    Resume.RenderScale : 0.0 = (0.9, 0.9, 1), 0.3 = (1, 1, 1) ease OutCubic
}
```

`Native.Button` 是插件按钮控件的注册标签；`OnClicked -> HandleResume` 要求 `WBP_Pause` 上有一个叫 `HandleResume`
的函数或自定义事件；`Appear` 编译成一个动画，带着自己的类成员变量，把这个变量拖出来交给 `PlayAnimation` 就能播放。

**提示：**

设计器和文本是同一个类的两个视图，不存在"应用"这一步，也没有要合并的逐实例覆盖列表。父级给子级的内容走命名插槽，
  见[控件蓝图](../concepts/widget-blueprints.md)。
