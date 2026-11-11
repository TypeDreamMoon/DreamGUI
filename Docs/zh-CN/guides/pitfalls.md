---
title: 常见陷阱
description: 那些看起来像 bug 其实并不是、或者能白白耗掉一下午的问题——同一属性上的补间、角度约定、射线检测目标、颜色字面量、样式覆盖、字体、世界空间控件、组内的效果、像素化强度、离屏录制，以及 UMG 命名不同之处——每项都附有讲解它的页面。
---

每条都说明你看到的现象、原因以及该怎么做；它链接到的页面有详细说明。

<a id="animation"></a>

## 动画

- **先淡入，再反向淡出，结果闪烁或结束得不对。** 同一属性上的两个补间过去会同时运行。现在补间启动时会停止同一属性上更早的补间；`SetReplacesEarlierTweens(false)` 可以让某个补间不受此限（例如一个应与悬停共存的循环呼吸效果）。序列中的补间永远不会被停止。[每个属性一个补间](../concepts/animation.md#one-tween-per-property)。
- **面板中的控件不会停留在 `AnchoredPositionTo` 设置的位置。** 面板会把位置写回去。请移动绘制内容：`RenderOffsetTo`、`RenderScaleTo`、`RenderRotationTo`、`RenderPoseTo`。[面板内的控件](../concepts/animation.md#widgets-inside-panels-animate-the-render-transform)。
- **正的 roll 会让控件顺时针旋转。** `RenderRotation.Roll`、`SetRenderTransformAngle` 和 `RenderAngleTo` 会让控件在屏幕上顺时针旋转（其右边缘向下），与 UMG 的角度一致。

<a id="angles"></a>

## 角度

- **各角度属性的零点并不相同。** `Ring` 从三点钟方向起算，逆时针；`Native.RingMenu`、`Native.ProgressBar` 以及路径上的文本从十二点钟方向起算，顺时针。`Ring` 上的进度环是 `StartAngle = 90`、`EndAngle = 90 - 360 * progress`。[角度约定](../concepts/widgets.md#visuals-what-a-widget-draws)。

<a id="input"></a>

## 输入

- **按钮上的标签不接收悬停或点击。** 没错：`UDreamText` 默认不是射线检测目标。如果文本本身就是要按的东西，请设置 `bRaycastTarget = true`。[视觉内容](../concepts/widgets.md#visuals-what-a-widget-draws)。
- **在 `Widget` 节点上使用 `bRaycastTarget` 是错误的。** 它属于视觉内容。请在控件上写 `Raycastable = Disabled`。[属性与值](../language/nodes-and-values.md#properties-and-values)。
- **手柄选中的按钮什么都不显示。** 可选中控件在拥有焦点时会穿上其 Hovered 外观作为过渡，除非 *Use Focused Visuals* 为焦点提供了单独的外观。由你自己的代码从 `OnSelectionStateChanged` 绘制的外观收到的是 `Focused`，而不是 `Hovered`：在那里请把两者画得一样。
- **由代码驱动的导航不产生移动。** 在同一帧内给出的按下和松开永远不会经历方向按下的那一帧。请调用 `TapNavigation`，它会在下一帧走一步；对于逐帧录制的录像，请打开事件系统的 *Navigation Repeat In Game Time*，否则按住的方向会按真实时间重复并跑在前面。
- **在控件的 construct 中请求焦点。** 该帧才显示的控件要到下一帧才获得焦点；在此期间 `SetKeyboardFocus` 返回 false，所以不要把该返回值当作最终结果。

<a id="the-dui-language"></a>

## `.dui` 语言

- **`Style.X = v` 不起任何作用。** 只有当 `StyleSource` 为 `Inline`（或没有样式表）时，才会使用实例的 `Style`，或者在使用 `ProjectStyleSheetOverride` 且勾选了该字段的 `bOverride_X` 时。`.dui` 从不改动这些位，而它们初始就是勾选状态。[在 .dui 中与运行时](../controls/styles.md#in-dui-and-at-run-time)。
- **`Text Note : A : B` 是语法错误。** 一个节点只能使用一个样式，一个样式只能继承一个样式。[样式](../language/styles-and-resources.md#styles)。
- **`#8C8C94` 比看起来更暗。** 颜色字面量采用 sRGB 编码，而着色是在线性空间中相乘的。[值](../language/nodes-and-values.md#values)。

<a id="text-and-fonts"></a>

## 文本与字体

- **本应显示中文、日文或韩文的地方出现方框，或者什么都不显示。** 字体没有该字符的字形，DreamGUI 会记录一次警告来指出它。默认字体通过 `/DreamGUI/DefaultFont_DistanceField_CJK` 覆盖这些字符；自己的字体需要提供回退字体。[字体](../concepts/text.md#fonts)。
- **控件的文本字体不对。** 带有 `Font` 字段的样式：文本输入框、数值微调框、富文本、可展开区域、环形菜单。[样式](../controls/styles.md)。

<a id="layout-and-world-space"></a>

## 布局与世界空间

- **展开或增长的列表绘制到面板之外。** 面板不会裁剪。请使用 `ScrollBox`，或 `Clipping = ClipToBounds`。[面板](../concepts/layout.md#the-panels)。
- **世界空间面板偏离了它的 actor。** 组件的 `Pivot` 和尺寸应设置在用户控件上；给该类的根节点设置从 `(0, 0)` 到 `(1, 1)` 的锚点，并且不要给它自身的尺寸。[世界空间 UI](../concepts/world-space.md)。

<a id="rendering"></a>

## 渲染

- **位于淡出或遮罩容器内的模糊会变成一片平灰。** 组（`GroupOpacity`、`GroupMask`……）会单独绘制，其中的背景效果只能看到该组。请把模糊放到外面，并用 `StrengthMap` 塑造它。[在图层组内](../concepts/backdrop-effects.md#inside-a-layer-group)。
- **我自己的材质忽略了裁剪或遮罩过渡。** 请通过 `MF_DreamUI_Shade` 着色。[裁剪遮罩](../concepts/clip-masks.md)。
- **像素化淡出先是很快，然后变得很慢。** 强度并不是块大小。[像素化强度](../concepts/backdrop-effects.md#pixelate-strength)。

<a id="code"></a>

## 代码

- **用 `NativeOnDestruct`，而不是 `NativeDestruct`。** 用户控件的 C++ 虚函数有自己的名字，而且 `GetWorld()` 在 `NativeOnInitialized` 中可能为 null；请使用 `NativeOnConstruct`。[实例的生命周期](../concepts/widget-blueprints.md#an-instances-life-cycle)。

<a id="tools"></a>

## Tools

- **录制的比例不对，或者两次录制互相干扰。** 请带上分辨率传入 `-ForceRes`，并一次只运行一个捕获。[离屏录制 UI](../tools/debugging.md#recording-the-ui-off-screen)。
- **从脚本编译 `.dui` 类。** 使用 `DreamUICompileWidgets` commandlet。[从命令行编译](../tools/scripting.md#compiling-from-the-command-line)。
