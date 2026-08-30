// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"

class UDreamWidgetTree;

/**
 * AST in, widget hierarchy out -- the first stage that touches reflection, and the only one that
 * decides what a value MEANS.
 *
 * The parser deliberately records shape and nothing else (see DreamUIAst.h), so `Left` is still an
 * identifier and `(400, 240)` is still a two-element tuple when they arrive here. Which of those is
 * an EDreamWidgetVisibility and which is an FVector2D is answered by the FProperty they land on, and
 * that answer only exists at this point in the pipeline. Everything upstream stays free of the
 * runtime's type list, which is what lets the parser's tests be strings in and data out.
 *
 * The tree that comes back is an AUTHORING tree, not a live hierarchy: nothing is registered, no
 * behaviour has been given Awake, and no widget has a world. That is what the compiler wants -- it
 * hands the result to UDreamWidgetGeneratedClass as a class template, from which every instance is
 * made. A caller that wants a living hierarchy instances the class; it does not register this.
 *
 * The bindings come out ALONGSIDE the tree rather than on it, because they do not live on a tree:
 * FDreamWidgetPropertyBinding names its widget by variable name and its destination by setter, and
 * both belong to the generated class. Returning them separately is what keeps the compiler from
 * having to walk the tree a second time inventing the same names this pass already computed.
 */
struct DREAMGUI_API FDreamUITextBuilder
{
	/**
	 * Build InAst into a tree owned by InOuter, reporting into OutDiagnostics and appending every
	 * `<-` binding to OutBindings.
	 *
	 * Returns null when this call raised an error, and only then: a bag that already carried the
	 * parser's errors does not, by itself, make the build a failure. Warnings never fail a build --
	 * an unexpanded `for` is a warning, and the rest of the file is still worth having.
	 *
	 * OutBindings is appended to, not reset, so a caller assembling several sources into one class
	 * does not have to keep its own accumulator.
	 */
	static UDreamWidgetTree* Build(const FDreamUIAst& InAst, UObject* InOuter,
		FDreamUIDiagnosticBag& OutDiagnostics, TArray<FDreamWidgetPropertyBinding>& OutBindings,
		TArray<FDreamWidgetEventBinding>* OutEventBindings = nullptr);

	/**
	 * The UDreamVisual class a built-in tag creates, and whether the tag is one at all.
	 *
	 * Two answers rather than one because `Widget` is a known tag whose visual class is null -- a
	 * plain widget with no visual is the ordinary container, not a failure to resolve. Exposed so the
	 * editor's completion and the write-back patcher read the table rather than each keeping a copy;
	 * a second copy is how "the editor offered RectBlock and the compiler rejected it" happens.
	 */
	static UClass* FindVisualClassForTag(const FString& InTag, bool& bOutIsKnownTag);

	/**
	 * The class `+ Xxx` names: an alias, a reflected name, or a full object path.
	 *
	 * Null when nothing resolves or when what resolved is abstract, deprecated, or not something a
	 * widget can carry. The caller raises UnknownBehaviourClass; this reports no diagnostics of its
	 * own so the editor can use it as a plain "is this spelling valid" probe.
	 */
	static UClass* ResolveComponentClass(const FString& InClassName);

	/**
	 * Whether a property can be given a value from text at all, and why not when it cannot.
	 *
	 * Exported because three places have to agree about it and only one of them can be right: the
	 * builder refuses the write, the designer greys the row, and the write-back skips the property
	 * when it walks the tree. Two of those had copied it. A copy that drifts does not fail loudly --
	 * it produces a panel that offers an edit the compiler will drop, which is the exact silent
	 * failure this pipeline exists to remove.
	 *
	 * OutReason is a sentence fragment meant to follow "because", so a caller can put it in a
	 * compile error, a tooltip, or a log line without rewording it.
	 */
	static bool IsWritableFromText(const FProperty* InProperty, FString& OutReason);
};
