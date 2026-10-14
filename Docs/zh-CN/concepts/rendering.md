---
title: 渲染
description: 渲染器怎么工作：DreamGUIRenderer 视图扩展、世界画布的两种渲染器、draw call 合批、渲染层、视觉上的材质（渲染线程代理、DreamUI_IsRenderByDreamUIRenderer、sRGB 编码值上的混合与渲染器自己做的预乘）、渲染目标画布、屏幕效果，以及 r.DreamUI.Verify* 检查。
---

<a id="渲染器"></a>

## 渲染器

DreamGUI 自己的渲染器是 `FDreamUIRenderer`，一个场景视图扩展（`FSceneViewExtensionBase`），住在最底层的 `DreamGUIRenderer`
模块里。它对控件一无所知：视图扩展、渲染目标画布的渲染命令、着色器、顶点和索引格式、画布用来回答材质参数的材质代理、
屏幕效果共用的后处理代理、`DreamUI.Stats` 背后的阶段计时 —— 都在这个模块里，核心模块注册它要什么，渲染器照做。

它只用引擎的公开头文件编译：场景深度通过公开的 scene-texture API 读取，任何指向 `Runtime/Renderer/Private` 或 `Internal` 的 include
都会被静态检查拒绝。和其他视图扩展之间的先后由 `PriorityInSceneViewExtension` 决定（*Project Settings > Plugins > DreamUI*，大的先）。
渲染器的日志类别是 `LogDreamGUIRenderer`，`stat DreamGUI` 仍然显示它的计数器。

谁来画由根画布的渲染模式决定（见[画布](canvas.md)）：

| 渲染模式 | 谁来画 |
| --- | --- |
| `ScreenSpaceOverlay` | DreamGUI 的渲染器，在场景之后画到视口上 |
| `WorldSpace_DreamUI` | DreamGUI 的渲染器，在场景之后按 DreamUI 的排序画，可以按 `BlendDepth` 被场景深度遮挡；不受光照和后处理影响 |
| `WorldSpace` | 引擎的渲染器：画布的网格是普通的场景图元，参与光照、雾、遮挡和后处理，按半透明规则排序 |
| `RenderTarget` | DreamGUI 的渲染器，用一个自己的渲染命令画进渲染目标 |

两种世界空间后端怎么选见[世界空间 UI](world-space.md)。不同渲染器的渲染数据不能共用：一个画布被挂到渲染模式不兼容的
另一个画布下时，渲染数据会重新创建。

<a id="合批"></a>

## 合批

画布按层级顺序收集元素，把相邻、可以合并的元素（同样的材质和纹理、同样的混合方式）合进一个 draw call：

- **2D 元素**容易判断是否重叠，可以在不改变绘制结果的前提下合批。判断一个元素是不是 2D，是把它换到画布的相对空间里，看
  Z 位置和 X / Y 旋转是否小于 `AutoBatchThreshold`（*Project Settings > Plugins > DreamUI*）。
- **3D 元素**几乎没法判断重叠，所以只允许合进 draw call 列表里的最后一个。渲染变换带深度、偏航或俯仰的控件就是这样掉出 2D 路径的 ——
  翻一张卡片多花一个 draw call。
- 混合方式不同的元素永远不能共用一个 draw call。内置着色器画的元素用视觉上的 `BlendMode`（`Alpha`、`Additive`、`Multiply`）；
  带材质的元素用材质自己的混合方式，但这个枚举仍然参与合批判断。
- 文本样式（描边、发光……）按控件存在画布的控件属性纹理里，所以样式不同的文本仍能合进一次绘制。
- `StaticMesh` 这类直接网格视觉每个都是一个 draw call。

更新只走请求了变化的控件，几何变了的网格段原地打补丁，控件只是移动时保留 draw call、把移动后的顶点和包围盒交给它们。
`DreamUI.Stats` 的计数器能看出每帧发生的是哪一种（见[画布](canvas.md)）。

<a id="渲染层"></a>

## 渲染层

