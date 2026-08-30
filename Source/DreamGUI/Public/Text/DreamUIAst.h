// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Text/DreamUIDiagnostics.h"

/**
 * The shape a .dui file parses into.
 *
 * Plain structs, not USTRUCTs, and deliberately free of UObject: the parser must run before any
 * engine object exists (a headless syntax check, a CI gate, the VSCode extension's own copy of the
 * grammar) and the builder is the only stage allowed to touch reflection. Keeping the AST inert is
 * what lets the parser's whole test suite be strings in and data out.
 *
 * Every node carries its FDreamUISourceLocation. That is not for error messages alone -- the
 * write-back patcher locates the line to edit from it, so a location that is merely approximate
 * would produce edits landing on the wrong property.
 *
 * The AST records SHAPE, never meaning. `Left` is an identifier here, not an enum value; `(400,
 * 240)` is a two-element tuple, not an FVector2D. Which of those it becomes is decided by the
 * FProperty it is written onto, which only the builder knows. A parser that guessed types would
 * have to be taught every struct in the runtime, and would disagree with reflection the first time
 * one changed.
 */

enum class EDreamUIValueKind : uint8
{
	/** A bare word: an enum value, true/false, or an unquoted name. */
	Identifier,
	/** 24, 0.95, -3. Kept as text; the builder parses it against the destination's numeric type. */
	Number,
	/** "确定" -- quotes stripped, escapes resolved. */
	String,
	/** (400, 240) or (8, 8, 8, 8). Elements are raw text, each parsed by the builder. */
	Tuple,
	/** #1E1E1E or #1E1E1EFF. Raw excludes the '#'. */
	HexColor,
	/** /Game/UI/F_Body -- an unquoted object path. */
	AssetPath,
	/** @Accent -- a reference into the file's `resources` block. Raw is the name, no '@'. */
	ResourceRef,
};

struct DREAMGUI_API FDreamUIValue
{
	EDreamUIValueKind Kind = EDreamUIValueKind::Identifier;

	/**
	 * The literal as written, minus its delimiters (no quotes on a String, no '#' on a HexColor).
	 *
	 * Kept verbatim so the write-back patcher can leave untouched values byte-identical: a round
	 * trip that renormalises "0.50" to "0.5" would rewrite lines nobody edited, and every one of
	 * those shows up in the author's diff.
	 */
	FString Raw;

	/** Tuple only: the elements, each already trimmed. */
	TArray<FString> Elements;

	/** String only: the key written in `@key("…")`, empty when the author did not override it. */
	FString LocalizationKeyOverride;

	FDreamUISourceLocation Location;
};

/**
 * One node of a binding expression -- the parsed shape of everything to the right of `<-`.
 *
 * A tree of value structs, exactly like nodes hold their children: Operands nests by value, and the
 * whole thing carries no UObject anywhere, so an AST stays as parseable-off-thread as it was.
 */
struct DREAMGUI_API FDreamUIExpression
{
	enum class EKind : uint8
	{
		/** `Func(a, b)` -- Symbol is the function name, Operands the arguments (often none). */
		Call,
		/** A bare identifier -- a variable on the user widget. Symbol is the name. */
		VariableRef,
		/** A literal: LiteralKind + LiteralRaw carry it exactly as FDreamUIValue would. */
		Literal,
		/** `!x` or unary `-x` -- Symbol is the operator spelling, Operands has one entry. */
		Unary,
		/** `a op b` -- Symbol is the operator spelling, Operands has two entries. */
		Binary,
	};

	EKind Kind = EKind::Call;
	/** Function name, variable name, or operator spelling ("!", "==", "&&", …). */
	FString Symbol;
	/** Literal only: the value kind and its raw spelling, matching FDreamUIValue's fields. */
	EDreamUIValueKind LiteralKind = EDreamUIValueKind::Identifier;
	FString LiteralRaw;

	TArray<FDreamUIExpression> Operands;
	FDreamUISourceLocation Location;

	/** True for exactly the shape the plain binding path always handled: `Name()` with no arguments. */
	bool IsBareCall() const { return Kind == EKind::Call && Operands.Num() == 0; }
};

/**
 * One `Name = Value` or `Name <- Func()` line.
 *
 * A binding and an assignment share this struct because they share a destination: the difference is
 * only where the value comes from, and treating them as two node types would double every walk
 * that resolves a property path.
 */
struct DREAMGUI_API FDreamUIProperty
{
	/** "FontSize", or a dotted path: "AnchorData.SizeDelta", "Brush.TintColor". */
	FString Name;

	/** Set when this is `=`. Empty and unused when BindingFunction is set. */
	FDreamUIValue Value;

	/**
	 * Set when this is `<-` with a bare `Func()`: the no-argument UFUNCTION on the user widget that
	 * drives the property. For anything richer the parser fills BindingExpression instead, and the
	 * COMPILER lowers it into a generated pure function whose name it writes back into this field
	 * before the builder runs -- so downstream of that pass, this is always the one name
	 * FDreamWidgetPropertyBinding can hold.
	 */
	FString BindingFunction;

