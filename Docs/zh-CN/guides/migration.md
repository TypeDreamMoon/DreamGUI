---
title: 从 LGUI、LexUI 或早期构建迁移
description: 把按 LGUI / LexUI 保存的资产、早期单模块 1.x 构建上的项目、以及针对插件写的 C++ 迁到 DreamGUI：动手之前、安装、从 2.1.0 借来的 redirect 带着什么、已不存在的东西、要检查的行为、C++ 的改动，以及迁不过来时怎么办。
---

这一页给三种项目：

- 有**按 LGUI 或 LexUI 保存的资产** —— 也就是这个分叉起步的上游；
- 在**本分叉的早期构建**上 —— 单模块的 1.x 构建（不是 1.0.0），在模块拆分、输入重做和渲染器重做之前；
- 有**自己针对插件写的 C++** —— 子类、自定义 visual、自己的视口客户端。

下面七节把这样的项目带到现在的结构上。走完之后，再读[从开发版升级](upgrading.md)：2.0 → 2.1 → 1.0.0
那几步的默认值和行为变化，对从更早版本迁过来的项目同样适用。这里说的一切都是为了**保住你已有的东西**；
从零开始见[安装](../getting-started/installation.md)。

<a id="1-打开项目之前"></a>

## 1. 打开项目之前

1. **引擎 5.8。** 插件只针对 5.8 构建，没有 5.7 的分支。
2. **先把项目放进版本控制**，或者复制一份。用新插件打开项目本身无害 —— redirect 改的是内存里的名字，不是磁盘上的 ——
   但一个资产第一次保存时会把新名字写进去，从那次保存再也回不到旧插件。
3. **把旧插件拿掉。** DreamGUI 放进来之前，`Plugins/` 下的 LGUI 或 LexUI 文件夹必须先移走：它的类会以 redirect 要映射的
   那些**旧**名字活着，而一个仍然解析得到的名字，redirect 永远不会作用于它。
