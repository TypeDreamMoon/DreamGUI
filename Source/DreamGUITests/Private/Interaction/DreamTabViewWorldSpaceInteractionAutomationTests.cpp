// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamTabView.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamTabView's strip on a panel in the level.
 *
 * UMG ships no tab view; the control's header states a browser's rules (a tab opens on a click, the open one does nothing),
 * and every tab is a button. In a UWidgetComponent a button is hit where the interaction's ray lands on it, so a press just
 * inside the seam between two tabs opens the tab on that side of it, and the pad walks the strip and presses a tab with
 * Accept (SButton::OnKeyDown, Slate/Private/Widgets/Input/SButton.cpp:296) where it stands in the level.
 */
namespace DreamTabViewWorldTestLocal
{
	const FVector2D TabViewSize(480.0, 220.0);

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

	/** Video, Audio and Input on InPanel at InPosition (panel units, Y up), the first open, its switches heard. */
	FPlacedTabView PlaceTabView(FDreamDriverRig& InRig, UDreamWidget* InPanel, const FVector2D& InPosition)
	{
		FPlacedTabView Placed;
		Placed.Listener = TStrongObjectPtr<UDreamPressInteractionListener>(NewObject<UDreamPressInteractionListener>());
		UDreamTabView* TabView = InRig.MakeControl<UDreamTabView>(TEXT("Settings"), InPanel, TabViewSize, InPosition);
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
	FDreamTabViewWorldHitTest,
	"DreamGUI.TabView.OnAWorldPanelAClickOrATapJustInsideTheSeamBetweenTwoTabsOpensTheTabOnThatSide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTabViewWorldHitTest, "DreamGUI.TabView.OnAWorldPanelAClickOrATapJustInsideTheSeamBetweenTwoTabsOpensTheTabOnThatSide", "[Pointer][Touch][World]")

/*
 * Three tabs on a panel three metres ahead, the first open. As the camera sees them: a click three pixels inside the third
 * tab's left edge opens the third, one three pixels inside the second tab's right edge -- six pixels from it, across the seam
 * -- opens the second. A tap just inside the third's left edge opens the third, and one just inside the first's right edge
 * opens the first. Four switches, each announced.
 */
bool FDreamTabViewWorldHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabViewWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	const FPlacedTabView Placed = PlaceTabView(Rig, Stage.Panel, FVector2D(-20.0, 20.0));
	if (!TestTrue(TEXT("A tab view with three tabs can be made on the panel"), Placed.IsReady()))
	{
		return false;
	}
	UDreamTabView* TabView = Placed.TabView;
	const TOptional<FBox2D> First = SeenRect(Rig, Placed.TabAt(0));
	const TOptional<FBox2D> Second = SeenRect(Rig, Placed.TabAt(1));
	const TOptional<FBox2D> Third = SeenRect(Rig, Placed.TabAt(2));
	if (!TestTrue(TEXT("The camera sees the three tabs"), First.IsSet() && Second.IsSet() && Third.IsSet()))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("A tab is seen at its size in panel units (%s)"), *Describe(Second.GetValue())),
		IsSeenAtItsSize(Second.GetValue(), Placed.TabAt(1)->GetSize()));
	TestTrue(TEXT("...the tabs left to right, without overlapping"),
		First->Max.X <= Second->Min.X + 0.5 && Second->Max.X <= Third->Min.X + 0.5);

	TestTrue(TEXT("A click just inside the third tab's left edge completes"), ClickAt(Rig, FVector2D(Third->Min.X + 3.0, Third->GetCenter().Y)));
	TestEqual(TEXT("It opens the third tab"), TabView->GetActiveTabIndex(), 2);
	TestTrue(TEXT("A click just inside the second tab's right edge completes"), ClickAt(Rig, FVector2D(Second->Max.X - 3.0, Second->GetCenter().Y)));
	TestEqual(TEXT("It opens the second tab"), TabView->GetActiveTabIndex(), 1);

	TestTrue(TEXT("A tap just inside the third tab's left edge completes"), TapAt(Rig, FVector2D(Third->Min.X + 3.0, Third->GetCenter().Y)));
	TestEqual(TEXT("It opens the third tab"), TabView->GetActiveTabIndex(), 2);
	TestTrue(TEXT("A tap just inside the first tab's right edge completes"), TapAt(Rig, FVector2D(First->Max.X - 3.0, First->GetCenter().Y)));
	TestEqual(TEXT("It opens the first tab"), TabView->GetActiveTabIndex(), 0);
	TestEqual(TEXT("Four switches were announced"), Placed.Listener->TabChangedIndices.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabViewWorldPadTest,
	"DreamGUI.TabView.OnAWorldPanelTheDPadReachesTheNextTabWithTheRingOnItAsSeenAndAcceptOpensIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTabViewWorldPadTest, "DreamGUI.TabView.OnAWorldPanelTheDPadReachesTheNextTabWithTheRingOnItAsSeenAndAcceptOpensIt", "[Nav][World]")

/*
 * The focus on the open first tab on the panel; D-pad right takes it to the second tab, the ring sits on the second tab where
 * the camera sees it, moving the focus opened nothing, and the pad's Accept opens the second tab.
 */
bool FDreamTabViewWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabViewWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	FFocusDrawnByKeys Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	const FPlacedTabView Placed = PlaceTabView(Rig, Stage.Panel, FVector2D(-20.0, 20.0));
	if (!TestTrue(TEXT("A tab view with three tabs can be made on the panel"), Placed.IsReady())
		|| !TestTrue(TEXT("The focus goes onto the first tab"), PutFocusOn(Rig, Placed.TabAt(0))))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("The D-pad took the focus to the second tab"), FocusIsIn(Rig, Placed.TabAt(1)));
	TestEqual(TEXT("Moving the focus opened nothing"), Placed.TabView->GetActiveTabIndex(), 0);
	FString Where;
	const bool bRingSits = WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the second tab where the camera sees it (%s)"), *Where), bRingSits);

	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("Accept opened the second tab"), Placed.TabView->GetActiveTabIndex(), 1);
	TestEqual(TEXT("...said once"), Placed.Listener->TabChangedIndices.Num(), 1);
	return true;
}

#endif
