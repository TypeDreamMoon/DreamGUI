---
title: "自定义网格、精灵与图集"
description: "视觉表说\"画成什么\"，这一页说\"那些几何与图像数据从哪来\"：自定义网格、精灵与两种图集、数据化纹理、静态网格缓存。"
---

[控件模型](widgets.md)那张视觉表回答的是控件挂上去之后**画成什么**。可是：

- `Sprite` 得有**一块精灵数据**才知道画图集里的哪一小块；
- `StaticMesh` 得有**一份烘好的网格数据**才画得出来；
- 有些几何**根本不是一个矩形** —— 圆柱面、沿曲线抬起来的平面、折线 —— 它们是**当场算出来的**。

**这一页就是讲这些「数据侧」的类。**

⚠️ 这几件事里**只有三件的外壳能写进 `.dui`** —— `Sprite` / `Texture` / `StaticMesh` 三个标签（视觉表已列）。其余都是「标签背后那个东西」，所以本页**大半在 C++ 与资产一侧，不在语言一侧**。

<a id="custom-meshes-geometry-outsourced-to-a-source"></a>

## 自定义网格：几何**外包**给一个「源」

`UDreamCustomMesh` 是 `UDreamVisualBatchMesh` 的子类，**本身不画矩形**：几何怎么填**外包**给一个「网格源」—— `CustomMesh` 属性（`Instanced`，可内联建）。类注释就一句话：**用网格生成器造自己的网格，而不是一个简单矩形**。

源是 `UDreamUICustomMeshSource`（抽象基类，`EditInlineNew`）。填几何的入口是它上面的 **`OnFillMesh`** —— 被画的 `UDreamCustomMesh` 在自己的 `OnUpdateGeometry` 里把几何对象交给源，再调用 `OnFillMesh`。

源的默认实现**什么都不做**，它是**给蓝图或子类去实现**的空壳：三个方法 `OnFillMesh` / `GetHitUV` / `SupportDrawcallBatching`，在「类是蓝图编译出来的」时候分别转发给蓝图事件 `ReceiveOnFillMesh` / `ReceiveGetHitUV` / `ReceiveSupportDrawcallBatching`。

填几何用的是 **`UDreamUIGeometryHelper`** 那套工具（`AddVertexSimple` / `AddVertexStruct` / `AddTriangle` / `SetMesh` / `AddVertexTriangleStream`）—— 它是 `UDreamUICustomMeshSource` 的**基类**。

⚠️ **源算几何时读的是控件的宽、高与轴心**（`Widget->GetWidth()` / `GetPivot()`），**不是它自己的矩形** —— 所以源造出来的图形**跟着控件矩形缩放**。

**两个内置源**：

| 源（显示名） | 关键旋钮 | 说明 |
| --- | --- | --- |
| `DreamCustomMeshSource Cylinder` | `CylinderArcAngle`（−180…180，默认 **45**） | 把矩形弯成一段圆柱面：**半径 = 宽 / 弧角**，段数在 4 到 32 之间按弧角插值 |
| `DreamCustomMeshSource CurvyPlane` | `Segment`（1…200，默认 **10**）、`ShapeCurve`、`CurveScale`（默认 **100**） | 沿宽度方向按一条曲线抬高 / 压低；曲线默认三键 `(0,0) → (0.5,1) → (1,0)` |

两个源都**只该在非批处理下用**（弯曲和圆柱是 3D 几何，合批会把它们和 2D 元素一起判错）。

<a id="why-it-deliberately-has-no-dui-tag"></a>

### 为什么它故意没有 `.dui` 标签

`UDreamCustomMesh` **没有**注册标签。源码在文件末尾专门写了原因：

> **它画的是「源递给它的东西」，而语言没有把源递给它的办法** —— 一个 `CustomMesh` 标签只会造出一个「**什么都不画、还不告诉你为什么**」的控件。

对照 `UDreamUMGWidget`：它**派生**自 `UDreamCustomMesh`，但**有**标签 —— 因为它托管的那个 widget 类是**一个属性**，而属性是 `.dui` 写得出来的东西。

⚠️ 这**不是「漏了」**，而是被一个自动化测试**双向守住**的取舍：`EveryVisualIsSpellableOrIsDeliberatelyNot` 遍历所有具体视觉子类 —— 没标签的**必须**出现在一张「刻意不注册」的名单里；名单里的类若哪天有了标签，**测试也会失败**。

