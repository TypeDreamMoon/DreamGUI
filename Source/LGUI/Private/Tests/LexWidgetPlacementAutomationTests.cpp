// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexWidgetPlacement.h"
#include "Engine/World.h"

/*
 * Lifting a widget out and putting it back.
 *
 * The whole reason this needs a type of its own is that detaching a widget DESTROYS its panel slot,
 * so re-attaching produces a fresh default one. Restoring the parent and the sibling index -- the
 * obvious subset, and what a first attempt writes -- leaves padding, alignment, fill weight, grid
 * placement and auto-size silently reset. That reads to an author as "the layout broke itself when I
 * dragged something", and nothing in the parenting API hints that it can happen.
 *
 * So these tests are mostly about the un-obvious half. A test that only checked the parent would
 * pass against exactly the implementation this exists to prevent.
 */

namespace LexWidgetPlacementTestLocal
{
	struct FScopedWorld
	{
		UWorld* World = nullptr;
		FScopedWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeWidget(UWorld* World, ULexWidget* Parent, const TCHAR* Name)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(100.0f);
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexWidgetPlacementRoundTripTest,
	"LGUI.Widget.Placement.ASlotSurvivesBeingLiftedOutAndPutBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexWidgetPlacementRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace LexWidgetPlacementTestLocal;
	FScopedWorld TestWorld;
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	ULexWidget* Hand = MakeWidget(TestWorld.World, Root, TEXT("Hand"));
	Hand->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	ULexWidget* DragLayer = MakeWidget(TestWorld.World, Root, TEXT("DragLayer"));

	ULexWidget* Filler = MakeWidget(TestWorld.World, Hand, TEXT("Filler"));
	ULexWidget* Card = MakeWidget(TestWorld.World, Hand, TEXT("Card"));
	ULexWidget* Trailing = MakeWidget(TestWorld.World, Hand, TEXT("Trailing"));

	// Author a slot that is nothing like a default one, so a restore that only handles the parent
	// and the ZOrder cannot pass by accident.
	ULexPanelSlot* Slot = Card->GetPanelSlot();
	if (!TestNotNull(TEXT("The card has a panel slot"), Slot))return false;
	Slot->SetPadding(FMargin(3.0f, 7.0f, 11.0f, 13.0f));
	Slot->SetHorizontalAlignment(ELexPanelHorizontalAlignment::Right);
	Slot->SetVerticalAlignment(ELexPanelVerticalAlignment::Top);
	Slot->SetSizeRule(ELexPanelSizeRule::Fill);
	Slot->SetFillWeight(2.75f);
	Slot->SetRow(2); Slot->SetColumn(3); Slot->SetRowSpan(4); Slot->SetColumnSpan(5);
	Slot->SetAutoSize(true);
	Slot->SetZOrder(17);
	Card->SetPivot(FVector2D(0.25, 0.75));
	Card->SetAnchorMin(FVector2D(0.1, 0.2));
	Card->SetAnchorMax(FVector2D(0.3, 0.4));
	Card->SetIgnoreLayout(true);
	const int32 OriginalSiblingIndex = Card->GetSiblingIndex();

	FLexWidgetPlacement Placement;
	Placement.Capture(Card);
	TestTrue(TEXT("A capture with a parent is valid"), Placement.IsValid());

	// Lift it out, the way a drag does.
	if (!TestTrue(TEXT("The card can be lifted to the drag layer"), Card->TrySetParent(DragLayer, true)))return false;
	TestEqual(TEXT("It really left"), Card->GetParent(), DragLayer);

	if (!TestTrue(TEXT("It can be put back"), Placement.Restore(Card)))return false;

	TestEqual(TEXT("Parent restored"), Card->GetParent(), Hand);
	TestEqual(TEXT("Sibling index restored"), Card->GetSiblingIndex(), OriginalSiblingIndex);
	TestEqual(TEXT("Pivot restored"), Card->GetPivot(), FVector2D(0.25, 0.75));
	TestEqual(TEXT("AnchorMin restored"), Card->GetAnchorMin(), FVector2D(0.1, 0.2));
	TestEqual(TEXT("AnchorMax restored"), Card->GetAnchorMax(), FVector2D(0.3, 0.4));
	TestTrue(TEXT("IgnoreLayout restored"), Card->GetIgnoreLayout());

