---
title: 控件模型
description: UDreamWidget 是什么：一个带矩形、锚点和轴心的 UObject，挂着视觉、布局、槽和行为，组成一棵树；渲染变换、透明度与可见性，以及它和 UMG 在尺寸上的根本差别。
---

DreamGUI 里的一个控件是一个 `UDreamWidget`：一个 `UObject`，有矩形、轴心和锚点，排成一棵树，由
[画布](canvas.md)把整棵树合批成尽量少的 draw call 画出来。这是 Unity uGUI 的形状，不是 Slate 的。
它不是 Actor，也不是 ActorComponent；没有 `SWidget` 在背后，所以 UMG 的控件、样式和绑定都不适用。

一个控件本身什么也不画、什么也不排。它画什么、怎么排孩子、在父级里被放在哪、有什么逻辑，分别由挂在它身上的几个子对象决定
—— 用 `ConstructWidget` 的注释里的话说："panel" 在这里是一个子对象，不是一个单独的类。

<a id="一个控件由什么组成"></a>

## 一个控件由什么组成

| 部分 | 属性 / 入口 | 决定什么 |
| --- | --- | --- |
| 矩形 | `AnchorData`（`FDreamUIAnchorData`） | 相对父级在哪、多大 |
| 视觉 | `Visual`（`UDreamVisual`），每个控件至多一个 | 画什么 |
| 布局容器 | `LayoutContainer`（`UDreamLayoutContainer`） | 怎么排自己的孩子 |
| 自身布局 | `LayoutSelf`（`UDreamLayoutSelf`） | 自己的尺寸规则，比如宽高比、Spacer |
| 面板槽 | `PanelSlot`（`UDreamPanelSlot`） | 父级的面板怎么放它 |
| 行为 | 组件数组：`AddComponent`、`GetComponent`、`GetComponents` | 逻辑：按钮、画布、导航作用域…… |
| 孩子 | `GetChildren`、`AddChild`、`SetParent` | 树 |

在 `.dui` 里这几样一眼就能分开：节点类型是视觉（`Text`、`Image`）或布局容器（`VerticalBox`），
`+ Class { }` 挂行为或容器，`@slot` 写的是父级面板对它的安排。

```dui
Widget Root {
    // 四个角都锚在父级的四个角上、不内缩：铺满父级，任何分辨率都一样
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)

    Widget Card {
        // 锚在父级正中的一个点上：SizeDelta 就是它的尺寸，AnchoredPosition 是轴心离锚点的偏移（y 朝上）
        AnchorData.AnchorMin = (0.5, 0.5)
        AnchorData.AnchorMax = (0.5, 0.5)
        AnchorData.SizeDelta = (420, 220)
        AnchorData.AnchoredPosition = (0, 40)

        // 视觉：铺满 Card 的底板
        Image Backdrop {
            Brush.TintColor = #1B1E26FF
            AnchorData.AnchorMin = (0, 0)
            AnchorData.AnchorMax = (1, 1)
            AnchorData.SizeDelta = (0, 0)
        }

        // 行为：一个挂了按钮行为的普通控件
        Widget Confirm {
            AnchorData.SizeDelta = (160, 44)
            + UIButton { TransitionType = None }
            Text Label { Text = "确定" }
        }
    }
}
```

<a id="矩形锚点轴心和尺寸"></a>

## 矩形：锚点、轴心和尺寸

`FDreamUIAnchorData` 有五个字段，默认值是一个锚在父级中心、100 × 100 的方块：

| 字段 | 默认 | 含义 |
| --- | --- | --- |
| `AnchorMin` / `AnchorMax` | `(0.5, 0.5)` | 父级矩形里的两个归一化点。某个轴上两者相等，控件在这个轴上钉在一个点；不等就在这个轴上拉伸 |
| `SizeDelta` | `(100, 100)` | 钉在点上时是尺寸；拉伸时是**超出锚点跨度**的那部分，`0` 正好贴合，负值是内缩 |
| `AnchoredPosition` | `(0, 0)` | 轴心相对锚点的偏移 |
| `Pivot` | `(0.5, 0.5)` | 控件自己矩形里的归一化点，`(0, 0)` 是左下、`(1, 1)` 是右上 |

