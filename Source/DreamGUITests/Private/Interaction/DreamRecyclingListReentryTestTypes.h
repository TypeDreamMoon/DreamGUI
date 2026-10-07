// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interaction/UIListView.h"
#include "UObject/Class.h"
#include "DreamRecyclingListReentryTestTypes.generated.h"

enum class EDreamRecyclingCallback : uint8
{
	Assigned,
	Generated,
	Before,
	After,
	Activated,
	Created,
};

/** A single mutation run from a real reflected row callback or data-source callback. */
UCLASS()
class UDreamRecyclingReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	EDreamRecyclingCallback Trigger = EDreamRecyclingCallback::Assigned;
	TFunction<void()> Action;
	int32 MutationCount = 0;

	void Run(EDreamRecyclingCallback InCallback)
	{
		if (Trigger == InCallback && Action)
		{
			TFunction<void()> RunOnce = MoveTemp(Action);
			Action = nullptr;
			++MutationCount;
			RunOnce();
		}
	}

	UFUNCTION()
	void OnGenerated(UObject* Item, UUIListEntry* Entry)
	{
		Run(EDreamRecyclingCallback::Generated);
	}
};

/** ProcessEvent drives the BlueprintImplementableEvent without requiring a generated Blueprint asset. */
UCLASS()
class UDreamRecyclingReentryEntry : public UUIListEntry
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TObjectPtr<UDreamRecyclingReentryProbe> Probe;

	virtual void OnEnable() override
	{
		Super::OnEnable();
		if (IsValid(this) && IsValid(Probe) && GetItemIndex() != INDEX_NONE)
		{
			Probe->Run(EDreamRecyclingCallback::Activated);
		}
	}

	virtual void ProcessEvent(UFunction* Function, void* Parms) override
	{
		Super::ProcessEvent(Function, Parms);
		if (!IsValid(this) || !IsValid(Probe) || GetItemIndex() == INDEX_NONE)return;
		if (Function->GetFName() == GET_FUNCTION_NAME_CHECKED(UUIListEntry, ReceiveOnListItemAssigned))
		{
			Probe->Run(EDreamRecyclingCallback::Assigned);
		}

	}
};

UCLASS()
class UDreamRecyclingReentryList : public UUIListView
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TObjectPtr<UDreamRecyclingReentryProbe> Probe;

	void BindProbe(UDreamRecyclingReentryProbe* InProbe)
	{
		Probe = InProbe;
		OnEntryGenerated.AddDynamic(InProbe, &UDreamRecyclingReentryProbe::OnGenerated);
	}

	virtual void InitOnCreate_Implementation(UDreamUIBehaviour* Component) override
	{
		Super::InitOnCreate_Implementation(Component);
		if (IsValid(this) && IsValid(Probe))Probe->Run(EDreamRecyclingCallback::Created);
	}

	virtual void BeforeSetCell_Implementation() override
	{
		if (IsValid(Probe))Probe->Run(EDreamRecyclingCallback::Before);
	}

	virtual void AfterSetCell_Implementation() override
	{
		if (IsValid(Probe))Probe->Run(EDreamRecyclingCallback::After);
	}
};

/** A second, independent source, so a callback can replace the object as well as the item count. */
UCLASS()
class UDreamRecyclingReplacementSource : public UObject, public IUIRecyclableScrollViewDataSource
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Items;

	virtual int GetItemCount_Implementation() override { return Items.Num(); }
	virtual void InitOnCreate_Implementation(UDreamUIBehaviour* Component) override {}
	virtual void BeforeSetCell_Implementation() override {}
	virtual void AfterSetCell_Implementation() override {}
	virtual void SetCell_Implementation(UDreamUIBehaviour* Component, int Index) override
	{
		if (UUIListEntry* Entry = Cast<UUIListEntry>(Component); Entry != nullptr && Items.IsValidIndex(Index))
		{
			Entry->Assign(nullptr, Items[Index], Index, 0, false);
		}
	}
};
