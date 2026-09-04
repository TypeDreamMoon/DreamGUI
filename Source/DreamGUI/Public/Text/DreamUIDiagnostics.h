// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * One position in a .dui file. Line and column are 1-based, as every editor counts them.
 *
 * Carried on every AST node and every diagnostic because the whole point of the text pipeline is
 * that a mistake points at a line: AI writes most .dui files, and "some property somewhere did not
 * take" is the one failure mode that is genuinely hard to chase.
 */
struct DREAMGUI_API FDreamUISourceLocation
{
	int32 Line = 0;
	int32 Column = 0;

	FDreamUISourceLocation() = default;
	FDreamUISourceLocation(int32 InLine, int32 InColumn) : Line(InLine), Column(InColumn) {}

	bool IsValid() const { return Line > 0; }
};

enum class EDreamUISeverity : uint8
{
	Error,
	Warning,
};

/**
 * The DUInnnn code table.
 *
 * One code per cause, never per message: two situations that a reader would fix differently get
 * two codes, and the same situation reached from two places keeps one. The number is the stable
 * part -- tests assert the code, never the text, so wording stays free to improve and the docs site
 * has one page per code.
 *
 * Ranges, so a code says which stage refused it before you look anything up:
 *   1xxx  lexer      -- a character sequence that is not a token
 *   2xxx  parser     -- tokens that do not form the grammar
 *   3xxx  semantic   -- grammatical, but the tree does not hold together (duplicate id, unknown type)
 *   4xxx  values     -- a value that does not fit the property it is written on
 *   5xxx  builder    -- reflection refused the write, or the target object does not exist
 *   6xxx  compile    -- the .dui could not be attached to its blueprint
 *   7xxx  write-back -- the patcher could not put an edit into the text
 *
 * Gaps are deliberate; do not renumber to close them. A code that is retired stays retired.
 */
enum class EDreamUIDiagnosticCode : int32
{
	None = 0,

	// --- 1xxx lexer ---
	/** A character that cannot begin any token. */
	UnexpectedCharacter = 1001,
	/** A string literal that reaches end of line or end of file without its closing quote. */
	UnterminatedString = 1002,
	/** A block comment that reaches end of file without its closing marker. */
	UnterminatedComment = 1003,
	/** A number the lexer cannot read: two decimal points, a trailing dot, a lone minus. */
	MalformedNumber = 1004,
	/** A `#` colour literal whose digit count is not 3, 4, 6 or 8. */
	MalformedHexColor = 1005,
	/**
	 * A name longer than an FName can hold (NAME_SIZE, see UnrealNames.h).
	 *
	 * Its own code, and a LEXICAL one, because the refusal has nothing to do with what the word
	 * means: every name this language writes down -- a node id, a property, a handler, a loop
	 * variable -- ends up as an FName somewhere downstream, and FName does not return an error for an
	 * over-long string, it calls checkf(false) and takes the editor with it. Caught at the token so
	 * one rule covers every position a word can appear in.
	 */
	IdentifierTooLong = 1006,

	// --- 2xxx parser ---
	/** A token appeared where the grammar allows something else. Message names both. */
	UnexpectedToken = 2001,
	/** A `{` with no matching `}` before end of file. */
	UnclosedBlock = 2002,
	/** A `(` with no matching `)`. */
	UnclosedTuple = 2003,
	/** A node header with no identifier: every node must be named, see the plan's id rule. */
	MissingNodeId = 2004,
	/** A property name with no `=` or `<-` after it, or one whose operator is followed by nothing. */
	MissingPropertyValue = 2005,
	/** The file has no root node, or has more than one. */
	MalformedRoot = 2006,
	/** `class` appeared more than once, or its path is empty. */
	MalformedClassDeclaration = 2007,
	/** `(was: …)` whose contents are not a single identifier. */
	MalformedWasClause = 2008,
	/** `@key(…)` whose contents are not a single string literal. */
	MalformedKeyOverride = 2009,
	/** A `for` / `each` header that is not `<keyword> <Var> in <Func>()`. */
	MalformedLoopHeader = 2010,
	/** The right side of `<-` did not parse as an expression: a stray token, an unclosed paren. */
	MalformedBindingExpression = 2011,
	/** A `use` that could not be honoured: no string, unresolvable path, unreadable file, a cycle. */
	ImportFailed = 2012,
	/**
	 * Nodes, blocks or parenthesised sub-expressions nested deeper than the parser will descend.
	 *
	 * A recursive-descent parser answers a file that nests a thousand deep by exhausting the stack,
	 * and a stack overflow is not a diagnostic -- it is the editor disappearing with the author's
	 * unsaved work. The limit is far above anything a hand-written or generated .dui reaches, so
	 * meeting it means a file that is malformed or hostile rather than merely deep.
	 */
	NestingTooDeep = 2013,

