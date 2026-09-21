// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIWidgetRegistry.h"

#include "Algo/Sort.h"

namespace DreamUIWidgetRegistryLocal
{
	/**
	 * The one bare tag with no class behind it.
	 *
	 * `Widget` is the language's word for a node that draws nothing, so it cannot be declared next
	 * to a visual the way every other tag is -- there is no visual. It stays here rather than in
	 * the builder because "which bare tags exist" now has exactly one answer, and a tag the
	 * enumerator does not know is a tag the VSCode extension marks as an error.
	 */
	static const TCHAR* WidgetTag = TEXT("Widget");
}

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

void FDreamUIWidgetRegistry::GetAllEntries(TArray<FEntry>& OutEntries)
{
	// Scoped tags only, which is what this has always meant and what its one caller -- the symbol
	// export, which keys them "Scope.Name" -- needs. Visual tags have their own enumerator because
	// they have no scope to key by.
	OutEntries.Reset();
	for (const FEntry& Entry : Entries())
	{
		if (Entry.Kind == EKind::ScopedWidget)
		{
			OutEntries.Add(Entry);
		}
	}
}

UClass* FDreamUIWidgetRegistry::Resolve(FName InScope, FName InName)
{
	for (const FEntry& Entry : Entries())
	{
		if (Entry.Kind == EKind::ScopedWidget && Entry.Scope == InScope && Entry.Name == InName
			&& Entry.ClassGetter != nullptr)
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
		if (Entry.Kind == EKind::ScopedWidget && Entry.Scope == InScope)
		{
			Names.Add(Entry.Name);
		}
	}
	return Names;
}

UClass* FDreamUIWidgetRegistry::ResolveVisual(FName InTag, bool& bOutIsKnownTag)
{
	if (InTag == FName(DreamUIWidgetRegistryLocal::WidgetTag))
	{
		bOutIsKnownTag = true;
		return nullptr;
	}
	for (const FEntry& Entry : Entries())
	{
		if (Entry.Kind == EKind::VisualTag && Entry.Name == InTag && Entry.ClassGetter != nullptr)
		{
			bOutIsKnownTag = true;
			return Entry.ClassGetter();
		}
	}
	bOutIsKnownTag = false;
	return nullptr;
}

void FDreamUIWidgetRegistry::GetVisualEntries(TArray<TPair<FName, UClass*>>& OutEntries)
{
	OutEntries.Reset();
	for (const FEntry& Entry : Entries())
	{
		if (Entry.Kind == EKind::VisualTag && Entry.ClassGetter != nullptr)
		{
			OutEntries.Emplace(Entry.Name, Entry.ClassGetter());
		}
	}
	Algo::Sort(OutEntries, [](const TPair<FName, UClass*>& A, const TPair<FName, UClass*>& B)
	{
		return A.Key.LexicalLess(B.Key);
	});
	// First, and not part of the sort: it is the tag an author reaches for most and the only one
	// that is not a visual, so it heads the list in every place this feeds.
	OutEntries.Insert(TPair<FName, UClass*>(FName(DreamUIWidgetRegistryLocal::WidgetTag), nullptr), 0);
}

FString FDreamUIWidgetRegistry::FindTagForClass(const UClass* InClass)
{
	if (InClass == nullptr)
	{
		return FString();
	}
	// EXACT, not IsChildOf. A subclass is a different tag when it has one and is unspellable when
	// it does not, and answering "Image" for a UDreamPolygon would have the write-back emit a node
	// that rebuilds as the wrong class -- a lossy round trip that compiles, which is the worst kind.
	for (const FEntry& Entry : Entries())
	{
		if (Entry.ClassGetter == nullptr || Entry.ClassGetter() != InClass)
		{
			continue;
		}
		return Entry.Kind == EKind::VisualTag
			? Entry.Name.ToString()
			: FString::Printf(TEXT("%s.%s"), *Entry.Scope.ToString(), *Entry.Name.ToString());
	}
	return FString();
}
