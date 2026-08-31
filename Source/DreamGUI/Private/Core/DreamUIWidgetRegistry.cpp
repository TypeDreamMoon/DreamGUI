// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIWidgetRegistry.h"

TArray<FDreamUIWidgetRegistry::FEntry>& FDreamUIWidgetRegistry::Entries()
{
	// Function-local so registrations arriving during static initialization never race the array's
	// own construction -- the classic ordering problem the DECLARE macro would otherwise have.
	static TArray<FEntry> Storage;
	return Storage;
}

void FDreamUIWidgetRegistry::Register(const FEntry& InEntry)
{
	Entries().Add(InEntry);
}

UClass* FDreamUIWidgetRegistry::Resolve(FName InScope, FName InName)
{
	for (const FEntry& Entry : Entries())
	{
		if (Entry.Scope == InScope && Entry.Name == InName && Entry.ClassGetter != nullptr)
		{
			return Entry.ClassGetter();
		}
	}
	return nullptr;
}

TArray<FName> FDreamUIWidgetRegistry::NamesInScope(FName InScope)
{
	TArray<FName> Names;
	for (const FEntry& Entry : Entries())
	{
		if (Entry.Scope == InScope)
		{
			Names.Add(Entry.Name);
		}
	}
	return Names;
}
