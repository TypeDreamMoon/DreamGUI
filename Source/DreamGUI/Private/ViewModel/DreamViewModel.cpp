// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "ViewModel/DreamViewModel.h"

#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"

// The same INotifyFieldValueChanged plumbing UDreamUserWidget carries (DreamUserWidget.cpp), on a plain UObject: a
// view model is the other half of the binding, and the two halves answer the descriptor and the delegate calls alike.

void UDreamViewModel::FFieldNotificationClassDescriptor::ForEachField(const UClass* Class, TFunctionRef<bool(::UE::FieldNotification::FFieldId FieldId)> Callback) const
{
	// The fields a Blueprint or an UnrealSharp class declared: both are UBlueprintGeneratedClass, which records its
	// FieldNotifies (with its parents', the `true`). A C++ subclass's own fields are listed by the UHT descriptor that
	// derives from this one before it calls here.
	if (const UBlueprintGeneratedClass* BPClass = Cast<const UBlueprintGeneratedClass>(Class))
	{
		BPClass->ForEachFieldNotify(Callback, true);
	}
}

FDelegateHandle UDreamViewModel::AddFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FFieldValueChangedDelegate InNewDelegate)
{
	return NotificationDelegates.AddFieldValueChangedDelegate(this, InFieldId, MoveTemp(InNewDelegate));
}

bool UDreamViewModel::RemoveFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FDelegateHandle InHandle)
{
	return NotificationDelegates.RemoveFieldValueChangedDelegate(this, InFieldId, InHandle);
}

int32 UDreamViewModel::RemoveAllFieldValueChangedDelegates(FDelegateUserObjectConst InUserObject)
{
	return NotificationDelegates.RemoveAllFieldValueChangedDelegates(this, InUserObject);
}

int32 UDreamViewModel::RemoveAllFieldValueChangedDelegates(UE::FieldNotification::FFieldId InFieldId, FDelegateUserObjectConst InUserObject)
{
	return NotificationDelegates.RemoveAllFieldValueChangedDelegates(this, InFieldId, InUserObject);
}

const UE::FieldNotification::IClassDescriptor& UDreamViewModel::GetFieldNotificationDescriptor() const
{
	static FFieldNotificationClassDescriptor Local;
	return Local;
}

void UDreamViewModel::BroadcastFieldValueChanged(UE::FieldNotification::FFieldId InFieldId)
{
	NotificationDelegates.BroadcastFieldValueChanged(this, InFieldId);
}

void UDreamViewModel::K2_AddFieldValueChangedDelegate(FFieldNotificationId InFieldId, FFieldValueChangedDynamicDelegate InDelegate)
{
	if (InFieldId.IsValid())
	{
		const UE::FieldNotification::FFieldId FieldId = GetFieldNotificationDescriptor().GetField(GetClass(), InFieldId.FieldName);
		if (FieldId.IsValid())
		{
			NotificationDelegates.AddFieldValueChangedDelegate(this, FieldId, InDelegate);
		}
	}
}

void UDreamViewModel::K2_RemoveFieldValueChangedDelegate(FFieldNotificationId InFieldId, FFieldValueChangedDynamicDelegate InDelegate)
{
	if (InFieldId.IsValid())
	{
		const UE::FieldNotification::FFieldId FieldId = GetFieldNotificationDescriptor().GetField(GetClass(), InFieldId.FieldName);
		if (FieldId.IsValid())
		{
			NotificationDelegates.RemoveFieldValueChangedDelegate(this, FieldId, InDelegate);
		}
	}
}

void UDreamViewModel::K2_BroadcastFieldValueChanged(FFieldNotificationId InFieldId)
{
	// By name, through the descriptor, as the add and the remove resolve it. A name this class announces nothing under
	// is an authoring mistake rather than a reason to broadcast under an invalid id. Through the virtual, so a subclass
	// that overrides BroadcastFieldValueChanged sees the graph's broadcasts too.
	if (InFieldId.IsValid())
	{
		const UE::FieldNotification::FFieldId FieldId = GetFieldNotificationDescriptor().GetField(GetClass(), InFieldId.FieldName);
		if (FieldId.IsValid())
		{
			BroadcastFieldValueChanged(FieldId);
		}
	}
}

UWorld* UDreamViewModel::GetWorld() const
{
	// Null on the class default object WITHOUT asking UObject's version: answering here at all is what tells the
	// Blueprint editor (UObject::ImplementsGetWorld, which asks the CDO) that subclasses have a world, so their graphs
	// get the world-context nodes. An instance has whatever its outers have.
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return nullptr;
	}
	// The widget that made it (`= new`), the game instance that registered it (FindOrCreate), or whatever a game
	// outered it to. Walked rather than asked of the first outer alone: an outer whose own answer is null (a package,
	// a plain object) is not the end of the chain.
	for (const UObject* Outer = GetOuter(); Outer != nullptr; Outer = Outer->GetOuter())
	{
		if (UWorld* World = Outer->GetWorld())
		{
			return World;
		}
	}
	return nullptr;
}
