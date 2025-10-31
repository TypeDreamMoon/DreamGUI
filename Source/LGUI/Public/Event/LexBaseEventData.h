// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LexBaseEventData.generated.h"

/** event execute type */
UENUM(BlueprintType, Category = LGUI)
enum class ELexUIEventFireType :uint8
{
	/** event will call on target actor and all components of the actor */
	TargetActorAndAllItsComponents,
	/** event will call on all components of target actor */
	OnlyTargetComponent,
	/** event will call only on target actor */
	OnlyTargetActor,
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexUIPointerEventType :uint8
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
UENUM(BlueprintType, Category = LGUI)
enum class ELexUIMouseButtonType :uint8
{
	Left,Middle,Right,
	/** UserDefinedX is for custom defined input buttun type */
	UserDefined1,
	UserDefined2,
	UserDefined3,
	UserDefined4,
	UserDefined5,
	UserDefined6,
	UserDefined7,
	UserDefined8,
};
UCLASS(BlueprintType, classGroup = LGUI)
class LGUI_API ULexBaseEventData :public UObject
{
	GENERATED_BODY()
public:
	/** current selected component. when call Deselect interface, this is also the new selected component*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI")
		TObjectPtr<USceneComponent> SelectedComponent = nullptr;
	/** event type*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI")
		ELexUIPointerEventType EventType = ELexUIPointerEventType::Click;

	ELexUIEventFireType SelectedComponentEventFireType = ELexUIEventFireType::TargetActorAndAllItsComponents;

	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToString (LexIEventData)", CompactNodeTitle = ".", BlueprintAutocast), Category = "LGUI") virtual FString ToString()const 
	{
		return TEXT("");
	};
};
