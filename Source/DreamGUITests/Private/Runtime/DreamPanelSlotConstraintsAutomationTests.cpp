// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "DreamScopedWorld.h"

/*
 * What a slot is allowed to say about its child, what a panel is allowed to do to children it does not
 * arrange, and what a read-only question about a panel is allowed to touch.
 */

namespace DreamPanelSlotConstraintsTestLocal
{
	using DreamTests::FScopedGameWorld;

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
	FDreamSlotMinDesiredSizeRaisesAChildInAnyPanelTest,
	"DreamGUI.PanelSlot.APerSlotMinimumRaisesAChildWithoutWrappingItInASizeBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSlotMinDesiredSizeRaisesAChildInAnyPanelTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelSlotConstraintsTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 400.0f);
	UDreamWidget* Small = MakeWidget(TestWorld.World, Root, TEXT("Small"), 50.0f, 20.0f);
	UDreamWidget* Plain = MakeWidget(TestWorld.World, Root, TEXT("Plain"), 50.0f, 20.0f);
	if (!TestNotNull(TEXT("Vertical box created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		return false;
	}

	UDreamPanelSlot* SmallSlot = Small->GetPanelSlot();
	if (!TestNotNull(TEXT("The container handed the child a slot"), SmallSlot))
	{
		return false;
	}
	// Before the slot carried these, the only Min/MaxDesiredSize in the plugin were SizeBox's, and
	// SizeBox takes one child -- so bounding one item of a stack meant wrapping that item in its own
	// panel. They are applied in UDreamPanelLayoutBase::GetDesiredSize, which is the single point every
	// panel measures through, so this works in a vertical box without the vertical box knowing about it.
	SmallSlot->SetMinDesiredSize(FVector2D(0.0, 80.0));

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);

	TestTrue(TEXT("The constrained child is raised to its slot minimum"),
		FMath::IsNearlyEqual(Small->GetHeight(), 80.0f, 0.01f));
	TestTrue(TEXT("Its unconstrained sibling is left alone"),
		FMath::IsNearlyEqual(Plain->GetHeight(), 20.0f, 0.01f));

	// And the ceiling, on the same slot: minimum wins when the two cross, matching SizeBox.
	SmallSlot->SetMaxDesiredSize(FVector2D(0.0, 40.0));
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("A maximum below the minimum resolves to the minimum"),
		FMath::IsNearlyEqual(Small->GetHeight(), 80.0f, 0.01f));

	SmallSlot->SetMinDesiredSize(FVector2D::ZeroVector);
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("With the minimum cleared the child drops back under its own ceiling"),
		FMath::IsNearlyEqual(Small->GetHeight(), 20.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWrapBoxSlotFillsTheRestOfItsLineTest,
	"DreamGUI.PanelSlot.AWrapBoxSlotThatAsksToFillTakesTheRoomItsLineDidNotUse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWrapBoxSlotFillsTheRestOfItsLineTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelSlotConstraintsTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* Fixed = MakeWidget(TestWorld.World, Root, TEXT("Fixed"), 100.0f, 30.0f);
	UDreamWidget* Filler = MakeWidget(TestWorld.World, Root, TEXT("Filler"), 100.0f, 30.0f);
	UDreamLayoutContainerWrapBox* Wrap = Root->CreateNewLayoutContainer<UDreamLayoutContainerWrapBox>();
	if (!TestNotNull(TEXT("Wrap box created"), Wrap))
	{
		return false;
	}

	UDreamPanelSlot* FillerSlot = Filler->GetPanelSlot();
	if (!TestNotNull(TEXT("The container handed the child a slot"), FillerSlot))
	{
		return false;
	}
	// UWrapBoxSlot::bFillEmptySpace, which had no counterpart here: the wrap box arranged strictly by
	// desired size and a line's leftover room simply stayed empty.
	FillerSlot->SetFillEmptySpace(true);

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);

