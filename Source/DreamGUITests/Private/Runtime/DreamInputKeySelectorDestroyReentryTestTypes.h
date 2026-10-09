// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamInputKeySelector.h"
#include "DreamInputKeySelectorDestroyReentryTestTypes.generated.h"

/** Removes the real settings control once from a public notification during key capture. */
UCLASS()
class UDreamInputKeySelectorDestroyReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UDreamInputKeySelector> Selector = nullptr;

	bool bDestroyFromNotification = true;
	int32 ReentryPhase = -1;
	int32 MutationCount = 0;
	int32 DestroyPhase = 0;
	int32 DestroyCount = 0;
	int32 ListeningCount = 0;
	int32 ChordCount = 0;
	int32 KeyCount = 0;
	int32 ValueCount = 0;
	bool bOwnerBecameInvalidInsideHandler = false;
	FKey LastPublishedKey = EKeys::G;

	void DestroyOwner()
	{
		if (DestroyCount != 0)return;
		++DestroyCount;
		const TWeakObjectPtr<UDreamInputKeySelector> WeakSelector(Selector.Get());
		Selector->DestroyWidget();
		bOwnerBecameInvalidInsideHandler = !WeakSelector.IsValid();
	}

	UFUNCTION()
	void HandleListeningChanged(bool bInListening)
	{
		++ListeningCount;
		if (!bInListening && MutationCount == 0 && (ReentryPhase == 0 || ReentryPhase == 2))
		{
			++MutationCount;
			if (ReentryPhase == 0)Selector->SetSelectedKey(EKeys::H);
			else Selector->BeginListening();
		}
		if (!bInListening && bDestroyFromNotification && DestroyPhase == 0)DestroyOwner();
	}

	UFUNCTION()
	void HandleChordSelected(FInputChord InChord)
	{
		++ChordCount;
		if (ReentryPhase == 1 && MutationCount == 0)
		{
			++MutationCount;
			Selector->SetSelectedKey(EKeys::H);
		}
		if (bDestroyFromNotification && DestroyPhase == 1)DestroyOwner();
	}

	UFUNCTION()
	void HandleKeySelected(FKey InKey)
	{
		++KeyCount;
		LastPublishedKey = InKey;
		if (bDestroyFromNotification && DestroyPhase == 2)DestroyOwner();
	}

	UFUNCTION()
	void HandleValueChanged(FKey InKey)
	{
		++ValueCount;
		LastPublishedKey = InKey;
	}
};
