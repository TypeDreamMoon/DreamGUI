// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamDropdown.h"
#include "Interaction/UIDropdown.h"
#include "DreamDropdownGenerationReentryTestTypes.generated.h"

/** Cancels or destroys a dropdown once from its public per-row generation notification. */
UCLASS()
class UDreamDropdownGenerationReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UDreamDropdown> Dropdown = nullptr;

	bool bDestroyOwner = false;
	bool bOwnerWasDestroyedInsideCallback = false;
	int32 GeneratedCount = 0;
	int32 MutationCount = 0;
	int32 OpeningCount = 0;

	UFUNCTION()
	void OnGenerated(int32 InIndex, UDreamWidget* InItem)
	{
		++GeneratedCount;
		if (MutationCount == 0)
		{
			++MutationCount;
			if (bDestroyOwner)
			{
				Dropdown->DestroyWidget();
				bOwnerWasDestroyedInsideCallback = !IsValid(Dropdown);
			}
			else
			{
				Dropdown->DropdownBehaviour->Hide();
			}
		}
	}

	UFUNCTION()
	void OnOpening() { ++OpeningCount; }
};