	// --- 3xxx semantic ---
	/**
	 * Two nodes share an id.
	 *
	 * An error rather than a warning, and never silently uniquified: the id is the node's identity
	 * (it becomes the guid AND the class member variable name), so renaming one for the author
	 * would silently change what their bindings point at.
	 */
	DuplicateNodeId = 3001,
	/** An id that cannot be a C++ identifier, or that collides with a reserved word. */
	InvalidNodeId = 3002,
	/** A node type that is neither a built-in tag nor a resolvable asset path. */
	UnknownNodeType = 3003,
	/** `: StyleName` naming a style the file does not declare. */
	UnknownStyle = 3004,
	/** Two styles share a name. */
	DuplicateStyle = 3005,
	/** A `+ ClassName` that does not resolve to a concrete UDreamUIBehaviour subclass. */
	UnknownBehaviourClass = 3006,
	/**
	 * RETIRED, never raised. Kept so the number is not reused; see the range comment above.
	 *
	 * It assumed slots had a namespace of their own. They do not: `slot Footer` produces a node
	 * whose id becomes a class member variable exactly like a widget's, so two slots colliding and a
	 * slot colliding with a widget are one cause with one fix. That is DuplicateNodeId, and giving
	 * the same cause a second number would only mean docs to keep in sync and a code the reader has
	 * to learn is a synonym.
	 */
	DuplicateSlotName_Retired = 3007,
	/**
	 * A loop variable that shadows an outer loop variable.
	 *
	 * A warning rather than an error, and deliberately: nothing in today's grammar can REFERENCE a
	 * loop variable -- a loop body is repetition, not interpolation -- so shadowing one provably
	 * cannot change the tree that gets built. Refusing a whole file over a name that has no effect
	 * is the wrong trade. The day a value can say `{Slot.Name}`, this becomes an error.
	 */
	ShadowedLoopVariable = 3008,
	/**
	 * A parent that will not take this child.
	 *
	 * Distinct from "unknown type" because the node is fine and the parent is fine -- only the pair
	 * is not. UDreamContentWidget caps itself at one child and a named slot at none, and the attach
	 * path DROPS the extra silently, so without this the author gets a tree that is quietly missing
	 * a subtree they can see in their own file.
	 */
	ParentRefusedChild = 3009,
	/**
	 * `(was: X)` where X is still a live id in this same file.
	 *
	 * The author renamed a node and then made a new one under the old name, so "move everything that
	 * pointed at X" would move it away from a node that exists and wants it. Refused rather than
	 * guessed, and refusing takes the WHOLE file's migration with it: a half-migrated file is worse
	 * than an unmigrated one, because the next compile no longer starts from what the author wrote.
	 */
	RenameOldIdStillInUse = 3010,
	/** Two nodes both claim `(was: X)`. Nothing can decide which one inherits X's references. */
	DuplicateWasId = 3011,
	/** `(was:)` naming the node itself. Nothing to migrate, and almost certainly a typo. */
	SelfRename = 3012,
	/**
	 * The graph leg of a rename was refused because the old name is not unambiguously the widget's.
	 *
	 * Graph references resolve by NAME, so if the old name is also a variable this blueprint
	 * declares, a member of its parent class, or a local inside some function, rewriting every
	 * reference to it would silently repoint code that had nothing to do with the widget. A warning
	 * rather than an error: the binding and animation legs are unambiguous and still run.
	 */
	RenameGraphReferenceAmbiguous = 3013,
	/** Two `resources` entries share a name; the first one wins everywhere, so the second is refused. */
	DuplicateResource = 3014,
	/** `style A : B` where following the bases comes back to A. Nothing applies; the node errors. */
	StyleCycle = 3015,