坐标是 y 朝上的：`Pivot`、`RenderTransformPivot`、`PerspectiveOrigin` 都以左下为原点，这和 UMG 的左上原点不同，
是有意的 —— 插件内部一致比和另一个引擎一致更重要。

运行时用对应的 setter 改：`SetAnchorMin`、`SetAnchorMax`、`SetAnchoredPosition`、`SetSizeDelta`、`SetPivot`，
以及按结果说话的 `SetWidth`、`SetHeight`、`SetAnchorOffset`（拉伸时四边的距离）。每个都有渐变版本：
`AnchoredPositionTo`、`SizeDeltaTo`、`WidthTo`、`HeightTo`，返回一个 `UDreamTweener`（见[动画](animation.md)）。

`RelativeLocation` 也能设，但它的 setter 会反算锚点并让父级重新布局；`.dui` 只写 `AnchorData`，
免得同一个位置被写两遍。

<a id="盒子优先和-umg-在尺寸上的差别"></a>

## 盒子优先：和 UMG 在尺寸上的差别

这是结构上的差别，不是外观上的，值得在选型之前就知道：

| | UMG / Slate | DreamGUI |
| --- | --- | --- |
| 控件 | `SWidget`，保留模式的 Slate | 组件式树里的一个 `UObject` |
| 尺寸 | 内容决定：控件的尺寸**就是**它的期望尺寸 | 盒子优先：你写一个矩形，内容在里面排 |
| 文本 | 盒子随文本长大 | 文本在盒子里对齐，可能溢出 |
| 定位 | 相对槽 | 锚点 + 轴心，与分辨率无关 |
| 复用 | Widget Blueprint 子类化 | Widget Blueprint 子类化，外加放内容的命名槽 |
| 放进世界 | `WidgetComponent`，一个渲染出来的四边形 | 一种一等的渲染模式 |

最让人意外的是文本那一行：UMG 的 `TextBlock` 不会溢出，因为它的盒子是从文本推出来的。这里盒子是写出来的，
所以文本可以溢出，也就有了 UMG 不需要的那些控制 —— `Margin`、`LineHeightPercentage`、`WrapTextAt` 和 Best Fit，
见[文本](text.md)。

盒子优先不等于没有"内容尺寸"。一个控件放进面板以后，面板会问它想要多大（`GetDesiredSize`），再把排好的矩形写回它的
`AnchorData`。视觉回答这个问题时，负数表示"没意见"，于是退回到写出来的矩形；`0` 是一个明确的回答。
完整的规则在[布局](layout.md)。

<a id="视觉控件画什么"></a>

## 视觉：控件画什么

视觉（`UDreamVisual`）是控件的子对象，每个控件至多一个。没有视觉的控件就是一个矩形 —— `.dui` 里的 `Widget`，
纯容器就是这样。视觉共有的属性是 `Color`（sRGB，可在 Sequencer 里打关键帧）、`bRaycastTarget` 和 `RaycastType`
（`Rect`、`Mesh`、`VisiblePixel`、`Custom`）。

