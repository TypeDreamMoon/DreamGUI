// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWorldWidgetActor.h"
#include "DreamUIBPLibrary.h"
#include "Engine/World.h"
#include "Extensions/Effects/DreamBackgroundBlur.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Lifecycle/DreamLifecycleProbe.h"
#include "Materials/MaterialInterface.h"
#include "RenderingThread.h"
#include "UObject/UObjectIterator.h"

/*
 * MADE AND UNMADE FIFTY TIMES OVER, AND NOTHING IS LEFT.
 *
 * What a canvas holds is replaced under it all the time: pages come and go, a child canvas is added to a
 * widget and taken off again, the data textures grow when a canvas outgrows them -- past 128 visuals, past
 * 32 rect blocks -- and the pools of material instances turn over when the canvas's material changes. Every
 * replacement happens under what the renderer was handed the frame before. Fifty rounds of all of it with a
 * background blur and a placed panel alongside, a collection every fifth, and afterwards the live objects
 * count back to where they started, no dynamic texture is without a size, and nothing reached the collector
 * still registered.
 */

namespace DreamLifecycleChurnTestLocal
{
	using namespace DreamTests::Lifecycle;

	constexpr int32 Rounds = 50;
	constexpr int32 BlocksPerPage = 140;
	constexpr int32 ImagesPerPage = 8;

	void Flush(bool bInFlush)
	{
		if (bInFlush)
		{
			FlushRenderingCommands();
		}
	}

	void Settle(bool bInFlush)
	{
		Flush(bInFlush);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		Flush(bInFlush);
	}

	template<typename T>
	int32 CountLive()
	{
		int32 Count = 0;
		for (TObjectIterator<T> It(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::Garbage); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	FString DescribeLiveObjects()
	{
		return FString::Printf(TEXT("%d widgets, %d trees, %d behaviours, %d meshes, %d data textures"),
			CountLive<UDreamWidget>(), CountLive<UDreamWidgetTree>(), CountLive<UDreamUIBehaviour>(),
			CountLive<UDreamUIMeshComponent>(), CountLive<UDreamUIDataAsTexture>());
	}

	/** InCount widgets drawing InVisualClass, hung under InParent. */
	void Fill(UWorld* InWorld, UDreamWidget* InParent, TSubclassOf<UDreamVisual> InVisualClass, int32 InCount)
	{
		for (int32 Index = 0; Index < InCount; ++Index)
		{
			if (UDreamWidget* Child = UDreamUIBPLibrary::ConstructWidget(InWorld, TEXT("Churn"), InVisualClass))
			{
				Child->SetWidth(8.0f);
				Child->SetHeight(8.0f);
				Child->TrySetParent(InParent, false);
			}
		}
	}

	bool RunChurn(FAutomationTestBase& InTest, bool bInFlush)
	{
		UMaterialInterface* const Materials[] = {
			LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamGUI/Materials/DreamUI_ImageAndFont.DreamUI_ImageAndFont")),
			LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamGUI/Materials/DreamUI_RectBlock.DreamUI_RectBlock")),
		};
		FScopedPanelClass Panel(bInFlush ? TEXT("ChurnRhi") : TEXT("Churn"));
		if (!InTest.TestNotNull(TEXT("the panel class compiled"), Panel.GetClass())
			|| !InTest.TestNotNull(TEXT("the image material loaded"), Materials[0])
			|| !InTest.TestNotNull(TEXT("and the rect block's"), Materials[1]))
		{
			return false;
		}
		Settle(bInFlush);
		const FString Baseline = DescribeLiveObjects();
		FLeakLogWatch Watch;
		{
			FScopedWorld Game(EWorldType::Game);
			FScopedWorld Level(EWorldType::Editor);
			UDreamScreenUISubsystem* Screen = Game.World->GetSubsystem<UDreamScreenUISubsystem>();
			if (!InTest.TestNotNull(TEXT("the game world has a screen"), Screen))
			{
				return false;
			}
			for (int32 Round = 0; Round < Rounds; ++Round)
			{
				// A page crowded past what the canvas's data textures start with room for.
				const FName PageName(*FString::Printf(TEXT("Churn%d"), Round));
				UDreamWidget* Page = Screen->ShowWidgetOfClass(PageName, UDreamUserWidget::StaticClass());
				if (!InTest.TestNotNull(FString::Printf(TEXT("round %d made its page"), Round), Page))
				{
					return false;
				}
				Fill(Game.World, Page, UDreamRectBlock::StaticClass(), BlocksPerPage);
				Fill(Game.World, Page, UDreamImage::StaticClass(), ImagesPerPage);
				// A background blur, with a canvas of its own under it.
				if (UDreamWidget* Blur = UDreamUIBPLibrary::ConstructWidget(Game.World, TEXT("Blur"), UDreamBackgroundBlur::StaticClass()))
				{
					Blur->SetWidth(64.0f);
					Blur->SetHeight(64.0f);
					Blur->TrySetParent(Page, false);
					Blur->AddComponent<UDreamCanvas>();
				}
				// A placed panel in the level alongside, gone by the end of the round.
				ADreamWorldWidgetActor* Placed = PlacePanel(Level.World, Panel.GetClass());
				DrawFrames(Game.World, 2);
				Flush(bInFlush);

				// A child canvas on and off again, under the frame just drawn.
				UDreamWidget* First = Page->GetChildren().Num() > 0 ? Page->GetChildren()[0] : nullptr;
				if (First != nullptr)
				{
					UDreamCanvas* ChildCanvas = First->AddComponent<UDreamCanvas>();
					DrawFrames(Game.World, 1);
					Flush(bInFlush);
					First->RemoveComponent(ChildCanvas);
				}
				// The screen canvas's material swapped, so its pool of material instances turns over.
				if (UDreamCanvas* Canvas = Page->GetRenderCanvas())
				{
					Canvas->SetDefaultMaterial(Materials[Round % 2]);
				}
				DrawFrames(Game.World, 2);
				Flush(bInFlush);

				Screen->RemoveUI(PageName);
				if (Placed != nullptr)
				{
					Level.World->DestroyActor(Placed);
				}
				DrawFrames(Game.World, 1);
				Flush(bInFlush);
				if (Round % 5 == 4)
				{
					Settle(bInFlush);
				}
			}
			const TArray<FString> Sizeless = FindZeroSizeDynamicTextures();
			InTest.TestEqual(TEXT("No dynamic texture was left without a size"), JoinLines(Sizeless), FString(TEXT("none")));
		}
		Settle(bInFlush);
		InTest.TestEqual(TEXT("Everything the rounds made is gone"), DescribeLiveObjects(), Baseline);
		InTest.TestEqual(TEXT("and none of it reached the collector still registered"), Watch.Describe(), FString(TEXT("none")));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleChurnTest,
	"DreamGUI.Lifecycle.MakingAndUnmakingTreesFiftyTimesOverLeavesNothingBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleChurnTest::RunTest(const FString& Parameters)
{
	// A test world has no world context of the engine's, so every destroyed actor says so.
	AddExpectedMessagePlain(TEXT("World has no context"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	return DreamLifecycleChurnTestLocal::RunChurn(*this, /*bInFlush*/ false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleChurnRhiTest,
	"DreamGUI.Lifecycle.RHI.MakingAndUnmakingTreesFiftyTimesOverUnderTheRendererLeavesNothingBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLifecycleChurnRhiTest::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("World has no context"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	return DreamLifecycleChurnTestLocal::RunChurn(*this, /*bInFlush*/ true);
}

#endif
