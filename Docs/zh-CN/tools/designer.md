---
title: 控件设计器
description: 对照 UMG 设计器重建的 DreamUI Designer：按矩形拾取、缩放手柄与锚点徽章、框选与拖放重挂、调色板、细节面板、组件、撤销、源文件菜单、事件路由、内容浏览器里的菜单和缩略图，以及对 .dui 支撑的类它能改什么、不能改什么。
---

打开一个 Dream Widget Blueprint，就进入 **DreamUI Designer**。它有两个模式：*Designer*（画布、调色板、层级、细节面板、动画）和 *Graph*（Blueprint 事件图）。设计器编辑的是这个类的一个**预览实例**，改动直接写回类本身，所以没有"应用"这一步，也没有每个实例各自的覆盖列表要对账。

**注意：**

  **设计器是被测试验证过的，不是被眼睛验证过的。** 它的手势有自动化测试（`Designer` 预设）覆盖，但没有人手动走过每一处角落。预计会有粗糙的地方，遇到了请报告。

<a id="gestures-on-the-canvas"></a>

## 画布上的手势

设计器是对照 UMG 的 widget 设计器审过一遍的。相对上游新增的有：

- **按矩形拾取**。点击按 widget 的 *rect* 命中，而不是按渲染出来的三角形。只做布局的面板没有网格，射线永远打不中，上游里面板因此点不中、也放不进东西——按矩形拾取解决的就是这个。
- **悬停反馈**：指针下的 widget 会被描出来。
- **按轴的缩放手柄**，以及**锚点徽章**（anchor medallion），在画布上显示选中 widget 的锚点。
- **选中内容自带的手柄**：变形晶格点、路径点、RectBlock 的渐变、裁剪遮罩、玻璃、阴影和光，同一时刻显示一种（见[视口手柄](#viewport-handles)）。一次拖动一步撤销，Esc 还原；以 `.dui` 为源的类在松开鼠标时把改动写进文件。
- **框选**：在空白处拖出一个框，选中框里的 widget。
- **在画布上拖动重挂**：把 widget 拖到另一个容器里。
- **从内容浏览器拖放**：把资产直接拖到设计画布上。
- **撤销**：每个设计器手势是一步撤销，创建、粘贴和拖放都在撤销覆盖之内。
- **吸附与网格**：工具栏的 *Snap* 开关和网格尺寸；拖动时有吸附参考线。
- **对齐与分布**：*Arrange* 菜单把选中的兄弟 widget 沿某条边或中心对齐（至少两个），或均分它们之间的间距（至少三个）。

层级面板的右键菜单有 *Wrap With...*（用一个新容器把所有选中项包起来，插入到它们的位置；widget 保持原来的布局）、*Unwrap*（把一个 widget 从父子之间拿掉，子节点按原顺序落在它的位置）、*Replace With...*（换掉排列子节点的面板——和 UMG 不同，widget 本身的名字、位置、槽、组件和子节点都不动，只换布局容器）、 *Show / Hide in Designer*、*Lock / Unlock*、*Find References*、*Focus Selection*（等同于 F 键），以及 *Reveal in VS Code*。

层级面板在控件名字后面用小图标标出它作为图层带了什么，每类一个：**样式**（图层样式）、**组**（图层组或形状融合组）、**遮罩**（裁剪上的遮罩、剪贴到下一层、遮罩转场）、非正常的**混合**模式、**效果**（玻璃、调整图层这类后处理视觉，全息箔、指针光、倾斜卡、网格修改器）、**变形**（变形、果冻、路径文字）和**粒子**。悬停在标记上会列出它代表什么、各自怎么设的。点一下，用效果自己的开关把这一类关掉，再点打开——方便对比前后；关掉时标记变暗。这是普通的编辑：一步撤销，会保存，由它支撑的类还会写回 `.dui`。双击样式标记会打开[图层样式对话框](#the-layer-style-dialog)，所以点这个标记时会先等过双击的时间再切换。

<a id="viewport-handles"></a>

### 视口手柄

单选一个 widget（未锁定、是资产自己的）时，视口会给它有的东西画手柄。一个 widget 可能有好几种手柄，同一时刻只显示一种，种类名写在选框下方，后面跟着按 **Tab** 会换成哪一种。Tab 在选中内容有的种类之间轮换，最后一种之后是「关」——只剩选框，比如手柄正好压在选框角上、想从那个角缩放时——再按回到第一种。Tab 只在视口拿着键盘焦点时归视口；在细节面板、层级或任何文本框里，它照常切换焦点。

选中一个控件时显示哪种：最近一次选过的种类（用 Tab，或细节面板里的 *Edit in Viewport* 按钮），只要选中内容有它；否则是它有的第一种：先是变形的晶格点和路径点，再是它的属性标签（`DreamGUI_Handle`，见[属性 meta 标签](../guides/property-meta.md#viewport-handles)）要的种类—— 视觉的、各个行为的、控件自己的——按声明顺序。用 Tab 切到「关」之后，选什么都不显示手柄，直到再按 Tab。

| 种类 | 显示什么，拖动改什么 |
|---|---|
| Warp Lattice、Path Points | 变形的晶格点；路径的点 |
| Body Gradient、Border Gradient | RectBlock 的渐变：端点、中心和色标（见[渐变](../concepts/gradients.md#视口手柄)） |
| Shadows | RectBlock 自带的外阴影和内阴影，画在阴影落下的位置：拖一个同时改它的角度和距离 |
| Glass | 玻璃的轮廓和折射边缘伸进来的那条线：圆角（`CornerRadius`，按它自己的单位模式）、`EdgeWidth`，以及高光的光从哪边来（`LightAngle`） |
| Style Shadows | 图层样式的每个阴影（投影是圆、内阴影是菱形），画在阴影落下的位置，同时改角度和距离。用全局光的阴影改的是全局光，所有用全局光的效果一起转 |
| Global Light | 图层样式有效果用全局光时，全局光画成一个圆盘——绕圆盘的方向是角度，离边缘往里的距离是高度 |
| Pointer Light | 指针光站的位置（在设计器里它停在 `LightPosition`）和它照到的圆 |
| Mask | 裁剪 widget 的裁剪区和上面的遮罩（见[裁剪遮罩](../concepts/clip-masks.md)）：遮罩切出的轮廓和羽化带；裁剪矩形的四条边（`ClippingMargin`，遮罩形状按它适配）、矩形遮罩的圆角（`ClippingCornerRadius`）、多边形的 `Roundness`、星形的内角或圆环的孔（`InnerRatio`），以及 `Feather` |

圆角和羽化默认四个一起动——裁剪区在 widget 设为统一圆角时也一样——按住 **Alt** 只动拖的那一个。**Shift** 让角度按 15° 一档；拖裁剪区的边时，Shift 让对边同时往外扩。拖动中按 **Esc** 全部还原。一次拖动是一步撤销，松开鼠标时写回 `.dui`。不归这个 widget 自己改的东西没有手柄：跟随预设的图层样式、由行为驱动的遮罩、形状来自形状融合的玻璃，以及关掉的效果。

<a id="the-palette"></a>

## 调色板

调色板列出可以放进来的东西，分组和 UMG 的调色板一样：**Basic**（Widget、Text、Image、RectBlock）、Panels、 Common、Input、Lists、Scrolling、Containers、Primitive、Shapes、Effects、Components、Modifiers、Advanced、 **User Created**（项目里的 widget Blueprint），最后是 **Legacy**。

- 每组里的条目按名字排序；面板去掉了 "UMG " 前缀。
- **Legacy** 放的是控件库取代掉的东西——LGUI 时代的行为、Blueprint 预设，以及 Tile View 和 Scroll Box 两个行为——留着是为了让建在它们上面的 widget 还能看懂。它总在最后，默认折叠；打开过一次，设计器会记住。
- User Created 不列插件自己的预设目录。
- 层级面板的 *Create* 菜单用的是同一套分组和顺序。
- **收藏**：常用的条目可以收藏，收藏组排在最前面。注册键没变，所以以前的收藏都还在。
- **搜索**：搜索框过滤条目，过滤时所有分组展开。

图层效果都在调色板里，而且拖进来就能看到东西——有几个效果在类默认值下什么都不画：

| 分组 | 条目 | 拖进来是什么 |
| --- | --- | --- |
| Effects | Glass | 带一点磨砂的玻璃，放在纯色上也看得出 |
| Effects | Adjustment Layer | 把背后变成黑白的调整图层 |
| Effects | Stylize | CRT 效果 |
| Components | Layer Style | 带投影和浅色描边的 RectBlock |
| Components | Layer Group | 透明度 60% 的独立组，里面两个重叠的块 |
| Components | Shape Blend Group | 融成一体的两个圆 |
| Components | Mask Transition | 擦除到一半的块 |
| Components | Holo Foil、Pointer Light、Tilt Card | 一个用来看效果的块 |
| Modifiers | Warp、Text On Path | 弯成弧形的文字、绕圆排列的文字 |
| Modifiers | Jelly | 一个块 |
| Shapes | Path | 一条曲线 |

它们设的都是普通的作者值。后处理蓝图在 Effects 里按去掉 `BP_` 和 `_C` 的名字列出。

项目自己注册的控件，分类在 DreamGUI 的分类之后、按注册顺序排列。分类名来自 `DreamUIPaletteCategory` （`DreamUIControlRegistry.h`）；以前拿 `FDreamUIControlDescriptor::Category` 去和 "Controls"、"Post Process"、"Extensions" 或某个旧分类名比较的扩展代码，要改用那里的名字。

<a id="the-details-panel"></a>

## 细节面板

选中一个 widget 时，分类按一个 widget 的构成排列：先是它作为图层的样子（Layer），再是它放在哪（Slot、Canvas、 Layout），然后是它是什么（Visual、Panel、Self Layout）、它怎么表现（Appearance、Behavior），最后是 Render Transform，以及默认折叠的 Perspective、Navigation、Accessibility 和 Localization。Is Enabled 在 Behavior 里。以前排在最下面、把 Layout 的行原样重复一遍的 Transform 和锚点数据分类已经去掉了。

属性放在哪，由它身上的 `DreamGUI_` 标签决定；没写标签的属性显示在它自己的分类下，所以 widget、视觉或组件能设的东西不会在面板里缺席；只对某些取值适用的行（填充、过渡的颜色）随取值出现。这些标签以及扩展的类怎么用它们，见[属性的 meta 标签](../guides/property-meta.md)。

**Layer** 分类把让 widget 成为图层的东西集中到一处，不管它们原本在哪个对象上：

| 行 | 编辑什么 |
| --- | --- |
| Opacity | widget 的 `RenderOpacity` |
| Blend Mode | 视觉的混合模式，视觉是元素（图片、文字、RectBlock……）时才有 |
| Clip To Layer Below | `bClipToBelow` |
| Mask | 快速选一个形状——选了就让 widget 裁剪到自身边界，遮罩需要这个——下面是完整的遮罩（`ClipMask`） |
| Layer Style、Layer Group | 各一个开关：打开时给没有的 widget 加一个，和 *Add Component* 一样；关掉时关闭它但保留设置。*Edit...*：图层样式的打开[图层样式对话框](#the-layer-style-dialog)，图层组的打开它自己的属性 |
| Group Mode | 图层组的隔离方式，所有选中 widget 都有图层组时显示 |
| Effects | widget 上的每个图层效果：一只眼睛负责开关，点名字打开它的属性 |

这些行都不会在别处重复出现：不透明度以前在 Appearance，遮罩和剪贴到下一层在 Appearance > Clipping，混合模式在视觉自己的行里。

<a id="editing-controls"></a>

### 编辑控件

- **混合模式**用图片显示：每种模式的图片是一个圆按该模式自己的公式混合在色相和灰阶上，菜单按图像编辑软件的顺序和分组列出全部 27 种（Normal 即 `Alpha`，Linear Dodge (Add) 即 `Additive`）。悬停在某种模式上，正在编辑的控件就临时按它绘制，指针移开或菜单关闭就恢复；点下去之前不记撤销、不保存。
- 标了 `DreamGUI_Editor = "AngleWheel"` 的**角度**在数字旁边多一个圆盘：拖动（按住 Shift 以 15 度吸附）或直接输入。冒号后的值说明角度怎么走：不写是图像编辑软件的约定（0 在三点钟方向，逆时针），`AngleWheel:CW`，或 `AngleWheel:TopCW`（时钟的约定）。图层样式里开着 *Use Global Light* 的效果，在自己的行里改的是样式的全局光角度，共用全局光的效果一起转；行上会标出 *Global*。一次拖动是一步撤销。图层样式的各个角度、渐变叠加的角度、玻璃的光照角度、遮罩转场的旋转和路径文字圆弧的起点都是圆盘。
- **等高线**用曲线显示：行里是曲线；弹出框里有各种命名形状的图片可选，下面是可编辑的曲线——点空白加点，把点拖出框外删除， Alt+点击或双击切换角点。编辑命名形状会从它开始生成一条自定义曲线。
- **缓动**（`EDreamTweenEase`）在行里和菜单里都显示曲线。

这些控件是给整个编辑器注册的，不只用在设计器的面板里：任何细节面板（数据资产的、对话框的、单独显示的结构体）里出现这类属性，都用同样的控件编辑。

视觉自己的行也按用途排：

- **Text** 以 Text、Font、Font Size、Font Style、Color、对齐、Overflow Type、Auto Wrap Text 和 Rich Text 开头；其余的——Wrapping、Typography（包括语言选择）、Text Style / Paint、Rendering（覆盖材质、预设材质、网格尺寸、小字栅格化等）——放在默认折叠的组里。
- **Image** 的刷子排在最前，射线检测的行排在最后。
- **Sprite** 和 **Texture** 的颜色在 sprite / 纹理的下面。

<a id="components"></a>

## 组件

*Add Component* 只列出选中 widget 支持的组件——以前它在每个 widget 上列出所有组件类，加上去之后大多数什么都不做：

| 组件 | 要求 |
| --- | --- |
| 网格修饰器 | 一个会建网格的视觉（`UDreamVisualBatchMesh`） |
| `UISpriteSequencePlayer` | Sprite 视觉 |
| `UISpriteSheetTexturePlayer` | Texture 视觉 |
| `UITextHyperlink`，文字动画 | Text 视觉 |
| `UDreamUMGWidgetInteraction` | UMG Widget 视觉 |
| `UDreamRetainerBox`、`UDreamInvalidationBox` | widget 上已经有画布（`UDreamCanvas`） |
| 画布、导航、动画组件、具名槽位、内容 widget、Retainer Box、Invalidation Box | 每个 widget 一个 |
| 可选择对象（`UUISelectable` 一族）、滚动视图（`UUIScrollView` 一族） | 每个 widget 只能是一种 |
| 内容 widget | 不提供给有多个子节点的 widget |

粘贴、复制，以及拖到面板上的组件资产，遵守同样的规则；被拒绝的拖放会说出原因。规则写在 `FDreamUIComponentSupport`（`DreamGUIEditor` 模块）里：为一个类注册的规则对它的每个子类都成立，包括 Blueprint 子类。项目在自己编辑器模块的 `StartupModule` 里，用 `RequireVisual`、 `RequireComponent`、`OnePerWidget` 或 `AddRule` 为自己的组件加规则。

<a id="layer-styles-colours-and-presets"></a>

## 图层样式、颜色与预设

<a id="the-layer-style-dialog"></a>

### 图层样式对话框

widget 菜单 Layer Style 一节最上面的 **Layer Style...**、细节面板 Layer 区里图层样式开关旁的 **Edit...**，以及在层级面板里双击 widget 的样式标记，都会在选中的 widget 上打开图像编辑器那样的图层样式对话框（从 Layer 区或标记打开时，那个 widget 不在选择里就只对它自己）。窗口不是模态的，开在设计器旁边，每个设计器同时只开一个。

- 左边是**混合选项**——图层的混合模式和不透明度、填充不透明度、所有尺寸的缩放、全局光——以及图层样式有的各种效果，顺序和图像编辑器一样：斜面和浮雕（下挂等高线、纹理）、描边、内阴影、内发光、光泽、颜色叠加、渐变叠加、图案叠加、外发光、投影。勾选框开关效果；勾上一种还没有的效果就加一个。
- 描边、内阴影、颜色叠加、渐变叠加和投影可以有多个：**+** 在这一行后面加一个（复制这一行），垃圾桶删掉它；把一行拖到同类的另一行上可以调整绘制顺序，后面的画在前面的上面。
- 右边是选中那一行的参数。它们就是样式自己的字段，所以项目或 DreamGUI 按字段类型或元数据注册的编辑控件，在这里和细节面板里一样会用上。
- 每次改动都立刻显示在 widget 上，只在设计器的预览里；取消勾选 *Preview* 就不显示。按 **OK** 之前什么都不写；OK 把样式写到每个 widget 上，一步撤销，并写进 `.dui`。**Cancel**（或直接关窗口）把预览恢复原样，文件逐字节不变。
- **New Style...** 把正在编辑的样式连同图层的混合存成预设，用的是[预设库](#the-preset-library)的新建样式窗口。
- 对话框开着时，在视口里拖 widget 的样式阴影或全局光，改的是对话框里的样式：立刻显示，松手算一次修改、OK 时写入，Esc 放回；视口自己不写任何东西。

多选时，对话框从第一个 widget 的样式和混合开始，写到所有 widget 上。什么都没改就按 OK，不写任何东西。混合模式和不透明度只有在混合选项里改过才写，否则多个 widget 各自保留自己的混合。原来跟随预设的样式会变成 widget 自己的样式。 `DreamUILayerStyleDialog::Open` 和 `OpenFrom`（`DreamGUIEditor` 模块）供项目自己的编辑器打开这个对话框，`FDreamUILayerStyleSession` 是不带窗口的编辑过程。

<a id="copying-and-pasting-a-layer-style"></a>

### 复制和粘贴图层样式

widget 菜单（层级面板的菜单，视口里右键打开的也是它）里有一节 **Layer Style**，就是图像编辑器里那一对：

- **Copy Layer Style** 把选中 widget 的[图层样式](../concepts/layer-styles.md)放到剪贴板上；*Copy Includes* 勾了的话，连同它的混合模式和不透明度、它的裁剪和遮罩一起（这个选择会记住）。
- **Paste Layer Style** 把它写到每个选中的 widget 上：写进各自的图层样式，没有的就加一个；剪贴板里带了混合模式和遮罩的，也写到元素的混合模式和 widget 的裁剪上。菜单项的文字会说剪贴板里带了哪几样。整次粘贴是一步撤销； `.dui` 驱动的 Blueprint 上会写进文件。

剪贴板里放的是 `.dui` 的几行，也就是一个节点块里能写的那些行，所以复制的样式能贴到另一个 Blueprint、另一次编辑器会话里，也能直接贴进文本编辑器打开的 `.dui`；手写的几行同样能粘贴：

```
// DreamGUI layer style
BlendMode = Multiply
RenderOpacity = 0.8
Clipping = ClipToBounds
ClipMask.Shape = Ellipse
+ DreamLayerStyle {
    Style.DropShadows = ["(Angle=120,Distance=6,Size=10,Opacity=0.6)"]
    Style.BevelEmboss.bEnabled = true
}
```

只写和默认值不同的值；粘贴会整块替换它带的那几样：样式贴到已有样式的 widget 上，旧样式的效果一个也不留。widget 的样式来自 `.dui` 的 style（`: Card`）时，不会去改那个 style，而是在 widget 自己的节点上写一个覆盖。

<a id="the-eyedropper-and-the-projects-palette"></a>

### 吸管和项目色板

细节面板里每一个颜色行——`FLinearColor` 和 `FColor` 都算，图层样式里的颜色也算——旁边有两个按钮：

- **吸管。** 点它，移到设计器视口上，再点一下取色。在视口上读的是视口自己的画面：画出来的那个像素，背后和自身的每个效果、图层样式、混合模式都已算进去，设计器盖在视口上的东西不算。在屏幕别处读的是屏幕上的像素。Esc 或右键放下吸管。取色保留该行原来的 alpha，是一步撤销。
- **色板。** 项目的色块，点一下就用；*Add This Colour to the Palette* 把这一行的颜色加进去，在色块上右键可以去掉它。色板是项目设置（Project Settings > Plugins > Dream GUI > Editor > Palette， `UDreamGUISettings::ProjectSwatches`，存在 `DefaultDreamGUI.ini`），项目里每个人看到的是同一套颜色。

渐变的 **Stops** 行也给每个色标配了吸管，在色标下方、色块旁边：取到的颜色就是那个色标的颜色，保留它的 alpha。

两者都是 `DreamGUIEditor` 模块的控件，项目自己的编辑器也能用：`SDreamUIEyedropperButton`（只要读色用 `FDreamUIEyedropper::Sample`）、`SDreamUIColorQuickPick` 和 `SDreamUISwatchGrid`。它们都不写值：调用方拿到颜色后在自己的事务里写。

<a id="the-preset-library"></a>

### 预设库

**Presets** 标签页（在调色板旁边，Window 菜单里也有）把项目的预设分成三栏：**Gradients**（渐变）、**Styles** （样式）和 **Effects**（效果参数）。每一栏列出项目设置里的文本预设和预设资产：

| 栏 | 文本预设（Project Settings > Plugins > Dream GUI） | 资产 |
| --- | --- | --- |
| Gradients | `GradientPresets`：CSS，`linear-gradient(90deg, #FF8A5B, #2B5FD9)` | `UDreamGradientAsset` |
| Styles | `StylePresets`：就是上面粘贴图层样式用的那几行 | `UDreamLayerStylePreset` |
| Effects | `EffectPresets`：一个效果节点 `Glass { Refraction = 18  Frost = 0.25 }`，或一个行为的一行 `+ DreamMeshModifierWarp { Style = Arc  Bend = 60 }` | —— |

双击一个预设，或点 *Apply to Selection*，套用到选中的 widget 上；拖到视口或层级里的某个 widget 上，就套用到那个 widget（拖到选中的某一个上时，套用到整个选择）。每次都是一步撤销，并写进 `.dui`：

- **渐变**写到 widget 的第一个 paint 上——文字的字面（`TextStyle.FacePaint`，并打开它）——没有的话写到第一个渐变属性，并打开它靠来显示的开关（它的 `EditCondition`，比如 RectBlock 的 `bEnableBodyGradient`）；把渐变预设拖到细节面板里某个渐变行上，就用它画那个渐变；
- **样式**写到 widget 的图层样式上，和粘贴一样；预设带了混合选项的，连同混合一起写；
- **效果参数**在同一种效果的 widget 上设它写到的那些值，其余不动；行为的参数会在 widget 没有该行为时把它加上。

资产是被跟随的：文字的 paint 把渐变资产记作自己的 Preset，图层样式把样式预设记作自己的 Preset，资产改了它们也跟着改。右键菜单里的 *Apply as a Copy* 改为把资产的值写进去。右键菜单还能把预设换一种方式保存——文本预设存成资产，资产存成文本预设——以及删除文本预设。*Save Selection* 把选中 widget 的渐变、图层样式或效果参数存成一个新预设。

样式预设资产也可以带上图层的混合选项（*Include Layer Blending Options*，`bIncludeBlending`）：套用时顺带设一次 widget 的混合模式和不透明度；跟随这个资产的图层样式只画它的效果。`DreamUIStylePresets`（`DreamGUIEditor` 模块）供项目自己的编辑器创建样式预设：`CreatePresetAsset`、`SaveTextPreset`，以及图像编辑器式的「新建样式」窗口 `PromptNewStyle`。

<a id="the-viewport-toolbar"></a>

## 视口工具栏

| 项 | 作用 |
| --- | --- |
| Screen Size | 预览的设备分辨率；*Flip Orientation* 交换宽高，*Show Resolution Guides* 叠加常见分辨率 |
| Size Rule | *Custom*：画布是选定或输入的分辨率；*Fill Screen*：画布就是视口，一个设计单位一个像素；*Desired*：按根 widget 的面板测量出的尺寸设定画布，适合按自身大小制作一个 tooltip 尺寸的 widget |
| Zoom | *Zoom to Fit* 框住整个画布（F 框住选中项），*Zoom 1:1* 一个设计单位一个屏幕像素 |
| Rulers、Guides、Layout Debug | 标尺、吸附参考线、选中 widget 的测量 / 排列 / 槽 / 裁剪诊断 |
| Safe Zone | 画出平台的标题安全区（桌面平台需要 `r.DebugSafeZone.TitleRatio` 才会有） |
| Respect Locks | 关掉后可以选中、拖动锁定的 widget 而不解锁 |
| Designer Chrome | 关掉画布边界、选框、手柄、参考线等所有覆盖层，只看 widget 本身 |
| Preview DPI | 按项目的 DPI 曲线缩小画布，预览 Slate 实际会施加的应用缩放 |
| Preview Language | 用另一种文化解析预览里的文本，相当于 UMG 的本地化预览；关闭设计器时自动关掉 |
| Screen Space / Canvas Eye | 按运行时的方式把预览构建成屏幕空间叠加画布（声明的透视才会生效）；把 3D 视图的相机放到画布自己的虚拟相机上 |

<a id="source-files-classes-backed-by-a-dui"></a>

## 源文件：.dui 支撑的类

一个类的层级可以来自一个 `.dui` 文件。工具栏上的 *DreamUI Source* 下拉菜单管这件事：

| 菜单项 | 作用 |
| --- | --- |
| Create Source File... | 写一个起步用的 `.dui`（一个根加一个居中的标签），让类指向它并编译。只在类还没有源文件时提供 |
| Set Source File... | 选一个已有的 `.dui` 作为这个类的层级来源，然后重新编译 |
| Open in Default Editor | 用系统关联 `.dui` 文件的程序打开 |
| Show in Explorer | 在文件浏览器里选中这个文件 |
| Reveal in VS Code | 在 VS Code 里打开并定位到正确位置（层级面板的右键菜单里也有，会定位到节点所在的行） |

能指定源文件的前提是类派生自 `UDreamTextUserWidget`；源文件就是它的类默认值 `SourceFile`。从脚本做同样的事见[脚本](scripting.md)。

<a id="converting-a-hand-built-blueprint"></a>

### 把设计器搭的蓝图转成 .dui

工具栏上的 **Convert to Text**（只对没有 `.dui` 的蓝图显示）、内容浏览器右键菜单里的 **Convert to Text (.dui)**（可多选），或者 `DreamUIConvertBlueprints` commandlet，会把一个在设计器里搭层级的蓝图变成由 `.dui` 编写的类：

- 文件由设计器用的同一套写回写出：层级、每个和默认值不同的值、行为和面板、绑定（`<-`）和路由（`->`），以及资产保留的每个动画，写成 `timeline Name external`。文件放在 `DUI/<path under /Game>/<AssetName>.dui`，已存在就不写。
- 节点 id 就是编译器给控件起的名字，所以事件图、变量和每个绑定照常工作。
- 文件装不下的东西先列出来——没有写法的值、找不到行的节点或绑定——确认后才继续（commandlet 要加 `-AllowLoss`）。这部分会丢掉。
- 改动之前会在旁边复制一份备份资产 `<AssetName>_PreDui`；确认没问题后自己删。
- 父类是 `UDreamUserWidget` 的会改成 `UDreamTextUserWidget`（它的子类，事件图不受影响）。父类是其他不继承 `UDreamTextUserWidget` 的类时拒绝——先让那个 C++ 基类改继承 `UDreamTextUserWidget`。
- 编译后会拿重建的树和原来的树比对，有不一致就报出来。

```text
UnrealEditor-Cmd.exe <project>.uproject -run=DreamUIConvertBlueprints -Path=/Game/UI [-DryRun] [-AllowLoss]
UnrealEditor-Cmd.exe <project>.uproject -run=DreamUIConvertBlueprints -Assets=/Game/UI/WBP_A,/Game/UI/WBP_B
```

`-DryRun` 只打印报告，不改任何东西。commandlet 会保存每个转换后的蓝图和它的备份；有蓝图被跳过时返回非零。

<a id="the-text-is-the-only-truth"></a>

### 文本是唯一的真相

一个类一旦指向了 `.dui`，**文件就是它层级的唯一真相，设计器是文件的图形前端**。换来的是：一个地方读、一个地方 diff、一个地方 review。代价是设计器不再什么都能做——而且它会在你动手之前就告诉你，而不是让改动存活到下次编译再悄悄消失。

**值会写回文件。** 改一个属性，写回会落在**持有这个值的那一行**上——不管节点的类型怎么写，有 `@slot { … }` 块就写进那个块，没有 id 的节点用它生成的 id 定位。设计器从不覆盖在磁盘上被改过的文件（DUI7002）。在画布上移动一个控件也是值的修改：它重算锚点，而 `AnchorData.*` 是会写回的；旋转写成 `RelativeRotationEuler`，缩放写成 `RelativeScale`。资产引用（字体、纹理、材质）写成带引号的路径，列表、Set 和 Map 写成 `[a, b]` 和 `{ key: value }`，指向本层级另一个控件的引用写成那个节点的 id。节点从**样式**的 `+ Component` 行拿到的值，写成节点自己的覆盖行（它自己的 `+ Component { … }` 行），共享的样式不动。

**结构也会写回。** 创建、删除、复制、粘贴控件，移动和调序，重命名（带 `(was: OldId)`，编译会把旧名字拥有的东西带过去），包一层和拆一层，添加、删除和重排行为，替换面板，以及三个类选择器（视觉、面板、自身布局），都会在从文件重建树的那次编译之前写进文件，并且和文件一起撤销。文件装不下的编辑在动手之前就拒绝，并说明原因——`for` 或 `each` 的模板、`if` 显示的控件、另一个文件的组件内部、给行改名或加 `+` 块。`rows` 表格的行可以删除、在本表格内移动和复制，都按它的那一行来。

**绑定和路由就是行。** Bind 控件写 `Property <- Function()`（*Create Binding* 会先检查这行能不能写再建函数）；*Remove Binding* 删掉这行。被绑定属性的值行保持只读，它的 Bind 菜单里有 **Unbind and Set to This Value**，把当前显示的值写在绑定的位置。通过 `OnValueChangedBP` 报告值的控件（滑条、数值框、开关）会多一项 **Two-Way Bind to Variable**：候选是类自己的变量和 viewmodel 成员，类型必须和事件给出的完全一致，写成 `Value <-> Variable`。

**写不回去的属性行是灰的**，并说明原因：

| 原因 | 例子 |
| --- | --- |
| 文件用绑定写它 | `Text <- GetTitle()`——写一个字面量会删掉绑定 |
| 由循环展开而来 | `for` 生成的 N 个副本没有逐个实例的值可写 |
| 在可写集合之外 | 写回没有地方放这个属性，改了也活不过编译 |
| 值在 `.dui` 里没有写法 | 指向另一个文件层级内部对象的引用，或语言写不出的值。预览会变，文件不会变，下次编译改动就没了 |

写回时还会明确拒绝几种情况，"说出来而不是写进去"：一个值没法写成语言读得回来的字面量（今天只有非有限浮点数），报 DUI7003；一个由 `if` 决定的可见性，或一行没人写过的语句，报 DUI7004。代码含义见 [DUI7xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx)，每种编辑在文本里变成什么见[设计器写回](../language/write-back.md)。

自己在设计器里搭层级、不指向 `.dui` 的类，以上限制都不存在。

<a id="events"></a>

## 事件

细节面板的 **Events** 一节列出选中控件能发出的事件。`+`（*Add Event Handler*）会在事件图里创建一个签名正确的自定义事件，**以及**一条指向它的路由——和 `.dui` 里的 `OnClick -> HandleConfirm` 是同一个东西。已经有处理函数时，同一个按钮打开它。*Remove Event Route* 去掉路由。在由 `.dui` 支撑的类上，路由就是那一行：写在节点的行上，节点自己写的 `+ Component` 行为则写进那个块，和处理函数在同一个事务里；*Remove Event Route* 删掉这行。处理函数无论哪种情况都留在事件图里。

路由名的是**用户控件**（层级编译成的那个类）上的函数，UMG 也把事件处理放在那里。编译器把每条路由解析进 `UDreamWidgetBlueprint::EventBindings`，`UDreamUserWidget::BindEventBindings` 在初始化时接上它们。它能接到本插件的两种事件：控件声明的 `BlueprintAssignable` 动态多播委托，和较老的 `Interaction/` 行为声明的 `FDreamUIEventDelegate` 属性。

**注意：**

  `FDreamUIEventDelegate` 自己的逐实例事件列表是遗留的、只读的。存在里面的绑定照常触发，也能从面板删掉，但不能再在那里新建：那种绑定是用字面量参数调用任意对象上的函数，UMG 没有对应物，`.dui` 也没有语法。打开含有这种绑定的面板会打一条警告点名它。不会自动改写——把"带这个值调用那个行为上的 `Foo`" 改成"调用用户控件上的处理函数"会改变游戏的行为——所以碰到时请重新写成路由。

<a id="the-animation-editor"></a>

## 动画编辑器

动画编辑器基于 Sequencer。`.dui` 里的 `timeline` 块编译成类里的一个 `UDreamWidgetAnimation`；因为**文件拥有它**，每次编译都会重建。在编辑器能写回 `.dui` 的类上，动画编辑器以可编辑方式打开它，并**把每次编辑写回 timeline 块**：关键帧的时间和值、两个关键帧之间的 ease（从编译采样出的曲线反认出来）、事件关键帧、duration，加一条轨道就写一行、删一条就删一行。关键帧没变的轨道，那一行逐字节不动。Sequencer 用自动切线加的关键帧会变成线性，序列里也一样，所以看到的就是文件会构建出的；自定义切线或阶梯插值的关键帧没有写法，会被拒绝并给出出路。重命名仍被拒绝（"在 `.dui` 的 timeline 块里改名"）。

要让 Sequencer 接管一个动画，把它的块改成 `external`——下面 `Pulse` 归文件，`Celebrate` 归资产：

```dui
class /Game/UI/WBP_Results

Widget Root {
    Text Title { Text = "Victory" }
}

timeline Pulse {
    duration = 0.6
    loop     = PingPong

    Title.RenderScale : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1) ease InOutQuad, 0.6 = (1, 1, 1)
}

timeline Celebrate external
```

`external` 不构建任何东西。它是一条清单：动画活在资产里，Sequencer 随便编辑，而文件仍然列着它—— 这让"这个类有哪些动画"可以靠读文件回答。材质参数轨道、手调曲线这类 timeline 写不出来的东西，属于 `external`。见 [Timelines](../language/timelines.md)。

<a id="the-content-browser"></a>

## 内容浏览器

**添加菜单。** **DreamGUI Widget** 在 *添加* 菜单最上面一组，和引擎的 Blueprint Class、Material 排在一起。下面的 **DreamGUI** 子菜单分节：Basic（DreamGUI Widget、Widget Animation）、Fonts、Graphics、Rich Text，每一项是一个短名字加一行提示。位图字体已经不再开发，它显示为 Legacy Bitmap Font，提示里指向距离场字体。资产的类型名——磁贴上、过滤器里的那个——没有变。

**缩略图。** 一个控件 Blueprint 的磁贴画的是它编写的那个界面：缩略图像设计器的预览一样把控件建出来、按设计画布的尺寸排好、画出来，和 UMG 画控件缩略图的方式一样。它在资产还没有干净的缩略图时和保存时画，不会在指针停在磁贴上的每一帧都画，所以大界面不会卡住内容浏览器。已经存进资产里的旧缩略图（锚点线框）会一直显示，直到资产下一次被修改并保存；还没有编译出类的 Blueprint 仍然画线框。
