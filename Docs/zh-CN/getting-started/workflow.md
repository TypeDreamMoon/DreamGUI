---
title: "工作流"
description: "创作一个界面的完整循环——文本与设计器互相写回、保存即重新编译、把它放到屏幕上或世界里、接上交互、添加动画，以及出问题时如何查看。"
---

<a id="one-file-two-ways-in"></a>

## 一份文件，两种进入方式

以文本方式创作的控件蓝图只有一个真相：它的 `.dui`。有两种方式可以修改它，而两者看到的始终是同一个东西。

- **在文本中。** 在 VS Code 中打开它（*Tools ▸ Open DreamUI Workspace (VSCode)* 会根据当前源根重写 `DUI/DreamUI.code-workspace`
  并打开它，当 VS Code 不可用时回退到记事本）。扩展的补全来自编辑器写出的 `DUI/.dui-symbols.json`；见 [VS Code extension](../tools/vscode.md)。
- **在设计器中。** 设计器是该文件的前端：在那里所做的修改就是对文本的修改，写入保存该值的那一行——或该节点其他行之后的新行——其余部分逐字节保持不变。少数修改没有可写入的行——例如由 `if` 显示或隐藏的控件的可见性——此时设计器会明确告知（[DUI7004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7004)），而不是凭空发明一行。见
  [Write-back](../language/write-back.md)。

<a id="save-to-recompile"></a>

### 保存即重新编译

在外部编辑器中保存一个 `.dui`，由它构建的**已加载**类就会重新编译。编辑器每次保存会多次写入文件，因此改动会先收集片刻（0.75 秒）再执行任何操作。

- 编译失败会显示一条通知，其中带有指向失败文件的 **Open the file** 链接。
- 同时有超过 8 个文件发生变化（切换分支、批量重写）时**不会**在未经询问的情况下编译；通知会提供 **Rebuild them now**。这些文件会被保留，而不是丢弃——丢弃会让那些类变成过期的却毫无提示。
- 源文件被删除或重命名时，通知会列出如今已无从构建的类。
- *Tools ▸ Rebuild DUI* 会重新编译项目中所有以文本为后端的控件蓝图，并加载尚未加载的那些，且会先询问。

诊断信息读取 `File.dui(line,col): error DUI3001: …`，其首位数字表明是哪个阶段拒绝了它：1 词法分析器、2 解析器、3 语义、4 值、5 构建树、6 编译进蓝图、7 写回。每一条都在
[Diagnostics](../diagnostics/index.md) 中附有原因和修复方法。

<a id="the-shape-of-a-day"></a>

## 一天的形态

<a id="write"></a>

### 编写

在 VS Code 或设计器中编写这棵树：节点、属性、样式、组件。新文件可以从设计器的 **Create Source File...** 开始，它会写入一个带有起始层级的 `.dui`。复用通过组件实现：在
`use "Components/Row.dui" as Row` 之后，`Row Audio { … }` 就是那个类的实例（见
[Components](../language/components.md)）。

<a id="compile"></a>

### 编译

保存。设计器的预览会随之更新。出错时，阅读通知以及诊断中的行号和列号。

<a id="place"></a>

### 放置

在屏幕上：在图表中使用 **Create Dream Widget** 然后 **Add to Viewport**，在 C++ 中使用 `UDreamUIBPLibrary::AddWidgetOfClassToViewport`。在世界上：将控件蓝图拖入关卡，
得到一个 `ADreamWorldWidgetActor`。同一个类，两种用途，其中没有任何改变（见
[Your first screen](first-screen.md) 和 [World space](../concepts/world-space.md)）。

<a id="wire-up-interaction"></a>

### 接上交互

事件只有一种机制，并且处处拼写相同：**路由**。`EventName -> Handler` 在**用户控件**上命名一个函数——这棵树所编译成的类，也是 UMG 放置事件处理的地方。编译器将每条路由解析为 `UDreamWidgetBlueprint::EventBindings`，并由 `UDreamUserWidget::BindEventBindings` 在
Initialize 时挂接它们。在设计器中，同样的事情是详情面板的 *Events* 部分：`+` 会创建一个具有正确签名以及命名它的路由的自定义事件。类中缺少的处理函数是
[DUI6004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6004)；参数不匹配的处理函数是
[DUI6005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6005)。

属性由绑定驱动：`Text <- GetTitle()` 让某个属性跟随一个表达式，`Value <-> Volume`
双向镜像它。见 [Bindings](../language/bindings.md)。

<a id="animate"></a>

### 添加动画

一个 `timeline` 块是文件所拥有的动画：每条轨道一行，包含节点路径、它所驱动的属性以及关键帧。它编译为根节点动画组件中的一个 `UDreamWidgetAnimation`，像任何其他动画一样获得其类成员变量，并通过 `UDreamUserWidget::PlayAnimation` 播放。由于文件拥有它，每次编译都会重建它，并且**动画编辑器以只读方式打开它**；要一劳永逸地把动画交给 Sequencer，请编写
`timeline Name external`。在面板内部，通过其渲染变换补间（`RenderOffsetTo`、
`RenderScaleTo`、`RenderAngleTo`、`RenderTranslationTo`）来为一个组件添加动画，这些只移动绘制的内容，而从不改变布局。见
[Timelines](../language/timelines.md) 和 [Animation](../concepts/animation.md)。

<a id="see-what-it-drew-and-what-it-cost"></a>

### 查看它绘制了什么，以及花费了多少

`DreamUI.Capture` 会将视口以及每个渲染目标画布写成 PNG；`DreamUI.Stats` 会打印自上次被请求以来每个阶段的
开销；`DreamGUI.Memory` 会打印字体、图集和画布所持有的内容。对于绘制错误或未绘制的内容，用 `r.DreamUI.VerifyPartialPrepare 1` 复现它。见 [Debugging](../tools/debugging.md)。

交互和动画放在一起，一个小暂停菜单看起来就像这样：

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

`Native.Button` 是插件按钮控件的注册标签；`OnClicked -> HandleResume` 需要在 `WBP_Pause` 上有一个名为 `HandleResume` 的函数或
自定义事件；`Appear` 会编译成一个动画，并带有自己的类成员变量——将该变量拖入 `PlayAnimation` 即可播放它。

**注意：**

  设计器和文本是同一个类的两种视图：没有应用步骤，也没有需要协调的逐实例覆盖列表。父级提供给子级的内容通过命名插槽传递；见
  [Widget Blueprints](../concepts/widget-blueprints.md)。
