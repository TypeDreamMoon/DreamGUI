---
title: "Diagnostics"
description: "Every DUInnnn diagnostic: what it means, how it happens, how to fix it."
---

Every message DreamGUI gives about a `.dui` carries a code, `DUInnnn`, and is printed as `File.dui(line,col): error DUI3001: …`: the file, the line and the column (both counted from 1), the severity, the code, and one sentence that names the offending text -- `Settings.dui(12,5): error DUI3001: 'Title' is already the id of the node on line 4`, for example. When a Blueprint compiles, they appear in its compiler results. The code is the stable half: one code per cause, the same cause reached from two places keeps one code, and tests assert the code rather than the wording, so the wording stays free to improve; a retired code stays retired and is never given to anything else. The first digit says which stage refused: 1 the lexer (characters that are not a token), 2 the parser (tokens that do not form the grammar), 3 meaning (grammatical, but the tree does not hold together -- a duplicate id, an unknown type), 4 values (a value that does not fit the property it is written on), 5 building the tree (reflection refused the write, or the object to write does not exist), 6 compiling into the Blueprint (the `.dui` could not be attached to its Blueprint), 7 writing back (the designer could not put an edit into the text).

Severity is decided where a diagnostic is raised, not by its range. Most codes are errors, and an error fails the compile -- a file that does not parse leaves the Blueprint's previous hierarchy where it was rather than blanking it. A few are warnings -- DUI3008, DUI3013, DUI3023, DUI5007, DUI6003 -- where the file compiles and something is still worth a look. The VS Code extension reports the same codes with the same wording: what one file settles on its own (the lexer, brackets, duplicate names and so on) it checks as you type, and the rest reaches its Problems panel through `DUI/.dui-diagnostics.json` after the Unreal editor compiles; the compiler stays the authority.

## The seven families

| | Stage | Meaning | Codes |
| --- | --- | --- | --- |
| [DUI1xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx) | Lexer | A character sequence that is not a token. | 7 |
| [DUI2xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx) | Parser | Tokens that do not form the grammar. | 21 |
| [DUI3xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx) | Meaning | Grammatical, but the tree does not hold together: a duplicate id, an unknown type. | 24 |
| [DUI4xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx) | Values | A value that does not fit the property it is written on. | 8 |
| [DUI5xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx) | Building the tree | Reflection refused the write, or the target object does not exist. | 25 |
| [DUI6xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx) | Compiling | The .dui could not be attached to its Blueprint. | 22 |
| [DUI7xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx) | Write-back | The designer's edit could not be put into the text. | 5 |

## Every code

