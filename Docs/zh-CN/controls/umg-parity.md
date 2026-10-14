---
title: "从 UMG 过来：名字是一样的"
description: "结构不同，词汇刻意保持一致——Resources/UMGParity 里每个 UMG 类一张对照表，adopt / map / reject 三种结论，自动化测试从两个方向拿它对照 UMG 的反射，以及由反射打印出来的 Docs/Reference。"
---

DreamGUI 和 UMG 的**结构**不同（盒子优先、锚点加枢轴、控件是 `UObject`，见
[控件与树](../concepts/widgets.md)），但它的**词汇**刻意保持一致。一个控件
用的是它 UMG 对应物的名字，含义也是那边的含义：

- `ScrollWidgetIntoView` 接受目标位置和边距；
- 滚动框有 `bFrontPadScrolling` 和 `WheelScrollMultiplier`；
- 数值框有 `MinSliderValue` 和 `ClearMaxValue`；
- 面板槽有 `Nudge` 和 `bForceNewLine`；
- 每个控件都有 `SetIsEnabled`、`SetRenderShear` 和 `FlowDirectionPreference`。

而且**细节面板能拧的每一个旋钮，蓝图在运行时都能拧**，通过一个会把变化推出去的 setter——而不是写一个
之后再没人读的字段。

<a id="the-tables"></a>

## 对照表

名字保不住的地方，是写下来的，而不是留给人去发现。
`Resources/UMGParity/` 里每个 UMG 类一张 JSON 表——共 **58 张**，覆盖 **960 个**面向蓝图的
成员——每一行只说三件事之一：

| 结论 | 含义 | 行数 |
| --- | --- | --- |
| `adopt` | 同名、同义 | 605 |
| `map` | 在这里换了个名字，或者在另一个类型上，并写明是哪个 | 301 |
| `reject` | 刻意没有，附上理由 | 54 |

`reject` 的理由都是结构上的：这里没有即时模式的绘制上下文可以往里画；一个程序化
矩形没有自己的材质实例。下面是 `Border.json` 的一行（它的 `note` 有删节）：

```json
{"umg": "SetBrushFromMaterial", "kind": "function", "status": "reject", "dream": "",
 "note": "A procedural rect has no material of its own to set. ..."}
```

一张表的形状——`ListView.json` 的表头和其中一行：

```json
{
	"umgClass": "UListView",
	"umgHeader": "Components/ListView.h",
	"dreamClass": "UDreamListView",
	"members": [
		{"umg": "EntrySpacing", "kind": "property", "status": "map", "dream": "FDreamListStyle::RowSpacing", "note": "The gap between entries is appearance, so it lives in the list's style rather than on the control: FDreamListStyle::RowSpacing, along the list's main axis (between rows of a vertical list, between columns of a horizontal one)."}
	]
}
```

| 字段 | 含义 |
| --- | --- |
| `umgClass`、`umgHeader` | UMG 那边的类和它的头文件 |
| `dreamClass` | 这边对应的类型 |
| `members[].umg` | UMG 成员名 |
| `members[].kind` | `property` 或 `function` |
| `members[].status` | `adopt` / `map` / `reject` |
| `members[].dream` | 在这边的名字；同名 `adopt` 时为空，`map` 时必须写 |
| `members[].note` | 说明；`reject` 时就是理由 |

对应关系不全是一对一的。几个值得知道的：

| UMG | 这里 |
| --- | --- |
| `UCheckBox` | `UDreamToggle` |
| `UComboBoxString` | `UDreamDropdown` |
| `UEditableTextBox`、`UMultiLineEditableTextBox` | `UDreamTextInput` |
| `UCircularThrobber`、`UThrobber` | `UDreamThrobber` |
| `UListView`、`UListViewBase` | `UDreamListView` |
| 每一种 `…Slot` | `UDreamPanelSlot` |
| `UPanelWidget` | `UDreamPanelLayoutBase` |
| `UWidget` | `UDreamWidget` |
| `UWidgetBlueprintLibrary` | `UDreamUIWidgetLibrary` |
| `UWidgetLayoutLibrary` | `UDreamUILayoutLibrary` |
| `USlateBlueprintLibrary` | `UDreamUIWidgetGeometryLibrary` |
| `UCanvasPanel`、`UVerticalBox` 和其他面板 | 同名的 `UDreamLayoutContainer…` |

