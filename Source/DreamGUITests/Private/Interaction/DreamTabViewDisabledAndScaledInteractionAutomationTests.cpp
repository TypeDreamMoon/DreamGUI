// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamTabView.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"
#include "Interaction/UIToggle.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamTabView's strip under a finger with tabs disabled, and on a canvas scaled to half and to double.
 *
 * UMG ships no tab view; the control's header states a browser's rules (a tab opens on a click, the open one does nothing,
 * a disabled tab cannot be clicked), and every tab is a button: disabled, it is off the hit path (FHittestGrid::
 * GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp:241), and a whole disabled strip is too (the path is cut at the
 * first disabled widget). On a scaled layer each tab is hit and focused where it is painted, so a press just inside the seam
 * between two tabs opens the tab on that side of it.
 */
namespace DreamTabViewConfigTestLocal
{
	const FVector2D TabViewSize(600.0, 300.0);

	struct FPlacedTabView
	{
		UDreamTabView* TabView = nullptr;
		TStrongObjectPtr<UDreamPressInteractionListener> Listener;

		bool IsReady() const { return TabView != nullptr && TabView->Tabs.Num() == 3; }

		UDreamWidget* TabAt(int32 InIndex) const
		{
			return TabView->Tabs.IsValidIndex(InIndex) ? TabView->Tabs[InIndex].TabNode.Get() : nullptr;
		}
	};