| Code | Name | Severity | Meaning |
| --- | --- | --- | --- |
| [DUI1001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1001) | `UnexpectedCharacter` | error | A character that cannot begin any token. |
| [DUI1002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1002) | `UnterminatedString` | error | A string that reaches the end of its line without its closing quote. |
| [DUI1003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1003) | `UnterminatedComment` | error | A `/*` that never reaches `*/`. |
| [DUI1004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1004) | `MalformedNumber` | error | A number that cannot be read: two decimal points, a trailing dot, a unit glued on (`24px`). |
| [DUI1005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1005) | `MalformedHexColor` | error | A colour whose digit count is not 3, 4, 6 or 8. |
| [DUI1006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1006) | `IdentifierTooLong` | error | A name longer than an `FName` holds. |
| [DUI1007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui1xxx#dui1007) | `AssetPathTooLong` | error | An asset path longer than an `FName` holds. |
| [DUI2001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2001) | `UnexpectedToken` | error | A token where the grammar wants something else; the message names both. |
| [DUI2002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2002) | `UnclosedBlock` | error | A `{` with no matching `}`. |
| [DUI2003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2003) | `UnclosedTuple` | error | A `(` with no matching `)`. |
| [DUI2004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2004) | `MissingNodeId` | error | A node with neither an id nor a block, or an unnamed node with `(was: …)`. |
| [DUI2005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2005) | `MissingPropertyValue` | error | A property with no `=` or arrow, or nothing after it. |
| [DUI2006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2006) | `MalformedRoot` | error | No root node in a file compiled into a class, or more than one. |
| [DUI2007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2007) | `MalformedClassDeclaration` | error | `class` twice, with an empty path, or inside a node. |
| [DUI2008](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2008) | `MalformedWasClause` | error | `(was: …)` holding something other than one id. |
| [DUI2009](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2009) | `MalformedKeyOverride` | error | `@key(…)` holding something other than one string, or after a value that is not a string. |
| [DUI2010](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2010) | `MalformedLoopHeader` | error | A `for` / `each` header that is not `<keyword> Var in Source` or `Source()`. |
| [DUI2011](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2011) | `MalformedBindingExpression` | error | The right side of `<-`, `<->` or an `emit`'s arguments that do not parse. |
| [DUI2012](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2012) | `ImportFailed` | error | A `use` that could not be honoured: no such file, unreadable, does not parse, or a cycle. |
| [DUI2013](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2013) | `NestingTooDeep` | error | Blocks or parentheses nested deeper than 256. |
| [DUI2014](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2014) | `MalformedTimeline` | error | A `timeline` block or line that is not the grammar. |
| [DUI2015](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2015) | `MalformedUseDeclaration` | error | `use … as` without a name, a keyword as the name, or a class path without `as`. |
| [DUI2016](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2016) | `MalformedPropsBlock` | error | A `props` line that is not `Type Name` or `Type Name = value`, or `props` inside a node. |
| [DUI2017](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2017) | `MalformedEventsBlock` | error | An `events` entry that is not `Name` or `Name(Type Param, …)`, or `events` inside a node. |
| [DUI2018](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2018) | `MalformedConditional` | error | An `if` without its condition or block, an `else` with no `if`, or a branch holding something other than widgets. |
| [DUI2019](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2019) | `MalformedSlotDeclaration` | error | A slot with `default` twice, or a block that both declares and fills. |
| [DUI2020](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2020) | `MalformedRows` | error | A `rows` table that does not read: a column list that is not names, a column twice, a row with the wrong number of values. |
| [DUI2021](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2021) | `MalformedViewModelsBlock` | error | A `viewmodels` line that is not `Type Name`, `= new`, `= global ["Name"]` or `= parent ["Name"]` (an empty `""` name included), or a `viewmodels` block inside a node. |
| [DUI3001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3001) | `DuplicateNodeId` | error | Two nodes share an id. |
| [DUI3002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3002) | `InvalidNodeId` | error | An id that is not an identifier, starts with a digit, or is a keyword. |
| [DUI3003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3003) | `UnknownNodeType` | error | A type that is no tag, container, alias, registered widget or path. |
| [DUI3004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3004) | `UnknownStyle` | error | `: Name` naming no style this file sees. |
| [DUI3005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3005) | `DuplicateStyle` | error | Two styles of one name in one file. |
| [DUI3006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3006) | `UnknownBehaviourClass` | error | `+ Name` naming nothing a widget can carry. |
| [DUI3007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3007) | `DuplicateSlotName_Retired` | error | Retired; two slots of one name are DUI3001. |
| [DUI3008](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3008) | `ShadowedLoopVariable` | warning | A loop variable that hides an enclosing loop's (a warning). |
| [DUI3009](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3009) | `ParentRefusedChild` | error | A parent that will not take this child: a full content widget, a named slot filled twice. |
| [DUI3010](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3010) | `RenameOldIdStillInUse` | error | `(was: X)` while X is still an id in the file. |
| [DUI3011](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3011) | `DuplicateWasId` | error | Two nodes claim `(was: X)`. |
| [DUI3012](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3012) | `SelfRename` | error | `(was: X)` on the node called X. |
| [DUI3013](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3013) | `RenameGraphReferenceAmbiguous` | warning | The old id is also another member's name, so graph references are not moved (a warning). |
| [DUI3014](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3014) | `DuplicateResource` | error | Two resource entries of one name. |
| [DUI3015](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3015) | `StyleCycle` | error | A style that inherits itself through its bases. |
| [DUI3016](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3016) | `DuplicateTimeline` | error | Two timelines of one name. |
| [DUI3017](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3017) | `DuplicateComponentAlias` | error | Two `use … as` lines give one name. |
| [DUI3018](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3018) | `AliasShadowsBuiltIn` | error | A `use … as` name that is a tag or a container. |
| [DUI3019](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3019) | `DuplicateProp` | error | Two `props` lines declare one name. |
| [DUI3020](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3020) | `DuplicateEvent` | error | Two `events` entries of one name, or one parameter twice. |
| [DUI3021](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3021) | `UnknownNamespace` | error | `ns.Name` whose `ns` no `use … as ns` declares. |
| [DUI3022](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3022) | `MultipleDefaultSlots` | error | More than one `slot … default` in one file. |
| [DUI3023](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3023) | `DuplicateRowKey` | warning | Warning. Two rows of a `rows` table whose first values make the same id; the second's id then moves with the order. |
| [DUI3024](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3024) | `DuplicateViewModel` | error | Two `viewmodels` entries of one name. |
| [DUI4001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4001) | `UnknownProperty` | error | No property of that name; the nearest one is suggested. |
| [DUI4002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4002) | `UnknownPropertyPathSegment` | error | A dotted path whose head resolves and whose tail does not. |
| [DUI4003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4003) | `ValueTypeMismatch` | error | A value whose shape cannot be the property's type. |
| [DUI4004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4004) | `TupleArityMismatch` | error | A tuple with the wrong number of elements. |
| [DUI4005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4005) | `UnknownEnumValue` | error | A word the property's enum does not declare. |
| [DUI4006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4006) | `PropertyNotWritable` | error | A property text cannot write: transient, deprecated, part of the object graph, a delegate. |
| [DUI4007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4007) | `UnknownResource` | error | `@Name` naming no resource entry. |
| [DUI4008](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4008) | `ResourceTypeMismatch` | error | A resource whose value is not its declared type, or a non-`Asset` resource used as a node type. |
| [DUI5001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5001) | `AssetNotFound` | error | An asset or class path that does not load. |
| [DUI5002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5002) | `NoVisualForProperty` | error | A property of a visual the node's type does not create. |
| [DUI5003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5003) | `NoPanelSlotForProperty` | error | A slot line on a node whose parent lays out no panel. |
| [DUI5004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5004) | `BindingFunctionNotFound` | error | A bound function the class does not have, or one that takes parameters. |
| [DUI5005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5005) | `BindingTargetHasNoSetter` | error | A bound property with no setter. |
| [DUI5006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5006) | `NotAUserWidgetClass` | error | A node typed by a class that is not a concrete DreamUI user widget. |
| [DUI5007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5007) | `LoopNotExpanded` | warning | A loop built by a caller with nowhere to record it, and skipped (a warning). |
| [DUI5008](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5008) | `BindingTargetNotSupported` | error | A binding on something no binding can name: a slot, a container, a field of a struct. |
| [DUI5009](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5009) | `NothingToBuild` | error | The tree had no root; the parser said why. |
| [DUI5010](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5010) | `EventNotFound` | error | `X -> …` where X is not an event of the destination. |
| [DUI5011](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5011) | `BindingExpressionUnsupported` | error | An expression the compiler cannot turn into a function. |
| [DUI5012](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5012) | `EachMisplaced` | error | An `each` at the root, nested, without a list view, or without exactly one template. |
| [DUI5013](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5013) | `NodeReferenceNotFound` | error | A node id written as a value that names no node in the file. |
| [DUI5014](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5014) | `LoopBodyBindingUnsupported` | error | An expression or a `<->` inside a loop body. |
| [DUI5015](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5015) | `TimelineTargetNotFound` | error | A track path that names no node. |
| [DUI5016](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5016) | `TimelinePropertyNotAnimatable` | error | A track property no animation track can drive. |
| [DUI5017](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5017) | `UnknownEaseName` | error | An `ease` name the tween library does not have. |
| [DUI5018](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5018) | `ComponentAliasUnresolved` | error | An alias that names no class: no `class` line and no Blueprint for the file, or a path that loads nothing. |
| [DUI5019](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5019) | `SlotFillOutsideComponent` | error | `slot Name { … }` filling a slot under a node that is not a component instance. |
| [DUI5020](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5020) | `UnknownSlotToFill` | error | A fill naming a slot the component does not declare. |
| [DUI5021](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5021) | `ForMisplaced` | error | A `for` at the root, in another loop, in a host with fixed room, or without exactly one template. |
| [DUI5022](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5022) | `SecondLayoutContainer` | error | A second layout container on one node. |
| [DUI5023](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5023) | `MemberPathNotFound` | error | A member path segment its class does not have, or does not show to Blueprint (an UnrealSharp property needs `BlueprintReadOnly` or `BlueprintReadWrite`). |
| [DUI5024](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5024) | `MemberPathThroughNonObject` | error | A member path that goes on past a value that is not an object. |
| [DUI5025](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5025) | `RouteOperatorMismatch` | error | `+=` on a single-cast delegate, or `=` on a multicast event. |
| [DUI6001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6001) | `SourceFileUnreadable` | error | The Blueprint's Source File does not exist or cannot be read. |
| [DUI6002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6002) | `EmptyTree` | error | The file parsed and produced no tree. |
| [DUI6003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6003) | `ClassPathMismatch` | warning | The `class` line names another Blueprint than the one compiling (a warning). |
| [DUI6004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6004) | `EventHandlerNotFound` | error | `-> Handler` naming a function the class does not have. |
| [DUI6005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6005) | `EventHandlerSignatureMismatch` | error | A handler whose parameters are not the event's. |
| [DUI6006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6006) | `EachSourceNotFound` | error | A loop source the class has neither as a function nor as a variable. |
| [DUI6007](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6007) | `EachSourceNotObjectArray` | error | A loop source that is not an array of objects. |
| [DUI6008](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6008) | `PropTypeUnknown` | error | A `props` or `events` type with no Blueprint pin. |
| [DUI6009](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6009) | `PropNameTaken` | error | A `props` name another member already has with another type, or a widget's id. |
| [DUI6010](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6010) | `EmitUnknownEvent` | error | `emit Name` of an event this file does not declare. |
| [DUI6011](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6011) | `EmitArgumentMismatch` | error | `emit` arguments that are not the event's parameters. |
| [DUI6012](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6012) | `EmitRouteUnsupported` | error | An `emit` where no handler can be generated: in a loop body, or for an event Blueprints cannot carry. |
| [DUI6013](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6013) | `PropDefaultInvalid` | error | A `props` default its type cannot hold. |
| [DUI6014](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6014) | `EventNameTaken` | error | An `events` name another member of the class already answers to. |
| [DUI6015](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6015) | `ViewModelClassUnknown` | error | A `viewmodels` type that names no class, or two. |
| [DUI6016](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6016) | `ViewModelNameTaken` | error | A `viewmodels` name another member of the class already answers to. |
| [DUI6017](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6017) | `ViewModelSourceInvalid` | error | `= new` of an abstract class. |
| [DUI6018](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6018) | `RouteMemberFunctionNotFound` | error | `-> Path.Func` whose function the object's class does not have, or does not let Blueprint call. |
| [DUI6019](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6019) | `RouteArgumentMismatch` | error | `-> Path.Func` whose arguments do not fit the function, or, without parentheses, whose function takes something other than nothing or what the event sends. |
| [DUI6020](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6020) | `TwoWayTargetReadOnly` | error | `<-> Path.Member` whose member cannot be written back: read-only, and no `Set<Member>`. |
| [DUI6021](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6021) | `LoopItemMemberNotFound` | error | `Item.Member` that the loop source's element class does not have. |
| [DUI6022](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6022) | `LoopItemRouteMismatch` | error | `-> Item.Func` that the element class does not have, or whose parameters fit neither nothing nor the event. |
| [DUI7001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7001) | `PatchTargetNotFound` | error | A designer edit with no home in the file: an unknown node, a bound property, a slot with no block. |
| [DUI7002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7002) | `SourceFileChangedUnderEdit` | error | The file changed under a pending edit; nothing was written. |
| [DUI7003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7003) | `PatchValueNotRepresentable` | error | A value the language cannot spell (a non-finite number); its line is left alone. |
| [DUI7004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7004) | `PatchSyntaxNotWritable` | error | An edit to something no line spells: a visibility an `if` decides, a shorthand that cannot be rewritten. |
| [DUI7005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7005) | `PatchStyleComponentNotWritable` | error | An edit to a value a style's `+` line gives the node; add the line to the node or change the style. |
