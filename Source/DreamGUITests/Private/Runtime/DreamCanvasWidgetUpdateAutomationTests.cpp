// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIManager.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Utils/DreamUIUtils.h"

#include "Driver/DreamDriverRig.h"
#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * A canvas used to look at every one of its widgets -- its clip, its geometry, and again to prepare the batching --
 * whenever anything in it changed, so a panel of a thousand widgets paid for a thousand when one of them moved. A
 * widget that asks for an update names itself now, and a canvas woken by its widgets alone looks at those widgets
 * alone; so does a widget coming into the canvas or going from it. Anything else that wakes the canvas -- the canvas
 * itself, a reordered hierarchy -- still looks at every widget.
 */
namespace DreamCanvasWidgetUpdateTestLocal
{
	static constexpr int32 BlockCount = 20;

	int64 WidgetsUpdatedBy(TFunctionRef<void()> InFrames)
	{
		DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
		InFrames();
		return DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true).Counters[static_cast<int32>(DreamUIRenderStats::ECounter::WidgetsUpdated)];
	}

	/** InFrames whole frames: the rig's frame, then the submission that ends an engine frame, where a canvas takes its draw calls. */
	void DrawFrames(FDreamDriverRig& InRig, UDreamUIManagerWorldSubsystem* InManager, int32 InFrames)
	{
		for (int32 Frame = 0; Frame < InFrames; ++Frame)
		{
			InRig.PumpFrames(1);
			InManager->SubmitCanvasDrawCall();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasWokenByOneWidgetTest,
	"DreamGUI.Canvas.ACanvasWokenByOneWidgetLeavesTheOthersAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasWokenByOneWidgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasWidgetUpdateTestLocal;
	DreamTests::Lifecycle::FScopedWorld World(EWorldType::Game);
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
	if (!TestNotNull(TEXT("A game world with a UI manager"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
	Root->SetWidth(400.0f);
	Root->SetHeight(400.0f);
	Root->OnRegister();
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("A canvas on the root"), Canvas))
	{
		return false;
	}
	// In the world rather than on a screen: nothing about a viewport wakes it, only its widgets and itself.
	Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);
	TArray<UDreamWidget*> Blocks;
	for (int32 Index = 0; Index < BlockCount; ++Index)
	{
		UDreamWidget* Block = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
		Block->SetWidth(10.0f);
		Block->SetHeight(10.0f);
		Block->OnRegister();
		Block->TrySetParent(Root, false);
		Block->SetAnchoredPosition(FVector2D(-150.0 + Index * 15.0, 0.0));
		if (UDreamTexture* Visual = Block->CreateNewVisual<UDreamTexture>())
		{
			Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
		}
		Blocks.Add(Block);
	}
	auto Frame = [Manager]() { Manager->Tick(1.0f / 30.0f); };
	// The first frames build the canvas: a new hierarchy, every widget looked at.
	const int64 Building = WidgetsUpdatedBy([&Frame]() { Frame(); Frame(); Frame(); });
	TestTrue(FString::Printf(TEXT("Building the canvas looks at every widget (%lld looked at)"), Building), Building >= BlockCount + 1);

	UDreamWidget* Moved = Blocks[5];
	const FDreamUIGeometry* Geometry = Moved->GetVisual() != nullptr ? static_cast<UDreamVisualBatchMesh*>(Moved->GetVisual())->GetGeometry() : nullptr;
	if (!TestTrue(TEXT("The block that moves has a geometry"), Geometry != nullptr && Geometry->Vertices.Num() > 0))
	{
		return false;
	}
	const float XBefore = Geometry->Vertices[0].Position.Y;
	const int64 OneMoved = WidgetsUpdatedBy([&Frame, Moved]()
	{
		Moved->SetAnchoredPosition(FVector2D(-60.0, 40.0));
		Frame();
	});
	TestTrue(FString::Printf(TEXT("One block moved: the canvas looks at that block and leaves the other %d widgets alone (%lld looked at)"), BlockCount, OneMoved),
		OneMoved >= 1 && OneMoved <= 2);
	TestNotEqual(TEXT("...and its geometry follows it"), Geometry->Vertices[0].Position.Y, XBefore);

	Frame();
	const int64 Nothing = WidgetsUpdatedBy([&Frame]() { Frame(); });
	TestEqual(TEXT("Nothing changed: nothing is looked at"), Nothing, static_cast<int64>(0));

	const int64 Everything = WidgetsUpdatedBy([&Frame, Canvas]()
	{
		Canvas->MarkCanvasUpdate(true);
		Frame();
	});
	TestEqual(TEXT("Woken by the canvas itself: every widget is looked at"), Everything, static_cast<int64>(BlockCount + 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasWidgetComingAndGoingTest,
	"DreamGUI.Canvas.AWidgetComingOrGoingIsLookedAtAloneAndTheDrawCallsFollowIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasWidgetComingAndGoingTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasWidgetUpdateTestLocal;
	// The widgets already in a canvas keep their order when another comes or goes, so the canvas looks at the one that
	// came and at nothing for the one that went, and its prepare keeps the last one's entries for the rest. Every prepare
	// made that way is checked here against a prepare of every widget, and a difference is an ensure.
	const DreamTests::Lifecycle::FScopedConsoleVariable Verify(TEXT("r.DreamUI.VerifyPartialPrepare"), 1);
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamCanvas* Canvas = Rig.RootCanvas();
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig's world has its UI manager"), Manager))
	{
		return false;
	}
	// White blocks in a row share a draw call; a block on a texture of its own draws in a second.
	for (int32 Index = 0; Index < BlockCount; ++Index)
	{
		UDreamWidget* Block = Rig.MakeWidget(FString::Printf(TEXT("Block%d"), Index), nullptr, FVector2D(10.0, 10.0), FVector2D(-300.0 + Index * 15.0, 0.0));
		if (UDreamTexture* Visual = Block != nullptr ? Block->CreateNewVisual<UDreamTexture>() : nullptr)
		{
			Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
		}
	}
	UTexture2D* OwnTexture = UTexture2D::CreateTransient(4, 4);
	if (!TestNotNull(TEXT("A texture of its own"), OwnTexture))
	{
		return false;
	}
	DrawFrames(Rig, Manager, 3);
	if (!TestEqual(TEXT("The white blocks share a draw call"), Canvas->GetDrawCallCount(), 1))
	{
		return false;
	}

	UDreamWidget* Odd = nullptr;
	const int64 Coming = WidgetsUpdatedBy([&Rig, Manager, OwnTexture, &Odd]()
	{
		Odd = Rig.MakeWidget(TEXT("Odd"), nullptr, FVector2D(10.0, 10.0), FVector2D(0.0, 100.0));
		if (UDreamTexture* Image = Odd != nullptr ? Odd->CreateNewVisual<UDreamTexture>() : nullptr)
		{
			Image->SetTexture(OwnTexture);
		}
		DrawFrames(Rig, Manager, 2);
	});
	if (!TestNotNull(TEXT("The block that came"), Odd))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("A block coming is looked at, and the %d already there are not (%lld looked at)"), BlockCount, Coming),
		Coming >= 1 && Coming < BlockCount);
	TestEqual(TEXT("...and it draws, in a draw call of its own"), Canvas->GetDrawCallCount(), 2);

	const int64 Going = WidgetsUpdatedBy([&Rig, Manager, Odd]()
	{
		Odd->DestroyWidget();
		DrawFrames(Rig, Manager, 2);
	});
	TestTrue(FString::Printf(TEXT("A block going has no widget looked at for it (%lld looked at)"), Going), Going < BlockCount);
	TestEqual(TEXT("...and its draw call goes with it"), Canvas->GetDrawCallCount(), 1);

	// A block moving under another parent of the same canvas comes to another place in its list, and asks.
	UDreamWidget* Holder = Rig.MakeWidget(TEXT("Holder"), nullptr, FVector2D(100.0, 100.0), FVector2D(0.0, -150.0));
	UDreamWidget* Moved = Rig.MakeWidget(TEXT("Moved"), nullptr, FVector2D(10.0, 10.0), FVector2D(200.0, 0.0));
	UDreamTexture* MovedImage = Moved != nullptr ? Moved->CreateNewVisual<UDreamTexture>() : nullptr;
	if (!TestNotNull(TEXT("A holder"), Holder) || !TestNotNull(TEXT("A block to move"), MovedImage))
	{
		return false;
	}
	MovedImage->SetTexture(OwnTexture);
	DrawFrames(Rig, Manager, 2);
	const int64 Moving = WidgetsUpdatedBy([&Rig, Manager, Moved, Holder]()
	{
		Moved->TrySetParent(Holder, /*InKeepWorldPosition*/ false);
		DrawFrames(Rig, Manager, 2);
	});
	TestTrue(FString::Printf(TEXT("A block moving within the canvas is looked at, and not every widget (%lld looked at)"), Moving),
		Moving >= 1 && Moving < BlockCount);
	TestEqual(TEXT("...and it still draws in its own draw call"), Canvas->GetDrawCallCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasWidgetHiddenOnItsOwnTest,
	"DreamGUI.Canvas.AWidgetHiddenOnItsOwnLeavesTheDrawCallsAndComesBackWhenShown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasWidgetHiddenOnItsOwnTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasWidgetUpdateTestLocal;
	// Hidden, a widget asks for an update as any other change does, so the canvas looks at it alone -- and what it drew
	// has to leave the draw calls all the same, which only a prepare of every widget can say.
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamCanvas* Canvas = Rig.RootCanvas();
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig's world has its UI manager"), Manager))
	{
		return false;
	}
	// White blocks in a row share a draw call; one block on a texture of its own draws in a second.
	for (int32 Index = 0; Index < BlockCount; ++Index)
	{
		UDreamWidget* Block = Rig.MakeWidget(FString::Printf(TEXT("Block%d"), Index), nullptr, FVector2D(10.0, 10.0), FVector2D(-300.0 + Index * 15.0, 0.0));
		if (UDreamTexture* Visual = Block != nullptr ? Block->CreateNewVisual<UDreamTexture>() : nullptr)
		{
			Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
		}
	}
	UTexture2D* OwnTexture = UTexture2D::CreateTransient(4, 4);
	UDreamWidget* Odd = Rig.MakeWidget(TEXT("Odd"), nullptr, FVector2D(10.0, 10.0), FVector2D(0.0, 100.0));
	UDreamTexture* OddImage = Odd != nullptr ? Odd->CreateNewVisual<UDreamTexture>() : nullptr;
	if (!TestNotNull(TEXT("A texture of its own"), OwnTexture) || !TestNotNull(TEXT("The odd block"), OddImage))
	{
		return false;
	}
	OddImage->SetTexture(OwnTexture);
	DrawFrames(Rig, Manager, 3);
	if (!TestEqual(TEXT("The white blocks share a draw call and the odd one has its own"), Canvas->GetDrawCallCount(), 2))
	{
		return false;
	}

	const int64 Hiding = WidgetsUpdatedBy([&Rig, Manager, Odd]()
	{
		Odd->SetWidgetActive(false);
		DrawFrames(Rig, Manager, 2);
	});
	TestTrue(FString::Printf(TEXT("Hiding the odd block looks at it and not at every widget (%lld looked at)"), Hiding), Hiding >= 1 && Hiding < BlockCount);
	TestEqual(TEXT("Hidden, the odd block's draw call is gone"), Canvas->GetDrawCallCount(), 1);

	Odd->SetWidgetActive(true);
	DrawFrames(Rig, Manager, 2);
	TestEqual(TEXT("Shown again, it is back"), Canvas->GetDrawCallCount(), 2);
	return true;
}

#endif