	// The half that a parent-and-ZOrder restore silently loses.
	ULexPanelSlot* Restored = Card->GetPanelSlot();
	if (!TestNotNull(TEXT("The card has a slot again"), Restored))return false;
	TestEqual(TEXT("Padding restored"), Restored->Padding, FMargin(3.0f, 7.0f, 11.0f, 13.0f));
	TestEqual(TEXT("Horizontal alignment restored"), Restored->HorizontalAlignment, ELexPanelHorizontalAlignment::Right);
	TestEqual(TEXT("Vertical alignment restored"), Restored->VerticalAlignment, ELexPanelVerticalAlignment::Top);
	TestEqual(TEXT("Size rule restored"), Restored->SizeRule, ELexPanelSizeRule::Fill);
	TestEqual(TEXT("Fill weight restored"), Restored->FillWeight, 2.75f);
	TestEqual(TEXT("Row restored"), Restored->Row, 2);
	TestEqual(TEXT("Column restored"), Restored->Column, 3);
	TestEqual(TEXT("Row span restored"), Restored->RowSpan, 4);
	TestEqual(TEXT("Column span restored"), Restored->ColumnSpan, 5);
	TestTrue(TEXT("Auto size restored"), Restored->bAutoSize);
	TestEqual(TEXT("ZOrder restored"), Restored->ZOrder, 17);

	// And the slot really was a new object, so the values above were written rather than survived --
	// otherwise this whole type would be unnecessary and the test would be proving nothing.
	TestNotEqual(TEXT("The slot is genuinely a new one"), (void*)Restored, (void*)Slot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexWidgetPlacementRefusalTest,
	"LGUI.Widget.Placement.ARestoreWithNowhereToGoRefuses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexWidgetPlacementRefusalTest::RunTest(const FString& Parameters)
{
	using namespace LexWidgetPlacementTestLocal;
	FScopedWorld TestWorld;
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	ULexWidget* Owner = MakeWidget(TestWorld.World, Root, TEXT("Owner"));
	ULexWidget* DragLayer = MakeWidget(TestWorld.World, Root, TEXT("DragLayer"));
	ULexWidget* Card = MakeWidget(TestWorld.World, Owner, TEXT("Card"));

	FLexWidgetPlacement Empty;
	TestFalse(TEXT("An uncaptured placement is not valid"), Empty.IsValid());
	TestFalse(TEXT("An uncaptured placement refuses to restore"), Empty.Restore(Card));

	// A widget with no parent -- a root, or one already lifted out -- has no placement to record, and
	// restoring one must not attach it to nothing.
	FLexWidgetPlacement FromRoot;
	FromRoot.Capture(Root);
	TestFalse(TEXT("Capturing a parentless widget yields no placement"), FromRoot.IsValid());
	TestFalse(TEXT("Restoring it refuses"), FromRoot.Restore(Root));

	FLexWidgetPlacement Placement;
	Placement.Capture(Card);
	Card->TrySetParent(DragLayer, true);
	Placement.Reset();
	TestFalse(TEXT("A reset placement is not valid"), Placement.IsValid());
	TestFalse(TEXT("A reset placement refuses to restore"), Placement.Restore(Card));

	// NOT asserted here: the original parent being garbage-collected while the widget is lifted out.
	// Restore guards it -- the parent is held weakly, so the pointer stops resolving -- but making it
	// happen in a test needs a forced GC, and this fixture holds raw pointers to the very widgets a
	// collection would be free to take. Verifying it would risk the stability of everything else in
	// the suite to confirm one branch, which is a bad trade. DestroyWidget alone is not enough: it
	// detaches and tears down but leaves the UObject resolvable until a collection actually runs.
	TestEqual(TEXT("The widget is left where it is rather than orphaned"), Card->GetParent(), DragLayer);
	return true;
}

#endif
