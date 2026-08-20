// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"
#include "Interaction/UISlider.h"
#include "Interaction/UITextInput.h"
#include "Interaction/UIToggle.h"
#include "XMLSupport/DreamUIMLBehaviour.h"
#include "DreamUIMLTestTypes.generated.h"

UCLASS()
class UDreamUIMLTestBehaviour : public UDreamUIMLBehaviour
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UUIButton> ActionButton;

	UPROPERTY()
	TObjectPtr<UUISlider> AmountSlider;

	UPROPERTY()
	TObjectPtr<UUITextInput> NameInput;

	UPROPERTY()
	TObjectPtr<UUIToggle> ReadyToggle;

	UPROPERTY()
	TObjectPtr<UDreamText> StatusLabel;

	UPROPERTY()
	TObjectPtr<UDreamWidget> VisibilityPanel;

	UPROPERTY()
	FText StatusText = FText::FromString(TEXT("Waiting"));

	UPROPERTY()
	bool bPanelVisible = true;

	UPROPERTY()
	int32 ClickCount = 0;

	UFUNCTION()
	void HandleClick()
	{
		++ClickCount;
	}
};
