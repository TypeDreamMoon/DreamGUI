// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "ViewModel/DreamViewModelSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UDreamViewModelSubsystem* UDreamViewModelSubsystem::Get(const UObject* InWorldContext)
{
	if (InWorldContext == nullptr || GEngine == nullptr)
	{
		return nullptr;
	}
	// ReturnNull rather than the logging modes: "no registry here" is the ordinary answer in the designer's preview
	// and in every editor world, and a widget asks it at every Initialize.
	UWorld* World = GEngine->GetWorldFromContextObject(InWorldContext, EGetWorldErrorMode::ReturnNull);
	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UDreamViewModelSubsystem>() : nullptr;
}

void UDreamViewModelSubsystem::Register(UObject* ViewModel, FName Name)
{
	if (!IsValid(ViewModel))
	{
		return;
	}

	// The entry this registration lands on. A named one is keyed by its name. An unnamed one is keyed by the object's
	// exact class -- "by its class alone" -- so a game can register its inventory and its settings unnamed side by
	// side, and registering a second object of the same class unnamed swaps the first, the way a second object under
	// a taken name does.
	FDreamViewModelRegistryEntry* Existing = Entries.FindByPredicate([ViewModel, Name](const FDreamViewModelRegistryEntry& Entry)
	{
		if (Entry.Name != Name)
		{
			return false;
		}
		if (!Name.IsNone())
		{
			return true;
		}
		const UObject* Held = Entry.ViewModel.Get();
		return Held == ViewModel || (IsValid(Held) && Held->GetClass() == ViewModel->GetClass());
	});

	if (Existing != nullptr)
	{
		if (Existing->ViewModel == ViewModel)
		{
			// The same object under the same name: nothing changed, so nobody is told.
			return;
		}
		Existing->ViewModel = ViewModel;
	}
	else
	{
		FDreamViewModelRegistryEntry& Added = Entries.AddDefaulted_GetRef();
		Added.ViewModel = ViewModel;
		Added.Name = Name;
	}

	// After the registry holds it, so a listener that calls Find gets it -- the native side first, which is where the
	// widgets waiting on a `= global` entry are. Nothing iterates Entries across the broadcasts, so a listener is free to
	// register or unregister in turn.
	OnRegisteredNative.Broadcast(ViewModel, Name);
	OnRegistered.Broadcast(ViewModel, Name);
}

bool UDreamViewModelSubsystem::Unregister(UObject* ViewModel)
{
	if (ViewModel == nullptr)
	{
		return false;
	}
	// Every name it was registered under: one object can be shared as "Stash" and by class at once.
	const int32 Removed = Entries.RemoveAll([ViewModel](const FDreamViewModelRegistryEntry& Entry)
	{
		return Entry.ViewModel == ViewModel;
	});
	return Removed > 0;
}

UObject* UDreamViewModelSubsystem::Find(TSubclassOf<UObject> Class, FName Name) const
{
	const UClass* WantedClass = Class.Get();
	if (WantedClass == nullptr)
	{
		return nullptr;
	}
	auto Fits = [WantedClass](const FDreamViewModelRegistryEntry& Entry)
	{
		const UObject* Held = Entry.ViewModel.Get();
		return IsValid(Held) && Held->IsA(WantedClass);
	};

	if (!Name.IsNone())
	{
		for (const FDreamViewModelRegistryEntry& Entry : Entries)
		{
			if (Entry.Name == Name && Fits(Entry))
			{
				return Entry.ViewModel;
			}
		}
		return nullptr;
	}

	// By class: what was shared by class first, then whatever was shared under a name -- a game that only ever names
	// its view models is still found by an entry that does not.
	for (const FDreamViewModelRegistryEntry& Entry : Entries)
	{
		if (Entry.Name.IsNone() && Fits(Entry))
		{
			return Entry.ViewModel;
		}
	}
	for (const FDreamViewModelRegistryEntry& Entry : Entries)
	{
		if (Fits(Entry))
		{
			return Entry.ViewModel;
		}
	}
	return nullptr;
}

UObject* UDreamViewModelSubsystem::FindOrCreate(TSubclassOf<UObject> Class, FName Name)
{
	if (UObject* Found = Find(Class, Name))
	{
		return Found;
	}
	UClass* MadeClass = Class.Get();
	if (MadeClass == nullptr || MadeClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return nullptr;
	}
	// Outered to the game instance, which is what its GetWorld walks to and what it lives as long as.
	UObject* Made = NewObject<UObject>(GetGameInstance(), MadeClass);
	Register(Made, Name);
	return Made;
}

void UDreamViewModelSubsystem::Deinitialize()
{
	// The game instance is going: what it shared goes with it, and nobody is left to wait for a registration.
	Entries.Empty();
	OnRegisteredNative.Clear();
	OnRegistered.Clear();
	Super::Deinitialize();
}
