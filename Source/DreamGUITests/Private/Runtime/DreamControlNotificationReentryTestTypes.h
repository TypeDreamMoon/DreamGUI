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
	bool bCloseAgainFromClosed = false;
	bool bDestroyFromClosed = false;
	bool bEndPlayFromClosed = false;
	int32 NestedCloseAttempts = 0;

	UFUNCTION()
	void HandleButtonClicked(FName InResult)
	{
		++ClickCount;
		if (ButtonAction == 1 || ButtonAction == 3)
		{
			Dialog->Close(TEXT("Cancel"));
			if (ButtonAction == 3)Dialog->SetWidgetActive(true);
		}
		else if (ButtonAction == 2)
		{
			Dialog->DestroyWidget();
		}
		else if (ButtonAction == 4)
		{
			Dialog->EndPlay();
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
		if (bDestroyFromClosed)Dialog->DestroyWidget();
		if (bEndPlayFromClosed)Dialog->EndPlay();
		if (bCloseAgainFromClosed && NestedCloseAttempts == 0)
		{
			++NestedCloseAttempts;
			Dialog->Close(TEXT("Cancel"));
		}
	}
};
