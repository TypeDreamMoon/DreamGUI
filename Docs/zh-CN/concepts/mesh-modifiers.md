---
title: "网格修改器与文字动画"
description: "在视觉交出的几何上做加法：七个修改器、文字动画那一族的三个维度与二十个类，以及它们的顶点预算与 UV 通道约束。"
---

视觉把几何交出去**之后**，还能动它吗？这一页讲的就是那件事 —— **不换视觉、不改布局**，只动顶点、颜色和 UV。

- 视觉是「画成什么」→ [控件模型](widgets.md)
- 文字怎么排 → [文本](text.md)
- **交出去之后还能不能动它** → 本页

<a id="what-a-modifier-is"></a>

## 修改器是什么

它是**行为组件**（`UDreamMeshModifierBase : UDreamUIBehaviour`），`Abstract`、可蓝图生成，**要挂在会建网格的视觉上**。

三个成员定义它的全部能力：

| 成员 | 回答什么 |
| --- | --- |
| `ModifyUIGeometry(geometry, four flags)` | 改网格本体；四个旗标说「三角形 / 顶点位置 / UV / 颜色」里**哪些这次变了** |
| `ModifierWillChangeVertexData(out four flags)` | **我会动哪些**：蓝图层一律返回全 `true`；原生子类应精确回答 |
| `GetDuplicatesMesh()` | **我是不是在追加整份网格副本**（描边 / 阴影 / 长阴影 → `true`） |

**第二个声明不是装饰**，它有两个后果：**省算力**（不声明就不重算那一类）；以及**文字用它决定走哪条绘制路径** —— 声明会动**顶点位置或 UV** 的修改器，会让文字**留在字体的距离场上**。原因是[文本](text.md)里那种按设备像素网格 1:1 采样的小字覆盖字形，**顶点一挪就错位**。

所以**答案宁可真、不可漏**：多声明只多一次重算，**少声明会丢掉写入**。

**第三个声明决定顺序**：追加副本的修改器**总排在就地修改的之后，不管你组件怎么摆**。副本是**一张照片** —— 它冻结前面修改器的产物，而它的顶点在**所有字符顶点范围之外**，文字动画够不到它。

蓝图侧走 `ReceiveModifyUIGeometry`，拿到的 `UDreamVisualBatchMeshModifierHelper`（继承 `UDreamUIGeometryHelper`）带一组 `UITextHelperFunction_*`，可按**字符下标**改单个字的变换 / 颜色 / Alpha；位置参照分 `Relative`（相对字原点）与 `Absolute`（相对文本轴心）。

<a id="the-seven-modifiers"></a>

## 七个修改器

模块按导出宏核过：`G` = `DREAMGUI_API`（核心），`X` = `DREAMGUIEXTENSIONS_API`。

| 类（显示名） | 模块 | 做什么 | 关键旋钮（含默认） |
| --- | :--: | --- | --- |
| `UDreamMeshModifierBase` | **G** | 基类（`Abstract`），不是挂上去用的 | `bEnable`（true） |
| `GradientColor` | X | 给顶点**上渐变色** | `DirectionType` 五值；`Color1`/`Color2`；`Color3`/`Color4`（仅 `FourCorner`）；`bMultiplySourceAlpha` |
| `LongShadow` | X | **长阴影**：沿一个方向把剪影拉出多段 | `ShadowSize` (0,1,-1)；`ShadowSegment` **5**；`bUseGradientColor` |
| `Mirror` | **G** | 绕**控件矩形中心**镜像 | `bMirrorHorizontally`（true）/ `bMirrorVertically`（false） |
| `Outline` | X | **描边**：四周各铺一份剪影 | `OutlineSize` (1,1)；`bUse8Direction`（false = 4 方向） |
| `Shadow` | X | **投影**：偏移一份剪影 | `ShadowOffset` (0,1,-1)；`ShadowColor`（黑） |
| `PositionAsUV` | X | 把**顶点位置写进一条 UV 通道** | `UVChannel` **0**；`Scale` 1 |

⚠️ **模块归属别按家族猜**：七个里**只有基类与 `Mirror` 在核心模块**，其余五个（含文字动画全族）都在 `DreamGUIExtensions`。

三个「副本类」（`LongShadow` / `Outline` / `Shadow`）都 `GetDuplicatesMesh() = true`；差别在**副本份数**：`Shadow` **1 份**、`Outline` **4 或 8 份**、`LongShadow` **每段一份**（默认 5 份）。副本**不携带源文本的渐变 paint**：它们的 `UV4` 是 (0,0)，文本副本的 paint 槽也会被清掉。它们照样画阴影或描边；在副本里，彩色字形被渲染成**剪影**。

三处原理各不相同：

