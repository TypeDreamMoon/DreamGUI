// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Lifecycle/DreamLifecycleFixtures.h"

#if WITH_EDITOR

#include "Components/MeshComponent.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamWidgetBlueprint.h"
#include "Engine/Texture2DDynamic.h"
#include "Engine/World.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Lifecycle/DreamLifecycleProbe.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/Package.h"

namespace DreamTests::Lifecycle
{
	FScopedWorld::FScopedWorld(EWorldType::Type InWorldType)
	{
		World = UWorld::CreateWorld(InWorldType, false);
	}

	FScopedWorld::~FScopedWorld()
	{
		if (World != nullptr)
		{
			World->DestroyWorld(false);
		}
	}

	FScopedPanelClass::FScopedPanelClass(const TCHAR* InName)
	{
		Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
		Package->AddToRoot();
		Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UDreamUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
			UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		if (Blueprint != nullptr)
		{
			UDreamWidget* Root = Blueprint->GetOrCreateWidgetTree()->RootWidget;
			Root->SetDisplayName(TEXT("Panel"));
			Root->CreateNewVisual<UDreamRectBlock>();
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		}
	}

	FScopedPanelClass::~FScopedPanelClass()
	{
		if (Package != nullptr)
		{
			Package->RemoveFromRoot();
		}
	}

	UClass* FScopedPanelClass::GetClass() const
	{
		return Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
	}

	ADreamWorldWidgetActor* PlacePanel(UWorld* InWorld, UClass* InClass)
	{
		ADreamWorldWidgetActor* Actor = InWorld->SpawnActor<ADreamWorldWidgetActor>();
		if (Actor != nullptr)
		{
			Actor->GetWidgetComponent()->SetWidgetClass(InClass);
			DrawFrames(InWorld, 2);
		}
		return Actor;
	}

	UMaterialInstanceDynamic* FindMaterialReadingADynamicTexture(const UMeshComponent* InMesh)
	{
		if (InMesh == nullptr)
		{
			return nullptr;
		}
		for (UMaterialInterface* Material : InMesh->OverrideMaterials)
		{
			if (UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Material))
			{
				for (const FTextureParameterValue& Value : Instance->TextureParameterValues)
				{
					if (Cast<UTexture2DDynamic>(Value.ParameterValue) != nullptr)
					{
						return Instance;
					}
				}
			}
		}
		return nullptr;
	}

	FString JoinLines(const TArray<FString>& InLines)
	{
		return InLines.Num() > 0 ? FString::Join(InLines, TEXT("; ")) : FString(TEXT("none"));
	}
}

#endif
