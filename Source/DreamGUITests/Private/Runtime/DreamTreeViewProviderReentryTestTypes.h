// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interaction/UIListView.h"
#include "DreamTreeViewProviderReentryTestTypes.generated.h"

/** The same children answer for the public delegate and the item interface, with one reentrant mutation. */
UCLASS()
class UDreamTreeProviderReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TObjectPtr<UObject> TriggerItem = nullptr;

	TFunction<void()> Action;
	int32 MutationCount = 0;
	TArray<int32> GeneratedRowCounts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> OwnedNodes;

	UFUNCTION()
	void RecordRows(int32 InCount) { GeneratedRowCounts.Add(InCount); }

	UFUNCTION()
	void ProvideChildren(UObject* InItem, TArray<UObject*>& OutChildren);
};

/** An ordinary hierarchy item whose interface asks the shared provider. */
UCLASS()
class UDreamTreeProviderReentryNode : public UObject, public IUITreeViewItem
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Children;

	UPROPERTY(Transient)
	TObjectPtr<UDreamTreeProviderReentryProbe> Probe = nullptr;

	virtual void GetTreeChildren_Implementation(TArray<UObject*>& OutChildren) const override
	{
		if (Probe != nullptr)Probe->ProvideChildren(const_cast<UDreamTreeProviderReentryNode*>(this), OutChildren);
	}
};

inline void UDreamTreeProviderReentryProbe::ProvideChildren(UObject* InItem, TArray<UObject*>& OutChildren)
{
	OutChildren.Reset();
	if (const UDreamTreeProviderReentryNode* Node = Cast<UDreamTreeProviderReentryNode>(InItem))
	{
		for (UObject* Child : Node->Children)OutChildren.Add(Child);
	}
	// A callback can return an answer computed before it replaces the source. The tree must discard
	// that answer along with the interrupted walk, rather than publishing it after the nested walk.
	if (InItem == TriggerItem && Action)
	{
		TFunction<void()> RunOnce = MoveTemp(Action);
		Action = nullptr;
		++MutationCount;
		RunOnce();
	}
}
