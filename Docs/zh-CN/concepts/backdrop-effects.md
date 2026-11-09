---
title: 作用于背后画面的效果
description: 调整图层、渐进模糊、折射玻璃与风格化效果——把背后的像素取来，加工后按自身矩形、裁剪和遮罩所及画回；调整图层和风格化效果也可以改为作用于自己控件的子树。各自做什么、如何混合、开销与适用范围。
---

有四种视觉元素作用于已经画在它们背后的东西：拷贝自身矩形覆盖的像素（外加效果需要读到的外围），加工后把这个矩形画回目标——
只在裁剪、裁剪遮罩（含纹理遮罩）和自身遮罩纹理允许的范围内。范围外的像素原样不动。

| 视觉元素 | 作用 |
| --- | --- |
| `UDreamAdjustmentLayer` | 按顺序最多 8 个调色操作，结果与原画面按混合模式和不透明度混合 |
| 带 `StrengthMap` 的 `UDreamBackgroundBlur` | 强度随位置变化的模糊：上方磨砂、下方清晰，或中间清晰、四周磨砂 |
| `UDreamGlass` | 一块玻璃：边缘折射背后画面，可磨砂、着色、边缘高光 |
| `UDreamStylizeEffect` | 按顺序最多 8 层：CRT、故障、色差、网点、抖动、胶片颗粒、暗角、色调分离 |

四者都是后处理视觉元素（`UDreamVisualPostProcess`）：像普通视觉元素一样挂到控件上，矩形即控件矩形。由 DreamGUI 自己的渲染器绘制——
屏幕空间、渲染目标、世界空间（DreamUI Renderer）画布，以及设计器和关卡编辑器的预览。由引擎渲染器绘制的画布
（World Space - UE Renderer）不画它们。后处理元素的 `Tint` 与输出渲染目标不被这四种使用。

每个后处理元素都有 `bEnabled` 开关：关掉时什么都不画，被它盖住的地方如同它不存在——作用于子项的调整或风格化效果则让子项
照原样画出——所有设置都保留。

## 调整图层

操作列表（`Adjustments`，`FDreamAdjustment` 数组），每个操作作用于上一个的结果：

| 操作 | 参数 |
| --- | --- |
| `BrightnessContrast` | `Brightness` 平移各通道；`Contrast` 绕中灰转动曲线（-1 为平灰，1 为阶跃） |
| `Levels` | `InputBlack`、`InputWhite`、`Gamma`、`OutputBlack`、`OutputWhite` |
| `Curves` | `Curve`：颜色曲线资产的 R、G、B，每通道一条；`MasterCurve`（浮点曲线）先作用 |
| `Exposure` | `Exposure`（档）、`ExposureOffset`、`ExposureGamma`，在线性光下计算 |
| `HueSaturation` | `Hue`（度），`Saturation` 与 `Lightness`（-1 到 1）；`bColorize` 让所有颜色取给定色相与饱和度 |
| `Vibrance` | `Vibrance` 对饱和度越低的颜色提升越多；`VibranceSaturation` 均匀提升 |
| `ColorBalance` | `Shadows`、`Midtones`、`Highlights`（各分量 -1 到 1，偏向红、绿、蓝）；`bPreserveLuminosity` |
| `BlackAndWhite` | 每个原色及原色对的权重（`Reds`、`Yellows`、`Greens`、`Cyans`、`Blues`、`Magentas`），`TintColor` 以 `TintStrength` 着色 |
| `PhotoFilter` | `FilterColor` 以 `Density` 相乘；`bPreserveLuminosity` |
| `ChannelMixer` | 每个输出通道由输入混合（`RedMix`、`GreenMix`、`BlueMix`）加 `MixConstants`；`bMonochrome` |
| `Invert` | |
| `Posterize` | `PosterizeLevels` |
| `Threshold` | `ThresholdLevel`，按明度 |
| `GradientMap` | 按明度沿 `Gradient` 的色标取色（不使用其类型与角度）；`bReverse` |
| `LookupTable` | 引擎调色查找表格式（256×16）的纹理，强度 `Intensity` |

操作在编码（sRGB）颜色上进行，与混合模式一致。结果与原画面按 `EffectBlendMode`（全部 27 种，公式与元素相同）和 `Opacity`
（乘以视觉元素自身 alpha）混合。混合保留每个像素的 alpha：渲染目标里透明的地方调整后仍然透明，正如图像编辑器的调整图层不会添加像素。
背景不透明时这与 W3C 合成完全一致；背景半透明时，同模式的元素会在调整图层不加 alpha 的地方加上 alpha。

`Target` 决定作用对象：

- `Backdrop`（默认）：自身矩形背后的画面。
- `Children`：自身控件的子树。该控件按组绘制——子元素先画进单独的纹理——组的画面经过这些操作后再放下；背景不受影响。
  同一控件上的 `UDreamLayerGroup` 与它合成同一个组，组的不透明度、混合模式与遮罩在效果之后应用。

## 渐进模糊

`UDreamBackgroundBlur::StrengthMap`（`FDreamBlurStrengthMap`）在矩形每一点上乘以模糊的 `BlurStrength`：

| Kind | 强度 |
| --- | --- |
| `None` | 处处相同：均匀模糊，与以前完全一样 |
| `Texture` | 拉伸铺满矩形的纹理通道 |
| `Linear` | 从 `Start` 处的 `StartStrength` 到 `End` 处的 `EndStrength`（矩形左上角起的比例） |
| `Radial` | 距 `Start` 在 `InnerRadius` 内为 `StartStrength`，`OuterRadius` 外为 `EndStrength`（矩形高度的比例） |
| `Procedural` | 裁剪遮罩的程序化图案之一（`Pattern`、`Frequency`、`Seed`），从 `StartStrength` 到 `EndStrength` |

