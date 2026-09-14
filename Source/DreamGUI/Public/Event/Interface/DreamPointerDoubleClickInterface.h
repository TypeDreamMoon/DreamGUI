// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamPointerEventData.h"
#include "DreamPointerDoubleClickInterface.generated.h"


UINTERFACE(Blueprintable, MinimalAPI)
class UDreamPointerDoubleClickInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for handling DreamUI double click event.
 *
 * The second click of a pair still delivers OnPointerClick first: a list row that opens on double click
 * usually also selects on single click, and making the caller choose would mean re-implementing the
 * single click behaviour in every double-click handler. A handler that wants only the double click
 * distinguishes them with UDreamPointerEventData::ClickCount.
 */
class DREAMGUI_API IDreamPointerDoubleClickInterface
{
	GENERATED_BODY()
public:
	/**
	 * Called when two clicks land on the same widget inside the event system's DoubleClickTime.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerDoubleClick(UDreamPointerEventData* EventData);
};
