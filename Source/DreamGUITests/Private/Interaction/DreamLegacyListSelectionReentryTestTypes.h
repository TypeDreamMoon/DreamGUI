// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interaction/UIListView.h"
#include "DreamLegacyListSelectionReentryTestTypes.generated.h"

/** A listener that records public selection notifications and mutates the list once from inside one. */
UCLASS()
class UDreamLegacyListSelectionReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> EventItems;
	TArray<bool> EventStates;
	TFunction<void(UObject*, bool)> Action;
	int32 ActionCount = 0;

	UFUNCTION()
	void OnChanged(UObject* Item, bool bSelected)
	{
		EventItems.Add(Item);
		EventStates.Add(bSelected);
		if (Action)
		{
			TFunction<void(UObject*, bool)> RunOnce = MoveTemp(Action);
			Action = nullptr;
			++ActionCount;
			RunOnce(Item, bSelected);
		}
	}
};

/** Exposes binding to the existing protected delegate without adding a production-only test API. */
UCLASS()
class UDreamLegacyListSelectionReentryList : public UUIListView
{
	GENERATED_BODY()

public:
	void BindProbe(UDreamLegacyListSelectionReentryProbe* InProbe)
	{
		OnSelectionChanged.AddDynamic(InProbe, &UDreamLegacyListSelectionReentryProbe::OnChanged);
	}
	void SetMode(EUIListSelectionMode InMode) { SelectionMode = InMode; }
};
