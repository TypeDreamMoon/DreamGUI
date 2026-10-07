// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamListView.h"
#include "Controls/DreamScrollBox.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamWorldSpaceRaycaster.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A SCROLL BOX AND A LIST ON A PANEL IN THE LEVEL.
 *
 * A widget on a world-space panel is pointed at through the player's eye, and UMG's UWidgetInteractionComponent hands
 * what its ray lands on to Slate as an ordinary pointer: SScrollBox::OnMouseWheel scrolls by the wheel's notches whatever
 * produced them, and a row of SListView is selected by a click as it is on the screen. So the scroll box's own claims and
 * the list's are made again here, unchanged, with the pointer coming through a world pointer -- the production world-space
 * raycaster with its ray made from the rig's virtual camera (DreamDriverWorld::AttachWorldPointer). Until now the world
 * pointer's wheel was shown to reach a plain widget, and no list was ever clicked on a panel.
 *
 * The eye is at the origin looking down +X, ninety degrees across a 1280 by 720 viewport; the panel stands 300 cm ahead,
 * face on. Controls on a panel are not under the rig's root, so they are aimed at with FDreamBy::Widget.
 */
namespace DreamWorldPanelControlsTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** One notch toward the user, in the shape the production input actors send a wheel. */
	const FVector2D WheelTowardUser(-1.0, -1.0);
	const FVector2D WheelAwayFromUser(1.0, 1.0);

	struct FWorldStage
	{
		UDreamDriverWorldSpaceRaycaster* Pointer = nullptr;
		UDreamWidget* Panel = nullptr;

		bool IsReady() const { return Pointer != nullptr && Panel != nullptr; }
	};

	/** The world pointer, then a 500 by 400 panel 300 cm ahead of it, facing it: the pointer exists before the panel begins. */
	FWorldStage SetUpStage(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		FWorldStage Stage;
		Stage.Pointer = DreamDriverWorld::AttachWorldPointer(InRig,
			DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, ViewportSize), EDreamWorldPointerSource::Mouse);
		Stage.Panel = DreamDriverWorld::MakeWorldPanel(InRig, TEXT("Panel"), FTransform(FVector(300.0, 0.0, 0.0)), FVector2D(500.0, 400.0));
		InTest.TestNotNull(TEXT("A world pointer was attached to the rig"), Stage.Pointer);
		InTest.TestNotNull(TEXT("A world-space panel was built in front of it"), Stage.Panel);
		return Stage;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldPanelScrollBoxWheelTest,
	"DreamGUI.ScrollBox.OnAWorldPanelEachNotchOfTheWheelScrollsTheBoxANotchAndTurningBackStopsAtTheTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldPanelScrollBoxWheelTest, "DreamGUI.ScrollBox.OnAWorldPanelEachNotchOfTheWheelScrollsTheBoxANotchAndTurningBackStopsAtTheTop", "[Pointer][World]")

/*
 * SScrollBox::OnMouseWheel moves the offset by one notch per wheel delta, clamped to [0, end], and raises OnUserScrolled
 * with the new offset. The world pointer's wheel goes to what its ray is over -- the box's viewport on the panel -- so
 * three notches toward the user scroll three notches, and four back stop at the top, exactly as on the screen
 * (DreamGUI.ScrollBox.ThreeNotchesDownScrollThreeNotchesAndTurningBackStopsAtTheTop).
 */
