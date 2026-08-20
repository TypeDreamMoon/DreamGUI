// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Tests/DreamLayoutInvalidationTestTypes.h"
#include "Engine/World.h"

/*
 * Invalidation used to be one bit, so every change reached every layout on the ancestor chain. The
 * distinction was already written down - several position setters carry the comment "only position
 * change, if parent contains LayoutContainer then we should rebuild layout, otherwise not" - but with
 * one bool the only expressible choices were the whole chain or nothing.
 *
 * A panel measures its children by desired size and never by where they sit, so a widget that only moved
 * cannot change any preferred size above it. EDreamLayoutInvalidation::Arrange says exactly that: the
 * parent re-arranges, the walk stops. What must NOT change is the visible outcome - a panel-managed
 * child still gets snapped back to where the panel wants it.
 */

namespace DreamInvalidationReasonTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	static UDreamWidget* MakeChild(UWorld* World, UDreamWidget* Parent, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(Parent ? (UObject*)Parent : (UObject*)World);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}

	/** Root(counting) -> Mid(counting) -> Leaf. Both panels report how often they really recomputed. */
	struct FCountedChainFixture
	{
		UDreamWidget* Root = nullptr;
		UDreamWidget* Mid = nullptr;
		UDreamWidget* Leaf = nullptr;
		UDreamLayoutPassCountingOverlay* RootOverlay = nullptr;
		UDreamLayoutPassCountingOverlay* MidOverlay = nullptr;

		bool Build(UWorld* World)
		{
			Root = NewObject<UDreamWidget>(World);
			Root->SetWidth(400.0f);
			Root->SetHeight(300.0f);
			Mid = MakeChild(World, Root, 200.0f, 150.0f);
			Leaf = MakeChild(World, Mid, 40.0f, 30.0f);

			RootOverlay = Cast<UDreamLayoutPassCountingOverlay>(
				Root->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));
			MidOverlay = Cast<UDreamLayoutPassCountingOverlay>(
				Mid->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));
			if (!RootOverlay || !MidOverlay)
			{
				return false;
			}
			Root->OnRegister();
			Mid->OnRegister();
			Leaf->OnRegister();
			return true;
		}

		void Settle(UDreamUIManagerWorldSubsystem* Manager)
		{
			UDreamWidget::MarkLayoutForRebuild(Root);
			Manager->TickDreamUI(0.016f);
			Manager->TickDreamUI(0.016f);
			RootOverlay->PassCount = 0;
			MidOverlay->PassCount = 0;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInvalidationMovingAChildStopsAtTheParentTest,
	"DreamGUI.Layout.InvalidationReason.MovingAChildStopsAtTheParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInvalidationMovingAChildStopsAtTheParentTest::RunTest(const FString& Parameters)
{
	using namespace DreamInvalidationReasonTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FCountedChainFixture Fixture;
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	Fixture.Settle(Manager);

	// Position only. Nothing above Mid can produce a different number.
	Fixture.Leaf->SetAnchoredPosition(FVector2D(11.0, 13.0));
	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("The parent re-arranges"), Fixture.MidOverlay->PassCount, 1);
	TestEqual(TEXT("The grandparent does not"), Fixture.RootOverlay->PassCount, 0);

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInvalidationResizingAChildStillReachesTheTopTest,
	"DreamGUI.Layout.InvalidationReason.ResizingAChildStillReachesTheTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInvalidationResizingAChildStillReachesTheTopTest::RunTest(const FString& Parameters)
{
	using namespace DreamInvalidationReasonTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FCountedChainFixture Fixture;
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	Fixture.Settle(Manager);

	// The half that must not regress. A size change really can move every preferred size above it, so
	// this one still has to reach the whole chain - narrowing the reach for position is only safe
	// because measurement is what does not depend on it.
	Fixture.Leaf->SetWidth(77.0f);
	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("The parent recomputes"), Fixture.MidOverlay->PassCount, 1);
	TestEqual(TEXT("...and so does the grandparent"), Fixture.RootOverlay->PassCount, 1);

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInvalidationSlotAlignmentStopsAtTheParentTest,
	"DreamGUI.Layout.InvalidationReason.SlotAlignmentStopsAtTheParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInvalidationSlotAlignmentStopsAtTheParentTest::RunTest(const FString& Parameters)
{
	using namespace DreamInvalidationReasonTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FCountedChainFixture Fixture;
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	Fixture.Settle(Manager);
	UDreamPanelSlot* Slot = Fixture.Leaf->GetPanelSlot();
	if (!TestNotNull(TEXT("The leaf has a panel slot"), Slot))
	{
		Fixture.Root->DestroyWidget();
		return false;
	}

	// Alignment is read by ApplyChildRect and by nothing that measures, so it cannot move a preferred
	// size anywhere on the chain.
	Slot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Right);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("Alignment re-arranges the parent"), Fixture.MidOverlay->PassCount, 1);
	TestEqual(TEXT("...and leaves the grandparent alone"), Fixture.RootOverlay->PassCount, 0);

	// Padding is added by every MeasureLayout there is, so it has to keep reaching the whole chain.
	Fixture.MidOverlay->PassCount = 0;
	Fixture.RootOverlay->PassCount = 0;
	Slot->SetPadding(FMargin(7.0f));
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("Padding re-arranges the parent"), Fixture.MidOverlay->PassCount, 1);
	TestEqual(TEXT("...and the grandparent too"), Fixture.RootOverlay->PassCount, 1);

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInvalidationParagraphAlignmentCostsNoLayoutTest,
	"DreamGUI.Layout.InvalidationReason.ParagraphAlignmentCostsNoLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInvalidationParagraphAlignmentCostsNoLayoutTest::RunTest(const FString& Parameters)
{
	using namespace DreamInvalidationReasonTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World);
	Root->SetWidth(320.0f);
	Root->SetHeight(180.0f);
	UDreamWidget* Child = MakeChild(TestWorld.World, Root, 40.0f, 20.0f);
	UDreamLayoutPassCountingOverlay* Overlay = Cast<UDreamLayoutPassCountingOverlay>(
		Root->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));
	UDreamText* Text = Cast<UDreamText>(Child->CreateNewVisual(UDreamText::StaticClass()));
	if (!Overlay || !Text)
	{
		return false;
	}
	Root->OnRegister();
	Child->OnRegister();
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	Overlay->PassCount = 0;
	const FVector2D SizeBefore = Child->GetSize();

	// UpdateUITextGeometry fills textPreferredSize from glyph advances and line heights, then uses the
	// paragraph alignment only to offset vertices inside the rect that size describes. Nothing layout
	// reads can move, so a re-alignment should not enter the layout loop at all - the canvas update
	// MarkVertexPositionDirty raises is the entire cost.
	Text->SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Right);
	Text->SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Bottom);
	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("Re-aligning a paragraph costs no layout pass"), Overlay->PassCount, 0);
	TestEqual(TEXT("...and does not move the widget"), Child->GetSize(), SizeBefore);

	// The neighbouring setter that does change the measurement must still reflow, so this cannot be
	// read as "text setters stopped invalidating".
	Text->SetFontSize(37.0f);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("Font size still reflows the parent"), Overlay->PassCount, 1);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInvalidationPanelStillOverridesAMovedChildTest,
	"DreamGUI.Layout.InvalidationReason.PanelStillOverridesAMovedChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInvalidationPanelStillOverridesAMovedChildTest::RunTest(const FString& Parameters)
{
	using namespace DreamInvalidationReasonTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World);
	Root->SetWidth(400.0f);
	Root->SetHeight(300.0f);
	UDreamWidget* Child = MakeChild(TestWorld.World, Root, 40.0f, 30.0f);
	if (!Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>())
	{
		return false;
	}
	Root->OnRegister();
	Child->OnRegister();

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	const FVector2D Arranged = Child->GetAnchoredPosition();

	// The behaviour that narrowing the reach must not break: a panel owns its children's positions, so
	// moving one by hand has to be undone on the next pass exactly as before.
	Child->SetAnchoredPosition(Arranged + FVector2D(37.0, 41.0));
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("The panel puts the child back where it wants it"), Child->GetAnchoredPosition(), Arranged);

	Root->DestroyWidget();
	return true;
}

#endif
