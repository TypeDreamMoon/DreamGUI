// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "MCP/DreamGUIMCPStage.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "DreamWidgetBlueprint.h"
#include "Preview/DreamWidgetPreviewScene.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace DreamGUIMCP
{
	namespace
	{
		TUniquePtr<FScreenStage> GStage;

		/**
		 * The visuals that keep time of their own outside a game world: a Niagara system steps its simulation as its canvas
		 * draws, by the engine's frame time, once a frame. The stage holds them (paused) and steps them by the passes' time
		 * instead, so the `time` and the frames of a preview show the particles a game would after as many seconds, and a
		 * pass of no time leaves them where they are. Reached by reflection: their module, DreamGUINiagara, is one this
		 * editor module does not depend on.
		 */
		void ForEachSelfTimedVisual(UDreamWidget* InRoot, TFunctionRef<void(UObject*)> InVisit)
		{
			const UClass* VisualClass = FindObject<UClass>(nullptr, TEXT("/Script/DreamGUINiagara.DreamNiagaraSystem"));
			if (VisualClass == nullptr || InRoot == nullptr)
			{
				return;
			}
			TArray<UDreamWidget*> Widgets;
			UDreamWidget::CollectChildrenWidgets(InRoot, Widgets, true);
			for (UDreamWidget* Widget : Widgets)
			{
				UObject* Visual = IsValid(Widget) ? Widget->GetVisual() : nullptr;
				if (Visual != nullptr && Visual->IsA(VisualClass))
				{
					InVisit(Visual);
				}
			}
		}

		/** Calls InObject's UFunction InName with the parameters InFill sets (each starts at its default). */
		void CallFunction(UObject* InObject, const TCHAR* InName, TFunctionRef<void(UFunction*, uint8*)> InFill)
		{
			UFunction* Function = InObject != nullptr ? InObject->FindFunction(FName(InName)) : nullptr;
			if (Function == nullptr)
			{
				return;
			}
			const int32 Size = FMath::Max<int32>(Function->ParmsSize, 1);
			uint8* Params = static_cast<uint8*>(FMemory::Malloc(Size, Function->GetMinAlignment()));
			FMemory::Memzero(Params, Size);
			Function->InitializeStruct(Params);
			InFill(Function, Params);
			InObject->ProcessEvent(Function, Params);
			Function->DestroyStruct(Params);
			FMemory::Free(Params);
		}
	}

	FScreenStage& FScreenStage::Get()
	{
		if (!GStage.IsValid())
		{
			GStage = MakeUnique<FScreenStage>();
		}
		return *GStage;
	}

	void FScreenStage::Shutdown()
	{
		if (GStage.IsValid())
		{
			GStage->Close();
		}
		GStage.Reset();
	}

	FScreenStage::~FScreenStage()
	{
		Close();
	}

	UWorld* FScreenStage::GetWorld() const
	{
		return Scene.IsValid() ? Scene->GetWorld() : nullptr;
	}

	UDreamWidget* FScreenStage::GetContentRoot() const
	{
		return Screen != nullptr ? Screen->GetContentRoot() : nullptr;
	}

	void FScreenStage::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(Target);
		Collector.AddReferencedObject(Root);
		Collector.AddReferencedObject(Canvas);
		Collector.AddReferencedObject(Screen);
	}

	UWorld* FScreenStage::EnsureWorld()
	{
		if (!Scene.IsValid())
		{
			// No lights, no physics, no undo: nothing here is lit or moves, and nothing done to a stage is an edit. In a
			// commandlet the world gets its UI manager only inside the scope (a commandlet's worlds have none otherwise).
			TOptional<UDreamUIManagerWorldSubsystem::FCommandletWorldScope> Allow;
			if (IsRunningCommandlet())
			{
				Allow.Emplace();
			}
			FDreamWidgetPreviewScene::ConstructionValues Values;
			Values.SetCreateDefaultLighting(false)
				.SetCreatePhysicsScene(false)
				.SetTransactional(false)
				.SetForceMipsResident(false)
				.SetName(MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("DreamMCPStage")));
			Scene = MakeUnique<FDreamWidgetPreviewScene>(Values);
		}
		// Null once the engine has begun to exit: the scene lets its world go then.
		return Scene->GetWorld();
	}

	UTextureRenderTarget2D* FScreenStage::EnsureTarget(const FIntPoint InSize)
	{
		if (Target == nullptr)
		{
			Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
			Target->AddressX = TextureAddress::TA_Clamp;
			Target->AddressY = TextureAddress::TA_Clamp;
		}
		Target->ClearColor = FLinearColor(Options.Backdrop);
		if (Target->SizeX != InSize.X || Target->SizeY != InSize.Y || Target->GetResource() == nullptr)
		{
			// The format the render tests draw into and hold to their goldens: 8-bit, the colours a screen shows.
			Target->InitCustomFormat(static_cast<uint32>(InSize.X), static_cast<uint32>(InSize.Y), EPixelFormat::PF_B8G8R8A8, false);
			Target->UpdateResourceImmediate(true);
		}
		return Target;
	}

	bool FScreenStage::Open(UDreamWidgetBlueprint* InBlueprint, const FStageOptions& InOptions, FString& OutError)
	{
		Close();
		Options = InOptions;
		Options.Viewport.X = FMath::Clamp(Options.Viewport.X, 16, 8192);
		Options.Viewport.Y = FMath::Clamp(Options.Viewport.Y, 16, 8192);
		ScreenTime = 0.0;

		UWorld* World = EnsureWorld();
		UDreamUIManagerWorldSubsystem* Manager = World != nullptr ? UDreamUIManagerWorldSubsystem::GetInstance(World) : nullptr;
		if (Manager == nullptr)
		{
			OutError = TEXT("the stage's world has no DreamGUI manager, so nothing can be laid out in this process");
			return false;
		}

		// The design canvas, as the thumbnails make it: a render-target canvas whose scaler matches the width to the
		// viewport, so the root is the viewport's size in units and a unit is a pixel of the picture.
		Root = NewObject<UDreamWidget>(World, NAME_None, RF_Transient);
		Root->SetDisplayName(TEXT("DreamMCPStageRoot"));
		Root->SetWidth(static_cast<float>(Options.Viewport.X));
		Root->SetHeight(static_cast<float>(Options.Viewport.Y));
		Root->OnRegister();
		Canvas = Root->AddComponent<UDreamCanvas>();
		if (Canvas == nullptr)
		{
			Root->DestroyWidget();
			Root = nullptr;
			OutError = TEXT("the stage's root would not take a canvas");
			return false;
		}
		Canvas->SetRenderMode(EDreamRenderMode::RenderTarget);
		Canvas->SetRenderTargetClearColor(Options.Backdrop);
		Canvas->SetRenderTargetResolutionScale(1.0f);
		Canvas->SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget);
		// A stage that only lays out never draws: no renderer is asked for anything.
		Canvas->SetRenderTargetUpdateMode(Options.bDraw ? EDreamCanvasRenderTargetUpdateMode::Always : EDreamCanvasRenderTargetUpdateMode::WhenRequest);
		Canvas->SetScaleMode(EDreamCanvasScaleMode::ScaleWithScreenSize);
		Canvas->SetScreenMatchMode(EDreamCanvasScreenMatchMode::MatchWidthOrHeight);
		Canvas->SetMatchFromWidthToHeight(0.0f);
		Canvas->SetReferenceResolution(FVector2D(Options.Viewport));
		Canvas->SetRenderTarget(EnsureTarget(Options.Viewport));

		Screen = FDreamWidgetPreviewHost::InstancePreview(InBlueprint, Root);
		if (Screen == nullptr)
		{
			OutError = TEXT("the widget Blueprint has no class that can be instanced");
			Close();
			return false;
		}
		// What the preview host does after the same call: tell the canvas it has something to draw, and put away what the
		// author put away in the designer.
		UDreamUIManagerWorldSubsystem::RefreshAllUI(World);
		FDreamWidgetPreviewHost::ApplyHiddenInDesigner(InBlueprint, Screen->GetContentRoot());
		ForEachSelfTimedVisual(Root, [](UObject* Visual)
		{
			CallFunction(Visual, TEXT("SetPaused"), [](UFunction* Function, uint8* Params)
			{
				if (const FBoolProperty* Paused = FindFProperty<FBoolProperty>(Function, TEXT("bInPaused")))
				{
					Paused->SetPropertyValue_InContainer(Params, true);
				}
			});
		});
		return true;
	}

	void FScreenStage::Pass(const float InDeltaSeconds)
	{
		UWorld* World = GetWorld();
		UDreamUIManagerWorldSubsystem* Manager = World != nullptr ? UDreamUIManagerWorldSubsystem::GetInstance(World) : nullptr;
		if (Manager == nullptr || Root == nullptr)
		{
			return;
		}
		// Every glyph rasterised on the spot: a glyph handed to a worker lands after the layout read it, and the text is
		// measured and drawn without it.
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
		if (InDeltaSeconds > 0.0f && Options.bDraw)
		{
			// The stage's world, as an asset editor ticks its preview world: a playing timeline is a sequence player, which the
			// world's sequence tick manager advances. Only while drawing: a layout reads the screen before anything plays.
			World->Tick(LEVELTICK_All, InDeltaSeconds);
		}
		if (InDeltaSeconds > 0.0f)
		{
			ForEachSelfTimedVisual(Root, [InDeltaSeconds](UObject* Visual)
			{
				CallFunction(Visual, TEXT("Advance"), [InDeltaSeconds](UFunction* Function, uint8* Params)
				{
					for (const TCHAR* Name : { TEXT("InSeconds"), TEXT("InStep") })
					{
						if (const FFloatProperty* Seconds = FindFProperty<FFloatProperty>(Function, Name))
						{
							Seconds->SetPropertyValue_InContainer(Params, InDeltaSeconds);
						}
					}
				});
			});
		}
		Manager->TickDreamUI(InDeltaSeconds);
		World->SendAllEndOfFrameUpdates();
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1);
		ScreenTime += InDeltaSeconds;
	}

	int32 FScreenStage::CountPendingGlyphs(const bool bFinish) const
	{
		if (Root == nullptr)
		{
			return 0;
		}
		TArray<UDreamWidget*> Widgets;
		UDreamWidget::CollectChildrenWidgets(Root, Widgets, true);
		TArray<UDreamUIFontData_FreeTypeRender*> Fonts;
		for (UDreamWidget* Widget : Widgets)
		{
			const UDreamText* Text = IsValid(Widget) ? Cast<UDreamText>(Widget->GetVisual()) : nullptr;
			if (UDreamUIFontData_FreeTypeRender* Font = Text != nullptr ? Cast<UDreamUIFontData_FreeTypeRender>(Text->GetFont()) : nullptr)
			{
				Fonts.AddUnique(Font);
			}
		}
		int32 Pending = 0;
		for (UDreamUIFontData_FreeTypeRender* Font : Fonts)
		{
			if (bFinish && Font->GetPendingAsyncGlyphCount() > 0)
			{
				Font->WaitForAsyncGlyphs();
			}
			Pending += Font->GetPendingAsyncGlyphCount();
		}
		return Pending;
	}

	bool FScreenStage::ReadPixels(TArray<FColor>& OutPixels, FIntPoint& OutSize) const
	{
		OutPixels.Reset();
		if (!Options.bDraw || Target == nullptr || !FApp::CanEverRender())
		{
			return false;
		}
		// The draw was recorded on the render thread, a frame behind the game thread: what is read must be what it drew.
		FlushRenderingCommands();
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		if (Resource == nullptr)
		{
			return false;
		}
		OutSize = Resource->GetSizeXY();
		FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);
		Flags.SetLinearToGamma(false);
		if (!Resource->ReadPixels(OutPixels, Flags) || OutPixels.Num() != OutSize.X * OutSize.Y)
		{
			OutPixels.Reset();
			return false;
		}
		return true;
	}

	void FScreenStage::Close()
	{
		// Deliberately not IsValid(): see FDreamWidgetPreviewHost::DestroyPreview.
		if (Screen != nullptr && !Screen->HasAnyFlags(RF_FinishDestroyed))
		{
			Screen->DestroyWidget();
		}
		Screen = nullptr;
		if (Root != nullptr && !Root->HasAnyFlags(RF_FinishDestroyed))
		{
			Root->DestroyWidget();
		}
		Root = nullptr;
		Canvas = nullptr;
	}
}
