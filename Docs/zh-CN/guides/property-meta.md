---
title: 属性的 meta 标签
description: 决定属性在 DreamGUI 细节面板里放在哪、是否隐藏、用什么控件、是否作为效果开关的 DreamGUI_ meta 标签——没写标签的属性永远不会被丢掉——以及扩展的类和定制怎么用它们。
---

DreamGUI 的细节面板按所显示属性上的 meta 标签来排版。标签都以 `DreamGUI_` 开头，而且一个都不是必需的：
**没写标签的属性，照引擎的做法显示在它自己的 `Category` 下。** 只有标了 `DreamGUI_Hidden` 的属性不显示。
DreamGUI 的面板不会整类隐藏分类，所以给类新加的属性，不会因为分类起错了就在编辑器里消失。

扩展作者自己的类也能用：一个类有标签、却没有细节定制，就会自动得到一个按标签摆放的定制；自己写的定制只要调用
`FDreamDetailsLayout::PlaceTagged` 就会摆放它们（见下文）。

## 标签

| 标签 | 取值 | 作用 |
|---|---|---|
| `DreamGUI_Section` | `Layer`、`Layout`、`Visual`、`Appearance`、`Effect`、`Behavior`、`Advanced` | 放进面板的这个顶层区。 |
| `DreamGUI_Group` | 路径，如 `Glass/Refraction` | 放进所在区的这个组；没写区时放进它自己分类的组。每个 `/` 嵌套一层。 |
| `DreamGUI_Order` | 数字 | 在区或组里的位置，小的在前。没写的算 0，保持声明顺序。 |
| `DreamGUI_Advanced` | -- | 放在所在位置的「高级」展开项下。 |
| `DreamGUI_Hidden` | -- | 不显示：内部状态，或者由面板里另一行编辑的值（在注释里写明是哪一行）。 |
| `DreamGUI_Editor` | `AngleWheel`、`Curve`、`Gradient`、`BlendMode`、`Color`、`Slider` | 用 DreamGUI 的哪种控件编辑，类型本身说明不了时用。选项写在冒号后：`AngleWheel:TopCW`。 |
| `DreamGUI_Handle` | 手柄种类，如 `Gradient`；参数写在冒号后（`Kind:Args`） | 选中控件时设计器视口为它显示哪种手柄。种类由视口手柄框架注册（`FDreamDesignerHandles::RegisterKind`）。 |
| `DreamGUI_LayerToggle` | -- | 整个效果的开关：层级里和图层区里的那只眼睛。结构体成员里的开关也算（`Style.bEnabled`）。 |

「只有另一个属性取某个值时才显示」继续用引擎的 `EditCondition` 和 `EditConditionHides`：行始终在，条件一满足就出现。

```cpp
// 和控件的不透明度、遮罩一起放在图层区，排第二。
UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (DreamGUI_Section = "Layer", DreamGUI_Order = 20))
EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;

// 层级里这个效果的眼睛开关的就是它。
UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil", meta = (DreamGUI_LayerToggle))
bool bEnabled = true;

// 只对填充图有用，也只在那时显示。
UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (EditCondition = "DrawType == EDreamUISpriteDrawType::Filled", EditConditionHides))
float FillAmount = 1;
```

## 区

区是面板里名字、标题、位置都固定的分类，也就是控件细节面板从上到下的那几块：

| 区 | 分类 | 顺序 |
|---|---|---|
| `Layer` | `DreamLayer` | -110 |
| `Layout` | `DreamLayout` | -90 |
| `Visual` | `Visual` | -80 |
| `Appearance` | `DreamAppearance` | -60 |
| `Effect` | `DreamEffect` | -55 |
| `Behavior` | `DreamBehavior` | -50 |
| `Advanced` | `DreamAdvanced` | 100，默认折叠 |

视觉显示在所属控件的细节面板里，所以视觉的 `Layer` 属性由控件的图层区来画，不再出现在视觉自己的行里——视觉的混合模式就是这样
和控件的不透明度排在一起的。

## 在自己的定制里

```cpp
void FMyCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FDreamDetailsLayout Layout(DetailBuilder);
	Layout.Section(DreamPropertyMeta::ESection::Behavior).AddCustomRow(...);   // 自己的行
	// 只在对象处于某种状态时才适用、又没有属性能表达的（画布是不是根），行照样留着，适用时显示：
	Layout.ShowWhen(TEXT("ReferenceResolution"), TAttribute<bool>::CreateLambda([...] { return IsRoot(); }));
	Layout.PlaceTagged();   // 放在最后：还没摆放的带标签属性都摆好
}
```

一个属性要么靠标签摆放，要么由定制自己的行摆放，不要两者都用：定制已经加过或藏过的属性，标签不会再挪它。定制为某个属性
自己画的行（比如旁边带单位按钮的数值）要用 `HideProperty` 藏掉该属性的默认行，在这一行上顺带编辑的属性（单位）标
`DreamGUI_Hidden`。

读标签用 `DreamPropertyMeta`（`DetailCustomization/DreamPropertyMeta.h`），摆放用 `FDreamDetailsLayout`
（`DetailCustomization/DreamDetailsLayout.h`）。

## 守住它的测试

`DreamGUI.Editor.Details.EveryEditablePropertyHasARowUnlessTaggedHidden` 按编辑器的方式显示插件各模块的每个类——控件在
自己的细节面板里，它的视觉、布局和导航在控件面板里，组件在自己的面板里，面板插槽放在每种面板下，其余按类默认值——以及每个
结构体本身；任何可编辑属性没有行又没标 `DreamGUI_Hidden`，或者同一属性出现两行，测试就失败。
