// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamWidgetBlueprint;
struct FDreamUIAst;
struct FDreamUIDiagnosticBag;

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
 */
namespace DreamUIExpressionThunks
{
	/** The prefix every generated thunk graph name carries; the cleanup pass keys on it. */
	extern const TCHAR* GeneratedGraphPrefix;

	/**
	 * Remove stale generated graphs, then lower every BindingExpression in InAst: create the
	 * function graph, wire the expression, and rewrite the property's BindingFunction in place.
	 */
	void Generate(UDreamWidgetBlueprint* InBlueprint, FDreamUIAst& InAst, FDreamUIDiagnosticBag& InDiagnostics);
}