渲染变换不停变化的控件会成为它的画布的**渲染层**：画布把它下面的几何保存为相对于它的坐标，在 GPU 上应用它的变换。
于是它转动或滑动时，下面的东西既不用重新变换，也不用重新上传 —— 只改世界渲染层表（`UDreamUIRenderLayerTable`）里属于它的一行。
任意多个渲染层可以共用一个 draw call。

代价在合批上：渲染层的元素像 3D 元素一样只合进紧挨在前面的那个 draw call，并且永远不会被画布矩形剔除。所以画布只在需要时才这么做：

| 控件上的 `RenderLayer` | 行为 |
| --- | --- |
| `Auto`（默认） | 渲染变换连续 `r.DreamUI.RenderLayerPromoteFrames`（2）帧都在变就成为渲染层；静止 `r.DreamUI.RenderLayerDemoteFrames`（60）帧后撤销，让下面的东西重新和画布其余部分合批 |
| `Always` | 只要能就是渲染层，动不动都一样 |
| `Never` | 永远不是：下面的东西和画布其余部分一起在 CPU 上变换 |

一个画布最多有 `r.DreamUI.RenderLayerMaxPerCanvas`（8192）个 `Auto` 渲染层，`Always` 不受这个数限制；`r.DreamUI.RenderLayers 0`
不做任何渲染层，并收回已有的。每次提升或撤销都让画布重建一次 draw call。渲染层里的小字号文本在层静止 3 帧之后才改用覆盖字形
（见[文本](text.md)）。`Tools/Bench` 里 5000 个转动按钮的基准屏幕量的就是这种情况（见[基准测试](../tools/bench.md)）。

<a id="视觉上的材质"></a>

## 视觉上的材质

内置着色器画大多数东西；视觉也可以带自己的材质（例如文本的 `OverrideMaterial`、图片的 `SetBrushFromMaterial`）。默认材质在
项目设置里（`DefaultUIMaterial`、`DefaultRectBlockMaterial`），画布上的 `DefaultMaterial` 可以覆盖；*Use Built-in UI Shader*
（`bUseBuiltInUIShader`）决定没有自己材质的元素是否走内置着色器。DreamGUI 自己的材质是 DreamShader 生成的，源文件在插件的 `DShader/` 下。

<a id="画布通过渲染线程代理回答参数"></a>

### 画布通过渲染线程代理回答参数

画布**不为每个 draw call 建材质实例**。它要给材质的参数 —— 主纹理和字体纹理、它的数据纹理（剪裁数据、控件属性……）、
渲染器标志 —— 通过该材质在渲染线程上的一个代理来回答（`FDreamUIMaterialProxy`）。一个你交给控件的材质实例也这样回答，
而且永远不会被写入，所以你在它上面设的参数始终是你的；首次绘制之后再设的参数也会到达屏幕。画布用来画的东西没有一样是关卡里的对象，
也不会被复制进 PIE 会话或粘贴里。

自己写的视觉要给材质额外的参数，覆写 `UDreamVisual::AddMaterialParameters(FDreamUIMaterialParameters&)`，画布替它的代理回答
（它取代了旧的 `OnMaterialInstanceDynamicCreated`）。

<a id="预乘与-dreamui_isrenderbydreamuirenderer"></a>

### 预乘与 `DreamUI_IsRenderByDreamUIRenderer`

DreamGUI 的渲染器**自己做预乘**：内置着色器输出预乘过的颜色（rgb 已乘过 alpha），三种混合方式因此只是三个混合状态，而不是三套着色器排列。
引擎的渲染器不做。一个既可能被 DreamGUI 渲染器、也可能被引擎渲染器画的材质，就要知道是谁在画它 ——
画布每次绘制都把标量参数 `DreamUI_IsRenderByDreamUIRenderer` 设好：DreamGUI 渲染器下为 `1`，否则为 `0`。
DreamGUI 自己的材质就是用它在 `rgb * opacity`（给引擎渲染器）和原样的 `rgb`（给 DreamGUI 渲染器，它会自己乘）之间插值。

**警告：**

