// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamKeyEventData.h"
#include "Event/Interface/DreamKeyInterface.h"
#include "DreamGUI.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"

bool UDreamUIActionRouter::ShouldCreateSubsystem(UObject* Outer) const
{
	//same gate as the tooltip, drag-drop, modal and virtual-cursor subsystems: a dedicated server has no
	//player to route a key for, and "UI subsystems do not exist on a server" should mean all of them
	return !IsRunningCommandlet() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
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

void UDreamUIActionRouter::GetModifierKeyState(int32 InUserIndex, bool& bOutShift, bool& bOutCtrl, bool& bOutAlt, bool& bOutCmd)const
{
	bOutShift = bOutCtrl = bOutAlt = bOutCmd = false;
	UDreamEventSystem* Events = UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UDreamUIActionRouter*>(this), InUserIndex);
	const APlayerController* PlayerController = Events != nullptr ? Events->GetPlayerController() : nullptr;
	if (PlayerController == nullptr)return;
	// Both spellings of each: a chord means the modifier, not the particular one under which hand.
	bOutShift = PlayerController->IsInputKeyDown(EKeys::LeftShift) || PlayerController->IsInputKeyDown(EKeys::RightShift);
	bOutCtrl = PlayerController->IsInputKeyDown(EKeys::LeftControl) || PlayerController->IsInputKeyDown(EKeys::RightControl);
	bOutAlt = PlayerController->IsInputKeyDown(EKeys::LeftAlt) || PlayerController->IsInputKeyDown(EKeys::RightAlt);
	bOutCmd = PlayerController->IsInputKeyDown(EKeys::LeftCommand) || PlayerController->IsInputKeyDown(EKeys::RightCommand);
}

bool UDreamUIActionRouter::DispatchKeyToFocusedWidget(int32 InUserIndex, const FKey& InKey, bool bPressed,
	bool bShiftDown, bool bCtrlDown, bool bAltDown, bool bCmdDown)
{
	UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(this, InUserIndex);
	if (!IsValid(EventSystem))
	{
		return false;
	}
	// Pointer 0's selection IS the focus: SetFocus/ClearFocus and the navigation cursor all write it.
	UDreamWidget* Focused = EventSystem->GetCurrentSelectedComponent(0);
	if (!IsValid(Focused))
	{
		return false;
	}
	UDreamKeyEventData* EventData = NewObject<UDreamKeyEventData>(this);
	EventData->KeyEventType = bPressed ? EDreamUIKeyEventType::KeyDown : EDreamUIKeyEventType::KeyUp;
	EventData->UserIndex = InUserIndex;
	EventData->Key = InKey;
	EventData->bIsPressed = bPressed;
	EventData->bShiftDown = bShiftDown;
	EventData->bCtrlDown = bCtrlDown;
	EventData->bAltDown = bAltDown;
	EventData->bCmdDown = bCmdDown;
	return DreamUIKeyDispatch::DispatchBubbling(Focused, EventData);
}

bool UDreamUIActionRouter::HandleKey(int32 InUserIndex, const FKey& InKey, bool bPressed)
{
	bool bShiftDown = false, bCtrlDown = false, bAltDown = false, bCmdDown = false;
	GetModifierKeyState(InUserIndex, bShiftDown, bCtrlDown, bAltDown, bCmdDown);
	return HandleKeyWithModifiers(InUserIndex, InKey, bPressed, bShiftDown, bCtrlDown, bAltDown, bCmdDown);
}

void UDreamUIActionRouter::ResolveInputActionKeys(FBindingEntry& InEntry)const
{
	if (InEntry.bInputActionKeysResolved)return;
	InEntry.bInputActionKeysResolved = true;
	InEntry.InputActionKeys.Reset();
	if (InEntry.Action.InputAction.IsNull())return;

	const ULocalPlayer* LocalPlayer = UDreamEventSystem::GetLocalPlayerForUser(this, InEntry.UserIndex);
	const UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer != nullptr
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (Subsystem == nullptr)return;

	// LoadSynchronous rather than Get: the action is a soft reference so that an action table does not
	// drag every Input Action asset into memory, and this is the one moment its keys are needed.
	if (const UInputAction* Action = InEntry.Action.InputAction.LoadSynchronous())
	{
		InEntry.InputActionKeys = Subsystem->QueryKeysMappedToAction(Action);
	}
}

void UDreamUIActionRouter::RefreshInputActionKeys()
{
	for (FBindingEntry& Entry : Bindings)
	{
		Entry.bInputActionKeysResolved = false;
		Entry.InputActionKeys.Reset();
	}
	// Prompts are drawn from the keys, so anyone showing them has to be told they moved. One broadcast
	// per user that actually has bindings, rather than one per binding.
	TSet<int32> AffectedUsers;
	for (const FBindingEntry& Entry : Bindings)
	{
		AffectedUsers.Add(Entry.UserIndex);
	}
	for (const int32 User : AffectedUsers)
	{
		BindingsChangedEvent.Broadcast(User);
	}
}

