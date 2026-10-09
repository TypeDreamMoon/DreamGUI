// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamDropdown.h"
#include "Core/DreamUIBehaviour.h"
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

	bool bReenterFromRowAwake = false;
	bool bDestroyOwner = false;
	bool bReopenWithNewOptions = false;
	bool bReopenWithSameOptions = false;
	bool bOwnerWasDestroyedInsideCallback = false;
	TWeakObjectPtr<UDreamWidget> CanceledAwakeRow;
	int32 AwakeRowCount = 0;
	int32 GeneratedCount = 0;
	int32 MutationCount = 0;
	int32 OpeningCount = 0;

	UFUNCTION()
	void OnGenerated(int32 InIndex, UDreamWidget* InItem)
	{
		++GeneratedCount;
		if (MutationCount == 0 && !bReenterFromRowAwake)
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
				if (bReopenWithNewOptions || bReopenWithSameOptions)
				{
					if (bReopenWithNewOptions)Dropdown->SetOptions({FText::AsCultureInvariant(TEXT("New first")), FText::AsCultureInvariant(TEXT("New second")), FText::AsCultureInvariant(TEXT("New third"))});
					Dropdown->DropdownBehaviour->Show();
				}
			}
		}
	}

	void OnRowAwake(UDreamWidget* InRow)
	{
		if (!bReenterFromRowAwake || !IsValid(Dropdown) || InRow == Dropdown->ItemTemplateNode)return;
		++AwakeRowCount;
		if (MutationCount != 0)return;
		++MutationCount;
		CanceledAwakeRow = InRow;
		Dropdown->DropdownBehaviour->Hide();
		if (bReopenWithNewOptions)
		{
			Dropdown->SetOptions({FText::AsCultureInvariant(TEXT("New first")), FText::AsCultureInvariant(TEXT("New second")), FText::AsCultureInvariant(TEXT("New third"))});
		}
		Dropdown->DropdownBehaviour->Show();
	}

	UFUNCTION()
	void OnOpening() { ++OpeningCount; }
};

/** The real copied row's BeginPlay invokes this before DuplicateWidget returns to the dropdown. */
UCLASS()
class UDreamDropdownRowAwakeReentryBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UDreamDropdownGenerationReentryProbe> Probe = nullptr;

protected:
	virtual void Awake() override
	{
		Super::Awake();
		if (IsValid(Probe))Probe->OnRowAwake(GetWidget());
	}
};
