// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamTextInput.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamTextCommitFanoutTestTypes.generated.h"

UCLASS()
class UDreamTextCommitFanoutProbe : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UDreamTextInput> Field = nullptr;
	bool bReplaceFromBlueprint = false;
	TArray<FString> NativeValues;
	TArray<FString> BlueprintValues;
	TArray<FString> AuthoredValues;

	UFUNCTION()
	void HandleBlueprint(FString InText)
	{
		BlueprintValues.Add(InText);
		if (bReplaceFromBlueprint)Field->SetText(TEXT("next"));
	}

	UFUNCTION()
	void HandleAuthored(FString InText)
	{
		AuthoredValues.Add(InText);
	}
};
