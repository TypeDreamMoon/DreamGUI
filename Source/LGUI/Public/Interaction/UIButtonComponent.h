// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Event/Interface/LexPointerClickInterface.h"
#include "UISelectableComponent.h"
#include "Event/LexUIEventDelegate.h"
#include "UIButtonComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FUIButtonClickedEvent);

UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUIButtonComponent : public UUISelectableComponent, public ILexPointerClickInterface
{
	GENERATED_BODY()
protected:

	UPROPERTY(EditAnywhere, Category = "LGUI-Button")
	FLexUIEventDelegate OnClick = FLexUIEventDelegate(ELexUIEventDelegateParameterType::Empty);
	FSimpleMulticastDelegate OnClickCPP;
	UPROPERTY(BlueprintAssignable, Category = "LGUI-Toggle", DisplayName="OnClick")
	FUIButtonClickedEvent OnClickBP;
	virtual bool OnPointerClick_Implementation(ULexPointerEventData* EventData)override;
public:
	FSimpleMulticastDelegate& GetOnClickEvent(){return OnClickCPP;}
};
