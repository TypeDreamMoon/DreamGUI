---
title: "诊断码"
description: "每一条 DUInnnn 诊断：它说的是什么、怎么会出现、怎么改。"
---

DreamGUI 处理 `.dui` 时说出的每一条消息都带一个 `DUInnnn` 码，打印成 `File.dui(line,col): error DUI3001: …` 的样子：文件、行、列（都从 1 开始数）、严重级别、码，最后是一句点名出错文本的说明，比如 `Settings.dui(12,5): error DUI3001: 'Title' is already the id of the node on line 4`。编译时它们出现在 Blueprint 的编译结果里。码是稳定的那一半：一个原因一个码，同一个原因从两个地方报出来仍是同一个码，测试断言的是码而不是措辞，所以措辞可以继续改进；退役的码永远退役，不会被重新分配。首位数字说明是哪个阶段拒绝的：1 是词法（一串字符不是任何记号），2 是语法（记号拼不成语法），3 是语义（合乎语法，但树立不住，比如 id 重复、类型未知），4 是值（值放不进它所写的那个属性），5 是构建控件树（反射拒绝了写入，或者要写的对象不存在），6 是编译进 Blueprint（`.dui` 挂不到它的 Blueprint 上），7 是写回（设计器没法把一次编辑写进文本）。

严重级别由报出它的那个地方决定，而不是由号段决定：大多数码是错误（error），会让这次编译失败，而一个解析不过的文件不会清空 Blueprint 原有的层级；少数是警告（warning），比如 DUI3008、DUI3013、DUI3023、DUI5007、DUI6003，文件照常编译，只是有一处值得你看一眼。VS Code 扩展报的是同一套码、同样的措辞：单个文件就能确定的问题（词法、括号、重复的名字等）在你打字时由扩展自己检查，其余的在 Unreal 编辑器编译之后经由 `DUI/.dui-diagnostics.json` 送进 Problems 面板，编译器始终是最终的裁决者。

<a id="七个家族"></a>

## 七个家族

