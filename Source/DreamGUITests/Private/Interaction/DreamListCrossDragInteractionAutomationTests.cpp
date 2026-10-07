// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamListView.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * An inventory: a backpack and a stash side by side, each a list whose rows can be picked up and dropped on, and an item
 * carried from one to the other with the mouse.
 *
 * UMG's list views leave what a drop does to whoever listens: a row's drag starts an operation carrying the item
 * (UListView's OnDragDetected on the entry), and the row it is let go over hears it (OnDrop on that entry, which this
 * library's lists announce as OnItemAcceptDrop with the zone). The row-drag tests drag within one list; these carry a
 * row ACROSS two, which is the inventory's whole gesture: the list it is let go over hears the drop with the other list's
 * item as its payload, the list it came from hears no cancel, and once the inventory has moved the item the stash shows
 * it and answers for it. Let go over neither list, the drag is cancelled, and only the list it came from hears so.
 */
namespace DreamListCrossDragTestLocal
{
	constexpr float RowHeight = 40.0f;
	const FVector2D ListSize(300.0, 400.0);

	/** A list whose rows can be dragged and dropped on, at InPosition, holding InItems. */
	UDreamListView* MakeInventoryList(FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InPosition, const TArray<UObject*>& InItems)
	{
		UDreamListView* List = InRig.MakeControl<UDreamListView>(InName, nullptr, ListSize, InPosition);
		if (List == nullptr)
		{
			return nullptr;
		}
		List->SetStyleSource(EDreamUIStyleSource::Inline);
		List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), RowHeight));
		List->SetItemObjects(InItems);
		List->SetAllowDragging(true);
		List->SetAllowDragDrop(true);
		return List;
	}

	struct FInventory
	{
		UDreamListView* Backpack = nullptr;
		UDreamListView* Stash = nullptr;
		TArray<UObject*> BackpackItems;
		TArray<UObject*> StashItems;

		bool IsReady() const
		{
			return Backpack != nullptr && Stash != nullptr && Backpack->GetNumItems() == BackpackItems.Num() && Stash->GetNumItems() == StashItems.Num();
		}
	};

	FInventory BuildInventory(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamListsInteractionProbe& InBackpackProbe, UDreamListsInteractionProbe& InStashProbe)
	{
		FInventory Inventory;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Inventory;
		}
		const TArray<UObject*> Items = DreamListsInteraction::MakeItems(10);
		Inventory.BackpackItems = TArray<UObject*>(Items.GetData(), 6);
		Inventory.StashItems = TArray<UObject*>(Items.GetData() + 6, 4);
		Inventory.Backpack = MakeInventoryList(InRig, TEXT("Backpack"), FVector2D(-300.0, 0.0), Inventory.BackpackItems);
		Inventory.Stash = MakeInventoryList(InRig, TEXT("Stash"), FVector2D(300.0, 0.0), Inventory.StashItems);
		if (Inventory.Backpack != nullptr && Inventory.Stash != nullptr)
		{
			DreamListsInteraction::ListenToList(*Inventory.Backpack, InBackpackProbe);
			DreamListsInteraction::ListenToList(*Inventory.Stash, InStashProbe);
		}
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(2);
		InTest.TestTrue(TEXT("The backpack and the stash came up with their items"), Inventory.IsReady());
		return Inventory;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListCrossDragDropTest,
	"DreamGUI.ListRowDragDrop.ARowCarriedOntoARowOfAnotherListIsDroppedThereWithItsItemAndTheInventoryCanMoveIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListCrossDragDropTest, "DreamGUI.ListRowDragDrop.ARowCarriedOntoARowOfAnotherListIsDroppedThereWithItsItemAndTheInventoryCanMoveIt", "[Pointer][Animated]")

/*
 * The backpack's second row pressed, carried across and let go on the middle of the stash's third row. The stash hears
 * one drop, on its third row, onto the row itself, carrying the backpack's second item; the backpack heard the drag begin
 * and hears no cancel and no drop. The inventory then moves the item, as a game's drop handler would: out of the
 * backpack, into the stash after the row it landed on -- and a click on the stash's new row chooses that item.
 */
