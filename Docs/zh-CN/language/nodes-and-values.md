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

块中可任意顺序包含：属性、`@slot` 行、`@tag` 行、`+ Component` 块、子节点，以及控制语句 `if` 和 `for` / `each`。没有什么要表达的节点不需要块：单独一行 `Image Divider` 就是一个节点。

**id** 是节点的身份。它是控件的名字、蓝图图表读取的类成员变量、绑定解析所用的键，以及节点字符串的本地化键。因此：

- 它是 C++ 标识符：字母、数字和 `_`，不以数字开头（DUI3002）。ASCII 之外的字符算作字母，所以 id 可以是中文。
- 它在文件中唯一，比较时不区分大小写（DUI3001）——它变成的变量是 `FName`，而它忽略大小写。
- 它不是关键字：`class`、`style`、`resources`、`slot`、`for`、`each`、`in`、`was`、`use`、`timeline`、`external`、`ease`。

id 后面可以跟两个子句，顺序随意：

- `: StyleName` —— 节点穿上一个样式，或几个，`: A : B`。见[样式](styles-and-resources.md#styles)。
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


<a id="tags-tag"></a>

## 标签：`@tag`

标签是控件携带的名字，好让代码按它是什么、而不是按它的 id 找到它 —— 每个 trim，每个 glow —— 就像 actor 的标签那样。一行 `@tag` 给节点一个或多个名字：

```
Image TrimLeft {
    @tag Trim
    @tag Glow, UI.Accent
}
```

- **一个名字**是标识符，保留大小写。`.` 可以连接标识符（`UI.Accent`），ASCII 之外的字符也算字母，所以标签可以是中文。名字之间用逗号分隔，行在换行、`;` 或收尾的 `}` 处结束。`tag` 只有在紧跟 `@` 之后且为小写时才是关键字；它仍可用作 id 和属性名。
- **位置。** 任何控件节点上都可以：`slot` 声明的块、`rows` 行的块，以及 `for` 或 `each` 重复出来的控件——每一份副本都带着这些标签。`style` 里不能放，`for` / `each` 的主体里直接也不能放（把该行写在它重复的控件上）：两者都是 DUI2024。`if` 的分支只能放控件，所以直接写在其一的标签行是 DUI2018，而容纳控件的槽填充同样不能放标签行（DUI2019）：给控件打标签。
- **读不通的行**——`@tag` 之后什么都没有、数字或字符串、两个名字之间没有逗号、末尾多一个逗号——是 DUI2024，报在 `@` 处。
- **同一个名字写两次仍是一个标签**，无论在同一行还是两行。
- **它们落在 `Tags` 里**，即每个控件都有的 `TArray<FName>`。同一节点上的 `Tags = [Trim, Glow]` 行同样有效，两者会叠加。

`@tag` 也是写出由 `Asset` 资源指定类型的节点的方式（`@Name Id { }`，见[节点类型](#node-types)），所以名为 `tag` 的资源必须加以区分。节点而不是标签行，是 `@tag` 后跟一个名字，然后是 `{`、`:` 或 `(was: …)`——`@tag Trim { }`、`@tag Trim : Style`——或者直接跟 `{` 或 `.`：`@tag { }` 和 `@tag.Something`。`@tag` 之后的其它一切内容都是标签行。

<a id="finding-widgets-by-tag"></a>

### 按标签查找控件

`UDreamWidget` 有 `Tags`、`HasTag(FName)`、`AddTag(FName)` 和 `RemoveTag(FName)`，它们都能从蓝图调用。用户控件用 `GetWidgetsByTag`（一个纯蓝图函数）查找自己内容中带标签的控件，或在 C++ 中用 `ForEachWidgetWithTag`，后者接受一个访问者且不生成数组：

```cpp
for (UDreamWidget* Trim : GetWidgetsByTag(TEXT("Trim")))
{
    Trim->SetRenderOpacity(Opacity);
}
```

- 它搜索控件的**自身**内容，先父后子，同级按顺序。它不会查看嵌套组件自身的内容——那些归该组件自己的 `GetWidgetsByTag` 管——但组件实例本身会被搜索，本控件放进该实例槽里的内容也会被搜索。
- 宿主放进**本**控件槽里的内容属于宿主，不会被找到。
- 在控件初始化之前，以及对于 `None`，它是空的。

库中没有任何东西会读取 `Tags`；它们供发问的代码使用。


<a id="node-types"></a>

## 节点类型

节点类型的含义按以下顺序判定：

1. **内置标签**：`Widget`（一个没有视觉的矩形，最常用的容器），或者某个视觉的标签——`Text`、`Image`、`RectBlock`、`Sprite`、`Texture`、`Ring`、`Polygon`、`PolygonLine`、`Shape`、`Line2DRaw`、`Line2DChildren`、`Empty`、`StaticMesh`、`BackgroundBlur`、`BackgroundPixelate`、`PixelSort`、`PostProcessRenderElement`、`PostProcessRenderElementText`、`CanvasRenderTargetPreviewer`、`UMGWidget`。插件可以用 `DECLARE_DREAM_GUI_VISUAL` 添加一个视觉。
2. **布局容器**：`VerticalBox`、`HorizontalBox`、`StackBox`、`Overlay`、`CanvasPanel`、`GridPanel`、`UniformGridPanel`、`WrapBox`、`SizeBox`、`ScaleBox`、`SafeZone`、`ScrollBox`、`WidgetSwitcher`、`Border`、`MenuAnchor`。这样的节点是一个挂着该容器的普通控件，它自己的行既能设容器的属性，也能设控件的属性：

   ```
   VerticalBox Categories : Lists {
       Spacing = 29
       Row Audio { Label = "Audio" }
   }
   ```

   和 `Widget Categories : Lists { + VerticalBox { Spacing = 29 } … }` 是同一棵树。控件和容器都有的名字算控件的。容器类型的节点不能再带第二个容器：在它上面写 `+ HorizontalBox` 是 DUI5022。
3. **组件别名**：由 `use … as` 给出的名字——`Row`，或者命名空间带进来的 `nier.Row`。见[`use`](use.md#use)。
4. **`@Name`**：某个 `resources` 块的 `Asset` 条目所指的控件类。这是用短名指代组件的旧写法；`use … as` 才是首选。（`@tag` 后跟一个名字是[标签行](#tags-tag)，除非后面跟的是块或样式。）
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

属性在拥有它的对象上查找，而一个普通的 `Widget` 节点只有控件：`bRaycastTarget` 属于某个视觉（`Image`、`RectBlock`、`Empty`），因此在 `Widget` 节点上它是错误（DUI5002 指明了拥有它的节点类型）。控件本身拥有的是 `Raycastable`——`Inherit`、`Enabled` 或 `Disabled`——而 `Raycastable = Disabled` 是用来把控件以及继承自它的东西排除在命中测试之外的方法。

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
| 列表 | `[1, 2, 3]`、`["A", "B"]`、`[(0, 0), (8, 8)]`、`[]` | 数组或 Set：每个元素是元素类型的一个值，数组里也可以是节点 id。Set 不保留顺序。可以跨行，末尾可以多一个逗号。 |
| Map | `{ Hover: #FFCC00, Pressed: #AA8800 }`、`{}` | Map：`键: 值` 条目，两边各是对应类型的值。只有一个词的名字键可以不加引号。 |
| Struct | `{ Distance: 12, Angle: 45 }`, `{ "Lamp", Tag: Lamp }` | 没有短写形式的 struct：`field: value` 条目，字段的写法与属性名相同。见[结构体作为字段映射](#structs-as-field-maps)。 |
| 等高线 | `Gaussian`、`"custom(0 0, 0.5 1 corner, 1 0)"` | 图层样式的等高线：命名形状写它的名字；自定义曲线写它的点——每个点是输入和输出，曲线在某点处折角就在该点后写 `corner`。 |

颜色字面量按 sRGB 编码，与 CSS 中一样：`#8C8C94` 是字节 140、140、148。在 `FColor` 属性上（视觉的 `Color`、画刷的 `TintColor`），它按书写原样存储。在 `FLinearColor` 上它会先被解码为线性值，因此同一个字面量的前两个通道约为 0.26，而不是 0.55。在涉及颜色相乘的地方，运算方式并不处处相同：控件的内容着色和颜色乘数在线性空间中作用于视觉的 `Color`（解码、相乘、再编码），而图像的 `Brush.TintColor` 则是在编码后的字节上逐通道乘入其颜色。线性空间中的中灰着色会比它看起来的样子更暗：`#808080` 在线性空间中是 0.22，而使光量减半的着色是 `#BCBCBC`。

标志位枚举没有 `|`；组合写成各个标志相加得到的数字。

把列表写在不是数组或 Set 的属性上、把 Map 写在不是 Map 的属性上，或者节点的 Set/Map，都是 DUI4009；读不通的列表或 Map（括号没有闭合、Map 条目缺少 `:`，但 struct 的第一个条目除外，它可以不带键）是 DUI2023。

字符串可以带上它本地化所依据的键：`Text = "OK" @key("Dialog.Confirm")`。不带键时，键为 `<node id>.<property>`——对于 `+` 块的某一行，两段之间会加上组件的类和位置——位于 `class` 行所给出的命名空间中。没写 `class` 行的文件，命名空间是它在所属 `DUI/` 目录下的路径（`Panels/Settings.dui`、`Plugin.X:Panels/Settings.dui`），换机器也不变。

另外三个注解写给译者，跟在字符串后面，顺序随意：

```
Text = "应用 {Count} 项更改" @note("Footer button; applies at once") @max(16)
Text = "v1.0.3" @noloc
```

- `@note("…")`——这段字是做什么的。它随文字一起交给本地化工具（也作为文字的开发者备注进引擎的采集）。
- `@max(n)`——译文最长多宽，按显示宽度算，全角字符算 2。
- `@noloc`——每种语言都一样的字，比如版本号、名字：永远不翻译，也不会出现在任何工具的列表里。它不能和另外三个一起写。

每个注解只写一次；写在不是字符串的值后面，或者写法不对，都报 DUI2022。


<a id="structs-as-field-maps"></a>

### 结构体作为字段映射

自身没有短写形式的 struct——不是 vector、颜色或 margin，那些由元组和颜色字面量覆盖——写为其字段的 Map，无论该 struct 的值出现在哪里：struct 属性上，或作为数组、Set 或 Map 的元素。

```
Style.DropShadows = [{ Distance: 12, Angle: 45 }, { Distance: 4 }]
```

- 每个键都是字段名，按属性名的方式解析，每个值按其字段的类型读取，所以本身是文本、颜色或列表的字段就按相应写法书写。没有写出的字段保留 struct 的默认值。struct 没有的字段名是 DUI4002。
- 文本字段和其它字符串一样本地化，键为其属性的键与字段名拼接而成（`Items.Label`），因此 struct 里的字符串也能到达译者。
- **带主字段的 struct** 把它作为 Map 的第一个条目且不带键：`{ "Lamp", Tag: Lamp }` 就是 `{ Label: "Lamp", Tag: Lamp }`。struct 用 `DreamGUI_PrimaryField` meta 指定其主字段；目前 `FDreamRingMenuItem` 就是这样，它的主字段是 `Label`。只有第一个条目可以不写键；在实际上是 `TMap` 的 Map 上，或在没有主字段的 struct 上这样做，是 DUI4009。
- 旧写法——把引擎对该 struct 自身的文本写在字符串里作为一个元素，即 `["(Distance=12)"]`——仍然可以读取。

环形菜单的条目正是为此而设的例子：

```
Native.RingMenu Wheel {
    Items = [
        { "Lamp", Tag: Lamp },
        { "Door", Tag: Door, bEnabled: false, Weight: 2 },
        { Label: "Window", Tag: Window },
    ]
}
```

写回的范围比读取窄。设计器和蓝图转换器只为指定了主字段的 struct 打印 Map；其它所有 struct 在输出时保留带引号的形式，因为像线性颜色这样的字段会经由十六进制码被量化。见[设计器写回](write-back.md#what-the-designer-writes-back)。
