// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIActionBar.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamText.h"
#include "Event/DreamEventSystem.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "Engine/Texture2D.h"

UDreamUIActionBarEntry::UDreamUIActionBarEntry()
{
	bStartWithTickEnabled = false;
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
	// though not one binding changed.
	if (UDreamEventSystem* Events = UDreamEventSystem::GetDreamEventSystemInstance(this, UserIndex))
	{
		SubscribedEventSystem = Events;
		InputDeviceChangedHandle = Events->GetInputDeviceChangedEvent().AddUObject(this, &UDreamUIActionBar::HandleInputDeviceChanged);
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

	if (UDreamEventSystem* Events = SubscribedEventSystem.Get(); Events != nullptr && InputDeviceChangedHandle.IsValid())
	{
		Events->GetInputDeviceChangedEvent().Remove(InputDeviceChangedHandle);
	}
	InputDeviceChangedHandle.Reset();
	SubscribedEventSystem.Reset();
}

void UDreamUIActionBar::HandleBindingsChanged(int32 InUserIndex)
{
	if (InUserIndex != UserIndex)return;
	Rebuild();
}

void UDreamUIActionBar::HandleInputDeviceChanged(EDreamUIInputDevice InDevice)
{
	Rebuild();
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

	UDreamWidget* BarWidget = GetWidget();
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this);
	if (!IsValid(BarWidget) || Router == nullptr || !IsValid(EntryClass))
	{
		return;//nothing to build into, or nothing to build from
	}

	TArray<FDreamUIActionBinding> Prompts;
	Router->GetDisplayBindings(UserIndex, Prompts);

	const int32 Count = FMath::Min(Prompts.Num(), FMath::Max(1, MaxEntries));
	SpawnedEntries.Reserve(Count);
	EntryWidgets.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FDreamUIActionBinding& Prompt = Prompts[Index];
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
