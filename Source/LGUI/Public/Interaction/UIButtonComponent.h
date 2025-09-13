// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Event/Interface/LexPointerClickInterface.h"
#include "UISelectableComponent.h"
#include "Event/LGUIEventDelegate.h"
#include "Event/LexDelegateDeclaration.h"
#include "LGUIDelegateHandleWrapper.h"
#include "UIButtonComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FUIButtonClickedEvent);
DECLARE_DYNAMIC_DELEGATE(FUIButtonClickedDelegate);

UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUIButtonComponent : public UUISelectableComponent, public ILexPointerClickInterface
{
	GENERATED_BODY()
protected:

	UPROPERTY(EditAnywhere, Category = "LGUI-Button")
	FLGUIEventDelegate OnClick = FLGUIEventDelegate(ELGUIEventDelegateParameterType::Empty);
	FSimpleMulticastDelegate OnClickCPP;
	virtual bool OnPointerClick_Implementation(ULexPointerEventData* eventData)override;
public:
	UPROPERTY(BlueprintAssignable, Category = "LGUI-Toggle", DisplayName="OnClick")
	FUIButtonClickedEvent OnClickBP;
	
	/** Register click event */
	FDelegateHandle RegisterClickEvent(const FSimpleDelegate& InDelegate);
	/** Register click event */
	FDelegateHandle RegisterClickEvent(const TFunction<void()>& InFunction);
	/** Unregister click event */
	void UnregisterClickEvent(const FDelegateHandle& InHandle);

	UFUNCTION(BlueprintCallable, Category = "LGUI-Button")
		FLGUIDelegateHandleWrapper RegisterClickEvent(const FUIButtonClickedDelegate& InDelegate);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Button")
		void UnregisterClickEvent(const FLGUIDelegateHandleWrapper& InDelegateHandle);
};
