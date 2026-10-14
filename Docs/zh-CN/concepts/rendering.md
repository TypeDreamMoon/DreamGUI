---
title: "渲染"
description: "渲染器如何工作——DreamGUIRenderer 视图扩展、世界画布的两种渲染器、绘制调用批处理、渲染图层、视觉元素上的材质（渲染线程代理、DreamUI_IsRenderByDreamUIRenderer、在 sRGB 编码值上混合且由渲染器自行预乘）、渲染目标画布、屏幕效果，以及 r.DreamUI.Verify* 检查。"
---

<a id="the-renderer"></a>

## 渲染器

DreamGUI 自己的渲染器是 `FDreamUIRenderer`，一个场景视图扩展（`FSceneViewExtensionBase`），位于底层
`DreamGUIRenderer` 模块中。它对控件一无所知：视图扩展、绘制渲染目标画布的渲染命令、着色器、顶点和索引格式、画布用来应答其参数的材质代理、
屏幕效果共用的后处理代理，以及 `DreamUI.Stats` 背后的阶段计时都位于该模块中；
核心模块注册它所需的内容，渲染器负责执行。

它仅针对引擎的公开头文件进行编译：场景深度通过公开的场景纹理 API 读取，并且
静态检查会拒绝任何指向 `Runtime/Renderer/Private` 或 `Internal` 的 include 路径。它在其他视图
扩展中的顺序由 `PriorityInSceneViewExtension` 决定（*Project Settings > Plugins > DreamUI*；数值越大越靠前）。它记录日志到
`LogDreamGUIRenderer`，`stat DreamGUI` 仍然显示其计数器。

由谁绘制一棵树取决于其根画布的渲染模式（参见[画布](canvas.md)）：

| 渲染模式 | 由谁绘制 |
| --- | --- |
| `ScreenSpaceOverlay` | DreamGUI 的渲染器，在场景之后绘制到视口上 |
| `WorldSpace_DreamUI` | DreamGUI 的渲染器，在场景之后按 DreamUI 的顺序绘制，按 `BlendDepth` 的说明被场景深度遮挡；无光照、无后处理 |
| `WorldSpace` | 引擎的渲染器：画布的网格是普通场景图元，接受光照、雾、遮挡和后处理，并按半透明规则排序 |
| `RenderTarget` | DreamGUI 的渲染器，通过其自己的渲染命令绘制到目标中 |

在两种世界空间后端之间如何选择见[世界空间 UI](world-space.md)。渲染器不能
共享渲染数据：一个画布挂到渲染模式与之不兼容的另一个画布下时，其渲染数据会被重新创建。

<a id="batching"></a>

## 批处理

画布按层级顺序收集其元素，并将可以合并的相邻元素——相同材质和纹理、
相同混合方式——合并为一个绘制调用：

- **2D 元素**容易检测重叠，因此它们可以在不改变绘制内容的情况下批处理。一个元素是否为 2D 是在画布的相对空间中决定的：
  其 Z 位置以及 X 和 Y 旋转必须低于 `AutoBatchThreshold`
  （*Project Settings > Plugins > DreamUI*）。
- **3D 元素**几乎不可能检测重叠，因此只能批处理到列表的最后一个绘制调用中。
  这就是为什么渲染变换带有深度、偏航或俯仰的控件会脱离 2D 路径——一张翻转的卡片会多花一个
  绘制调用。
- 混合方式不同的元素永远不会共享绘制调用。由内置着色器绘制的元素使用其视觉元素的
  `BlendMode`（`Alpha`、`Additive`、`Multiply`）；带有材质的元素从材质获取其混合方式，而该
  枚举仍然参与批处理决策。
- 文本样式（描边、发光……）按控件存储在画布的控件属性纹理中，因此具有不同
  样式的文本仍会批处理到一个绘制中。
- 诸如 `StaticMesh` 之类的直接网格视觉元素各占一个绘制调用。

一次更新只遍历请求更改的控件，原地修补几何体发生变化的区段，并且当
控件只是移动时保留其绘制调用，并将移动后的顶点和边界交给它们。`DreamUI.Stats` 统计每帧执行的是其中哪一种（参见[画布](canvas.md)）。

<a id="render-layers"></a>

## 渲染图层

渲染变换持续变化的控件会成为其画布的**渲染图层**：画布将其下方的几何体
保持为相对于它的坐标，并在 GPU 上应用其变换。当它旋转或滑动时，其下方的任何内容都不会再次变换或再次上传——只有它的
世界渲染图层表（`UDreamUIRenderLayerTable`）中的那一行发生变化。任意数量的
图层都可以共享一个绘制调用。