bool FDreamWorldPanelScrollBoxWheelTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldPanelControlsTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	const FWorldStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	// Twenty rows of 60 in a 240-tall window on the panel: plenty to scroll.
	UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("Box"), Stage.Panel, FVector2D(300.0, 240.0));
	if (!TestTrue(TEXT("The box came up on the panel with a viewport and a content node"),
		Box != nullptr && Box->ViewportNode != nullptr && Box->GetContentNode() != nullptr))
	{
		return false;
	}
	for (int32 RowIndex = 0; RowIndex < 20; ++RowIndex)
	{
		Rig.MakeWidget(FString::Printf(TEXT("Box_Row%02d"), RowIndex), Box->GetContentNode(), FVector2D(300.0, 60.0));
	}
	Box->RefreshContentExtent();
	Rig.PumpFrames(2);
	const float Notch = Box->GetScrollSensitivity() * Box->GetWheelScrollMultiplier();
	const float End = Box->GetScrollOffsetOfEnd();
	if (!TestTrue(FString::Printf(TEXT("There is more than three notches to scroll (end %.1f, notch %.1f)"), End, Notch),
		Notch > 0.0f && End > 3.0f * Notch))
	{
		return false;
	}
	TStrongObjectPtr<UDreamDragInteractionProbe> UserScrolled(NewObject<UDreamDragInteractionProbe>());
	Box->OnUserScrolled.AddDynamic(UserScrolled.Get(), &UDreamDragInteractionProbe::RecordFloat);

	FDreamElementRef Viewport = Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()));
	TestTrue(TEXT("Seen through the world pointer's camera, the box's viewport has a pixel"), Viewport->GetCentrePixel().IsSet());
	for (int32 NotchIndex = 0; NotchIndex < 3; ++NotchIndex)
	{
		TestTrue(TEXT("A notch toward the user over the box on the panel completes"), Viewport->ScrollBy(WheelTowardUser));
	}
	TestNearlyEqual(TEXT("Three notches through the world pointer scrolled three notches' distance"), Box->GetScrollOffset(), 3.0f * Notch, 0.5f);
	TestEqual(TEXT("Each notch was reported as the user scrolling"), UserScrolled->NumFloats(), 3);
	TestNearlyEqual(TEXT("The last report carried the offset the box is at"), UserScrolled->LastFloat(-1.0f), Box->GetScrollOffset(), 0.5f);

	for (int32 NotchIndex = 0; NotchIndex < 4; ++NotchIndex)
	{
		TestTrue(TEXT("A notch away from the user completes"), Viewport->ScrollBy(WheelAwayFromUser));
	}
	TestNearlyEqual(TEXT("Turning back stops at the top"), Box->GetScrollOffset(), 0.0f, 0.5f);
	TestTrue(TEXT("No report ever carried an offset outside the box's range"), UserScrolled->AllFloatsWithin(0.0f, End, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldPanelListClickTest,
	"DreamGUI.ListView.OnAWorldPanelClickingTheThirdRowSelectsItWithOneSelectionChangeAndOneClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldPanelListClickTest, "DreamGUI.ListView.OnAWorldPanelClickingTheThirdRowSelectsItWithOneSelectionChangeAndOneClick", "[Pointer][World]")

/*
 * SObjectTableRow selects its item on the press and reports the click on the release: one selection change and one click
 * for one click. A list on a panel, clicked through the world pointer on its third row, says the same as a list on the
 * screen (DreamGUI.ListView.ClickingTheThirdRowSelectsItWithOneSelectionChangeAndOneClick) -- and the second row, a row's
 * height away along the panel, is not the one chosen.
 */
bool FDreamWorldPanelListClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldPanelControlsTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamListView* List = Rig.MakeControl<UDreamListView>(TEXT("List"), Stage.Panel, FVector2D(300.0, 240.0));
	if (!TestNotNull(TEXT("A list was made on the panel"), List))
	{
		return false;
	}
	// Inline, so a project sheet cannot decide how tall a row is.
	List->SetStyleSource(EDreamUIStyleSource::Inline);
	List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), 40.0f));
	const TArray<UObject*> Items = DreamListsInteraction::MakeItems(12);
	List->SetItemObjects(Items);
	Rig.PumpFrames(2);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	FDreamElementRef ThirdRow = Rig.Driver()->Find(FDreamBy::Widget(List->GetRowWidget(2)));
	if (!TestTrue(TEXT("The third item has a row on the panel"), ThirdRow->Exists())
		|| !TestTrue(TEXT("...that the world pointer's camera sees"), ThirdRow->GetCentrePixel().IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("Clicking the third row through the world pointer completes"), ThirdRow->Click());

	TestEqual(TEXT("The third item is the selection"), List->GetSelectedIndex(), 2);
	const TArray<UObject*> Selected = List->GetSelectedItems();
	if (TestEqual(TEXT("Exactly one item is selected"), Selected.Num(), 1))
	{
		TestSamePtr(TEXT("...and it is the third item's object"), Selected[0], Items[2]);
	}
	if (TestEqual(TEXT("The selection changed once"), Probe->SelectionChanges.Num(), 1))
	{
		TestEqual(TEXT("...to the third item"), Probe->SelectionChanges[0], 2);
	}
	if (TestEqual(TEXT("One click was announced"), Probe->ClickedItems.Num(), 1))
	{
		TestEqual(TEXT("...for the third item"), Probe->ClickedItems[0], 2);
	}
	return true;
}

#endif
