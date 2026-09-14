// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamKeyEventData.h"
#include "DreamKeyInterface.generated.h"

class UDreamWidget;

UINTERFACE(Blueprintable, MinimalAPI)
class UDreamKeyInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Keys, characters and analog samples, offered to the FOCUSED widget and bubbled up from there.
 *
 * The counterpart of the pointer interfaces for input that has no position. DreamGUI had no such
 * channel: a key reached the action router, which matches it against named bindings, and that is a
 * different question -- "is this key the Confirm action" rather than "does the thing the player is
 * looking at want this key". A text box, a key-rebinding row and a widget with its own shortcut all
 * need the second one, and each had to invent its own way to get at raw input.
 *
 * WHERE IT ENTERS: UDreamUIActionRouter::HandleKeyWithModifiers, ahead of the named bindings. That
 * order is Slate's and UMG's -- focus sees a key before a global binding does -- and it is what lets
 * a text box hold on to Escape while the same key still closes a dialog when nothing is focused.
 *
 * Handled, not bubble: a key handler sets UDreamKeyEventData::bHandled and the walk stops. A handled
 * key is also not offered to the action router, and therefore not read as navigation.
 */
class DREAMGUI_API IDreamKeyInterface
{
	GENERATED_BODY()
public:
	/** A key went down on this player. Set EventData->bHandled to keep it. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	void OnKeyDown(UDreamKeyEventData* EventData);

	/** A key came up on this player. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	void OnKeyUp(UDreamKeyEventData* EventData);

	/** A character was produced -- what a text box wants, and what a key code cannot give it. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	void OnKeyChar(UDreamKeyEventData* EventData);

	/** An analog axis moved: a stick, a trigger. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	void OnAnalogValueChanged(UDreamKeyEventData* EventData);
};

/**
 * Offer a key event to a widget and everything above it, stopping at the first handler that keeps it.
 *
 * Free functions rather than members of the event system, because the dispatch is the whole of the
 * mechanism: find the behaviours on a widget that speak the interface, call the one function this
 * event's channel names, stop when somebody sets bHandled, then go up. The same walk the pointer
 * events make, minus the hit testing they need and a key does not.
 *
 * @return true when somebody handled it, which the caller must read as "this key is spent".
 */
namespace DreamUIKeyDispatch
{
	/** Offer InEventData to InWidget only. */
	DREAMGUI_API bool DispatchToWidget(UDreamWidget* InWidget, UDreamKeyEventData* InEventData);
	/** Offer InEventData to InWidget, then to each ancestor, until one keeps it. */
	DREAMGUI_API bool DispatchBubbling(UDreamWidget* InFocusedWidget, UDreamKeyEventData* InEventData);
}
