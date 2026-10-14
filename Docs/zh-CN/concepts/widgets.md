---
title: "控件模型"
description: "UDreamWidget 是什么 —— 一个携带矩形、锚点和轴心的 UObject，挂着视觉、布局、槽和行为，排列成一棵树 —— 外加渲染变换、不透明度与可见性，以及它与 UMG 在尺寸上的差异。"
---

DreamGUI 中的控件是一个 `UDreamWidget`：一个带矩形、轴心和锚点的 `UObject`，排列成一棵树，由 [画布](canvas.md) 绘制，后者会把整棵树尽可能合批成最少的绘制调用。这是 Unity 的
uGUI 形态，而非 Slate 的形态。它不是 Actor，也不是 Actor 组件；其背后没有 `SWidget`，因此 UMG 的任何控件、样式或绑定都不适用。

控件本身不绘制任何东西，也不排列任何东西。它绘制什么、如何排列子级、父级把它放在哪里、它运行什么逻辑，都由它携带的子对象决定 —— 用 `ConstructWidget` 的注释里的话说，"panel" 在这里是一个子对象，而不是一个单独的类。

<a id="what-a-widget-is-made-of"></a>

## 控件由什么组成

| 部分 | 属性 / 入口 | 决定什么 |
| --- | --- | --- |
| 矩形 | `AnchorData`（`FDreamUIAnchorData`） | 相对父级的位置和大小 |
| 视觉 | `Visual`（`UDreamVisual`），每个控件至多一个 | 绘制什么 |
| 布局容器 | `LayoutContainer`（`UDreamLayoutContainer`） | 如何排列子级 |
| 自身布局 | `LayoutSelf`（`UDreamLayoutSelf`） | 自身的尺寸规则：宽高比、间隔器 |
| 面板槽 | `PanelSlot`（`UDreamPanelSlot`） | 父级的面板如何放置它 |
| 行为 | 组件数组：`AddComponent`、`GetComponent`、`GetComponents` | 逻辑：按钮、画布、导航作用域…… |
| 子级 | `GetChildren`、`AddChild`、`SetParent` | 树 |

在 `.dui` 中，各部分很容易区分：节点类型是视觉（`Text`、`Image`）或布局容器
（`VerticalBox`），`+ Class { }` 挂载行为或容器，`@slot` 行则是父级面板对该节点的处理方式。

```dui
Widget Root {
    // All four anchors on the parent's corners, no inset: fills the parent at any resolution
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)

    Widget Card {
        // Anchored to one point, the parent's centre: SizeDelta is its size, AnchoredPosition the pivot's offset from the anchor (y up)
        AnchorData.AnchorMin = (0.5, 0.5)
        AnchorData.AnchorMax = (0.5, 0.5)
        AnchorData.SizeDelta = (420, 220)
        AnchorData.AnchoredPosition = (0, 40)

        // A visual: a backdrop that fills the card
        Image Backdrop {
            Brush.TintColor = #1B1E26FF
            AnchorData.AnchorMin = (0, 0)
            AnchorData.AnchorMax = (1, 1)
            AnchorData.SizeDelta = (0, 0)
        }

        // A behaviour: a plain widget carrying the button behaviour
        Widget Confirm {
            AnchorData.SizeDelta = (160, 44)
            + UIButton { TransitionType = None }
            Text Label { Text = "OK" }
        }
    }
}
```

<a id="the-rect-anchors-pivot-size"></a>

## 矩形：锚点、轴心和尺寸

`FDreamUIAnchorData` 有五个字段，默认值是一个锚定在父级中心的 100 × 100 方块：

| 字段 | 默认值 | 含义 |
| --- | --- | --- |
| `AnchorMin` / `AnchorMax` | `(0.5, 0.5)` | 父级矩形中的两个归一化点。在某个轴上相等时，控件钉在该轴上的一个点；不等时则沿该轴拉伸 |
| `SizeDelta` | `(100, 100)` | 钉在某个轴上时为尺寸；在拉伸轴上则为**超出锚点跨度**的尺寸 —— `0` 正好贴合，负值表示内缩 |
| `AnchoredPosition` | `(0, 0)` | 轴心相对锚点的偏移 |
| `Pivot` | `(0.5, 0.5)` | 控件自身矩形中的归一化点，`(0, 0)` 为左下，`(1, 1)` 为右上 |

