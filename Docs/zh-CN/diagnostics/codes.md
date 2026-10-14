---
title: "诊断"
description: "诊断"
---

<a id="diagnostics"></a>

## 诊断

每条消息都有一个代码 `DUInnnn`，打印为 `File.dui(line,col): error DUI3001: …`。第一位数字表示是哪个阶段拒绝的：1 词法分析器，2 语法分析器，3 语义，4 值，5 构建树，6 编译进蓝图，7 写回。

| 代码 | 名称 | 含义 |
|---|---|---|
| DUI1001 | UnexpectedCharacter | 一个无法作为任何记号开头的字符。 |
| DUI1002 | UnterminatedString | 一个到达行尾仍没有闭合引号的字符串。 |
| DUI1003 | UnterminatedComment | 一个从未到达 `*/` 的 `/*`。 |
| DUI1004 | MalformedNumber | 一个无法读取的数字：两个小数点、一个结尾的点、粘上的单位（`24px`）。 |
| DUI1005 | MalformedHexColor | 一个位数不是 3、4、6 或 8 的颜色。 |
| DUI1006 | IdentifierTooLong | 一个比 `FName` 所能容纳的更长的名称。 |
| DUI1007 | AssetPathTooLong | 一个比 `FName` 所能容纳的更长的资产路径。 |
| DUI2001 | UnexpectedToken | 一个出现在语法需要别的东西的位置上的记号；消息会同时指出两者。 |
| DUI2002 | UnclosedBlock | 一个没有匹配 `}` 的 `{`。 |
| DUI2003 | UnclosedTuple | 一个没有匹配 `)` 的 `(`。 |
| DUI2004 | MissingNodeId | 一个既没有 id 也没有块的节点，或者一个带着 `(was: …)` 却没有名字的节点。 |
| DUI2005 | MissingPropertyValue | 一个没有 `=` 或箭头、或者后面什么都没有的属性。 |
| DUI2006 | MalformedRoot | 一个被编译成类的文件中没有根节点，或者有不止一个根节点。 |
| DUI2007 | MalformedClassDeclaration | `class` 写了两次、路径为空，或者写在节点里面。 |
| DUI2008 | MalformedWasClause | `(was: …)` 里装的不是恰好一个 id。 |
| DUI2009 | MalformedKeyOverride | `@key(…)` 里装的不是恰好一个字符串，或者跟在一个不是字符串的值后面。 |
| DUI2010 | MalformedLoopHeader | 一个不是 `<keyword> Var in Source` 或 `Source()` 的 `for` / `each` 头部。 |
| DUI2011 | MalformedBindingExpression | `<-`、`<->` 的右侧，或者一个 `emit` 的参数解析不成表达式。 |
| DUI2012 | ImportFailed | 一个无法兑现的 `use`：没有这个文件、读不出来、解析不过，或者形成了环。 |
| DUI2013 | NestingTooDeep | 嵌套深度超过 256 层的块或括号。 |
| DUI2014 | MalformedTimeline | 一个不合语法的 `timeline` 块或行。 |
| DUI2015 | MalformedUseDeclaration | `use … as` 没有名字、名字是关键字，或者类路径没有写 `as`。 |
| DUI2016 | MalformedPropsBlock | 一行不是 `Type Name` 或 `Type Name = value` 的 `props`，或者写在节点里的 `props`。 |
| DUI2017 | MalformedEventsBlock | 一个不是 `Name` 或 `Name(Type Param, …)` 的 `events` 条目，或者写在节点里的 `events`。 |
| DUI2018 | MalformedConditional | 一个缺少条件或块的 `if`、一个没有 `if` 的 `else`，或者一个装了控件以外东西的分支。 |
| DUI2019 | MalformedSlotDeclaration | 一个写了两次 `default` 的 slot，或者一个既声明又填充的块。 |
| DUI2020 | MalformedRows | 一个读不通的 `rows` 表：列清单不是名称、某一列写了两次、某一行的值个数不对。 |
| DUI2021 | MalformedViewModelsBlock | 一行不是 `Type Name`、`= new`、`= global ["Name"]` 或 `= parent ["Name"]` 的 `viewmodels`（包括空的 `""` 名称），或者写在节点里的 `viewmodels` 块。 |
| DUI3001 | DuplicateNodeId | 两个节点共用一个 id。 |
| DUI3002 | InvalidNodeId | 一个不是标识符、以数字开头，或者是关键字的 id。 |
| DUI3003 | UnknownNodeType | 一个不是标签、容器、别名、已注册控件或路径的类型。 |
| DUI3004 | UnknownStyle | `: Name` 指向这个文件看不到的样式。 |
| DUI3005 | DuplicateStyle | 同一个文件里的两个同名样式。 |
| DUI3006 | UnknownBehaviourClass | `+ Name` 指向控件无法携带的东西。 |
| DUI3007 | DuplicateSlotName_Retired | 已退役；两个同名 slot 现在报 DUI3001。 |
| DUI3008 | ShadowedLoopVariable | 一个遮蔽了外层循环变量的循环变量（警告）。 |
| DUI3009 | ParentRefusedChild | 一个不肯接受这个子节点的父节点：已经满了的内容控件，或者被填了两次的命名 slot。 |
| DUI3010 | RenameOldIdStillInUse | `(was: X)`，而 X 仍然是这个文件里的一个 id。 |
| DUI3011 | DuplicateWasId | 两个节点都声称 `(was: X)`。 |
| DUI3012 | SelfRename | 在名为 X 的节点上使用 `(was: X)`。 |
| DUI3013 | RenameGraphReferenceAmbiguous | 旧 id 同时也是另一个成员的名字，因此图表引用不会被迁移（警告）。 |
| DUI3014 | DuplicateResource | 两个同名的资源条目。 |
| DUI3015 | StyleCycle | 一个沿着基类链继承了自己的样式。 |
| DUI3016 | DuplicateTimeline | 两个同名的时间轴。 |
| DUI3017 | DuplicateComponentAlias | 两行 `use … as` 给出了同一个名字。 |
| DUI3018 | AliasShadowsBuiltIn | 一个与标签或容器同名的 `use … as` 名字。 |
| DUI3019 | DuplicateProp | 两行 `props` 声明了同一个名字。 |
| DUI3020 | DuplicateEvent | 两个同名的 `events` 条目，或者一个参数写了两次。 |
| DUI3021 | UnknownNamespace | `ns.Name`，其 `ns` 没有任何 `use … as ns` 声明。 |
| DUI3022 | MultipleDefaultSlots | 一个文件里有不止一个 `slot … default`。 |
| DUI3023 | DuplicateRowKey | 警告。一个 `rows` 表的两行，其第一个值构成了相同的 id；第二行的 id 随后会随顺序移动。 |
| DUI3024 | DuplicateViewModel | 两个同名的 `viewmodels` 条目。 |
| DUI4001 | UnknownProperty | 没有这个名称的属性；会给出最接近的一个作为建议。 |
| DUI4002 | UnknownPropertyPathSegment | 一个点路径，其头部能解析而尾部不能。 |
| DUI4003 | ValueTypeMismatch | 一个形状不可能是该属性类型的值。 |
| DUI4004 | TupleArityMismatch | 一个元素个数不对的元组。 |
| DUI4005 | UnknownEnumValue | 一个该属性的枚举没有声明的词。 |
| DUI4006 | PropertyNotWritable | 一个文本无法写入的属性：瞬态、已弃用、属于对象图的一部分、一个委托。 |
| DUI4007 | UnknownResource | `@Name` 指向不存在的资源条目。 |
| DUI4008 | ResourceTypeMismatch | 一个其值不是所声明类型的资源，或者一个非 `Asset` 的资源被当作节点类型使用。 |
| DUI5001 | AssetNotFound | 一个无法加载的资产或类路径。 |
| DUI5002 | NoVisualForProperty | 一个属于该节点类型不会创建的视觉元素的属性。 |
| DUI5003 | NoPanelSlotForProperty | 一行 slot 写在了一个其父节点不布局任何面板的节点上。 |
| DUI5004 | BindingFunctionNotFound | 一个该类没有的绑定函数，或者一个带参数的绑定函数。 |
| DUI5005 | BindingTargetHasNoSetter | 一个没有 setter 的绑定属性。 |
| DUI5006 | NotAUserWidgetClass | 一个由并非具体 DreamUI 用户控件的类所标注的节点。 |
| DUI5007 | LoopNotExpanded | 一个由调用方构建、却无处记录的循环，已被跳过（一条警告）。 |
| DUI5008 | BindingTargetNotSupported | 落在任何绑定都无法指名的东西上的绑定：一个面板槽、一个容器、结构体的一个字段。 |
| DUI5009 | NothingToBuild | 树没有根节点；解析器已说明原因。 |
| DUI5010 | EventNotFound | `X -> …`，其中 X 不是目标上的事件。 |
| DUI5011 | BindingExpressionUnsupported | 编译器无法转成函数的表达式。 |
| DUI5012 | EachMisplaced | 一个位于根上、嵌套在循环里、没有列表视图，或模板不是恰好一个的 `each`。 |
| DUI5013 | NodeReferenceNotFound | 一个作为值写下、却没有指向文件里任何节点的节点 id。 |
| DUI5014 | LoopBodyBindingUnsupported | 循环体里的一个表达式或一个 `<->`。 |
| DUI5015 | TimelineTargetNotFound | 一条没有指向任何节点的轨道路径。 |
| DUI5016 | TimelinePropertyNotAnimatable | 一个没有任何动画轨道能驱动的轨道属性。 |
| DUI5017 | UnknownEaseName | 缓动库里没有的一个 `ease` 名称。 |
| DUI5018 | ComponentAliasUnresolved | 一个没有指向任何类的别名：文件既没有 `class` 行也没有 Blueprint 用它，或者路径什么也加载不出来。 |
| DUI5019 | SlotFillOutsideComponent | `slot Name { … }` 填充一个位于并非组件实例的节点下的槽。 |
| DUI5020 | UnknownSlotToFill | 一个指向组件未声明槽的填充。 |
| DUI5021 | ForMisplaced | 一个位于根上、在另一个循环里、在容量固定的宿主里，或模板不是恰好一个的 `for`。 |
| DUI5022 | SecondLayoutContainer | 一个节点上的第二个布局容器。 |
| DUI5023 | MemberPathNotFound | 成员路径里的某一段，在它所在的类上不存在，或者 Blueprint 看不到（UnrealSharp 属性需要 `BlueprintReadOnly` 或 `BlueprintReadWrite`）。 |
| DUI5024 | MemberPathThroughNonObject | 一条越过非对象的值继续往下走的成员路径。 |
| DUI5025 | RouteOperatorMismatch | 单播委托上的 `+=`，或者多播事件上的 `=`。 |
| DUI6001 | SourceFileUnreadable | Blueprint 的 Source File 不存在或读不出来。 |
| DUI6002 | EmptyTree | 文件解析通过、却没有产生任何树。 |
| DUI6003 | ClassPathMismatch | `class` 行指向的是另一个 Blueprint、而不是正在编译的这个（一条警告）。 |
| DUI6004 | EventHandlerNotFound | `-> Handler` 指向一个类里没有的函数。 |
| DUI6005 | EventHandlerSignatureMismatch | 一个参数与事件不一致的处理函数。 |
| DUI6006 | EachSourceNotFound | 一个类里既没有同名函数、也没有同名变量的循环来源。 |
| DUI6007 | EachSourceNotObjectArray | 一个不是对象数组的循环来源。 |
| DUI6008 | PropTypeUnknown | 一个没有对应 Blueprint 引脚类型的 `props` 或 `events` 类型。 |
| DUI6009 | PropNameTaken | 一个已经被另一个类型不同的成员占用、或者与某个控件 id 相同的 `props` 名字。 |
| DUI6010 | EmitUnknownEvent | `emit Name` 一个这个文件没有声明的事件。 |
| DUI6011 | EmitArgumentMismatch | `emit` 的实参与事件参数不一致。 |
| DUI6012 | EmitRouteUnsupported | 一个无法生成处理函数的 `emit`：在循环体里，或者事件的参数 Blueprint 无法承载。 |
| DUI6013 | PropDefaultInvalid | 一个声明的类型容纳不了的 `props` 默认值。 |
| DUI6014 | EventNameTaken | 一个已经被类的另一个成员占用的 `events` 名字。 |
| DUI6015 | ViewModelClassUnknown | 一个找不到类、或者找到不止一个的 `viewmodels` 类型。 |
| DUI6016 | ViewModelNameTaken | 一个已经被类的另一个成员占用的 `viewmodels` 名字。 |
| DUI6017 | ViewModelSourceInvalid | 对抽象类写的 `= new`。 |
| DUI6018 | RouteMemberFunctionNotFound | `-> Path.Func` 的函数在对象的类上没有，或者 Blueprint 不能调用。 |
| DUI6019 | RouteArgumentMismatch | `-> Path.Func` 的实参和函数对不上，或者不带括号时，函数收的不是什么都不收、也不是事件发出的那些。 |
| DUI6020 | TwoWayTargetReadOnly | `<-> Path.Member` 的成员写不回去：只读，且没有 `Set<Member>`。 |
| DUI6021 | LoopItemMemberNotFound | `Item.Member`，而循环来源的元素类里没有它。 |
| DUI6022 | LoopItemRouteMismatch | `-> Item.Func`，而元素类里没有它，或者它的参数既不是什么都不收、也不是事件发出的那些。 |
| DUI6023 | SourceFileWritePending | 文档里的修改无法写入，所以拒绝编译旧文件。 |
| DUI7001 | PatchTargetNotFound | 一个在文件里没有归属的设计器修改：未知的节点、一个绑定的属性、一个没有块的槽。 |
| DUI7002 | SourceFileChangedUnderEdit | 文件在一个待处理的修改之下发生了变化；没有写入任何内容。 |
| DUI7003 | PatchValueNotRepresentable | 一个这门语言无法拼写的值（一个非有限数字）；它所在的行保持不变。 |
| DUI7004 | PatchSyntaxNotWritable | 一个对任何一行都无法拼写的东西的修改：由 `if` 决定的可见性、无法重写的简写。 |
| DUI7005 | PatchStyleComponentNotWritable | 一个对接点值的修改，而这个值来自样式的 `+` 行；把这一行加到节点上，或者修改样式。 |
