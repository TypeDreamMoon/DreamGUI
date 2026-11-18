---
title: DreamMCP 端点
description: DreamMCP 服务器借以访问 DreamGUI 的 HTTP 端点——针对 .dui 界面执行 check、compile、layout、preview、diagnostics、lookup 与 info，可在用户的编辑器中运行，也可在服务器启动的宿主中运行——并说明 layout 列出什么、布局规则及其阈值，以及 preview 测量什么。
---

通过 [DreamMCP](https://github.com/TypeDreamMoon/dream-mcp) 连接的 AI 代理会得到一个 `dream_gui` 工具。代理通过它检查和编译 `.dui` 文件，查看界面中每个节点在某个视口尺寸下落在何处，以及人一眼就能看出的问题（`layout`），把界面画成一张图片（`preview`），读取诊断信息，并查找编译器接受的名称。当用户的编辑器打开时，由它来应答这些调用；未打开时，则由服务器启动的宿主进程应答。

该端点不添加自己的编译器：每个操作都转发给插件已有的能力——解析器与树构建器、控件蓝图编译器、诊断信箱、[VS Code 扩展](vscode.md) 读取的符号导出，以及设计器实例化界面的方式。它添加的是为所有这些提供统一的应答结构，以及编写 `.dui` 的模型否则无法看到的两样东西：以文字描述的布局后的界面，以及图片。

| | |
| --- | --- |
| 类型 | 编辑器服务，位于 `DreamGUIEditor` 模块（`Source/DreamGUIEditor/Private/MCP/`）中 |
| 协议 | DreamMCP 协议 `1` |
| 清单 | `<Plugin>/Resources/DreamMCP.json`：各操作、其以 JSON Schema 表示的参数、路由、宿主 commandlet |
| 参考资料 | `.skill/reference/dui.md`、`example.md`、`presets.md`、`blocks.md`、`screens.md`、`pitfalls.md`、`controls.md`、`diagnostics.md`，由清单指定，随插件一起打包。每一份都独立成篇：其中的链接指向本手册，打不开它们的代理也不会错过任何必需的内容 |
| 路由 | `http://127.0.0.1:<port>/dream/gui`，工具名 `dream_gui` |
| 端口 | `47800`，或 `-DreamMCPPort=<n>`；无法绑定的端口会跳到下一个，最多 16 个。同一进程中的其他 Dream 插件共享它，各自在自己的路由下 |
| 绑定地址 | 仅 `127.0.0.1`，在启动时验证 |
| 身份验证 | 每个请求都带 `X-Dream-Token` 头；令牌取自环境变量 `DREAM_MCP_TOKEN`，否则用 `-DreamMCPToken=<t>`，再否则随机生成。带 `Origin` 头的请求会被拒绝（403） |
| 发现 | `<Project>/Saved/DreamMCP/gui.json`，在端点应答一次 hello 后写入（它自己的 hello，或服务器发来的第一个 hello，在该 hello 被应答之前），停止时若仍指向本进程则删除 |
| 宿主 | 带 `-DreamMCPHost -RenderOffscreen` 的编辑器（绘制）；`-run=DreamGUIHost -nullrhi -DreamMCPHost`（不绘制，仍可布局） |
| 关闭方式 | `-NoDreamMCP`；在用户的编辑器中还可通过 *Project Settings ▸ Plugins ▸ Dream GUI ▸ Editor ▸ Enable DreamMCP Endpoint*（默认开启，启动时读取） |

<a id="which-process-serves-it"></a>

## 由哪个进程提供服务

| 进程 | 端点 | `mode` | 绘制 |
| --- | --- | --- | --- |
| 用户的编辑器 | 是，引擎初始化之后 | `editor` | 是 |
| 渲染宿主：带 `-DreamMCPHost -RenderOffscreen` 的编辑器，不带 `-run=` | 是 | `host` | 是 |
| 无头宿主：`-run=DreamGUIHost -nullrhi -DreamMCPHost`，或其他 Dream 插件的宿主 commandlet | 是 | `host` | 否 |
| 任何其他 commandlet（`-run=DreamUICompileWidgets`、cook）、自动化运行、游戏 | 否 | — | — |

宿主不是任何人的编辑器：它不写编辑器设置、不自动保存、不渲染实时视口，在进程中没有 Dream 端点有任务时把帧率限制为 15（控制台变量 `DreamMCP.BusyEndpoints` 为 0，每个 Dream 插件的端点都会在有任务时把自己计入、结束时把自己计出），接受 `POST /dream/gui/shutdown`，在启动它的进程消失时退出（`-DreamMCPParentPid=<pid>`，无父进程时用 `0`），若在 `-DreamMCPStartupTimeout` 秒内无法开始应答则以代码 3 退出（默认 600）。在用户的编辑器中，`/shutdown` 会被拒绝（409，`not-a-host`）。

内容的写入位置：在用户的编辑器中编译会把控件蓝图留在内存中供用户保存，就像保存 `.dui` 一样；在宿主中编译则会保存它们。图片写入 `<Project>/Saved/DreamMCP/artifacts/<job>.png`。

<a id="actions"></a>

## 操作

| 操作 | 耗时 | 写入 | 参数 | 应答 |
| --- | --- | --- | --- | --- |
| `check` | 是 | 否 | `file`，或 `all: true` | 按编译器的方式解析文件并构建其树，构建出的树会被丢弃。报告词法、语法、语义、取值和构建器错误（DUI1xxx–5xxx）；只有编译才能看到的错误（属性、事件、视图模型、表达式绑定：DUI6xxx）需要 `compile`。文件中放置的尚未编译的组件不算源文件的错误：它的 DUI5018 是信息，摘要中会指出它，`next` 会提示运行 `layout` 或 `compile`，由它们先进行编译 |
| `compile` | 是 | 是 | `file`, `force` | 构建文件 `class` 行所命名的控件蓝图，不存在时创建它，并先构建文件通过 `use "X.dui" as X` 放置的每个组件。自本进程上次编译以来未发生变化的内容不会重新编译，除非 `force` |
| `diagnostics` | 否 | 否 | `file` | 引擎从每个文件上次 check 或 compile 中保留的内容，错误在前，不进行编译 |
| `lookup` | 否 | 否 | `name`, `kind`, `limit` | 精确的节点类型、组件、枚举或视图模型类会给出其条目（属性含类型、默认值以及来自 C++ 源码的一行描述，还有事件），一个一行显示在应答文本中；`Type.Property` 缩小为以此开头的属性（同时配合 `kind: "property"`）；否则列出所有包含该文本的名称。`Widget` 列出每个节点接受的项，`slot` 列出 `@slot` 接受的项，`Widget.RenderRotation`、`slot.HorizontalAlignment` 对它们进行缩小。锚点和渲染变换会说明各自的轴（Y 向上，X 为深度） |
| `layout` | 是 | 是 | `file`, `width`, `height`, `sizes`, `root`, `depth`, `force` | 像 `compile` 那样编译界面，在视口尺寸下布局但不绘制，并列出每个节点和问题（见下文） |
| `preview` | 是 | 是 | `file`, `width`, `height`, `background`, `timeline`, `time`, `frames`, `duration`, `force` | 编译，把界面绘制成 PNG，并对其进行测量（见下文）。需要能绘制的进程 |
| `info` | 否 | 否 | — | 插件版本与提交、引擎版本与类型、操作系统、区域设置、模式、本进程是否绘制、源文件夹 |

参数和应答中的路径相对于项目根目录（`DUI/Menu.dui`）；相对于 `DUI/` 文件夹的路径（`Menu.dui`）也能找到。`help` 和 `reference` 由服务器添加。

<a id="what-layout-lists"></a>

## `layout` 列出什么

界面会被放到端点自己的舞台上——一个预览世界、一个视口大小的根、按设计器实例化方式在其下实例化的已编译类——并反复布局，直到每个字形都就位且两次遍历结果一致。用户的关卡丝毫不会被触碰。布局不读取像素，因此无头宿主的界面布局效果与编辑器一样好。

应答的摘要是一句话——文件、视口、节点数量、问题数量以及最严重的问题。其 `text` 由服务器按原文显示在摘要之后，内容是界面，一个节点一行，子节点缩进在父节点下：

```text
Laid out DUI/Menu.dui at 1920x1080: 7 nodes, 1 problem (1 warning(s), 0 info); the most serious: ...
The screen, one node a line (x, y from the top left of the viewport, in pixels):
Widget Root  x=0 y=0 w=1920 h=1080  [Overlay]
  Widget Card  x=701 y=466 w=519 h=148  [Overlay]
    Image Backdrop  x=701 y=466 w=519 h=148
    Widget Column  x=701 y=466 w=519 h=148  [VerticalBox]
      Text Heading  x=725 y=494 w=471 h=33  text="Hello, DreamGUI" size=28
```

每一行依次是该节点在文件中写出的类型、它的 id、它在视口像素中的矩形（左上角为原点，Y 向下，取整）、它的文本及其字号、文本放不下时的 `CUT` 或 `OVERFLOWS(needs WxH)`，以及方括号中其他重要信息：`hidden` / `inactive` / `transparent`、`pressable`、它自身的布局盒、没有盒子放置它时锚点如何固定它（`centred`、`fills its parent`、`held at the top left`...）、`partly clipped`、`turned in 3D`、`own canvas X`，以及 `part of its control` 表示控件为自身构建的内容（按钮的表面）。`depth`（默认 6）限制列出的层级，列表最多 250 行；答案的 `next` 说明有多少节点被省略以及如何查看它们；`root` 列出一个节点及其下的内容。

`data.tree` 是同样的树，形式为 JSON：每个节点有 `type`、`name`、`path`、`rect`、被裁剪时的 `visibleRect`、`visible`、`order`（在其画布中的绘制顺序）、`canvas`、`pressable`、`container`、`placedBy`、`anchors`、`in3d`、`text`（`text`、`fontSize`、`cut`、`overflows`、`neededW`、`neededH`）、`file` 和 `line`。在 3D 中旋转（或被剪切，或位于 `Perspective` 内）的节点，以画布相机绘制它时其轮廓的边界为准——它的四个角经过绘制其几何体所用的矩阵和画布的视图投影——因此它的矩形就是画面中显示的位置；该行会说明 `turned in 3D, rect as drawn in perspective`。当某个角位于视点后方时，它会说明 `rect approximate`，并给出正对着看到的轮廓（`approximate` 中的 `data.tree`）。

<a id="which-way-a-turned-face-looks"></a>

### 翻转的面朝向哪一侧

看不到画面的读者无法分辨一个面板是朝向屏幕中央还是背向屏幕，因此 `layout` 用文字说明这一点。每个在 3D 中翻转的面——一个面板、一张卡片、一块板，其自身平面与它所在的面（或屏幕）倾斜 2 度或更多，每个方向 24 px 或更多——都会在树之前得到一行说明；位于该面上的内容随之倾斜，不再重复列出。摘要以每个面的几句话结尾（`In 3D: RosterLeft faces toward the centre, Board tilted back.`）：

```text
Turned in 3D, as drawn (one line a face; the farther edge is drawn shorter):
  Panel RosterLeft, on the left: turned 14 degrees: faces toward the screen centre (right edge farther)
  Panel RosterRight, on the right: turned 14 degrees: faces away from the screen centre (right edge farther)
  Widget Board, in the middle: tilted back 35 degrees: top edge farther and narrower
```

- **角度**是面的平面相对于屏幕平面的角度：绕垂直轴（`turned`）和绕水平轴（`tilted`）。
- **哪条边更远**是从画面上读出的，而非从属性读出：在两条相对的边中，取画布通过控件的绘制矩阵（其中折入了 `Perspective`）和画布的相机绘制得更短的那一条；绘制得一样时，取更深的那一条。`narrower` 表示更远的边被绘制得比更近的边更窄。
- **它朝向哪边**取决于其更远边的方向：右边靠后，它就朝右。中心位于视口宽度中点左侧或右侧超过 8 % 的面，朝向 `toward the screen centre` 或 `away from`；更靠近中间的则朝向 `left` 或 `right`。向后倾斜时上边更远，向前倾斜时下边更远。翻转超过 90 度的面会说明其背面朝向观察者。

这些是事实，而非发现：没有警告也没有信息，而某个翻转是否是屏幕所需由读者判断。最多列出 12 个面；`data.faces` 包含全部内容，每个面有 `name`、`path`、`file`、`line`、`centre`（`x`、`y`）、`side`（`left`、`right`、`middle`）、`turn`（`degrees`、`fartherEdge`、`faces`、`towardCentre`）、`tilt`（`degrees`、`fartherEdge`、`leans`：`back` 或 `forward`、`fartherEdgeNarrower`）、`back`，以及 `text`，即那一行。`preview` 给出与其 `text` 相同的行、其摘要末尾相同的文字，以及 `data.faces`。让面按屏幕通常所需方式翻转的迹象见 [Turned in 3D](../language/building-blocks.md#turned-in-3d)。

`sizes` 会在更多视口下布局该屏幕（`"1280x720, 2560x1440, 1080x1920"`，最多五个）。在其中之一发现、而在 `width` x `height` 下未发现的问题会报告为 DUI8006，并指明该尺寸。

<a id="the-layout-rules"></a>

## 布局规则

它们从不会让编译失败；它们由 `layout`（以及 `preview` 中的 DUI8007）以警告和信息的形式给出，每条都带有节点的路径、写出它的行（或生成它的控件）以及提示。一个分支中的一个问题只报告一次，不会对其中的每个节点重复报告；被多次放置的组件内部的问题是一条条目，其中会指明其他实例（`(the same at Root/CardB/Mute, ...)`）；在同一位置重复出现的任何发现都会说明 `(reported N times)`。这对每个结果都成立，编译错误也不例外：多个节点共用的 `style` 中某一行上的错误是一条条目，`(raised 3 times by this one line, once for each node that uses it)`，并且只计一次。

| 代码 | 严重性 | 报告条件 | 阈值 |
| --- | --- | --- | --- |
| [DUI8001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8001) | 警告 | 节点能显示的部分（其矩形被裁剪到其裁剪祖先）越过视口边缘，或完全位于视口之外；或放不下其盒子的文本溢出到屏幕上没有裁剪它的地方（从底部对齐的盒子向上溢出，从居中的盒子向两端溢出） | 超过 1 px |
| [DUI8002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8002) | 警告 | 文本被其溢出策略截短，或被布局到超出其盒子 | 文本自身的度量（`UDreamText::MeasureFit`） |
| [DUI8003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8003) | 警告（对于带子级的普通控件为信息） | 会绘制、容纳子级或可被按下的节点没有宽度或高度；可按下控件小于指针所需 | 小于 1 px；小于 24 x 24 px（WCAG 2.2 的最小目标） |
| [DUI8004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8004) | 警告 | 同一画布中的两个可按下控件在屏幕上重叠，无论它们在树中的位置如何（一个位于另一个内部的情况除外）：那里的按下只会到达上面那个。至少 95 % 落在面积至少为其四倍的控件上的小控件（可按下卡片上的按钮、点击外部关闭的背景上的对话框）是有意分层，不会被报告 | 两个轴上都达到 2 px 或更多 |
| [DUI8005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8005) | 信息 | 同级节点的左、右、上或下边缘或中心几乎对齐；一行或一列三个或更多节点具有近乎均匀的间距。在任意一边都不超过 128 px 的框内，纯视觉元素（无文本、无可按压内容）属于绘图的一部分或由形状绘制的图标，不参与比较 | 相差超过 0.5 且至多 3 px |
| [DUI8006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8006) | 警告 | DUI8001–8004、DUI8008 或 DUI8009 在 `sizes` 的某一处出现，且不在第一个视口中 | 与其所重复的规则相同 |
| [DUI8007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8007) | 警告 / 信息 | 在 `preview` 中：文本的颜色（按其 alpha）与其盒子内侧一圈一像素宽环上像素的中位数对比。控件自身某部分与背后内容的对比度在 1.1 : 1 以内（关闭状态的开关的勾选标记）属于有意隐藏，不会被报告 | WCAG 对比度低于 3 : 1 为警告；小于 24 px 的文本低于 4.5 : 1 为信息 |
| [DUI8008](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8008) | 警告 | 节点绘制的内容超出其所在的面板或卡片：绘制盒子的最近祖先（非文本的视觉元素，每个方向至少 24 px），或者在覆盖层中，绘制内容填满父级的最后一个更早的兄弟节点——覆盖在阴影之上的表面。不报告：标记为 `Bleed` 的节点或位于其中的节点、会裁剪的框架（或介于其间的节点）、与屏幕同尺寸的框架（DUI8001 负责的情形）、每个方向至少占其框架 90 % 且覆盖其大部分区域的底板（覆盖在其阴影上的表面、卡片后面的光晕） | 超过 4 px，且至少达到节点自身在该侧宽度或高度的 5 % |
| [DUI8009](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8009) | 警告 | 一个不接收指针的视觉元素，在同一画布中绘制在文本之后（在文件中更靠后，或位于更高层），覆盖了其字形——或者，不大于文本行且大部分位于其中。不报告：绘制在文本之前的内容（其背板）、其祖先或后代、控件（DUI8004）、没有主体的轮廓（`bEnableBody = false`）或比 40 / 255 更淡的轮廓、覆盖文本所在卡片 90 % 以上的蒙层、覆盖屏幕 60 % 以上的遮罩、标记为 `Bleed` 的节点、控件自身标签周围的控件自身部件。此外，文本横跨在绘制于其之前的形状边缘上，部分字母在形状上、其余在形状外（一个标签滑到下一个徽章上），且该形状不超过字形框的 8 倍——徽章、图标、下一项的图片，而不是背景形状或光晕。不报告：承载文本的形状（其底板）、位于文本所在面板或底板之后的形状（绘制在形状之后，无论是否透明）、文本所在面板的背板、环形或线条（轮廓）、控件、标记为 `Bleed` 的节点。每个文本只产生一个发现 | 字形框的 12 %，或行内装饰的 30 %；且任一至少 4 px 厚。跨边缘：形状上覆盖字形框的 12 % 到 88 %，至少 4 px 厚 |
| [DUI8010](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8010) | 信息 | 面板（绘制盒子的节点，或覆盖层的底板，至少 240 x 120 px 且占屏幕 2 %，但小于屏幕的 60 %）内含文本和三个或更多内容，依据它们绘制的内容判断（忽略细于 6 px 的分隔线和强调条）：其内容偏居一侧，或在其中部横向留出一条空白带，或其行（列）分布稀疏。对于条形栏或侧边栏（长度是宽度的 2.5 倍），不检查其中部和分布 | 一侧空白 38 %，而相对一侧低于其三分之一，或在该轴 60 % 为空时一侧空白 20 %；中部条带为 38 %；项目之间的间隙占 60 % |

两个节点会在其中之一的平面内进行比较——框架、文本或面板的平面——通过其控件的变换，因此在 3D 中翻转的卡片、旋转的徽章或处于透视中的屏幕，其边缘仍保持笔直；矩形为近似值的节点（某个角点位于视点之后）会被排除。节点绘制的内容是其视觉元素的几何形状：文本的字形、多边形的角点（翻转后的形状不按其包围盒测量）、图像的四边形、矩形块的主体，对于 DUI8009，当其不透明度至少为 40 / 255 时，还包括矩形块的外阴影或光晕。文本横跨的形状按其凸轮廓测量（多边形的角点，任何其他视觉元素的四个角点），且不超过其裁剪祖先允许显示的范围。

<a id="going-past-the-frame-on-purpose-tag-bleed"></a>

### 有意超出框架：`@tag Bleed`

光晕、投影、溢出其卡片的角标、覆盖在标题上的光泽：给节点标记 `Bleed`，DUI8008 和 DUI8009 便会忽略它及其内部的一切。标记为 `Bleed` 的框架也不参与这两项测量。

```
RectBlock CardGlow {
    @tag Bleed
    AnchorData.SizeDelta = (360, 520)
}
```

会裁剪的容器（`Clipping = ClipToBounds`）不需要标记：它裁掉的内容不会被绘制。

<a id="what-preview-measures"></a>

## `preview` 测量什么

屏幕在同一舞台上绘制到与图片同尺寸的渲染目标中，前提是画面已静止三帧，没有着色器在编译，也没有字形待处理（最多两分钟；超时的话答案会说明）。颜色是文件中的颜色：写入的 `#1B1E26` 读回为 `#1B1E26`。随后把同一帧分别绘制在黑色和白色之上：

| 指标 | 定义 |
| --- | --- |
| 覆盖 | 在黑色和白色之上每个通道相差不超过 8 的像素，或其值介于该范围与 247 之间（部分透明）的像素；随背景变化的像素不属于覆盖 |
| `coverage` | 覆盖像素占所有像素的比例 |
| `meanColor`, `meanColorName` | 覆盖像素在画面中的平均值，以及它的通俗名称（饱和度低于 0.12 为灰色，低于 0.4 为带色调的灰色） |
| `luminance` | 按 Rec. 709 权重对 PNG 的编码值计算：最小值、最大值、平均值、标准差 |
| `nearWhite`, `nearBlack` | 每个通道都在 240 或以上 / 12 或以下的覆盖像素 |
| `flat` | 亮度及每个通道的标准差至多 0.012，且至多 16 个覆盖像素在某一通道上与画面平均色的偏差达到 24 或以上；带有控件和标签的纯色背景并不算平坦，摘要会说明其上绘制了多少内容 |

摘要以最关键的一个事实开头：屏幕几乎什么都没绘制（覆盖低于 0.5 %）、接近黑色（90 % 或以上）、接近白色（30 % 或以上）、单一纯色，否则就是其覆盖率、平均颜色和亮度范围；然后是难以阅读的文本数量（DUI8007）、`layout` 会报告的布局问题数量，以及图像的路径。一帧条带会由它们全部来描述（`Over its 16 frames it covers ...`，包含覆盖最少和最多的帧），绝不会只以第一帧来描述。

`background` 是屏幕背后显示的内容：`#RRGGBB`（默认 `#1C1E26`）或 `checker`。`timeline` 块不会自行播放：`timeline` 指定文件中的一个，在屏幕稳定后从开头循环播放。`time` 会让屏幕先运行这么多秒再截取画面，并推进时间轴、补间和粒子；`frames`（最多 16 帧）分散在 `duration` 秒内，并排放入一张 PNG（最宽 8192 px），`data.animation` 说明它们是否不同、差异多大，以及是否来回跳动（需要更多帧或更短的时长）。`RenderTranslation` 关键帧沿 X 轴朝靠近或远离观察者的方向移动（X 为深度，Y 向右，Z 向上）：`(0, -400, 0)` 到 `(0, 400, 0)` 会把节点横向滑动，`(-400, 0, 0)` 会把它拉近 400 个单位并绘制得大得多。

<a id="screens-in-3d"></a>

### 3D 中的屏幕

| 项目 | 在 `preview` 中 |
| --- | --- |
| 使控件离开画布平面的渲染变换（`RenderRotation`、带深度的 `RenderTranslation`） | 以透视方式、通过画布自身的相机绘制，与游戏绘制屏幕空间画布的方式相同 |
| 图层效果、混合模式、裁剪遮罩、矩形块、文本效果 | 绘制：它们是屏幕自身绘制的一部分 |
| 在关卡中的面板上显示的屏幕（世界空间画布），通过相机观察 | 不绘制：舞台会把屏幕本身平面地绘制到其图片中；把屏幕放进关卡并在编辑器中查看 |
| 屏幕中的 Niagara 粒子 | 绘制：舞台持有每个系统，并按预览自身的时间推进它，因此 `time` 和各帧显示的是游戏在相应秒数后应有的粒子；等待指针或点击的系统（`bAutoActivate = false`）在有什么启动它之前不显示任何内容 |

<a id="see-also"></a>

## 另请参阅

- [使用 AI 代理](ai-agents.md)：技能、脚本，以及没有 DreamMCP 时代理会做什么。
- [诊断](../diagnostics/index.md)：每一个 `DUInnnn` 代码。
- [从命令行编译](scripting.md#compiling-from-the-command-line)：由 `.skill/dui.ps1` 驱动的 commandlet。
