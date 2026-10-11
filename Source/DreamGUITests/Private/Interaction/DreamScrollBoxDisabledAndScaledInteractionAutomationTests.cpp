// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamScrollBox.h"
#include "Core/Components/DreamWidget.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * THE SCROLL BOX DISABLED, AND ON A CANVAS SCALED TO HALF AND TO DOUBLE.
 *
 * SScrollBox scrolls under the wheel (OnMouseWheel), under a right-button drag, and under a finger that has travelled the
 * drag distance (OnMouseMove, which a touch reaches: every move scrolls by its own delta over the geometry's scale -- in the
 * box's own units, so the content stays under the finger; Slate/Private/Widgets/Layout/SScrollBox.cpp:1040-1045). Disabled,
 * it is off the hit path (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp:241) -- none of those reach
 * it -- and everything inside it is passed over by navigation (FindFocusableWidget asks every parent whether it is enabled,
 * HittestGrid.cpp:336, :471). On a scaled layer a
 * finger's pan scrolls by its travel in the box's units, and the pad walking down a column of buttons scrolls each one it
 * reaches into the box's window as the window is drawn (SScrollBox::OnNavigation, ScrollDescendantIntoView).
 */
namespace DreamScrollBoxConfigTestLocal
{
	/**
	 * A box of InRowCount plain rows 100 units tall at InPosition (canvas units, Y up), 300 wide and InHeight tall, its UI
	 * begun -- a scroll view's inertia is its Tick.
	 */
	UDreamScrollBox* MakeFilledBox(FDreamDriverRig& InRig, const FVector2D& InPosition, double InHeight, int32 InRowCount)
	{
		if (!DreamDragInteraction::BeginPlayForUI(InRig.GetWorld()))
		{
			return nullptr;
		}
		UDreamScrollBox* Box = InRig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(300.0, InHeight), InPosition);
		if (Box == nullptr || Box->ViewportNode == nullptr || Box->GetContentNode() == nullptr)
		{
			return nullptr;
		}
		for (int32 RowIndex = 0; RowIndex < InRowCount; ++RowIndex)
		{
			InRig.MakeWidget(FString::Printf(TEXT("Box_Row%02d"), RowIndex), Box->GetContentNode(), FVector2D(300.0, 100.0));
		}
		Box->RefreshContentExtent();
		InRig.PumpFrames(2);
		return Box;
	}

	/** Pumped until the box has come to rest, for as long as its own glide takes. */
	bool WaitUntilStill(FDreamDriverRig& InRig, const UDreamScrollBox* InBox)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(5.0);
		return InRig.Driver()->Wait(FDreamUntil::Condition([InBox]() { return !InBox->GetIsScrolling(); }, Timeout),
			Timeout, TEXT("the scroll box comes to rest"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxDisabledPointerTest,
	"DreamGUI.ScrollBox.ADisabledBoxIsScrolledByNoWheelRightButtonDragOrFingerUntilItIsEnabledAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxDisabledPointerTest, "DreamGUI.ScrollBox.ADisabledBoxIsScrolledByNoWheelRightButtonDragOrFingerUntilItIsEnabledAgain", "[Pointer][Touch][Disabled]")

/*
 * Twenty rows in a 300-by-400 window, the box disabled at the top. Three wheel notches over it, a right-button drag up it and
 * a finger's pan up it leave it at the top, and no scrolling is announced. Enabled again, the same three notches scroll it.
 */
bool FDreamScrollBoxDisabledPointerTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamScrollBox* Box = MakeFilledBox(Rig, FVector2D::ZeroVector, 400.0, 20);
	if (!TestNotNull(TEXT("A box with a viewport and twenty rows came up"), Box))
	{
		return false;
	}
	Box->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TStrongObjectPtr<UDreamDragInteractionProbe> UserScrolled(NewObject<UDreamDragInteractionProbe>());
	Box->OnUserScrolled.AddDynamic(UserScrolled.Get(), &UDreamDragInteractionProbe::RecordFloat);
	FDreamElementRef Window = Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()));
	const TOptional<FVector2D> Centre = Window->GetCentrePixel();
	if (!TestTrue(TEXT("The box's window is on the viewport"), Centre.IsSet()))
	{
		return false;
	}
	const FVector2D Grab = Centre.GetValue() + FVector2D(0.0, 120.0);
	const double FirstMove = PastDragThreshold(Rig);

	TestTrue(TEXT("Three wheel notches over the disabled box complete"), Window->ScrollBy(FVector2D(0.0, -3.0)));
	TestTrue(TEXT("A right-button drag up it completes"),
		Rig.Driver()->Sequence()
			.MoveToPixel(Grab)
			.Press(EDreamUIMouseButtonType::Right)
			.MoveBy(FVector2D(0.0, -FirstMove))
			.MoveBy(FVector2D(0.0, -100.0))
			.Release(EDreamUIMouseButtonType::Right)
			.Perform());
	TestTrue(TEXT("A finger's pan up it completes"),
		Rig.Driver()->Sequence()
			.TouchDown(0, Grab)
			.TouchMoveTo(0, Grab - FVector2D(0.0, FirstMove))
			.TouchMoveTo(0, Grab - FVector2D(0.0, FirstMove + 100.0))
			.TouchUp(0)
			.WaitFrames(10)
			.Perform());
	TestNearlyEqual(TEXT("The disabled box is still at the top"), Box->GetScrollOffset(), 0.0f, 0.5f);
	TestEqual(TEXT("...and no scrolling was announced"), UserScrolled->NumFloats(), 0);

	Box->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The same three notches over the box enabled again complete"), Window->ScrollBy(FVector2D(0.0, -3.0)));
	TestTrue(TEXT("...and the box comes to rest"), WaitUntilStill(Rig, Box));
	TestTrue(FString::Printf(TEXT("Enabled again, the wheel scrolled it (offset %.1f)"), Box->GetScrollOffset()), Box->GetScrollOffset() > 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxDisabledPadTest,
	"DreamGUI.ScrollBox.TheDPadPassesOverTheButtonsInADisabledBoxAndOnceEnabledStopsOnTheFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxDisabledPadTest, "DreamGUI.ScrollBox.TheDPadPassesOverTheButtonsInADisabledBoxAndOnceEnabledStopsOnTheFirst", "[Nav][Disabled]")

/*
 * A button above a box of three buttons and a button below it, the box disabled and the focus on the top button. D-pad down
 * passes over everything in the box to the bottom button, and D-pad up back over it. Enabled again, D-pad down from the top
 * button stops on the box's first button.
 */
bool FDreamScrollBoxDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Above = Rig.MakeControl<UDreamButton>(TEXT("Above"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, 260.0));
	UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(300.0, 300.0));
	UDreamButton* Below = Rig.MakeControl<UDreamButton>(TEXT("Below"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, -260.0));
	if (!TestTrue(TEXT("A button, a box and a button came up"), Above != nullptr && Below != nullptr && Box != nullptr
		&& Above->FaceNode != nullptr && Below->FaceNode != nullptr && Box->GetContentNode() != nullptr))
	{
		return false;
	}
	TArray<UDreamButton*> Inside;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Inside.Add(Rig.MakeControl<UDreamButton>(FString::Printf(TEXT("Inside%d"), Index), Box->GetContentNode(), FVector2D(280.0, 80.0)));
	}
	if (!TestFalse(TEXT("Three buttons were put in the box"), Inside.Contains(nullptr)))
	{
		return false;
	}
	Box->RefreshContentExtent();
	Box->SetIsEnabled(false);
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The focus goes onto the top button"), PutFocusOn(Rig, Above->FaceNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad down completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
	TestTrue(TEXT("D-pad down passes over everything in the disabled box to the bottom button"), FocusIsIn(Rig, Below));
	TestTrue(TEXT("D-pad up completes"), PressKey(Rig, EKeys::Gamepad_DPad_Up));
	TestTrue(TEXT("D-pad up passes back over it to the top button"), FocusIsIn(Rig, Above));

	Box->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("D-pad down completes once the box is enabled"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
	TestTrue(TEXT("Enabled again, the box's first button is the next stop"), FocusIsIn(Rig, Inside[0]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxScaledFingerTest,
	"DreamGUI.ScrollBox.OnACanvasScaledToHalfAndToDoubleAFingersPanScrollsByItsTravelInTheCanvassUnits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxScaledFingerTest, "DreamGUI.ScrollBox.OnACanvasScaledToHalfAndToDoubleAFingersPanScrollsByItsTravelInTheCanvassUnits", "[Touch][Scaled]")

/*
 * A box 320 pixels tall at either scale -- 640 units at half, 160 at double. A finger lands near its bottom, crosses the drag
 * distance (in canvas units, so that many more pixels at double) and goes on 120 pixels up. While it is still down the content
 * has followed it by those 120 pixels in the box's own units: 240 at half scale, 60 at double -- give or take the stretch that
 * only decided this was a pan. A box that scrolled in pixels would be out by a factor of two either way.
 */
bool FDreamScrollBoxScaledFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamScrollBox* Box = MakeFilledBox(Rig, FVector2D(-150.0, 0.0), 320.0 / Scale, 20);
		if (!TestNotNull(*At(Scale, TEXT("A box with a viewport and twenty rows came up")), Box))
		{
			return false;
		}
		const TOptional<FBox2D> Window = DrawnRect(Box->ViewportNode.Get());
		if (!TestTrue(At(Scale, TEXT("The box's window is on the viewport")), Window.IsSet()))
		{
			return false;
		}
		TestNearlyEqual(*At(Scale, TEXT("The window is drawn 320 pixels tall")), Window->Max.Y - Window->Min.Y,
			static_cast<double>(Box->ViewportNode->GetHeight()) * Scale, 1.5);
		TStrongObjectPtr<UDreamDragInteractionProbe> UserScrolled(NewObject<UDreamDragInteractionProbe>());
		Box->OnUserScrolled.AddDynamic(UserScrolled.Get(), &UDreamDragInteractionProbe::RecordFloat);
		const double FirstMove = PastDragThreshold(Rig);
		const double Travel = 120.0;
		const FVector2D Landed(Window->GetCenter().X, Window->Max.Y - 30.0);

		TestTrue(At(Scale, TEXT("The finger's pan completes")),
			Rig.Driver()->Sequence()
				.TouchDown(0, Landed)
				.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove))
				.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove + Travel * 0.5))
				.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove + Travel))
				.Perform());
		const double WhileHeld = Box->GetScrollOffset();
		const double Least = (Travel - 6.0) / Scale;
		const double Most = (FirstMove + Travel + 2.0) / Scale;
		TestTrue(At(Scale, FString::Printf(TEXT("With the finger still down the content followed it by its travel in the box's units: between %.1f and %.1f (it is %.1f)"),
			Least, Most, WhileHeld)), WhileHeld >= Least && WhileHeld <= Most);
		TestTrue(At(Scale, TEXT("...and the user's scrolling was announced")), UserScrolled->NumFloats() >= 1);
		TestTrue(At(Scale, TEXT("The finger lifts")), Rig.Driver()->Sequence().TouchUp(0).Perform());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxScaledPadTest,
	"DreamGUI.ScrollBox.OnACanvasScaledToHalfAndToDoubleTheDPadScrollsEachButtonItReachesIntoTheDrawnWindowWithTheRingOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxScaledPadTest, "DreamGUI.ScrollBox.OnACanvasScaledToHalfAndToDoubleTheDPadScrollsEachButtonItReachesIntoTheDrawnWindowWithTheRingOnIt", "[Nav][Scaled]")

