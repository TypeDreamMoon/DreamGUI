// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Components/SceneComponent.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamUIBPLibrary.h"
#include "DreamWidgetLifecycleTestTypes.h"
#include "Engine/World.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Lifecycle/DreamLifecycleProbe.h"

/*
 * Canvases coming and going under a panel that keeps drawing.
 *
 * A visual writes itself into the canvas it draws in: its geometry into the canvas's draw calls, its
 * clip rect and properties into the canvas's data textures, at positions that canvas handed out. When
 * the widget's canvas changes -- a canvas added to it or to a parent, or taken away -- none of that is
 * in the new canvas until the visual writes it again.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleVisualFollowsItsCanvasTest,
	"DreamGUI.Lifecycle.AVisualThatMovesToAnotherCanvasIsWrittenWholeIntoIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleVisualFollowsItsCanvasTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	FScopedPanelClass Panel(TEXT("LifecycleCanvasSwitch"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = PlacePanel(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("the panel was placed"), Actor))return false;
	UDreamWidget* Root = Actor->GetWidgetComponent()->GetLoadedWidget();
	UDreamCanvas* PanelCanvas = Actor->GetWidgetComponent()->GetLoadedCanvas();
	if (!TestNotNull(TEXT("with its tree"), Root) || !TestNotNull(TEXT("and its canvas"), PanelCanvas))return false;

	UDreamWidget* Probe = UDreamUIBPLibrary::ConstructWidget(Level.World, TEXT("Probe"), UDreamWidgetCanvasProbeVisual::StaticClass());
	UDreamWidgetCanvasProbeVisual* Visual = Probe != nullptr ? Cast<UDreamWidgetCanvasProbeVisual>(Probe->GetVisual()) : nullptr;
	if (!TestNotNull(TEXT("a widget that records its visual's canvas"), Visual))return false;
	TestTrue(TEXT("joins the panel"), Probe->TrySetParent(Root, false));
	DrawFrames(Level.World, 2);
	TestTrue(TEXT("and draws in the panel's canvas"), Probe->GetRenderCanvas() == PanelCanvas);

	Visual->CanvasChanges.Reset();
	Visual->MarkAllDirtyCount = 0;
	UDreamCanvas* OwnCanvas = Probe->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("the widget is given a canvas of its own"), OwnCanvas))return false;
	DrawFrames(Level.World, 1);
	TestTrue(TEXT("and draws in it"), Probe->GetRenderCanvas() == OwnCanvas);
	TestTrue(TEXT("its visual was told it moved from the panel's canvas to that one"),
		Visual->CanvasChanges.Contains(TPair<const UDreamCanvas*, const UDreamCanvas*>(PanelCanvas, OwnCanvas)));
	TestTrue(TEXT("and was marked to be written whole into it"), Visual->MarkAllDirtyCount > 0);

	Visual->CanvasChanges.Reset();
	Visual->MarkAllDirtyCount = 0;
	Probe->RemoveComponent(OwnCanvas);
	DrawFrames(Level.World, 1);
	TestTrue(TEXT("without its own canvas the widget draws in the panel's again"), Probe->GetRenderCanvas() == PanelCanvas);
	TestTrue(TEXT("its visual was told it moved back"),
		Visual->CanvasChanges.Contains(TPair<const UDreamCanvas*, const UDreamCanvas*>(OwnCanvas, PanelCanvas)));
	TestTrue(TEXT("and was marked to be written whole into the panel's canvas again"), Visual->MarkAllDirtyCount > 0);

	Probe->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleCanvasRegisteredMidUpdateTest,
	"DreamGUI.Lifecycle.ACanvasAddedOrRemovedWhileTheManagerUpdatesCanvasesWaitsItsTurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleCanvasRegisteredMidUpdateTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	// Two render-target canvases. The first makes its render target in the middle of the manager's pass
	// over the canvases and says so; whoever listens gives a third widget a canvas and takes the second's
	// away -- one canvas registered and one unregistered while the manager is walking them.
	FScopedWorld Level(EWorldType::Editor);
	AActor* Actor = Level.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("an actor to hang the canvases from"), Actor))return false;
	USceneComponent* Anchor = NewObject<USceneComponent>(Actor, TEXT("Anchor"), RF_Transactional);
	Actor->SetRootComponent(Anchor);
	Actor->AddInstanceComponent(Anchor);
	Anchor->RegisterComponent();

	TArray<UDreamWidget*> Roots;
	auto MakeRoot = [&Roots, &Level, Anchor](const TCHAR* InName) -> UDreamWidget*
	{
		UDreamWidget* Root = UDreamUIBPLibrary::ConstructWidget(Level.World, InName, nullptr);
		if (Root != nullptr)
		{
			UDreamUIBPLibrary::AttachWidgetToSceneComponent(Root, Anchor);
			Roots.Add(Root);
		}
		return Root;
	};
	UDreamWidget* First = MakeRoot(TEXT("First"));
	UDreamWidget* Second = MakeRoot(TEXT("Second"));
	UDreamWidget* Third = MakeRoot(TEXT("Third"));
	UDreamCanvas* FirstCanvas = First != nullptr ? First->AddComponent<UDreamCanvas>() : nullptr;
	UDreamCanvas* SecondCanvas = Second != nullptr ? Second->AddComponent<UDreamCanvas>() : nullptr;
	if (!TestNotNull(TEXT("the first canvas"), FirstCanvas) || !TestNotNull(TEXT("the second canvas"), SecondCanvas) || !TestNotNull(TEXT("a third widget"), Third))
	{
		for (UDreamWidget* Root : Roots)
		{
			Root->DestroyWidget();
		}
		return false;
	}
	FirstCanvas->SetRenderMode(EDreamRenderMode::RenderTarget);
	SecondCanvas->SetRenderMode(EDreamRenderMode::RenderTarget);

	UDreamCanvas* ThirdCanvas = nullptr;
	int32 Broadcasts = 0;
	FirstCanvas->GetRenderTargetChangedEvent().AddLambda([&](UTextureRenderTarget2D*)
	{
		if (Broadcasts++ == 0)
		{
			ThirdCanvas = Third->AddComponent<UDreamCanvas>();
			Second->RemoveComponent(SecondCanvas);
		}
	});
	const TWeakObjectPtr<UDreamCanvas> WeakSecondCanvas(SecondCanvas);
	DrawFrames(Level.World, 2);

	const UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Level.World);
	if (TestNotNull(TEXT("the world has a manager"), Manager))
	{
		TestTrue(TEXT("the listener ran, in the middle of the manager's pass"), Broadcasts > 0);
		TestTrue(TEXT("the canvas it added is registered"), ThirdCanvas != nullptr && Manager->GetAllCanvasArray().Contains(ThirdCanvas));
		TestFalse(TEXT("the canvas it removed is not"), Manager->GetAllCanvasArray().Contains(WeakSecondCanvas));
		TestTrue(TEXT("and the first canvas still is"), Manager->GetAllCanvasArray().Contains(FirstCanvas));
	}
	for (UDreamWidget* Root : Roots)
	{
		Root->DestroyWidget();
	}
	return true;
}

#endif
