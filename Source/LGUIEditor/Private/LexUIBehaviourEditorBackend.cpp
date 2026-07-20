// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIBehaviourEditorBackend.h"

#include "Core/LexUIBehaviour.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PrefabEditor/LexUIPrefabBehaviourUtils.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "SourceCodeNavigation.h"

namespace
{
	class FLexUIBlueprintBehaviourEditorBackend final : public ILexUIBehaviourEditorBackend
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

		virtual bool PromoteToVariable(ULexWidget* InRootWidget, ULexUIBehaviour* InPrimaryBehaviour, UObject* InTarget, FText& OutMessage) override
		{
			UBlueprint* Blueprint = IsValid(InPrimaryBehaviour) ? Cast<UBlueprint>(InPrimaryBehaviour->GetClass()->ClassGeneratedBy) : nullptr;
			if (Blueprint == nullptr)
			{
				OutMessage = NSLOCTEXT("LexUIBehaviourEditorBackend", "MissingBlueprint", "The primary behaviour is not backed by a Blueprint.");
				return false;
			}
			return LexUIPrefabBehaviourUtils::PromoteToVariable(Blueprint, InRootWidget, InTarget,
				LexUIPrefabBehaviourUtils::MakeVariableNameForTarget(InTarget), OutMessage);
		}

		virtual bool CanAddEventHandler(const UClass* InBehaviourClass) const override { return SupportsClass(InBehaviourClass); }

		virtual FName AddEventHandler(ULexWidget* InRootWidget, ULexUIBehaviour* InPrimaryBehaviour,
			ULexUIBehaviour* InSourceComponent, FName InEventPropertyName, FText& OutMessage) override
		{
			UBlueprint* Blueprint = IsValid(InPrimaryBehaviour) ? Cast<UBlueprint>(InPrimaryBehaviour->GetClass()->ClassGeneratedBy) : nullptr;
			FStructProperty* EventProperty = IsValid(InSourceComponent)
				? FindFProperty<FStructProperty>(InSourceComponent->GetClass(), InEventPropertyName)
				: nullptr;
			if (Blueprint == nullptr || EventProperty == nullptr)
			{
				OutMessage = NSLOCTEXT("LexUIBehaviourEditorBackend", "MissingEventInput", "The Blueprint or event property is no longer valid.");
				return NAME_None;
			}

			LexUIPrefabBehaviourUtils::FDiscoveredEvent Event;
			Event.Component = InSourceComponent;
			Event.EventProperty = EventProperty;
			Event.DisplayName = EventProperty->GetName();
			return LexUIPrefabBehaviourUtils::AddEventHandler(Blueprint, InRootWidget, Event, OutMessage);
		}
	};

	class FLexUIGenericBehaviourEditorBackend final : public ILexUIBehaviourEditorBackend
	{
	public:
		virtual FName GetBackendName() const override { return TEXT("Generic"); }
		virtual int32 GetPriority() const override { return MIN_int32; }

		virtual bool SupportsClass(const UClass* InBehaviourClass) const override
		{
			return InBehaviourClass != nullptr && InBehaviourClass->IsChildOf(ULexUIBehaviour::StaticClass());
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

FLexUIBehaviourEditorBackendRegistry& FLexUIBehaviourEditorBackendRegistry::Get()
{
	static FLexUIBehaviourEditorBackendRegistry Instance;
	return Instance;
}

void FLexUIBehaviourEditorBackendRegistry::RegisterBackend(const TSharedRef<ILexUIBehaviourEditorBackend>& InBackend)
{
	UnregisterBackend(InBackend->GetBackendName());
	Backends.Add(InBackend);
	Backends.StableSort([](const TSharedRef<ILexUIBehaviourEditorBackend>& A, const TSharedRef<ILexUIBehaviourEditorBackend>& B)
	{
		return A->GetPriority() > B->GetPriority();
	});
}

void FLexUIBehaviourEditorBackendRegistry::UnregisterBackend(FName InBackendName)
{
	Backends.RemoveAll([InBackendName](const TSharedRef<ILexUIBehaviourEditorBackend>& Backend)
	{
		return Backend->GetBackendName() == InBackendName;
	});
}

TSharedPtr<ILexUIBehaviourEditorBackend> FLexUIBehaviourEditorBackendRegistry::FindBackend(const UClass* InBehaviourClass) const
{
	for (const TSharedRef<ILexUIBehaviourEditorBackend>& Backend : Backends)
	{
		if (Backend->SupportsClass(InBehaviourClass))
		{
			return Backend;
		}
	}
	return nullptr;
}

void FLexUIBehaviourEditorBackendRegistry::RegisterBuiltInBackends()
{
	RegisterBackend(MakeShared<FLexUIBlueprintBehaviourEditorBackend>());
	RegisterBackend(MakeShared<FLexUIGenericBehaviourEditorBackend>());
}

void FLexUIBehaviourEditorBackendRegistry::UnregisterBuiltInBackends()
{
	UnregisterBackend(TEXT("Blueprint"));
	UnregisterBackend(TEXT("Generic"));
}
