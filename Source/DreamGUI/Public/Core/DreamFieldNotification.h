// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FieldNotificationDelegate.h"
#include "INotifyFieldValueChanged.h"

/**
 * The delegate store behind an INotifyFieldValueChanged implementation: the multicast container
 * plus the per-field "anyone listening?" bits that let Broadcast be a cheap no-op on fields nobody
 * subscribed to.
 *
 * The engine ships this exact shape twice -- UWidget keeps it in a UserWidget extension object,
 * UMVVMViewModelBase in a plugin-private wrapper -- and neither is reachable from here without
 * either the UMG widget model or a hard dependency on the ModelViewViewModel plugin. A plugin
 * dependency for thirty lines is how the ThinCustom backend accident happened; this depends only
 * on the engine-level FieldNotification module.
 */
class FDreamFieldNotificationDelegates
{
public:
	FDelegateHandle AddFieldValueChangedDelegate(UObject* InOwner, UE::FieldNotification::FFieldId InFieldId, INotifyFieldValueChanged::FFieldValueChangedDelegate InNewDelegate)
	{
		FDelegateHandle Result;
		if (InFieldId.IsValid())
		{
			Result = Delegates.Add(InOwner, InFieldId, MoveTemp(InNewDelegate));
			if (Result.IsValid())
			{
				EnabledFieldNotifications.PadToNum(InFieldId.GetIndex() + 1, false);
				EnabledFieldNotifications[InFieldId.GetIndex()] = true;
			}
		}
		return Result;
	}

	void AddFieldValueChangedDelegate(UObject* InOwner, UE::FieldNotification::FFieldId InFieldId, const FFieldValueChangedDynamicDelegate& InDelegate)
	{
		if (InFieldId.IsValid())
		{
			const FDelegateHandle Result = Delegates.Add(InOwner, InFieldId, InDelegate);
			if (Result.IsValid())
			{
				EnabledFieldNotifications.PadToNum(InFieldId.GetIndex() + 1, false);
				EnabledFieldNotifications[InFieldId.GetIndex()] = true;
			}
		}
	}

	bool RemoveFieldValueChangedDelegate(UObject* InOwner, UE::FieldNotification::FFieldId InFieldId, FDelegateHandle InHandle)
	{
		bool bResult = false;
		if (InFieldId.IsValid() && InHandle.IsValid()
			&& EnabledFieldNotifications.IsValidIndex(InFieldId.GetIndex()) && EnabledFieldNotifications[InFieldId.GetIndex()])
		{
			const UE::FieldNotification::FFieldMulticastDelegate::FRemoveFromResult RemoveResult = Delegates.RemoveFrom(InOwner, InFieldId, InHandle);
			bResult = RemoveResult.bRemoved;
			EnabledFieldNotifications[InFieldId.GetIndex()] = RemoveResult.bHasOtherBoundDelegates;
		}
		return bResult;
	}

	bool RemoveFieldValueChangedDelegate(UObject* InOwner, UE::FieldNotification::FFieldId InFieldId, const FFieldValueChangedDynamicDelegate& InDelegate)
	{
		bool bResult = false;
		if (InFieldId.IsValid()
			&& EnabledFieldNotifications.IsValidIndex(InFieldId.GetIndex()) && EnabledFieldNotifications[InFieldId.GetIndex()])
		{
			const UE::FieldNotification::FFieldMulticastDelegate::FRemoveFromResult RemoveResult = Delegates.RemoveFrom(InOwner, InFieldId, InDelegate);
			bResult = RemoveResult.bRemoved;
			EnabledFieldNotifications[InFieldId.GetIndex()] = RemoveResult.bHasOtherBoundDelegates;
		}
		return bResult;
	}

	int32 RemoveAllFieldValueChangedDelegates(UObject* InOwner, FDelegateUserObjectConst InUserObject)
	{
		int32 Result = 0;
		if (InUserObject)
		{
			UE::FieldNotification::FFieldMulticastDelegate::FRemoveAllResult RemoveResult = Delegates.RemoveAll(InOwner, InUserObject);
			Result = RemoveResult.RemoveCount;
			EnabledFieldNotifications = MoveTemp(RemoveResult.HasFields);
		}
		return Result;
	}

	int32 RemoveAllFieldValueChangedDelegates(UObject* InOwner, UE::FieldNotification::FFieldId InFieldId, FDelegateUserObjectConst InUserObject)
	{
		int32 Result = 0;
		if (InFieldId.IsValid() && InUserObject)
		{
			UE::FieldNotification::FFieldMulticastDelegate::FRemoveAllResult RemoveResult = Delegates.RemoveAll(InOwner, InFieldId, InUserObject);
			Result = RemoveResult.RemoveCount;
			EnabledFieldNotifications = MoveTemp(RemoveResult.HasFields);
		}
		return Result;
	}

	void BroadcastFieldValueChanged(UObject* InOwner, UE::FieldNotification::FFieldId InFieldId)
	{
		if (InFieldId.IsValid()
			&& EnabledFieldNotifications.IsValidIndex(InFieldId.GetIndex()) && EnabledFieldNotifications[InFieldId.GetIndex()])
		{
			Delegates.Broadcast(InOwner, InFieldId);
		}
	}

private:
	UE::FieldNotification::FFieldMulticastDelegate Delegates;
	TBitArray<> EnabledFieldNotifications;
};
