// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamBaseEventData.generated.h"

class UDreamWidget;

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUIPointerEventType :uint8
{
	Click = 0,
	Enter = 1,
	Exit = 2,
	Down = 3,
	Up = 4,
	BeginDrag = 5,
	Drag = 6,
	EndDrag = 7,
	Scroll = 8,
	DragDrop = 11,
	Select = 12,
	Deselect = 13,
	/** The second press on the same widget inside DoubleClickTime, sent instead of its Down. Its Up and Click still follow. */
	DoubleClick = 15,
	/** The trigger held on one widget for LongPressTime without becoming a drag. */
	LongPress = 16,
	/** Two pointers moving together or apart. Carried by UDreamGestureEventData. */
	Pinch = 17,
	/** One pointer travelling far enough, fast enough, in one direction. Carried by UDreamGestureEventData. */
	Swipe = 18,
	// There was a Navigate = 14 here. Every value in this enum is written by exactly one
	// ExecuteEvent_On* function and read by handlers that switch on it; nothing ever wrote that one, so
	// no handler could ever see it. Navigation reports itself through the events it actually dispatches
	// (Enter/Exit/Select on the widget it moved to), which is why nothing needed it.
};
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUIMouseButtonType :uint8
{
	Left,Middle,Right,
	/**
	 * UserDefinedX is for custom defined input button type.
	 *
	 * Deliberately kept although no plugin code produces one: these are the extension point for a
	 * project that has a fourth or fifth button, which reaches them by passing one to
	 * UDreamStandaloneInputModule::InputTrigger. "Unused inside the plugin" is what they are for.
	 */
	UserDefined1,
	UserDefined2,
	UserDefined3,
	UserDefined4,
	UserDefined5,
	UserDefined6,
	UserDefined7,
	UserDefined8,
};
UCLASS(BlueprintType, classGroup = DreamGUI)
class DREAMGUI_API UDreamBaseEventData :public UObject
{
	GENERATED_BODY()
public:
	/** current selected component. when call Deselect interface, this is also the new selected component*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
		TObjectPtr<UDreamWidget> SelectedComponent = nullptr;
	/** event type*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI")
		EDreamUIPointerEventType EventType = EDreamUIPointerEventType::Click;

	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToString (DreamIEventData)", CompactNodeTitle = ".", BlueprintAutocast), Category = "DreamGUI")
	virtual FString ToString()const 
	{
		return TEXT("");
	};
};
