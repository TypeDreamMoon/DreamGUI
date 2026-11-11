---
title: 遮罩转场
description: 通过移动的遮罩显示或隐藏控件及其子树——擦除、时钟、圆形开合、菱形、百叶窗、棋盘、溶解、墨迹、碎裂或纹理给出的顺序——可由 Tween、属性轨道或蓝图驱动；交叉淡化与整屏转场；开销，以及它不会碰的东西。
---

`UDreamMaskTransition`（`Mask Transition`，`DreamGUIExtensions` 里的行为组件）透过一个随 `Progress` 从 0（隐藏）到 1（显示）而移动的遮罩来显示控件及其下的全部内容。它就是[裁剪遮罩](clip-masks.md)里的裁剪遮罩，只是替你驱动好了：一个程序化图案或一张纹理， `Progress` 移动它的阈值。

<a id="presets"></a>

## 预设

| 预设 | 显示方式 | 读取的设置 |
| --- | --- | --- |
| Wipe（擦除） | 一条直边从左往右扫过 | `Angle`（顺时针角度） |
| Clock（时钟） | 一根指针从十二点方向顺时针扫过 | `Centre`、`Angle` |
| Iris（圆形开合） | 一个圆从 `Centre` 张开（在任何控件上都是正圆） | `Centre` |
| Diamond（菱形） | 一个菱形从 `Centre` 张开 | `Centre` |
| Blinds（百叶窗） | `Count` 条百叶，每条从自己的左边打开 | `Count`、`Angle` |
| Checker（棋盘） | 横向 `Count` 格的棋盘：一种颜色的格子先擦入，然后是另一种 | `Count` |
| Dissolve（溶解） | 遍布各处的斑点，每个约 `Grain` 像素宽 | `Grain`、`Seed` |
| Ink（墨迹） | 一团墨从 `Centre` 晕开，边缘参差 | `Centre`、`Seed` |
| Shatter（碎裂） | `Count` 块碎片一块块出现，靠近 `Centre` 的先出现 | `Centre`、`Count`、`Seed` |
| Texture（纹理） | 纹理某个通道给出的顺序：暗的先、亮的后，拉伸铺满矩形 | `Texture`、`Channel` |

所有预设都有：`Softness`（移动边缘的宽度，占整个扫过范围的比例，0 为硬边）、`bReverse`（反向：擦除从右边开始、时钟逆时针、圆形从边缘向中心收拢）、`bInvert`（反相：随 Progress 增大而按同样的形状隐藏——显示时会先显示的部分先消失，两者逐像素加起来正好是全部）。

Ink、Shatter 和 Checker 读取的纹理由这个组件自己用代码生成——噪声，以及抖动网格的 Voronoi 碎片——只在控件形状、`Centre`、`Seed` 或 `Count` 改变时重新生成。不拷贝任何外部素材。

<a id="driving-it"></a>

## 驱动方式

- `Progress` 是可做动画的属性：可以在控件动画里打关键帧、用 Tween 驱动，或在蓝图里设置。
- `bEnabled` 关掉时不论进度如何控件都完整显示，方便对比有无转场。
- `PlayForward`、`PlayBackward`、`PlayFromTo` 用 Tween 驱动它并返回 Tween，便于链式调用（`OnComplete`）；`Stop` 让它停在当前位置。
- `CrossFade(From, To, Preset, Duration)` 让 `From` 通过同一种形状让位给 `To`：`To` 恰好在 `From` 隐藏的地方显示。没有转场组件的会自动添加；消失的一方沿用出现一方的设置，保证两者形状一致。
- `PlayScreenTransition(Preset, Cover, Hold, Uncover, OnCovered, OnFinished, Colour)` 用一种颜色按预设盖住整个屏幕，调用 `OnCovered` （加载下一个关卡、换界面），停留一会儿，再按同样的形状揭开并调用 `OnFinished`。遮盖层位于所有页面之上（排序 900），存在期间会挡住指针。

<a id="what-it-touches"></a>

## 它会碰什么

它不碰控件的 `Clip Mask`：它在运行期间**驱动**裁剪（`UDreamWidget::SetDrivenClipMask`），这部分既不保存进资产，也不显示在细节面板，也不写回 `.dui`。驱动期间，原本不裁剪到自身边界的控件会裁剪——Inherit 按 Clip To Bounds、Disabled 按 Clip To Bounds Without Intersecting——这样遮罩才有一层裁剪可切；其间控件矩形之外的内容会被裁掉。

- **完全显示时，它放手**：没有遮罩，没有它自己的裁剪层，零开销。
- **完全隐藏时，它下面的东西都点不到。**
- 在两者之间，Wipe、Clock、Iris、Diamond、Blinds 和 Dissolve 的点击按遮罩判定（`bHitTestMask`）；读纹理的预设在显示完之前按整个矩形命中。

<a id="what-it-costs"></a>

## 开销

一层带遮罩的裁剪：就是裁剪遮罩的开销（每层多 4 个 texel，见[裁剪遮罩](clip-masks.md)）；Ink、Shatter、Checker 和 Texture 还占用一条裁剪链仅有的两个遮罩纹理槽之一——位于不同遮罩纹理下的内容在不同的绘制调用里画。生成的纹理是 256×256（墨迹）、512×512（碎裂）和 64×64（棋盘）的半精度浮点纹理。

<a id="limits"></a>

## 限制

- 交叉淡化的两半共用一段软边时，在两者都只盖住一半的像素上，后面的东西会透出一点：两者各自叠在已有内容上，两个一半并不等于一个整体。硬边（`Softness` 0）没有这个问题；把交叉淡化放进一个有不透明底的隔离[图层组](blend-modes-and-groups.md)里也没有。
- 转场在没有完全显示时，会把控件的子树裁剪到控件矩形内。
- 你自己的材质只有在通过 `MF_DreamUI_Shade` 进行着色（并将其 alpha 用作不透明度）时，才会随遮罩移动；否则，当子树其余部分被显示出来时，它会保持完整。参见 [裁剪遮罩](clip-masks.md#what-it-costs)。