<a id="the-mesh-that-holds-the-geometry"></a>

## 装几何的那块网格

**`UDreamUIMeshComponent`** 不是给用户挂的视觉，而是**画布用来装几何的那块网格**：它继承 `UMeshComponent`。它的渲染段分**四类**：`Mesh`、`DirectMesh`、`PostProcess`、`ChildCanvas` —— 正好对应三条绘制路径再加一个「子画布」。

**原地修补**（`TryPatchMeshSection`）与**认领池化段**（`ClaimPooledMeshSections`）都住在这。它也是 `DreamGUI.Memory` 报网格段字节数的来源。

**`UDream2DLineRendererBase`**（`DreamGUIExtensions`，抽象）是 2D 线条那一族的公共底座（`Line2DRaw` / `Line2DChildren`）。它**继承 `UDreamImage`** —— **线也是用刷子画的**，只是它们的顶点不是矩形。

| 属性 | 默认 | 说明 |
| --- | --- | --- |
| `LineWidth` | **10** | 线宽 |
| `EndType` | `Cap` | `None` / `Cap`（两头各加一个方块）/ `ConnectStartAndEnd`（把首尾连起来） |
| `LineWidthOffset` | **0.5** | 把线宽从左往右「推」过去，**做流动感** |

⭐ 一处写在注释里的取舍：它对「我想要多大」**弃权**，理由是 —— **刷子说明不了线有多长**，线是**由点算出来的**。

<a id="sprite-data"></a>

## 精灵数据

**精灵就是「大图集里的一小块图」。** `UDreamUISpriteData` 存这一块：`SpriteTexture`、`SpriteInfo`（在源图里占哪块、边界与内边距），以及打包设置。基类 `UDreamUISpriteData_BaseObject` 只定义接口。

`FDreamUISpriteInfo` 里值得记的：`Width` / `Height`、`Border` / `Padding`（各一个 `FMargin`），以及四个 UV 矩形（后两个是**九宫格**边框用的）。

⚠️ **颜色不在精灵上** —— 它在视觉上。

**两种打包**：

| `PackingType` | 什么时候打 | mipmap |
| --- | --- | --- |
| `Static` | 编辑器与 cook 时 | ✅ |
| **`Dynamic`**（默认） | 用的时候打 | ❌ |

⭐ **`PackingTag` 是关键开关**，默认 `"Main"`；同 tag 的精灵打进同一组图集。

⚠️ **把它设成 `None`，这个精灵就不被当作精灵**，而被当成一张普通的 `Texture`（视觉表的 `Texture` 标签）。

⚠️ **动态打包失败时**（比如图太大），源码会**自动把 `PackingTag` 清成 `None`**，让它退化成一张自己的纹理，**并报一条警告**。

<a id="two-kinds-of-atlas"></a>

## 两种图集

**静态图集**：`UDreamUIStaticSpriteAtlasData` **在编辑器里**以及 **cook 时**把多张精灵打进一张图集纹理（或一组**数组**）。所以**运行期没有打包开销**，也**支持 mipmap**；代价是**运行期不能再往里加东西**。`MaxTextureSize` 默认 **2048×2048** —— **超过就放弃打包**。它是 `NotBlueprintable` / `NotBlueprintType`，**只能当资产**。

**动态图集打包**：一张「动态图集」按 `PackingTag` 分组，**每组是若干页**（每页一张四方纹理），每页配一个 **MAXRECTS 装箱器**决定精灵摆哪（数据在 `FDreamUIDynamicSpriteAtlasData` 里）。管理它的是 **`UDreamUIDynamicSpriteAtlasManager`** —— 一个 **`UEngineSubsystem`**，用 `PackingTag` 当键。

**为什么做成引擎子系统**：头文件注释说它**曾经**是某个 static 里 rooted 的对象、谁都不 unroot —— **活得比每个世界都长，只有进程结束才拆**。改成引擎子系统，是因为引擎会负责创建它、拆掉它、并在 GC 时持有它。

<a id="packing-order-reuse-first--reclaim-next--add-a-page-last"></a>

### 打包顺序：先复用 → 再回收 → 最后才加页