bool FDreamListCrossDragDropTest::RunTest(const FString& Parameters)
{
	using namespace DreamListCrossDragTestLocal;
	TStrongObjectPtr<UDreamListsInteractionProbe> BackpackProbe(NewObject<UDreamListsInteractionProbe>());
	TStrongObjectPtr<UDreamListsInteractionProbe> StashProbe(NewObject<UDreamListsInteractionProbe>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	const FInventory Inventory = BuildInventory(*this, Rig, *BackpackProbe, *StashProbe);
	if (!Inventory.IsReady())
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Carried = Driver->Find(FDreamBy::Widget(Inventory.Backpack->GetRowWidget(1)));
	FDreamElementRef Target = Driver->Find(FDreamBy::Widget(Inventory.Stash->GetRowWidget(2)));
	if (!TestTrue(TEXT("The backpack's second row and the stash's third are there"), Carried->Exists() && Target->Exists()))
	{
		return false;
	}
	UObject* Item = Inventory.BackpackItems[1];

	TestTrue(TEXT("Carrying the row across to the stash completes"), Carried->DragTo(Target));

	if (TestEqual(TEXT("The backpack heard one drag begin"), BackpackProbe->DragDetectedItems.Num(), 1))
	{
		TestEqual(TEXT("...on its second row"), BackpackProbe->DragDetectedItems[0], 1);
	}
	TestEqual(TEXT("...and no cancel: the drag landed"), BackpackProbe->DragCancelledItems.Num(), 0);
	TestEqual(TEXT("...and no drop of its own"), BackpackProbe->DropItems.Num(), 0);
	if (TestEqual(TEXT("The stash heard one drop"), StashProbe->DropItems.Num(), 1))
	{
		TestEqual(TEXT("...on its third row"), StashProbe->DropItems[0], 2);
		TestTrue(TEXT("...onto the row itself"), StashProbe->DropZones[0] == EDreamItemDropZone::OntoItem);
	}
	TestSamePtr(TEXT("What landed in the stash is the backpack's item"), StashProbe->LastDropPayload.Get(), Item);
	TestEqual(TEXT("Neither list changed by itself: the drop is the inventory's to carry out"),
		Inventory.Backpack->GetNumItems() + Inventory.Stash->GetNumItems(), Inventory.BackpackItems.Num() + Inventory.StashItems.Num());

	// What the inventory's drop handler does with it.
	Inventory.Backpack->RemoveItem(Item);
	Inventory.Stash->AddItemAt(Item, 3);
	Rig.PumpFrames(2);
	TestEqual(TEXT("The backpack has one item fewer"), Inventory.Backpack->GetNumItems(), Inventory.BackpackItems.Num() - 1);
	TestEqual(TEXT("...and the stash one more"), Inventory.Stash->GetNumItems(), Inventory.StashItems.Num() + 1);
	TestSamePtr(TEXT("...standing after the row it was dropped on"), Inventory.Stash->GetItemAt(3), Item);
	TestTrue(TEXT("Clicking the stash's new row completes"), Driver->Find(FDreamBy::Widget(Inventory.Stash->GetRowWidget(3)))->Click());
	TestEqual(TEXT("...and chooses the item that was carried there"), Inventory.Stash->GetSelectedIndex(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListCrossDragCancelTest,
	"DreamGUI.ListRowDragDrop.ARowCarriedOverAnotherListAndLetGoOutsideBothIsCancelledAndNeitherListHearsADrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamListCrossDragCancelTest, "DreamGUI.ListRowDragDrop.ARowCarriedOverAnotherListAndLetGoOutsideBothIsCancelledAndNeitherListHearsADrop", "[Pointer][Animated]")

/*
 * The backpack's second row carried over the stash and on past it, and let go where neither list is. Nothing takes the
 * drop, so the drag is cancelled -- the backpack hears that, once -- the stash hears no drop for having been passed over,
 * and both lists hold what they held.
 */
bool FDreamListCrossDragCancelTest::RunTest(const FString& Parameters)
{
	using namespace DreamListCrossDragTestLocal;
	TStrongObjectPtr<UDreamListsInteractionProbe> BackpackProbe(NewObject<UDreamListsInteractionProbe>());
	TStrongObjectPtr<UDreamListsInteractionProbe> StashProbe(NewObject<UDreamListsInteractionProbe>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamListsInteraction::ViewportSize());
	const FInventory Inventory = BuildInventory(*this, Rig, *BackpackProbe, *StashProbe);
	if (!Inventory.IsReady())
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	const TOptional<FVector2D> Over = Driver->Find(FDreamBy::Widget(Inventory.Stash->GetRowWidget(1)))->GetCentrePixel();
	const TOptional<FBox2D> StashRect = Driver->Find(FDreamBy::Widget(Inventory.Stash))->GetPixelRect();
	if (!TestTrue(TEXT("The stash is on screen"), Over.IsSet() && StashRect.IsSet()))
	{
		return false;
	}
	// Past the stash's right edge, where nothing is.
	const FVector2D Beyond(StashRect->Max.X + 60.0, Over->Y);
	// Past the raycaster's drag threshold on the first move, read rather than assumed: the comparison is strictly greater.
	const double PastThreshold = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;

	TestTrue(TEXT("Carrying the row over the stash and beyond it completes"),
		Driver->Sequence()
			.MoveTo(FDreamBy::Widget(Inventory.Backpack->GetRowWidget(1)))
			.Press()
			.MoveBy(FVector2D(PastThreshold, 0.0))
			.MoveToPixel(Over.GetValue())
			.MoveToPixel(Beyond)
			.Release()
			.Perform());

	TestEqual(TEXT("The backpack heard one drag begin"), BackpackProbe->DragDetectedItems.Num(), 1);
	if (TestEqual(TEXT("...and its cancel, once"), BackpackProbe->DragCancelledItems.Num(), 1))
	{
		TestEqual(TEXT("...for its second row"), BackpackProbe->DragCancelledItems[0], 1);
	}
	TestEqual(TEXT("The stash heard no drop for having been passed over"), StashProbe->DropItems.Num(), 0);
	TestEqual(TEXT("...and the backpack none either"), BackpackProbe->DropItems.Num(), 0);
	TestEqual(TEXT("The backpack still holds its items"), Inventory.Backpack->GetNumItems(), Inventory.BackpackItems.Num());
	TestEqual(TEXT("...and the stash its own"), Inventory.Stash->GetNumItems(), Inventory.StashItems.Num());
	return true;
}

#endif