| `.dui` 标签 | 类 | 模块 | 画什么 |
| --- | --- | --- | --- |
| `Text` | `UDreamText` | `DreamGUI` | 文本，见[文本](text.md) |
| `Image` | `UDreamImage` | `DreamGUI` | 图片 brush |
| `RectBlock` | `UDreamRectBlock` | `DreamGUI` | 程序化的矩形块：圆角、主体、边框、内外阴影，主体和边框可带渐变 |
| `Sprite` / `Texture` | `UDreamSprite` / `UDreamTexture` | `DreamGUI` | 图集精灵 / 整张纹理 |
| `Empty` | `UDreamVisualEmpty` | `DreamGUI` | 不画任何东西，但能被射线命中 |
| `Ring` / `Polygon` / `PolygonLine` | `UDreamRing` / `UDreamPolygon` / `UDreamPolygonLine` | `DreamGUIExtensions` | 环、多边形、多边形描边 |
| `Line2DRaw` / `Line2DChildren` | `UDream2DLineRaw` / `UDream2DLineChildrenAsPoints` | `DreamGUIExtensions` | 2D 线：给定点 / 以孩子为点 |
| `StaticMesh` | `UDreamStaticMesh` | `DreamGUIExtensions` | 静态网格，自成一个 draw call |
| `BackgroundBlur` / `BackgroundPixelate` / `PixelSort` | `UDreamBackgroundBlur` / `UDreamBackgroundPixelate` / `UDreamPixelSort` | `DreamGUIExtensions` | 屏幕效果，见[渲染](rendering.md) |
| `CanvasRenderTargetPreviewer` | `UDreamCanvasRenderTargetPreviewer` | `DreamGUIExtensions` | 显示一个渲染目标画布的内容 |
| `UMGWidget` | `UDreamUMGWidget` | `DreamGUIExtensions` | 把一个 UMG widget 渲染到渲染目标，再当作 DreamGUI 自己的几何画出来，见 [UMG 互操作](../controls/index.md#umg-互操作) |
| `PostProcessRenderElement` | `UDreamPostProcessRenderElement` | `DreamGUIExtensions` | 把某个后处理视觉的结果图像抓回来，当一块几何显示在这里 |
| `PostProcessRenderElementText` | `UDreamPostProcessRenderElement_Text` | `DreamGUIExtensions` | 同上，但宿主是一段文本视觉 |

视觉分三类（`EDreamVisualType`）：`BatchMesh` 由画布合批，绝大多数视觉都是；`DirectMesh` 直接写自己的网格段，
每个都是一个 draw call，用于顶点量大、变化频繁的内容（静态网格）；`PostProcess` 是读写屏幕的效果。
插件之外也可以加视觉：用 `DECLARE_DREAM_GUI_VISUAL` 注册一个 `.dui` 标签。

程序化形状（环、多边形、多边形描边）没有"想要多大"的答案 —— 它们的顶点就是从给定的矩形推出来的，
所以它们在测量里弃权，跟着写出来的矩形走。

<a id="行为挂在控件上的逻辑"></a>

## 行为：挂在控件上的逻辑

行为是 `UDreamUIBehaviour` 的子类，按顺序挂在控件的组件数组里。它们有一套 Unity 式的生命周期：
`Awake`、`OnEnable`、`Start`、`Tick`、`OnDisable`、`OnDestroy`，以及 `OnDimensionsChanged`、`OnAttachmentChanged`、
`OnInteractableChanged` 这些回调；Blueprint 子类里同名的事件（`Awake`、`Tick`……）对应同样的时机。
`bStartWithTickEnabled` 和 `bTickEvenWhenPaused` 控制它的 Tick。

`Awake` 的顺序是**层级高的先跑** —— 挂在根控件上的脚本最先执行。

六个时机之外还有**七个变更回调**：`OnTransformChanged`、`OnDimensionsChanged`、`OnChildDimensionsChanged`、
`OnAttachmentChanged`、`OnSiblingIndexChanged`、`OnInteractableChanged`、`OnRaycastableChanged`。
`OnDimensionsChanged` 会**逐项**告诉你改了哪个（`PivotChanged` / `WidthChanged` / `HeightChanged`）；
`OnChildDimensionsChanged` 是**独立的一个** —— 父级要跟着**孩子**长大时用它，而不是用自己那个。
有些回调想先于 `Awake` 跑，它们先被缓存起来（`CallbacksBeforeAwake`），等 `Awake` 时才执行。

Blueprint 侧是 **13 个 `Receive*` 事件**（6 个时机 + 7 个回调），全部是 `BlueprintImplementableEvent`，
而且**在蓝图里显示出来的名字不带 `Receive`** —— `ReceiveAwake` 就叫 `Awake`。

<a id="声明我不用"></a>

### 声明「我不用」

`bStartWithTickEnabled` / `bTickEvenWhenPaused` 是**运行时**开关；另有两个是**在构造函数里声明**的：

| 声明 | 之后 |
| --- | --- |
| `DeclareTickUnused(InClass)` | 恰好是这个类的行为（以及由它派生的蓝图）**不进 UI 管理器的 tick 列表**，而不是每帧被白叫一次 |
| `DeclareTransformChangedUnused(InClass)` | **不听控件的移动通知** —— 连带效应是「没人听它移动的控件」也不会被通知移动 |

两条限制要记住：它**只覆盖「恰好是 `InClass`」这一个类**（C++ 子类可以重写 `Tick`，而**没有任何东西能替它声明** —— 子类照旧 tick，直到它自己的构造函数也声明一次）；它**不改变「可不可以 tick」**，那是 `SetCanExecuteTick` 管的，而且蓝图写了 `Tick` 事件照旧算数。

<a id="两条子对象基类链"></a>

### 两条子对象基类链

挂在控件上的子对象有**两条**，重量不同。上面说的是重的那条；轻的是 `UDreamWidgetSubObjectBehaviour`
（`BlueprintType`、`Abstract`、`DefaultToInstanced`、`EditInlineNew`，**没有** `ClassGroup`、**没有** `Blueprintable`），
只提供 `OnRegister` / `OnUnregister`、`BeginPlay` / `EndPlay`、`IsRegistered`、`GetWidget`、`GetPathDisplayName` ——
**面板槽**走这条。会每帧跑、或者要听控件变化的，走重的；纯粹是「挂在控件上的数据与逻辑」，走轻的。

插件里大部分东西都是行为：画布本身（`UDreamCanvas`）、控件库里 `UI*` 那一族（`UIButton` 等，`Dream*` 控件由它们搭成）、
导航作用域、命名槽（`UDreamNamedSlot`）、拖放的源和目标、网格修改器（阴影、描边、渐变色、文字动画）。
代码里用 `AddComponent`、`GetComponent`、`GetComponents`、`GetComponentByInterface`、`RemoveComponent`、
`MoveComponentToIndex`；`.dui` 里写 `+ UIButton { … }`。

<a id="渲染变换只动画法不动布局"></a>

## 渲染变换：只动画法，不动布局

渲染变换移动的是控件**画在哪**，布局一无所知：布局照常测量和排布它，兄弟不会挪，任何东西都不会写回 `AnchorData`。
这就是它和 `RelativeLocation` 的全部区别 —— 后者的 setter 会反算锚点、请父级重新布局，所以在面板里给它做动画永远不行，
动画和布局只会轮流改它。

| 属性 | 类型 | 说明 |
| --- | --- | --- |
| `RenderTranslation` | `FVector` | 本地空间：X 是深度，**朝远离观察者**的方向，所以负 X 把控件拉向屏幕 |
| `RenderRotation` | `FRotator` | 绕 `RenderTransformPivot` 旋转，单位度；`SetRenderTransformAngle` 只写其中贴着画布平面的那个分量 |
| `RenderScale` | `FVector` | 绕 `RenderTransformPivot` 缩放 |
| `RenderTransformPivot` | `FVector2D` | 控件自己矩形里的归一化点，`(0, 0)` 左下；和布局用的 `AnchorData.Pivot` 互不相干 |
| `RenderShear` | `FVector2D` | 斜切，单位度，作用于控件和整棵子树 |

它是三维的，这是有意为之：被它镜像的那套变换本来就是 `FVector` / `FQuat` / `FVector`，世界空间的画布也存在，
一张卡片绕竖轴翻过来是很平常的需求。代价是批处理那条老规矩：在画布平面里滚转、缩放、平移仍然合批；
一旦有了深度、偏航或俯仰，这个控件就离开 2D 批处理路径，翻一张卡片多花一个 draw call，和你直接写那个旋转一样。

`RenderShear` 有两点不同：布局看不到它，**命中测试也不跟随它** —— 斜切的控件按没斜切的矩形接收点击（只有在透视作用域里，
射线才走矩阵路径）。它也不能打关键帧，另外三个通道可以。`ClearRenderTransform` 一次性把它们全部放回原样，
`HasRenderTransform` 回答"现在画法是否和布局不同"。

渲染变换不停变化的控件会被它的画布做成**渲染层**，在 GPU 上移动，见[渲染](rendering.md)。
在面板里做动画请用 `RenderOffsetTo`、`RenderScaleTo`、`RenderAngleTo`、`RenderTranslationTo`，见[动画](animation.md)。

<a id="每个控件自己的透视"></a>

### 每个控件自己的透视

`bPerspective` 让一个控件为它的**后代**建立透视，形状和 CSS 的 `perspective` 一样：有深度的孩子朝站在这个控件平面前方的
眼睛缩短。`PerspectiveFieldOfView`（默认 60 度）决定眼睛离多远，`PerspectiveOrigin` 是消失点（CSS 的 `perspective-origin`）。
嵌套的透视是嵌套，不是覆盖。关着时不花任何代价，子树里没有深度时也不花 —— 平的控件原样返回。
它需要一个使用透视投影的屏幕空间画布，否则不起作用，详见[画布](canvas.md)。

<a id="可见透明可交互"></a>

## 可见、透明、可交互

几个开关各管一件事，不要混用：

| 开关 | 管什么 |
| --- | --- |
| `Visibility`（`EDreamWidgetVisibility`） | 与 UMG 一一对应：`Visible`、`Hidden`（占位但不画）、`Collapsed`（不占位）、`HitTestInvisible`、`SelfHitTestInvisible` |
| `Shown` | `Visibility` 的是 / 否形式：`true` 是 `Visible`，`false` 是 `Collapsed`。自己不存任何东西，`.dui` 里的 `Shown <- HasSave()` 绑的就是它 |
| `bWidgetActive` | 行为生命周期的开关：不激活就不可见、不占布局、不可交互、不可命中，它的行为收到 `OnDisable` |
| `bIsEnabled` / `SetIsEnabled` | UMG 的启用：整棵子树不再接收输入，控件显示禁用外观；`GetIsEnabledInHierarchy` 是层级合并后的答案 |
| `Interactable`、`Raycastable` | 三态（`Inherit`、`Enabled`、`Disabled`）的可交互 / 可被射线命中 |
| `RenderOpacity` | 透明度，沿树向下相乘；`GetFinalRenderOpacity` 是乘完的值 |
| `SetContentTint` | 给**后代**的视觉染色（不含自己），像 UMG 的 content colour |

`Shown` 不会改掉"隐藏"或"不可命中"：一个 `Hidden` 的控件被设成 shown 以后还是 `Hidden`。
剪裁是 `Clipping`（`Inherit`、`ClipToBounds`、`ClipToBoundsWithoutIntersecting`、`Disabled`），另有 `ClippingCornerRadius`
和 `ClippingMargin`。`OnVisibilityChanged`、`OnFocusReceived`、`OnFocusLost` 是控件上可绑定的三个事件。

<a id="在代码里建控件"></a>

## 在代码里建控件

`UDreamUIBPLibrary::ConstructWidget(WorldContext, DisplayName, VisualClass)` 是 UMG 的
`WidgetTree::ConstructWidget<T>()` 的对应物：建出来的控件存在但不在屏幕上，不画任何东西、行为不运行，
直到交给 `AddChild` 或 `AddToViewport`；在此之前 DreamUI 的管理器持有它，不会被回收。`VisualClass` 留空就是一个纯容器，
之后给它一个布局容器就成了面板。

| 动作 | 结果 |
| --- | --- |
| `AddChild(Child, SiblingIndex)` | 挂上去，返回孩子的 `UDreamPanelSlot`（所有面板只有这一种槽，不必转型） |
| `RemoveChild` / `RemoveFromParent` | 摘下来但留着：仍注册、子树完整，回到刚建出来的状态 —— 对象池用的就是它 |
| `DestroyChild` / `DestroyWidget` | 拆掉整棵子树，之后 `IsValid` 为假 |
| `FindChildByDisplayName` | 按显示名找孩子，支持路径，如 `"Content/ListItem/NameLabel"` |

一个 Widget Blueprint 的实例在蓝图里用 **Create Dream Widget** 节点建，C++ 里用 `UDreamUIBPLibrary::CreateDreamWidgetOfClass` 或 `AddWidgetOfClassToViewport` 建，
见 [Widget Blueprint](widget-blueprints.md)。每个属性和函数的完整列表在
[`UDreamWidget` 参考](../Reference/DreamWidget.md)，末尾有与 UMG `UWidget` 的逐项对照。