坐标 y 轴向上：`Pivot`、`RenderTransformPivot` 和 `PerspectiveOrigin` 都把原点放在左下角，与 UMG 的左上角原点不同 —— 这是有意为之，因为插件内部的一致比与其他引擎的一致更重要。

运行时每个字段都有对应的 setter：`SetAnchorMin`、`SetAnchorMax`、`SetAnchoredPosition`、`SetSizeDelta`、
`SetPivot`，以及面向结果的 `SetWidth`、`SetHeight` 和 `SetAnchorOffset`（拉伸控件的四条边距离）。每个也有对应的补间 —— `AnchoredPositionTo`、`SizeDeltaTo`、`WidthTo`、`HeightTo` —— 返回一个 `UDreamTweener`
（见[动画](animation.md)）。

`RelativeLocation` 也可以设置，但它的 setter 会重新计算锚点并请求父级重新布局；而 `.dui` 只写 `AnchorData`，因此同一位置永远不会被写两次。

<a id="box-first-the-sizing-difference-from-umg"></a>

## 盒子优先：与 UMG 在尺寸上的差异

这一差异是结构性的而非外观性的，在决定采用哪一种之前值得了解：

| | UMG / Slate | DreamGUI |
| --- | --- | --- |
| 控件 | `SWidget`，保留模式的 Slate | 组件式树中的一个 `UObject` |
| 尺寸 | 内容决定：控件的尺寸**就是**其期望尺寸 | 盒子优先：你编写一个矩形，内容在其中排列 |
| 文本 | 盒子随文本增长 | 文本在盒子中对齐，可能溢出盒子 |
| 定位 | 相对槽位 | 锚点 + 轴心，与分辨率无关 |
| 复用 | Widget Blueprint 子类化 | Widget Blueprint 子类化，外加用于内容的命名槽位 |
| 世界中 | `WidgetComponent`，一个渲染的四边形 | 一种一等的渲染模式 |

文本那一行最让人意外。UMG 的 `TextBlock` 不会溢出，因为它的盒子由文本推导而来。而这里盒子是编写的，因此文本可以溢出盒子，也就有了 UMG 不需要的控件 —— `Margin`、
`LineHeightPercentage`、`WrapTextAt` 和 Best Fit；见[文本](text.md)。

盒子优先并不意味着没有内容尺寸。一旦控件位于面板中，面板会询问它想要多大
（`GetDesiredSize`），并把排列后的矩形写回它的 `AnchorData`。视觉以负数回答该问题时表示弃权，此时使用编写的矩形；`0` 是一个真实的回答。完整规则见
[布局](layout.md)。

<a id="visuals-what-a-widget-draws"></a>

## 视觉：控件绘制什么

视觉（`UDreamVisual`）是其控件的子对象，每个控件至多一个。没有视觉的控件就是一个矩形 —— `Widget`
中的 `.dui` —— 这正是纯容器的形态。每个视觉都有 `Color`（sRGB，可在 Sequencer 中打关键帧）、`bRaycastTarget`
和 `RaycastType`（`Rect`、`Mesh`、`VisiblePixel`、`Custom`）。

