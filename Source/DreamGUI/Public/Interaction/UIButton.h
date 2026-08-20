// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Event/Interface/DreamPointerClickInterface.h"
#include "UISelectable.h"
#include "Event/DreamUIEventDelegate.h"
#include "UIButton.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FUIButtonClickedEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FUIButtonSimpleEvent);

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIButton : public UUISelectable, public IDreamPointerClickInterface
{
	GENERATED_BODY()
protected:

	UPROPERTY(EditAnywhere, Category = "DreamGUI-Button")
	FDreamUIEventDelegate OnClick = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Empty);
	FSimpleMulticastDelegate OnClickCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Toggle", DisplayName="OnClick")
	FUIButtonClickedEvent OnClickBP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Button", DisplayName="OnHovered")
	FUIButtonSimpleEvent OnHoveredBP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Button", DisplayName="OnUnhovered")
	FUIButtonSimpleEvent OnUnhoveredBP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Button", DisplayName="OnPressed")
	FUIButtonSimpleEvent OnPressedBP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Button", DisplayName="OnReleased")
	FUIButtonSimpleEvent OnReleasedBP;
	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData)override;
public:
	FSimpleMulticastDelegate& GetOnClickEvent(){return OnClickCPP;}
};
