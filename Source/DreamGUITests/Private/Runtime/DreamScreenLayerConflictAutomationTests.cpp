// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverTypes.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamScreenLayerConflictTest,
	"DreamGUI.Screen.ASharedRootAndOneRootPerPlayerDoNotCompeteButDuplicateRootsInALayerDo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScreenLayerConflictTest::RunTest(const FString& Parameters)
{
	FDreamRigOptions Options;
	Options.PlayerCount = 2;
	Options.PlayerScreens = EDreamRigPlayerScreens::Split;
	Options.InputHost = EDreamRigInputHost::StandaloneActor;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The two-player rig is usable"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig has a UI manager"), Manager))
	{
		return false;
	}
	const auto MakeRoot = [&Rig](int32 InPlayer)
	{
		UDreamWidget* Root = NewObject<UDreamWidget>(Rig.GetWorld(), NAME_None, RF_Transient);
		Root->OnRegister();
		UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
		Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
		Canvas->SetViewportPlayerIndex(InPlayer);
		Root->BeginPlay();
		return Root;
	};
	UDreamWidget* Shared = MakeRoot(INDEX_NONE);
	TestEqual(TEXT("The shared layer and two players each have one active root"), Manager->CountCompetingScreenSpaceOverlayCanvases(), 1);
	Rig.PumpFrames(1); // The runtime diagnostic must also stay silent for this valid arrangement.

	// Inspect conflicts before ticking, then remove them, so the deliberate duplicates do not log automation errors.
	UDreamWidget* DuplicatePlayer = MakeRoot(0);
	TestEqual(TEXT("Two roots for player 0 compete even though player 1 also has its own root"), Manager->CountCompetingScreenSpaceOverlayCanvases(), 2);
	DuplicatePlayer->SetWidgetActive(false);
	TestEqual(TEXT("An inactive duplicate player root does not compete"), Manager->CountCompetingScreenSpaceOverlayCanvases(), 1);
	DuplicatePlayer->DestroyWidget();

	UDreamWidget* DuplicateShared = MakeRoot(INDEX_NONE);
	TestEqual(TEXT("Two shared roots compete independently of the player roots"), Manager->CountCompetingScreenSpaceOverlayCanvases(), 2);
	DuplicateShared->DestroyWidget();
	TestEqual(TEXT("Removing the duplicate leaves one root per layer again"), Manager->CountCompetingScreenSpaceOverlayCanvases(), 1);
	Shared->DestroyWidget();
	return true;
}

#endif