| `.dui` 标签 | 类 | 模块 | 绘制 |
| --- | --- | --- | --- |
| `Text` | `UDreamText` | `DreamGUI` | 文本；见[文本](text.md) |
| `Image` | `UDreamImage` | `DreamGUI` | 一个图像 brush |
| `RectBlock` | `UDreamRectBlock` | `DreamGUI` | 程序化块：圆角、主体、边框、内外阴影，主体和边框可选渐变 |
| `Sprite` / `Texture` | `UDreamSprite` / `UDreamTexture` | `DreamGUI` | 一个图集精灵 / 整张纹理 |
| `Empty` | `UDreamVisualEmpty` | `DreamGUI` | 不绘制任何东西，但接收射线检测 |
| `Ring` / `Polygon` / `PolygonLine` | `UDreamRing` / `UDreamPolygon` / `UDreamPolygonLine` | `DreamGUIExtensions` | 一个环、一个多边形、一个多边形描边 |
| `Line2DRaw` / `Line2DChildren` | `UDream2DLineRaw` / `UDream2DLineChildrenAsPoints` | `DreamGUIExtensions` | 2D 线：经过给定点 / 经过子级 |
| `StaticMesh` | `UDreamStaticMesh` | `DreamGUIExtensions` | 一个静态网格，自成一个绘制调用 |
| `BackgroundBlur` / `BackgroundPixelate` / `PixelSort` | `UDreamBackgroundBlur` / `UDreamBackgroundPixelate` / `UDreamPixelSort` | `DreamGUIExtensions` | 屏幕效果；见[渲染](rendering.md) |
| `CanvasRenderTargetPreviewer` | `UDreamCanvasRenderTargetPreviewer` | `DreamGUIExtensions` | 渲染目标画布所绘制的内容 |
| `UMGWidget` | `UDreamUMGWidget` | `DreamGUIExtensions` | 将一个 UMG 控件渲染到渲染目标，然后作为 DreamGUI 自身的几何绘制出来；见 [UMG 互操作](../controls/index.md#umg-interop) |
| `PostProcessRenderElement` | `UDreamPostProcessRenderElement` | `DreamGUIExtensions` | 抓取后处理视觉的结果图像并在此处作为一块几何显示 |
| `PostProcessRenderElementText` | `UDreamPostProcessRenderElement_Text` | `DreamGUIExtensions` | 同上，但以文本视觉作为宿主 |

视觉分三类（`EDreamVisualType`）：`BatchMesh` 由画布合批，几乎所有的视觉都是这一类；`DirectMesh` 自己写网格段，每一个都是一个 draw call，用于顶点量大、变化频繁的内容（静态网格）；以及 `PostProcess`，一种读写屏幕的效果。插件也可以添加自己的视觉，用 `DECLARE_DREAM_GUI_VISUAL` 为它注册一个 `.dui` 标签。

程序化形状——环、多边形、多边形描边——没有"想要多大"的答案：它们的顶点是从给定的矩形推出来的。它们在测量里弃权，跟着写出来的矩形走。

<a id="behaviours-logic-on-a-widget"></a>

## 行为：挂在控件上的逻辑

行为是 `UDreamUIBehaviour` 的子类，按顺序挂在控件的组件数组里。它们有一套 Unity 式的生命周期——`Awake`、`OnEnable`、`Start`、`Tick`、`OnDisable`、`OnDestroy`——再加上 `OnDimensionsChanged`、`OnAttachmentChanged`、`OnInteractableChanged` 这些回调；Blueprint 子类里同名的事件会在同样的时机触发。`bStartWithTickEnabled` 和 `bTickEvenWhenPaused` 控制它的 tick。

`Awake` 的顺序是**层级高的先跑**——挂在根控件上的脚本最先执行。

六个时机之外还有**七个变更回调**：`OnTransformChanged`、`OnDimensionsChanged`、`OnChildDimensionsChanged`、`OnAttachmentChanged`、`OnSiblingIndexChanged`、`OnInteractableChanged`、`OnRaycastableChanged`。`OnDimensionsChanged` 会**逐项**告诉你改了哪个（`PivotChanged` / `WidthChanged` / `HeightChanged`）；`OnChildDimensionsChanged` 是**独立的一个**——父级要跟着**孩子**长大时用它，而不是用自己那个。有些回调想先于 `Awake` 跑，它们先被缓存起来（`CallbacksBeforeAwake`），等 `Awake` 时才执行。

Blueprint 侧是 **13 个 `Receive*` 事件**（6 个时机 + 7 个回调），全部是 `BlueprintImplementableEvent`，而且**在蓝图里显示出来的名字不带 `Receive`**——`ReceiveAwake` 就叫 `Awake`。

<a id="declaring-i-dont-use-it"></a>

### 声明「我不用」

`bStartWithTickEnabled` / `bTickEvenWhenPaused` 是**运行时**开关；另有两个是**在构造函数里声明**的：

| 声明 | 之后 |
| --- | --- |
| `DeclareTickUnused(InClass)` | 恰好是这个类的行为（以及由它派生的 Blueprint）**不进 UI 管理器的 tick 列表**，而不是每帧被白叫一次 |
| `DeclareTransformChangedUnused(InClass)` | **不听控件的移动通知**——连带效应是「没人听它移动的控件」也不会被通知移动 |

两条限制要记住：它**只覆盖「恰好是 `InClass`」这一个类**（C++ 子类可以重写 `Tick`，而**没有任何东西能替它声明**——子类照旧 tick，直到它自己的构造函数也声明一次）；它**不改变「可不可以 tick」**，那是 `SetCanExecuteTick` 管的，而且 Blueprint 写了 `Tick` 事件照旧算数。

<a id="two-sub-object-base-chains"></a>

### 两条子对象基类链

挂在控件上的子对象有**两条**，重量不同。上面说的是重的那条；轻的是 `UDreamWidgetSubObjectBehaviour`（`BlueprintType`、`Abstract`、`DefaultToInstanced`、`EditInlineNew`，**没有** `ClassGroup`、**没有** `Blueprintable`），只提供 `OnRegister` / `OnUnregister`、`BeginPlay` / `EndPlay`、`IsRegistered`、`GetWidget` 和 `GetPathDisplayName`——**面板槽**走这条。会每帧跑、或者要听控件变化的，走重的；纯粹是「挂在控件上的数据与逻辑」，走轻的。

插件里大部分东西都是行为：画布本身（`UDreamCanvas`）、`Dream*` 控件由之搭成的 `UI*` 一族（`UIButton` 及其兄弟姐妹）、导航作用域、命名槽（`UDreamNamedSlot`）、拖放的源和目标、网格修改器（阴影、描边、渐变色、文字动画）。代码里用 `AddComponent`、`GetComponent`、`GetComponents`、`GetComponentByInterface`、`RemoveComponent` 和 `MoveComponentToIndex`；在 `.dui` 里写 `+ UIButton { … }`。

<a id="render-transform-drawing-moves-layout-does-not"></a>

## 渲染变换：只动画法，不动布局

渲染变换移动的是控件**画在哪**，布局一无所知。布局照常测量和排布它，兄弟不会挪，任何东西都不会写回 `AnchorData`。这就是它和 `RelativeLocation` 的全部区别——后者的 setter 会反算锚点、请父级重新布局，所以在面板里给它做动画永远不行：动画和布局只会轮流改它。

| 属性 | 类型 | 说明 |
| --- | --- | --- |
| `RenderTranslation` | `FVector` | 本地空间。X 是深度，**朝远离观察者**的方向，所以负 X 把控件拉向屏幕 |
| `RenderRotation` | `FRotator` | 绕 `RenderTransformPivot` 旋转，单位度；`SetRenderTransformAngle` 只写其中贴着画布平面的那个分量 |
| `RenderScale` | `FVector` | 绕 `RenderTransformPivot` 缩放 |
| `RenderTransformPivot` | `FVector2D` | 控件自己矩形里的归一化点，`(0, 0)` 左下；和属于布局的 `AnchorData.Pivot` 互不相干 |
| `RenderShear` | `FVector2D` | 斜切，单位度，作用于控件和整棵子树 |

它是三维的，这是有意为之：被它镜像的那套变换本来就是 `FVector` / `FQuat` / `FVector`，世界空间的画布也存在，一张卡片绕竖轴翻过来是很平常的需求。代价是批处理那条老规矩：在画布平面里滚转、缩放、平移仍然合批；一旦有了深度、偏航或俯仰，这个控件就离开 2D 批处理路径，翻一张卡片多花一个 draw call——和你直接写那个旋转一样。

`RenderShear` 有两点不同：布局看不到它，**命中测试也不跟随它**——斜切的控件按没斜切的矩形接收点击（只有在透视作用域里，射线才走矩阵路径）。它也不能打关键帧，另外三个通道可以。`ClearRenderTransform` 一次性把它们全部放回原样；`HasRenderTransform` 回答"现在画法是否和布局不同"。

渲染变换不停变化的控件会被它的画布做成**渲染层**，在 GPU 上移动，见[渲染](rendering.md)。在面板里给控件做动画请用 `RenderOffsetTo`、`RenderScaleTo`、`RenderAngleTo` 和 `RenderTranslationTo`，见[动画](animation.md)。

<a id="perspective-per-widget"></a>

### 每个控件自己的透视

`bPerspective` 让一个控件为它的**后代**建立透视，形状和 CSS 的 `perspective` 一样：有深度的孩子朝站在这个控件平面前方的眼睛缩短。`PerspectiveFieldOfView`（默认 60 度）决定眼睛离多远，`PerspectiveOrigin` 是消失点（CSS 的 `perspective-origin`）。嵌套的透视是嵌套，不是覆盖。关着时不花任何代价，子树里没有深度时也不花——平的控件原样返回。它需要一个使用透视投影的屏幕空间画布，否则不起作用，详见[画布](canvas.md)。

<a id="visible-transparent-interactive"></a>

## 可见、透明、可交互

几个开关各管一件事，不要混用：

| 开关 | 管什么 |
| --- | --- |
| `Visibility`（`EDreamWidgetVisibility`） | 与 UMG 一一对应：`Visible`、`Hidden`（占位但不画）、`Collapsed`（不占位）、`HitTestInvisible`、`SelfHitTestInvisible` |
| `Shown` | `Visibility` 的是 / 否形式：`true` 是 `Visible`，`false` 是 `Collapsed`。自己不存任何东西；它正是 `Shown <- HasSave()` 在 `.dui` 里绑定的东西 |
| `bWidgetActive` | 行为生命周期的开关：不激活就不可见、不占布局、不可交互、不可命中，它的行为收到 `OnDisable` |
| `bIsEnabled` / `SetIsEnabled` | UMG 的启用开关：整棵子树不再接收输入，控件显示禁用外观；`GetIsEnabledInHierarchy` 是层级合并后的答案 |
| `Interactable`、`Raycastable` | 三态（`Inherit`、`Enabled`、`Disabled`）的可交互 / 可被射线命中 |
| `RenderOpacity` | 透明度，沿树向下相乘；`GetFinalRenderOpacity` 是乘完的值 |
| `SetContentTint` | 给**后代**的视觉染色（不含自己），像 UMG 的 content colour |

`Shown` 永远不会改掉隐藏或不可命中：一个 `Hidden` 的控件被设成 shown 以后还是 `Hidden`。剪裁是
`Clipping`（`Inherit`、`ClipToBounds`、`ClipToBoundsWithoutIntersecting`、`Disabled`），另有 `ClippingCornerRadius` 和
`ClippingMargin`。`OnVisibilityChanged`、`OnFocusReceived` 和 `OnFocusLost` 是控件上可绑定的三个事件。

<a id="making-widgets-in-code"></a>

## 在代码里建控件

`UDreamUIBPLibrary::ConstructWidget(WorldContext, DisplayName, VisualClass)` 是 UMG 的
`WidgetTree::ConstructWidget<T>()` 的对应物：控件存在但不在屏幕上。它不画任何东西，行为也不会
运行，直到你把它交给 `AddChild` 或 `AddToViewport`；在此之前 DreamUI 的管理器持有它，所以你在配置它时它不会被回收。纯容器就把 `VisualClass` 留空，然后给它一个布局容器，就成
面板了。

| 调用 | 结果 |
| --- | --- |
| `AddChild(Child, SiblingIndex)` | 挂上去，返回孩子的 `UDreamPanelSlot`（所有面板只有这一种槽类，不必转型） |
| `RemoveChild` / `RemoveFromParent` | 摘下来但留着：仍注册、子树完整，回到还没加进去的状态；对象池用的就是它 |
| `DestroyChild` / `DestroyWidget` | 拆掉整棵子树；之后 `IsValid` 返回假 |
| `FindChildByDisplayName` | 按显示名找孩子，支持路径，如 `"Content/ListItem/NameLabel"` |

一个 Widget Blueprint 的实例在蓝图里用 **Create Dream Widget** 节点建，C++ 里用
`UDreamUIBPLibrary::CreateDreamWidgetOfClass` 或 `AddWidgetOfClassToViewport` 建；见
[Widget Blueprint](widget-blueprints.md)。每个属性和函数都列在
[`UDreamWidget` 参考](../Reference/DreamWidget.md)里，末尾有与 UMG `UWidget` 的逐项对照。
