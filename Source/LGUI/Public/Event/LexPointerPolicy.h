// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "GenericPlatform/ICursor.h"

class ULexWidget;

/**
 * Two decisions the pointer pipeline makes on every hit, pulled out of it so they can be tested.
 *
 * Both were previously inline in ULexPointerInputModule, which is only reachable with a world, a
 * registered raycaster and a live event system -- which is a large part of why the interaction layer
 * had no test coverage at all, and why a commented-out line was able to disable the navigation
 * cursor without anything going red. Neither of these needs any of that machinery: they are
 * questions about widgets, so they are answered here where a test can ask them directly.
 */
namespace LexPointerPolicy
{
	/**
	 * Whether a raycast hit should be ignored because it IS the thing currently being dragged.
	 *
	 * A dragged widget stays raycastable and sits directly under the cursor, so without this it wins
	 * its own hit test every frame and the pointer never reaches what is underneath. That is not a
	 * cosmetic problem: the drop event is dispatched to the widget under the cursor and skipped when
	 * that widget is the drag source, so leaving the source in the results makes the drop event
	 * unreachable in the ordinary case -- drag a card over a slot and nothing can ever be told about
	 * it. Enter and exit events are equally affected, which is why drag-enter and drag-leave
	 * highlighting has to be hand-rolled by every consumer today.
	 *
	 * Descendants are excluded too: the drag visual is usually a subtree, and half of it blocking
	 * the pointer would be worse than all of it.
	 *
	 * Deliberately NOT implemented by clearing the widget's raycastable flag for the duration. That
	 * is authored state -- serialized, undoable, and observable by anything else that asks -- and a
	 * drag that ends abnormally would leave it wrong.
	 */
	LGUI_API bool ShouldIgnoreHitWhileDragging(const ULexWidget* InHitWidget, const ULexWidget* InDragWidget, bool bInIsDragging);

	/**
	 * The cursor to show for a hover stack, innermost widget first.
	 *
	 * ULexWidget::Cursor has existed, been editable in the details panel, and done absolutely
	 * nothing -- its getter had no caller anywhere in the plugin. A property that a designer can set
	 * and that silently does nothing is worse than one that is missing, so this resolves it.
	 *
	 * The innermost widget claiming a cursor wins, and a widget claiming EMouseCursor::Default is
	 * treated as claiming nothing so that a container does not silently override a child that wants
	 * a hand. Returns false when nothing in the stack claims one, which is the caller's signal to
	 * restore the default rather than write one.
	 */
	LGUI_API bool ResolveCursor(TArrayView<ULexWidget* const> InHoverStackInnermostFirst, EMouseCursor::Type& OutCursor);
}
