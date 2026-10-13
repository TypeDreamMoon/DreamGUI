## Diagnostics

Every message has a code, `DUInnnn`, printed as `File.dui(line,col): error DUI3001: …`. The first digit says which stage
refused: 1 lexer, 2 parser, 3 meaning, 4 values, 5 building the tree, 6 compiling into the Blueprint, 7 writing back.

| Code | Name | Meaning |
|---|---|---|
| DUI1001 | UnexpectedCharacter | A character that cannot begin any token. |
| DUI1002 | UnterminatedString | A string that reaches the end of its line without its closing quote. |
| DUI1003 | UnterminatedComment | A `/*` that never reaches `*/`. |
| DUI1004 | MalformedNumber | A number that cannot be read: two decimal points, a trailing dot, a unit glued on (`24px`). |
| DUI1005 | MalformedHexColor | A colour whose digit count is not 3, 4, 6 or 8. |
| DUI1006 | IdentifierTooLong | A name longer than an `FName` holds. |
| DUI1007 | AssetPathTooLong | An asset path longer than an `FName` holds. |
| DUI2001 | UnexpectedToken | A token where the grammar wants something else; the message names both. |
| DUI2002 | UnclosedBlock | A `{` with no matching `}`. |
| DUI2003 | UnclosedTuple | A `(` with no matching `)`. |
| DUI2004 | MissingNodeId | A node with neither an id nor a block, or an unnamed node with `(was: …)`. |
| DUI2005 | MissingPropertyValue | A property with no `=` or arrow, or nothing after it. |
| DUI2006 | MalformedRoot | No root node in a file compiled into a class, or more than one. |
| DUI2007 | MalformedClassDeclaration | `class` twice, with an empty path, or inside a node. |
| DUI2008 | MalformedWasClause | `(was: …)` holding something other than one id. |
| DUI2009 | MalformedKeyOverride | `@key(…)` holding something other than one string, or after a value that is not a string. |
| DUI2010 | MalformedLoopHeader | A `for` / `each` header that is not `<keyword> Var in Source` or `Source()`. |
| DUI2011 | MalformedBindingExpression | The right side of `<-`, `<->` or an `emit`'s arguments that do not parse. |
| DUI2012 | ImportFailed | A `use` that could not be honoured: no such file, unreadable, does not parse, or a cycle. |
| DUI2013 | NestingTooDeep | Blocks or parentheses nested deeper than 256. |
| DUI2014 | MalformedTimeline | A `timeline` block or line that is not the grammar. |
| DUI2015 | MalformedUseDeclaration | `use … as` without a name, a keyword as the name, or a class path without `as`. |
| DUI2016 | MalformedPropsBlock | A `props` line that is not `Type Name` or `Type Name = value`, or `props` inside a node. |
| DUI2017 | MalformedEventsBlock | An `events` entry that is not `Name` or `Name(Type Param, …)`, or `events` inside a node. |
| DUI2018 | MalformedConditional | An `if` without its condition or block, an `else` with no `if`, or a branch holding something other than widgets. |
| DUI2019 | MalformedSlotDeclaration | A slot with `default` twice, or a block that both declares and fills. |
| DUI2020 | MalformedRows | A `rows` table that does not read: a column list that is not names, a column twice, a row with the wrong number of values. |
| DUI2021 | MalformedViewModelsBlock | A `viewmodels` line that is not `Type Name`, `= new`, `= global ["Name"]` or `= parent ["Name"]` (an empty `""` name included), or a `viewmodels` block inside a node. |
| DUI3001 | DuplicateNodeId | Two nodes share an id. |
| DUI3002 | InvalidNodeId | An id that is not an identifier, starts with a digit, or is a keyword. |
| DUI3003 | UnknownNodeType | A type that is no tag, container, alias, registered widget or path. |
| DUI3004 | UnknownStyle | `: Name` naming no style this file sees. |
| DUI3005 | DuplicateStyle | Two styles of one name in one file. |
| DUI3006 | UnknownBehaviourClass | `+ Name` naming nothing a widget can carry. |
| DUI3007 | DuplicateSlotName_Retired | Retired; two slots of one name are DUI3001. |
| DUI3008 | ShadowedLoopVariable | A loop variable that hides an enclosing loop's (a warning). |
| DUI3009 | ParentRefusedChild | A parent that will not take this child: a full content widget, a named slot filled twice. |
| DUI3010 | RenameOldIdStillInUse | `(was: X)` while X is still an id in the file. |
| DUI3011 | DuplicateWasId | Two nodes claim `(was: X)`. |
| DUI3012 | SelfRename | `(was: X)` on the node called X. |
| DUI3013 | RenameGraphReferenceAmbiguous | The old id is also another member's name, so graph references are not moved (a warning). |
| DUI3014 | DuplicateResource | Two resource entries of one name. |
| DUI3015 | StyleCycle | A style that inherits itself through its bases. |
| DUI3016 | DuplicateTimeline | Two timelines of one name. |
| DUI3017 | DuplicateComponentAlias | Two `use … as` lines give one name. |
| DUI3018 | AliasShadowsBuiltIn | A `use … as` name that is a tag or a container. |
| DUI3019 | DuplicateProp | Two `props` lines declare one name. |
| DUI3020 | DuplicateEvent | Two `events` entries of one name, or one parameter twice. |
| DUI3021 | UnknownNamespace | `ns.Name` whose `ns` no `use … as ns` declares. |
| DUI3022 | MultipleDefaultSlots | More than one `slot … default` in one file. |
| DUI3023 | DuplicateRowKey | Warning. Two rows of a `rows` table whose first values make the same id; the second's id then moves with the order. |
| DUI3024 | DuplicateViewModel | Two `viewmodels` entries of one name. |
| DUI4001 | UnknownProperty | No property of that name; the nearest one is suggested. |
| DUI4002 | UnknownPropertyPathSegment | A dotted path whose head resolves and whose tail does not. |
| DUI4003 | ValueTypeMismatch | A value whose shape cannot be the property's type. |
| DUI4004 | TupleArityMismatch | A tuple with the wrong number of elements. |
| DUI4005 | UnknownEnumValue | A word the property's enum does not declare. |
| DUI4006 | PropertyNotWritable | A property text cannot write: transient, deprecated, part of the object graph, a delegate. |
| DUI4007 | UnknownResource | `@Name` naming no resource entry. |
| DUI4008 | ResourceTypeMismatch | A resource whose value is not its declared type, or a non-`Asset` resource used as a node type. |
| DUI5001 | AssetNotFound | An asset or class path that does not load. |
| DUI5002 | NoVisualForProperty | A property of a visual the node's type does not create. |
| DUI5003 | NoPanelSlotForProperty | A slot line on a node whose parent lays out no panel. |
| DUI5004 | BindingFunctionNotFound | A bound function the class does not have, or one that takes parameters. |
| DUI5005 | BindingTargetHasNoSetter | A bound property with no setter. |
| DUI5006 | NotAUserWidgetClass | A node typed by a class that is not a concrete DreamUI user widget. |
| DUI5007 | LoopNotExpanded | A loop built by a caller with nowhere to record it, and skipped (a warning). |
| DUI5008 | BindingTargetNotSupported | A binding on something no binding can name: a slot, a container, a field of a struct. |
| DUI5009 | NothingToBuild | The tree had no root; the parser said why. |
| DUI5010 | EventNotFound | `X -> …` where X is not an event of the destination. |
| DUI5011 | BindingExpressionUnsupported | An expression the compiler cannot turn into a function. |
| DUI5012 | EachMisplaced | An `each` at the root, nested, without a list view, or without exactly one template. |
| DUI5013 | NodeReferenceNotFound | A node id written as a value that names no node in the file. |
| DUI5014 | LoopBodyBindingUnsupported | An expression or a `<->` inside a loop body. |
| DUI5015 | TimelineTargetNotFound | A track path that names no node. |
| DUI5016 | TimelinePropertyNotAnimatable | A track property no animation track can drive. |
| DUI5017 | UnknownEaseName | An `ease` name the tween library does not have. |
| DUI5018 | ComponentAliasUnresolved | An alias that names no class: no `class` line and no Blueprint for the file, or a path that loads nothing. |
| DUI5019 | SlotFillOutsideComponent | `slot Name { … }` filling a slot under a node that is not a component instance. |
| DUI5020 | UnknownSlotToFill | A fill naming a slot the component does not declare. |
| DUI5021 | ForMisplaced | A `for` at the root, in another loop, in a host with fixed room, or without exactly one template. |
| DUI5022 | SecondLayoutContainer | A second layout container on one node. |
| DUI5023 | MemberPathNotFound | A member path segment its class does not have, or does not show to Blueprint (an UnrealSharp property needs `BlueprintReadOnly` or `BlueprintReadWrite`). |
| DUI5024 | MemberPathThroughNonObject | A member path that goes on past a value that is not an object. |
| DUI5025 | RouteOperatorMismatch | `+=` on a single-cast delegate, or `=` on a multicast event. |
| DUI6001 | SourceFileUnreadable | The Blueprint's Source File does not exist or cannot be read. |
| DUI6002 | EmptyTree | The file parsed and produced no tree. |
| DUI6003 | ClassPathMismatch | The `class` line names another Blueprint than the one compiling (a warning). |
| DUI6004 | EventHandlerNotFound | `-> Handler` naming a function the class does not have. |
| DUI6005 | EventHandlerSignatureMismatch | A handler whose parameters are not the event's. |
| DUI6006 | EachSourceNotFound | A loop source the class has neither as a function nor as a variable. |
| DUI6007 | EachSourceNotObjectArray | A loop source that is not an array of objects. |
| DUI6008 | PropTypeUnknown | A `props` or `events` type with no Blueprint pin. |
| DUI6009 | PropNameTaken | A `props` name another member already has with another type, or a widget's id. |
| DUI6010 | EmitUnknownEvent | `emit Name` of an event this file does not declare. |
| DUI6011 | EmitArgumentMismatch | `emit` arguments that are not the event's parameters. |
| DUI6012 | EmitRouteUnsupported | An `emit` where no handler can be generated: in a loop body, or for an event Blueprints cannot carry. |
| DUI6013 | PropDefaultInvalid | A `props` default its type cannot hold. |
| DUI6014 | EventNameTaken | An `events` name another member of the class already answers to. |
| DUI6015 | ViewModelClassUnknown | A `viewmodels` type that names no class, or two. |
| DUI6016 | ViewModelNameTaken | A `viewmodels` name another member of the class already answers to. |
| DUI6017 | ViewModelSourceInvalid | `= new` of an abstract class. |
| DUI6018 | RouteMemberFunctionNotFound | `-> Path.Func` whose function the object's class does not have, or does not let Blueprint call. |
| DUI6019 | RouteArgumentMismatch | `-> Path.Func` whose arguments do not fit the function, or, without parentheses, whose function takes something other than nothing or what the event sends. |
| DUI6020 | TwoWayTargetReadOnly | `<-> Path.Member` whose member cannot be written back: read-only, and no `Set<Member>`. |
| DUI6021 | LoopItemMemberNotFound | `Item.Member` that the loop source's element class does not have. |
| DUI6022 | LoopItemRouteMismatch | `-> Item.Func` that the element class does not have, or whose parameters fit neither nothing nor the event. |
| DUI6023 | SourceFileWritePending | The document has edits that could not be written, so compiling the older file was refused. |
| DUI7001 | PatchTargetNotFound | A designer edit with no home in the file: an unknown node, a bound property, a slot with no block. |
| DUI7002 | SourceFileChangedUnderEdit | The file changed under a pending edit; nothing was written. |
| DUI7003 | PatchValueNotRepresentable | A value the language cannot spell (a non-finite number); its line is left alone. |
| DUI7004 | PatchSyntaxNotWritable | An edit to something no line spells: a visibility an `if` decides, a shorthand that cannot be rewritten. |
| DUI7005 | PatchStyleComponentNotWritable | An edit to a value a style's `+` line gives the node; add the line to the node or change the style. |
