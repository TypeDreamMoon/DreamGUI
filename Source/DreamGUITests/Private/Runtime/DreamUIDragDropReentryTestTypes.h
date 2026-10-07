// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/DreamUserWidget.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUIDragDrop.h"
#include "DreamUIDragDropReentryTestTypes.generated.h"

/**
 * A drag-drop broadcast that does something: a test's action, run from inside the handler -- what a drop target's
 * OnDragLeave does when it cancels the drag, or ends another one -- the first time the broadcast arrives.
 *
 * Every drag-drop event is a dynamic multicast, which binds only a UFUNCTION on a UObject; this is that object, with
 * the action a test hands it.
 */
UCLASS()
class UDreamDragDropReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	/** Run from inside the next broadcast, then forgotten. */
	TFunction<void()> Action;
	int32 CallCount = 0;

	UFUNCTION()
	void OnOperation(UDreamDragDropOperation* Operation)
	{
		++CallCount;
		if (Action)
		{
			TFunction<void()> RunOnce = MoveTemp(Action);
			Action = nullptr;
			RunOnce();
		}
	}
};

/** Runs a one-shot action from the acceptance query, before any hover event. */
UCLASS()
class UDreamDropAcceptanceReentryTarget : public UDreamUIDropTarget
{
	GENERATED_BODY()

public:
	TFunction<void()> Action;
	int32 QueryCount = 0;

	virtual bool CanAcceptDrop_Implementation(UDreamDragDropOperation* Operation) override
	{
		++QueryCount;
		if (Action)
		{
			TFunction<void()> RunOnce = MoveTemp(Action);
			Action = nullptr;
			RunOnce();
		}
		return true;
	}
};

/** Records visual instances and runs a mutation from their real initialization hook. */
UCLASS()
class UDreamDragVisualReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void(UDreamUserWidget*)> Action;
	int32 MutationCount = 0;
	TArray<TWeakObjectPtr<UDreamUserWidget>> InitializedVisuals;
	TArray<TWeakObjectPtr<UDreamWidget>> InitialHolders;
	TWeakObjectPtr<UDreamWidget> ScreenRoot;

	void OnVisualInitialized(UDreamUserWidget* InVisual)
	{
		InitializedVisuals.Add(InVisual);
		// Initialize precedes parent attachment. The holder was already added to the screen,
		// and the innermost creation has the last one, including nested replacement creation.
		UDreamWidget* Holder = nullptr;
		if (ScreenRoot.IsValid())
		{
			const auto& Children = ScreenRoot->GetChildren();
			for (int32 Index = Children.Num() - 1; Index >= 0; --Index)
			{
				if (IsValid(Children[Index]) && Children[Index]->GetDisplayName() == TEXT("DreamUIDragVisual"))
				{
					Holder = Children[Index];
					break;
				}
			}
		}
		InitialHolders.Add(Holder);
		if (Action)
		{
			TFunction<void(UDreamUserWidget*)> RunOnce = MoveTemp(Action);
			Action = nullptr;
			++MutationCount;
			RunOnce(InVisual);
		}
	}
};

UCLASS()
class UDreamDragVisualReentryWidget : public UDreamUserWidget
{
	GENERATED_BODY()

public:
	inline static TWeakObjectPtr<UDreamDragVisualReentryProbe> ActiveProbe;

	virtual void NativeOnInitialized() override
	{
		Super::NativeOnInitialized();
		if (UDreamDragVisualReentryProbe* Probe = ActiveProbe.Get())Probe->OnVisualInitialized(this);
	}
};