非 `None` 时在拷贝上建模糊金字塔（dual filter，最多 `MaxLevel` 级，每级宽一倍），每个像素按强度取级，并在相邻两级之间插值。
不支持全尺寸（full size）和输出渲染目标，这两种情况退回均匀模糊。

## 玻璃

`UDreamGlass` 是控件大小的圆角矩形，圆角同 RectBlock（`CornerRadius`，单位或较短边一半的比例），也可以是形状融合组融出的形状（见下）。点击落在形状之外不命中。

| 分组 | 属性 |
| --- | --- |
| 折射 | `EdgeWidth`（会折射的边缘带）、`Refraction`（最边缘的像素向内看多远，单位）、`Profile`（squircle、圆弧或线性升起）、`Dispersion`（红、蓝通道分别少折射与多折射的比例） |
| 磨砂 | `Frost`：0 清透，1 为模糊金字塔最强一级 |
| 色调 | `GlassTint` 以 `GlassTintStrength`、`Saturation`、`Brightness` |
| 高光 | 朝向 `LightAngle`（0 为上方，顺时针；背光一侧也有少许）的边缘上 `HighlightColor`、`HighlightStrength`、`HighlightWidth`；边缘内侧的 `ShadowWidth`、`ShadowStrength` |

比 `EdgeWidth` 更靠内的平坦中部原样显示背后画面（磨砂与色调除外）。视觉元素的 alpha 让它淡出。
在设计器里，它的 **Glass** 手柄能在视口里拖圆角、边缘宽度和光的方向（见[视口手柄](../tools/designer.md#viewport-handles)）。

`Shape` 设为 `ShapeBlend` 时，玻璃取形状融合组把 RectBlock 融成的形状（见[形状融合](layer-styles.md)）：
`ShapeBlendGroup` 指定的控件上的融合组，不指定时取玻璃自己控件上的。玻璃画满整个融合形状，成员伸到哪里就画到哪里，
它自己的矩形只是测量形状所用的坐标框；点击落在形状上即命中。成员仍按自己的颜色画出这个形状，本体透明的 RectBlock
只留下玻璃。移动成员，玻璃在下一帧跟上。融合组关闭或子项里没有 RectBlock 时，玻璃仍是自己的圆角矩形。

测量融合形状要遍历所有成员，每个像素三次（距离和它的斜率），所以成员多、面积大的玻璃比圆角矩形贵；普通玻璃不带形状的
2 KB 常量，只有它自己的着色器排列读取。成员移动导致四边形变大或变小时，画布在那一帧重建绘制调用。

## 风格化效果

`UDreamStylizeEffect::Layers`（`FDreamStylizeLayer`），每层是一个 pass，作用于前面各层的结果：

| 层 | 参数 |
| --- | --- |
| `CRT` | 扫描线（`ScanlineStrength`、`ScanlineSpacing`）、屏幕弧面 `Curvature`（边缘外为黑）、荧光栅格（`PhosphorStrength`、`PhosphorSize`）、闪烁（`FlickerStrength`、`FlickerFrequency`） |
| `Glitch` | `GlitchIntensity`；块位移（`BlockSize`、`BlockShift`）、行抖动（`LineJitter`、`LineDensity`）、红蓝分离（`ColorSplit`），每秒变化 `Rate` 次（0 为静止），`Seed`。`TriggerBurst(Duration, Intensity)` 触发一次爆发，在持续时间内衰减 |
| `ChromaticAberration` | `AberrationAmount`，从中心向外（`Radial`）或沿 `AberrationAngle`（`Directional`） |
| `Halftone` | 圆点或线条，单色墨印在 `PaperColor` 上或四色（`bCMYK`），`CellSize`、`ScreenAngle` |
| `Dither` | 4×4 或 8×8 Bayer `MatrixSize`、`Levels`、`PixelSize` |
| `FilmGrain` | `GrainIntensity`、`GrainSize`、`bAnimateGrain`（每秒换 24 次） |
| `Vignette` | `VignetteIntensity`、`VignetteRadius`、`VignetteSoftness`、`VignetteRoundness`、`VignetteColor` |
| `Posterize` | `Levels` |

尺寸以画布单位给出，随 UI 缩放保持大小。扫描线、网点、抖动和颗粒锚定在目标像素上，元素移动时图案不动；CRT 弧面、暗角和径向色差按元素矩形计算。
会动的效果随世界时间变化。`Opacity`、`EffectBlendMode` 与 `Target` 和调整图层相同。

像素排序仍是原来那个独立的视觉元素。

## 开销与适用范围

- 每个效果拷贝自己在目标上覆盖的矩形，外加效果需要读到的外围（模糊半径、玻璃折射、故障位移），并用一个 pass 画回。调整图层在拷贝上多一个 pass，
  风格化每层一个（不透明度不满或非普通模式时再加一个混合 pass），渐进模糊与磨砂玻璃加上半分辨率及以下的金字塔 pass。
- `Target = Children` 的开销与一个隔离的图层组相同，再加上效果在组纹理上的 pass。
- 多重采样目标上，拷贝是整张目标的解析结果。
- 后处理视觉元素位于读纹理的遮罩裁剪内时，也会被该纹理遮罩——均匀模糊、像素化、像素排序同样如此。
