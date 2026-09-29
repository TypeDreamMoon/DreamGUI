// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "DreamUICaptureLibrary.h"
#include "Utils/DreamUIUtils.h"

#include "DreamPixelProbe.h"
#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * A render-target canvas used to be drawn inside the render of one of its world's views, so a world that no viewport
 * renders -- one a tool or a test makes, a game whose viewport is hidden -- never updated its targets. It is drawn by
 * a render command and a graph of its own now, as soon as it has updated, whatever renders its world or does not.
 */
namespace DreamRenderTargetDrawerTestLocal
{
	static constexpr int32 TargetExtent = 64;
	static constexpr uint8 ColourTolerance = 8;
	static constexpr int32 FramesToDraw = 6;

	/** Through a named local: a lambda's capture list carries commas, and the macro would cut its argument at the first. */
	void EnqueueStep(TFunction<bool()> InStep)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(InStep));
	}

	/** A game world of its own, which nothing renders, holding a render-target canvas with a red block in its middle. */
	struct FUnrenderedWorld
	{
		DreamTests::Lifecycle::FScopedWorld World{EWorldType::Game};
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
		TStrongObjectPtr<UDreamWidget> Root;

		bool Build(FAutomationTestBase& InTest)
		{
			if (!InTest.TestNotNull(TEXT("a game world of its own, which no viewport renders"), World.World))
			{
				return false;
			}
			UTextureRenderTarget2D* NewTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
			NewTarget->ClearColor = FLinearColor::Black;
			NewTarget->InitCustomFormat(TargetExtent, TargetExtent, EPixelFormat::PF_B8G8R8A8, false);
			NewTarget->UpdateResourceImmediate(true);
			Target.Reset(NewTarget);

			UDreamWidget* NewRoot = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
			NewRoot->SetWidth(static_cast<float>(TargetExtent));
			NewRoot->SetHeight(static_cast<float>(TargetExtent));
			NewRoot->OnRegister();
			Root.Reset(NewRoot);
			UDreamCanvas* Canvas = NewRoot->AddComponent<UDreamCanvas>();
			if (!InTest.TestNotNull(TEXT("a canvas on the root"), Canvas))
			{
				return false;
			}
			Canvas->SetRenderMode(EDreamRenderMode::RenderTarget);
			Canvas->SetRenderTargetClearColor(FColor(0, 0, 0, 255));
			Canvas->SetRenderTargetResolutionScale(1.0f);
			Canvas->SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget);
			Canvas->SetRenderTargetUpdateMode(EDreamCanvasRenderTargetUpdateMode::Always);
			Canvas->SetRenderTarget(NewTarget);

			UDreamWidget* Block = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
			Block->SetWidth(40.0f);
			Block->SetHeight(40.0f);
			Block->OnRegister();
			Block->TrySetParent(NewRoot, false);
			UDreamTexture* Visual = Block->CreateNewVisual<UDreamTexture>();
			if (!InTest.TestNotNull(TEXT("a texture visual on the block"), Visual))
			{
				return false;
			}
			Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
			Visual->SetColor(FColor::Red);
			return true;
		}

		/** One frame of this world alone: its UI manager ticked, its end-of-frame updates sent. Nothing renders it. */
		void Frame()
		{
			if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World))
			{
				Manager->Tick(1.0f / 30.0f);
			}
			World.World->SendAllEndOfFrameUpdates();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderTargetDrawerUnrenderedWorldTest,
	"DreamGUI.RHI.ARenderTargetCanvasInAWorldThatNothingRendersStillDrawsIntoItsTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRenderTargetDrawerUnrenderedWorldTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTargetDrawerTestLocal;
	const TSharedRef<FUnrenderedWorld> Stage = MakeShared<FUnrenderedWorld>();
	if (!Stage->Build(*this))
	{
		return false;
	}
	const TSharedRef<int32> Frames = MakeShared<int32>(0);
	EnqueueStep([Stage, Frames]()
	{
		Stage->Frame();
		return ++(*Frames) >= FramesToDraw;
	});
	EnqueueStep([this, Stage]()
	{
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (TestTrue(TEXT("the target reads back"), UDreamUICaptureLibrary::ReadRenderTargetPixels(Stage->Target.Get(), Pixels, Size))
			&& TestEqual(TEXT("...all of it"), Pixels.Num(), Size.X * Size.Y) && Size.X > 2 && Size.Y > 2)
		{
			FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("Drawer_UnrenderedWorld"));
			const FColor Centre = Pixels[(Size.Y / 2) * Size.X + Size.X / 2];
			const FColor Corner = Pixels[Size.X + 1];
			TestTrue(FString::Printf(TEXT("the block is in the middle of the target (%s)"), *Centre.ToString()),
				FDreamPixelProbe::IsNear(Centre, FColor::Red, ColourTolerance));
			TestTrue(FString::Printf(TEXT("and the canvas's clear colour round it (%s)"), *Corner.ToString()),
				FDreamPixelProbe::IsNear(Corner, FColor::Black, ColourTolerance));
		}
		return true;
	});
	return true;
}

#endif
