---
title: 渐变与视口手柄
description: RectBlock 主体和描边上的多色标渐变——线性、径向、角度、菱形、对称和四角，和文字用的是同一种渐变——在细节面板里改，或在设计器视口里拖它的端点和色标；以前的两色渐变怎么迁移过来；以及其它功能拖点用的手柄框架。
---

## RectBlock 的渐变

`bEnableBodyGradient`（或 `bEnableBorderGradient`）打开时，RectBlock（`UDreamRectBlock`）的主体（或描边）用渐变代替纯色。
渐变是 `BodyGradient` 和 `BorderGradient`，都是 `FDreamGradient`——文字涂色用的那个结构
（[渐变文字](text.md#渐变文字)），GPU 上也从同一组渐变行里画：RectBlock 和文字用同一个渐变时共用一行。

| 类型 | 图像编辑器里叫 | 是什么 |
| --- | --- | --- |
| `Linear` | 线性 | 沿一个方向（`Angle`），即 CSS 的 `linear-gradient` |
| `Radial` | 径向 | 从 `Center` 向外到一个椭圆（`Shape`、`Size`、`Radius`），椭圆按 `Angle` 旋转 |
| `Conic` | 角度 | 绕 `Center`，从 `Angle` 起顺时针 |
| `Diamond` | 菱形 | 从 `Center` 向外成菱形，按 `Angle` 旋转 |
| `Reflected` | 对称 | 从过 `Center`、垂直于 `Angle` 的一条线向两边对称展开 |
| `Corners` | —— | 前四个色标放在四个角，在中间混合 |

最多 16 个色标，每个是一个颜色（sRGB、直通 alpha）加它在渐变上的位置；`Spread` 决定两端以外怎么办（延伸、重复、往返），
`Scale` 和 `Offset` 拉伸和移动渐变，`Interpolation` 决定色标在 sRGB、线性光还是 Oklab 里混合。主体的渐变代替
`BodyColor`，和颜色一样乘以主体纹理；描边的渐变代替 `BorderColor`。

在 `.dui` 里，渐变写成它的 CSS，也就是细节面板显示和复制的写法：

```
RectBlock Card {
    bEnableBodyGradient = true
    BodyGradient = "linear-gradient(135deg, #FFF3B0, #E8B64A 55%, #9C6A12)"
    bEnableBorder = true
    bEnableBorderGradient = true
    BorderGradient = "reflected-gradient(90deg at 50% 50%, #FFFFFF, #30B0C7)"
}
```

`diamond-gradient`、`reflected-gradient` 和 `corners-gradient` 是 DreamGUI 给 CSS 没有的类型起的名字；`radial-gradient`
里写角度表示旋转它的椭圆。C++ 和蓝图里用 `SetBodyGradient`、`SetBorderGradient`，`GetEffectiveBodyGradient` /
`GetEffectiveBorderGradient` 拿到 RectBlock 实际画的渐变。

### 细节面板

Body › Gradient（和 Border › Gradient）这一行把渐变画成一条色带，旁边是类型下拉和 **Presets** 菜单：项目里的渐变资产
（`UDreamGradientAsset`）和命名预设（Project Settings › Plugins › Dream GUI › Gradient Presets），也可以把当前渐变存成这两种。
展开后是渐变的 CSS（可以输入或粘贴）、引擎的渐变编辑器里的色标（拖动标记、双击选颜色、点色带加色标）、色标列表，
以及当前类型用到的字段。

## 视口手柄

在设计器里选中一个 RectBlock，它的渐变会像图像编辑器的渐变工具那样画在上面：

- **线性、对称**：起点和终点。拖哪个，另一个不动。
- **径向、角度（Conic）、菱形**：中心和终点——终点决定半径和旋转（径向椭圆还有第二个终点管另一条半径；角度的终点只决定起始方向）。
  拖中心移动整个渐变。
- **色标**：每个色标是连线旁边一个它自己颜色的小方块，用短线连到连线上。沿连线拖动改位置；拖离连线足够远就删掉（至少留两个）。
  双击连线，在那里加一个当前颜色的色标。
- 按住 **Shift** 时旋转按 15° 一档。拖动中按 **Esc** 全部还原。

一次拖动是一步撤销，松开鼠标时写回 `.dui`。主体和描边的渐变都开着时，手柄显示主体的；细节面板里描边那一行有个按钮，
可以换成显示描边的。

## 以前的两色渐变

RectBlock 以前的渐变是两色径向：`BodyGradientColor` 在 `BodyGradientCenter`，渐变到 `BodyGradientRadius` 处的 `BodyColor`，
按 `BodyGradientRotation` 旋转，中心和半径各有单位模式（描边有一套同样的 `BorderGradient…` 属性）。这些都会自动迁移：

- `BodyGradient` 没有色标的 RectBlock 照样画以前的两色渐变，只是现在换成两个色标：一个按原来的中心、半径和旋转摆放的径向椭圆，
  在线性光里混合。画出来和以前一样，每个通道最多差一级。
- 旧写法的 `.dui` 照样能编，设计器也不会动这些行，直到渐变被改动。第一次改动——不管在细节面板还是用手柄——会把两个色标写进
  `BodyGradient`，文件里新的一行替换掉旧的那几行。
- 旧的 setter（`SetBodyGradientColor`、`…Center`、`…Radius`、`…Rotation`，描边的同名函数，以及单位模式的）仍然可用，
  标为 deprecated。渐变已经有色标时，它们改的是第一个色标的颜色、中心、半径和角度。补间（`BodyGradientColorTo` 等）也走它们。

有两点和以前不同。渐变还是两色时，按单位（而不是百分比）写的中心和半径仍按单位固定；有了色标之后，一切都是矩形的比例，
跟着矩形缩放。另外，主体纹理本身半透明时，以前渐变色会在它的 alpha 上再叠一次；现在渐变像主体颜色一样给纹理染色。

## 给其它功能用的手柄

视口手柄是一个框架（DreamGUIEditor 模块里的 `Designer/DreamDesignerHandles.h`），选中的任何东西都可以用。提供者——用
`FDreamDesignerHandles::Register` 注册的 `IDreamDesignerHandleProvider`——针对选中的控件说明：有哪些点、画哪些辅助线、
拖一个点会写哪些属性、拖动的每一步怎么改预览对象。其余的事视口对所有提供者都一样处理：命中、绘制、一次拖动一个事务、
Esc 时还原预览、松开时把属性写进资产再写回 `.dui`，还有双击。变形晶格点、路径点和 RectBlock 渐变就是三个提供者。

```cpp
#include "Designer/DreamDesignerHandles.h"

class FMyHandles : public IDreamDesignerHandleProvider
{
public:
	virtual FName GetName() const override { return TEXT("MyHandles"); }
	virtual void CollectHandles(UDreamWidget* InWidget, FDreamDesignerHandleSet& OutSet) const override
	{
		if (const UMyVisual* Visual = Cast<UMyVisual>(InWidget->GetVisual()))
		{
			OutSet.AddHandle(0, Visual->GetPoint());
		}
	}
	virtual void GetEditedProperties(UDreamWidget* InWidget, int32 InHandleId, TArray<FDreamDesignerEditedProperty>& Out) const override
	{
		Out.Emplace(InWidget->GetVisual(), TEXT("Point"));
	}
	virtual FText GetTransactionName(int32 InHandleId) const override { return INVTEXT("Move Point"); }
	virtual bool UpdateDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag) override
	{
		Cast<UMyVisual>(InWidget->GetVisual())->SetPoint(InDrag.GetTarget());
		return true;
	}
};

// 在你的编辑器模块的 StartupModule 里：
FDreamDesignerHandles::Register(MakeShared<FMyHandles>());
```

点的坐标在控件的本地空间——以 pivot 为原点的单位，+Y 向上——`RectFractionToLocal` / `LocalToRectFraction` 在它和矩形比例之间换算。
