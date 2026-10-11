// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Interaction/DreamUIVirtualCursor.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"

/*
 * THE GAMEPAD'S CURSOR AS SHIPPED, AND OVER A PANEL IN THE LEVEL.
 *
 * Slate's analog cursor (FAnalogCursor, Slate/Private/Framework/Application/AnalogCursor.cpp) moves the user's one cursor by
 * the stick -- MaxSpeed in desktop pixels a second (UpdateCursorPosition) -- and turns the pad's accept into the left mouse
 * button at the cursor (HandleKeyDownEvent). It is the same cursor the mouse moves, so it starts wherever the mouse left it,
 * and what its press lands on is decided as for any mouse press. A widget component in the level is pressed by the cursor
 * through its interaction exactly as by the mouse: DreamGUI's world pointer makes its ray from the pointer's own screen
 * position, which is the cursor's while the cursor is on (UDreamWorldSpaceRaycaster::GenerateRay), so the stick carries the
 * cursor onto a button on a panel and the confirm clicks it there.
 */
namespace DreamVirtualCursorWorldTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	float CursorSpeed()
	{
		return UDreamGUISettings::Get()->VirtualCursorSpeed;
	}

	/** Player 0's cursor position, or unset while there is no cursor service or the cursor is off. */
	TOptional<FVector2D> CursorPixel(const FDreamDriverRig& InRig)
	{
		const UDreamUIVirtualCursorSubsystem* Cursor = UDreamUIVirtualCursorSubsystem::Get(InRig.GetWorld());
		if (Cursor == nullptr || !Cursor->IsVirtualCursorActiveForUser(0))
		{
			return TOptional<FVector2D>();
		}
		return Cursor->GetVirtualCursorPositionForUser(0);
	}

	/** Two buttons side by side under InParent (the rig's root when null), their clicks heard. */
	struct FTwoButtons
	{
		UDreamButton* Under = nullptr;
		UDreamButton* Beside = nullptr;
	};

	FTwoButtons MakeTwoButtons(FDreamDriverRig& InRig, UDreamWidget* InParent, UDreamPressInteractionListener* InUnderListener,
		UDreamPressInteractionListener* InBesideListener)
	{
		FTwoButtons Buttons;
		Buttons.Under = InRig.MakeControl<UDreamButton>(TEXT("Under"), InParent, FVector2D(140.0, 60.0), FVector2D(-100.0, 50.0));
		Buttons.Beside = InRig.MakeControl<UDreamButton>(TEXT("Beside"), InParent, FVector2D(140.0, 60.0), FVector2D(100.0, 50.0));
		if (Buttons.Under != nullptr && Buttons.Beside != nullptr)
		{
			Buttons.Under->OnClicked.AddDynamic(InUnderListener, &UDreamPressInteractionListener::HandleClicked);
			Buttons.Beside->OnClicked.AddDynamic(InBesideListener, &UDreamPressInteractionListener::HandleClicked);
		}
		return Buttons;
	}

	/**
	 * The mouse rests on one of two buttons side by side, under InParent. The cursor turned on there starts at the mouse's
	 * pixel and its confirm clicks the button under it, not the one beside it.
	 */
	bool RunTurnedOnWhereTheMouseRests(FAutomationTestBase& InTest, bool bInOnPanel)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
		Rig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		UDreamWidget* Parent = nullptr;
		if (bInOnPanel)
		{
			const FWorldStage Stage = SetUpPanel(InTest, Rig);
			if (!Stage.IsReady())
			{
				return false;
			}
			Parent = Stage.Panel;
		}
		// The cursor reads the stick from player 0's controller; made after any world pointer, which asks to come first.
		Rig.EnsureGameInputHost();
		TStrongObjectPtr<UDreamPressInteractionListener> UnderListener(NewObject<UDreamPressInteractionListener>());
		TStrongObjectPtr<UDreamPressInteractionListener> BesideListener(NewObject<UDreamPressInteractionListener>());
		const FTwoButtons Buttons = MakeTwoButtons(Rig, Parent, UnderListener.Get(), BesideListener.Get());
		if (!InTest.TestTrue(TEXT("Two buttons came up"), Buttons.Under != nullptr && Buttons.Beside != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(2);
		FDreamElementRef UnderElement = Rig.Driver()->Find(FDreamBy::Widget(Buttons.Under));
		const TOptional<FVector2D> OnButton = UnderElement->GetCentrePixel();
		if (!InTest.TestTrue(TEXT("The button is somewhere the pointer can reach"), OnButton.IsSet()))
		{
			return false;
		}

		InTest.TestTrue(TEXT("The mouse moving onto the button completes"), UnderElement->Hover());
		InTest.TestTrue(TEXT("The cursor turned on there completes"), Rig.Driver()->Sequence().ActivateVirtualCursor().WaitFrames(1).Perform());
		const TOptional<FVector2D> Cursor = CursorPixel(Rig);
		InTest.TestTrue(TEXT("The cursor is on, at the mouse's pixel"), Cursor.IsSet() && Cursor->Equals(OnButton.GetValue(), 1.0));
		InTest.TestTrue(TEXT("The confirm pressed and let go completes"), Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().WaitFrames(1).Perform());
		InTest.TestEqual(TEXT("The confirm clicked the button under the cursor once"), UnderListener->ClickedCount, 1);
		InTest.TestEqual(TEXT("...and not the one beside it"), BesideListener->ClickedCount, 0);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedVirtualCursorFromMouseTest,
	"DreamGUI.VirtualCursor.TurnedOnWhereTheMouseRestsItStartsThereAndItsConfirmClicksTheButtonUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedVirtualCursorFromMouseTest, "DreamGUI.VirtualCursor.TurnedOnWhereTheMouseRestsItStartsThereAndItsConfirmClicksTheButtonUnderIt", "[Pointer][Animated]")

/* As shipped on the screen: one cursor, the mouse's, so the pad's cursor takes up where the mouse is. */
bool FDreamAnimatedVirtualCursorFromMouseTest::RunTest(const FString& Parameters)
{
	using namespace DreamVirtualCursorWorldTestLocal;
	return RunTurnedOnWhereTheMouseRests(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldVirtualCursorFromMouseTest,
	"DreamGUI.VirtualCursor.TurnedOnWhereTheMouseRestsOnAWorldPanelItsConfirmClicksTheButtonUnderItThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldVirtualCursorFromMouseTest, "DreamGUI.VirtualCursor.TurnedOnWhereTheMouseRestsOnAWorldPanelItsConfirmClicksTheButtonUnderItThere", "[Pointer][World]")

/* The two buttons on a panel: the cursor's pixel is the world pointer's screen position, so its press lands where the mouse's did. */
bool FDreamWorldVirtualCursorFromMouseTest::RunTest(const FString& Parameters)
{
	using namespace DreamVirtualCursorWorldTestLocal;
	return RunTurnedOnWhereTheMouseRests(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldVirtualCursorStickTest,
	"DreamGUI.VirtualCursor.TheStickCarriesTheCursorOntoAButtonOnAWorldPanelAndConfirmClicksIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldVirtualCursorStickTest, "DreamGUI.VirtualCursor.TheStickCarriesTheCursorOntoAButtonOnAWorldPanelAndConfirmClicksIt", "[Nav][World]")

/*
 * A button where the cursor starts on a panel and one to its right. The cursor turned on over the first and the stick pushed
 * right for as long as its speed takes to cover the pixels between them: the cursor is over the second as the camera sees it,
 * and the confirm clicks that one, not the one it left.
 */
bool FDreamWorldVirtualCursorStickTest::RunTest(const FString& Parameters)
{
	using namespace DreamVirtualCursorWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	Rig.EnsureGameInputHost();
	TStrongObjectPtr<UDreamPressInteractionListener> StartListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> TargetListener(NewObject<UDreamPressInteractionListener>());
	// 300 cm apart along the panel, the target wide enough to hold the cursor well either side of where its speed takes it.
	UDreamButton* Start = Rig.MakeControl<UDreamButton>(TEXT("Start"), Stage.Panel, FVector2D(120.0, 60.0), FVector2D(-150.0, 0.0));
	UDreamButton* Target = Rig.MakeControl<UDreamButton>(TEXT("Target"), Stage.Panel, FVector2D(200.0, 80.0), FVector2D(150.0, 0.0));
	if (!TestTrue(TEXT("Two buttons came up on the panel"), Start != nullptr && Target != nullptr))
	{
		return false;
	}
	Start->OnClicked.AddDynamic(StartListener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Target->OnClicked.AddDynamic(TargetListener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Rig.PumpFrames(2);
	const TOptional<FVector2D> OnStart = PixelOf(Rig, Start);
	const TOptional<FVector2D> OnTarget = PixelOf(Rig, Target);
	if (!TestTrue(TEXT("The camera sees both buttons on the panel"), OnStart.IsSet() && OnTarget.IsSet()))
	{
		return false;
	}
	const float Seconds = static_cast<float>((OnTarget->X - OnStart->X) / CursorSpeed());

	TestTrue(TEXT("The cursor turned on over the first button and pushed right completes"), Rig.Driver()->Sequence()
		.MoveToPixel(OnStart.GetValue())
		.ActivateVirtualCursor()
		.VirtualCursorStick(FVector2D(1.0, 0.0), Seconds)
		.WaitFrames(1)
		.Perform());
	const TOptional<FVector2D> Cursor = CursorPixel(Rig);
	const TOptional<FBox2D> TargetRect = RectOf(Rig, Target);
	if (!TestTrue(TEXT("The cursor is on, over the second button as the camera sees it"),
		Cursor.IsSet() && TargetRect.IsSet() && TargetRect->IsInside(Cursor.GetValue())))
	{
		return false;
	}
	TestTrue(TEXT("The confirm pressed and let go completes"), Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().WaitFrames(1).Perform());
	TestEqual(TEXT("The confirm clicked the button on the panel under the cursor once"), TargetListener->ClickedCount, 1);
	TestEqual(TEXT("...and not the one it started on"), StartListener->ClickedCount, 0);
	return true;
}

#endif
