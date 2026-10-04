// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Text/DreamUIAst.h"

class UDreamWidgetBlueprint;
class UDreamWidgetTree;
struct FDreamUIDiagnosticBag;
struct FDreamWidgetEventBinding;
struct FEdGraphPinType;

/**
 * Lowers binding EXPRESSIONS into generated Blueprint functions -- the "编译期 BP thunk" ruling.
 *
 * `Prop <- !IsLoading()` becomes a hidden pure function on the class whose graph computes the
 * expression, and the property's BindingFunction is rewritten to that function's name BEFORE the
 * builder runs. Everything downstream -- FDreamWidgetPropertyBinding's single FunctionName, the
 * compiler's signature validation, the runtime's resolve-and-subscribe, `(was:)` migration, the
 * write-back -- is untouched, because by the time any of them look, an expression IS a function.
 *
 * Thunk names derive deterministically from the node id and property name, so recompiles reuse
 * identities; every generated graph is dropped and rebuilt from the file each compile, exactly the
 * contract GeneratedVariables live under. An expression the generator cannot lower reports
 * DUI5011 into the bag and clears the binding, so the compile fails loudly with the file and line
 * rather than shipping a binding that silently does nothing.
 *
 * `Event -> emit Name(args)` is lowered the same way, into a generated HANDLER whose body evaluates the
 * arguments and broadcasts the dispatcher Name -- and in two halves, because a handler is the one generated
 * function whose signature is not the file's to choose. It has to take exactly what the source event sends, and
 * which event that is (a widget's, its visual's, one of its behaviours', of whatever class the node's type turns
 * out to be) is the builder's to resolve. So Generate names the handler and writes the name into EventHandler --
 * from there on the builder records an ordinary route -- and GenerateEmitHandlers, run on the tree the builder
 * returned, reads the source event off the route it recorded and builds the body to fit it.
 */
namespace DreamUIExpressionThunks
{
	/** The prefix every generated thunk graph name carries; the cleanup pass keys on it. */
	extern DREAMGUIEDITOR_API const TCHAR* GeneratedGraphPrefix;

	/** The prefix every generated `-> emit` handler carries; the same cleanup pass keys on it. */
	extern DREAMGUIEDITOR_API const TCHAR* GeneratedEmitPrefix;

	/** One `Event -> emit Name(args)` route, between the half of the lowering that named its handler and the half that builds it. */
	struct FEmitRoute
	{
		/** The generated handler, as written into the property's EventHandler. */
		FString HandlerName;
		/** The `events` entry it raises. */
		FString EventName;
		/** One per parameter of that entry, already counted against it. */
		TArray<FDreamUIExpression> Arguments;
		FDreamUISourceLocation Location;
	};

	/**
	 * The Blueprint pin type a `props` or `events` type names: Text, String, Number (a double), Integer (an int32),
	 * Bool, Color (FLinearColor), Vector2 (FVector2D), Asset (an object reference), Class, or `Enum <path>` (the byte
	 * pin of that enum, which must be a BlueprintType one). One table for a prop, a dispatcher parameter and the pin an
	 * emit argument feeds, so the three cannot come to disagree about what `Number` means. False with OutReason, a
	 * sentence fragment, when the type is none of those.
	 */
	DREAMGUIEDITOR_API bool MakeDeclaredPinType(const FString& InTypeName, const FString& InEnumPath, FEdGraphPinType& OutPinType,
		FString& OutReason);

	/**
	 * Remove stale generated graphs, then lower every BindingExpression in InAst: create the
	 * function graph, wire the expression, and rewrite the property's BindingFunction in place.
	 *
	 * And name the handler of every `-> emit` route, into OutEmitRoutes for GenerateEmitHandlers, after checking it
	 * against the file's `events` block (EmitUnknownEvent, and EmitArgumentMismatch on the count). A route raising an
	 * event in InRefusedEvents -- declared by the file, refused by the compiler, which said why -- is left as it is.
	 * With no OutEmitRoutes, emit routes are not touched at all.
	 */
	void Generate(UDreamWidgetBlueprint* InBlueprint, FDreamUIAst& InAst, FDreamUIDiagnosticBag& InDiagnostics,
		TArray<FEmitRoute>* OutEmitRoutes = nullptr, const TSet<FString>* InRefusedEvents = nullptr);

	/**
	 * Build the body of every handler Generate named, against the tree the builder made of the same AST.
	 *
	 * The handler takes the parameters of the event its route leaves from -- read off the route the builder recorded
	 * in InOutEventBindings, through the same resolver the run time binds with -- evaluates the arguments like any
	 * `<-` expression, with those parameters in scope by name, and calls the dispatcher. A route whose handler cannot
	 * be built is reported and taken out of InOutEventBindings, so the compile does not go on to say a second time,
	 * and wrongly, that the class has no function of that name.
	 */
	void GenerateEmitHandlers(UDreamWidgetBlueprint* InBlueprint, const FDreamUIAst& InAst, const TArray<FEmitRoute>& InRoutes,
		UDreamWidgetTree* InTree, TArray<FDreamWidgetEventBinding>& InOutEventBindings, FDreamUIDiagnosticBag& InDiagnostics);
}