- **渐变**就地在**顶点色**上改，不动位置与 UV —— 所以便宜，也**不会**把文字从小字覆盖字形路径赶回距离场。
- **镜像**把顶点绕矩形中心**反射**，但**保留纹理坐标**。反射一个轴会让每个三角形**翻面**，所以它**把三角形重绕一遍** —— 否则剔除背面的材质会什么都画不出。它存在的理由很具体：UMG 用一个 `scale(-1,1)` 的渲染变换翻背景给**从右到左**的文化用，`UDreamLayoutContainerBorder` 照此为 `bFlipForRightToLeftFlowDirection` 造一个。
- **位置转 UV** 是**唯一改 UV 的一个**。⚠️ 通道归属是最大的坑：**`UV1` 被画布占着**（见[渲染](rendering.md#vertex-format)的「顶点格式」），所以 `UV1` 被 **setter 与写入双双拒收**，默认也改成了 `UV0` —— 之前的默认正是画布那条，**等于开箱即坏**。

<a id="text-animation-is-a-modifier-family"></a>

## 文字动画是一个修改器族

`UDreamMeshModifierTextAnimation`（显示名 `TextAnimation`）**继承 `UDreamMeshModifierBase`** —— 上面那整套对它**全部适用**。它在基类之上加两样：

- **一个 `Selector`**（`Instanced`）：决定**选哪些字**，并给每个字一个 **0–1 的插值**。选择器的 `Offset` 就是**做动画的那根轴**；修改器上的 `SelectorOffset` 是它的代理，**专门给 Sequencer 打关键帧**用。
- **一个 `Properties` 数组**（`Instanced`）：决定**改什么、怎么改**。

它还要**登记到文本**上：这样文本会**逐字符排版**、不再把字母连成连字（"fi" 默认并成一个字形，逐字符动画必须先拆开）；换视觉或注销时再解除。

它自己的 `ModifierWillChangeVertexData` 有一处精确判断：**只有装了「会移动字形」的属性**才声明位置与 UV —— 内置的 `Alpha`、`Color` 与 `ColorRandom` **只改颜色、不移动**，于是**纯淡入 / 变色不会把文字赶出小字路径**。

<a id="twenty-classes"></a>

## 二十个类

**实测 20 个**，由三个维度**正交**组合出来 —— 用一张三维表说清，比逐个列清楚得多：

| 维度 | 类 | 作用 |
| --- | --- | --- |
| **中间基类**（3） | `_Property` / `_PropertyWithEase` / `_PropertyWithWave` | 属性的根（`Init` / `Deinit` / `ApplyProperty`）／加**缓动**（`EaseType` 默认 `InOutSine`）／加**正弦波**（`Frequency` 默认 1、`Speed` 默认 1） |
| **选择器**（4） | `_Selector`（抽象）/ `RangeSelector` / `RandomSelector` / `RichTextTagSelector` | 按 0–1 的**位置区间**选／**随机**选／按**富文本自定义标签**选范围 |
| **属性**（12） | Alpha 1、Color 2、Position 3、Rotation 3、Scale 3 | 每个按「普通 / 随机 / 波」铺开 |

**20 = 1 基类 + 3 中间基类 + 4 选择器 + 12 属性。** 「普通」与「随机」继承 `_PropertyWithEase`，
「波」继承 `_PropertyWithWave` —— **缓动 / 波来自中间基类那一维，不是每个属性各写一遍**。

⚠️ `_PropertyWithWave` 有一条**已修的历史 bug**：`Frequency`（值越大波长越短）与 `Speed`
（波速）**曾经读写串了** —— `SetFrequency` 实际动的是 `Speed`。修对之后，
**从蓝图调用 `SetFrequency` 的行为会静默改变**：节点还在、还能编译，只是动的属性变了。
资产不受影响（两个都是 `EditAnywhere`、各自序列化）。

<a id="using-it"></a>

## 怎么用

给会建网格的视觉控件加一个修改器组件（`bEnable` 默认开）。选择器加属性：

```cpp
auto* Anim  = Text->AddComponent<UDreamMeshModifierTextAnimation>();
auto* Sel   = NewObject<UDreamMeshModifierTextAnimation_RangeSelector>(Anim);
Sel->SetStart(0.f);
Sel->SetEnd(1.f);
auto* Alpha = NewObject<UDreamMeshModifierTextAnimation_AlphaProperty>(Anim);
Anim->SetSelector(Sel);
Anim->SetProperties({ Alpha });
// Sequencer pulls SelectorOffset from 0 to 1, and the characters light up one by one
```

当 `SetProperties` / `SetProperty` 在**已登记**状态下换属性时，会把旧的 `Deinit`、新的
`Init`。

<a id="when-it-does-not-serve"></a>

## 什么时候它不好用

- **副本类修改器吃顶点**：`Outline` 4 或 8 份、`LongShadow` 每段一份。顶点多到一个绘制调用的索引装不下时（见
  [渲染](rendering.md#indices-are-16-bit) 的「索引是 16 位的」），源码**每个修改器只报一次错、然后
  不画那个副本** —— 「阴影突然没了」先看顶点预算。
- **顺序不由你定**：`GetDuplicatesMesh` 为真的修改器**总排在就地修改的后面**。
- **`PositionAsUV` 不能写 `UV1`**；写 `UV0` **会替换图像**；`UV2` / `UV3` 占没占要看
  **具体视觉**。
- **会移动顶点的修改器会让文字离开小字覆盖字形路径**（退回距离场）；
  `Alpha` / `Color` / `ColorRandom` 除外。
- **镜像只能绕控件矩形中心**，且反射会翻面、靠重绕三角形修 —— 用剔除背面的材质时尤其记得。
- **修改器要挂在会建网格的视觉上**，文字动画要挂在有 `Text` 的控件上。

<a id="not-yet-confirmed"></a>

## 还没证实的

- 这块**没有任何 `Docs/Reference/` 页面**（搜 MeshModifier 为空），以上全部来自
  源码头文件 —— 逐类的完整属性表要看生成的参考页。
- `LongShadow` 的 `ShadowSize` 第三分量（Z = −1）与段数的几何关系**未
  逐行核**。
- `GradientColor` 的 `FourCorner` 四个颜色到四角的映射**未逐行核**。
