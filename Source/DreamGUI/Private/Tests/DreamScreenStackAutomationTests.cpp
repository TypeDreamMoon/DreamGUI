// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"

/*
 * What "this page is not on screen" means.
 *
 * A widget carries two independent axes: Visibility decides rendering, layout and hit testing;
 * bWidgetActive decides the BEHAVIOUR lifecycle -- OnEnable/OnDisable, the manager's tick list, and
 * through the user widget bridge, On Tick and the polled property bindings. The page stack used to
 * move only the first of them, so a page covered by a full-screen page above it went on ticking and
 * evaluating its bindings for as long as it stayed covered, which is neither what a caller means by
 * "hide previous" nor what UMG does with a page taken out of the viewport.
 *
 * These pin both axes, because pinning only the visible one is exactly how this was missed.
 */

namespace DreamScreenStackTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamScreenUISubsystem* GetScreenSubsystem(UWorld* InWorld)
	{
		return IsValid(InWorld) ? InWorld->GetSubsystem<UDreamScreenUISubsystem>() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScreenStackCoveredPageStopsRunningTest,
	"DreamGUI.Screen.APageTheStackCoversStopsRunningAndNotJustDrawing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScreenStackCoveredPageStopsRunningTest::RunTest(const FString& Parameters)
{
	using namespace DreamScreenStackTestLocal;
	FScopedGameWorld TestWorld;
	UDreamScreenUISubsystem* Screen = GetScreenSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("the screen subsystem exists in a game world"), Screen))
	{
		return false;
	}

	UDreamWidget* Bottom = Screen->PushWidgetOfClass(TEXT("Bottom"), UDreamUserWidget::StaticClass(),
		EDreamUIScreenPageCachePolicy::KeepAlive, /*bHidePrevious*/true);
	if (!TestNotNull(TEXT("the first page was pushed"), Bottom))
	{
		return false;
	}
	TestTrue(TEXT("the only page on the stack is active"), Bottom->GetWidgetActive());
	TestEqual(TEXT("...and visible"), Bottom->GetVisibility(), EDreamWidgetVisibility::Visible);

	UDreamWidget* Top = Screen->PushWidgetOfClass(TEXT("Top"), UDreamUserWidget::StaticClass(),
		EDreamUIScreenPageCachePolicy::KeepAlive, /*bHidePrevious*/true);
	if (!TestNotNull(TEXT("the second page was pushed"), Top))
	{
		return false;
	}

	// The point of the whole test: collapsed is not enough.
	TestEqual(TEXT("the covered page is collapsed"), Bottom->GetVisibility(), EDreamWidgetVisibility::Collapsed);
	TestFalse(TEXT("and switched OFF, so its behaviours, tick and bindings stop"), Bottom->GetWidgetActive());
	TestTrue(TEXT("the page on top runs"), Top->GetWidgetActive());
	TestEqual(TEXT("...and draws"), Top->GetVisibility(), EDreamWidgetVisibility::Visible);

	// And comes back when it is uncovered, which is the half that makes the change usable at all.
	Screen->PopUI();
	TestTrue(TEXT("uncovering the page switches it back on"), Bottom->GetWidgetActive());
	TestEqual(TEXT("...and back to visible"), Bottom->GetVisibility(), EDreamWidgetVisibility::Visible);
	Screen->RemoveAllUI();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScreenHiddenPageStopsRunningTest,
	"DreamGUI.Screen.HidingAPageByNameSwitchesItOffToo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScreenHiddenPageStopsRunningTest::RunTest(const FString& Parameters)
{
	using namespace DreamScreenStackTestLocal;
	FScopedGameWorld TestWorld;
	UDreamScreenUISubsystem* Screen = GetScreenSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("the screen subsystem exists in a game world"), Screen))
	{
		return false;
	}

	UDreamWidget* Page = Screen->ShowWidgetOfClass(TEXT("Hud"), UDreamUserWidget::StaticClass());
	if (!TestNotNull(TEXT("the page was created"), Page))
	{
		return false;
	}
	TestTrue(TEXT("a shown page runs"), Page->GetWidgetActive());

	Screen->SetUIVisible(TEXT("Hud"), false);
	TestFalse(TEXT("hiding by name switches the page off"), Page->GetWidgetActive());
	TestFalse(TEXT("and IsUIShowing agrees"), Screen->IsUIShowing(TEXT("Hud")));

	Screen->SetUIVisible(TEXT("Hud"), true);
	TestTrue(TEXT("showing it again switches it back on"), Page->GetWidgetActive());
	TestTrue(TEXT("and IsUIShowing agrees"), Screen->IsUIShowing(TEXT("Hud")));
	Screen->RemoveAllUI();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScreenDeadEntryIsReclaimedTest,
	"DreamGUI.Screen.APageDestroyedBehindTheSubsystemsBackGivesItsNameBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScreenDeadEntryIsReclaimedTest::RunTest(const FString& Parameters)
{
	using namespace DreamScreenStackTestLocal;
	FScopedGameWorld TestWorld;
	UDreamScreenUISubsystem* Screen = GetScreenSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("the screen subsystem exists in a game world"), Screen))
	{
		return false;
	}

	UDreamWidget* Page = Screen->PushWidgetOfClass(TEXT("Doomed"), UDreamUserWidget::StaticClass());
	if (!TestNotNull(TEXT("the page was created"), Page))
	{
		return false;
	}
	TestEqual(TEXT("one name is held"), Screen->GetPageEntryCount(), 1);

	// Somebody else tears the page down -- an owner destroying its own tree, say. The subsystem is
	// told nothing, and used to keep the name and the entry forever: GetUI answers null for it,
	// RefreshStack only ever pruned the Stack, and nothing touched the map.
	Page->DestroyWidget();
	UDreamWidget* Next = Screen->PushWidgetOfClass(TEXT("Next"), UDreamUserWidget::StaticClass());
	TestNotNull(TEXT("the next page was pushed"), Next);

	TestEqual(TEXT("the dead entry was reclaimed, leaving only the live one"), Screen->GetPageEntryCount(), 1);
	TestFalse(TEXT("and the dead name is out of the stack"), Screen->IsUIInStack(TEXT("Doomed")));
	TestTrue(TEXT("while the live one is on it"), Screen->IsUIInStack(TEXT("Next")));
	Screen->RemoveAllUI();
	return true;
}

#endif
