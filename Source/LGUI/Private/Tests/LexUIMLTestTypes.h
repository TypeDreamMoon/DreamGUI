#pragma once

#include "Interaction/UIButton.h"
#include "Interaction/UISlider.h"
#include "Interaction/UITextInput.h"
#include "Interaction/UIToggle.h"
#include "XMLSupport/LexUIMLBehaviour.h"
#include "LexUIMLTestTypes.generated.h"

UCLASS()
class ULexUIMLTestBehaviour : public ULexUIMLBehaviour
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
	int32 ClickCount = 0;

	UFUNCTION()
	void HandleClick()
	{
		++ClickCount;
	}
};
