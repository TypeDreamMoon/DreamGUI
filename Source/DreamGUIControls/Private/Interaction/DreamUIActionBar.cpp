// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIActionBar.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamText.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "Event/DreamUIKeyRouting.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "DreamUIActionBar"

// Defined out of line on purpose: the header only forward-declares the visual classes, and assigning a
// forward-declared pointer into a TWeakObjectPtr needs the complete type (the generated .cpp includes
// the header alone and cannot see that UDreamImage is a UObject).
void UDreamUIActionBarEntry::SetLabelText(UDreamText* Value)
{
	LabelText = Value;
}

void UDreamUIActionBarEntry::SetIconImage(UDreamImage* Value)
{
	IconImage = Value;
}

void UDreamUIActionBarEntry::SetKeyText(UDreamText* Value)
{
	KeyText = Value;
}


UDreamUIActionBarEntry::UDreamUIActionBarEntry()
{
	bStartWithTickEnabled = false;
}

void UDreamUIActionBarEntry::OnEnable()
{
	Super::OnEnable();
	SubscribeToRouter();
}

void UDreamUIActionBarEntry::OnDisable()
{
	UnsubscribeFromRouter();
	Super::OnDisable();
}

void UDreamUIActionBarEntry::OnUnregister()
{
	UnsubscribeFromRouter();
	Super::OnUnregister();
}

void UDreamUIActionBarEntry::SubscribeToRouter()
{
	UnsubscribeFromRouter();
	// Subscribed rather than ticked, and subscribed for every entry rather than only the ones with a
	// hold: which action this entry shows changes under it (SetBinding is called again on a rebuild),
	// and the filter is one integer compare in a delegate that only fires while a key is actually down.
	if (UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this))
	{
		SubscribedHoldRouter = Router;
		HoldProgressHandle = Router->GetHoldProgressEvent().AddUObject(this, &UDreamUIActionBarEntry::HandleHoldProgressChanged);
	}
}

void UDreamUIActionBarEntry::UnsubscribeFromRouter()
{
	if (UDreamUIActionRouter* Router = SubscribedHoldRouter.Get(); Router != nullptr && HoldProgressHandle.IsValid())
	{
		Router->GetHoldProgressEvent().Remove(HoldProgressHandle);
	}
	HoldProgressHandle.Reset();
	SubscribedHoldRouter.Reset();
}

void UDreamUIActionBarEntry::HandleHoldProgressChanged(FDreamUIActionHandle InHandle, float InProgress)
{
	if (!(InHandle == Binding.Handle))return;//some other action's hold
	if (FMath::IsNearlyEqual(Binding.HoldProgress, InProgress))return;
	Binding.HoldProgress = InProgress;
	ReceiveOnHoldProgressChanged(InProgress);
}

void UDreamUIActionBarEntry::SetBinding(const FDreamUIActionBinding& InBinding)
{
	Binding = InBinding;

	if (UDreamText* Label = LabelText.Get())
	{
		Label->SetText(Binding.DisplayName);
	}

	// The glyph is loaded synchronously: this runs when the bar rebuilds, which is when bindings change
	// or the player switches device, and a prompt that appears a frame or two later than the screen it
	// belongs to reads as a glitch.
	UTexture2D* Icon = Binding.Icon.IsNull() ? nullptr : Binding.Icon.LoadSynchronous();
	if (UDreamImage* Image = IconImage.Get())
	{
		if (Icon != nullptr)
		{
			Image->SetBrush_Texture(Icon);
		}
		if (UDreamWidget* ImageWidget = Image->GetWidget())
		{
			ImageWidget->SetVisibility(Icon != nullptr ? EDreamWidgetVisibility::Visible : EDreamWidgetVisibility::Collapsed);
		}
	}
	// Exactly one of the two is shown. Without a glyph the key's own name is all there is; with one,
	// printing the name beside it says the same thing twice.
	if (UDreamText* KeyLabel = KeyText.Get())
	{
		KeyLabel->SetText(Binding.Key.GetDisplayName());
		if (UDreamWidget* KeyWidget = KeyLabel->GetWidget())
		{
			KeyWidget->SetVisibility(Icon == nullptr ? EDreamWidgetVisibility::Visible : EDreamWidgetVisibility::Collapsed);
		}
	}

	ReceiveOnBindingChanged(Binding);
}

