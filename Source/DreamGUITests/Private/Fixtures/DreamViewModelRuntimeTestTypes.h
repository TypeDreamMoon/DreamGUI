// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamFieldNotification.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "DreamViewModelTestTypes.h"
#include "INotifyFieldValueChanged.h"
#include "DreamViewModelRuntimeTestTypes.generated.h"

/*
 * What the view-model run-time tests (DreamGUI.ViewModel.Core.*, DreamGUI.ViewModel.Runtime.*) need beyond the shared
 * view models in DreamViewModelTestTypes.h: view models that COUNT the delegates they hold, which is the only way to
 * see "every subscription came back off" from outside; an object that implements INotifyFieldValueChanged by hand,
 * with no UDreamViewModel and no UHT descriptor; and the widget classes a compiled .dui or a hand-built Blueprint
 * derives from.
 */

/**
 * A DreamTestPlayerVM that knows how many delegates it holds. The count moves with every add and remove made through
 * the INotifyFieldValueChanged surface -- which is the only surface a binding uses -- so zero means nobody is
 * listening any more.
 */
UCLASS(Transient, NotBlueprintable, HideDropdown)
class UDreamTestCountingPlayerVM : public UDreamTestPlayerVM
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

/** The same count on the second hop of `Player.Stats.ClassName`. */
UCLASS(Transient, NotBlueprintable, HideDropdown)
class UDreamTestCountingStatsVM : public UDreamTestStatsVM
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
 * A view model that is not a UDreamViewModel: INotifyFieldValueChanged implemented by hand, with a descriptor written
 * out instead of generated -- the shape of any third-party notifying object. It announces Value and Next; Silent is a
 * plain member nobody announces. Its properties carry no FieldNotify specifier on purpose: UHT would generate a
 * descriptor deriving from the base class's, and UObject has none.
 */
UCLASS(Transient, NotBlueprintable, HideDropdown)
class UDreamTestHandNotifyModel : public UObject, public INotifyFieldValueChanged
{
	GENERATED_BODY()

public:
	struct FHandDescriptor : public ::UE::FieldNotification::IClassDescriptor
	{
		virtual void ForEachField(const UClass* Class, TFunctionRef<bool(::UE::FieldNotification::FFieldId FieldId)> Callback) const override
		{
			if (Callback(ValueField()))
			{
				Callback(NextField());
			}
		}
	};

	static ::UE::FieldNotification::FFieldId ValueField() { return ::UE::FieldNotification::FFieldId(FName(TEXT("Value")), 0); }
	static ::UE::FieldNotification::FFieldId NextField() { return ::UE::FieldNotification::FFieldId(FName(TEXT("Next")), 1); }

	UPROPERTY(BlueprintReadWrite, Category = "Test")
	float Value = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Test")
	TObjectPtr<UDreamTestHandNotifyModel> Next = nullptr;

	/** Not in the descriptor: announced by nothing. */
	UPROPERTY(BlueprintReadWrite, Category = "Test")
	float Silent = 0.f;

	int32 LiveDelegateCount = 0;

	void SetValue(float InValue)
	{
		Value = InValue;
		BroadcastFieldValueChanged(ValueField());
	}

	void SetNext(UDreamTestHandNotifyModel* InNext)
	{
		Next = InNext;
		BroadcastFieldValueChanged(NextField());
	}

	//~ Begin INotifyFieldValueChanged Interface
	virtual FDelegateHandle AddFieldValueChangedDelegate(::UE::FieldNotification::FFieldId InFieldId, FFieldValueChangedDelegate InNewDelegate) override
	{
		const FDelegateHandle Handle = Delegates.AddFieldValueChangedDelegate(this, InFieldId, MoveTemp(InNewDelegate));
		LiveDelegateCount += Handle.IsValid() ? 1 : 0;
		return Handle;
	}

	virtual bool RemoveFieldValueChangedDelegate(::UE::FieldNotification::FFieldId InFieldId, FDelegateHandle InHandle) override
	{
		const bool bRemoved = Delegates.RemoveFieldValueChangedDelegate(this, InFieldId, InHandle);
		LiveDelegateCount -= bRemoved ? 1 : 0;
		return bRemoved;
	}

	virtual int32 RemoveAllFieldValueChangedDelegates(FDelegateUserObjectConst InUserObject) override
	{
		const int32 Removed = Delegates.RemoveAllFieldValueChangedDelegates(this, InUserObject);
		LiveDelegateCount -= Removed;
		return Removed;
	}

	virtual int32 RemoveAllFieldValueChangedDelegates(::UE::FieldNotification::FFieldId InFieldId, FDelegateUserObjectConst InUserObject) override
	{
		const int32 Removed = Delegates.RemoveAllFieldValueChangedDelegates(this, InFieldId, InUserObject);
		LiveDelegateCount -= Removed;
		return Removed;
	}

	virtual const ::UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override
	{
		static FHandDescriptor Descriptor;
		return Descriptor;
	}

	virtual void BroadcastFieldValueChanged(::UE::FieldNotification::FFieldId InFieldId) override
	{
		Delegates.BroadcastFieldValueChanged(this, InFieldId);
	}
	//~ End INotifyFieldValueChanged Interface

private:
	FDreamFieldNotificationDelegates Delegates;
};

/**
 * The parent class of the compiled .dui fixtures. Records what GetViewModel(InitializedProbe) answered INSIDE
 * NativeOnInitialized -- the moment On Initialized runs -- which is what "a `= new` entry exists before On
 * Initialized" is asserted with.
 */
UCLASS(NotBlueprintType, HideDropdown)
class UDreamTestViewModelWidgetBase : public UDreamTextUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Test")
	TObjectPtr<UObject> SeenAtInitialized = nullptr;

	/** The entry probed; the .dui fixtures that test `= new` name theirs Settings. */
	FName InitializedProbe = FName(TEXT("Settings"));

	virtual void NativeOnInitialized() override
	{
		Super::NativeOnInitialized();
		SeenAtInitialized = GetViewModel(InitializedProbe);
	}
};

/** A single-cast dynamic delegate: what an `Event = Handler` route binds, replacing its one listener. */
DECLARE_DYNAMIC_DELEGATE(FDreamTestSingleCastEvent);

/**
 * A widget whose event is single-cast -- the FDelegateProperty kind of event, beside the multicast ones controls have.
 * BlueprintType: the compiler declares a class variable of it for the Blueprint it is placed in.
 */
UCLASS(BlueprintType, HideDropdown)
class UDreamTestSingleCastWidget : public UDreamWidget
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FDreamTestSingleCastEvent OnAsk;

	void Ask() { OnAsk.ExecuteIfBound(); }
};

/**
 * The user widget a single-cast route lands on. Its NativeOnInitialized binds the event to HandleOther first, the way
 * a control or a graph might have bound it before the routes are -- so a route that appended instead of replacing, or
 * did not bind at all, is told apart from one that replaced.
 */
UCLASS(NotBlueprintType, HideDropdown)
class UDreamTestRouteHostWidget : public UDreamUserWidget
{
	GENERATED_BODY()

public:
	int32 AskCount = 0;
	int32 OtherCount = 0;

	UFUNCTION()
	void HandleAsk() { ++AskCount; }

	UFUNCTION()
	void HandleOther() { ++OtherCount; }

	virtual void NativeOnInitialized() override
	{
		Super::NativeOnInitialized();
		if (UDreamTestSingleCastWidget* Probe = Cast<UDreamTestSingleCastWidget>(GetWidgetFromName(FName(TEXT("Probe")))))
		{
			Probe->OnAsk.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UDreamTestRouteHostWidget, HandleOther));
		}
	}
};
