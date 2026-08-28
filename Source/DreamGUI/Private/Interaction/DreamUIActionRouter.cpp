// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Event/DreamEventSystem.h"
#include "Engine/World.h"

bool UDreamUIActionRouter::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningCommandlet() && Super::ShouldCreateSubsystem(Outer);
}

void UDreamUIActionRouter::Deinitialize()
{
	Bindings.Reset();
	Super::Deinitialize();
}

TStatId UDreamUIActionRouter::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamUIActionRouter, STATGROUP_Tickables);
}

UDreamUIActionRouter* UDreamUIActionRouter::Get(const UObject* WorldContextObject)
{
	if (!IsValid(WorldContextObject))return nullptr;
	UWorld* World = WorldContextObject->GetWorld();
	return IsValid(World) ? World->GetSubsystem<UDreamUIActionRouter>() : nullptr;
}

void UDreamUIActionRouter::RemoveStaleBindings()
{
	// A binding whose scope was destroyed without unregistering. Its eligibility test would keep
	// returning false, so it is harmless but permanent -- worth sweeping rather than accumulating.
	Bindings.RemoveAll([](const FBindingEntry& Entry)
	{
		return Entry.Scope.IsStale();
	});
}

bool UDreamUIActionRouter::IsEligible(const FBindingEntry& InEntry) const
{
	UDreamUINavigationScope* Scope = InEntry.Scope.Get();
	if (Scope == nullptr)
	{
		return !InEntry.Scope.IsStale();//a global binding, as opposed to one whose screen is gone
	}
	const UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(this);
	return Stack != nullptr && Stack->GetActiveScope(InEntry.UserIndex) == Scope;
}

UDreamUIActionRouter::FBindingEntry* UDreamUIActionRouter::FindBinding(const FDreamUIActionHandle& InHandle)
{
	return Bindings.FindByPredicate([&InHandle](const FBindingEntry& Entry) { return Entry.Id == InHandle.Id; });
}

const UDreamUIActionRouter::FBindingEntry* UDreamUIActionRouter::FindBinding(const FDreamUIActionHandle& InHandle) const
{
	return Bindings.FindByPredicate([&InHandle](const FBindingEntry& Entry) { return Entry.Id == InHandle.Id; });
}

FDreamUIActionHandle UDreamUIActionRouter::RegisterAction(UDreamUINavigationScope* InScope, const FDataTableRowHandle& InAction, FDreamUIActionExecutedDelegate InCallback, int32 InUserIndex, bool bDisplayInActionBar)
{
	FDreamUIActionHandle Handle;
	const FDreamUIInputActionData* Row = InAction.GetRow<FDreamUIInputActionData>(TEXT("DreamUIActionRouter::RegisterAction"));
	if (Row == nullptr)
	{
		return Handle;//GetRow has already logged which handle failed to resolve
	}
	RemoveStaleBindings();

	FBindingEntry& Entry = Bindings.AddDefaulted_GetRef();
	Entry.Id = NextId++;
	Entry.UserIndex = InUserIndex;
	Entry.Scope = InScope;
	// A copy of the row, not a pointer into the table: a binding outlives any particular lookup, and a
	// table reloaded underneath a live screen would otherwise leave dangling rows.
	Entry.Action = *Row;
	Entry.Callback = InCallback;
	// And-ed, never or-ed: the action decides whether it is the sort of thing a player is told about,
	// and one caller must not be able to advertise an action the designer marked hidden.
	Entry.bDisplayInActionBar = bDisplayInActionBar && Row->bDisplayInActionBar;

	Handle.Id = Entry.Id;
	BindingsChangedEvent.Broadcast(InUserIndex);
	return Handle;
}

void UDreamUIActionRouter::UnregisterAction(const FDreamUIActionHandle& InHandle)
{
	if (!InHandle.IsValidHandle())return;
	int32 UserIndex = 0;
	const int32 Removed = Bindings.RemoveAll([&InHandle, &UserIndex](const FBindingEntry& Entry)
	{
		if (Entry.Id != InHandle.Id)return false;
		UserIndex = Entry.UserIndex;
		return true;
	});
	if (Removed > 0)
	{
		BindingsChangedEvent.Broadcast(UserIndex);
	}
}

void UDreamUIActionRouter::UnregisterScope(UDreamUINavigationScope* InScope)
{
	if (InScope == nullptr)return;
	TSet<int32> AffectedUsers;
	const int32 Removed = Bindings.RemoveAll([InScope, &AffectedUsers](const FBindingEntry& Entry)
	{
		if (Entry.Scope.Get() != InScope)return false;
		AffectedUsers.Add(Entry.UserIndex);
		return true;
	});
	if (Removed > 0)
	{
		for (const int32 User : AffectedUsers)
		{
			BindingsChangedEvent.Broadcast(User);
		}
	}
}

