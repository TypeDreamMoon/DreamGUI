// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamPointerEventData.h"
#include "DreamPointerLongPressInterface.generated.h"


UINTERFACE(Blueprintable, MinimalAPI)
class UDreamPointerLongPressInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for handling DreamUI long press -- the press-and-hold that opens a context menu on a
 * phone and a tooltip or a radial menu everywhere else.
 *
 * Fires once per press, at the moment the hold time is reached rather than on release, so a handler
 * can show something while the finger is still down. A press that has already become a drag never
 * gets one: hold-to-drag (UDreamScreenSpaceRaycaster::bHoldToDrag) and long press are two readings of
 * the same gesture, and the drag is the one the player can see happening.
 */
class DREAMGUI_API IDreamPointerLongPressInterface
{
	GENERATED_BODY()
public:
	/**
	 * Called when the trigger has been held on this widget for the event system's LongPressTime.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerLongPress(UDreamPointerEventData* EventData);
};
