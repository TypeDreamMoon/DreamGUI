// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIBehaviourEditorBackend.h"

#include "Core/DreamUIBehaviour.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PrefabEditor/DreamUIPrefabBehaviourUtils.h"
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

		virtual bool CanPromoteToVariable(const UClass* InBehaviourClass) const override { return SupportsClass(InBehaviourClass); }

		virtual bool PromoteToVariable(UDreamWidget* InRootWidget, UDreamUIBehaviour* InPrimaryBehaviour, UObject* InTarget, FText& OutMessage) override
		{
			UBlueprint* Blueprint = IsValid(InPrimaryBehaviour) ? Cast<UBlueprint>(InPrimaryBehaviour->GetClass()->ClassGeneratedBy) : nullptr;
			if (Blueprint == nullptr)
			{
				OutMessage = NSLOCTEXT("DreamUIBehaviourEditorBackend", "MissingBlueprint", "The primary behaviour is not backed by a Blueprint.");
				return false;
			}
			return DreamUIPrefabBehaviourUtils::PromoteToVariable(Blueprint, InRootWidget, InTarget,
				DreamUIPrefabBehaviourUtils::MakeVariableNameForTarget(InTarget), OutMessage);
		}

		virtual bool CanAddEventHandler(const UClass* InBehaviourClass) const override { return SupportsClass(InBehaviourClass); }
		virtual bool CanAddEventHandler(const UClass* InBehaviourClass, EDreamUIBehaviourHandlerType InHandlerType) const override
		{
			return SupportsClass(InBehaviourClass);
		}

		virtual FName AddEventHandler(UDreamWidget* InRootWidget, UDreamUIBehaviour* InPrimaryBehaviour,
			UDreamUIBehaviour* InSourceComponent, FName InEventPropertyName, EDreamUIBehaviourHandlerType InHandlerType,
			FText& OutMessage) override
		{
			UBlueprint* Blueprint = IsValid(InPrimaryBehaviour) ? Cast<UBlueprint>(InPrimaryBehaviour->GetClass()->ClassGeneratedBy) : nullptr;
			FStructProperty* EventProperty = IsValid(InSourceComponent)
				? FindFProperty<FStructProperty>(InSourceComponent->GetClass(), InEventPropertyName)
				: nullptr;
			if (Blueprint == nullptr || EventProperty == nullptr)
			{
				OutMessage = NSLOCTEXT("DreamUIBehaviourEditorBackend", "MissingEventInput", "The Blueprint or event property is no longer valid.");
				return NAME_None;
			}

			DreamUIPrefabBehaviourUtils::FDiscoveredEvent Event;
			Event.Component = InSourceComponent;
			Event.EventProperty = EventProperty;
			Event.DisplayName = EventProperty->GetName();
			return DreamUIPrefabBehaviourUtils::AddEventHandler(Blueprint, InRootWidget, Event, InHandlerType, OutMessage);
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