材质里只要用到画布的任何一个参数，画布就会回答它 —— 包括只有 `DreamUI_IsRenderByDreamUIRenderer` 这一个标量的程序化材质。
  1.0.0 之前画布只看纹理参数，这样的材质拿不到标志、在 DreamGUI 渲染器下被预乘了两次，边缘偏暗；为此做过补偿的材质现在会偏亮，
  把补偿去掉（见[升级](../guides/upgrading.md)）。

<a id="颜色与混合"></a>

### 颜色与混合

颜色按 sRGB 编写（`.dui` 里的 `#RRGGBB` 是 sRGB），着色器在内部把顶点色转成线性。混合发生在哪种值上取决于目标：
在视口里（着色器做伽马校正），DreamGUI 混合的是 **sRGB 编码值**；在渲染目标里（sRGB 格式在混合之后才编码），混合的是线性光。
所以一个要在材质内部、在已知底色上自己做合成并和 DreamGUI 的混合结果一致的材质，在视口里要按 sRGB 编码值做运算，
最后才把结果转成线性的直通色。
目标是 sRGB 的多重采样画布同样在 sRGB 里混合。

抗锯齿是 `AntiAliasingMethod`（`None` 或 MSAA）和 `MSAASampleCount`，作用于 DreamGUI 渲染器的三种模式；RHI 报告不支持 MSAA 的平台上
会回退成不抗锯齿并警告一次，而不是假装开着。Win64 是唯一构建并运行过的平台，见[平台](../guides/platforms.md)。

<a id="渲染目标画布"></a>

## 渲染目标画布

渲染目标画布由**它自己的一个渲染命令**画，用一张它自己的渲染图，在它的网格段变化之后执行，而不是在它所在世界的某个视图里 ——
所以不管有没有东西在渲染那个世界，它都会更新。以前把旧行为放回来的开关（`r.DreamUI.RTDrawer`、`r.DreamUI.MaterialWrappers`）连同旧行为一起删掉了。
`UDreamRetainerBox` 行为就建在这上面：它是 UMG `URetainerBox` 的对应物，把控件的子树渲染进一张纹理、在中间的帧里重用
（`Phase` / `PhaseCount` 把重渲染分散到多帧，`RequestRender()` 强制一次），用的是同一控件上画布的 `bForceRenderToTarget` 和
`WhenRequest` 更新模式。被保留的子树先合成、再一次性乘上 `GroupRenderOpacity`，所以淡出一组互相重叠的孩子时不会彼此透出 ——
这正是逐控件的 `RenderOpacity` 做不到的。

<a id="屏幕效果"></a>

## 屏幕效果

`DreamGUIExtensions` 里有三种读写屏幕的视觉，都是 `UDreamVisualPostProcess` 的子类：

| `.dui` 标签 | 类 | 效果 |
| --- | --- | --- |
| `BackgroundBlur` | `UDreamBackgroundBlur` | 模糊它下面的画面 |
| `BackgroundPixelate` | `UDreamBackgroundPixelate` | 像素化 |
| `PixelSort` | `UDreamPixelSort` | 像素排序（`SetMaxSortPasses` 限制在 1 到 512） |

它们共用一套读取、裁剪、写回屏幕的方式，全在渲染图的纹理上。自己写的效果也走这条路：在 `FDreamVisualPostProcessRenderProxy` 里用
`ReadScreen_RenderThread` 和 `GrabRegion_RenderThread` 读屏幕，在 `CreateWorkTexture` 建的纹理里干活，用 `WriteBack_RenderThread` 写回。

<a id="把结果抓回来postprocessrenderelement"></a>

### 把结果抓回来：`PostProcessRenderElement`

上面三种是**读屏幕**的视觉。另有两个反过来 —— 把**后处理算出来的结果图像抓回来**，当一块几何显示在它所在的位置：

| `.dui` 标签 | 类 |
| --- | --- |
| `PostProcessRenderElement` | `UDreamPostProcessRenderElement` |
| `PostProcessRenderElementText` | `UDreamPostProcessRenderElement_Text` |

⚠️ **一条必须知道的前提**（源码用 `NOTE!!!` 标出）：**只有目标后处理的 `RenderType` 是 `RenderTarget`、且 `bUseFullSize` 为 `false` 时才有效**。不满足时**它什么都不显示，而且不报错**。

