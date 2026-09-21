// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamWidgetPlacement.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"

/*
 * The slot side of the UMG surface: the canvas rect family, the nudge, the forced line break, and the
 * UMG names that forward rather than storing a second copy of something.
 *
 * The canvas family is the interesting one. UMG keeps a whole FAnchorData on the slot; here the same
 * statement already lives on the widget, so every one of those functions has to be a rename of what the
 * widget owns. If any of them kept its own field the details panel and Blueprint would describe the same
 * widget differently, and nothing would say which was right -- so the assertions below all read back
 * through the WIDGET, never through the slot that was written.
 */

namespace DreamPanelSlotParityTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasSlotRectFamilyWritesTheWidgetsOwnAnchorDataTest,
	"DreamGUI.PanelSlot.TheCanvasRectFamilyReadsAndWritesTheWidgetsOwnAnchorData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasSlotRectFamilyWritesTheWidgetsOwnAnchorDataTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelSlotParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 100.0f, 50.0f);
	if (!TestNotNull(TEXT("Canvas panel created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerCanvasPanel>()))
	{
		return false;
	}
	UDreamPanelSlot* Slot = Child->GetPanelSlot();
	if (!TestNotNull(TEXT("The canvas handed the child a slot"), Slot))
	{
		return false;
	}

	Slot->SetPosition(FVector2D(10.0, -20.0));
	TestEqual(TEXT("Position is the widget's anchored position, y up"), Child->GetAnchoredPosition(), FVector2D(10.0, -20.0));
	TestEqual(TEXT("and reads back through the slot unchanged"), Slot->GetPosition(), FVector2D(10.0, -20.0));

	Slot->SetSize(FVector2D(64.0, 32.0));
	TestEqual(TEXT("Size is the widget's size delta"), Child->GetSizeDelta(), FVector2D(64.0, 32.0));
	TestTrue(TEXT("and on a point anchor that is the resolved width"), FMath::IsNearlyEqual(Child->GetWidth(), 64.0f, 0.01f));

	Slot->SetAlignment(FVector2D(0.0, 1.0));
	TestEqual(TEXT("Alignment is the pivot: same quantity, same range"), Child->GetPivot(), FVector2D(0.0, 1.0));

	Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	TestEqual(TEXT("Anchors minimum reaches AnchorMin"), Child->GetAnchorMin(), FVector2D(0.0, 0.0));
	TestEqual(TEXT("Anchors maximum reaches AnchorMax"), Child->GetAnchorMax(), FVector2D(1.0, 1.0));

	// Offsets are the four edge insets, and on a stretched child they are what decides the resolved size.
	Slot->SetOffsets(FMargin(8.0f, 6.0f, 4.0f, 2.0f));
	const FMargin ReadBack = Slot->GetOffsets();
	TestTrue(TEXT("Offsets round-trip through the widget's anchor offsets"),
		FMath::IsNearlyEqual(ReadBack.Left, 8.0f, 0.01f) && FMath::IsNearlyEqual(ReadBack.Top, 6.0f, 0.01f)
		&& FMath::IsNearlyEqual(ReadBack.Right, 4.0f, 0.01f) && FMath::IsNearlyEqual(ReadBack.Bottom, 2.0f, 0.01f));
	TestTrue(TEXT("A stretched child resolves to the parent span less its insets"),
		FMath::IsNearlyEqual(Child->GetWidth(), 288.0f, 0.01f)
		&& FMath::IsNearlyEqual(Child->GetHeight(), 192.0f, 0.01f));

	// The whole rect in one statement, and the read that follows it comes off the widget.
	FDreamUIAnchorData Layout = Slot->GetLayout();
	Layout.AnchoredPosition = FVector2D(3.0, 4.0);
	Slot->SetLayout(Layout);
	TestEqual(TEXT("SetLayout writes through to the widget's anchor data"),
		Child->GetAnchorData().AnchoredPosition, FVector2D(3.0, 4.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlotNudgeMovesAChildWithoutResizingThePanelTest,
	"DreamGUI.PanelSlot.ANudgeMovesAChildWithoutMovingThePanelAroundIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSlotNudgeMovesAChildWithoutResizingThePanelTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelSlotParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* First = MakeWidget(TestWorld.World, Root, TEXT("First"), 80.0f, 30.0f);
	UDreamWidget* Second = MakeWidget(TestWorld.World, Root, TEXT("Second"), 80.0f, 30.0f);
	UDreamLayoutContainerVerticalBox* Box = Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
	if (!TestNotNull(TEXT("Vertical box created"), Box))
	{
		return false;
	}
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);

	const FVector2D FirstBefore = First->GetAnchoredPosition();
	const FVector2D SecondBefore = Second->GetAnchoredPosition();
	const FVector2f PreferredBefore = Box->GetLayoutPreferredSize();

	UDreamPanelSlot* FirstSlot = First->GetPanelSlot();
	if (!TestNotNull(TEXT("The box handed the first child a slot"), FirstSlot))
	{
		return false;
	}
	FirstSlot->SetNudge(FVector2D(12.0, 7.0));
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);

	// The nudge is in the panel's content space: x right, y DOWN, which is downward in a y-up local space.
	TestTrue(TEXT("The nudged child moved by exactly the nudge"),
		FMath::IsNearlyEqual(First->GetAnchoredPosition().X, FirstBefore.X + 12.0, 0.01)
		&& FMath::IsNearlyEqual(First->GetAnchoredPosition().Y, FirstBefore.Y - 7.0, 0.01));
	TestEqual(TEXT("Its sibling did not move: a nudge is not a layout input"),
		Second->GetAnchoredPosition(), SecondBefore);
	TestEqual(TEXT("and the box still wants exactly the size it wanted before"),
		Box->GetLayoutPreferredSize(), PreferredBefore);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWrapBoxSlotForcedNewLineTest,
	"DreamGUI.PanelSlot.AWrapBoxSlotAskedForANewLineStartsOneHoweverMuchRoomIsLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWrapBoxSlotForcedNewLineTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelSlotParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* First = MakeWidget(TestWorld.World, Root, TEXT("First"), 80.0f, 30.0f);
	UDreamWidget* Second = MakeWidget(TestWorld.World, Root, TEXT("Second"), 80.0f, 30.0f);
	if (!TestNotNull(TEXT("Wrap box created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerWrapBox>()))
	{
		return false;
	}
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("Both children share a line while there is room for them"),
		FMath::IsNearlyEqual(First->GetAnchoredPosition().Y, Second->GetAnchoredPosition().Y, 0.01));

	UDreamPanelSlot* SecondSlot = Second->GetPanelSlot();
	if (!TestNotNull(TEXT("The wrap box handed the second child a slot"), SecondSlot))
	{
		return false;
	}
	SecondSlot->SetNewLine(true);
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);

	TestTrue(TEXT("The child that asked for a line of its own is now below the first"),
		Second->GetAnchoredPosition().Y < First->GetAnchoredPosition().Y - 1.0);
	// The break has to be visible to measurement too, or the box arranges more lines than it made room for.
	const UDreamLayoutContainerWrapBox* Box = Cast<UDreamLayoutContainerWrapBox>(Root->GetLayoutContainer());
	TestTrue(TEXT("and the box measures itself two lines tall"),
		Box && Box->GetLayoutPreferredSize().Y >= 60.0f - 0.01f);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlotLayerIsZOrderUnderAnotherNameTest,
	"DreamGUI.PanelSlot.GridLayerIsZOrderUnderItsUMGNameRatherThanASecondNumber",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSlotLayerIsZOrderUnderAnotherNameTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelSlotParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 80.0f, 30.0f);
	if (!TestNotNull(TEXT("Grid panel created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerGridPanel>()))
	{
		return false;
	}
	UDreamPanelSlot* Slot = Child->GetPanelSlot();
	if (!TestNotNull(TEXT("The grid handed the child a slot"), Slot))
	{
		return false;
	}

	Slot->SetLayer(5);
	TestEqual(TEXT("Setting the layer sets the z-order, because they are one number"), Slot->ZOrder, 5);
	Slot->SetZOrder(9);
	TestEqual(TEXT("and reading the layer back answers with it"), Slot->GetLayer(), 9);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlacementRestoresEveryAuthoredSlotValueTest,
	"DreamGUI.PanelSlot.APlacementPutsBackEverySlotValueTheDetachDestroyed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlacementRestoresEveryAuthoredSlotValueTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelSlotParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* Elsewhere = MakeWidget(TestWorld.World, nullptr, TEXT("Elsewhere"), 300.0f, 200.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 80.0f, 30.0f);
	if (!TestNotNull(TEXT("Wrap box created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerWrapBox>())
		|| !TestNotNull(TEXT("Second parent has a box too"), Elsewhere->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		return false;
	}
	UDreamPanelSlot* Slot = Child->GetPanelSlot();
	if (!TestNotNull(TEXT("The box handed the child a slot"), Slot))
	{
		return false;
	}
	Slot->SetMinDesiredSize(FVector2D(0.0, 44.0));
	Slot->SetFillEmptySpace(true);
	Slot->SetFillSpanWhenLessThan(120.0f);
	Slot->SetNewLine(true);
	Slot->SetNudge(FVector2D(3.0, 5.0));

	FDreamWidgetPlacement Placement;
	Placement.Capture(Child);
	// A detach destroys the slot; the one it gets on the way back is a fresh default.
	Child->TrySetParent(Elsewhere, false);
	TestTrue(TEXT("The slot really was replaced by a default one"),
		Child->GetPanelSlot() == nullptr || Child->GetPanelSlot()->Nudge.IsZero());
	TestTrue(TEXT("The placement could put the widget back"), Placement.Restore(Child));

	UDreamPanelSlot* Restored = Child->GetPanelSlot();
	if (!TestNotNull(TEXT("The original parent handed it a slot again"), Restored))
	{
		return false;
	}
	TestEqual(TEXT("The per-slot minimum came back"), Restored->MinDesiredSize, FVector2D(0.0, 44.0));
	TestTrue(TEXT("The fill-empty-space request came back"), Restored->bFillEmptySpace);
	TestTrue(TEXT("The span threshold came back"),
		FMath::IsNearlyEqual(Restored->FillSpanWhenLessThan, 120.0f, 0.01f));
	TestTrue(TEXT("The forced line break came back"), Restored->bForceNewLine);
	TestEqual(TEXT("The nudge came back"), Restored->Nudge, FVector2D(3.0, 5.0));

	Root->DestroyWidget();
	Elsewhere->DestroyWidget();
	return true;
}

#endif
