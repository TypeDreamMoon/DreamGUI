// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Lifecycle/DreamLifecycleFixtures.h"

#if WITH_EDITOR

#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIDataTexture.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamUIBPLibrary.h"
#include "DreamWidgetBlueprint.h"
#include "Engine/Level.h"
#include "Engine/Texture2DDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Extensions/DreamUIRenderTargetGeometrySource.h"
#include "Extensions/Effects/DreamBackgroundBlur.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Lifecycle/DreamLifecycleProbe.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace DreamTests::Lifecycle
{
	FScopedWorld::FScopedWorld(EWorldType::Type InWorldType)
	{
		World = UWorld::CreateWorld(InWorldType, false);
	}

	FScopedWorld::~FScopedWorld()
	{
		if (World == nullptr)
		{
			return;
		}
		// The trees first. DestroyWorld leaves a registered tree to the collector, whose last-resort
		// teardown reports it as leaked -- inside whichever test happens to collect next.
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World))
		{
			TArray<UDreamWidget*> Roots;
			for (UDreamWidget* Widget : Manager->GetRegisteredWidgets())
			{
				if (IsValid(Widget) && Widget->GetParent() == nullptr)
				{
					Roots.Add(Widget);
				}
			}
			for (UDreamWidget* Root : Roots)
			{
				if (IsValid(Root))
				{
					Root->DestroyWidget();
				}
			}
		}
		World->DestroyWorld(false);
	}

	FScopedPanelClass::FScopedPanelClass(const TCHAR* InName, bool bInSavedToDisk)
	{
		Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
		Package->AddToRoot();
		// A package made here reads as fully loaded only while no file has its name (see the on-disk fixture);
		// once one is saved below, anything loaded that imports the class would load it again, over the one
		// in memory.
		Package->MarkAsFullyLoaded();
		Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UDreamUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
			UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		if (Blueprint != nullptr)
		{
			UDreamWidget* Root = Blueprint->GetOrCreateWidgetTree()->RootWidget;
			Root->SetDisplayName(TEXT("Panel"));
			Root->CreateNewVisual<UDreamRectBlock>();
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
			if (bInSavedToDisk)
			{
				FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
				FSavePackageArgs Args;
				Args.TopLevelFlags = RF_Public | RF_Standalone;
				Args.SaveFlags = SAVE_None;
				// Not through GError, which in an unattended editor turns SavePackage's explanation into a crash.
				Args.Error = GWarn;
				Args.bSlowTask = false;
				if (!UPackage::Save(Package, Blueprint, *FileName, Args).IsSuccessful())
				{
					UE_LOG(LogTemp, Warning, TEXT("[%s].%d The test class %s could not be saved to %s."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Package->GetName(), *FileName);
				}
			}
		}
	}

	FScopedPanelClass::~FScopedPanelClass()
	{
		if (Package != nullptr)
		{
			if (!FileName.IsEmpty())
			{
				// A loader that read the file still holds it open.
				ResetLoaders(Package);
				IFileManager::Get().Delete(*FileName, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);
			}
			Package->RemoveFromRoot();
		}
	}

	UClass* FScopedPanelClass::GetClass() const
	{
		return Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
	}

	/** The registered hierarchy roots of InWorld. */
	TArray<UDreamWidget*> RegisteredRoots(UWorld* InWorld)
	{
		TArray<UDreamWidget*> Roots;
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(InWorld))
		{
			for (UDreamWidget* Widget : Manager->GetRegisteredWidgets())
			{
				if (Widget->GetParent() == nullptr)
				{
					Roots.Add(Widget);
				}
			}
		}
		return Roots;
	}

	/** The object a root is held by: the outer of the tree it roots, or its own outer. */
	const UObject* HolderOf(const UDreamWidget* InRoot)
	{
		const UObject* Owner = InRoot != nullptr ? InRoot->GetOuter() : nullptr;
		if (const UDreamWidgetTree* Tree = Cast<UDreamWidgetTree>(Owner); Tree != nullptr && Tree->RootWidget == InRoot)
		{
			Owner = Tree->GetOuter();
		}
		return Owner;
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

	UDreamWidget* AddBackgroundBlur(UWorld* InWorld, UDreamWidget* InParent)
	{
		if (InWorld == nullptr || InParent == nullptr)
		{
			return nullptr;
		}
		UDreamWidget* Blur = UDreamUIBPLibrary::ConstructWidget(InWorld, TEXT("Blur"), UDreamBackgroundBlur::StaticClass());
		if (Blur != nullptr)
		{
			Blur->SetWidth(64.0f);
			Blur->SetHeight(64.0f);
			Blur->TrySetParent(InParent, false);
			Blur->AddComponent<UDreamCanvas>();
		}
		return Blur;
	}

	UDreamUIRenderTargetGeometrySource* PlaceSurface(UWorld* InWorld, ULevel* InLevel, UDreamWidget*& OutCanvasRoot, bool bInAlsoOnAStaticMesh)
	{
		OutCanvasRoot = nullptr;
		if (InWorld == nullptr)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.OverrideLevel = InLevel;
		AActor* Actor = InWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		if (Actor == nullptr)
		{
			return nullptr;
		}
		USceneComponent* Anchor = NewObject<USceneComponent>(Actor, TEXT("Anchor"), RF_Transactional);
		Actor->SetRootComponent(Anchor);
		Actor->AddInstanceComponent(Anchor);
		Anchor->RegisterComponent();

		UDreamWidget* Root = UDreamUIBPLibrary::ConstructWidget(InWorld, TEXT("SurfaceCanvas"), nullptr);
		UDreamCanvas* Canvas = Root != nullptr ? Root->AddComponent<UDreamCanvas>() : nullptr;
		if (Canvas == nullptr)
		{
			if (Root != nullptr)
			{
				Root->DestroyWidget();
			}
			return nullptr;
		}
		Canvas->SetRenderMode(EDreamRenderMode::RenderTarget);
		UDreamUIBPLibrary::AttachWidgetToSceneComponent(Root, Anchor);
		if (!InWorld->IsGameWorld())
		{
			// An edited world has nothing to poll the canvas with until it has its target, so it draws first; a
			// playing world's surface polls for the target itself.
			DrawFrames(InWorld, 2);
		}
		UDreamUIRenderTargetGeometrySource* Surface = NewObject<UDreamUIRenderTargetGeometrySource>(Actor, TEXT("Surface"), RF_Transactional);
		Surface->SetCanvas(Canvas);
		Surface->SetupAttachment(Anchor);
		Actor->AddInstanceComponent(Surface);
		Surface->RegisterComponent();
		if (bInAlsoOnAStaticMesh)
		{
			UStaticMeshComponent* Shown = NewObject<UStaticMeshComponent>(Actor, TEXT("ShowsTheSurface"), RF_Transactional);
			Shown->SetupAttachment(Anchor);
			Actor->AddInstanceComponent(Shown);
			Shown->RegisterComponent();
			Shown->SetMaterial(0, Surface->GetMaterialInstance());
		}
		OutCanvasRoot = Root;
		return Surface;
	}

	bool ShowsItsCanvas(const UDreamUIRenderTargetGeometrySource* InSurface)
	{
		const UDreamCanvas* Canvas = InSurface != nullptr ? InSurface->GetCanvas() : nullptr;
		return InSurface != nullptr && InSurface->IsRegistered() && Canvas != nullptr && Canvas->GetRenderTarget() != nullptr
			&& (!FApp::CanEverRender() || InSurface->GetMaterialInstance() != nullptr);
	}

	TArray<AActor*> FWorldSpaceKinds::Actors() const
	{
		TArray<AActor*> Placed;
		for (AActor* Actor : { static_cast<AActor*>(DreamRendered), static_cast<AActor*>(EngineRendered), Surface != nullptr ? Surface->GetOwner() : nullptr })
		{
			if (Actor != nullptr)
			{
				Placed.Add(Actor);
			}
		}
		return Placed;
	}

	FWorldSpaceKinds PlaceEveryWorldSpaceKind(UWorld* InWorld, UClass* InPanelClass, bool bInWithSurface)
	{
		FWorldSpaceKinds Kinds;
		if (InWorld == nullptr)
		{
			return Kinds;
		}
		Kinds.DreamRendered = PlacePanel(InWorld, InPanelClass);
		if (Kinds.DreamRendered != nullptr)
		{
			AddBackgroundBlur(InWorld, Kinds.DreamRendered->GetWidgetComponent()->GetLoadedWidget());
		}
		Kinds.EngineRendered = InWorld->SpawnActor<ADreamWorldWidgetActor>(FVector(0.0, 400.0, 0.0), FRotator::ZeroRotator);
		if (Kinds.EngineRendered != nullptr)
		{
			Kinds.EngineRendered->GetWidgetComponent()->SetBackend(EDreamWorldWidgetBackend::UERenderer);
			Kinds.EngineRendered->GetWidgetComponent()->SetWidgetClass(InPanelClass);
		}
		if (bInWithSurface)
		{
			Kinds.Surface = PlaceSurface(InWorld, nullptr, Kinds.SurfaceCanvasRoot, /*bInAlsoOnAStaticMesh*/ true);
		}
		DrawFrames(InWorld, 2);
		return Kinds;
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
					if (Cast<UDreamUIDataTexture>(Value.ParameterValue) != nullptr || Cast<UTexture2DDynamic>(Value.ParameterValue) != nullptr)
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
