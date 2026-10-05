// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/SubclassOf.h"
#include "DreamViewModelSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamViewModelRegisteredEvent, UObject*, ViewModel, FName, Name);

/** One registered view model. */
USTRUCT()
struct DREAMGUI_API FDreamViewModelRegistryEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UObject> ViewModel = nullptr;

	/** None for an entry registered by class alone. */
	UPROPERTY()
	FName Name;
};

/**
 * The game's shared view models: what a `viewmodels` entry written `= global` or `= global "Name"` is filled from.
 *
 * Per game instance, so it outlives level loads and is not shared between PIE clients. The registry holds what it is
 * given strongly until it is unregistered or the game instance shuts down.
 *
 * Lookup (Find): with a Name, the entry of that name whose object is a Class; with None, the first entry registered
 * with no name whose object is a Class, else the first entry of any name whose object is a Class. Registering an object
 * a second time under the same name replaces nothing and is ignored; registering a second object under a name already
 * taken replaces the first (and broadcasts), which is how a game swaps a global view model wholesale. With no name, the
 * entry is keyed by the object's exact class: objects of two classes sit side by side unnamed, and a second unnamed
 * object of the same class replaces the first.
 *
 * A widget whose global entry found nothing waits: OnRegisteredNative tells it when an object that fits arrives.
 */
UCLASS()
class DREAMGUI_API UDreamViewModelSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The registry of InWorldContext's game instance. Null where there is none: the designer's preview, an editor world. */
	static UDreamViewModelSubsystem* Get(const UObject* InWorldContext);

	/** Share ViewModel under Name (None: by its class alone). Null is ignored. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|ViewModel")
	void Register(UObject* ViewModel, FName Name = NAME_None);

	/** Take ViewModel out, under whatever name it was registered. False when it was not registered. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|ViewModel")
	bool Unregister(UObject* ViewModel);

	/** See the class comment for the lookup rule. Null when nothing fits. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|ViewModel", meta = (DeterminesOutputType = "Class"))
	UObject* Find(TSubclassOf<UObject> Class, FName Name = NAME_None) const;

	/** Find, or make one of Class (outered to the game instance), register it under Name and return it. Null for an abstract class. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|ViewModel", meta = (DeterminesOutputType = "Class"))
	UObject* FindOrCreate(TSubclassOf<UObject> Class, FName Name = NAME_None);

	/** After every Register that changed the registry. */
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI|ViewModel")
	FDreamViewModelRegisteredEvent OnRegistered;

	/** The same moment, for native listeners -- a widget waiting on a global entry. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRegisteredNative, UObject* /*ViewModel*/, FName /*Name*/);
	FOnRegisteredNative OnRegisteredNative;

	virtual void Deinitialize() override;

private:
	UPROPERTY(Transient)
	TArray<FDreamViewModelRegistryEntry> Entries;
};
