// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamTabView.h"
#include "DreamTabViewMutationReentryTestTypes.generated.h"

/** Mutates the real tab view once from the public close or generation hook. */
UCLASS()
class UDreamTabViewMutationReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UDreamTabView> View = nullptr;

	bool bCloseAgain = false;
	int32 MutationCount = 0;
	int32 GeneratedCount = 0;
	int32 ValueNotificationCount = 0;
	int32 PublishedIndex = 0;

	UFUNCTION()
	void HandleTabClosed(int32 InIndex)
	{
		if (MutationCount != 0)return;
		++MutationCount;
		if (bCloseAgain)View->CloseTab(InIndex);
		else View->MoveTab(InIndex, 2);
	}

	UFUNCTION()
	void HandleTabGenerated(int32 InIndex, UDreamWidget* InTab)
	{
		++GeneratedCount;
		if (MutationCount != 0)return;
		++MutationCount;
		View->SetTabLabels({FText::AsCultureInvariant(TEXT("New A")), FText::AsCultureInvariant(TEXT("New B")), FText::AsCultureInvariant(TEXT("New C"))});
	}
	UFUNCTION()
	void HandleTabChanged(int32 InIndex)
	{
		if (MutationCount != 0)return;
		++MutationCount;
		PublishedIndex = 2;
		View->SetActiveTabIndexWithoutNotify(2);
	}

	UFUNCTION()
	void HandleValueChanged(int32 InIndex)
	{
		++ValueNotificationCount;
		PublishedIndex = InIndex;
	}
};
