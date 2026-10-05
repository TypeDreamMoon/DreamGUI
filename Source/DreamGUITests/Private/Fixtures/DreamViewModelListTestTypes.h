// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "DreamViewModelTestTypes.h"
#include "Event/DreamUIEventDelegate.h"
#include "UObject/Object.h"
#include "DreamViewModelListTestTypes.generated.h"

/*
 * What the view-model list tests (DreamGUI.ViewModel.List, plan 17 section E) drive besides the shared view models:
 * an item that counts the watchers on it, a widget a hand-assembled loop belongs to, and a row widget carrying one event
 * of every kind a loop body's `-> Item.Func` can name.
 */

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDreamTestRouteClickedEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamTestRouteValueEvent, float, Value);
DECLARE_DYNAMIC_DELEGATE(FDreamTestRoutePickedDelegate);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamTestRouteAmountDelegate, float, Value);

/**
 * One row of `Player.Items` that knows how many FieldNotify delegates are on it right now -- the number a row that let
 * go of its item has to bring back down. Counted at the interface, which is where every watcher adds and removes, so it
 * holds whoever the delegates are bound to.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamTestCountingItemVM : public UDreamTestItemVM
{
	GENERATED_BODY()

public:
	int32 LiveDelegateCount = 0;

	virtual FDelegateHandle AddFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FFieldValueChangedDelegate InNewDelegate) override
	{
		const FDelegateHandle Handle = Super::AddFieldValueChangedDelegate(InFieldId, MoveTemp(InNewDelegate));
		LiveDelegateCount += Handle.IsValid() ? 1 : 0;
		return Handle;
	}

	virtual bool RemoveFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FDelegateHandle InHandle) override
	{
		const bool bRemoved = Super::RemoveFieldValueChangedDelegate(InFieldId, InHandle);
		LiveDelegateCount -= bRemoved ? 1 : 0;
		return bRemoved;
	}

	virtual int32 RemoveAllFieldValueChangedDelegates(FDelegateUserObjectConst InUserObject) override
	{
		const int32 Removed = Super::RemoveAllFieldValueChangedDelegates(InUserObject);
		LiveDelegateCount -= Removed;
		return Removed;
	}

	virtual int32 RemoveAllFieldValueChangedDelegates(UE::FieldNotification::FFieldId InFieldId, FDelegateUserObjectConst InUserObject) override
	{
		const int32 Removed = Super::RemoveAllFieldValueChangedDelegates(InFieldId, InUserObject);
		LiveDelegateCount -= Removed;
		return Removed;
	}
};

/**
 * The widget a hand-assembled loop belongs to: a view model variable for the source path to start on, the shape a
 * `viewmodels { DreamTestPlayerVM Player }` entry compiles into. Not FieldNotify: these tests ask the adapters directly,
 * and what the owning widget does when Player changes is the user widget's own suite.
 */
UCLASS(NotBlueprintType, HideDropdown)
class UDreamTestListHost : public UDreamTextUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Test")
	TObjectPtr<UDreamTestPlayerVM> Player = nullptr;
};

/**
 * A row's root carrying one event of every kind a route attaches to: a multicast event with nothing and with a value
 * (what a button's OnClicked and a slider's OnValueChanged are), a single-cast delegate with nothing and with a value
 * (what `=` routes), and an FDreamUIEventDelegate with nothing and with a value (what the Interaction behaviours fire).
 */
UCLASS(NotBlueprintType, HideDropdown)
class UDreamTestRouteWidget : public UDreamWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Test")
	FDreamTestRouteClickedEvent OnClicked;

	UPROPERTY(BlueprintAssignable, Category = "Test")
	FDreamTestRouteValueEvent OnValueChanged;

	UPROPERTY()
	FDreamTestRoutePickedDelegate OnPicked;

	UPROPERTY()
	FDreamTestRouteAmountDelegate OnAmount;

	UPROPERTY()
	FDreamUIEventDelegate OnSubmit = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Empty);

	UPROPERTY()
	FDreamUIEventDelegate OnScale = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Float);
};

/**
 * A handler that takes its own route off the event calling it -- what a row's button does when its click changes the
 * list and the row is re-aimed from inside the click.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamTestSelfRemovingHandler : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TObjectPtr<UDreamTestRouteWidget> Widget = nullptr;

	int32 CallCount = 0;

	UFUNCTION()
	void RemoveSelf()
	{
		++CallCount;
		if (Widget != nullptr)
		{
			Widget->OnSubmit.RemoveRuntimeRoute(this, GET_FUNCTION_NAME_CHECKED(UDreamTestSelfRemovingHandler, RemoveSelf));
		}
	}
};