4. **把 redirect 段放进你的 `Config/DefaultEngine.ini`。** 1.0.0 不带任何 redirect（见
   [从 2.1 到 1.0.0](upgrading.md#从-21-到-100)）：把 [2.1.0 的 `Config/DefaultDreamGUI.ini`](https://github.com/TypeDreamMoon/DreamGUI/blob/3049561a1742ab59d83a404ee267d2f580e53589/Config/DefaultDreamGUI.ini)（提交 `3049561a`）里的
   `[CoreRedirects]` 段拷到那里，拷一次就够。那里如果已经有一份更旧的拷贝，先删掉它 —— 同一个旧名字对应两个不同新名字的
   redirect 是错误，先注册的赢。

<a id="2-安装"></a>

## 2. 安装

克隆进项目的 `Plugins/`，重新生成工程文件，编译：

```bash
git clone https://github.com/TypeDreamMoon/DreamGUI.git Plugins/DreamGUI
```

插件需要 `EnhancedInput`，它会自己启用。对项目没有别的要求：不需要源码引擎，不需要引擎私有头文件，没有要拷贝的设置。

<a id="3-redirect-带着什么"></a>

## 3. redirect 带着什么

2.1.0 的这一段放进项目的 `Config/DefaultEngine.ini` 之后，引擎在加载第一个包之前应用它。它一共 763 条：

| 种类 | 条数 | 覆盖什么 |
| --- | --- | --- |
| Package | 4 | `/LGUI/` 内容挂载点到 `/DreamGUI/`，以及 `/Script/LGUI`、`/Script/LGUIEditor`、`/Script/LTween` 三个模块到它们的 DreamGUI 名字 |
| Class | 395 | 每个 LGUI/LexUI 类；被类模型取代的 prefab 词汇；控件改名（`UIButtonComponent` → `UIButton` 和它的十三个兄弟）；模块拆分时从核心挪走的每个类，从旧的 `/Script/DreamGUI` 名字到它的新模块 |
| Struct、Enum | 96、124 | 同上，针对数据类型 |
| Function | 74 | 蓝图调用或重写、被改名或挪走的函数 —— `UDreamUIBehaviour` 的 `Update` → `Tick`，`UDreamUINavigationScope` 去掉了 `On` 的那些事件 |
| Object | 70 | 随类一起被模块拆分挪走的委托签名 |

**然后重存，再把这一段拿掉。** 每次加载一个需要 redirect 的资产时，redirect 都会被应用一遍。项目打开、UI 看起来对了之后，把用到 DreamGUI 的资产重存一遍
（打开它们后 *File ▸ Save All*，或者对你的内容跑 `ResavePackages` commandlet），让它们自己写上现在的类型名，然后从 `Config/DefaultEngine.ini`
删掉这一段：之后再没有东西需要它，1.0.0 自己也不留一份。

<a id="4-已经不存在的"></a>

## 4. 已经不存在的

这些没有 redirect，因为已经没有东西可指了。还引用着其中之一的资产会在加载时丢掉那一部分，引擎打一条警告点名它。

- **根蓝图** `WorldSpaceRoot_DreamRenderer`、`WorldSpaceRoot_UERenderer`、`ScreenSpaceRoot` 和
  `DreamWorldSpaceRaycasterSource_Mouse`。放着其中之一的关卡会丢掉那个 Actor —— 它的类不再解析，整个导出被丢弃，并打一条警告。
  没有残留需要修：重新把控件蓝图拖进关卡即可，作为你自己 Actor 上的一个 `UDreamWorldWidgetComponent`，或者通过关卡编辑器的拖放。
- **`UDreamWidgetPresenterComponent`。** 抽象基类 `UDreamWidgetPresenterComponentBase` 还在；现在要放的是 `UDreamWorldWidgetComponent`。
- **`UDreamWorldSpaceRaycasterBase` / `ForWorldTrigger` / `Source` 一族** —— `UDreamWorldSpaceRaycaster` 吸收了它们的全部。
- **插件设置** `ScreenSpaceRootClass`、`WorldSpaceRootClass`、`WorldSpaceUERendererRootClass`、`WorldSpaceRaycasterSourceClass`，
  以及 `bLegacyTouchPointerIds`（见第 5 节）。
- **Lex 布局一族** —— `ULexLayoutContainerFlexBox`、`ULexLayoutContainerGrid`、`ULexLayoutSelfFlexBox`、`ULexLayoutSelfGrid`
  和 `ELexUILayoutMode` 开关。UMG 形状的面板取代了它们；用它们排布的树要手工重排。
- **prefab 机制**及其类型，在上面这些之前就被控件类模型取代了，连同 `SavePrefab`、`Apply`、`ClearLoadedPrefab` 和 *Save on Apply*。
- **两个找回旧渲染器行为的控制台变量** —— `r.DreamUI.MaterialWrappers` 和 `r.DreamUI.RTDrawer` —— 以及它们找回的那些行为（见第 5 节）。

<a id="5-要检查的行为"></a>

## 5. 要检查的行为

什么都没改名，资产的表现也可能和以前不同。跑过早期版本的项目都值得看一眼：

<a id="输入"></a>

### 输入

- **指针 id 有了区间。** 鼠标是指针 0，手指是 100 加它的序号，脚本造出来的 id 从 1000 开始。以前一根手指就是它自己的序号，
  于是第一根手指和鼠标是同一个指针，抢悬停和选中。按手指序号读触摸指针的代码改问 `DreamUIPointerIds::ForTouch`；找回旧 id 的那个设置没有了。
- **Enhanced Input 预设按原样绑定它拿到的动作。** 以前它玩的是这些动作的运行时副本，每帧改写副本的 `bTriggerWhenPaused`。
  现在暂停按事件判断：游戏暂停且 `UDreamUISettings::bScreenSpaceUIAffectByGamePause` 开着时，预设丢掉到达的输入。要让暂停那一帧的点击到得了，
  它的动作必须在暂停时也触发 —— 插件自带的 `IA_*` 都是；预设绑定的你自己的动作要设 `bTriggerWhenPaused`。
- **焦点和文本输入属于某个玩家。** 分屏的第二个玩家自己聚焦、自己打字。`UUITextInput::GetActiveTextInputForPlayer` 按玩家回答；
  `GetActiveTextInput` 跨世界回答。
- **字符通过游戏视口客户端到达文本框。** 用 `UDreamGameViewportClient`，或者在你自己视口客户端的 `InputChar` 里、控制台之后基类之前
  调用 `DreamUITextInputRouter::RouteViewportCharacter`（见[安装](../getting-started/installation.md)）。
- **Slate 输入源是可选的，默认关。** *Project Settings ▸ Plugins ▸ Dream GUI ▸ Input ▸ Use Slate Input Source* 在视口之前听每一个
  指针、按键和摇杆，让 UI 在引擎自己的 UI-only 输入模式下也能工作；开着时预设 Actor 让位，`SlateInputConsumePolicy` 决定它从游戏那里扣下什么。

<a id="渲染"></a>

### 渲染

- **画布不再自己造材质实例。** 它给材质的参数 —— 主纹理、字体纹理、它的数据纹理 —— 通过这个材质在渲染线程上的一个代理来回答。
  你交给控件的材质实例也这样回答，而且从不被写入，所以你在它上面设的参数始终是你的。画布绘制用的东西没有一个是关卡里的对象，
  也不会被拷进一次 play session 或一次粘贴。
- **渲染目标画布不管有没有东西在渲染它的世界，都会绘制。** 它的分段改变后，由自己的 render command 和一张自己的渲染图绘制，
  而不是在它世界的某个视图里。
- **背景模糊、像素化和像素排序**看起来和以前一样，修了两处：多重采样画布上的全尺寸模糊现在能显示（以前被画布的 resolve 吞掉了），
  输出到目标的全尺寸模糊现在模糊的是屏幕（以前模糊的是一张空纹理）。

<a id="看清楚"></a>

### 看清楚

`DreamUI.Capture` 把视口和每块渲染目标画布写成 PNG，`DreamUI.Stats` 逐阶段打印 UI 的开销 —— 足够拿迁移后的界面和以前对比。
有东西画错或没画出来，开着 `r.DreamUI.VerifyPartialPrepare 1` 再跑，把它触发的 ensure 附进报告。见[调试](../tools/debugging.md)。

<a id="6-你自己的-c"></a>

## 6. 你自己的 C++

<a id="模块"></a>

### 模块

运行时现在是几个模块。你用到哪些模块的类型，就把哪些加进 `Build.cs`：`DreamGUIRenderer`（绘制、效果的渲染代理）、`DreamGUI`
（控件、visual、画布）、`DreamGUIInput`（事件系统、射线检测器、导航、视口客户端）、`DreamGUIControls`（`Dream*` 控件和 `UI*` 行为）、
`DreamGUIExtensions`（线、环、效果、渲染目标辅助）。什么在哪里，见[项目里的布局](../getting-started/project-layout.md)。

<a id="include"></a>

### include

除下表这些之外，每个头文件都保留了原来的路径：

| 原来的 include | 现在 |
| --- | --- |
| `Core/DreamUIRender/*`、`Core/DreamUIMesh/DreamUIGizmoMesh.h`、`Core/DreamUIMeshVertex.h`、`Core/DreamUIMeshIndex.h`、`Core/DreamUIBlendMode.h`、`Core/DreamVisualPostProcessRenderProxy.h` | `DreamUIRender/` 加同一个文件名 |
| `Extensions/DreamGameViewportClient.h` | `Event/DreamGameViewportClient.h` |
| `Extensions/DreamUMGWidget.h`、`Extensions/DreamUMGWidgetInteraction.h` | `UMG/` 加同一个文件名 |
| `Core/DreamUIEachAdapter.h` | `Binding/DreamUIEachAdapter.h` |
| `Core/Components/DreamBackgroundBlur.h`、`Core/Components/DreamBackgroundPixelate.h`、`Core/Components/DreamPixelSort.h` | `Extensions/Effects/` 加同一个文件名 |

<a id="调用"></a>

### 调用

随模块拆分挪走的：

| 原来的调用 | 现在 |
| --- | --- |
| `UUITextInput::RouteCharacterInputToActiveInput` | `DreamUITextInputRouter::RouteViewportCharacter`，在基类之前调用（见[安装](../getting-started/installation.md)） |
| `UDreamCanvas::CalculateRenderScaledSize` | `FDreamUIRenderer::CalculateRenderScaledSize` |
| `DreamPixelSort::ResolveRegionSize` | `DreamUIPostProcessEffects::ResolvePixelSortRegionSize`（`DreamUIRender/DreamUIPostProcessEffects.h`） |
| `UDreamUIManagerWorldSubsystem` 的事件系统注册表和玩家交互：`GetEventSystemByUserIndex`、`GetMapUserIndexToEventSystem`、`AddEventSystem`、`RemoveEventSystem`、`EnsureInteractionForPlayer`、`GetInteractionHost` | `UDreamUIInputSubsystem`（`Event/DreamUIInputSubsystem.h`）上的同名函数；`UDreamUIInputSubsystem::Get(WorldContext)` 找到它 |
| `UDreamUIManagerWorldSubsystem::AddSelectable`、`RemoveSelectable` 和 `GetAllSelectableArray`，配 `UUISelectable` | 同名，改配 `UDreamUIBehaviour` |
| `UDreamGUISettings::DefaultStyleSheet`，类型是 `UDreamUIStyleSheet` | 一个 `TSoftObjectPtr<UDataAsset>`；`UDreamUIStyleSheet::GetProjectSheet()` 负责转换 |

拆分之后又改的 —— 一个 visual 重写的材质回调，以及随旧代码路径一起走的输入和渲染器调用：

| 原来 | 现在 |
| --- | --- |
| `UDreamVisual::OnMaterialInstanceDynamicCreated(UMaterialInstanceDynamic*)` | `UDreamVisual::AddMaterialParameters(FDreamUIMaterialParameters&)`：给出你的 visual 的材质需要的参数，画布替它的代理回答 |
| `DreamUITextInputRouter::RouteCharacter(TCHAR)` | 视口客户端里用 `RouteViewportCharacter`，或 `RouteCharacter(WorldContext, UserIndex, Character)` |
| `UDreamGUISettings::bLegacyTouchPointerIds` | 没了；`DreamUIPointerIds::ForTouch` 给出一根手指的指针 id |
| `ADreamEnhancedInputEventSystemActor::GetOriginalAction` | 没了：Actor 持有的动作就是它拿到的那些 |
| `FDreamUIRenderer::IsRenderTargetDrawerEnabled` | 没了：渲染目标画布总是由 `DrawRenderTarget_GameThread` 绘制 |
| `UDreamUIMeshComponent::OnSceneProxyCreated` | `OnRenderRootCreated`，带着网格的 render root 广播。root 持有画布的分段，比为它创建的 scene proxy 活得长 |
| `FDreamVisualPostProcessRenderProxy::RenderMeshOnScreen_RenderThread` 接受 RHI 纹理 | 改接受渲染图的纹理（`FRDGTextureRef`）。你自己的效果用 `ReadScreen_RenderThread` 和 `GrabRegion_RenderThread` 读屏幕，在 `CreateWorkTexture` 给的纹理里干活，用 `WriteBack_RenderThread` 写回 |

<a id="代码里的设置"></a>

### 代码里的设置

`UDreamGUISettings` 是 *Project Settings ▸ Plugins ▸ Dream GUI*；`UDreamUISettings` 是 *Project Settings ▸ Plugins ▸ DreamUI*。
两者都在用到的地方读，所以运行时改一个值，从下一帧起生效。

<a id="7-有东西没过来时"></a>

## 7. 有东西没过来时

插件自带的自动化测试会加载插件保存的资产，并拿它们和保存时的内容对照（`DreamGUI.Compatibility.*`、`DreamGUI.Assets.*`）；
redirect 段本身由 2.1.0 的测试检查过：每一条都到达了引擎、没有旧名字仍是活着的类型、没有一条跳进另一条 redirect、每个新名字都存在。
放着这一段时你的某个资产仍加载失败，值得带着引擎为它打的那条警告报告上来：DreamGUI 改了名却没给 redirect 的名字，是一个 bug。

问题反馈：https://github.com/TypeDreamMoon/DreamGUI/issues
