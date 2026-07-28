// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Event/Interface/LexPointerClickInterface.h"
#include "UISelectable.h"
#include "Event/LexUIEventDelegate.h"
#include "UIButton.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FUIButtonClickedEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FUIButtonSimpleEvent);

UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUIButton : public UUISelectable, public ILexPointerClickInterface
{
	GENERATED_BODY()
protected:

	UPROPERTY(EditAnywhere, Category = "LGUI-Button")
	FLexUIEventDelegate OnClick = FLexUIEventDelegate(ELexUIEventDelegateParameterType::Empty);
	FSimpleMulticastDelegate OnClickCPP;
	UPROPERTY(BlueprintAssignable, Category = "LGUI-Toggle", DisplayName="OnClick")
	FUIButtonClickedEvent OnClickBP;
	UPROPERTY(BlueprintAssignable, Category = "LGUI-Button", DisplayName="OnHovered")
	FUIButtonSimpleEvent OnHoveredBP;
	UPROPERTY(BlueprintAssignable, Category = "LGUI-Button", DisplayName="OnUnhovered")
	FUIButtonSimpleEvent OnUnhoveredBP;
	UPROPERTY(BlueprintAssignable, Category = "LGUI-Button", DisplayName="OnPressed")
	FUIButtonSimpleEvent OnPressedBP;
	UPROPERTY(BlueprintAssignable, Category = "LGUI-Button", DisplayName="OnReleased")
	FUIButtonSimpleEvent OnReleasedBP;
	virtual bool OnPointerEnter_Implementation(ULexPointerEventData* EventData) override;
	virtual bool OnPointerExit_Implementation(ULexPointerEventData* EventData) override;
	virtual bool OnPointerDown_Implementation(ULexPointerEventData* EventData) override;
	virtual bool OnPointerUp_Implementation(ULexPointerEventData* EventData) override;
	virtual bool OnPointerClick_Implementation(ULexPointerEventData* EventData)override;
public:
	FSimpleMulticastDelegate& GetOnClickEvent(){return OnClickCPP;}
};
