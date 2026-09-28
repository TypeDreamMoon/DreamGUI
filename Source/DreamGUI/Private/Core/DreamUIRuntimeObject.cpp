// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIRuntimeObject.h"
#include "DreamGUI.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include <atomic>

namespace DreamUIRuntimeObjectLocal
{
	std::atomic<int32> CopiedIntoPlaySessionCount{ 0 };
	int32 ExpectedCopiesDepth = 0;
	/** The play sessions -- by the package of their world -- that have already ensured once. */
	TSet<FName> EnsuredPlaySessions;

	/** The tree InObject belongs to, as the outermost widget, behaviour or widget tree on its outer chain; null outside every tree. */
	const UObject* FindTreeOf(const UObject* InObject)
	{
		const UObject* Tree = nullptr;
		for (const UObject* Outer = InObject; Outer != nullptr; Outer = Outer->GetOuter())
		{
			if (Outer->IsA<UDreamWidgetTree>() || Outer->IsA<UDreamWidget>() || Outer->IsA<UDreamUIBehaviour>())
			{
				Tree = Outer;
			}
		}
		return Tree;
	}

	/**
	 * Whether a save, a play session's duplication and Copy all pass InObject over: it, or an outer of it
	 * below its level, is transient. A tree its host made is outered to the host inside the level, and is
	 * transient itself, so nothing in it is kept however its own flags read.
	 */
	bool IsLeftOutOfTheLevel(const UObject* InObject)
	{
		for (const UObject* Outer = InObject; Outer != nullptr && !Outer->IsA<ULevel>(); Outer = Outer->GetOuter())
		{
			if (Outer->HasAnyFlags(RF_Transient))
			{
				return true;
			}
		}
		return false;
	}
}

void DreamUI::ReportCopiedIntoPlaySession(const UObject& InCopy)
{
	using namespace DreamUIRuntimeObjectLocal;
	++CopiedIntoPlaySessionCount;
	UE_LOG(DreamGUI, Warning, TEXT("%s was copied into a play session's world: something the level keeps still refers into a widget tree. DreamGUI.Diag.FindTreeBridges, run in the editor, lists what refers where."),
		*InCopy.GetFullName());
	const FName Session = InCopy.GetOutermost()->GetFName();
	if (ExpectedCopiesDepth == 0 && !EnsuredPlaySessions.Contains(Session))
	{
		EnsuredPlaySessions.Add(Session);
		ensureAlwaysMsgf(false, TEXT("%s was copied into the play session %s. A widget tree is rebuilt in play from its class; a copy of the editor's tree is how the play session inherits a texture without a size."),
			*InCopy.GetFullName(), *Session.ToString());
	}
}

int32 DreamUI::GetCopiedIntoPlaySessionCount()
{
	return DreamUIRuntimeObjectLocal::CopiedIntoPlaySessionCount;
}

DreamUI::FScopedExpectedCopiesIntoPlaySession::FScopedExpectedCopiesIntoPlaySession()
{
	++DreamUIRuntimeObjectLocal::ExpectedCopiesDepth;
}

DreamUI::FScopedExpectedCopiesIntoPlaySession::~FScopedExpectedCopiesIntoPlaySession()
{
	--DreamUIRuntimeObjectLocal::ExpectedCopiesDepth;
}

bool DreamUI::IsKeptByUndo(const UObject& InObject)
{
	return InObject.HasAnyFlags(RF_Transactional) && !InObject.HasAnyFlags(RF_Transient);
}

void DreamUI::ModifyIfKeptByUndo(UObject& InObject)
{
	if (IsKeptByUndo(InObject))
	{
		InObject.Modify();
	}
}

EObjectFlags DreamUI::TransactionalFlagFor(const UObject* InOuter)
{
	if (InOuter == nullptr || !IsKeptByUndo(*InOuter) || InOuter->IsA<UWorld>())
	{
		return RF_NoFlags;
	}
	return RF_Transactional;
}