1. 先试**已有的页**；
2. 放不下就**回收**没人画的精灵、腾出空间，再试一次；
3. 还是没有空间，**才加一张新页**。

第 2 步的注释写得很直白：**先回收、再加页** —— 否则「**一个长会话会一直往显存里爬，再也不下来**」。所以这套图集**会自己回收**，而且**不用你调任何 API 就会发生**。

<a id="how-memory-goes-back"></a>

### 怎么把内存还回去

**MAXRECTS 没有「还回一个矩形」的操作** —— 装箱器给出去的矩形**收不回来**。所以回收的**唯一**办法是：**拿还活着的精灵把页重建一遍**。

**谁算「还活着」不是猜的**：`RenderSpriteArray` 是当前所有**从这张图集取图、正在画**的元素 —— **没人点名的精灵，就是可以回收的**。

| 动作 | 做什么 |
| --- | --- |
| `TouchSprite` | 维护 **LRU 顺序**；**回收从最旧的开始挑** |
| `DropEmptyPages` | 只丢**尾部**空白页、**永远保留第一张** |
| `NotifyRenderSpritesAtlasChanged` | 重建之后**逐个通知**还在画的元素 —— 因为重建挪了它们的矩形，**UV 全过期了** |
| （重建时） | **不清像素** —— 被回收精灵留下的像素没人采样，下一张落进去就会覆盖；**清一遍是整页上传，太贵** |

对外主动释放有三档：`DisposeAtlasByPackingTag`（⚠️ 默认 tag `"Main"` **不许**释放）、`TrimAtlasByPackingTag` / `TrimUnusedAtlases`（只在整组都没人画时释放页、但**保留条目**）、`EvictUnusedSpritesByPackingTag`（只回收**没人画**的精灵）。

⚠️ **动态图集不会自己缩小**（除非走到「加页」那一步触发的回收）—— 要确定还内存得主动调 `Trim*`。

<a id="data-textures-letting-the-gpu-read-what-the-cpu-computed"></a>

## 数据化纹理：让 GPU 读到 CPU 算的东西

CPU 侧算了一堆**每个控件一行/一块**的数据（控件属性、裁剪数据），**着色器得能读到**。办法是把它**当纹理**喂给 GPU。

| 类 | 角色 |
| --- | --- |
| `UDreamUIDataAsTexture` | 一个 `UDataAsset`，把纹理**按块**发出去：`RegisterBuffer` / `UpdateBlock` / `UnregisterBuffer` |
| `UDreamUIDataTexture` | 底下那张**真纹理**，`UTexture` 子类，会**原地长大** |

⚠️ **别和[渲染](rendering.md)那页的「材质代理」混**：那里讲的是**谁回答参数**，这里讲的是**数据放在哪张纹理上**。

<a id="what-udreamuidatatexture-buys"></a>

### `UDreamUIDataTexture` 换来了什么

它顶替了 `UTexture2DDynamic`。差别在**原地长大**：

> 行用完了，渲染线程把它换成一张**更高的新纹理**、**把同一批行搬过去**，而**纹理的引用**（材质、材质实例、内置着色器绑的都是它）**在同一条命令里指向新的那张**。

→ 所以**采样它的东西不用被告知就跟着走了** —— 不用重设参数、不用重建绘制调用、不用刷 RHI。

而旧的 `UTexture2DDynamic` 是**换一个新对象**，于是**每个材质实例、每份渲染线程副本都得听通知** —— 没听到的就会拿着一张**被 GC 收走的纹理**去画。

块格式是 `EDreamUIDataAsTexturePixelFormat`（`R8` / `R16` / `R32` / `R8G8B8A8` / `R16G16B16A16` / `R32G32B32A32`）。它**默认不是逐块写、而是攒一批**：`PrepareForBatchUpdate` → 若干 `UpdateBlock` → 一次 `Flush` 全部送出去。

写入走 `Upload`，一批 `FDreamUIDataTextureUpdate` —— 每条是**一行上的一段像素**、或同样的段重复若干行。

⚠️ 它的大小**故意不放在属性里**：运行时在 transient 包建、永不保存 / 复制。尺寸走 `Initialize` / `Grow`（**不会缩**）。

<a id="the-static-mesh-cache"></a>

