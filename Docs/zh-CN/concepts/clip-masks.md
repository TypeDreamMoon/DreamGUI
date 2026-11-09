---
title: "裁剪遮罩与剪贴到下方图层"
description: "裁剪控件的裁剪遮罩——形状、羽化边、纹理或程序化图案，可反相——以及只在下方图层画出处才显示的控件；各自的开销、合批方式与点击检测。"
---

裁剪控件（`Clipping` 为 `ClipToBounds` 或 `ClipToBoundsWithoutIntersecting`）把整棵子树裁到自己的矩形内，圆角由
`ClippingCornerRadius` 决定。它的**裁剪遮罩**（`ClipMask`，类型 `FDreamClipMask`）改变这个裁剪放行哪些像素。打开
**剪贴到下方图层**（`bClipToBelow`）的控件，连同其下所有内容，只在绘制顺序中前一个兄弟画出的地方显示。两者都由同一段着色器
代码逐像素求值，无论 UI 由谁绘制——内置着色器、UI 材质、世界空间、渲染层、透视作用域——也都在 CPU 上用同样的公式做点击检测。

## 裁剪遮罩

| 属性 | 作用 |
| --- | --- |
| `Shape` | `Rect`（裁剪矩形本身，带控件的圆角）、`Ellipse`、`Polygon`（`Sides` 边数，`Roundness` 圆角像素）、`Star`（`Sides` 个角，`InnerRatio` 内径比）、`Ring`（`InnerRatio` 是中间的洞）——都贴合裁剪矩形，一个角或尖朝正上方 |
| `Feather` | 遮罩淡入的像素宽度。`Rect` 每条边各用自己的宽度，从边向内量；其它形状用 `Left` 作为整条轮廓的羽化，以轮廓为中心 |
| `Source` | `None`（只有形状）、`Texture` 或 `Procedural` |
| `Texture`、`Channel`、`Fill` | 遮罩纹理、读取的通道（`Alpha`、`Red`、`Green`、`Blue`、`Luminance`），以及铺法：`Stretch`、`Fit`（完整放入、保持比例、外面为空）、`Fill`（铺满、保持比例）、`Tile`（按自身像素大小平铺） |
| `Pattern`、`Frequency`、`Seed` | 程序化图案：`Linear`、`Radial`、`Angular`（时钟擦除）、`Diamond` 在裁剪范围内从 0 走到 1；`Blinds`、`Checker`、`Noise` 在范围内重复 `Frequency` 次 |
| `Rotation`、`Scale`、`Offset` | 旋转（绕裁剪中心顺时针的角度）、缩放、平移纹理或图案。`Rotation` 就是线性渐变的角度 |
| `Threshold`、`Softness` | 来源值 `v` 变为 `saturate((v - Threshold + Softness) / (2 * Softness))`。取 `0.5` 和 `0.5` 时就是 `v` 本身；`Softness` 很小时在 `Threshold` 处一刀切——驱动 `Threshold` 即可做擦除、溶解 |
| `bReverse` | 让纹理或图案的值反向（过阈值前 `v` 变为 `1 - v`）：从右到左的渐变、从边缘向内的径向渐变、逆时针的时钟擦除。与 `bInvert` 不同，它不动形状，也不动裁剪矩形之外的部分 |
| `bInvert` | 显示遮罩藏起的、藏起遮罩显示的——**裁剪矩形之外也一样**，聚光灯就靠这个：被反相椭圆裁剪的遮挡层盖住椭圆以外的一切 |
| `HitTest` | `Bounds`（默认：按裁剪矩形做点击检测，与没有遮罩时相同）或 `Mask`（遮罩值至少一半处才命中） |