	// --- 4xxx values ---
	/** No property of that name on the target object. Message suggests the nearest match. */
	UnknownProperty = 4001,
	/** A dotted path whose head resolves but whose tail does not. */
	UnknownPropertyPathSegment = 4002,
	/** The literal's shape cannot produce the property's type. */
	ValueTypeMismatch = 4003,
	/** A tuple with the wrong number of elements for its destination struct. */
	TupleArityMismatch = 4004,
	/** An identifier written on an enum property that the enum does not declare. */
	UnknownEnumValue = 4005,
	/** A property that exists but is not writable from text (Transient, editor-only, no setter). */
	PropertyNotWritable = 4006,
	/** `@Name` names no entry in any `resources` block. */
	UnknownResource = 4007,
	/** A `resources` entry whose declared type does not match its own literal (`Color Accent = 8`). */
	ResourceTypeMismatch = 4008,

	// --- 5xxx builder ---
	/** The node names an asset that could not be loaded. */
	AssetNotFound = 5001,
	/** A property was written on a Visual the node's type does not create. */
	NoVisualForProperty = 5002,
	/** `@slot` on a node whose parent lays out no panel slot. */
	NoPanelSlotForProperty = 5003,
	/** A binding naming a function the class does not declare, or that takes parameters. */
	BindingFunctionNotFound = 5004,
	/** A binding whose destination property has no setter -- see FindDreamWidgetSetterFor. */
	BindingTargetHasNoSetter = 5005,
	/** A nested user widget class that is not a UDreamUserWidget subclass. */
	NotAUserWidgetClass = 5006,
	/**
	 * A `for` / `each` body the builder cannot expand yet. Warning, not error.
	 *
	 * Its own code rather than a general "unsupported" because it is temporary and the docs page for
	 * it says so: the grammar accepts loops from day one so files written today keep parsing, and
	 * this is what tells an author the body did not make it into the tree. Retire it when loops land.
	 */
	LoopNotExpanded = 5007,
	/**
	 * A binding whose destination is a kind of object FDreamWidgetPropertyBinding cannot name.
	 *
	 * Not the same as having no setter, and worth separating precisely because the fix is different:
	 * "no setter" is about the property, this is about the STRUCT -- EDreamWidgetBindingTarget knows
	 * Widget, Visual and Behaviour, so a panel slot, a layout container and a dotted sub-property
	 * have nowhere to be recorded no matter what setters they have. Reporting these as 5005 would
	 * send the reader hunting for a setter that would not help them if they wrote it.
	 */
	BindingTargetNotSupported = 5008,
	/** The AST carried no root node, so there was nothing to build. Parser already said why. */
	NothingToBuild = 5009,
	/** `X -> Handler` where X is not an assignable event on the destination, or is a path into one. */
	EventNotFound = 5010,
	/**
	 * A binding expression the thunk generator could not lower into a Blueprint function: a name
	 * neither a variable nor a function on this class, an operator with no overload for its operand
	 * types, or a type the generator does not know how to convert. The message names the specific
	 * refusal; the code is one because the reader's next move is the same for all of them -- fix the
	 * expression, or move the logic into a real function and bind that.
	 */
	BindingExpressionUnsupported = 5011,
	/**
	 * An `each` somewhere it cannot work: nested in another, at the root, on a widget with no list
	 * view to fill, or with a body that is not exactly one template widget. One code because the
	 * reader's move is the same -- restructure the block; the message names which rule.
	 */
	EachMisplaced = 5012,
	/**
	 * A property pointed at a node id this file does not declare.
	 *
	 * The node-reference twin of AssetNotFound, and split from the type check for the same reason
	 * the asset case is: "no node named CheckMark" and "CheckMark is not the kind of thing this
	 * property holds" are different mistakes and only the first one is about the NAME. A node that
	 * exists but creates no visual, or is the wrong widget class, is a ValueTypeMismatch -- the same
	 * code an asset path raises when it loads something the property cannot take.
	 */
	NodeReferenceNotFound = 5013,
	/**
	 * A `<-` expression or a `<->` inside a `for` / `each` body.
	 *
	 * The thunk pass deliberately skips loop bodies -- a generated function would ask the CLASS for a
	 * name only the iteration has -- so the only source shape an `each` supports today is the single
	 * hop `Item.Member`, which is recorded per cell instead. Anything richer used to be dropped where
	 * the builder found a binding with no function name: the file compiled green and the property was
	 * simply never driven. Its own code rather than BindingExpressionUnsupported because the
	 * expression is fine and the PLACE is not, which is a different fix and a temporary limit.
	 */
	LoopBodyBindingUnsupported = 5014,