/*
 * Thirty buttons in a box 300 pixels tall at either scale, the focus on the first. The box's stack gives each button its
 * desired height, as SScrollBox's slots do, so how many the window shows at a scale is the layout's to say: the D-pad goes
 * down until it has reached two buttons past the first one drawn below the window. After every press the focus is on the
 * next button and that button lies inside the box's window as the window is drawn, the box having scrolled to show it once
 * it was below the window; and the ring sits on the last one where it is drawn.
 */
bool FDreamScrollBoxScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale)
			|| !TestTrue(At(Scale, TEXT("Its UI has begun play, as a game's has")), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
		{
			return false;
		}
		UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(320.0, 300.0 / Scale), FVector2D(100.0, 0.0));
		if (!TestTrue(At(Scale, TEXT("A box with a viewport came up")), Box != nullptr && Box->ViewportNode != nullptr && Box->GetContentNode() != nullptr))
		{
			return false;
		}
		TArray<UDreamButton*> Buttons;
		for (int32 Index = 0; Index < 30; ++Index)
		{
			Buttons.Add(Rig.MakeControl<UDreamButton>(FString::Printf(TEXT("Row%02d"), Index), Box->GetContentNode(), FVector2D(300.0, 80.0)));
		}
		if (!TestFalse(At(Scale, TEXT("Thirty buttons were put in the box")), Buttons.Contains(nullptr)))
		{
			return false;
		}
		Box->RefreshContentExtent();
		Rig.PumpFrames(2);
		const TOptional<FBox2D> Window = DrawnRect(Box->ViewportNode.Get());
		if (!TestTrue(At(Scale, TEXT("The box's window is on the viewport")), Window.IsSet())
			|| !TestTrue(At(Scale, TEXT("The focus goes onto the first button")), PutFocusOn(Rig, Buttons[0]->FaceNode.Get())))
		{
			return false;
		}

		// The first button the window does not show all of, as the box laid the column out.
		int32 FirstBelow = INDEX_NONE;
		for (int32 Index = 0; Index < Buttons.Num() && FirstBelow == INDEX_NONE; ++Index)
		{
			const TOptional<FBox2D> Row = DrawnRect(Buttons[Index]);
			if (Row.IsSet() && Row->Max.Y > Window->Max.Y + 1.5)
			{
				FirstBelow = Index;
			}
		}
		const TOptional<FBox2D> FirstRow = DrawnRect(Buttons[0]);
		AddInfo(At(Scale, FString::Printf(TEXT("The window is drawn at %s, the first button at %s; the first button drawn below the window is %d"),
			*Describe(Window.GetValue()), FirstRow.IsSet() ? *Describe(FirstRow.GetValue()) : TEXT("nowhere"), FirstBelow)));
		if (!TestTrue(At(Scale, TEXT("The column runs on below the window")), FirstBelow > 0 && FirstBelow + 2 < Buttons.Num()))
		{
			return false;
		}

		const int32 Presses = FirstBelow + 2;
		for (int32 Press = 1; Press <= Presses; ++Press)
		{
			TestTrue(At(Scale, FString::Printf(TEXT("D-pad down %d completes"), Press)), PressKey(Rig, EKeys::Gamepad_DPad_Down));
			// The scroll into view may glide; the focused button is judged once the box has come to rest.
			WaitUntilStill(Rig, Box);
			const TOptional<FBox2D> Row = DrawnRect(Buttons[Press]);
			const bool bFocused = FocusIsIn(Rig, Buttons[Press]);
			const bool bInside = Row.IsSet() && Row->Min.Y >= Window->Min.Y - 1.5 && Row->Max.Y <= Window->Max.Y + 1.5;
			if (!TestTrue(At(Scale, FString::Printf(TEXT("After D-pad down %d the focus is on button %d, drawn inside the window %s (it is at %s)"),
				Press, Press, *Describe(Window.GetValue()), Row.IsSet() ? *Describe(Row.GetValue()) : TEXT("nowhere"))), bFocused && bInside))
			{
				return false;
			}
		}
		TestTrue(At(Scale, FString::Printf(TEXT("The box scrolled to show the buttons the pad went to (offset %.1f)"), Box->GetScrollOffset())),
			Box->GetScrollOffset() > 1.0f);
		FString Where;
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the last button where it is drawn (%s)"), *Where)), bRingSits);
	}
	return true;
}

#endif