默认值的遮罩等于没有遮罩：裁剪就是原来的矩形，开销与原来完全相同。细节面板里遮罩位于 **外观 ▸ Clipping**；设计器会描出所选控件
的遮罩轮廓（视口工具栏的 **Clip Masks** 开关），它的 **Mask** 手柄能在视口里拖裁剪矩形的四条边、矩形遮罩的圆角、多边形的圆角、
星形或圆环的内径，以及羽化（见[视口手柄](../tools/designer.md#viewport-handles)）。

行为组件可以在运行期间代替 `ClipMask` 驱动控件的裁剪遮罩（`UDreamWidget::SetDrivenClipMask`）：[遮罩转场](mask-transitions.md)就是这样
做的。被驱动的遮罩不保存、不显示，也不写进 `ClipMask`；驱动期间，原本不裁剪到自身边界的控件会裁剪（Inherit 按 Clip To Bounds、
Disabled 按 Clip To Bounds Without Intersecting）。`GetEffectiveClipMask` 返回裁剪实际绘制和点击检测所用的遮罩。

### 纹理遮罩与绘制调用

一个绘制调用绑定两张遮罩纹理，每个**槽**一张。一条裁剪链上的纹理遮罩从外到内占槽：最外层的纹理遮罩读槽 0，下一个读槽 1，同一条链上
第三个会被忽略（其形状照样裁剪）并给出警告。处在不同遮罩纹理下的内容画在不同的绘制调用里；处在同一张遮罩纹理下的内容，以及不在任何纹理
遮罩下的内容，照旧合批。

`HitTest = Mask` 时，能读的话会在 CPU 上读纹理——像素在 CPU 上、未压缩的 `Texture2D`（`UserInterface2D (RGBA)` 或
`VectorDisplacementmap`）——否则按白色处理并给出警告。渲染目标和动态图集读不了。

颜色通道按纹理被采样后的值读取：把值存在红、绿、蓝通道里的遮罩，请关掉 sRGB。

## 剪贴到下方图层

`bClipToBelow` 就是图像编辑软件里的剪贴蒙版：

- **下方图层**是绘制顺序中位于该控件之前、且自身没有勾选剪贴到下方图层的那个兄弟。连续勾选的兄弟共用同一个下方图层。
- 下方图层照常绘制。该控件及其子树只在下方图层画出的地方显示，并透过它的不透明度（它的 `Color` alpha 和它自己的
  `RenderOpacity`）。下方图层隐藏时它们也隐藏；前面没有兄弟时，该控件不被剪贴。
- 凡是裁剪下方图层的——它父级的裁剪、它自己的遮罩——也裁剪该控件。使用 `ClipToBoundsWithoutIntersecting` 或 `Disabled` 时，
  外层的裁剪被排除，下方图层不会。

下方图层可以是简单或九宫格的图片、纹理或精灵（不填充中心的九宫格中间为空），也可以是 RectBlock（其主体的圆角矩形）：裁剪直接跟随
它的形状。其它任何东西——文字、平铺或填充的图片、自定义网格、被网格修改器改变形状的元素、容器或图层组——在游戏中的屏幕空间画布和
渲染目标画布上经由一个组来剪贴（见[混合模式与图层组](blend-modes-and-groups.md)）；在世界空间画布上不会被剪贴，并给出指名该控件
的警告。

点击检测按下方图层画出的形状：它的四边形（不读取其纹理的 alpha），或 RectBlock 的圆角主体。

## 开销

没有遮罩的裁剪层仍是画布裁剪数据里的五个 texel；有遮罩或剪贴到下方图层的层多四个，只由它裁剪的像素读取。没有任何遮罩的裁剪链上传的
字节数与以前完全相同。材质路径以 `DreamUI_ClipMaskTexture0` 和 `DreamUI_ClipMaskTexture1` 读取遮罩纹理，画布会在每个 UI 材质
（`MF_DreamUI_Shade`、`MF_DreamUI_Clip`）上设置它们；以原有参数调用 `DreamUI_CalculateClipData` 或 `DreamUI_ShadePixel` 的
自定义材质照样能编译，其纹理遮罩把每个 texel 当作白色。被遮罩裁剪的背景效果（`UDreamBackgroundBlur` 等后处理元素，以及
[作用于背后画面的效果](backdrop-effects.md)）会被遮罩的形状、羽化、图案和纹理裁剪。
