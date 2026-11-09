---
title: 更新日志
description: DreamGUI 每个版本改了什么——main 上有什么、尚未发布的内容，1.0.0 首个公开版本，以及往前一直到 2.1.0 和 2.0.0 两个开发版。
---

DreamGUI 每个版本改了什么，按它对一个项目带来的变化来分组。把项目从一个版本搬到下一个版本——哪些东西加载起来不一样了、哪些默认值变了、自己的 C++ 要改什么——见[升级](../guides/upgrading.md)；从 LGUI、LexUI 或早期 1.x 构建过来的项目，从[迁移](../guides/migration.md)开始。本页与仓库的 `CHANGELOG.md` 同源： https://github.com/TypeDreamMoon/DreamGUI/blob/main/CHANGELOG.md

**提示：**

**1.0.0 是首个公开发布版本。** 在它之前的构建是这个分支的开发线：先是单模块的 1.x 构建，然后是 2.0.0 和 2.1.0（`main` 上的提交 `2416e3f8` 和 `3049561a`）。公开版本号从 1.0.0 重新开始，所以下面的 2.0 或 2.1 都是开发版，比 1.0.0 更旧。插件描述文件里的整数 `Version` 一直在涨——1.0.0 是 100，2.1.0 之后是 3——所以引擎永远不会把 1.0.0 当成两者中较旧的那个。

<a id="unreleased"></a>

## 未发布

1.0.0 之后 `main` 上的内容，会随下一个版本发布。

<a id="new"></a>

### 新增

- **裁剪蒙版。** 裁剪到自身边界的控件可在其裁剪上使用蒙版（`ClipMask`）：贴合裁剪矩形的椭圆、多边形、星形或环形，羽化边缘，叠加其上的纹理通道（拉伸、适配、填充或平铺），或程序化图案（线性、径向、角度、菱形、百叶窗、棋盘、噪点），并通过阈值与柔和度实现擦除和溶解效果，还可按需反转——聚光灯。它在每条绘制路径上裁剪整个子树，并可通过蒙版进行命中测试。没有蒙版的裁剪链的开销与上传与原来完全一致。参见[裁剪蒙版与裁剪到下方图层](../concepts/clip-masks.md)。
- **裁剪到下方图层。** `bClipToBelow` 仅在先于它绘制的同级项被绘制的位置、并以该同级项的不透明度显示控件及其下方的一切：即裁剪蒙版。下方图层可以是简单或九宫格图像、纹理或精灵，或矩形块；其他任何类型都不会被裁剪到，并给出警告。
- **混合模式。** `EDreamUIBlendMode` 从三种模式增加到 27 种：溶解、变暗、颜色加深、线性加深、较暗颜色、变亮、滤色、颜色减淡、较浅颜色、叠加、柔光、强光、亮光、线性光、点光、硬混合、差值、排除、减去、划分、色相、饱和度、颜色和明度，按 W3C 的合成公式在编码值上计算。滤色、排除和溶解由混合硬件完成并像以前一样合批；其余模式在后方内容的副本上混合，副本每次绘制时生成，同一模式的元素在不重叠时共享一次绘制。通过带这些新模式之一的材质绘制的元素经由仅含一个元素的组绘制。参见[混合模式与图层组](../concepts/blend-modes-and-groups.md)。
- **图层组。** 容器上的 `UDreamLayerGroup` 会将其子树绘制到自己的纹理中，并作为一个整体绘制：`GroupOpacity` 淡出它时子级不会互相透出，`GroupBlendMode` 将其作为整体混合，`GroupMask` 对合成结果应用蒙版。在隔离的组内，子级的模式仅与组混合。组可以嵌套；其纹理来自帧的池。裁剪到没有裁剪层级跟随的下方图层——文本、平铺图像、容器、组——通过这样的组完成，`UDreamRetainerBox` 通过相同的合成绘制其保留的纹理（新增 `GroupBlendMode`）。在 DreamGUI 自己的渲染器绘制的每块画布上——包括 World Space - DreamUI Renderer 中的世界空间画布以及设计器和关卡编辑器的预览——组的纹理会像其元素一样被场景遮挡；由引擎渲染器绘制的画布会将其直接透传，并给出警告。

