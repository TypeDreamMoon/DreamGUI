// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Driver/DreamDriverRig.h"
#include "DreamBehaviourCallbackTestTypes.h"
#include "Interaction/DreamContentWidget.h"

/*
 * A BEHAVIOUR IS CALLED FOR WHAT ITS CLASS DOES.
 *
 * Every behaviour used to be ticked each frame and told of each move of its widget, whether or not its
 * class had anything to do then. A thousand moving panels paid for both. A class of the plugin whose C++
 * does nothing on either says so in its constructor, and is then neither ticked nor told. What it says
 * covers its own class and no other: a C++ subclass may override either callback, which nothing can ask
 * of it, so it is called as it always was.
 */

namespace DreamBehaviourCallbackTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBehaviourCallbacksDeclaredUnusedTest,
	"DreamGUI.Behaviour.Callbacks.ABehaviourWhoseClassDoesNothingOnTickOrOnAMoveIsNeitherTickedNorToldOfMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBehaviourCallbacksDeclaredUnusedTest::RunTest(const FString& Parameters)
{
	using namespace DreamBehaviourCallbackTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Rig.GetWorld());
	UDreamWidget* Widget = Rig.MakeWidget(TEXT("Holder"), nullptr, FVector2D(100.0, 100.0));
	if (!TestNotNull(TEXT("The rig's world has a UI manager"), Manager) || !TestNotNull(TEXT("A widget can be made on the rig"), Widget))
	{
		return false;
	}
	const bool bListenedBefore = Widget->GetTransformChangedEvent().IsBound();

	UDreamNamedSlot* Slot = Widget->AddComponent<UDreamNamedSlot>();
	if (!TestNotNull(TEXT("A named slot can be added to the widget"), Slot))
	{
		return false;
	}
	Rig.PumpFrames(1);
	TestFalse(TEXT("The slot started, and is not on the tick visit"), Manager->IsBehaviourOnTickVisit(Slot));
	TestEqual(TEXT("Nor does it listen to its widget's moves"), Widget->GetTransformChangedEvent().IsBound(), bListenedBefore);

	// Allowed to tick all the same: the declaration keeps it off the visit, not the switch.
	Slot->SetCanExecuteTick(false);
	Slot->SetCanExecuteTick(true);
	TestFalse(TEXT("Switched off and on again, it is still off the visit"), Manager->IsBehaviourOnTickVisit(Slot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBehaviourCallbacksSubclassTest,
	"DreamGUI.Behaviour.Callbacks.ACppSubclassOfSuchABehaviourThatOverridesThemIsTickedAndToldOfMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBehaviourCallbacksSubclassTest::RunTest(const FString& Parameters)
{
	using namespace DreamBehaviourCallbackTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Rig.GetWorld());
	UDreamWidget* Widget = Rig.MakeWidget(TEXT("Holder"), nullptr, FVector2D(100.0, 100.0));
	if (!TestNotNull(TEXT("The rig's world has a UI manager"), Manager) || !TestNotNull(TEXT("A widget can be made on the rig"), Widget))
	{
		return false;
	}
	UDreamCountingNamedSlot* Slot = Widget->AddComponent<UDreamCountingNamedSlot>();
	if (!TestNotNull(TEXT("The counting slot can be added to the widget"), Slot))
	{
		return false;
	}
	TestTrue(TEXT("It listens to its widget's moves"), Widget->GetTransformChangedEvent().IsBound());

	Rig.PumpFrames(1);
	TestTrue(TEXT("It is on the tick visit"), Manager->IsBehaviourOnTickVisit(Slot));
	TestEqual(TEXT("And ticked in the frame it started"), Slot->TickCount, 1);

	const int32 MovesBefore = Slot->TransformChangedCount;
	Widget->SetRenderRotation(FRotator(0.0, 0.0, 30.0));
	Rig.PumpFrames(1);
	TestEqual(TEXT("A move of its widget reached it once"), Slot->TransformChangedCount, MovesBefore + 1);
	TestEqual(TEXT("And it ticked again"), Slot->TickCount, 2);
	return true;
}

#endif