代价在于批处理：图层的元素像 3D 元素一样批处理，只能批处理到紧挨其前的绘制调用中，并且永远不会被画布矩形
剔除。因此画布只在划算时才生成图层：

| 控件上的 `RenderLayer` | 行为 |
| --- | --- |
| `Auto`（默认） | 一旦其渲染变换连续 `r.DreamUI.RenderLayerPromoteFrames` (2) 帧发生变化，就成为一个图层；静止 `r.DreamUI.RenderLayerDemoteFrames` (60) 帧后不再是图层，这样其下方的元素会重新与其画布的其余部分一起批处理 |
| `Always` | 只要能成为图层就始终是图层，无论是否动画 |
| `Never` | 从不：其下方的元素在 CPU 上随其画布的其余部分一起变换 |

一个画布最多生成 `r.DreamUI.RenderLayerMaxPerCanvas` (8192) 个 `Auto` 图层，而 `Always` 忽略该数量；
`r.DreamUI.RenderLayers 0` 不生成任何图层并回收已有的图层。每次提升或降级都会让画布重建一次
绘制调用。图层内的小号文本只有在图层静止 3 帧后才会使用覆盖字形绘制
（参见[文本](text.md)）。`Tools/Bench` 中 5000 个旋转按钮的屏幕正是衡量这一情况（参见
[基准测试](../tools/bench.md)）。

<a id="materials-on-visuals"></a>

## 视觉元素上的材质

内置着色器绘制大多数内容；视觉元素也可以携带自己的材质（文本的 `OverrideMaterial`、
图像的 `SetBrushFromMaterial`）。默认材质是项目设置（`DefaultUIMaterial`、
`DefaultRectBlockMaterial`），可由画布上的 `DefaultMaterial` 覆盖，而 *Use Built-in UI Shader*
（`bUseBuiltInUIShader`）决定没有自己材质的元素是否通过内置着色器。DreamGUI
自己的材质是 DreamShader 输出，由插件 `DShader/` 下的源构建。

<a id="a-canvas-answers-parameters-through-render-thread-proxies"></a>

### 画布通过渲染线程代理应答参数

画布**不会为每个绘制调用创建材质实例**。它赋予材质的参数——主纹理和字体纹理、
其数据纹理（裁剪数据、控件属性……）、渲染器标志——通过该材质的渲染线程代理
（`FDreamUIMaterialProxy`）应答。你赋予控件的材质实例也以同样的方式应答，并且永远不会
被写入，因此你在其上设置的参数归你所有，你在其首次绘制之后设置的参数也会到达屏幕。画布
用于绘制的任何内容都不是关卡中的对象，其中任何内容都不会被复制到游玩会话或粘贴中。

你自己的、需要自己参数的视觉元素重写 `UDreamVisual::AddMaterialParameters(FDreamUIMaterialParameters&)`，
画布会为其代理应答它们（这取代了旧的 `OnMaterialInstanceDynamicCreated`）。

<a id="premultiplying-and-dreamui_isrenderbydreamuirenderer"></a>

### 预乘，以及 `DreamUI_IsRenderByDreamUIRenderer`

DreamGUI 的渲染器**自行预乘**：内置着色器输出预乘颜色（rgb 已乘以
alpha），这正是三种混合模式成为三种混合状态而非三种着色器变体的原因。引擎的
渲染器则不这样做。可能由两者之一绘制的材质必须知道是哪一方在绘制它，因此画布在每次绘制时设置标量
`DreamUI_IsRenderByDreamUIRenderer`：在 DreamGUI 的渲染器下为 `1`，否则为 `0`。DreamGUI 自己的材质
用它来在 `rgb * opacity`（用于引擎的渲染器）和普通 `rgb`（用于 DreamGUI 的渲染器，它自行
相乘）之间进行 lerp。

**注意：**

  画布会应答使用其任何参数的材质——包括仅使用
  `DreamUI_IsRenderByDreamUIRenderer` 标量的程序化材质。在 1.0.0 之前，画布只查看纹理参数，因此这种材质
  从未获得该标志，在 DreamGUI 的渲染器下被预乘了两次，边缘更暗；为此进行补偿的材质现在绘制出来会过亮，因此请移除
  该补偿（参见[升级](../guides/upgrading.md)）。

<a id="colour-and-blending"></a>

