// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamGestureEventData.h"
#include "DreamPointerGestureInterface.generated.h"


UINTERFACE(Blueprintable, MinimalAPI)
class UDreamPointerGestureInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for handling DreamUI multi-pointer gestures: pinch and swipe.
 *
 * Dispatched to the widget the gesture's first pointer was pressed on, and bubbling from there, so a
 * scroll view can take a swipe its own row did not handle. The pointers that made the gesture have
 * already dispatched their ordinary press/drag/release events; a gesture is a second reading of the
 * same movement, not a replacement for it.
 */
class DREAMGUI_API IDreamPointerGestureInterface
{
	GENERATED_BODY()
public:
	/**
	 * Two fingers moved together or apart. Called every frame the distance changes by more than the
	 * event system's PinchMinDistanceChange.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerPinch(UDreamGestureEventData* EventData);
	/**
	 * A finger travelled far enough, fast enough, in one direction and then left the glass. Called
	 * once, on release.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerSwipe(UDreamGestureEventData* EventData);
};
