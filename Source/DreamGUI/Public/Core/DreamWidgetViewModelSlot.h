// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWidgetViewModelSlot.generated.h"

/**
 * Where a `viewmodels { … }` entry's object comes from when nobody handed one over.
 *
 * Host is what an entry with nothing after its name means: whoever makes the widget supplies the object -- an Expose
 * on Spawn pin of Create Dream Widget, UDreamUserWidget::SetViewModel, a Blueprint Set, or a host's own
 * `Name <- Other.Thing` line on this widget used as a component.
 */
UENUM()
enum class EDreamViewModelSource : uint8
{
	/** `PlayerVM Player` -- the host gives it. */
	Host,
	/** `SettingsVM Settings = new` -- this widget makes one, outered to itself, when it was given none. */
	New,
	/** `InventoryVM Inventory = global`, `= global "Stash"` -- UDreamViewModelSubsystem's, by class and name. */
	Global,
	/** `PartyVM Party = parent`, `= parent "Party"` -- the nearest enclosing user widget's view model of that class. */
	Parent,
};

/**
 * One `viewmodels` entry, compiled: the class variable that holds the object, the class it must be, and where the
 * object comes from. Filled at Initialize, after the tree is instanced and before NativeOnInitialized
 * (UDreamUserWidget::ResolveViewModels), so On Initialized and every binding's first value already see it.
 *
 * The variable itself is an ordinary Blueprint variable the compiler declares -- an object reference of Class,
 * FieldNotify and Expose on Spawn -- so a host setting it is heard by every binding that reads through it.
 */
USTRUCT()
struct DREAMGUI_API FDreamWidgetViewModelSlot
{
	GENERATED_BODY()

	/** The variable the compiler declared for the entry: the name the file wrote. */
	UPROPERTY()
	FName VariableName;

	/** The declared class. Any UObject class; one implementing INotifyFieldValueChanged is what makes bindings through it subscribe instead of poll. */
	UPROPERTY()
	TObjectPtr<UClass> Class = nullptr;

	UPROPERTY()
	EDreamViewModelSource Source = EDreamViewModelSource::Host;

	/** Global and Parent: the name written after the keyword (`global "Stash"`). None when the class alone decides. */
	UPROPERTY()
	FName SourceName;

	bool operator==(const FDreamWidgetViewModelSlot& Other) const
	{
		return VariableName == Other.VariableName
			&& Class == Other.Class
			&& Source == Other.Source
			&& SourceName == Other.SourceName;
	}
};
