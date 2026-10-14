---
title: "节点与值"
description: "节点怎么写、id 保持的规则、没有 id 的节点、节点类型的解析顺序，以及属性可取的每一种值。"
---

<a id="nodes-and-ids"></a>

## 节点与 id

一个节点由一个类型、一个 id 和一个可选的块组成：

```
Text Title {
    Text = "Settings"
}
```

块中可任意顺序包含：属性、`@slot` 行、`+ Component` 块、子节点，以及控制语句 `if` 和 `for` / `each`。没有什么要表达的节点不需要块：单独一行 `Image Divider` 就是一个节点。

**id** 是节点的身份。它是控件的名字、蓝图图表读取的类成员变量、绑定解析所用的键，以及节点字符串的本地化键。因此：

- 它是 C++ 标识符：字母、数字和 `_`，不以数字开头（DUI3002）。ASCII 之外的字符算作字母，所以 id 可以是中文。
- 它在文件中唯一，比较时不区分大小写（DUI3001）——它变成的变量是 `FName`，而它忽略大小写。
- 它不是关键字：`class`、`style`、`resources`、`slot`、`for`、`each`、`in`、`was`、`use`、`timeline`、`external`、`ease`。

id 后面可以跟两个子句，顺序随意：

- `: StyleName` —— 节点穿上一个样式。见[样式](styles-and-resources.md#styles)。
- `(was: OldId)` —— 节点改过名。下一次编译会把指向旧 id 的东西——图表引用、绑定、动画轨道——搬到新 id 上。保留该子句直到文件至少编译过一次；再次改名会保留第一个旧 id。DUI3010 到 DUI3013 是改名可能被拒绝的几种情况。

<a id="nodes-with-no-id"></a>

### 没有 id 的节点

没有谁按名字引用的节点可以省掉 id，只要类型后面跟着一个块或一个样式子句：

```
HorizontalBox {
    Spacing = 14
    Text { Text = "Status" }
    Text : Caption { Text = "Ready" }
}
```

解析器会给这样的节点一个自己的 id：`<parent id>__<type><n>` —— 最近的有 id 的外层节点的 id（或者是 `Root`）、两个下划线、类型（id 不能包含的每个字符都变成 `_`），以及它前面同类型的无名兄弟节点的个数。`Root__HorizontalBox0` 下的第一个无名 `Text` 是 `Root__HorizontalBox0__Text0`。相同的文本总是得到相同的 id，所以只要文件保持形状，它们就和手写的 id 一样稳定；但在某个无名节点前面插入一个同类型的无名兄弟，后面的节点就会重新编号。

无名节点仍然有类成员变量——运行时通过它找到绑定所在的控件——但那是一个隐藏变量：蓝图图表看不到它。代码需要用到某个节点时就给它一个 id。无名节点不能带 `(was: …)`（DUI2004）：没有旧名可改。

一行里只有一个类型，没有 id、没有块也没有样式（`Text`），仍然是一个错误（DUI2004）：它同样可能是一个漏了 `=` 的属性。


<a id="node-types"></a>

## 节点类型

节点类型的含义按以下顺序判定：

1. **内置标签**：`Widget`（一个没有视觉的矩形，最常用的容器），或者某个视觉的标签——`Text`、`Image`、`RectBlock`、`Sprite`、`Texture`、`Ring`、`Polygon`、`PolygonLine`、`Line2DRaw`、`Line2DChildren`、`Empty`、`StaticMesh`、`BackgroundBlur`、`BackgroundPixelate`、`PixelSort`、`PostProcessRenderElement`、`PostProcessRenderElementText`、`CanvasRenderTargetPreviewer`、`UMGWidget`。插件可以用 `DECLARE_DREAM_GUI_VISUAL` 添加一个视觉。
2. **布局容器**：`VerticalBox`、`HorizontalBox`、`StackBox`、`Overlay`、`CanvasPanel`、`GridPanel`、`UniformGridPanel`、`WrapBox`、`SizeBox`、`ScaleBox`、`SafeZone`、`ScrollBox`、`WidgetSwitcher`、`Border`、`MenuAnchor`。这样的节点是一个挂着该容器的普通控件，它自己的行既能设容器的属性，也能设控件的属性：

   ```
   VerticalBox Categories : Lists {
       Spacing = 29
       Row Audio { Label = "Audio" }
   }
   ```

   和 `Widget Categories : Lists { + VerticalBox { Spacing = 29 } … }` 是同一棵树。控件和容器都有的名字算控件的。容器类型的节点不能再带第二个容器：在它上面写 `+ HorizontalBox` 是 DUI5022。
3. **组件别名**：由 `use … as` 给出的名字——`Row`，或者命名空间带进来的 `nier.Row`。见[`use`](use.md#use)。
4. **`@Name`**：某个 `resources` 块的 `Asset` 条目所指的控件类。这是用短名指代组件的旧写法；`use … as` 才是首选。
5. **注册标签**：`Scope.Name`，用 `DECLARE_DREAM_GUI_WIDGET` 注册的控件类。插件的控件都在 `Native` 下：`Native.Button`、`Native.Toggle`、`Native.Slider`、`Native.SpinBox`、`Native.ProgressBar`、`Native.Dropdown`、`Native.TextInput`、`Native.EditableText`、`Native.MultiLineEditableText`、`Native.RichText`、`Native.ScrollBox`、`Native.ScrollBar`、`Native.List`、`Native.TileView`、`Native.TreeView`、`Native.TabView`、`Native.ExpandableArea`、`Native.Border`、`Native.Dialog`、`Native.MenuAnchor`、`Native.RingMenu`、`Native.RadioButton`、`Native.InputKeySelector`、`Native.Throbber`、`Native.NativeWidgetHost`。
6. **资产路径**：`/Game/UI/WBP_Row`，原生类则写 `/Script/MyGame.MyWidget`。

由类（3 到 6）作为类型的节点是某个组件的*实例*：它的内容来自那个类，它的行设的是那个类的属性。这个类必须是一个具体的 DreamUI user widget（DUI5006）。

因为标签和容器最先判定，别名永远改变不了 `Text` 或 `VerticalBox` 的含义；试图这么做的别名会被拒绝（DUI3018）。

`DUI/.dui-symbols.json` 会在编辑器启动时以及 `DreamUI.ExportSymbols` 时写出，列出每个标签、容器和注册控件及其属性，供编辑器补全使用。


<a id="properties-and-values"></a>

## 属性与值

```
Name = Value
AnchorData.SizeDelta = (400, 240)
```

名字是一个反射属性，或者一条指进结构体属性的点分路径。裸名字的查找顺序是：先控件，再它的视觉，然后——对容器类型的节点——它的容器，最后它的各个行为；第一个有该属性的拿走。谁都没有的名字是 DUI4001，并给出最接近的匹配。

同一个块里把一个属性写两次，后写的行赢。节点自己的行赢过它样式里的行。

<a id="values"></a>

### 值

| 种类 | 例子 | 说明 |
|---|---|---|
| 数字 | `24`、`0.95`、`-3`、`1e-45` | 按属性的数值类型读。 |
| 字符串 | `"Settings"`、`"a \"quote\""` | 转义：`\"`、`\\`、`\n`、`\t`、`\r`。写在 `FText` 上时字符串会被本地化。 |
| 颜色 | `#FFF`、`#FFFF`、`#1E1E1E`、`#1E1E1EFF` | 3、4、6 或 8 位十六进制。sRGB。 |
| 元组 | `(400, 240)`、`(0, 8, 0, 0)` | 向量、边距、旋转——属性是什么结构体就是什么。可以跨行。 |
| 词 | `Left`、`Collapsed`、`true`、`None` | 枚举值、布尔值，或表示空引用的 `None`。 |
| 资产路径 | `/Game/UI/T_Icon`、`"/Game/UI/T_Icon.T_Icon"` | 对象或类引用。加引号也行。 |
| 资源 | `@Accent` | 某个 `resources` 条目的值。见[资源](styles-and-resources.md#resources)。 |
| 节点 id | `CheckMark` | 写在持有控件、视觉或行为的属性上时，指本文件里那个 id 的节点。 |

标志位枚举没有 `|`；组合写成各个标志相加得到的数字。

字符串可以带上它本地化所依据的键：`Text = "OK" @key("Dialog.Confirm")`。不带键时，键为
`<node id>.<property>`——对于 `+` 块的某一行，两段之间会加上组件的类和位置——位于
`class` 行所给出的命名空间中。
