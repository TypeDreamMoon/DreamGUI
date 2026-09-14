// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUIManager.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/DreamUITooltip.h"
#include "Interaction/DreamUIVirtualCursor.h"

/*
 * "The UI keeps working while the game is paused" is a contract this framework states in several
 * places and used to keep in only some of them.
 *
 * UDreamEventSystem's component sets bTickEvenWhenPaused, the screen-space raycaster has a project
 * setting for whether pausing reaches it, and UDreamUIManagerWorldSubsystem overrides
 * IsTickableWhenPaused to true -- so input still arrives at a paused UI and the layout still runs.
 * Four tickable services did not override it, and FTickableGameObject answers false by default: the
 * tooltip's dwell timer, the drag visual's follow, the virtual cursor's stick integration and the
 * action router's hold timer all simply stopped.
 *
 * That is exactly backwards from where those four are used. A pause menu is the single most likely
 * place to read a tooltip, to be driving with a virtual cursor because a gamepad has no pointer, and
 * to meet a hold-to-confirm ("hold to quit"). The symptom was not a crash but a menu that ignores
 * the player: the bubble never appears, the cursor will not move, the ring never fills.
 *
 * IsTickableWhenPaused is a pure function of the class, so the CDO can answer it and the assertion
 * needs no world -- which is the point of pinning it here rather than in a timing test that would
 * need a paused one.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPausedUIServicesTickTest,
	"DreamGUI.Interaction.Pause.EveryInteractionServiceStillTicksWhileTheGameIsPaused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPausedUIServicesTickTest::RunTest(const FString& Parameters)
{
	// The one that already did it, asserted first: it is the reference the other four are being held
	// to, and a test that silently agreed with a framework that had changed its mind would be worse
	// than no test.
	TestTrue(TEXT("the UI manager ticks while paused, as it always has"),
		GetDefault<UDreamUIManagerWorldSubsystem>()->IsTickableWhenPaused());

	TestTrue(TEXT("the tooltip dwell keeps counting, so a pause menu can still explain itself"),
		GetDefault<UDreamUITooltipSubsystem>()->IsTickableWhenPaused());
	TestTrue(TEXT("the drag visual keeps following, so a drag begun before the pause stays under the pointer"),
		GetDefault<UDreamUIDragDropSubsystem>()->IsTickableWhenPaused());
	TestTrue(TEXT("the virtual cursor keeps integrating the stick, which is a gamepad's only pointer"),
		GetDefault<UDreamUIVirtualCursorSubsystem>()->IsTickableWhenPaused());
	TestTrue(TEXT("the action router keeps advancing holds, which is where hold-to-quit lives"),
		GetDefault<UDreamUIActionRouter>()->IsTickableWhenPaused());
	return true;
}

#endif
