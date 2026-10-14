---
title: "诊断码"
description: "每一条 DUInnnn 诊断码：它意味着什么、如何产生、如何修复。"
---

DreamGUI 针对一个 `.dui` 给出的每一条消息都带有一个编码，`DUInnnn`，并以 `File.dui(line,col): error DUI3001: …` 的形式输出：文件、行和列（均从 1 开始计数）、严重级别、编码，以及一句指出问题文本的说明——例如 `Settings.dui(12,5): error DUI3001: 'Title' is already the id of the node on line 4`。当 Blueprint 编译时，它们会出现在其编译器结果中。编码是稳定的那一半：一个原因对应一个编码，同一个原因从两处到达仍保留同一个编码，测试断言的是编码而非措辞，因此措辞可以自由改进；已退役的编码保持退役，绝不会分配给其他任何事物。第一位数字说明是哪个阶段拒绝了它：1 词法分析器（不是标记的字符），2 解析器（无法组成语法的标记），3 含义（语法正确，但树不成立——重复的 id、未知的类型），4 值（值与它所写入的属性不匹配），5 构建树（反射拒绝了写入，或要写入的对象不存在），6 编译进 Blueprint（`.dui` 无法附加到其 Blueprint），7 写回（设计器无法将编辑写入文本）。

严重级别在诊断被引发的地方决定，而不是由其范围决定。大多数编码是错误，错误会导致编译失败——一个无法解析的文件会保留 Blueprint 之前的层级结构，而不是将其清空。少数是警告——DUI3008、DUI3013、DUI3023、DUI5007、DUI6003——此时文件可以编译，但仍有值得一看之处。VS Code 扩展报告相同的编码和相同的措辞：单个文件能自行确定的内容（词法分析器、括号、重复名称等）会在你输入时检查，其余内容在 Unreal 编辑器编译后通过 `DUI/.dui-diagnostics.json` 进入其 Problems 面板；编译器始终是权威。

<a id="the-seven-families"></a>

## 七个家族

| | 阶段 | 含义 | 编码 |
| --- | --- | --- | --- |
| [DUI1xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx) | 词法分析器 | 不是标记的字符序列。 | 7 |
| [DUI2xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx) | 解析器 | 无法组成语法的标记。 | 21 |
| [DUI3xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx) | 含义 | 语法正确，但树不成立：重复的 id、未知的类型。 | 24 |
| [DUI4xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx) | 值 | 值与它所写入的属性不匹配。 | 8 |
| [DUI5xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx) | 构建树 | 反射拒绝了写入，或目标对象不存在。 | 25 |
| [DUI6xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx) | 编译 | .dui 无法附加到其 Blueprint。 | 22 |
| [DUI7xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx) | 写回 | 设计器的编辑无法写入文本。 | 5 |

<a id="every-code"></a>

## 全部编码