| | 阶段 | 说明 | 码数 |
| --- | --- | --- | --- |
| [DUI1xxx](codes.md#dui1xxx) | 词法 | 一串字符构不成任何记号。 | 7 |
| [DUI2xxx](codes.md#dui2xxx) | 语法 | 记号拼不成语法。 | 21 |
| [DUI3xxx](codes.md#dui3xxx) | 语义 | 语法没错，但这棵树对不上：重复的 id、未知的类型。 | 24 |
| [DUI4xxx](codes.md#dui4xxx) | 取值 | 值和它写在的属性对不上。 | 8 |
| [DUI5xxx](codes.md#dui5xxx) | 构建 | 反射拒绝了写入，或者目标对象不存在。 | 25 |
| [DUI6xxx](codes.md#dui6xxx) | 编译 | .dui 挂不到它的 Blueprint 上。 | 22 |
| [DUI7xxx](codes.md#dui7xxx) | 写回 | 设计器的改动没法写回文本。 | 5 |

<a id="全部诊断码"></a>

## 全部诊断码

| 码 | 枚举名 | 级别 | 含义 |
| --- | --- | --- | --- |
| [DUI1001](codes.md#dui1001) | `UnexpectedCharacter` | 错误 | 一个不能作为任何记号开头的字符。 |
| [DUI1002](codes.md#dui1002) | `UnterminatedString` | 错误 | 一个在行尾之前没有遇到右引号的字符串。 |
| [DUI1003](codes.md#dui1003) | `UnterminatedComment` | 错误 | 一个直到文件末尾都没有遇到 `*/` 的 `/*`。 |
| [DUI1004](codes.md#dui1004) | `MalformedNumber` | 错误 | 一个读不成数字的数字：两个小数点、末尾的点，或者粘了一个单位（`24px`）。 |
| [DUI1005](codes.md#dui1005) | `MalformedHexColor` | 错误 | 一个十六进制位数不是 3、4、6 或 8 的 `#` 颜色。 |
| [DUI1006](codes.md#dui1006) | `IdentifierTooLong` | 错误 | 一个比 `FName` 能容纳的更长的名字。 |
| [DUI1007](codes.md#dui1007) | `AssetPathTooLong` | 错误 | 一个比 `FName` 能容纳的更长的资产路径。 |
| [DUI2001](codes.md#dui2001) | `UnexpectedToken` | 错误 | 一个出现在语法期待别的东西的位置上的记号；消息会写出期待什么、遇到了什么。 |
| [DUI2002](codes.md#dui2002) | `UnclosedBlock` | 错误 | 一个没有匹配 `}` 的 `{`。 |
| [DUI2003](codes.md#dui2003) | `UnclosedTuple` | 错误 | 一个没有匹配 `)` 的 `(`。 |
| [DUI2004](codes.md#dui2004) | `MissingNodeId` | 错误 | 一个既没有 id 也没有块的节点，或者一个带着 `(was: …)` 却没写 id 的节点。 |
| [DUI2005](codes.md#dui2005) | `MissingPropertyValue` | 错误 | 一个后面没有 `=` 或箭头、或者 `=`、箭头后面什么都没有的属性。 |
| [DUI2006](codes.md#dui2006) | `MalformedRoot` | 错误 | 一个被编译成类、却没有根节点或者有不止一个根节点的文件。 |
| [DUI2007](codes.md#dui2007) | `MalformedClassDeclaration` | 错误 | 一个写了两次、路径为空或者写在节点里的 `class`。 |
| [DUI2008](codes.md#dui2008) | `MalformedWasClause` | 错误 | 一个括号里不是恰好一个 id 的 `(was: …)`。 |
| [DUI2009](codes.md#dui2009) | `MalformedKeyOverride` | 错误 | 一个括号里不是恰好一个字符串、或者跟在非字符串值后面的 `@key(…)`。 |
| [DUI2010](codes.md#dui2010) | `MalformedLoopHeader` | 错误 | 一个写法不是 `<keyword> Var in Source` 或 `Source()` 的 `for` / `each` 头部。 |
| [DUI2011](codes.md#dui2011) | `MalformedBindingExpression` | 错误 | 一个解析不成表达式的 `<-`、`<->` 右边或者 `emit` 实参。 |
| [DUI2012](codes.md#dui2012) | `ImportFailed` | 错误 | 一个无法兑现的 `use`：文件不存在、读不出来、解析不过，或者导入形成了环。 |
| [DUI2013](codes.md#dui2013) | `NestingTooDeep` | 错误 | 一个嵌套深度超过 256 层的块或括号。 |
| [DUI2014](codes.md#dui2014) | `MalformedTimeline` | 错误 | 一个不合语法的 `timeline` 块或其中的一行。 |
| [DUI2015](codes.md#dui2015) | `MalformedUseDeclaration` | 错误 | 一个 `as` 后面没有名字、名字是关键字，或者类路径没写 `as` 的 `use`。 |
| [DUI2016](codes.md#dui2016) | `MalformedPropsBlock` | 错误 | 一个不是 `Type Name` 或 `Type Name = value` 的 `props` 行，或者写在了节点里的 `props`。 |
| [DUI2017](codes.md#dui2017) | `MalformedEventsBlock` | 错误 | 一个不是 `Name` 或 `Name(Type Param, …)` 的 `events` 条目，或者写在了节点里的 `events`。 |
| [DUI2018](codes.md#dui2018) | `MalformedConditional` | 错误 | 一个缺了条件或块的 `if`、前面没有 `if` 的 `else`，或者一个装了控件以外东西的分支。 |
| [DUI2019](codes.md#dui2019) | `MalformedSlotDeclaration` | 错误 | 一个写了两次 `default` 的 slot，或者一个既声明又填充的 slot 块。 |
| [DUI2020](codes.md#dui2020) | `MalformedRows` | 错误 | 一个读不通的 `rows` 表：列清单不是属性名、某一列写了两次，或者某一行的值个数不对。 |
| [DUI2021](codes.md#dui2021) | `MalformedViewModelsBlock` | 错误 | 一行读不通的 `viewmodels`，或者一个写在节点里的 `viewmodels` 块。 |
| [DUI3001](codes.md#dui3001) | `DuplicateNodeId` | 错误 | 两个用了同一个 id 的节点。 |
| [DUI3002](codes.md#dui3002) | `InvalidNodeId` | 错误 | 一个不是标识符、以数字开头或者是关键字的 id。 |
| [DUI3003](codes.md#dui3003) | `UnknownNodeType` | 错误 | 一个既不是标签、容器、别名、注册控件，也不是路径的节点类型。 |
| [DUI3004](codes.md#dui3004) | `UnknownStyle` | 错误 | 一个指向这个文件看不到的样式的 `: Name`。 |
| [DUI3005](codes.md#dui3005) | `DuplicateStyle` | 错误 | 同一个文件里的两个同名样式。 |
| [DUI3006](codes.md#dui3006) | `UnknownBehaviourClass` | 错误 | 一个指向控件无法携带的东西的 `+ Name`。 |
| [DUI3007](codes.md#dui3007) | `DuplicateSlotName_Retired` | 错误 | 这个码已经退役、从不报出，两个同名 slot 现在报 DUI3001。 |
| [DUI3008](codes.md#dui3008) | `ShadowedLoopVariable` | 警告 | 一个遮蔽了外层循环变量的循环变量（警告）。 |
| [DUI3009](codes.md#dui3009) | `ParentRefusedChild` | 错误 | 一个不肯接受这个子节点的父节点：已经满了的内容控件，或者被填了两次的命名 slot。 |
| [DUI3010](codes.md#dui3010) | `RenameOldIdStillInUse` | 错误 | 一个 `(was: X)`，而 X 仍然是这个文件里某个节点的 id。 |
| [DUI3011](codes.md#dui3011) | `DuplicateWasId` | 错误 | 两个都声称 `(was: X)` 的节点。 |
| [DUI3012](codes.md#dui3012) | `SelfRename` | 错误 | 一个写在节点 X 自己身上的 `(was: X)`。 |
| [DUI3013](codes.md#dui3013) | `RenameGraphReferenceAmbiguous` | 警告 | 一个同时是别的成员名字的旧 id，因此图表引用没有被迁移（警告）。 |
| [DUI3014](codes.md#dui3014) | `DuplicateResource` | 错误 | 两个同名的资源条目。 |
| [DUI3015](codes.md#dui3015) | `StyleCycle` | 错误 | 一个沿着基类链继承了自己的样式。 |
| [DUI3016](codes.md#dui3016) | `DuplicateTimeline` | 错误 | 两个同名的时间轴。 |
| [DUI3017](codes.md#dui3017) | `DuplicateComponentAlias` | 错误 | 两行给出同一个名字的 `use … as`。 |
| [DUI3018](codes.md#dui3018) | `AliasShadowsBuiltIn` | 错误 | 一个与标签或容器同名的 `use … as` 名字。 |
| [DUI3019](codes.md#dui3019) | `DuplicateProp` | 错误 | 两行声明同一个名字的 `props`。 |
| [DUI3020](codes.md#dui3020) | `DuplicateEvent` | 错误 | 两个同名的 `events` 条目，或者一个条目里声明了两次的参数。 |
| [DUI3021](codes.md#dui3021) | `UnknownNamespace` | 错误 | 一个 `ns` 没有被任何 `use … as ns` 声明过的 `ns.Name`。 |
| [DUI3022](codes.md#dui3022) | `MultipleDefaultSlots` | 错误 | 一个文件里不止一个 `slot … default`。 |
| [DUI3023](codes.md#dui3023) | `DuplicateRowKey` | 警告 | 一个 `rows` 表里第一个值生成了同一个 id 的两行（警告）。 |
| [DUI3024](codes.md#dui3024) | `DuplicateViewModel` | 错误 | 两个同名的 `viewmodels` 条目。 |
| [DUI4001](codes.md#dui4001) | `UnknownProperty` | 错误 | 目标对象上没有这个名字的属性；消息会给出最接近的那个。 |
| [DUI4002](codes.md#dui4002) | `UnknownPropertyPathSegment` | 错误 | 一条开头能解析、后面解析不了的带点路径。 |
| [DUI4003](codes.md#dui4003) | `ValueTypeMismatch` | 错误 | 一个形状不可能成为该属性类型的值。 |
| [DUI4004](codes.md#dui4004) | `TupleArityMismatch` | 错误 | 一个元素个数不对的元组。 |
| [DUI4005](codes.md#dui4005) | `UnknownEnumValue` | 错误 | 一个属性的枚举没有声明的词。 |
| [DUI4006](codes.md#dui4006) | `PropertyNotWritable` | 错误 | 一个文本写不了的属性：瞬态的、已弃用的、属于对象图的，或者是委托。 |
| [DUI4007](codes.md#dui4007) | `UnknownResource` | 错误 | 一个没有指向任何资源条目的 `@Name`。 |
| [DUI4008](codes.md#dui4008) | `ResourceTypeMismatch` | 错误 | 一个值与声明的类型不符的资源，或者一个被当作节点类型、却不是 `Asset` 的资源。 |
| [DUI5001](codes.md#dui5001) | `AssetNotFound` | 错误 | 一个加载不出来的资产或类路径。 |
| [DUI5002](codes.md#dui5002) | `NoVisualForProperty` | 错误 | 一个属于视觉对象、而节点的类型并不创建那个视觉对象的属性。 |
| [DUI5003](codes.md#dui5003) | `NoPanelSlotForProperty` | 错误 | 一个父节点不布局面板、却写了 slot 行的节点。 |
| [DUI5004](codes.md#dui5004) | `BindingFunctionNotFound` | 错误 | 一个类里没有的绑定函数，或者一个带参数的绑定函数。 |
| [DUI5005](codes.md#dui5005) | `BindingTargetHasNoSetter` | 错误 | 一个没有 setter 的被绑定属性。 |
| [DUI5006](codes.md#dui5006) | `NotAUserWidgetClass` | 错误 | 一个以不是具体 DreamUI 用户控件的类作类型的节点。 |
| [DUI5007](codes.md#dui5007) | `LoopNotExpanded` | 警告 | 一个调用方没有地方记录、因而被跳过的循环（警告）。 |
| [DUI5008](codes.md#dui5008) | `BindingTargetNotSupported` | 错误 | 一个落在任何绑定都无法指名的东西上的绑定：面板 slot、布局容器，或者结构体里的字段。 |
| [DUI5009](codes.md#dui5009) | `NothingToBuild` | 错误 | 一个没有根节点、因而无从构建的树。 |
| [DUI5010](codes.md#dui5010) | `EventNotFound` | 错误 | 一个 `X -> …`，而 X 不是目标上的事件。 |
| [DUI5011](codes.md#dui5011) | `BindingExpressionUnsupported` | 错误 | 一个编译器无法转成函数的表达式。 |
| [DUI5012](codes.md#dui5012) | `EachMisplaced` | 错误 | 一个放错了地方的 `each`：在根上、嵌套在另一个循环里、宿主没有列表视图，或者循环体不是恰好一个模板控件。 |
| [DUI5013](codes.md#dui5013) | `NodeReferenceNotFound` | 错误 | 一个作为值写下、却没有指向文件里任何节点的节点 id。 |
| [DUI5014](codes.md#dui5014) | `LoopBodyBindingUnsupported` | 错误 | 一个写在循环体里的表达式或 `<->`，或者一条通往别的对象的路由。 |
| [DUI5015](codes.md#dui5015) | `TimelineTargetNotFound` | 错误 | 一条路径没有指向任何节点的轨道。 |
| [DUI5016](codes.md#dui5016) | `TimelinePropertyNotAnimatable` | 错误 | 一个没有任何动画轨道能驱动的轨道属性。 |
| [DUI5017](codes.md#dui5017) | `UnknownEaseName` | 错误 | 一个缓动库里没有的 `ease` 名字。 |
| [DUI5018](codes.md#dui5018) | `ComponentAliasUnresolved` | 错误 | 一个没有指向任何类的组件别名：文件既没有 `class` 行也没有 Blueprint 用它，或者路径什么也加载不出来。 |
| [DUI5019](codes.md#dui5019) | `SlotFillOutsideComponent` | 错误 | 一个写在非组件实例下、用来填充 slot 的 `slot Name { … }`。 |
| [DUI5020](codes.md#dui5020) | `UnknownSlotToFill` | 错误 | 一个指向组件没有声明的 slot 的填充。 |
| [DUI5021](codes.md#dui5021) | `ForMisplaced` | 错误 | 一个放错了地方的 `for`：在根上、在另一个循环里、在容量固定的宿主里，或者循环体不是恰好一个模板。 |
| [DUI5022](codes.md#dui5022) | `SecondLayoutContainer` | 错误 | 一个节点上的第二个布局容器。 |
| [DUI5023](codes.md#dui5023) | `MemberPathNotFound` | 错误 | 成员路径里的某一段，在它所在的类上找不到，或者 Blueprint 看不到。 |
| [DUI5024](codes.md#dui5024) | `MemberPathThroughNonObject` | 错误 | 一条越过非对象的值继续往下走的成员路径。 |
| [DUI5025](codes.md#dui5025) | `RouteOperatorMismatch` | 错误 | 事件接不住的路由运算符：单播委托上的 `+=`，或者多播事件上的 `=`。 |
| [DUI6001](codes.md#dui6001) | `SourceFileUnreadable` | 错误 | Blueprint 的 Source File 指向一个不存在或读不出来的文件。 |
| [DUI6002](codes.md#dui6002) | `EmptyTree` | 错误 | 一个解析通过、却没有产生任何树的文件。 |
| [DUI6003](codes.md#dui6003) | `ClassPathMismatch` | 警告 | 一个指向了另一个 Blueprint、而不是正在编译的这个的 `class` 行（警告）。 |
| [DUI6004](codes.md#dui6004) | `EventHandlerNotFound` | 错误 | 一个 `-> Handler`，而类里没有这个函数。 |
| [DUI6005](codes.md#dui6005) | `EventHandlerSignatureMismatch` | 错误 | 一个参数与事件发出的参数不一致的处理函数。 |
| [DUI6006](codes.md#dui6006) | `EachSourceNotFound` | 错误 | 一个类里既没有同名函数、也没有同名变量的循环来源。 |
| [DUI6007](codes.md#dui6007) | `EachSourceNotObjectArray` | 错误 | 一个提供的不是对象数组的循环来源。 |
| [DUI6008](codes.md#dui6008) | `PropTypeUnknown` | 错误 | 一个没有对应 Blueprint 引脚类型的 `props` 或 `events` 类型。 |
| [DUI6009](codes.md#dui6009) | `PropNameTaken` | 错误 | 一个已经被另一个类型不同的成员占用、或者与某个控件 id 相同的 `props` 名字。 |
| [DUI6010](codes.md#dui6010) | `EmitUnknownEvent` | 错误 | 一个 `emit Name`，而这个文件没有声明这个事件。 |
| [DUI6011](codes.md#dui6011) | `EmitArgumentMismatch` | 错误 | 一个实参与事件参数不一致的 `emit`。 |
| [DUI6012](codes.md#dui6012) | `EmitRouteUnsupported` | 错误 | 一个无法生成处理函数的 `emit`：在循环体里，或者事件的参数 Blueprint 无法承载。 |
| [DUI6013](codes.md#dui6013) | `PropDefaultInvalid` | 错误 | 一个声明的类型容纳不了的 `props` 默认值。 |
| [DUI6014](codes.md#dui6014) | `EventNameTaken` | 错误 | 一个已经被类的另一个成员占用的 `events` 名字。 |
| [DUI6015](codes.md#dui6015) | `ViewModelClassUnknown` | 错误 | `viewmodels` 里的一个类型，找不到类，或者找到不止一个。 |
| [DUI6016](codes.md#dui6016) | `ViewModelNameTaken` | 错误 | 一个已经被类的另一个成员占用的 `viewmodels` 名字。 |
| [DUI6017](codes.md#dui6017) | `ViewModelSourceInvalid` | 错误 | 一个没法兑现的来源：对抽象类写 `= new`。 |
| [DUI6018](codes.md#dui6018) | `RouteMemberFunctionNotFound` | 错误 | `-> Path.Func` 的函数，在对象的类上没有，或者 Blueprint 不能调用。 |
| [DUI6019](codes.md#dui6019) | `RouteArgumentMismatch` | 错误 | `-> Path.Func` 的实参和函数对不上。 |
| [DUI6020](codes.md#dui6020) | `TwoWayTargetReadOnly` | 错误 | `<-> Path.Member` 的成员写不回去。 |
| [DUI6021](codes.md#dui6021) | `LoopItemMemberNotFound` | 错误 | 循环体里一个元素类上没有的 `Item.Member`。 |
| [DUI6022](codes.md#dui6022) | `LoopItemRouteMismatch` | 错误 | 循环体里一个 `-> Item.Func`，元素类上没有这个函数，或者参数对不上。 |
| [DUI7001](codes.md#dui7001) | `PatchTargetNotFound` | 错误 | 设计器的一次编辑在文件里找不到落脚处：未知的节点、被绑定的属性、没有块的 slot。 |
| [DUI7002](codes.md#dui7002) | `SourceFileChangedUnderEdit` | 错误 | 文件在一次待写入的编辑底下被改动了，于是什么都没有写入。 |
| [DUI7003](codes.md#dui7003) | `PatchValueNotRepresentable` | 错误 | 一个这门语言写不出来的值（非有限的数），它所在的那一行保持原样。 |
| [DUI7004](codes.md#dui7004) | `PatchSyntaxNotWritable` | 错误 | 设计器要改的东西没有哪一行写着它：由 `if` 决定的可见性，或者无法改写的简写。 |
| [DUI7005](codes.md#dui7005) | `PatchStyleComponentNotWritable` | 错误 | 设计器要改的值来自样式的 `+` 行；要么给节点加上它自己的那一行，要么去改样式。 |