UDreamUIActionBar::UDreamUIActionBar()
{
	bStartWithTickEnabled = false;
}

void UDreamUIActionBar::OnEnable()
{
	Super::OnEnable();
	SubscribeToSources();
	Rebuild();
}

void UDreamUIActionBar::OnDisable()
{
	UnsubscribeFromSources();
	ClearEntries();
	Super::OnDisable();
}

void UDreamUIActionBar::OnUnregister()
{
	UnsubscribeFromSources();
	ClearEntries();
	Super::OnUnregister();
}

int32 UDreamUIActionBar::GetUserIndex() const
{
	if (UserIndex >= 0)
	{
		return UserIndex;
	}
	// The player who owns the bar: a bar on player 2's screen shows player 2's prompts with nobody having told it so.
	const UDreamWidget* Widget = GetWidget();
	return IsValid(Widget) ? Widget->GetOwningPlayerIndex() : 0;
}

void UDreamUIActionBar::SetUserIndex(int32 Value)
{
	if (UserIndex == Value)return;
	// The subscriptions are per player, so the bar has to let go of the old one's before it can hear
	// from the new one.
	UnsubscribeFromSources();
	UserIndex = Value;
	SubscribeToSources();
	Rebuild();
}

void UDreamUIActionBar::SubscribeToSources()
{
	UnsubscribeFromSources();

	if (UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this))
	{
		SubscribedRouter = Router;
		BindingsChangedHandle = Router->GetBindingsChangedEvent().AddUObject(this, &UDreamUIActionBar::HandleBindingsChanged);
	}
	// The device decides which key each prompt names, so switching pad to keyboard has to rebuild even
	// though not one binding changed -- and the pad's model decides which glyph, so does swapping one pad for
	// another while the device stays a pad.
	if (UDreamEventSystem* Events = UDreamEventSystem::GetDreamEventSystemInstance(this, GetUserIndex()))
	{
		SubscribedEventSystem = Events;
		InputDeviceChangedHandle = Events->GetInputDeviceChangedEvent().AddUObject(this, &UDreamUIActionBar::HandleInputDeviceChanged);
		GamepadModelChangedHandle = Events->GetGamepadModelChangedEvent().AddUObject(this, &UDreamUIActionBar::HandleGamepadModelChanged);
	}
}

void UDreamUIActionBar::UnsubscribeFromSources()
{
	if (UDreamUIActionRouter* Router = SubscribedRouter.Get(); Router != nullptr && BindingsChangedHandle.IsValid())
	{
		Router->GetBindingsChangedEvent().Remove(BindingsChangedHandle);
	}
	BindingsChangedHandle.Reset();
	SubscribedRouter.Reset();

	if (UDreamEventSystem* Events = SubscribedEventSystem.Get(); Events != nullptr)
	{
		if (InputDeviceChangedHandle.IsValid())
		{
			Events->GetInputDeviceChangedEvent().Remove(InputDeviceChangedHandle);
		}
		if (GamepadModelChangedHandle.IsValid())
		{
			Events->GetGamepadModelChangedEvent().Remove(GamepadModelChangedHandle);
		}
	}
	InputDeviceChangedHandle.Reset();
	GamepadModelChangedHandle.Reset();
	SubscribedEventSystem.Reset();
}

void UDreamUIActionBar::HandleBindingsChanged(int32 InUserIndex)
{
	if (InUserIndex != GetUserIndex())return;
	Rebuild();
}

void UDreamUIActionBar::HandleInputDeviceChanged(EDreamUIInputDevice InDevice)
{
	Rebuild();
}

void UDreamUIActionBar::HandleGamepadModelChanged(EDreamUIGamepadModel InModel)
{
	Rebuild();
}