| 编码 | 名称 | 严重级别 | 含义 |
| --- | --- | --- | --- |
| [DUI1001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1001) | `UnexpectedCharacter` | error | 不能作为任何标记开头的字符。 |
| [DUI1002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1002) | `UnterminatedString` | error | 到达行尾仍未遇到闭合引号的字符串。 |
| [DUI1003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1003) | `UnterminatedComment` | error | 始终没有到达 `*/` 的 `/*`。 |
| [DUI1004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1004) | `MalformedNumber` | error | 无法读取的数字：两个小数点、末尾的点、粘附的单位（`24px`）。 |
| [DUI1005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1005) | `MalformedHexColor` | error | 位数不是 3、4、6 或 8 的颜色。 |
| [DUI1006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1006) | `IdentifierTooLong` | error | 超出 `FName` 所能容纳长度的名称。 |
| [DUI1007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1007) | `AssetPathTooLong` | error | 超出 `FName` 所能容纳长度的资产路径。 |
| [DUI2001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2001) | `UnexpectedToken` | error | 语法期望其他内容处却出现的标记；消息会同时指出两者。 |
| [DUI2002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2002) | `UnclosedBlock` | error | 没有匹配 `}` 的 `{`。 |
| [DUI2003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2003) | `UnclosedTuple` | error | 没有匹配 `)` 的 `(`。 |
| [DUI2004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2004) | `MissingNodeId` | error | 既没有 id 也没有块的节点，或带有 `(was: …)` 的未命名节点。 |
| [DUI2005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2005) | `MissingPropertyValue` | error | 没有 `=` 或箭头的属性，或其后面没有任何内容的属性。 |
| [DUI2006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2006) | `MalformedRoot` | error | 编译进类的文件中没有根节点，或有多个根节点。 |
| [DUI2007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2007) | `MalformedClassDeclaration` | error | `class` 出现两次、路径为空，或位于节点内部。 |
| [DUI2008](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2008) | `MalformedWasClause` | error | `(was: …)` 包含的不是一个 id。 |
| [DUI2009](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2009) | `MalformedKeyOverride` | error | `@key(…)` 包含的不是一个字符串，或位于不是字符串的值之后。 |
| [DUI2010](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2010) | `MalformedLoopHeader` | error | 不是 `<keyword> Var in Source` 或 `Source()` 的 `for` / `each` 头部。 |
| [DUI2011](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2011) | `MalformedBindingExpression` | error | `<-`、`<->` 的右侧或 `emit` 的参数无法解析。 |
| [DUI2012](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2012) | `ImportFailed` | error | 无法兑现的 `use`：文件不存在、无法读取、无法解析，或存在循环。 |
| [DUI2013](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2013) | `NestingTooDeep` | error | 嵌套深度超过 256 的块或括号。 |
| [DUI2014](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2014) | `MalformedTimeline` | error | 不符合语法的 `timeline` 块或行。 |
| [DUI2015](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2015) | `MalformedUseDeclaration` | error | 没有名称的 `use … as`、以关键字作为名称，或没有 `as` 的类路径。 |
| [DUI2016](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2016) | `MalformedPropsBlock` | error | 不是 `Type Name` 或 `Type Name = value` 的 `props` 行，或位于节点内部的 `props`。 |
| [DUI2017](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2017) | `MalformedEventsBlock` | error | 不是 `Name` 或 `Name(Type Param, …)` 的 `events` 条目，或位于节点内部的 `events`。 |
| [DUI2018](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2018) | `MalformedConditional` | error | 没有条件或块的 `if`、没有 `if` 的 `else`，或包含不是控件内容的分支。 |
| [DUI2019](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2019) | `MalformedSlotDeclaration` | error | 出现两次 `default` 的 slot，或既声明又填充的块。 |
| [DUI2020](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2020) | `MalformedRows` | error | 无法读取的 `rows` 表：列清单不是名称、某一列出现两次、某行的值数量不对。 |
| [DUI2021](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2021) | `MalformedViewModelsBlock` | error | 不是 `Type Name`、`= new`、`= global ["Name"]` 或 `= parent ["Name"]` 的 `viewmodels` 行（包括为空的 `""` 名称），或位于节点内部的 `viewmodels` 块。 |
| [DUI3001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3001) | `DuplicateNodeId` | error | 两个节点共享同一个 id。 |
| [DUI3002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3002) | `InvalidNodeId` | error | 不是标识符、以数字开头或是关键字的 id。 |
| [DUI3003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3003) | `UnknownNodeType` | error | 不是标签、容器、别名、已注册控件或路径的类型。 |
| [DUI3004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3004) | `UnknownStyle` | 错误 | 这个文件看不到的、名为 `: Name` 的样式。 |
| [DUI3005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3005) | `DuplicateStyle` | 错误 | 同一个文件里的两个同名样式。 |
| [DUI3006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3006) | `UnknownBehaviourClass` | 错误 | 一个指向控件无法携带的东西的 `+ Name`。 |
| [DUI3007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3007) | `DuplicateSlotName_Retired` | 错误 | 已退役；两个同名 slot 现在报 DUI3001。 |
| [DUI3008](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3008) | `ShadowedLoopVariable` | 警告 | 一个遮蔽了外层循环变量的循环变量（警告）。 |
| [DUI3009](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3009) | `ParentRefusedChild` | 错误 | 一个不肯接受这个子节点的父节点：已经满了的内容控件，或者被填了两次的命名 slot。 |
| [DUI3010](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3010) | `RenameOldIdStillInUse` | 错误 | 一个 `(was: X)`，而 X 仍然是这个文件里的一个 id。 |
| [DUI3011](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3011) | `DuplicateWasId` | 错误 | 两个节点都声称 `(was: X)`。 |
| [DUI3012](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3012) | `SelfRename` | 错误 | 一个写在名为 X 的节点自己身上的 `(was: X)`。 |
| [DUI3013](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3013) | `RenameGraphReferenceAmbiguous` | 警告 | 这个旧 id 同时是另一个成员的名字，因此图表引用没有被迁移（警告）。 |
| [DUI3014](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3014) | `DuplicateResource` | 错误 | 两个同名的资源条目。 |
| [DUI3015](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3015) | `StyleCycle` | 错误 | 一个沿着基类链继承了自己的样式。 |
| [DUI3016](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3016) | `DuplicateTimeline` | 错误 | 两个同名的时间轴。 |
| [DUI3017](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3017) | `DuplicateComponentAlias` | 错误 | 两行 `use … as` 给出同一个名字。 |
| [DUI3018](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3018) | `AliasShadowsBuiltIn` | 错误 | 一个与标签或容器同名的 `use … as` 名字。 |
| [DUI3019](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3019) | `DuplicateProp` | 错误 | 两行 `props` 声明同一个名字。 |
| [DUI3020](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3020) | `DuplicateEvent` | 错误 | 两个同名的 `events` 条目，或者一个条目里声明了两次的参数。 |
| [DUI3021](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3021) | `UnknownNamespace` | 错误 | 一个 `ns.Name`，其 `ns` 没有被任何 `use … as ns` 声明过。 |
| [DUI3022](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3022) | `MultipleDefaultSlots` | 错误 | 一个文件里不止一个 `slot … default`。 |
| [DUI3023](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3023) | `DuplicateRowKey` | 警告 | 警告。一个 `rows` 表里有两行，其第一个值生成同一个 id；第二行的 id 随之跟着顺序走。 |
| [DUI3024](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3024) | `DuplicateViewModel` | 错误 | 两个同名的 `viewmodels` 条目。 |
| [DUI4001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4001) | `UnknownProperty` | 错误 | 没有这个名字的属性；消息会给出最接近的那个。 |
| [DUI4002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4002) | `UnknownPropertyPathSegment` | 错误 | 一条开头能解析、后面解析不了的带点路径。 |
| [DUI4003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4003) | `ValueTypeMismatch` | 错误 | 一个形状不可能成为该属性类型的值。 |
| [DUI4004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4004) | `TupleArityMismatch` | 错误 | 一个元素个数不对的元组。 |
| [DUI4005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4005) | `UnknownEnumValue` | 错误 | 一个属性的枚举没有声明的词。 |
| [DUI4006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4006) | `PropertyNotWritable` | 错误 | 一个文本写不了的属性：瞬态的、已弃用的、属于对象图的，或者是委托。 |
| [DUI4007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4007) | `UnknownResource` | 错误 | 一个没有指向任何资源条目的 `@Name`。 |
| [DUI4008](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4008) | `ResourceTypeMismatch` | 错误 | 一个值与声明的类型不符的资源，或者一个被当作节点类型、却不是 `Asset` 的资源。 |
| [DUI5001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5001) | `AssetNotFound` | 错误 | 一个加载不出来的资产或类路径。 |
| [DUI5002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5002) | `NoVisualForProperty` | 错误 | 一个属于视觉对象、而节点的类型并不创建那个视觉对象的属性。 |
| [DUI5003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5003) | `NoPanelSlotForProperty` | 错误 | 一个父节点不布局面板、却写了 slot 行的节点。 |
| [DUI5004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5004) | `BindingFunctionNotFound` | 错误 | 一个类里没有的绑定函数，或者一个带参数的绑定函数。 |
| [DUI5005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5005) | `BindingTargetHasNoSetter` | 错误 | 一个没有 setter 的被绑定属性。 |
| [DUI5006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5006) | `NotAUserWidgetClass` | 错误 | 一个以不是具体 DreamUI 用户控件的类作类型的节点。 |
| [DUI5007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5007) | `LoopNotExpanded` | 警告 | 一个调用方没有地方记录、因而被跳过的循环（警告）。 |
| [DUI5008](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5008) | `BindingTargetNotSupported` | 错误 | 一个落在任何绑定都无法指名的东西上的绑定：面板 slot、布局容器，或者结构体里的字段。 |
| [DUI5009](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5009) | `NothingToBuild` | 错误 | 这棵树没有根节点；解析器说明了原因。 |
| [DUI5010](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5010) | `EventNotFound` | 错误 | 一个 `X -> …`，而 X 不是目标上的事件。 |
| [DUI5011](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5011) | `BindingExpressionUnsupported` | 错误 | 一个编译器无法转成函数的表达式。 |
| [DUI5012](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5012) | `EachMisplaced` | 错误 | 一个放在根上的 `each`：嵌套在另一个循环里、宿主没有列表视图，或者不是恰好一个模板。 |
| [DUI5013](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5013) | `NodeReferenceNotFound` | 错误 | 一个作为值写下、却没有指向文件里任何节点的节点 id。 |
| [DUI5014](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5014) | `LoopBodyBindingUnsupported` | 错误 | 一个写在循环体里的表达式或 `<->`。 |
| [DUI5015](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5015) | `TimelineTargetNotFound` | 错误 | 一条没有指向任何节点的轨道路径。 |
| [DUI5016](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5016) | `TimelinePropertyNotAnimatable` | 错误 | 一个没有任何动画轨道能驱动的轨道属性。 |
| [DUI5017](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5017) | `UnknownEaseName` | 错误 | 一个缓动库里没有的 `ease` 名字。 |
| [DUI5018](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5018) | `ComponentAliasUnresolved` | 错误 | 一个没有指向任何类的别名：文件既没有 `class` 行也没有 Blueprint 用它，或者路径什么也加载不出来。 |
| [DUI5019](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5019) | `SlotFillOutsideComponent` | 错误 | 一个 `slot Name { … }` 填充了非组件实例下的 slot。 |
| [DUI5020](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5020) | `UnknownSlotToFill` | 错误 | 一个指向组件没有声明的 slot 的填充。 |
| [DUI5021](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5021) | `ForMisplaced` | 错误 | 一个放在根上的 `for`：在另一个循环里、在容量固定的宿主里，或者不是恰好一个模板。 |
| [DUI5022](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5022) | `SecondLayoutContainer` | 错误 | 一个节点上的第二个布局容器。 |
| [DUI5023](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5023) | `MemberPathNotFound` | 错误 | 成员路径里的某一段，在它所在的类上找不到，或者 Blueprint 看不到（UnrealSharp 属性需要 `BlueprintReadOnly` 或 `BlueprintReadWrite`）。 |
| [DUI5024](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5024) | `MemberPathThroughNonObject` | 错误 | 一条越过非对象的值继续往下走的成员路径。 |
| [DUI5025](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5025) | `RouteOperatorMismatch` | 错误 | 单播委托上的 `+=`，或者多播事件上的 `=`。 |
| [DUI6001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6001) | `SourceFileUnreadable` | 错误 | Blueprint 的 Source File 指向的文件不存在或读不出来。 |
| [DUI6002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6002) | `EmptyTree` | 错误 | 文件解析通过，却没有产生任何树。 |
| [DUI6003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6003) | `ClassPathMismatch` | 警告 | `class` 行指向的是另一个蓝图，而不是正在编译的那个（警告）。 |
| [DUI6004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6004) | `EventHandlerNotFound` | 错误 | `-> Handler` 指向一个类上没有的函数。 |
| [DUI6005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6005) | `EventHandlerSignatureMismatch` | 错误 | 一个参数不是事件参数的处理函数。 |
| [DUI6006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6006) | `EachSourceNotFound` | 错误 | 一个类里既没有同名函数、也没有同名变量的循环来源。 |
| [DUI6007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6007) | `EachSourceNotObjectArray` | 错误 | 一个不是对象数组的循环来源。 |
| [DUI6008](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6008) | `PropTypeUnknown` | 错误 | 一个 `props` 或 `events` 类型没有对应的蓝图引脚。 |
| [DUI6009](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6009) | `PropNameTaken` | 错误 | 一个 `props` 名字已被另一个类型不同的成员占用，或者与某个控件 id 相同。 |
| [DUI6010](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6010) | `EmitUnknownEvent` | 错误 | `emit Name` 一个这个文件没有声明的事件。 |
| [DUI6011](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6011) | `EmitArgumentMismatch` | 错误 | `emit` 实参不是事件的参数。 |
| [DUI6012](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6012) | `EmitRouteUnsupported` | 错误 | 一个无法生成处理函数的 `emit`：在循环体里，或者事件的参数蓝图无法承载。 |
| [DUI6013](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6013) | `PropDefaultInvalid` | 错误 | 一个 `props` 默认值其类型容纳不了。 |
| [DUI6014](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6014) | `EventNameTaken` | 错误 | 一个 `events` 名字已被类的另一个成员占用。 |
| [DUI6015](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6015) | `ViewModelClassUnknown` | 错误 | 一个 `viewmodels` 类型找不到类，或者找到不止一个。 |
| [DUI6016](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6016) | `ViewModelNameTaken` | 错误 | 一个 `viewmodels` 名字已被类的另一个成员占用。 |
| [DUI6017](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6017) | `ViewModelSourceInvalid` | 错误 | 对抽象类的 `= new`。 |
| [DUI6018](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6018) | `RouteMemberFunctionNotFound` | 错误 | `-> Path.Func` 其函数在对象的类上没有，或者 Blueprint 不能调用。 |
| [DUI6019](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6019) | `RouteArgumentMismatch` | 错误 | `-> Path.Func` 其实参与函数对不上，或者在没有括号时其函数不是什么都不接收、也不是事件发出的那些。 |
| [DUI6020](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6020) | `TwoWayTargetReadOnly` | 错误 | `<-> Path.Member` 其成员写不回去：只读，且没有 `Set<Member>`。 |
| [DUI6021](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6021) | `LoopItemMemberNotFound` | 错误 | `Item.Member` 循环来源的元素类上没有。 |
| [DUI6022](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6022) | `LoopItemRouteMismatch` | 错误 | `-> Item.Func` 元素类上没有，或者其参数既不是什么都不接收、也不是事件的那些。 |
| [DUI7001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7001) | `PatchTargetNotFound` | 错误 | 设计器的一次编辑在文件里找不到落脚处：未知的节点、被绑定的属性、没有块的 slot。 |
| [DUI7002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7002) | `SourceFileChangedUnderEdit` | 错误 | 文件在一次待写入的编辑底下被改动了，于是什么都没有写入。 |
| [DUI7003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7003) | `PatchValueNotRepresentable` | 错误 | 一个这门语言写不出来的值（非有限的数），它所在的那一行保持原样。 |
| [DUI7004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7004) | `PatchSyntaxNotWritable` | 错误 | 对没有哪一行写着它的东西的一次编辑：由 `if` 决定的可见性，或者无法改写的简写。 |
| [DUI7005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7005) | `PatchStyleComponentNotWritable` | 错误 | 对样式 `+` 行给节点的值的一次编辑；要么给节点加上那一行，要么去改样式。 |
