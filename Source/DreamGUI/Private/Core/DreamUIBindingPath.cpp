// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIBindingObserver.h"

#include "INotifyFieldValueChanged.h"
#include "UObject/Class.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace DreamUIBindingPath
{
	UObject* ReadObjectMember(const UObject* InOwner, FName InMember)
	{
		if (!IsValid(InOwner) || InMember.IsNone())
		{
			return nullptr;
		}
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(InOwner->GetClass(), InMember);
		if (Property == nullptr)
		{
			return nullptr;
		}
		UObject* Value = Property->GetObjectPropertyValue_InContainer(InOwner);
		return IsValid(Value) ? Value : nullptr;
	}

	UObject* ResolveOwner(UObject* InRoot, TConstArrayView<FName> InSegments)
	{
		UObject* Current = IsValid(InRoot) ? InRoot : nullptr;
		for (int32 Index = 0; Current != nullptr && Index + 1 < InSegments.Num(); ++Index)
		{
			Current = ReadObjectMember(Current, InSegments[Index]);
		}
		return Current;
	}

	bool IsComplete(UObject* InRoot, TConstArrayView<FName> InSegments)
	{
		return ResolveOwner(InRoot, InSegments) != nullptr;
	}

	void ReadObjectArray(UObject* InOwner, FName InMember, bool bIsFunction, TArray<UObject*>& OutItems)
	{
		OutItems.Reset();
		if (!IsValid(InOwner) || InMember.IsNone())
		{
			return;
		}

		auto CopyOut = [&OutItems](const FArrayProperty* InItemsProperty, const void* InItemsMemory)
		{
			const FObjectPropertyBase* Inner = InItemsProperty != nullptr ? CastField<FObjectPropertyBase>(InItemsProperty->Inner) : nullptr;
			if (Inner == nullptr || InItemsMemory == nullptr)
			{
				return;
			}
			FScriptArrayHelper Helper(InItemsProperty, InItemsMemory);
			OutItems.Reserve(Helper.Num());
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				OutItems.Add(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index)));
			}
		};

		if (bIsFunction)
		{
			UFunction* Source = InOwner->FindFunction(InMember);
			const FArrayProperty* ItemsProperty = Source != nullptr ? CastField<FArrayProperty>(Source->GetReturnProperty()) : nullptr;
			if (ItemsProperty == nullptr)
			{
				return;
			}
			// FStructOnScope rather than a raw buffer: the returned array has to be constructed before the call writes it and
			// destroyed after it has been read.
			FStructOnScope SourceFrame(Source);
			InOwner->ProcessEvent(Source, SourceFrame.GetStructMemory());
			CopyOut(ItemsProperty, ItemsProperty->ContainerPtrToValuePtr<void>(SourceFrame.GetStructMemory()));
			return;
		}

		const FArrayProperty* ItemsProperty = FindFProperty<FArrayProperty>(InOwner->GetClass(), InMember);
		CopyOut(ItemsProperty, ItemsProperty != nullptr ? ItemsProperty->ContainerPtrToValuePtr<void>(InOwner) : nullptr);
	}

	UE::FieldNotification::FFieldId FindFieldId(const UClass* InClass, FName InMember)
	{
		if (InClass == nullptr || InMember.IsNone() || !InClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
		{
			return UE::FieldNotification::FFieldId();
		}
		// The descriptor is a property of the class, reached through any instance; the default object is the one that
		// always exists, which is what lets a path be ruled on before the objects along it do.
		const INotifyFieldValueChanged* Notifier = Cast<INotifyFieldValueChanged>(InClass->GetDefaultObject());
		if (Notifier == nullptr)
		{
			return UE::FieldNotification::FFieldId();
		}
		return Notifier->GetFieldNotificationDescriptor().GetField(InClass, InMember);
	}

	UClass* FindMemberClass(const UClass* InClass, FName InMember)
	{
		if (InClass == nullptr || InMember.IsNone())
		{
			return nullptr;
		}
		if (const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(InClass, InMember))
		{
			return Property->PropertyClass;
		}
		if (const UFunction* Function = InClass->FindFunctionByName(InMember))
		{
			if (const FObjectPropertyBase* Return = CastField<FObjectPropertyBase>(Function->GetReturnProperty()))
			{
				return Return->PropertyClass;
			}
		}
		return nullptr;
	}

	bool CanNotifyAlong(const UClass* InRootClass, TConstArrayView<FName> InSegments, FString* OutWhyNot)
	{
		const UClass* OwnerClass = InRootClass;
		for (int32 Index = 0; Index < InSegments.Num(); ++Index)
		{
			const FName Segment = InSegments[Index];
			if (OwnerClass == nullptr)
			{
				if (OutWhyNot != nullptr)
				{
					*OutWhyNot = FString::Printf(TEXT("'%s' follows a member that holds no object"), *Segment.ToString());
				}
				return false;
			}
			if (!FindFieldId(OwnerClass, Segment).IsValid())
			{
				if (OutWhyNot != nullptr)
				{
					*OutWhyNot = OwnerClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass())
						? FString::Printf(TEXT("'%s' on %s is not FieldNotify"), *Segment.ToString(), *OwnerClass->GetName())
						: FString::Printf(TEXT("%s does not implement INotifyFieldValueChanged, so its '%s' cannot announce a change"),
							*OwnerClass->GetName(), *Segment.ToString());
				}
				return false;
			}
			if (Index + 1 < InSegments.Num())
			{
				OwnerClass = FindMemberClass(OwnerClass, Segment);
			}
		}
		// An empty path reads nothing that could change: vacuously announceable.
		return true;
	}
}