文件名以下划线开头的是**给测试用的数据**，而不是对照表：`_BlueprintWritableExceptions.json`
列出刻意只在构造时生效的旋钮，每个附理由（比如
`UDreamUIControl::Template`：树只在初始化时实例化一次，已经持有其部件的控件没法
换一棵树，而不扔掉接在旧树上的所有行为和绑定）。

<a id="the-suite-holds-them-both-ways"></a>

## 测试从两个方向守着它

自动化测试套件（`DreamGUI.UMGParity.*`）把这些表和 **UMG 自己的反射**对照：

- **从表到 UMG**：`adopt` 或 `map` 的目标必须在 `dreamClass` 上解析得到；`map` 必须写明
  目标；`reject` 必须给出一个别人能反驳的理由；没有结论的行是错误。这一半
  抓的是**这边改了名**——表里一行指向的东西这个插件已经没有了。
- **从 UMG 到表**：反射列出 UMG 类自己的面向蓝图的成员，表里没提到的每一个都是错误。这一半抓的是
  **引擎升级**——UMG 多了一个成员，就会多出一行并不存在的行。
- **`TheTablesShipWithThePlugin`**：表目录存在且非空。逐表的测试是枚举该
  目录生成的，所以空目录会让它什么都不跑就通过——这是这类守卫唯一一种没人察觉的失败方式。
- **`AKnobThePanelCanTurnIsOneABlueprintCanTurn`**：细节面板能编辑的属性，必须能从
  蓝图通过 setter 改，例外只能来自 `_BlueprintWritableExceptions.json`。控件的字段只在它组装自己的时候
  读一次，所以直接写字段改的是一个之后没人会再看的数。

测试套件本身见[自动化测试套件](../tools/tests.md)。

<a id="two-differences-worth-knowing-in-advance"></a>

## 两条事先值得知道的差别

**新旋钮的默认值是控件原本的行为，不是 UMG 的默认值**，这样既有内容不会动。
滚动框上的 `ScrollWhenFocusChanges` 在这里默认是 `AnimatedScroll`，UMG 是 `NoScroll`：这里的导航一直会把
目标滚进视野，而 `NoScroll` 会让每一个手柄驱动的列表都不再跟随焦点。表里会记下
每一个这样的情况：

```json
{"umg": "ScrollWhenFocusChanges", "kind": "property", "status": "adopt", "dream": "", "note": "Default AnimatedScroll, not UMG's NoScroll: navigation here has always revealed its target, and NoScroll would stop every existing gamepad-driven list from following focus."}
```

**外观住在控件的样式结构体里，行为住在控件上**，所以有几个 UMG 属性在这里低了一层：`EntrySpacing` 是
`Style.RowSpacing`，按钮的 `WidgetStyle` 是 `Style`（按 `StyleSource` 解析）。
见[样式与样式表](styles.md)。

<a id="docsreference-is-printed-from-reflection"></a>

## Docs/Reference 是从反射打印出来的

插件的 `Docs/Reference/` 是属性和函数参考，每个类一页，每页最后一节是这个类的
UMG 对照（读的就是上面那些表）。本站的[参考](../Reference/index.md)就是这些页面。它们
**不是手写的**；由一个 commandlet 从反射打印出来，所以不会落后于头文件：

```powershell
UnrealEditor-Cmd.exe <project>.uproject -run=DreamGUIReferenceDocs
UnrealEditor-Cmd.exe <project>.uproject -run=DreamGUIReferenceDocs -Out=<directory>
```

不带 `-Out` 时写进插件的 `Docs/Reference/`。

- 属性的描述就是头文件里它上面的注释——这也是为什么那条注释要写给用这个控件的人，
  而不是维护它的人。
- 每个属性一行写明它是否出现在细节面板、蓝图怎么读写（getter / setter 的名字）。
- 输出是确定性的——成员按声明顺序，其余排序——所以重新生成一页的 diff，就是 API 的 diff。