TArray<DreamUI::FTreeBridge> DreamUI::FindTreeBridges(const UWorld& InWorld)
{
	using namespace DreamUIRuntimeObjectLocal;

	TMap<const UObject*, const AActor*> HostOfTree;
	for (TObjectIterator<UDreamWidgetPresenterComponentBase> It; It; ++It)
	{
		if (It->GetWorld() != &InWorld)
		{
			continue;
		}
		if (const UObject* Tree = FindTreeOf(It->GetLoadedWidget()))
		{
			HostOfTree.Add(Tree, It->GetOwner());
		}
	}

	TArray<FTreeBridge> Bridges;
	for (const ULevel* Level : InWorld.GetLevels())
	{
		if (Level == nullptr)
		{
			continue;
		}
		for (AActor* Actor : Level->Actors)
		{
			if (!IsValid(Actor) || Actor->HasAnyFlags(RF_Transient))
			{
				continue;
			}
			TArray<UObject*> Kept;
			Kept.Add(Actor);
			GetObjectsWithOuter(Actor, Kept, /*bIncludeNestedObjects*/ true);
			for (UObject* Object : Kept)
			{
				// Only what the level keeps: a save and a play session's duplication both pass over transient
				// objects, and over whatever a transient object holds -- a hosted tree among them.
				if (IsLeftOutOfTheLevel(Object))
				{
					continue;
				}
				TArray<UObject*> Referenced;
				FReferenceFinder Finder(Referenced, nullptr, /*bRequireDirectOuter*/ false, /*bShouldIgnoreArchetype*/ true,
					/*bSerializeRecursively*/ false, /*bShouldIgnoreTransient*/ true);
				Finder.FindReferences(Object);
				for (const UObject* Target : Referenced)
				{
					if (Target == nullptr || !Target->IsIn(&InWorld))
					{
						continue;
					}
					const UObject* Tree = FindTreeOf(Target);
					if (Tree == nullptr)
					{
						continue;
					}
					const AActor* const* Host = HostOfTree.Find(Tree);
					FTreeBridge& Bridge = Bridges.AddDefaulted_GetRef();
					Bridge.From = Object->GetPathName();
					Bridge.To = Target->GetPathName();
					Bridge.Host = Host != nullptr ? GetPathNameSafe(*Host) : FString();
					Bridge.bIntoOwnHost = Host != nullptr && *Host == Actor;
				}
			}
		}
	}
	return Bridges;
}

static FAutoConsoleCommandWithWorldAndArgs GDreamGUIFindTreeBridgesCommand(
	TEXT("DreamGUI.Diag.FindTreeBridges"),
	TEXT("Lists every reference from an object the current world's levels keep -- an actor, one of its components or sub-objects -- into a DreamGUI widget tree, and whose tree it is. A play session's copy of the level follows such a reference into the tree."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* InWorld)
	{
		if (InWorld == nullptr)
		{
			UE_LOG(DreamGUI, Display, TEXT("DreamGUI.Diag.FindTreeBridges: no world."));
			return;
		}
		const TArray<DreamUI::FTreeBridge> Bridges = DreamUI::FindTreeBridges(*InWorld);
		UE_LOG(DreamGUI, Display, TEXT("DreamGUI.Diag.FindTreeBridges: %d reference(s) from what %s keeps into widget trees."),
			Bridges.Num(), *InWorld->GetPathName());
		for (const DreamUI::FTreeBridge& Bridge : Bridges)
		{
			const FString Whose = Bridge.bIntoOwnHost ? FString(TEXT("its own actor's tree"))
				: Bridge.Host.IsEmpty() ? FString(TEXT("a tree no presenter in this world loaded"))
				: FString::Printf(TEXT("the tree of %s"), *Bridge.Host);
			UE_LOG(DreamGUI, Display, TEXT("  %s -> %s (%s)"), *Bridge.From, *Bridge.To, *Whose);
		}
	}));