	// --- 6xxx compile ---
	/** The class's Source File names a file that does not exist or cannot be read. */
	SourceFileUnreadable = 6001,
	/** The .dui parsed, but produced no tree to compile. */
	EmptyTree = 6002,
	/** The `class` line names a different asset than the Blueprint being compiled. A warning: the
	 * line's job is a stable localization namespace, and being wrong drifts keys, not the build. */
	ClassPathMismatch = 6003,
	/**
	 * `Event -> Handler` naming a function the compiled class does not declare.
	 *
	 * Split from EventNotFound because the two halves of a route fail for opposite reasons and only
	 * the compiler can see this one: EventNotFound is about the EVENT, which the builder checks
	 * against a class that already exists, while the handler lives on the class this compile is
	 * building and cannot be looked up until it has been built.
	 */
	EventHandlerNotFound = 6004,
	/** `Event -> Handler` where the handler's parameters are not the ones the event sends. */
	EventHandlerSignatureMismatch = 6005,

	// --- 7xxx write-back ---
	/** The patcher was asked to write a property it cannot locate a home for. */
	PatchTargetNotFound = 7001,
	/** The file on disk changed underneath a pending edit. */
	SourceFileChangedUnderEdit = 7002,
	/**
	 * A live value that cannot be written as a literal this language can read back.
	 *
	 * Non-finite floats are the whole of it today: the printer spells them the way printf does, and
	 * `inf` lexes as an identifier, so a file written with one parses but refuses on the next compile
	 * -- a designer edit that breaks a line nobody touched. Refusing at write time keeps the damage
	 * where the author can see it.
	 *
	 * Its own code rather than borrowing the lexer's MalformedNumber: nothing is wrong with the
	 * FILE, and pointing an author at a line they did not write is worse than saying plainly that
	 * the value has no spelling.
	 */
	PatchValueNotRepresentable = 7003,
};

/**
 * One thing wrong with one .dui file, at one place in it.
 *
 * Severity is decided at the site that raises it, not by the code's range: the same cause can be
 * fatal to a compile and merely worth mentioning to an editor preview.
 */
struct DREAMGUI_API FDreamUIDiagnostic
{
	EDreamUIDiagnosticCode Code = EDreamUIDiagnosticCode::None;
	EDreamUISeverity Severity = EDreamUISeverity::Error;
	FDreamUISourceLocation Location;
	/** What went wrong, in one sentence, naming the offending text. No code prefix; ToString adds it. */
	FString Message;
	/** The file this came from, for messages. May be a display name rather than a path. */
	FString SourceName;

	FDreamUIDiagnostic() = default;
	FDreamUIDiagnostic(EDreamUIDiagnosticCode InCode, const FDreamUISourceLocation& InLocation, FString InMessage,
		EDreamUISeverity InSeverity = EDreamUISeverity::Error)
		: Code(InCode), Severity(InSeverity), Location(InLocation), Message(MoveTemp(InMessage))
	{
	}

	bool IsError() const { return Severity == EDreamUISeverity::Error; }

	/** "Login.dui(42,9): error DUI3001: two nodes are named 'OkBtn'". */
	FString ToString() const;

	/** "DUI3001". */
	static FString CodeToString(EDreamUIDiagnosticCode InCode);
};

/**
 * A diagnostic sink that also remembers whether anything fatal landed in it.
 *
 * Passed by reference through the whole front end so a caller asks one object "did this work",
 * rather than every stage inventing its own bool-plus-array convention.
 */
struct DREAMGUI_API FDreamUIDiagnosticBag
{
	TArray<FDreamUIDiagnostic> Diagnostics;
	/** Prefixed onto every diagnostic added through this bag. */
	FString SourceName;

	void Add(FDreamUIDiagnostic InDiagnostic);
	void AddError(EDreamUIDiagnosticCode InCode, const FDreamUISourceLocation& InLocation, FString InMessage);
	void AddWarning(EDreamUIDiagnosticCode InCode, const FDreamUISourceLocation& InLocation, FString InMessage);

	bool HasErrors() const;
	int32 NumErrors() const;
	/** Every diagnostic, one per line, in the order they were raised. */
	FString ToString() const;
	void Reset() { Diagnostics.Reset(); }
};