- **视图模型。** 一个 `.dui` 文件在 `viewmodels { … }` 块中声明它的类持有的视图模型——按类声明，并写明每个从哪来：宿主（创建引脚、`SetViewModel`、宿主的 `.dui` 行）、`= new`、从新的 `UDreamViewModelSubsystem` 来的 `= global` / `= global "Name"`，或者从最近的外层控件来的 `= parent`。绑定、路由和循环通过成员路径到达它们：`Text <- Player.Name`、`Value <-> Settings.MasterVolume`、 `OnClicked += Settings.Apply()`、`OnValueChanged += Settings.SetVolume(Value)`、`for Item in Inventory.Items`。任何 UObject 类都可以是视图模型；`UDreamViewModel` 是方便的基类（C++ setter 使用 `DREAM_VM_SET`），任何实现了 `INotifyFieldValueChanged` 的东西——包括 UE 的 `UMVVMViewModelBase`——都会被订阅，不依赖 ModelViewViewModel 插件。路径上的对象没有全部设置好时不求值，属性保留原来的值。见[视图模型](../language/viewmodels.md)。
- **绑定在它读取的内容变化时更新。** 编译器记录 `<-` 表达式读取的每个变量和成员；当它们全部宣布自己的变化（FieldNotify）时，绑定就被订阅，两次变化之间不花任何代价。FieldNotify 变量上的 `Text <- Title` 和 `Shown <- Count > 0` 以前每帧都会读取。 `DreamUI.Binding.Dump` 列出哪些绑定每帧读取以及为什么；`DreamUI.Binding.ForcePoll 1` 让它们重新全部每帧读取。见[绑定什么时候更新](../language/bindings.md#bindings-and-routes)。
- **行各自更新。** 在 `for` 或 `each` 中，副本所显示的成员宣布变化时，该副本会更新，而不用刷新整个列表；`Event -> Item.Func()` 把每个副本的事件路由到它自己那一项——列表视图回收单元格时跟着挪过去。源是有类型的（`TArray<UItemVM*>`）时，编译器检查 `Item.Member`（DUI6021）和 `Item.Func`（DUI6022）。
- **C++ 的 `UDreamUserWidget` 子类可以声明 FieldNotify 成员。** `DreamUserWidget.h` 现在包含 `FieldNotificationDeclaration.h`，UHT 为它们生成的代码需要它，就像 UMG 的 `Widget.h` 那样；这样的类以前编译不过。绑定到该成员的 `.dui` 通过它的描述符订阅，`DREAM_VM_SET` 就是它的 setter。
- **事件上的 `+=` 和 `=`**，与 `->` 并列：`+=` 添加一个监听者，只接受多播事件，`=` 是单播委托唯一的监听者——路由现在终于能指向它了——并且只接受它；用错是 DUI5025。 `->` 两种都接受。
- **Create Dream Widget、Add to Viewport 和 Add to Player Screen**，即 UMG 的 Create Widget 节点以及与之配套的两个 user widget 调用。在节点上选一个类，它就为每个 Expose on Spawn 属性开一个引脚——.dui 文件的每个 `props` 条目都是一个——外加一个 Owning Player 引脚，并把结果类型设成那个类。引脚在控件初始化之前赋值，所以 Pre Construct、On Initialized、On Construct 和每个绑定的第一次取值都已经能看到它们；UMG 在 Create 之后才赋值，对 On Initialized 来说太晚了。Add to Viewport 把控件放上它拥有者玩家的屏幕，并且和 UMG 一样，拒绝已经有父节点的控件；Add to Player Screen 还会拒绝没有拥有者玩家的控件。Remove from Parent 本来就有。C++ 拿到节点的两半： `BeginCreateDreamWidget` 和 `FinishCreateDreamWidget`。
- **Add Component 只列出选中控件支持的组件**（`FDreamUIComponentSupport`）。网格修饰器要一个会建网格的视觉，精灵播放器要 Sprite，精灵图集播放器要 Texture，超链接和文字动画要 Text，UMG 交互要 UMG Widget 视觉；Retainer Box 或 Invalidation Box 要控件先有画布；画布、导航、动画组件、具名槽位和内容控件每个控件一个，一个控件只能是一种可选择对象和一种滚动视图，有多个子节点的控件不提供内容控件。这些以前哪里都能加，加了之后什么都不做。粘贴、复制和拖到面板上的组件资产遵守同样的规则；被拒绝的拖放会说明原因。项目在自己的编辑器模块里为自己的组件加规则。见[组件](../tools/designer.md#components)。
- **调整图层。** `UDreamAdjustmentLayer` 作用于其后方的内容，范围以其矩形、裁剪和蒙版为限：按顺序最多八项操作——亮度和对比度、色阶、曲线、曝光、色相和饱和度（或着色）、自然饱和度、色彩平衡、黑白、照片滤镜、通道混合器、反相、色调分离、阈值、渐变映射和查找表——通过 27 种混合模式中的任意一种与不透明度与原始内容混合。当 `Target` 设为 `Children` 时，它改为作用于自身控件的子树，并作为组绘制。参见[作用于后方内容的效果](../concepts/backdrop-effects.md)。
- **渐进模糊。** `UDreamBackgroundBlur` 接受一个 `StrengthMap`——纹理的通道、线性或径向渐变，或裁剪蒙版的图案——并按照该图在此处指定的强度模糊每个像素，基于后方内容的模糊金字塔。没有图时，它就是原来的均匀模糊。
- **玻璃。** `UDreamGlass` 是一块圆角矩形玻璃，其边缘会折射后方的内容，红色和蓝色略微分离，可选磨砂、着色并沿边缘打光；其圆角与矩形块的圆角一致，点击圆角之外不会命中它。
- **风格化效果。** `UDreamStylizeEffect` 在后方内容或其自身子级之上叠加最多八层：CRT 屏幕、故障（`TriggerBurst` 会使其闪光）、色差、单色或四色半调网点、有序抖动、胶片颗粒、暗角、色调分离。
- **图层样式。** `UDreamLayerStyle` 给任何控件加图像编辑器式的图层样式（`FDreamLayerEffects`）：投影、外发光、内阴影、内发光、斜面和浮雕（内斜面、外斜面、浮雕、枕状浮雕、描边浮雕；平滑或雕刻）、光泽、颜色/渐变/图案叠加和描边（外部、内部、居中；颜色、渐变或图案），各有自己的混合模式、不透明度和对话框里的那些设置——等高线、杂色、扩展与阻塞、范围与抖动、全局光。 `UDreamLayerStylePreset` 存一份共用。RectBlock 在自己的像素里画样式，照旧合批；其它的——文字、图片、容器的子树、图层组—— 单独绘制，按 alpha 做跳跃泛洪量距离，再逐层放下，每个外部效果按自己的模式。RectBlock 自己的阴影、描边和渐变可以按图层样式读写（`GetEffectsAsLayerStyle`、`SetEffectsFromLayerStyle`）。见[图层样式、形状融合与光](../concepts/layer-styles.md)。
- **形状融合。** `UDreamShapeBlendGroup` 用平滑最小值把容器子项里的 RectBlock 融成一个形状——各按 `ShapeBlendOperation` 并集、相减或相交——由第一个成员用一个四边形画出，颜色按成员混合，组上的图层样式围绕它绘制；点击按同一公式落在最近的成员上。
- **指针光、倾斜卡和全息箔。** `UDreamPointerLight` 跟随指针，照亮它下面图层样式的斜面和 RectBlock 的描边（Reveal 高光）； `UDreamTiltCard` 让控件带弹簧地朝指针倾斜；`UDreamHoloFoil` 给图片或 RectBlock 画上闪卡的全息箔，视角取指针或倾斜卡。
- **融合形状里的玻璃。** `UDreamGlass` 的 `Shape` 设为 `ShapeBlend` 时取形状融合组把 RectBlock 融成的形状——`ShapeBlendGroup` 指定的控件上的，或它自己控件上的——画满整个形状，成员伸到哪里画到哪里，边缘沿融合后的轮廓折射背后画面；点击落在形状上即命中。见[作用于背后画面的效果](../concepts/backdrop-effects.md)。
- **网格变形。** `UDreamMeshModifierWarp` 可按图像编辑器文本变形中的十五种样式之一（弧形、下弧和上弧、拱形、凸起、贝壳下和上、旗帜、波浪、鱼形、上升、鱼眼、膨胀、挤压、扭转）对任何批量网格视觉绘制的内容——图像、矩形块、逐字形的文本——进行弯曲，并带有弯曲度以及水平和垂直扭曲，或通过每个方向 2 到 5 个点的贝塞尔格架，在设计器视口中拖动。网格会先沿整个网格共享的一套网格线细分得更细，每个属性都进行插值，因此圆角、字形距离场和像素会随之弯曲；静止时没有任何开销。参见[变形、果冻与路径文本](../concepts/warp-and-text-on-path.md)。
- **果冻。** `UDreamMeshModifierJelly` 将网格固定在一组阻尼弹簧构成的格架上，这些弹簧会滞后于控件的移动，在按下时压扁、松开时弹回、拖动后拉伸，并以固定步长推进，使相同的帧产生相同的晃动；按压和拖动会传递给控件的按钮或滚动视图。
- **路径文本。** `UDreamMeshModifierTextOnPath` 将文本的字形沿圆周、弧线或 `UDreamPath` 控件绘制的路径（一种新的线条视觉，经过可编辑的点，直线或平滑、开放或闭合、绘制或不绘制）排布：对齐到其起点、中点或终点，设置间距，抬离路径，翻转到另一侧，沿路径滚动，每个字形整体旋转或弯曲；文本动画的位移会保留。
- **修改器阶段。** 网格修改器按阶段运行（`GetStage`）：作用于字符的、路径文本、切割网格的变形，然后是副本（阴影、描边、长阴影）——现在无论先添加哪个组件，都会复制弯曲后的网格。
- **蒙版过渡。** `UDreamMaskTransition` 通过移动的裁剪蒙版显示或隐藏控件的子树——擦除、时钟、光圈、菱形、百叶窗、棋盘、溶解、墨水、碎裂或纹理的顺序——并带有柔和度、反向、反转和中心；其 `Progress` 支持补间和属性轨道。`CrossFade` 通过一个形状交换两个控件，每个像素由其中之一显示，`PlayScreenTransition` 覆盖和揭开屏幕。墨水、碎裂和棋盘读取代码中生成的纹理。它驱动控件的裁剪（`UDreamWidget::SetDrivenClipMask`，新增）而不触及它的 `ClipMask`，完全显示时没有任何开销。参见[蒙版过渡](../concepts/mask-transitions.md)。
- **反转的裁剪蒙版。** `FDreamClipMask::bReverse` 在阈值之前以相反方向读取纹理或图案的值：渐变从右到左，径向渐变从边缘向内。
- **Niagara 粒子。** 新模块 `DreamGUINiagara` 把 Niagara 系统画进 DreamGUI 画布：`NiagaraSystem` 视觉用一个不渲染、不受可伸缩性管理的组件模拟系统，tick 之后读取每个 CPU 发射器的 Sprite 和 Ribbon 渲染器，写成自己的网格段——各一个绘制调用——像任何元素一样被裁剪、遮罩、混合和编组。Sprite 保留图集帧和帧间混合、轴心、旋转、对齐和排序；Ribbon 保留 id、link order、两套 UV 和穿过各点的曲线。一个单位是控件平面（XZ 或 XY）上的一个像素；时间可以暂停、缩放、游戏暂停时照跑、固定步长推进和快进。内置着色器用粒子材质的贴图和混合方式画，或者用转换出的 UI 材质画：**Convert Particle Materials** 把粒子材质复制成 UI 材质，粒子输入改读顶点，不透明度经过 DreamGUI 的裁剪。系统里声明了 `User.DreamUI_WidgetSize`、`User.DreamUI_PointerPosition`、`User.DreamUI_PointerInside` 或 `User.DreamUI_TargetPosition` 的会被写入。`BurstAt` 在一点再生成一次系统，`BurstTo` 让飞行粒子（默认用插件的飞行模板）飞向另一个控件，`Niagara Trigger` 组件在点击、指针进出和进度填满时播放系统。GPU 发射器和 Mesh 渲染器会被报告，暂不绘制。插件现在依赖 Niagara。见 [Niagara 粒子](../concepts/niagara.md)。
- **直绘网格段。** `UDreamVisualDirectMesh` 可以分多个网格段画、每段一个绘制调用，可以把着色交给画布——内置着色器或材质，像批处理元素一样拿到裁剪数据、遮罩纹理和混合模式——也可以在画布每帧发送网格段时写自己的顶点。
- **导入分层文档。** `.psd` 或 `.psb` 导入为 DreamUI 控件：一个把图层排成控件的 `.dui`、它们的贴图，以及以这个 `.dui` 为源文件的控件蓝图——从文件夹的 DreamGUI 菜单、commandlet（`-run=DreamPsdImport`），或打开设置后从内容浏览器的 Import 导入。像素图层变成裁过边的贴图图片，组变成容器（混合或淡出时挂图层组），实时矩形和圆变成 RectBlock，填充变成 body，文字经字体映射变成文字；蒙版变成裁剪遮罩，剪贴变成 `bClipToBelow`，混合模式对应 27 种。图层名可写 `!skip`、`@Button`、`#9(l,t,r,b)`。再次导入会编辑上次写的 `.dui`，保留作者加的每个节点和每一行，并列出被覆盖的内容。图层样式、调整图层和文字变形已读出并列出，暂未导入。新编辑器模块为 `DreamGUIPsd`。见[导入分层文档](../guides/layered-document-import.md)。
- **RectBlock 的多色标渐变。** RectBlock 的主体和描边改用 `BodyGradient`、`BorderGradient`，都是最多 16 个色标的 `FDreamGradient`—— 线性、径向、角度（Conic）、菱形、对称或四角，和文字涂色是同一种渐变，从世界的同一组渐变行里画——代替原来的两色径向渐变，内置着色器和 RectBlock 材质都支持。细节面板里显示成带类型和预设的色带；`.dui` 里写它的 CSS。以前的两色渐变自动迁移：渐变没有色标时画成两个色标（每通道最多差一级），`.dui` 里的旧行在渐变被改动前原样保留，改动后由渐变的一行替换；旧 setter 仍可用。见[渐变与视口手柄](../concepts/gradients.md)。
- **设计器里的渐变手柄。** 选中的 RectBlock 的渐变像图像编辑器的渐变工具那样画在上面：可拖的端点和中心，连线旁的色标（沿线拖动、拖离删除），双击连线加色标。Shift 让旋转按 15° 一档，Esc 还原；一次拖动一步撤销，松开鼠标时写回 `.dui`。
- **通用视口手柄。** 用 `FDreamDesignerHandles::Register` 注册的 `IDreamDesignerHandleProvider` 给选中的控件提供可在设计器视口里拖的点；命中、绘制、撤销、Esc、双击和写回 `.dui` 由视口统一处理。变形晶格点和路径点也改走这套。
- **属性用标签要手柄。** 控件、视觉或行为的属性标上 `meta=(DreamGUI_Handle="Kind")`（或 `"Kind:Args"`），视口就为它画这种手柄，提供者由该种类的工厂为它造（`FDreamDesignerHandles::RegisterKind`）。RectBlock 的 `BodyGradient`、`BorderGradient` 标的是 `Gradient`。见[属性 meta 标签](../guides/property-meta.md#viewport-handles)。
- **对称渐变。** `EDreamPaintType::Reflected`（`reflected-gradient(…)`）从过渐变中心的一条线向两边对称展开，文字和 RectBlock 都能用。
- **遮罩、玻璃、阴影和光的视口手柄。** 设计器视口给裁剪 widget 的裁剪区和遮罩画手柄——裁剪矩形的四条边（它的边距）、矩形遮罩的圆角、多边形的圆角、星形或圆环的内径和羽化，轮廓按遮罩自己的距离函数描出；玻璃的圆角、边缘宽度和光；图层样式和 RectBlock 的每个阴影，拖到它落下的位置就同时改角度和距离（用全局光的样式阴影转的是全局光，用它的效果一起转）；指针光的位置和照射范围，以及画成圆盘的图层样式全局光。都随属性标签挂上（`DreamGUI_Handle`：`MaskShape`、`GlassShape`、`Shadow`、`Light`）。圆角和羽化默认一起动，Alt 只动一个；Shift 让角度按 15° 一档。见[视口手柄](../tools/designer.md#viewport-handles)。
- **效果的光在一处读写。** `DreamLayerStyleLight`（`Core/DreamLayerStyleTypes.h`）在效果的角度和高度实际存放的地方读写它们——用全局光的效果是样式的全局光——并在阴影的角度、距离和偏移之间换算；渲染器打包阴影偏移也走它。
- **复制和粘贴图层样式。** 设计器的 widget 菜单复制选中 widget 的图层样式（*Copy Includes* 勾上时连同混合模式、不透明度、裁剪和遮罩），粘贴到每个选中的 widget 上，一步撤销，并写进 `.dui`。剪贴板里是 `.dui` 的几行，所以能贴到别的 Blueprint、贴进文本编辑器，手写的几行也能粘贴。见[控件设计器](../tools/designer.md#copying-and-pasting-a-layer-style)。
- **吸管和项目色板。** 设计器细节面板的每个颜色行都有吸管，读设计器视口自己的画面（效果、样式、混合之后的像素）或屏幕上的像素；旁边是项目色板（`UDreamGUISettings::ProjectSwatches`），点一下就用，也能从这里加色。两者都是项目自己的编辑器也能用的控件（`SDreamUIEyedropperButton`、`SDreamUIColorQuickPick`）。见[控件设计器](../tools/designer.md#the-eyedropper-and-the-projects-palette)。
- **预设库。** 设计器新增 Presets 标签页，分栏列出项目的渐变、图层样式和效果参数——项目设置里的文本预设（`GradientPresets`，以及新增的 `StylePresets`、`EffectPresets`）和资产（`UDreamGradientAsset`、`UDreamLayerStylePreset`）——双击或拖到 widget 上套用到选择，可跟随也可复制，也能从选中的 widget 存成预设。样式预设资产可以带上图层的混合模式和不透明度（`bIncludeBlending`）；`DreamUIStylePresets` 供项目自己的编辑器创建样式预设。见[控件设计器](../tools/designer.md#the-preset-library)。
- **图层样式对话框。** 设计器可以在选中的 widget 上打开图像编辑器那样的图层样式对话框——widget 菜单里的 *Layer Style...*、 Layer 区图层样式旁的 *Edit...*，或在层级面板里双击样式标记：效果带勾选框列出，同类效果可加多个、可拖动排序，旁边是各自的参数；每次改动立刻在 widget 上预览，OK 一步撤销写进 `.dui`，Cancel 文件不变，New Style 存成预设。见[控件设计器](../tools/designer.md#the-layer-style-dialog)。
- **结构体列表能写回 `.dui` 了。** 图层样式的投影、描边和叠加，调整图层的各项调整，以及其他没有简写的结构体列表、Set、Map，都按读入的写法写回——`Style.DropShadows = ["(Angle=120,Distance=6)"]`，只写与结构体默认值不同的字段；以前设计器能从文件读这种列表，却写不回去。见[写回](../language/write-back.md#what-the-designer-writes-back)。

- **编辑器里的图层效果。** 控件的细节面板最上面是 **Layer** 分类：不透明度、视觉的混合模式、剪贴到下一层、遮罩（快速选形状，选了自动开启裁剪到边界）、添加或关闭图层样式和图层组的开关，以及控件上的每个效果——眼睛负责开关，点名字打开设置。层级面板在名字后用小图标标出每个控件的效果（样式、组、遮罩、混合、效果、变形、粒子），悬停看详情，点击开关。调色板收齐了它们：Effects 里的 Glass、Adjustment Layer、Stylize，Components 里的 Layer Style、Layer Group、Shape Blend Group、Mask Transition、Holo Foil、 Pointer Light、Tilt Card，Modifiers 里的 Warp、Jelly、Text On Path，Shapes 里的 Path，拖进来就能看到；后处理蓝图按去掉 `BP_` 和 `_C` 的名字列出。见[控件设计器](../tools/designer.md)。
- **图层效果的开关。** `UDreamLayerGroup`、`UDreamMaskTransition`、`UDreamHoloFoil`、`UDreamPointerLight`、`UDreamTiltCard` 和所有后处理视觉（`UDreamVisualPostProcess`：玻璃、调整图层、风格化、背景模糊……）都有 `bEnabled` 属性，默认开，只有关掉时才写进 `.dui`：关掉时效果如同不存在，设置都保留。
- **不会丢属性的细节面板。** DreamGUI 的细节面板按属性上的 `DreamGUI_` meta 标签排版——区（`DreamGUI_Section="Layer"`）、组、顺序、「高级」展开项、编辑控件、效果的开关——没写标签的属性显示在它自己的分类下。面板不再整类隐藏分类，属性不会因为分类起错就从编辑器里消失；只有 `DreamGUI_Hidden` 会藏起属性。图层区和层级里的效果开关都按标签生成；只对某些取值适用的行（填充、过渡的颜色、密码字符）随取值显示和隐藏，面板不用重建。扩展的类也用同一套标签。见[属性的 meta 标签](../guides/property-meta.md)。

- **编辑控件。** 混合模式在行里和菜单里都是图片——按各自公式画出，27 种按图像编辑软件的顺序和分组排列——悬停在某种模式上，控件就临时按它绘制，点下去之前不记撤销、不保存。标了 `DreamGUI_Editor = "AngleWheel"` 的角度（图层样式的、渐变叠加的、玻璃光照的、遮罩转场旋转的、路径文字起点的）在数字旁边多一个圆盘；开着 Use Global Light 的效果在自己的行里改样式的全局光。等高线是曲线，可选命名形状也可直接编辑；缓动显示曲线。`.dui` 里等高线写命名形状的名字或 `"custom(0 0, 0.5 1 corner, 1 0)"`。见[控件设计器](../tools/designer.md)。

<a id="changed"></a>

### 变更

- **视口同一时刻只显示一种手柄。** 设计器视口只显示选中内容的一种手柄——渐变的、遮罩的、阴影的……——选框下方写着它和按 Tab 会换成的那种；**Tab** 依次轮换，最后是「关」，只剩选框。`FDreamDesignerHandles::SetActive` 选一种，RectBlock 渐变下的 *Edit in Viewport* 按钮就是这么做的；RectBlock 的主体渐变和描边渐变是两种。

- **只读取 FieldNotify 变量的绑定在它的 Blueprint 重新编译后不再每帧读取它们**：变量宣布变化时才更新。C++ 给这样的变量赋值却不广播——FieldNotify 从不允许这样做——不再被轮询掩盖；`DreamUI.Binding.ForcePoll 1` 能把它找出来。
- **Create Dream Widget Of Class 不再出现在 Blueprint 菜单中**：Create Dream Widget 节点代表它，就像 UMG 的 Create Widget 节点代表 `UWidgetBlueprintLibrary::Create` 一样。已经调用它的图表照常编译。由于它是 BlueprintInternalUseOnly，在 Python 中也没有了。
- **内容浏览器的添加菜单在最上面提供 DreamGUI Widget**，和引擎的 Blueprint Class 与 Material 并列，而不再只在一级子菜单里。子菜单本身现在叫 DreamGUI，分节——Basic（DreamGUI Widget、Widget Animation）、Fonts、Graphics、Rich Text——每项一个短标签和一行提示，而不是一整列按类型名排序、widget 排在最后、提示是类的代码注释的列表。不再开发的位图字体显示为 Legacy Bitmap Font，并指向距离场字体。资产的类型名——磁贴上、过滤器里的——不变。
- **设计器的调色板按 UMG 的方式分组。** Basic、Panels、Common、Input、Lists、Scrolling、Containers、Primitive、Shapes、Effects、Components、Modifiers、Advanced、User Created，最后是默认折叠的 Legacy——以前是一个三十多行、无所不包的 Controls 分类，旁边是十来个只差一个词的小分类。条目按名字列出，面板去掉了 "UMG " 前缀，Tile View 和 Scroll Box 两个行为挪进 Legacy，和取代它们的控件放在一起。User Created 不再列插件自己的预设目录。层级面板的 Create 菜单用同样的分组。注册键没变，收藏都还在；拿 `FDreamUIControlDescriptor::Category` 去和 "Controls"、"Post Process"、"Extensions" 或旧分类名比较的项目扩展，改从 `DreamUIPaletteCategory` 读名字。见[调色板](../tools/designer.md#the-palette)。
- **细节面板把 widget 是什么放在它放在哪的正下面。** Slot、Layout，然后 Visual、Panel、Self Layout，然后 Appearance 和 Behavior，然后 Render Transform 和默认折叠的 Perspective、Navigation、Accessibility、Localization；以前视觉排在 Behavior、Appearance、Panel、Self Layout 之后。Text 的行以 Text、Font、Size、Style、Color 和对齐开头，换行、排版、渲染设置放在默认折叠的组里；Sprite 和 Texture 的颜色在它的 sprite / 纹理下面。底部重复 Layout 行的 Transform 和锚点数据分类去掉了，Is Enabled 进了 Behavior。见[细节面板](../tools/designer.md#the-details-panel)。
- **径向渐变的椭圆随 `Angle` 旋转**，和菱形一样。径向渐变的角度不是 0 或 180 的文字涂色（从线性切过来时留下的）现在会画成旋转后的椭圆；圆形不变。
- **RectBlock 的两色渐变属性**不再显示在细节面板里，它们的 setter 标为 deprecated：渐变已有色标时，它们改第一个色标的颜色、中心、半径和角度。按单位写的中心和半径只在渐变还是两色时按单位固定。半透明的主体纹理现在像被主体颜色那样被渐变染色。
- **渐变行的行头多了类型下拉**，在色带和预设旁边。

<a id="fixed"></a>

### 修复

- **写进单行节点的行留在它的块里。** 一次写入往一个在同一行开闭的块（`Text Title { Text = "Hi" }`）里加两个属性时，整次写入会被拒绝；给这样的节点加一行 `+` 时，这一行会写到右括号后面，变成父节点的行为。两者现在都落在块里。
- **Multiply 中的内置元素在部分透明时是精确的。** 其透明像素会把背后的内容变暗成黑色；现在它会按照 alpha 所表示的程度用自身的颜色变暗，与合成时一致。
- **枢轴不在中心的裁剪在父级缩放或旋转时仍贴合其控件。** 其中心之前是按枢轴偏移量以世界单位移动的，而不是通过裁剪器自身的缩放和旋转，因此在画布缩放或父级旋转下，绘制的裁剪矩形会偏离命中测试所用的矩形。现在两者都通过裁剪器的变换来定位它。

- **创建抽象类的控件会记录错误**，而不是停在 NewObject 的断言上。从列表中选择的类不可能是抽象类，但类引脚或由变量提供输入的 Create Dream Widget Of Class 调用则可能。
- **创建一个抽象类的 widget 会记一条错误**，而不是停在 NewObject 的断言上。从列表里选的类不会是抽象的，但由变量喂给类引脚或 Create Dream Widget Of Class 调用的类可能是。
- **DreamUI Widget Blueprint 的缩略图就是它编写的那个界面。** 内容浏览器的磁贴以前是编写时锚点矩形的线框，不知道布局把东西排到了哪里，所以大多数界面画出来是一个灰框加一两条线。缩略图现在像设计器的预览一样建出 widget，在设计画布上排好、画出来，和 UMG 画 widget 缩略图一样；没有编译出类的 Blueprint 仍然画线框。它在资产没有干净的缩略图时和保存时画，不会在指针停在上面的每一帧都画，所以大界面不会卡住浏览器。已经存进资产的缩略图会一直显示，直到资产下一次被修改并保存。见[内容浏览器](../tools/designer.md#the-content-browser)。
- **开了 Substrate 的工程里，Additive 材质画得出来了。** 开 Substrate 时，引擎把旧式 Additive 材质转成「自发光乘不透明度、全透射」的无光照层，DreamGUI 的渲染器把它当成没有覆盖，材质什么都没画。现在画的和不开 Substrate 时一样：自发光，按不透明度。
- **Niagara 的精灵粒子在 PIE 里画得出来了。** 内置着色器用一个裸 floor 从打包的颜色倍率里取帧混合。这个值到像素时已经过插值，PIE 的屏幕空间视图里一个整数步可能差一点点落到它下面，精灵就整个混合到「下一帧」——单帧精灵的下一帧在贴图外面，默认的柔和圆点在那里是透明的。所以精灵在设计器和渲染目标里画得出来，游戏视口里一个都不画；条带不受影响。现在内置着色器和转换出来的粒子材质都按混合在下一步之下留出的间隙的一半来取整数步。
- **Niagara 效果示例排好了，火花也散开了。** 示例的行和卡片按标题文字量尺寸，五张卡只有一个词那么宽；现在分别要 1080×300 和 200×300。其中三个系统在同一个 Set Parameters 模块里读这个模块自己写的属性，Niagara 读到的是模块之前的值：点击火花和满条碎片的速度是零，边框火花全从一个角出发。现在每个系统都在更早的一组里写它要读的值。
- **NiagaraSystem 视觉的系统不会再在世界里画一帧了。** 模拟它的组件关了渲染，但 Niagara 只在组件 tick 时才把这个开关交给它的场景代理，系统激活或爆发时新建的代理会先画一帧：整个系统，亮着，画在 UI 后面的世界里——PIE 里每次爆发闪一下，在不 tick 组件的世界里则一直画着。现在组件根本不给场景建代理。

- **设计器里能看到指针光。** 跟随指针时，它在不在运行的世界里（没有指针）什么都不照；现在在那里停在 `LightPosition`。
- **细节面板里缺的行补上了。** 用户控件的 *Wants Tick*、*Allow Event Bubble Up* 和 *Can Navigate Here*；控件的提示框控件类；图片画刷的填充（数量、方式、起点、方向）和圆角框的圆角；矩形块九宫格的绘制方式和边距，以及它的形状融合方式；精灵在还没有贴图时的内边距和边框。这些以前只能在 `.dui` 里写。
- **打开序列帧播放器的细节面板不再让编辑器崩溃。** `UUISpriteSequencePlayer` 和 `UUISpriteSheetTexturePlayer` 的错误提示行在控件的组件里找它们需要的视觉，再从空指针上读名字。
- **子分类里的高级属性有行了。** 引擎画子分类（`DreamGUI|Pointer Light`）时不画它的高级行，开着「显示全部高级项」也一样，所以全息箔、指针光、倾斜卡的 *User Index* 和 *Pointer Id* 在哪个面板里都看不到。DreamGUI 的面板把这类属性作为子分类里的普通行，排在其它行后面。

<a id="100"></a>

## 1.0.0

包含 2.1.0 开发版的全部内容，外加 `.dui` 语言长成了能用来写界面的样子：只用 `.dui` 写的组件、`use … as`、`if`、`for`、`rows`、更短的布局写法；覆盖这一切的参考；以及最早两个展示界面发现的问题的修复。2.1.0 上的项目打开后不需要任何改动（见[升级](../guides/upgrading.md)里的“从 2.1 到 1.0.0”），只有仍靠重定向才能加载的资产例外：插件不再带任何重定向。

<a id="removed"></a>

### 移除

- **CoreRedirects。** `Config/DefaultDreamGUI.ini` 和它的 763 条重定向——来自 LGUI 和 LexUI 的、来自 prefab 词汇的、来自控件和事件改名的、来自模块拆分的——都没了，引擎不再对挂载这个插件的项目应用其中任何一条。插件自己的资产和测试夹具都写着现在的类型名。按旧名字保存的资产，要把 2.1.0 的那一段借进项目的 `Config/DefaultEngine.ini`，重存一次（见[升级](../guides/upgrading.md#from-21-to-100)）。守着这个重定向文件的四个 `DreamGUI.Packaging` 测试随它一起去掉了，`DreamGUI.Packaging.ThePluginShipsNoCoreRedirects` 则保证 `[CoreRedirects]` 段不会悄悄回来。
- **模块拆分之前保存的旧资产夹具。** 它们只靠重定向才能加载；测试宿主的 `DreamGUIFixtures` 现在由 1.0.0 保存，是之后的版本都要能继续加载的基线。
- **`Tools/ModuleSplit/generate_split_redirects.py`**，它负责把一次拆分的重定向追加进那个文件。现在它叫 `moved_types.py`：列出一次拆分挪走的类型，加 `--redirects` 时打印一段给项目自己的配置用的重定向，不往插件的配置里写任何东西。`retarget_script_paths.py` 从它拿类型名。

<a id="new-1"></a>

### 新增

- **`rows`：`.dui` 中的一张实例表。** `rows Row : ListRow (Label, Description) { "City Ruins", "…" … }` 只写一次类型、样式和属性名，然后每个实例一行——同一个组件出现 N 次、只差几个值，设置页或菜单项列表就是这样构成的。表被读成它的每一行所代表的普通未命名子节点，所以下游什么都没变。每一行按它的第一个值命名（`Page_0__Row_City_Ruins`），因此插入行或调整行的顺序不会动到其他行的 id 和本地化键。一行可以以一个块结尾，写这一行在列之外还需要的东西。设计器会把一列的值写回它的单元格，而一行写不出来的东西会被拒绝并说明原因（[DUI7004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7004)）。新增诊断码 [DUI2020](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2020) MalformedRows 和 [DUI3023](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3023) DuplicateRowKey（警告）。见 [`rows`](../language/rows.md)。
- **在 `.dui` 里用短名字引用组件。** `@Row Row1 { }` 是一个节点，它的类型由某个 `resources` 块里的 `Asset` 条目 `Row` 指定——本文件的，或者 `use` 带进来的——这样一族组件只在给它们定样式的库里命名一次，每个界面写 `@Row`，而不必每一行都写资产路径。条目缺失、不是 `Asset`、或者指向的不是用户控件，都会如实报出。
- **`use … as` 在 `.dui` 里。** `use "Components/Row.dui" as Row` 指名一个组件文件编译成的类——它的 `class` 行写的那个，或者把它当 Source File 的那个蓝图——`use /Game/UI/WBP_Row as Row` 则指名一个没有 `.dui` 的类；之后 `Row Row1 { }` 就是一个实例。对库写 `use "Lib.dui" as nier`，会把它的样式、资源和组件名放到 `nier.` 下面（`: nier.Label`、`@nier.Ink`、`nier.Row`），这样两个库可以并排使用；库里的 `use … as` 行会随库一起带过来。`@Row` 照样可用。见 [`use`](../language/use.md)。
- **只用 `.dui` 写的组件。** 一个文件声明它的类向宿主提供什么：`props { Text Label }` （蓝图变量，可带默认值，宿主可以设置它，也可以绑定它——`Label <- GetName()`——尽管它们没有 setter；C++ 父类上的同名属性会被直接使用）、`events { Picked(Number Index) }`（事件分发器，用 `OnClick -> emit Picked(Index)` 引发，宿主像路由任何事件一样路由它），以及带布局和样式、可以标为 `default` 的 slot——嵌套进来的内容会进入它，取代对 `GetDefaultSlotName` 的重写。宿主在实例里用 `slot Detail { … }` 填充一个命名 slot。见[组件](../language/components.md)。
- **`.dui` 里更短的布局写法。** 布局容器是一种节点类型（`VerticalBox Column { Spacing = 29 }`），没人引用的节点不需要 id（`HorizontalBox { … }`），slot 行可以归进 `@slot { … }`，`@fill` / `@fill 2` 代表最常写的那两种，样式可以携带 `+ Component` 块和 slot 行，于是一类列只需要一个名字。
- **`if` 和 `for` 在 `.dui` 里。** `if Cond { } else if … { } else { }` 每次只显示一个分支的控件（每个控件都绑定一个代表它所在分支的 `Shown`；隐藏的分支保留自己的状态），`Shown <- Expr` 是每个控件都有的、用是或否表示的可见性（`UDreamWidget::Shown`），`for Item in Items { … }` 在任何面板里为每一项复制一份模板，而 `each` 继续负责填充虚拟化的列表视图。见[控制流](../language/control-flow.md)。
- **语言参考。** `Docs/DuiLanguage.md`——在本站就是 [`.dui` 一节](../language/overview.md)—— 描述了 `.dui` 的全部内容：文件结构、类型、值、样式、`use`、组件、绑定、控制语句、时间轴，并附有每一个诊断码的表（[诊断码](../diagnostics/index.md)）。编辑器的符号导出（`DUI/.dui-symbols.json`）列出关键字、容器类型及其属性，供编辑器做补全。
- **设计器能写回新语法。** 一次编辑会落到持有那个值的行上，不管节点的类型是怎么写的；有 `@slot { … }` 块时写进块里；没有 id 的节点按为它生成的 id 定位。在设计器里新建的节点，用文件为它的类起的别名来写，或者写成它的容器类型；拖进组件命名 slot 的内容，写进那个 slot 的填充里。没有哪一行写着的东西，会说明而不是硬写：由 `if` 决定的可见性、承接不了这次改动的简写是 [DUI7004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7004)，来自样式组件的值是 [DUI7005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7005)。见[写回](../language/write-back.md)。
- **渲染变换补间。** `UDreamWidget::RenderTranslationTo`、`RenderOffsetTo`（在画布平面上，保留深度）、 `RenderScaleTo` 和 `RenderAngleTo` 移动、缩放、旋转的是画出来的东西，从不动布局：它们是给由面板摆放的控件用的补间，这类控件的锚定位置和尺寸会在面板下一次排布时被写回。
- **控件可以拒绝焦点环。** `UUISelectable::bUseFocusRing`（默认开启；运行时用 `SetUseFocusRing`）为那些自身外观已经能标示焦点的控件关掉项目的焦点环。焦点移到这样的控件上时，焦点环会被收起，而不是留在焦点离开的那个控件上；一个界面上所有控件都拒绝它时，焦点环根本不会被创建。`NavigationSelectionClass` 没有设置时，现在表示处处都没有焦点环，并且不会在每次焦点变化时报一条警告。
- **限制一帧推进补间的幅度。** `DreamTween.MaxStepSeconds`（控制台变量；0 即关闭，也是默认值）：比它更长的一帧——界面在加载资产、新文字的第一次绘制——只把世界驱动的补间推进这个上限，于是在这种卡顿之前刚开始的入场动画会从停下的地方接着播，而不是直接跳到结尾。手动推进从不受限制。

<a id="fixed-1"></a>

### 修复

- **焦点环贴合它所标示的控件。** 插件的焦点环把边框画在一个子节点上，这个子节点以固定的 100x100 制作在焦点环根节点的中心，所以无论是整行宽的列表行还是一个小图标，边框都是 100x100，只有看不见的根节点跟随了控件的尺寸。没有自己的 Blueprint 处理函数的焦点环类，现在会把每个覆盖其根节点的子节点（中心锚定、不小于根节点）改为随根节点拉伸，保留制作时的边距；更小的或者锚在角上的子节点保持自己的尺寸。
- **刚出现的焦点环不再停在透明状态。** 移动焦点环会杀掉它正在运行的补间，其中包括刚刚显示的焦点环的淡入，于是四分之一秒内的第二次按键——或者同一次焦点先以选中、再以导航进入的形式到达——会让焦点环停在淡入已经到达的透明度上。现在移动会先把淡入完成。
- **代码移动焦点后会离开原来的控件。** `SetFocus` 会随焦点一起移动导航光标，但被离开的控件直到下一次导航步进才听到“退出”，于是它在拿到焦点的控件旁边继续把自己画成 Focused——对 `GetCurrentSelectionState` 也回答 Focused——无论是界面打开子列表或对话框，还是它被隐藏再显示。现在一次取消选中就结束导航对它的持有。
- **在经由材质绘制的图片之后，内置着色器画的东西重新由内置着色器绘制。** 在屏幕画布或渲染目标画布上，经由材质的绘制绑定了材质的管线，却让这一遍的内置管线缓存仍然以为内置管线还绑定着，于是接下来的文字或块用材质的着色器去画：材质绘制的图片之后的文字变成空白或方块，RHI 还会因为给一个没有绑定的顶点着色器设置参数而触发 ensure。世界空间画布本来就会重新设置内置管线，现在屏幕画布也会。
- **画布参数里只用到渲染器标志的笔刷材质，会被告知渲染器。** 画布只向材质询问它的贴图参数，所以 `DreamUI_IsRenderByDreamUIRenderer` 这个标量参数从来不算数：一个按它做预乘的程序化材质被原样绘制、标志停在默认值，于是又被 DreamGUI 的渲染器预乘了第二次。
- **交给图片的材质实例会显示它的拥有者在它上面设置的参数。** 画布通过自己的一个代理来绘制材质，这个代理缓存了材质的 uniform 表达式，而且只在 DreamGUI 自己的参数变化时才重建缓存：在首次绘制之后对实例调用 `SetScalarParameterValue`，结果永远到不了屏幕上，于是动画材质一动不动。现在只要实例自己的渲染代理重建过，这个代理就在收集绘制时重建自己的缓存。

<a id="210-development-build"></a>

## 2.1.0（开发版）

2.0 之后的五轮工程：把一帧的开销压到能承载成千上万个动画控件，审查了每一个控件和编辑器路径，把文字与 Chrome 和 Slate 对照测量、在有差异的地方重写，渐变文字，键盘和手柄导航，以及发布所需要的一切。`DreamGUI.uplugin` 的版本号是 3。2.1 保存的资产无法在 2.0 中打开。

<a id="new-2"></a>

### 新增

- **渐变文字。** 文字的字面、描边和覆盖在字面上的叠加层，都可以各自用渐变来绘制：线性、径向、锥形、菱形或四角渐变，最多 16 个色标，填充、重复或反射，在 sRGB、线性光或 Oklab 中混合，按整个文本块、内容框、每一行、每一个字形或每一段富文本来度量。渐变按 CSS 的写法书写（`linear-gradient(180deg, #FFF3B0, #E8B64A 55%, #9C6A12)`）——在细节面板里、在 `.dui` 里，或者作为一段富文本（`<gradient=Name>...</gradient>`，名字可以是自定义样式、项目预设或 CSS 本身）。预设是 `UDreamGradientAsset` 资产，或者项目设置里的 CSS 字符串（`GradientPresets`）；使用同一个渐变的文字在 GPU 上共享它的那一行。绘制的相位、角度、中心和缩放可以做动画，而不需要重新排版或重新绘制文字——可以在 Sequencer 里打关键帧（`FacePaintPhase`、`OutlinePaintPhase`、`OverlayPaintPhase`、`PaintAngleOffset`），也可以用 `UDreamTextPaintLibrary` 做补间（`PaintPhaseTo`、`PaintAngleTo`、`PlayShimmer`）。不通过 `MF_DreamUI_Shade` 着色的材质改为按顶点色绘制。
- **编辑器里的渐变。** 渐变属性显示一条可以拖动色标的渐变条、一个带复制和粘贴的 CSS 框，以及一个预设菜单：列出项目的预设和渐变资产，并能把当前渐变保存为其中任一种。渐变资产有缩略图和 *Copy as CSS*。文字的绘制字段在细节面板里归为一个 Paint 组。
- **Tab 导航。** 在没有按住 Ctrl、Alt 或 Cmd 时，Tab 和 Shift+Tab 按层级顺序移动焦点，兄弟节点按新的 `TabIndex` 排序；`bIsTabStop` 和 `TabNavigation`（Continue、Cycle、Contained、Once、None）决定遍历的方式；列表、平铺视图和树是一个停靠点，从它们选中的那一行进入；滚出视野的停靠点会被滚动到视野内。模态框、带遮罩的对话框和会循环的弹出层把 Tab 留在自己内部，其他玩家的弹出层永远不是停靠点。Tab 会提交文本框的编辑并继续前进，通过 Tab 到达文本框则开始编辑；下拉框在 Tab 时提交高亮的那一行，菜单则关闭它的整条链。当玩家拥有 DreamGUI 的焦点或 Tab 停靠点时，Slate 自己从裸游戏视口出发的导航——Tab、方向键——永远不会把焦点移进 UMG。`TabOrder = LegacyGeometric` 在这个版本里保留 2.0 的几何顺序。
- **手柄与输入模式。** 导航作用域说明它的界面接收菜单输入还是游戏输入（`InputMode`：All、Menu、Game；Game 会为那个玩家关闭内置的导航、确认和返回）。按键和手柄作用于焦点：Enter、空格和手柄的确认键按下的是有焦点的控件，从不是悬停的控件，没有焦点时什么也不按。焦点外观只在焦点是由按键或手柄移动时才显示，就像 CSS 的 `:focus-visible`（`bFocusVisibleOnlyFromKeys`）；在 DreamGUI 的纯 UI 输入模式下，使用手柄时光标会隐藏（`bHideCursorOnGamepad`）。确认、返回、方向、翻页、首尾和切换标签页的按键都是项目设置（`ConfirmKeys`、`BackKeys`、`DirectionKeys`、`PageKeys`、`ExtentKeys`、`PreviousTabKeys`、`NextTabKeys`），设置变化时会重新读取；平台的确认和返回按钮在运行时读取（`bUsePlatformAcceptBack`）；扳机键翻页，肩键切换当前标签视图的标签页，并在操作栏里显示提示。分屏：作用域、动作触发器和操作栏属于它们所在控件的拥有者玩家，除非另外指定了索引。
- **文字**，与 Chrome 和 Slate 对照测量并向它们看齐：字素簇和双向文本层级，带连字和 CSS 行框；按 Unicode 范围、文化和呈现方式选择的回退字体，带真正的粗体和斜体字体；彩色 emoji（CBDT/CBLC、sbix、COLRv0），它们的阴影、描边和长阴影副本是剪影；两端对齐、制表位和中间省略；长文本只在编辑触及的地方重新排版；按游戏的文化断行；富文本的 `<lang=xx>` 标签；富文本里的图片有尺寸和对齐。
- **小字号文字**——不超过 `SmallTextMaxPixelSize`，默认 20 px——用对齐到设备像素网格的、经过 hinting 的覆盖率字形绘制，和 Slate 一样清晰。带描边、发光或衬底的文字，字面这样绘制，效果从距离场绘制（`SmallTextEffectFace`）；渲染层里的文字在该层静止 3 帧之后也这样绘制。只要还有文字在用某个字体的覆盖率字形，这个字体就保留它们在图集里的格子；超过 `MaxCoverageCells` 的两倍后，新的字形要排队，期间从距离场绘制，每个字体只报一条警告。
- **默认字体**改为 Slate 的：Roboto，带真正的常规、粗体、斜体和粗斜体字体，CJK 用 DroidSansFallback，使用描边（多通道）距离场。
- **面板**追上 UMG：滚动框的填充 slot 和首尾内边距、换行框的填充、缩放框的模式和指定缩放、网格的跨行跨列、拖动开关、边框的着色。
- **弹出层**：每个玩家一个弹出层，取代了点击拦截器，把焦点还给打开弹出层的那一方，Back 只关闭最上面的一个。弹出层会说明 Tab 在它里面做什么（`TabBehavior`：Cycle，下拉列表和菜单则是 CloseAndContinue）。在打开的弹出层上显示的模态框会关闭它，并接住第一次点击和第一次 Back；在一个画在打开的弹出层前面的层上按下，会关闭弹出层并且仍然传到那个层，Back 也先交给那个层。被自己的“打开时获取焦点”关掉的弹出层最终保持关闭，抬起的面板菜单保留它的 slot 的内边距、微调和尺寸上下限。
- **`DreamGUI.Memory`**，一个控制台命令：每个字体的字形图集（切片、GPU 字节数和 CPU 副本、格子、字形、字体数据字节数）、精灵图集页，以及每个世界的画布网格段和绘制行；`DreamGUI.Memory Json` 以 JSON 输出，`File=<path>` 写入文件。
- **按需检查**：`r.DreamUI.VerifyKeptPointers` 重新查找 DreamGUI 在帧与帧之间保存的每一个对象——控件的画布、画布的渲染层、 UI 管理器的画布、动画属性所属的对象——并指出两者不一致的地方，就像 `r.DreamUI.VerifyPartialPrepare` 对画布的 prepare 做的那样。
- **用来测量的开关。** `DreamGUI.Text.SmallTextCoverage` 和 `DreamGUI.Text.SmallTextMaxPixelSize` 为所有字体覆盖小字号设置，改动后重绘所有文字。`DreamGUI.Text.SmallTextOnMove`、 `DreamGUI.Scroll.SnapToDevicePixels` 和 `bFieldTextCorrection` 设置尝试用其他方式绘制移动中、滚动中和大号的文字，在测量之前保持关闭。 `DreamGUI.Text.IncrementalParse`、`IncrementalMeasure` 和 `InPlaceDisplayList` 各自关闭增量排版的一部分，`DreamGUI.Text.VerifyIncremental` 则让它只用于从零开始的排版。

<a id="performance"></a>

### 性能

- 渲染变换不断变化的控件会变成**渲染层**，由它们的画布在 GPU 上移动，任意多个渲染层可以共享一次绘制调用。
- 世界变换在被读取时才合成，移动每帧只通知一次。
- 只由普通属性轨道组成的控件动画，由 DreamGUI 自己的播放器播放，而不是 Sequencer 的实体系统。
- 世界空间画布在共享的渲染图 pass 里绘制，每个面板的工作移到了工作线程上。两个 5000 个按钮的基准关卡都跑在 60 FPS 的上限。
- 画布只遍历请求变化的控件，就地修补几何发生变化的网格段，控件只是移动时保留原有的绘制调用。
- 在长段落里的一次编辑只读取和测量它周围的一个窗口，文字的显示列表在原处修改，而不是重新写一遍。
- 一个世界每帧最多把 `SmallTextRepaintBudgetPerFrame`（512）段文字重绘到覆盖率字形上；一段在旋转、缩放或移动时从距离场绘制的小字号文字，会在它静止之后再被检查一次，而不是每次移动都检查。
- 增量排版让每一条退回路径都保持精确：在一个以标点、数字或 emoji 开头的长段落的开头附近编辑，或者紧跟在内联图片之后编辑，得到的字形排布与整段一起排布时完全一样。
- 在编辑器里，DreamGUI 自带的 ICU 副本会拿到引擎的 ICU 数据，所以那里的断行也按游戏的文化进行，和打包后的游戏一样；没有这些数据时，它退回到引擎的迭代器，并且只提示一次。

<a id="fixed-2"></a>

### 修复

- **距离场文字按预乘的方式合成各层**：距离场字形的衬底、发光、描边和字面被当作非预乘颜色混合，所以在什么都没有的背景上，一层的颜色会被拉向黑色，最终的透明度变成了它的平方。半透明或正在淡出的文字会变暗，渐变的透明色标会发灰，发光衰减得太快，浅色文字的边缘比 Slate 在视口里画得更细。现在每一层都叠加在它下面那一层之上。
- **游戏中的视口截取**：`UDreamUICaptureLibrary::SaveViewportToPng` 和 `ReadViewportPixels` 读取游戏的视口，而游戏视口是直接画进窗口的，帧与帧之间没有贴图：这会触发 ensure 并得到一张黑图。现在它们在这种情况下返回 false，并提示改用引擎的截图请求。
- **打包游戏中的中文回退**：游戏烘焙时使用的 ICU 数据（English 或 EFIGSCJK）没有 likely subtags，所以引擎把 `zh-CN` 展开成 `zh-CN, zh`，没有 `zh-Hans`；为 `"zh-Hans"` 或 `"zh-Hant"` 写的回退条目在编辑器里能匹配上，在包里却永远匹配不上。DreamGUI 现在会为中文文化名补回书写系统。
- **渲染线程上的崩溃**：渲染器在它的渲染命令还在排队时就被释放（那个时有时无的 all-preset 崩溃）；渲染根和渲染图 pass 原先对可能先被销毁的对象持有裸指针，现在改为按引用持有。
- **扩展**：TextAnimation 的属性会跳过没有顶点的字符，而不是越界读取；画布没有渲染目标、或者自身没有顶点的圆柱渲染目标表面、在控件离开画布之后才设置的静态网格、子节点数量发生变化的 2D 线条，以及发往一个已被替换的画布的渲染目标输入，都不再崩溃或者送错地方；圆柱的弧度为 0 时按 1 度处理，负弧度的包围盒在它弯曲的那一侧。保留框（retainer box）真正做到了保留：它把自己的画布设为渲染目标模式，用完后恢复，不碰编辑器世界，没有 `DisplayVisual` 时只警告一次。静态网格视觉对象会拒绝超出顶点预算的网格并报错，而不是截断它的索引，它的包围盒也就在画布绘制它的地方。StaticMesh 模式下的渲染目标表面（`UDreamUIRenderTargetGeometrySource`）只在运行中才把它的材质放进静态网格的 0 号槽：在编辑器里，它放在那里的瞬态实例会让保存关卡时把网格自己的材质写成 null。Pixelate 把它的目标交给它的代理；pixel sort 把 pass 数限制在 1 到 512 之间；歌词的 TTML 读取器在 256 层嵌套处停止；运行时设置的 TextAnimation 属性会开始生效，被它们替换掉的那些会停止。
- **DreamTween**：序列在回收器带走它的子项之后依然存活；重启或跳转自身序列的回调不再陷入循环，嵌套 16 层的跳转会被拒绝并报错；`Kill` 只触发一次回调；已完成和被杀掉的补间会退役，它们的句柄读作无效——`Kill`、`ForceComplete` 和 `Goto` 对已被杀掉或已退役的补间不做任何事，`Restart` 对已退役补间只会警告，在补间自己的 `OnComplete` 里调用 `Restart` 会让它继续运行；对尚未开始的补间调用 `ForceComplete` 会先启动它；`Goto` 到终点只完成一次，`Goto(0)` 会应用起始值；空曲线回退为线性并给出警告，而不是卡住；颜色和透明度补间在越界的缓动下取整并钳制，而不是回绕；InExpo 精确地停在 1；反向的 yoyo 循环停在 0；静止的弹簧会在 `SetTarget` 时醒来；次数为 0 的 `RepeatCall` 什么也不调用；时间缩放、延迟和增量都会被检查；第二个 tick 辅助对象不再让补间速度翻倍；拥有者已经不在的补间会被直接丢弃，不触发回调；布局动画和嵌入的 UMG 控件遵循与其他控件补间相同的暂停和时间膨胀规则。
- **控件**：一次审查发现的大约四十个问题，每个都配了测试——条目变化期间的列表和滚动视图行、文本框缓冲区之前的一次堆写入、文本框缓冲区之前的堆写入、持有裸指针的下拉框、按下时被关闭的按钮和选择、源对象被销毁的拖拽、按键选择器、在 IME 下按 UTF-16 偏移编辑的文本框。
- **文字**：打包后的文字样式值如果产生了非规格化浮点数——一对值中第一个为零，比如面柔度为 0 旁边是膨胀，或者效果透明度恰好是 128——会被微调到最近的规格化值，这样会把非规格化数刷成 0 的 GPU 不再把它们变成 0；长到 `FName` 装不下（1024 个字符或更多）的富文本标签名会作为文字显示，而不是触发断言。
- **渲染**：如果加上阴影副本会让网格超出顶点预算，网格会不带阴影绘制并给出一条警告，而不是完全不画。
- **字体**：位图字体在撤销和重做时重新加载；嵌入的字体文件在烘焙时会重新读取它的字节，空文件是一个烘焙错误；字体的图集贴图和它的文字列表不再被记录进撤销事务。
- **输入**：没有对应玩家的玩家索引不再被分配一个输入用户。
- **控件蓝图在未烘焙内容上运行游戏时会执行它们的图表**（`-game`，Standalone）：`DreamGUIEditor` 为 UncookedOnly。
- **编辑器**：设计器里的一次手势是一个撤销步骤；设计器把编辑写进资产指向的那个 `.dui`，并且从不覆盖在磁盘上被改过的文件；重新编译以及设计器自己重建预览，不再记录进一个打开着的事务。

<a id="platforms-and-servers"></a>

### 平台与服务器

- 只有 Win64 被构建和运行过：编辑器和自动化测试套件，以及在发布门运行过测试宿主的打包文字冒烟测试之后的 Development 打包游戏。Mac、Linux、iOS 和 Android 列在插件描述文件里，但从未被编译过（README 的 Platforms 一节如今这样说明，而不是写“Builds: yes”；见[平台](../guides/platforms.md)）。
- UI 管理器不再在专用服务器的世界里启动（包括编辑器内运行的服务器）：没有画布，没有渲染器，没有绘制行。

<a id="documentation-and-tools"></a>

### 文档与工具

- `Docs/FontsAndPackaging.md`（[字体与打包](../guides/fonts-and-packaging.md)）：彩色 emoji 字体、打包字体文件的三种方式、ICU 打包预设，以及没有它时会退化的东西。
- `Docs/Migration.md` 新增了从 2.0 迁到 2.1 的一节（[升级](../guides/upgrading.md)）。
- msdfgen 的 MIT 许可证随插件一起发布，放在它的源码旁边。
- 测试宿主有一个打包文字冒烟测试，是发布门的一部分：它的游戏模块里的一个探针记录打包游戏画出了什么，`Tools/Tests/compare_text_smoke.py` 把它与在未烘焙内容上运行的同一构建对照。
- `Tools/ModuleSplit/check_game_includes.py` 还能找出：文件用到了却没有包含其定义的引擎宏和全局变量、游戏会编译的代码里的编辑器头文件，以及依赖编辑器模块的运行时 `Build.cs` 文件。
- 渲染基准测试在七种运动下分别测量开启和关闭覆盖率时的小字号文字；文字对照运行测量笔画边缘、线性光下的墨量以及相对 Slate 的灰度，并把渐变填充与 Chrome 的 `background-clip: text` 并排报告。
- `Tools/Bench` 存放基准测试和读取它们性能数据的脚本（[基准测试](../tools/bench.md)）；测试运行器有预设、一个夜间脚本，以及一张说明每个控件用哪种输入测试过的覆盖表（[测试](../tools/tests.md)）。

<a id="changes-to-know-about"></a>

### 需要留意的变化

从 2.0 升级会改变一些默认值和行为：默认字体（文字会重新排版）、小字号文字的渲染、Tab、按键和手柄作用于焦点而不是悬停、焦点外观、玩家索引、滚动条和列表作为 Tab 停靠点、淡出的描边文字的效果透明度、文本框文字合成各层的方式、MSAA 混合、按游戏文化断行，以及对使用自定义着色器或网格修改器的人来说，带有新 `UV4` 通道的 64 字节顶点。它们都列在[升级](../guides/upgrading.md)的“从 2.0 到 2.1”里。

<a id="200-development-build"></a>

## 2.0.0（开发版）

模块化的版本（2026-09-29）。

- **模块。** 单一的运行时模块按层拆开：`DreamGUIRenderer`（视图扩展、着色器和代理）、`DreamGUI`（控件、视觉对象、画布、布局、文字、`.dui`）、`DreamGUIInput`（事件系统、射线检测器、导航、视口客户端）、`DreamGUIControls`、`DreamGUIExtensions`、`DreamGUISamples` 和 `DreamTween`，其中 `DreamGUIEditor` 和 `DreamGUIK2Nodes` 为 uncooked-only，`DreamGUITests` 仅限编辑器。较低的层从不包含较高的层。
- **重定向随插件一起发布**：`Config/DefaultDreamGUI.ini` 带有全部重定向——来自 LGUI 和 LexUI 的、来自 prefab 词汇的、来自控件改名的、来自模块拆分的（共 763 条）。
- **按玩家区分的输入**：每个本地玩家有自己的指针、焦点和文字输入目标；指针 id 有各自的范围（鼠标 0，手指 100 起，脚本 1000 起）；一个可选的 Slate 输入源在每种输入模式下都能收到输入；字符通过游戏视口客户端送达文本框。
- **渲染**：画布不创建材质实例，通过渲染线程代理回答它的材质的参数；渲染目标画布由它自己的一个渲染命令绘制；屏幕效果通过渲染图贴图上的同一条路径读写屏幕。
- **控件蓝图** 取代了 prefab 资产模型；设计器参照 UMG 的重建；控件的层级可以用文本（`.dui`）编写，带时间轴、路由和样式。
- **布局**沿着 Blink 和 Yoga 的思路实现，配有 UMG 形状的面板；Lex 布局族被移除。
- **移除**：根蓝图、`UDreamWidgetPresenterComponent`、世界空间射线检测源一族，以及那些恢复旧渲染器行为的设置和控制台变量。见[迁移](../guides/migration.md)里的“已经不存在的”一节。