### 颜色与混合

颜色按 sRGB 编写（`#RRGGBB` 里的 `.dui` 是 sRGB），着色器在内部把顶点色转成线性。混合发生在哪种值上取决于目标：在视口里（着色器做伽马校正），DreamGUI 混合的是
**sRGB 编码值**；在渲染目标里（sRGB 格式在混合之后才编码），混合的是线性光。所以一个要在材质内部、在已知底色上自己做合成并必须与 DreamGUI 的混合结果一致的材质，在视口里要对
sRGB 编码值做运算，最后才把结果转成线性的直通色。目标是 sRGB 的多重采样画布同样在 sRGB 里混合。

抗锯齿是 `AntiAliasingMethod`（`None` 或 MSAA）和 `MSAASampleCount`，作用于 DreamGUI 渲染器的三种模式；RHI 报告不支持 MSAA 的平台上，
渲染器会回退成不抗锯齿并警告一次，而不是假装开着。Win64 是唯一构建并运行过的平台，见[平台](../guides/platforms.md)。

<a id="render-target-canvases"></a>

## 渲染目标画布

渲染目标画布由**它自己的一个渲染命令**画，用一张它自己的渲染图，在它的网格段变化之后执行，而不是在它所在世界的某个视图里 ——
所以不管有没有东西在渲染那个世界，它都会更新。以前把旧行为放回来的开关（`r.DreamUI.RTDrawer`、`r.DreamUI.MaterialWrappers`）连同旧行为一起删掉了。

`UDreamRetainerBox` 行为就建在这上面。它是 UMG 的 `URetainerBox`：把控件的子树渲染进一张纹理、在中间的帧里重用
（`Phase` / `PhaseCount` 把重渲染分散到多帧，`RequestRender()` 强制一次），用的是同一控件上画布的 `bForceRenderToTarget` 和
`WhenRequest` 更新模式。被保留的子树先合成、再对结果一次性应用 `GroupRenderOpacity`，所以淡出一组互相重叠的孩子时不会彼此透出 ——
这正是逐控件的 `RenderOpacity` 做不到的。

<a id="screen-effects"></a>

## 屏幕效果

`DreamGUIExtensions` 里有三种读写屏幕的视觉，都是 `UDreamVisualPostProcess` 的子类：

| `.dui` 标签 | 类 | 效果 |
| --- | --- | --- |
| `BackgroundBlur` | `UDreamBackgroundBlur` | 模糊它下面的画面 |
| `BackgroundPixelate` | `UDreamBackgroundPixelate` | 像素化 |
| `PixelSort` | `UDreamPixelSort` | 像素排序（`SetMaxSortPasses` 把数量限制在 1 到 512） |

它们共用一套读取、裁剪、写回屏幕的方式，全在渲染图的纹理上。自己写的效果也走这条路：在 `FDreamVisualPostProcessRenderProxy` 里，用 `ReadScreen_RenderThread` 和
`GrabRegion_RenderThread` 读屏幕，在 `CreateWorkTexture` 建的纹理里干活，用 `WriteBack_RenderThread` 写回。

<a id="grabbing-the-result-back-postprocessrenderelement"></a>

### 把结果抓回来：`PostProcessRenderElement`

上面三种是**读屏幕**的视觉。另有两个反过来 —— 把**后处理算出来的结果图像抓回来**，当一块几何显示在它所在的位置：

| `.dui` 标签 | 类 |
| --- | --- |
| `PostProcessRenderElement` | `UDreamPostProcessRenderElement` |
| `PostProcessRenderElementText` | `UDreamPostProcessRenderElement_Text` |

⚠️ **一条必须知道的前提**（源码用 `NOTE!!!` 标出）：**只有目标后处理的 `RenderType` 是 `RenderTarget`、且 `bUseFullSize` 为 `false` 时才有效**。不满足时**它什么都不显示，而且不报错**。

