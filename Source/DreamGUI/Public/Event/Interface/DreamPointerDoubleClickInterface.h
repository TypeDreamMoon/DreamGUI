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
 * The double click is the SECOND PRESS of a pair, delivered at that press and IN PLACE of its
 * OnPointerDown -- Slate's routing (FSlateApplication::ProcessMouseButtonDoubleClickEvent), so a widget
 * that only listens for downs sees one down per double click, as a UMG widget does. The release that
 * follows is an ordinary OnPointerUp and OnPointerClick, so a list row that selects on a click and
 * opens on a double click does both; ClickCount says which click of a run each event belongs to.
 *
 * Nothing turns an unanswered double click back into a down, in Slate or here. A control that wants
 * the second press as a press does it itself, as SButton and SCheckBox do: UUIButton and UUIToggle
 * answer this by running their own down. Only pointer presses double-click; a key or a pad's confirm
 * never does.
 */
class DREAMGUI_API IDreamPointerDoubleClickInterface
{
	GENERATED_BODY()
public:
	/**
	 * Called at the second press on the same widget, with the same button, inside the event system's
	 * DoubleClickTime of the last click there and within the press raycaster's drag threshold of where
	 * that click was pressed -- instead of OnPointerDown for that press.
	 * @return Allow event bubble up? If all interface of same actor's components return true, then the event can bubble up.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
		bool OnPointerDoubleClick(UDreamPointerEventData* EventData);
};