bool UDreamUIActionRouter::HandleKeyWithModifiers(int32 InUserIndex, const FKey& InKey, bool bPressed, bool bShiftDown, bool bCtrlDown, bool bAltDown, bool bCmdDown)
{
	if (!InKey.IsValid())return false;
	RemoveStaleBindings();

	// The FOCUSED widget sees the key before any named binding does -- Slate's order and UMG's, and
	// what lets a text box keep Escape while the same key still closes a dialog when nothing is
	// focused. Nothing happens here unless a widget in the focus chain actually speaks
	// IDreamKeyInterface, so a project with no key handlers pays one null check and behaves as before.
	if (DispatchKeyToFocusedWidget(InUserIndex, InKey, bPressed, bShiftDown, bCtrlDown, bAltDown, bCmdDown))
	{
		return true;
	}

	// Two passes, newest first. A screen's own bindings beat the global ones, and within a screen the
	// most recently registered wins -- so a dialog's Delete is what Delete means while it is open.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bScopedPass = Pass == 0;
		// Within a pass the most specific chord wins rather than simply the newest: with Ctrl held, a
		// Ctrl+S binding beats a plain S one whichever was registered first. Ties keep the newest, which
		// is the order this loop walks, so behaviour is unchanged wherever no modifiers are authored.
		int32 BestIndex = INDEX_NONE;
		int32 BestModifierCount = -1;
		// The Enhanced Input half is collected separately and only consulted when the table has nothing
		// to say. The action table is the authority: a key typed into a row beats the same key reached
		// through an Input Action, so a project migrating to Enhanced Input cannot silently lose a
		// binding it can see in its own data table.
		int32 BestInputActionIndex = INDEX_NONE;
		for (int32 Index = Bindings.Num() - 1; Index >= 0; --Index)
		{
			FBindingEntry& Entry = Bindings[Index];
			if (Entry.UserIndex != InUserIndex)continue;
			if ((Entry.Scope.Get() != nullptr) != bScopedPass)continue;
			if (!IsEligible(Entry))continue;

			if (Entry.Action.MatchesKey(InKey, bShiftDown, bCtrlDown, bAltDown, bCmdDown))
			{
				const int32 ModifierCount = Entry.Action.CountRequiredModifiers();
				if (ModifierCount > BestModifierCount)
				{
					BestModifierCount = ModifierCount;
					BestIndex = Index;
				}
				continue;
			}
			if (!Entry.Action.InputAction.IsNull())
			{
				ResolveInputActionKeys(Entry);
				if (Entry.InputActionKeys.Contains(InKey) && BestInputActionIndex == INDEX_NONE)
				{
					BestInputActionIndex = Index;//newest first, so the first one seen is the newest
				}
			}
		}
		if (BestIndex == INDEX_NONE)
		{
			BestIndex = BestInputActionIndex;
		}
		else if (BestInputActionIndex != INDEX_NONE)
		{
			// Both spellings claimed the key. Said out loud once per key rather than silently: a project
			// that has drifted into defining the same key twice wants to know which half is being
			// ignored, and finding that out from behaviour alone is hours. Remembered on the subsystem,
			// not in a function-local static -- one world's warning must not silence another's.
			bool bAlreadyReported = false;
			ReportedInputActionConflictKeys.Add(InKey, &bAlreadyReported);
			if (!bAlreadyReported)
			{
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Key '%s' is claimed both by an action table row and by an Input Action on action '%s'. The action table wins; remove one of the two spellings."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InKey.ToString(),
					*Bindings[BestInputActionIndex].Action.DisplayName.ToString());
			}
		}
		if (BestIndex != INDEX_NONE)
		{
			FBindingEntry& Entry = Bindings[BestIndex];

			if (!bPressed)
			{
				// Letting go before the threshold is a cancel, not a fire. Consumed all the same: the
				// press was consumed, and leaking only the release would look like a stray keypress.
				const bool bWasHolding = Entry.bHeld && Entry.Action.HoldTime > 0.0f;
				const int32 EntryId = Entry.Id;
				Entry.bHeld = false;
				Entry.HeldSeconds = 0.0f;
				Entry.bHoldFired = false;
				if (bWasHolding)
				{
					// The ring has to empty on screen too. Broadcast last and nothing read from Entry
					// afterwards: a listener is free to register or unregister actions, which reallocates
					// the array this reference points into.
					FDreamUIActionHandle ReleasedHandle;
					ReleasedHandle.Id = EntryId;
					HoldProgressEvent.Broadcast(ReleasedHandle, 0.0f);
				}
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

	// Advance every hold first, then dispatch. Execute runs a game callback, and what a UI callback
	// most often does is register or unregister actions: RegisterAction appends to Bindings and can
	// reallocate it, UnregisterAction and UnregisterScope RemoveAll from it. Either one leaves this
	// range-for walking freed storage with a dangling FBindingEntry&, and a hold-to-confirm that
	// closes its own screen is the ordinary case, not an exotic one.
	TArray<int32, TInlineAllocator<4>> DueBindingIds;
	// Collected, not broadcast in place, for the same reason as the due list below: a listener is game
	// code and may register or unregister an action from it.
	TArray<TPair<int32, float>, TInlineAllocator<4>> ProgressUpdates;
	for (FBindingEntry& Entry : Bindings)
	{
		if (!Entry.bHeld || Entry.bHoldFired)continue;
		// A screen that closed mid-hold takes its hold with it, rather than firing into a dead screen
		// the moment the timer runs out.
		if (!IsEligible(Entry))
		{
			Entry.bHeld = false;
			Entry.HeldSeconds = 0.0f;
			ProgressUpdates.Emplace(Entry.Id, 0.0f);//whatever was drawing the ring has to empty it
			continue;
		}
		Entry.HeldSeconds += DeltaTime;
		ProgressUpdates.Emplace(Entry.Id, Entry.Action.HoldTime > 0.0f
			? FMath::Clamp(Entry.HeldSeconds / Entry.Action.HoldTime, 0.0f, 1.0f)
			: 0.0f);
		if (Entry.HeldSeconds >= Entry.Action.HoldTime)
		{
			// Fires when the time is up, not when the player lets go: a hold-to-confirm that waits for
			// the release cannot show a filled ring and then act on it.
			Entry.bHoldFired = true;
			DueBindingIds.Add(Entry.Id);
		}
	}
	for (const TPair<int32, float>& Update : ProgressUpdates)
	{
		FDreamUIActionHandle Handle;
		Handle.Id = Update.Key;
		HoldProgressEvent.Broadcast(Handle, Update.Value);
	}
	// Re-looked-up by id rather than kept as pointers, so a callback that unregisters one of the
	// others simply finds nothing to run instead of firing through a removed entry.
	for (const int32 DueId : DueBindingIds)
	{
		FDreamUIActionHandle Handle;
		Handle.Id = DueId;
		if (FBindingEntry* Entry = FindBinding(Handle))
		{
			Execute(*Entry);
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
	EDreamUIGamepadModel GamepadModel = EDreamUIGamepadModel::Generic;
	if (UDreamEventSystem* Events = UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UDreamUIActionRouter*>(this), InUserIndex))
	{
		Device = Events->GetCurrentInputDevice();
		GamepadModel = Events->GetCurrentGamepadModel();
	}

	// Newest first, matching the order HandleKey resolves in: what the player sees at the front of the
	// bar is what the key would actually do.
	for (int32 Index = Bindings.Num() - 1; Index >= 0; --Index)
	{
		const FBindingEntry& Entry = Bindings[Index];
		if (Entry.UserIndex != InUserIndex)continue;
		if (!Entry.bDisplayInActionBar)continue;
		if (!IsEligible(Entry))continue;

		FKey Key = Entry.Action.GetKeyForDevice(Device);
		if (!Key.IsValid() && !Entry.Action.InputAction.IsNull())
		{
			// Nothing typed into the row for this device, but an Input Action is named: the keys behind
			// it are as real as typed ones, and a prompt bar that stayed blank would be the whole reason
			// a project keeps typing its keys twice.
			ResolveInputActionKeys(const_cast<FBindingEntry&>(Entry));
			for (const FKey& ActionKey : Entry.InputActionKeys)
			{
				const bool bWantGamepad = Device == EDreamUIInputDevice::Gamepad;
				if (ActionKey.IsValid() && ActionKey.IsGamepadKey() == bWantGamepad && !ActionKey.IsTouch())
				{
					Key = ActionKey;
					break;
				}
			}
		}
		if (!Key.IsValid())
		{
			continue;//nothing to press on this device, so nothing honest to draw
		}

		FDreamUIActionBinding& Out = OutBindings.AddDefaulted_GetRef();
		Out.Handle.Id = Entry.Id;
		Out.DisplayName = Entry.Action.DisplayName;
		Out.Key = Key;
		Out.Icon = Entry.Action.GetIconForDevice(Device, GamepadModel);
		Out.HoldTime = Entry.Action.HoldTime;
		Out.HoldProgress = (Entry.bHeld && Entry.Action.HoldTime > 0.0f)
			? FMath::Clamp(Entry.HeldSeconds / Entry.Action.HoldTime, 0.0f, 1.0f)
			: 0.0f;
	}
}
