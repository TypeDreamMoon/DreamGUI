// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamDialog.h"
#include "Controls/DreamTextInput.h"
#include "DreamControlNotificationReentryTestTypes.generated.h"

UCLASS()
class UDreamTextCommitReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UDreamTextInput> Field = nullptr;
	bool bReplaceOnSubmit = false;
	TArray<FString> Submitted;
	TArray<FString> Committed;

	UFUNCTION()
	void HandleSubmitted(const FString& InText)
	{
		Submitted.Add(InText);
		if (bReplaceOnSubmit)
		{
			Field->SetText(TEXT("next"));
		}
	}

	UFUNCTION()
	void HandleCommitted(const FString& InText)
	{
		Committed.Add(InText);
	}
};

UCLASS()
class UDreamDialogCloseReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UDreamDialog> Dialog = nullptr;
	int32 ButtonAction = 0;
	int32 ClickCount = 0;
	TArray<FName> ClosedResults;
	int32 ClosedAfterDestruction = 0;

	UFUNCTION()
	void HandleButtonClicked(FName InResult)
	{
		++ClickCount;
		if (ButtonAction == 1)
		{
			Dialog->Close(TEXT("Cancel"));
		}
		else if (ButtonAction == 2)
		{
			Dialog->DestroyWidget();
		}
	}

	UFUNCTION()
	void HandleClosed(FName InResult)
	{
		ClosedResults.Add(InResult);
		if (!IsValid(Dialog))
		{
			++ClosedAfterDestruction;
		}
	}
};
