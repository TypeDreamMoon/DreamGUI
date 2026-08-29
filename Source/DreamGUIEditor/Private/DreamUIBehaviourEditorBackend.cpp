// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIBehaviourEditorBackend.h"

#include "Core/DreamUIBehaviour.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "SourceCodeNavigation.h"

namespace
{
	class FDreamUIBlueprintBehaviourEditorBackend final : public IDreamUIBehaviourEditorBackend
	{
	public:
		virtual FName GetBackendName() const override { return TEXT("Blueprint"); }
		virtual int32 GetPriority() const override { return 100; }

		virtual bool SupportsClass(const UClass* InBehaviourClass) const override
		{
			return InBehaviourClass != nullptr && Cast<UBlueprint>(InBehaviourClass->ClassGeneratedBy) != nullptr;
		}

		virtual bool OpenClass(UClass* InBehaviourClass) override
		{
			if (UBlueprint* Blueprint = IsValid(InBehaviourClass) ? Cast<UBlueprint>(InBehaviourClass->ClassGeneratedBy) : nullptr)
			{
				return GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
			}
			return false;
		}

		/**
		 * Both of these wrote into a prefab's COMPANION behaviour Blueprint -- the second asset a prefab
		 * needed to hold its widget variables and its graph. A widget class holds both itself: the
		 * compiler mints the variables from the display names, and the graph is the Blueprint's own.
		 *
		 * They answer no rather than disappearing, because the panel asks before it offers, and "no,
		 * and here is why" is the answer worth showing.
		 */
		virtual bool CanPromoteToVariable(const UClass* InBehaviourClass) const override { return false; }

		virtual bool PromoteToVariable(UDreamWidget* InRootWidget, UDreamUIBehaviour* InPrimaryBehaviour, UObject* InTarget, FText& OutMessage) override
		{
			OutMessage = NSLOCTEXT("DreamUIBehaviourEditorBackend", "PromoteRetired",
				"Widget variables are minted by the Widget Blueprint compiler from the display names in the hierarchy. Rename the widget in the designer instead.");
			return false;
		}

		virtual bool CanAddEventHandler(const UClass* InBehaviourClass) const override { return false; }
		virtual bool CanAddEventHandler(const UClass* InBehaviourClass, EDreamUIBehaviourHandlerType InHandlerType) const override
		{
			return false;
		}

		virtual FName AddEventHandler(UDreamWidget* InRootWidget, UDreamUIBehaviour* InPrimaryBehaviour,
			UDreamUIBehaviour* InSourceComponent, FName InEventPropertyName, EDreamUIBehaviourHandlerType InHandlerType,
			FText& OutMessage) override
		{
			OutMessage = NSLOCTEXT("DreamUIBehaviourEditorBackend", "AddEventRetired",
				"Bind the event in the Widget Blueprint's own graph; it has the widget variables the handler needs.");
			return NAME_None;
		}
	};

	class FDreamUIGenericBehaviourEditorBackend final : public IDreamUIBehaviourEditorBackend
	{
	public:
		virtual FName GetBackendName() const override { return TEXT("Generic"); }
		virtual int32 GetPriority() const override { return MIN_int32; }

		virtual bool SupportsClass(const UClass* InBehaviourClass) const override
		{
			return InBehaviourClass != nullptr && InBehaviourClass->IsChildOf(UDreamUIBehaviour::StaticClass());
		}

		virtual bool OpenClass(UClass* InBehaviourClass) override
		{
			if (!IsValid(InBehaviourClass))
			{
				return false;
			}
			if (UObject* GeneratedBy = InBehaviourClass->ClassGeneratedBy)
			{
				if (GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(GeneratedBy))
				{
					return true;
				}
			}
			return FSourceCodeNavigation::NavigateToClass(InBehaviourClass);
		}
	};
}

FDreamUIBehaviourEditorBackendRegistry& FDreamUIBehaviourEditorBackendRegistry::Get()
{
	static FDreamUIBehaviourEditorBackendRegistry Instance;
	return Instance;
}

void FDreamUIBehaviourEditorBackendRegistry::RegisterBackend(const TSharedRef<IDreamUIBehaviourEditorBackend>& InBackend)
{
	UnregisterBackend(InBackend->GetBackendName());
	Backends.Add(InBackend);
	Backends.StableSort([](const TSharedRef<IDreamUIBehaviourEditorBackend>& A, const TSharedRef<IDreamUIBehaviourEditorBackend>& B)
	{
		return A->GetPriority() > B->GetPriority();
	});
}

void FDreamUIBehaviourEditorBackendRegistry::UnregisterBackend(FName InBackendName)
{
	Backends.RemoveAll([InBackendName](const TSharedRef<IDreamUIBehaviourEditorBackend>& Backend)
	{
		return Backend->GetBackendName() == InBackendName;
	});
}

TSharedPtr<IDreamUIBehaviourEditorBackend> FDreamUIBehaviourEditorBackendRegistry::FindBackend(const UClass* InBehaviourClass) const
{
	for (const TSharedRef<IDreamUIBehaviourEditorBackend>& Backend : Backends)
	{
		if (Backend->SupportsClass(InBehaviourClass))
		{
			return Backend;
		}
	}
	return nullptr;
}

void FDreamUIBehaviourEditorBackendRegistry::RegisterBuiltInBackends()
{
	RegisterBackend(MakeShared<FDreamUIBlueprintBehaviourEditorBackend>());
	RegisterBackend(MakeShared<FDreamUIGenericBehaviourEditorBackend>());
}

void FDreamUIBehaviourEditorBackendRegistry::UnregisterBuiltInBackends()
{
	UnregisterBackend(TEXT("Blueprint"));
	UnregisterBackend(TEXT("Generic"));
}