	// Both are 100 wide on a 300-wide line, so 100 is left over and one child asked for it.
	TestTrue(TEXT("The filling child absorbs the line's leftover"),
		FMath::IsNearlyEqual(Filler->GetWidth(), 200.0f, 0.01f));
	TestTrue(TEXT("The child that did not ask keeps its own width"),
		FMath::IsNearlyEqual(Fixed->GetWidth(), 100.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWrapBoxSlotTakesAWholeLineBelowItsThresholdTest,
	"DreamGUI.PanelSlot.AWrapBoxSlotBelowItsSpanThresholdStopsSharingItsLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWrapBoxSlotTakesAWholeLineBelowItsThresholdTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelSlotConstraintsTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* First = MakeWidget(TestWorld.World, Root, TEXT("First"), 80.0f, 30.0f);
	UDreamWidget* Wide = MakeWidget(TestWorld.World, Root, TEXT("Wide"), 80.0f, 30.0f);
	UDreamLayoutContainerWrapBox* Wrap = Root->CreateNewLayoutContainer<UDreamLayoutContainerWrapBox>();
	if (!TestNotNull(TEXT("Wrap box created"), Wrap))
	{
		return false;
	}
	UDreamPanelSlot* WideSlot = Wide->GetPanelSlot();
	if (!TestNotNull(TEXT("The container handed the child a slot"), WideSlot))
	{
		return false;
	}

	// UWrapBoxSlot::FillSpanWhenLessThan. The wrap width is 300 and the threshold is 400, so the child
	// is under it and must get a line of its own; both children would otherwise share the first line.
	WideSlot->SetFillSpanWhenLessThan(400.0f);

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);

	TestTrue(TEXT("The child under its threshold takes the whole span"),
		FMath::IsNearlyEqual(Wide->GetWidth(), 300.0f, 0.01f));
	// Sharing would have put them side by side at the same Y; a line of its own puts it below.
	TestFalse(TEXT("...on a line below its sibling rather than beside it"),
		FMath::IsNearlyEqual(Wide->GetAnchoredPosition().Y, First->GetAnchoredPosition().Y, 0.01));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSizeBoxCollapsesItsSurplusChildrenTest,
	"DreamGUI.PanelLayout.ASizeBoxHoldingMoreThanOneChildCollapsesTheOnesItCannotArrange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSizeBoxCollapsesItsSurplusChildrenTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelSlotConstraintsTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* Content = MakeWidget(TestWorld.World, Root, TEXT("Content"), 50.0f, 50.0f);
	UDreamLayoutContainerSizeBox* SizeBox = Root->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>();
	if (!TestNotNull(TEXT("Size box created"), SizeBox))
	{
		return false;
	}

	// A SizeBox reports GetMaxChildren() == 1, so this state only arrives from data authored before the
	// capacity rule or from a path that went around it -- which is exactly what this call models. The
	// surplus child used to be neither arranged NOR hidden: restored to its authored rect and left
	// visible, floating over the content wherever the designer's root size had put it. UMG cannot
	// express a second child at all, and the one panel here that already had to choose --
	// WidgetSwitcher -- collapses what it is not showing.
	UDreamWidget* Surplus = MakeWidget(TestWorld.World, nullptr, TEXT("Surplus"), 40.0f, 40.0f);
	if (!TestTrue(TEXT("The surplus child attaches through the capacity-ignoring path"),
		Surplus->SetParentIgnoringCapacity(Root)))
	{
		return false;
	}

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);

	TestTrue(TEXT("The content child is laid out and visible"), Content->GetLayoutVisibleInHierarchy());
	TestFalse(TEXT("The surplus child is collapsed rather than left floating"),
		Surplus->GetLayoutVisibleInHierarchy());