	/**
	 * Set when the right side of `<-` is more than a bare call. Lowered by the compiler's thunk
	 * pass; a consumer that builds an un-lowered AST (the write-back's reference tree) sees a
	 * binding whose function name is still empty and must treat it as a binding, not a literal.
	 */
	TOptional<FDreamUIExpression> BindingExpression;

	/** Set when this is `->`: the UFUNCTION on the user widget the event calls. */
	FString EventHandler;

	/**
	 * Set when this is `<->`: the FieldNotify variable on the user widget the property mirrors,
	 * both ways. The compiler's thunk pass desugars it -- a generated getter lands in
	 * BindingFunction (this field then rides into the binding's NotifyField), and a synthesized
	 * `OnValueChangedBP -> generated-setter` event property joins the node.
	 */
	FString TwoWayProperty;

	bool IsBinding() const { return !BindingFunction.IsEmpty() || BindingExpression.IsSet() || !TwoWayProperty.IsEmpty(); }
	bool IsEventBinding() const { return !EventHandler.IsEmpty(); }

	FDreamUISourceLocation Location;
};

/** `+ UIButton { … }` -- a UDreamUIBehaviour attached to the enclosing node. */
struct DREAMGUI_API FDreamUIComponent
{
	/** As written: an alias, a reflected class name, or a full /Script/ path. */
	FString ClassName;
	TArray<FDreamUIProperty> Properties;
	FDreamUISourceLocation Location;
};

enum class EDreamUINodeKind : uint8
{
	/** `Type Id { … }` -- the ordinary case. */
	Widget,
	/** `slot Name` -- declares a UDreamNamedSlot the host fills. */
	NamedSlot,
	/** `for Var in Func() { … }` -- expanded at compile time into N copies. */
	ForLoop,
	/** `each Var in Func() { … }` -- bound at run time to a list. Parsed now, built later. */
	EachLoop,
};

struct DREAMGUI_API FDreamUINode
{
	EDreamUINodeKind Kind = EDreamUINodeKind::Widget;

	/**
	 * Widget: the built-in tag ("Widget", "Image", "Text", …) or an asset path to a
	 * UDreamUserWidget subclass. NamedSlot: unused. Loops: unused.
	 */
	FString TypeName;

	/** Loops only: whether the source is `Func()` (true) or a variable read (false). */
	bool bLoopSourceIsFunction = true;

	/**
	 * The node's identity. Required on Widget and NamedSlot, empty on loops.
	 *
	 * This one string is the guid source, the class member variable name, the binding key and the
	 * localization key. Unique across the whole tree -- see DuplicateNodeId for why that is an
	 * error rather than something to fix up.
	 */
	FString Id;

	/** `(was: OldId)` -- the previous id, so a rename can carry graph, binding and animation across. */
	FString WasId;

	/** `: StyleName` -- properties from that style are applied first, then these override. */
	FString StyleName;

	/** Loops only: the loop variable, and the no-argument UFUNCTION supplying the sequence. */
	FString LoopVariable;
	FString LoopSourceFunction;

	/** Bare-name properties. Destination is the widget, or its visual when the widget has none. */
	TArray<FDreamUIProperty> Properties;

	/** `@slot Name = Value` -- destination is this node's UDreamPanelSlot. */
	TArray<FDreamUIProperty> SlotProperties;

	TArray<FDreamUIComponent> Components;

	TArray<FDreamUINode> Children;

	FDreamUISourceLocation Location;
};

/** `style Card { … }` -- a named bag of properties, applied by name. No inheritance yet. */
struct DREAMGUI_API FDreamUIStyle
{
	FString Name;
	/** `style Danger : Button` -- Button's properties apply first, then these override. Empty = none. */
	FString BaseName;
	TArray<FDreamUIProperty> Properties;
	FDreamUISourceLocation Location;
};

/**
 * One `resources { Color Accent = #FF6600 }` entry: a named constant with a declared type.
 *
 * TYPED, unlike everything else in the language, because an entry is authored without a
 * destination: `8` on a property knows what it is from the FProperty it lands on, but `8` in a
 * resources block has nothing to land on until somebody writes `@CornerRadius` -- and by then a
 * type error would point at the use site instead of the mistake. The type names are the builder's
 * to validate (Color, Number, Vector2, String, Asset); the parser records shape, as always.
 */
struct DREAMGUI_API FDreamUIResource
{
	FString TypeName;
	FString Name;
	FDreamUIValue Value;
	FDreamUISourceLocation Location;
};

struct DREAMGUI_API FDreamUIAst
{
	/** `class /Game/UI/WBP_SavePanel` -- which blueprint this tree compiles into. */
	FString ClassPath;
	FDreamUISourceLocation ClassPathLocation;

	TArray<FDreamUIStyle> Styles;

	TArray<FDreamUIResource> Resources;

	/** Exactly one root. bHasRoot is false when parsing failed before one was produced. */
	FDreamUINode Root;
	bool bHasRoot = false;

	const FDreamUIStyle* FindStyle(const FString& InName) const;

	/** The entry `@InName` refers to, or null. First declaration wins, like styles. */
	const FDreamUIResource* FindResource(const FString& InName) const;

	/** Every node in the tree, root first, parents before children. Loop bodies included. */
	void ForEachNode(TFunctionRef<void(const FDreamUINode&)> InPredicate) const;
};
