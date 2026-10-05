// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamFieldNotification.h"
#include "FieldNotificationDelegate.h"
#include "INotifyFieldValueChanged.h"
#include "UObject/Object.h"
#include "DreamViewModel.generated.h"

namespace DreamViewModel
{
	/** Equality as a setter means it: FText has no operator==, and "the same text" is IdenticalTo. */
	template<typename TValue>
	bool IsSameValue(const TValue& InA, const TValue& InB)
	{
		return InA == InB;
	}

	inline bool IsSameValue(const FText& InA, const FText& InB)
	{
		return InA.IdenticalTo(InB);
	}

	/**
	 * The body of a C++ FieldNotify setter: assign when the value changed, then announce the field. False, with no
	 * broadcast, when it did not -- a setter called every frame with the same value costs a comparison and nothing else.
	 */
	template<typename TOwner, typename TValue, typename TNewValue>
	bool SetAndBroadcast(TOwner* InOwner, TValue& InOutMember, TNewValue&& InNewValue, const UE::FieldNotification::FFieldId& InFieldId)
	{
		TValue NewValue(Forward<TNewValue>(InNewValue));
		if (IsSameValue(InOutMember, NewValue))
		{
			return false;
		}
		InOutMember = MoveTemp(NewValue);
		InOwner->BroadcastFieldValueChanged(InFieldId);
		return true;
	}
}

/**
 * DREAM_VM_SET(Member, NewValue) -- the whole of a C++ setter for a `UPROPERTY(FieldNotify)` member of a view model:
 *
 *     UPROPERTY(BlueprintReadOnly, FieldNotify)
 *     float Health = 100.f;
 *
 *     void SetHealth(float InHealth) { DREAM_VM_SET(Health, InHealth); }
 *
 * UHT declares the field id it names (ThisClass::FFieldNotificationClassDescriptor::Health) for every class with a
 * FieldNotify property, so this works in any INotifyFieldValueChanged class, not only UDreamViewModel's. A Blueprint
 * Set node and an UnrealSharp `[FieldNotify]` property setter broadcast on their own and need nothing like it.
 */
#define DREAM_VM_SET(MemberName, NewValue) \
	::DreamViewModel::SetAndBroadcast(this, MemberName, NewValue, ThisClass::FFieldNotificationClassDescriptor::MemberName)

/**
 * The convenient base for a view model: a UObject that announces its fields.
 *
 * Not required. A `viewmodels` entry may name any UObject class, and any class implementing INotifyFieldValueChanged
 * -- UE's own UMVVMViewModelBase included -- is subscribed to the same way. What this base adds is the plumbing such
 * a class would otherwise write itself: the delegate store (FDreamFieldNotificationDelegates), a descriptor that
 * covers Blueprint and UnrealSharp subclasses (both are UBlueprintGeneratedClass, whose FieldNotifies it walks) as
 * well as C++ ones (UHT generates their descriptor on top of this one), a world through its outer, and the design-time
 * flag.
 *
 * A view model never references the widgets that show it. Bindings subscribe to it; it does not know they exist.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, meta = (ShowWorldContextPin = "true"))
class DREAMGUI_API UDreamViewModel : public UObject, public INotifyFieldValueChanged
{
	GENERATED_BODY()

public:
	/**
	 * The fields Blueprint and UnrealSharp subclasses declare, which their generated class records. A C++ subclass with
	 * FieldNotify properties gets a UHT-generated descriptor deriving from this one, which lists its own fields and then
	 * calls this.
	 */
	struct FFieldNotificationClassDescriptor : public ::UE::FieldNotification::IClassDescriptor
	{
		DREAMGUI_API virtual void ForEachField(const UClass* Class, TFunctionRef<bool(::UE::FieldNotification::FFieldId FieldId)> Callback) const override;
	};

	//~ Begin INotifyFieldValueChanged Interface
	virtual FDelegateHandle AddFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FFieldValueChangedDelegate InNewDelegate) override;
	virtual bool RemoveFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FDelegateHandle InHandle) override;
	virtual int32 RemoveAllFieldValueChangedDelegates(FDelegateUserObjectConst InUserObject) override;
	virtual int32 RemoveAllFieldValueChangedDelegates(UE::FieldNotification::FFieldId InFieldId, FDelegateUserObjectConst InUserObject) override;
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	virtual void BroadcastFieldValueChanged(UE::FieldNotification::FFieldId InFieldId) override;
	//~ End INotifyFieldValueChanged Interface

	UFUNCTION(BlueprintCallable, Category = "FieldNotify", meta = (DisplayName = "Add Field Value Changed Delegate", ScriptName = "AddFieldValueChangedDelegate"))
	void K2_AddFieldValueChangedDelegate(FFieldNotificationId FieldId, FFieldValueChangedDynamicDelegate Delegate);

	UFUNCTION(BlueprintCallable, Category = "FieldNotify", meta = (DisplayName = "Remove Field Value Changed Delegate", ScriptName = "RemoveFieldValueChangedDelegate"))
	void K2_RemoveFieldValueChangedDelegate(FFieldNotificationId FieldId, FFieldValueChangedDynamicDelegate Delegate);

	/**
	 * Announce a field whose value changed without passing through a broadcasting setter: a C++ member written in bulk,
	 * a value computed from others (`GoldText` after `Gold` changed).
	 */
	UFUNCTION(BlueprintCallable, Category = "FieldNotify", meta = (DisplayName = "Broadcast Field Value Changed", ScriptName = "BroadcastFieldValueChanged"))
	void K2_BroadcastFieldValueChanged(FFieldNotificationId FieldId);

	/**
	 * True on an instance the designer's preview made for a `= new` entry: no game is running, no subsystem a game
	 * would set up exists. A view model that reaches into the game in its initialization checks this first.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|ViewModel")
	bool IsDesignTimeInstance() const { return bDesignTimeInstance; }

	/** Set by the widget that makes the instance, before anything reads it. */
	void SetDesignTimeInstance(bool bInDesignTime) { bDesignTimeInstance = bInDesignTime; }

	/** Through the outer chain -- the widget that made it, or the game instance that registered it -- so world-context nodes work in Blueprint subclasses. Null on the class default object. */
	virtual UWorld* GetWorld() const override;

private:
	FDreamFieldNotificationDelegates NotificationDelegates;

	bool bDesignTimeInstance = false;
};