## 静态网格缓存

想在 UI 里画一个静态网格（`StaticMesh` 标签），**运行期读不到 `UStaticMesh` 的数据** —— 所以要先建一个 **`UDreamUIStaticMeshCacheData`**，**在编辑器里**把网格烘成 UI 能画的顶点与索引。

它缓存 `FDreamUIStaticMeshVertex` 数组（位置、两条切线、顶点色、UV0–UV3）、索引、材质、包围盒。`EnsureValidData()` 在 **cooked 构建里什么都不做**（数据早就烘好了）。

烘好的数据喂给 `UDreamStaticMesh`，三个旋钮：`MeshCache`、`VertexColorType`（默认 `NotAffectByUIColor`）、`ReplaceMaterial`。它的**首选尺寸从网格包围盒来**。

⚠️ 顶点多到一个绘制调用索引不下时，`HaveValidData()` 返回 `false`、**每个网格只报一次错**、**不再更新**。

<a id="sequence-players"></a>

## 序列播放器

精灵和纹理都可以被**逐帧播放**。三个类：

| 类 | 模块 | 需要什么视觉 |
| --- | --- | --- |
| `UDreamImageSequencePlayer`（**`Abstract`**） | `DreamGUI` | — |
| `UUISpriteSequencePlayer` | `DreamGUI` | `Sprite` 视觉 |
| `UUISpriteSheetTexturePlayer` | `DreamGUIExtensions` | `Texture` 视觉 |

基类的旋钮：

| 字段 | 默认 | 说明 |
| --- | --- | --- |
| `Fps` | **24** | ⚠️ `ClampMin = 0.0001` —— 源码注释写明「必须保持大于零」 |
| `bLoop` | **true** | |
| `bPlayOnStart` | **true** | |
| `bAffectByGamePause` / `bAffectByTimeDilation` | **false** / **false** | 和 `UDreamUIPlayTween` 同款默认 |
| `bPreviewInEditor` | **true** | 仅编辑器 |

内部用一个 `TWeakObjectPtr<UDreamTweener>` 推进 —— **所以它建在补间引擎上**（见[动画](animation.md)）。

各子类有各自的旋钮：`UUISpriteSequencePlayer` 有 `SpriteSequence`（精灵数据数组）与 `bSnapSpriteSize`（默认 **true** —— 要不要顺便把尺寸也设成精灵数据的尺寸）；`UUISpriteSheetTexturePlayer` 有 `WidthCount` / `HeightCount`（一张贴图里横向 / 纵向几格，各默认 **8**）。

<a id="when-it-does-not-serve"></a>

## 什么时候它不好用

- **`CustomMesh` 在 `.dui` 里找不到** —— 故意的，有测试守着。
- **自定义网格的源读的是控件宽高**，图形会随控件矩形缩放；要固定形状得自己控住控件尺寸。
- **圆柱 / 弯曲平面是 3D 几何**，别指望它们和 2D 元素共用绘制调用。
- **`PackingTag = ` `PackingTag = None` 的精灵不是精灵**；动态打包失败也会被**自动**清成 `None`。
- **动态图集不会自己缩小**；要确定还内存得主动调 `Trim*`。
- **`DisposeAtlasByPackingTag` 不许动默认 `"Main"`**，且调用前要保证屏幕上没有东西在画那些页。
- **静态图集运行期不能加精灵**；超 `MaxTextureSize` **直接放弃打包**。
- **静态网格缓存运行期读不到网格数据**，必须先在编辑器里烘。
- **`UDreamUIDataTexture` 只增不减**（`Grow` 不缩）。

<a id="not-yet-confirmed"></a>

## 还没证实的

- `UDreamUIDataTexture` / `UDreamUIDataAsTexture` 带的是 **2026 版权**（`TypeDreamMoon`），而精灵 / 图集
  那批是 2019（`LexLiu`）；`CHANGELOG` 没有点名它们。
- 内置保留的 `PackingTag` 只核到默认 `"Main"`；有没有别的保留 tag
  **未逐一核**。
- 动态图集回收的触发时机（除了「加页失败前」）**未逐条读实现**。
- 这块**一个参考页都没有**（搜 sprite / atlas / custommesh / mesh 全空），全部
  来自源码头文件。
