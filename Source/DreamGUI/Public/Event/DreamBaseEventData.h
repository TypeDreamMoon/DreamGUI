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
	Navigate = 14,
};
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUIMouseButtonType :uint8
{
	Left,Middle,Right,
	/** UserDefinedX is for custom defined input button type */
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