	// And the suppression belongs to the panel: taking the panel away has to give the child back.
	Root->RemoveLayoutContainer();
	TestTrue(TEXT("Removing the panel un-collapses what it had suppressed"),
		Surplus->GetLayoutVisibleInHierarchy());

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollQueryDoesNotWriteGeometryTest,
	"DreamGUI.ScrollBox.AskingWhetherAWidgetCanBeScrolledIntoViewWritesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollQueryDoesNotWriteGeometryTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelSlotConstraintsTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 200.0f, 120.0f);
	UDreamWidget* A = MakeWidget(TestWorld.World, Root, TEXT("A"), 100.0f, 60.0f);
	UDreamWidget* B = MakeWidget(TestWorld.World, Root, TEXT("B"), 100.0f, 60.0f);
	UDreamWidget* Floating = MakeWidget(TestWorld.World, Root, TEXT("Floating"), 100.0f, 60.0f);
	UDreamLayoutContainerScrollBox* Scroll = Root->CreateNewLayoutContainer<UDreamLayoutContainerScrollBox>();
	if (!TestNotNull(TEXT("Scroll box created"), Scroll))
	{
		return false;
	}

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);

	// Opt one child out of layout and then move it by hand, which is what "I position myself" means.
	Floating->SetIgnoreLayout(true);
	Floating->SetAnchoredPosition(FVector2D(777.0, 888.0));
	const FVector2D Placed = Floating->GetAnchoredPosition();

	// CanScrollWidgetIntoView is what directional navigation asks before it will even consider a
	// candidate -- once per press of a stick or d-pad. It reached CollectLayoutChildren with slot
	// creation enabled, which restores authored geometry onto every opted-out child (a real
	// SetAnchorData) and news up a slot object for any child that has none. A question that answers
	// "yes" or "no" was quietly putting this widget back where the designer left it.
	Scroll->CanScrollWidgetIntoView(A);
	Scroll->CanScrollWidgetIntoView(B);

	TestEqual(TEXT("The opted-out child is still where it was put"), Floating->GetAnchoredPosition(), Placed);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamZOrderReorderCostsNoExtraPassTest,
	"DreamGUI.PanelLayout.RestackingAnOverlayByZOrderSettlesInTheSamePassThatDidIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamZOrderReorderCostsNoExtraPassTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelSlotConstraintsTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* Back = MakeWidget(TestWorld.World, Root, TEXT("Back"), 50.0f, 50.0f);
	UDreamWidget* Middle = MakeWidget(TestWorld.World, Root, TEXT("Middle"), 50.0f, 50.0f);
	UDreamWidget* Front = MakeWidget(TestWorld.World, Root, TEXT("Front"), 50.0f, 50.0f);
	if (!TestNotNull(TEXT("Overlay created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>()))
	{
		return false;
	}

	// Settle first: the count only means something once the tree has stopped moving for other reasons.
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);

	UDreamPanelSlot* BackSlot = Back->GetPanelSlot();
	UDreamPanelSlot* FrontSlot = Front->GetPanelSlot();
	if (!TestNotNull(TEXT("Back has a slot"), BackSlot) || !TestNotNull(TEXT("Front has a slot"), FrontSlot))
	{
		return false;
	}
	// Ask for a restack that genuinely reverses the array: Front to the bottom, Back to the top.
	BackSlot->SetZOrder(10);
	FrontSlot->SetZOrder(-10);

	Manager->TickDreamUI(0.016f);

	// The pass that applies the restack used to re-dirty the very panel running it: the reorder went
	// through SetSiblingIndex, which calls MarkLayoutForRebuild on the parent, relighting the
	// bIsLayoutDirty that BeginLayoutPass had just consumed. Every real ZOrder change therefore bought
	// a second whole-tree pass. The order is a paint order; no panel's arrangement reads it.
	TestEqual(TEXT("A restack settles in a single layout pass"), Manager->GetLastLayoutPassCount(), 1);

	const TArray<UDreamWidget*>& Sorted = Root->GetChildren();
	if (TestEqual(TEXT("Still three children"), Sorted.Num(), 3))
	{
		TestTrue(TEXT("Lowest ZOrder is drawn first"), Sorted[0] == Front);
		TestTrue(TEXT("Equal-by-default keeps its place in the middle"), Sorted[1] == Middle);
		TestTrue(TEXT("Highest ZOrder is drawn last"), Sorted[2] == Back);
	}

	// A second, identical tick must not move anything again.
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("The restack is stable"), Root->GetChildren()[2] == Back);

	Root->DestroyWidget();
	return true;
}

#endif