void UDreamUIActionRouter::Execute(FBindingEntry& InEntry)
{
	InEntry.Callback.ExecuteIfBound();
}

bool UDreamUIActionRouter::HandleKey(int32 InUserIndex, const FKey& InKey, bool bPressed)
{
	if (!InKey.IsValid())return false;
	RemoveStaleBindings();

	// Two passes, newest first. A screen's own bindings beat the global ones, and within a screen the
	// most recently registered wins -- so a dialog's Delete is what Delete means while it is open.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bScopedPass = Pass == 0;
		for (int32 Index = Bindings.Num() - 1; Index >= 0; --Index)
		{
			FBindingEntry& Entry = Bindings[Index];
			if (Entry.UserIndex != InUserIndex)continue;
			if ((Entry.Scope.Get() != nullptr) != bScopedPass)continue;
			if (!Entry.Action.MatchesKey(InKey))continue;
			if (!IsEligible(Entry))continue;

			if (!bPressed)
			{
				// Letting go before the threshold is a cancel, not a fire. Consumed all the same: the
				// press was consumed, and leaking only the release would look like a stray keypress.
				Entry.bHeld = false;
				Entry.HeldSeconds = 0.0f;
				Entry.bHoldFired = false;
				return true;
			}
			if (Entry.Action.HoldTime <= 0.0f)
			{
				Execute(Entry);
				return true;
			}
			Entry.bHeld = true;
			Entry.HeldSeconds = 0.0f;
			Entry.bHoldFired = false;
			return true;
		}
	}
	return false;
}

void UDreamUIActionRouter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	for (FBindingEntry& Entry : Bindings)
	{
		if (!Entry.bHeld || Entry.bHoldFired)continue;
		// A screen that closed mid-hold takes its hold with it, rather than firing into a dead screen
		// the moment the timer runs out.
		if (!IsEligible(Entry))
		{
			Entry.bHeld = false;
			Entry.HeldSeconds = 0.0f;
			continue;
		}
		Entry.HeldSeconds += DeltaTime;
		if (Entry.HeldSeconds >= Entry.Action.HoldTime)
		{
			// Fires when the time is up, not when the player lets go: a hold-to-confirm that waits for
			// the release cannot show a filled ring and then act on it.
			Entry.bHoldFired = true;
			Execute(Entry);
		}
	}
}

float UDreamUIActionRouter::GetHoldProgress(const FDreamUIActionHandle& InHandle) const
{
	const FBindingEntry* Entry = FindBinding(InHandle);
	if (Entry == nullptr || !Entry->bHeld || Entry->Action.HoldTime <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Clamp(Entry->HeldSeconds / Entry->Action.HoldTime, 0.0f, 1.0f);
}

void UDreamUIActionRouter::GetDisplayBindings(int32 InUserIndex, TArray<FDreamUIActionBinding>& OutBindings) const
{
	OutBindings.Reset();

	EDreamUIInputDevice Device = EDreamUIInputDevice::MouseAndKeyboard;
	if (UDreamEventSystem* Events = UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UDreamUIActionRouter*>(this), InUserIndex))
	{
		Device = Events->GetCurrentInputDevice();
	}

	// Newest first, matching the order HandleKey resolves in: what the player sees at the front of the
	// bar is what the key would actually do.
	for (int32 Index = Bindings.Num() - 1; Index >= 0; --Index)
	{
		const FBindingEntry& Entry = Bindings[Index];
		if (Entry.UserIndex != InUserIndex)continue;
		if (!Entry.bDisplayInActionBar)continue;
		if (!IsEligible(Entry))continue;

		const FKey Key = Entry.Action.GetKeyForDevice(Device);
		if (!Key.IsValid())
		{
			continue;//nothing to press on this device, so nothing honest to draw
		}

		FDreamUIActionBinding& Out = OutBindings.AddDefaulted_GetRef();
		Out.Handle.Id = Entry.Id;
		Out.DisplayName = Entry.Action.DisplayName;
		Out.Key = Key;
		Out.Icon = Entry.Action.GetIconForDevice(Device);
		Out.HoldTime = Entry.Action.HoldTime;
		Out.HoldProgress = (Entry.bHeld && Entry.Action.HoldTime > 0.0f)
			? FMath::Clamp(Entry.HeldSeconds / Entry.Action.HoldTime, 0.0f, 1.0f)
			: 0.0f;
	}
}