void UDreamUIActionBar::AppendTabSwitchPrompts(int32 InUserIndex, TArray<FDreamUIActionBinding>& InOutPrompts) const
{
	const UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(this);
	const UDreamUIInputUser* User = Input != nullptr ? Input->GetUser(InUserIndex) : nullptr;
	// Only while the keys would switch something: the routing sends them to this same answer, so the bar
	// cannot advertise a switch the key would not make.
	if (User == nullptr || DreamUIKeyRouting::FindTabSwitchTarget(User) == nullptr)
	{
		return;
	}
	// The first key of the table for the device in hand, as the router names an action's key: a pad's prompt
	// for a pad, a keyboard's for a keyboard, and none where the table has no key for the device -- LB and RB,
	// the defaults, say nothing to a keyboard player.
	const bool bGamepad = User->GetCurrentInputDevice() == EDreamUIInputDevice::Gamepad;
	auto KeyForDevice = [bGamepad](const TArray<FKey>& InKeys) -> FKey
	{
		for (const FKey& TableKey : InKeys)
		{
			if (TableKey.IsValid() && TableKey.IsGamepadKey() == bGamepad && !TableKey.IsTouch())
			{
				return TableKey;
			}
		}
		return FKey();
	};
	const UDreamGUISettings* Settings = UDreamGUISettings::Get();
	if (Settings == nullptr)
	{
		return;
	}
	// No handle: nothing is registered with the router for these, so no hold progress ever names them.
	const FKey PreviousTabKey = KeyForDevice(Settings->PreviousTabKeys);
	if (PreviousTabKey.IsValid())
	{
		FDreamUIActionBinding& PreviousTabPrompt = InOutPrompts.AddDefaulted_GetRef();
		PreviousTabPrompt.DisplayName = LOCTEXT("PreviousTab", "Previous tab");
		PreviousTabPrompt.Key = PreviousTabKey;
	}
	const FKey NextTabKey = KeyForDevice(Settings->NextTabKeys);
	if (NextTabKey.IsValid())
	{
		FDreamUIActionBinding& NextTabPrompt = InOutPrompts.AddDefaulted_GetRef();
		NextTabPrompt.DisplayName = LOCTEXT("NextTab", "Next tab");
		NextTabPrompt.Key = NextTabKey;
	}
}

void UDreamUIActionBar::ClearEntries()
{
	for (UDreamWidget* Entry : SpawnedEntries)
	{
		if (IsValid(Entry))
		{
			Entry->DestroyWidget();
		}
	}
	SpawnedEntries.Reset();
	EntryWidgets.Reset();
}

void UDreamUIActionBar::Rebuild()
{
	ClearEntries();
	Prompts.Reset();

	UDreamWidget* BarWidget = GetWidget();
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this);
	if (!IsValid(BarWidget) || Router == nullptr)
	{
		return;//nothing to build into, or nothing to build from
	}

	// The screen's actions, then the tab view's two -- the screen's own say first what its keys do. Built in a
	// local and drawn from it: an entry's own start-up could ask for a rebuild, which resets the member.
	const int32 ResolvedUserIndex = GetUserIndex();
	TArray<FDreamUIActionBinding> Shown;
	Router->GetDisplayBindings(ResolvedUserIndex, Shown);
	AppendTabSwitchPrompts(ResolvedUserIndex, Shown);
	if (Shown.Num() > FMath::Max(1, MaxEntries))
	{
		Shown.SetNum(FMath::Max(1, MaxEntries));
	}
	Prompts = Shown;
	if (!IsValid(EntryClass))
	{
		return;//the list stands (GetPrompts); there is just nothing to draw it with
	}

	const int32 Count = Shown.Num();
	SpawnedEntries.Reserve(Count);
	EntryWidgets.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FDreamUIActionBinding& Prompt = Shown[Index];
		// Filled in before Awake: an entry that pops in blank and is corrected a frame later is visible,
		// and the entry's own Awake may already want to read what it is showing.
		UDreamWidget* Entry = CreateDreamWidget(GetWorld(), EntryClass, BarWidget,
			[&Prompt](UDreamUserWidget* LoadedRoot)
			{
				if (!IsValid(LoadedRoot))return;
				if (UDreamUIActionBarEntry* EntryComp = LoadedRoot->GetComponent<UDreamUIActionBarEntry>())
				{
					EntryComp->SetBinding(Prompt);
				}
			});
		if (IsValid(Entry))
		{
			SpawnedEntries.Add(Entry);
			EntryWidgets.Add(Entry);
		}
	}
}

#undef LOCTEXT_NAMESPACE