`UDreamPostProcessRenderElement` 的 UV 约定：`UV0` 是矩形 UV（(0,0) 到 (1,1)）；`UV1` 见下面的
[顶点格式](#vertex-format)。它通过四个静态矩阵参数（`DreamUI_World2PostProcess_Row1` … `Row4`）把「世界 → 后处理」的变换交给材质 —— 这就是结果图像对齐到这块
几何上的方式。

⚠️ `PostProcessRenderElementText` **不是它的子类** —— 它在**独立文件**里、**继承 `UDreamText`**：
它是「宿主是一段文本视觉」的那个版本。注意它的标签名比类名**少一个下划线**。

<a id="vertex-format"></a>

## 顶点格式

自己写着色器或网格修改器的人只需要这一个结构体：`FDreamUIMeshVertex`，**恰好 64 字节**。
源码里有一条 `static_assert` 钉住它 —— 顶点声明、上传和几何的逐字节比较都假定没有填充，**所以这是 ABI**。

| 字段 | 属性 | 说明 |
| --- | --- | --- |
| `Position` | 0 | `FVector3f` |
| `Color` | 1 | `FColor` |
| `TextureCoordinate[0..3]` | 2–5 | UV0…UV3 |
| `TangentX` / `TangentZ` | 6 / 7 | 打包法线 |
| `UV4` | **8** | 第五个纹理坐标，**排在最后** |

`UV4` 是 2.0 新加的（顶点从 56 变 64 字节）。它**排在属性 8 而不是插在中间**，是为了让之前的属性一个都没挪位。
材质里当 `TexCoord(4)` 读；**文本用它装渐变绘制**（一个被绘制的字形四边形在它那个渐变盒子里的位置），其它一切写 `(0, 0)`。
用构造函数、`SetNum` 或整结构拷贝造出来的顶点自动是对的；**只有「先把顶点数组撑大、再逐字段填」的代码必须自己写 `UV4`**。

两个默认 UV 通道另有含义：

- **`UV0` 是纹理坐标**；
- **`UV1` 是控件属性数据的坐标** —— x 是数据纹理里的剪裁数据坐标，y 是纹理数组的切片下标，主要给字体渲染用。

⚠️ `UV1` 被画布占着（画布还会在同一次重建的后面把 x 盖回去），所以**一个把顶点位置写进 `UV1` 的网格修改器会丢一半、还破坏字形查找**。其余通道由各自的视觉定义（`DreamText`、`DreamRectBlock` 等等）。

**文本四边形在 `UV2.x` 里带一个 paint 槽**：`UV2.x += 128 * slot`。不绘制的文本槽是 0，它的顶点就是 2.0 写的那套。
**要解码 `UV2.x` 的着色器必须先剥掉槽**（`DreamUIText.ush` 里的 `DreamUIText_UnpackPaintSlot`）；**要重写文本四边形 `UV2.x` 的网格修改器必须把它保住**。

另有**五个全局缓冲资源**：全屏 quad 的顶点缓冲、索引缓冲、切片 quad 的索引缓冲（**三个都是 `TGlobalResource`、全局各一份** —— 用 `static` 会给**每个包含它的 `.cpp` 各造一份**、各占一组 GPU 缓冲），加上网格顶点缓冲 `FDreamUIMeshVertexBuffer` 与顶点声明
`FDreamUIMeshVertexDeclaration`。

<a id="indices-are-16-bit"></a>

### 索引是 16 位的

索引默认 **16 位**（`FDreamUIMeshIndex` = `uint16`）：半个索引的内存与带宽，代价是**一个 draw call 最多 65535 个顶点**（约 16k 个 quad）。真要单个元素超过，就得定义 `LEXUI_USE_32BIT_INDEXBUFFER`。

这条也是「顶点预算」的出处：顶点多到一个绘制调用的索引装不下时，网格修改器会**每个只报一次错、然后不画那个副本**。

<a id="checks"></a>

## 检查

报告"画错了"或"没画出来"时，这两个控制台变量很有用（测试套件运行时两个都开着）：

| 变量 | 检查内容 |
| --- | --- |
| `r.DreamUI.VerifyPartialPrepare 1` | 画布每次基于上一次准备所做的部分准备，都会与每个控件的一次准备进行对照，不一致就会触发一个说出画布名字的 ensure。代价是每次都要做一次完整的准备 |
| `r.DreamUI.VerifyKeptPointers 1` | DreamGUI 保留下来、不每帧重新查找的对象——控件的画布、画布的渲染层、UI 管理器的画布、动画属性的对象——在每次使用时同样会重新查找，不一致就是错误，并触发一个 ensure 指明是哪一个 |

`DreamUI.Capture` 会把视口和每个渲染到目标的根画布写成 PNG，`DreamUI.Stats`
打印每个阶段的耗时，`DreamGUI.Memory` 则列出字体图集、精灵图集页以及每个世界的画布网格
段。完整的流程见[调试](../tools/debugging.md)。

自定义着色器和网格修改器请从上面的[顶点格式](#vertex-format)开始。