`UDreamPostProcessRenderElement` 的 UV 约定是：`UV0` 是矩形 UV（(0,0) 到 (1,1)），`UV1` 见上面的[顶点格式](#顶点格式)。它通过四个静态矩阵参数（`DreamUI_World2PostProcess_Row1` … `Row4`）把「世界 → 后处理」的变换交给材质 —— 这就是结果图像对齐到这块几何上的方式。

⚠️ `PostProcessRenderElementText` **不是它的子类** —— 它在**独立文件**里、**继承 `UDreamText`**，是「宿主是一段文本视觉」的那个版本。注意它的标签名比类名**少一个下划线**。

<a id="顶点格式"></a>

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
材质里当 `TexCoord(4)` 读，**文本用它装渐变绘制**（一个被绘制的字形四边形在它那个渐变盒子里的位置），其它一切写 `(0, 0)`。
用构造函数 / `SetNum` / 整结构拷贝造出来的顶点自动是对的；**只有「先把顶点数组撑大、再逐字段填」的代码必须自己写 `UV4`**。

两个默认 UV 通道另有含义：

- **`UV0` 是纹理坐标**；
- **`UV1` 是控件属性数据的坐标** —— x 是数据纹理里的剪裁数据坐标，y 是纹理数组的切片下标，主要给字体渲染用。

⚠️ `UV1` 被画布占着（画布还会在同一次重建的后面把 x 盖回去），所以**一个把顶点位置写进 `UV1` 的网格修改器会丢一半、还破坏字形查找**。其余通道由各自的视觉定义（`DreamText`、`DreamRectBlock` 等）。

**文本四边形在 `UV2.x` 里带一个 paint 槽**：`UV2.x += 128 * slot`。不绘制的文本槽是 0，它的顶点就是 2.0 写的那套。
**要解码 `UV2.x` 的着色器必须先剥掉槽**（`DreamUIText.ush` 里的 `DreamUIText_UnpackPaintSlot`）；**要重写文本四边形 `UV2.x` 的网格修改器必须把它保住**。

另有**五个全局缓冲资源**：全屏 quad 的顶点缓冲、索引缓冲、切片 quad 的索引缓冲（**三个都是 `TGlobalResource`、全局各一份** —— 用 `static` 会给**每个包含它的 `.cpp` 各造一份**、各占一组 GPU 缓冲），加上网格顶点缓冲 `FDreamUIMeshVertexBuffer` 与顶点声明 `FDreamUIMeshVertexDeclaration`。

<a id="索引是-16-位的"></a>

### 索引是 16 位的

索引默认 **16 位**（`FDreamUIMeshIndex` = `uint16`）：半个索引的内存与带宽，代价是**一个 draw call 最多 65535 个顶点**（约 16k 个 quad）。真要单个元素超过，就得定义 `LEXUI_USE_32BIT_INDEXBUFFER`。

这条也是「顶点预算」的出处：顶点多到一个绘制调用的索引装不下时，网格修改器会**每个只报一次错、然后不画那个副本**。

<a id="检查"></a>

## 检查

报告"画错了"或"没画出来"时，这两个控制台变量很有用（测试套件运行时两个都开着）：

| 变量 | 检查什么 |
| --- | --- |
| `r.DreamUI.VerifyPartialPrepare 1` | 画布每次基于上一次做的部分准备，都和一次全量准备对照，不一致就触发一个说出画布名字的 ensure。代价是每次多一次全量准备 |
| `r.DreamUI.VerifyKeptPointers 1` | DreamGUI 保留下来、不每帧重新查找的对象 —— 控件的画布、画布的渲染层、UI 管理器的画布、动画属性绑定的对象 —— 每次使用时也查一遍，不一致就报错并 ensure，说出是哪一个 |

`DreamUI.Capture` 把视口和每个渲染到目标的根画布存成 PNG，`DreamUI.Stats` 打印每个阶段的开销，`DreamGUI.Memory` 列出字体图集、
精灵图集页和每个世界的画布网格段。完整的做法见[调试](../tools/debugging.md)。

自己写着色器或网格修改器的话，从上面的[顶点格式](#顶点格式)开始。