	/** Video, Audio and Input, the first open, at InPosition (canvas units, Y up), its switches heard. */
	FPlacedTabView PlaceTabView(FDreamDriverRig& InRig, const FVector2D& InPosition)
	{
		FPlacedTabView Placed;
		Placed.Listener = TStrongObjectPtr<UDreamPressInteractionListener>(NewObject<UDreamPressInteractionListener>());
		UDreamTabView* TabView = InRig.MakeControl<UDreamTabView>(TEXT("Settings"), nullptr, TabViewSize, InPosition);
		if (TabView == nullptr)
		{
			return Placed;
		}
		TabView->SetTabLabels({
			FText::AsCultureInvariant(TEXT("Video")),
			FText::AsCultureInvariant(TEXT("Audio")),
			FText::AsCultureInvariant(TEXT("Input")) });
		TabView->SetActiveTabIndex(0);
		TabView->OnTabChanged.AddDynamic(Placed.Listener.Get(), &UDreamPressInteractionListener::HandleTabChanged);
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		InRig.PumpFrames(2);
		Placed.TabView = TabView;
		return Placed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabViewDisabledTapTest,
	"DreamGUI.TabView.ATapOnADisabledTabOrOnATabOfADisabledStripLeavesTheOpenTabWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTabViewDisabledTapTest, "DreamGUI.TabView.ATapOnADisabledTabOrOnATabOfADisabledStripLeavesTheOpenTabWhereItWas", "[Touch][Disabled]")

/*
 * The third tab disabled: a tap on it leaves the first tab open, announces nothing and does not switch the tab on. Then the
 * whole tab view disabled with the third tab enabled again: a tap on the second leaves the first open too. With the tab view
 * enabled again, the same tap opens the second.
 */
bool FDreamTabViewDisabledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPlacedTabView Placed = PlaceTabView(Rig, FVector2D::ZeroVector);
	if (!TestTrue(TEXT("A tab view with three tabs can be made"), Placed.IsReady()))
	{
		return false;
	}
	UDreamTabView* TabView = Placed.TabView;
	TabView->SetTabEnabled(2, false);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Tapping the disabled third tab completes"), Rig.Driver()->Find(FDreamBy::Widget(Placed.TabAt(2)))->Tap());
	TestEqual(TEXT("The first tab is still the open one"), TabView->GetActiveTabIndex(), 0);
	TestEqual(TEXT("...and no switch was announced"), Placed.Listener->TabChangedIndices.Num(), 0);
	if (UUIToggle* ThirdToggle = TabView->Tabs[2].Toggle.Get())
	{
		TestFalse(TEXT("...and the tab did not switch itself on"), ThirdToggle->GetValue());
	}

	TabView->SetTabEnabled(2, true);
	TabView->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FDreamElementRef SecondTab = Rig.Driver()->Find(FDreamBy::Widget(Placed.TabAt(1)));
	TestTrue(TEXT("Tapping the second tab of the disabled strip completes"), SecondTab->Tap());
	TestEqual(TEXT("A disabled strip leaves the first tab open"), TabView->GetActiveTabIndex(), 0);
	TestEqual(TEXT("...and announces nothing"), Placed.Listener->TabChangedIndices.Num(), 0);

	TabView->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Tapping the second tab with the strip enabled again completes"), SecondTab->Tap());
	TestEqual(TEXT("Enabled again, the tap opens the second tab"), TabView->GetActiveTabIndex(), 1);
	TestEqual(TEXT("...said once"), Placed.Listener->TabChangedIndices.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabViewScaledHitTest,
	"DreamGUI.TabView.OnACanvasScaledToHalfAndToDoubleAClickOrATapJustInsideTheSeamBetweenTwoTabsOpensTheTabOnThatSide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTabViewScaledHitTest, "DreamGUI.TabView.OnACanvasScaledToHalfAndToDoubleAClickOrATapJustInsideTheSeamBetweenTwoTabsOpensTheTabOnThatSide", "[Pointer][Touch][Scaled]")

/*
 * The strip off the middle, high on the screen, at each scale. A click three pixels inside the third tab's left edge, as
 * drawn, opens the third; a click three pixels inside the second tab's right edge opens the second. A tap three pixels inside
 * the third tab's left edge opens the third again, and a tap three pixels inside the first tab's right edge opens the first.
 */
bool FDreamTabViewScaledHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FPlacedTabView Placed = PlaceTabView(Rig, FVector2D(-20.0, 30.0));
		if (!TestTrue(At(Scale, TEXT("A tab view with three tabs can be made")), Placed.IsReady()))
		{
			return false;
		}
		UDreamTabView* TabView = Placed.TabView;
		const TOptional<FBox2D> First = DrawnRect(Placed.TabAt(0));
		const TOptional<FBox2D> Second = DrawnRect(Placed.TabAt(1));
		const TOptional<FBox2D> Third = DrawnRect(Placed.TabAt(2));
		if (!TestTrue(At(Scale, TEXT("The three tabs are on the viewport")), First.IsSet() && Second.IsSet() && Third.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, FString::Printf(TEXT("A tab is drawn at the canvas's scale (%s)"), *Describe(Second.GetValue()))),
			IsDrawnAtScale(Second.GetValue(), Placed.TabAt(1)->GetSize(), Scale));
		TestTrue(At(Scale, TEXT("...the tabs left to right, without overlapping")),
			First->Max.X <= Second->Min.X + 0.5 && Second->Max.X <= Third->Min.X + 0.5);

		TestTrue(At(Scale, TEXT("A click just inside the third tab's left edge completes")),
			ClickAt(Rig, FVector2D(Third->Min.X + 3.0, Third->GetCenter().Y)));
		TestEqual(*At(Scale, TEXT("It opens the third tab")), TabView->GetActiveTabIndex(), 2);
		TestTrue(At(Scale, TEXT("A click just inside the second tab's right edge completes")),
			ClickAt(Rig, FVector2D(Second->Max.X - 3.0, Second->GetCenter().Y)));
		TestEqual(*At(Scale, TEXT("It opens the second tab")), TabView->GetActiveTabIndex(), 1);

		TestTrue(At(Scale, TEXT("A tap just inside the third tab's left edge completes")),
			TapAt(Rig, FVector2D(Third->Min.X + 3.0, Third->GetCenter().Y)));
		TestEqual(*At(Scale, TEXT("It opens the third tab")), TabView->GetActiveTabIndex(), 2);
		TestTrue(At(Scale, TEXT("A tap just inside the first tab's right edge completes")),
			TapAt(Rig, FVector2D(First->Max.X - 3.0, First->GetCenter().Y)));
		TestEqual(*At(Scale, TEXT("It opens the first tab")), TabView->GetActiveTabIndex(), 0);
		TestEqual(*At(Scale, TEXT("Four switches were announced")), Placed.Listener->TabChangedIndices.Num(), 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabViewScaledPadTest,
	"DreamGUI.TabView.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheNextTabWithTheRingOnItAndAcceptOpensIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTabViewScaledPadTest, "DreamGUI.TabView.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheNextTabWithTheRingOnItAndAcceptOpensIt", "[Nav][Scaled]")

/*
 * The focus on the open first tab; D-pad right takes it to the second tab, the ring sits on the second tab where it is
 * drawn, moving the focus opened nothing, and the pad's Accept opens the second tab.
 */
bool FDreamTabViewScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabViewConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FPlacedTabView Placed = PlaceTabView(Rig, FVector2D(-20.0, 30.0));
		if (!TestTrue(At(Scale, TEXT("A tab view with three tabs can be made")), Placed.IsReady())
			|| !TestTrue(At(Scale, TEXT("The focus goes onto the first tab")), PutFocusOn(Rig, Placed.TabAt(0))))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("D-pad right completes")), PressKey(Rig, EKeys::Gamepad_DPad_Right));
		TestTrue(At(Scale, TEXT("The D-pad took the focus to the second tab")), FocusIsIn(Rig, Placed.TabAt(1)));
		TestEqual(*At(Scale, TEXT("Moving the focus opened nothing")), Placed.TabView->GetActiveTabIndex(), 0);
		FString Where;
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the second tab where it is drawn (%s)"), *Where)), bRingSits);

		TestTrue(At(Scale, TEXT("The pad's Accept completes")), PadConfirm(Rig));
		TestEqual(*At(Scale, TEXT("Accept opened the second tab")), Placed.TabView->GetActiveTabIndex(), 1);
		TestEqual(*At(Scale, TEXT("...said once")), Placed.Listener->TabChangedIndices.Num(), 1);
	}
	return true;
}

#endif
