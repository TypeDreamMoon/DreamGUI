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
 * Layout mirroring for right-to-left cultures.
 *
 * The claim being tested is narrow and strong: mirroring a horizontal layout is ONE reflection of the
 * finished rect about the panel's width, and everything else people list separately -- the order
 * children run in, Left and Right alignment, the left and right halves of the paddings -- falls out of
 * it, because all of them are statements about where in the rect the child ends up. So each test below
 * checks a different one of those consequences against the same single mechanism, and every one of them
 * first checks that the left-to-right answer is exactly what it always was.
 */

namespace DreamFlowDirectionTestLocal
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

	void Relayout(UDreamUIManagerWorldSubsystem* Manager, UDreamWidget* Root)
	{
		UDreamWidget::MarkLayoutForRebuild(Root);
		Manager->TickDreamUI(0.016f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamHorizontalBoxMirrorsTest,
	"DreamGUI.Layout.FlowDirection.AHorizontalBoxRunsRightToLeftWhenTheFlowDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamHorizontalBoxMirrorsTest::RunTest(const FString& Parameters)
{
	using namespace DreamFlowDirectionTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 100.0f);
	UDreamWidget* First = MakeWidget(TestWorld.World, Root, TEXT("First"), 80.0f, 40.0f);
	UDreamWidget* Second = MakeWidget(TestWorld.World, Root, TEXT("Second"), 80.0f, 40.0f);
	if (!TestNotNull(TEXT("Horizontal box created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerHorizontalBox>()))
	{
		return false;
	}
	Relayout(Manager, Root);
	const FVector2D FirstLeftToRight = First->GetAnchoredPosition();
	const FVector2D SecondLeftToRight = Second->GetAnchoredPosition();
	TestTrue(TEXT("Left to right, the first child is on the left"), FirstLeftToRight.X < SecondLeftToRight.X);

	Root->SetFlowDirectionPreference(EDreamFlowDirectionPreference::RightToLeft);
	Relayout(Manager, Root);
	TestTrue(TEXT("Right to left, the first child is on the right"),
		First->GetAnchoredPosition().X > Second->GetAnchoredPosition().X);
	// A reflection, not an approximation of one: the panel is symmetric about its own centre, so each
	// child's offset simply changes sign.
	TestTrue(TEXT("and each child sits exactly where its mirror image was"),
		FMath::IsNearlyEqual(First->GetAnchoredPosition().X, -FirstLeftToRight.X, 0.01)
		&& FMath::IsNearlyEqual(Second->GetAnchoredPosition().X, -SecondLeftToRight.X, 0.01));
	TestTrue(TEXT("Nothing moved vertically"),
		FMath::IsNearlyEqual(First->GetAnchoredPosition().Y, FirstLeftToRight.Y, 0.01));

	Root->SetFlowDirectionPreference(EDreamFlowDirectionPreference::LeftToRight);
	Relayout(Manager, Root);
	TestEqual(TEXT("Switching back restores the original layout exactly"),
		First->GetAnchoredPosition(), FirstLeftToRight);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWrapBoxLineMirrorsTest,
	"DreamGUI.Layout.FlowDirection.AWrapBoxsLineRunsRightToLeftWhileItsLinesStillStackDownwards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWrapBoxLineMirrorsTest::RunTest(const FString& Parameters)
{
	using namespace DreamFlowDirectionTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* First = MakeWidget(TestWorld.World, Root, TEXT("First"), 80.0f, 40.0f);
	UDreamWidget* Second = MakeWidget(TestWorld.World, Root, TEXT("Second"), 80.0f, 40.0f);
	UDreamWidget* Wrapped = MakeWidget(TestWorld.World, Root, TEXT("Wrapped"), 200.0f, 40.0f);
	if (!TestNotNull(TEXT("Wrap box created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerWrapBox>()))
	{
		return false;
	}
	Relayout(Manager, Root);
	const double FirstLineY = First->GetAnchoredPosition().Y;
	const double SecondLineY = Wrapped->GetAnchoredPosition().Y;
	TestTrue(TEXT("The third child wrapped to a second line, below the first"), SecondLineY < FirstLineY - 1.0);

	Root->SetFlowDirectionPreference(EDreamFlowDirectionPreference::RightToLeft);
	Relayout(Manager, Root);
	TestTrue(TEXT("The line now runs right to left"),
		First->GetAnchoredPosition().X > Second->GetAnchoredPosition().X);
	TestTrue(TEXT("but the lines still stack downwards: mirroring is horizontal only"),
		FMath::IsNearlyEqual(First->GetAnchoredPosition().Y, FirstLineY, 0.01)
		&& FMath::IsNearlyEqual(Wrapped->GetAnchoredPosition().Y, SecondLineY, 0.01));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGridColumnsMirrorTest,
	"DreamGUI.Layout.FlowDirection.AGridsColumnsRunRightToLeftWhileItsRowsDoNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGridColumnsMirrorTest::RunTest(const FString& Parameters)
{
	using namespace DreamFlowDirectionTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* ColumnZero = MakeWidget(TestWorld.World, Root, TEXT("ColumnZero"), 80.0f, 40.0f);
	UDreamWidget* ColumnOne = MakeWidget(TestWorld.World, Root, TEXT("ColumnOne"), 80.0f, 40.0f);
	if (!TestNotNull(TEXT("Grid created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerGridPanel>()))
	{
		return false;
	}
	if (UDreamPanelSlot* Slot = ColumnOne->GetPanelSlot(); IsValid(Slot))
	{
		Slot->SetColumn(1);
	}
	else
	{
		return false;
	}
	Relayout(Manager, Root);
	TestTrue(TEXT("Left to right, column one is to the right of column zero"),
		ColumnOne->GetAnchoredPosition().X > ColumnZero->GetAnchoredPosition().X);
	const double RowY = ColumnZero->GetAnchoredPosition().Y;

	Root->SetFlowDirectionPreference(EDreamFlowDirectionPreference::RightToLeft);
	Relayout(Manager, Root);
	TestTrue(TEXT("Right to left, column one is to the left of column zero"),
		ColumnOne->GetAnchoredPosition().X < ColumnZero->GetAnchoredPosition().X);
	TestTrue(TEXT("and both are still on the same row"),
		FMath::IsNearlyEqual(ColumnZero->GetAnchoredPosition().Y, RowY, 0.01)
		&& FMath::IsNearlyEqual(ColumnOne->GetAnchoredPosition().Y, RowY, 0.01));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAlignmentAndPaddingMirrorTest,
	"DreamGUI.Layout.FlowDirection.LeftAlignmentAndLeftPaddingBothBecomeRightUnderRightToLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAlignmentAndPaddingMirrorTest::RunTest(const FString& Parameters)
{
	using namespace DreamFlowDirectionTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 100.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 80.0f, 40.0f);
	UDreamWidget* Mirror = MakeWidget(TestWorld.World, nullptr, TEXT("Mirror"), 300.0f, 100.0f);
	UDreamWidget* MirrorChild = MakeWidget(TestWorld.World, Mirror, TEXT("MirrorChild"), 80.0f, 40.0f);
	if (!TestNotNull(TEXT("Overlay created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>())
		|| !TestNotNull(TEXT("Comparison overlay created"), Mirror->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>()))
	{
		return false;
	}
	UDreamPanelSlot* Slot = Child->GetPanelSlot();
	UDreamPanelSlot* MirrorSlot = MirrorChild->GetPanelSlot();
	if (!Slot || !MirrorSlot)
	{
		return false;
	}
	// One slot says "left, twenty in from the left edge"; the other says the mirror image of that in
	// so many words. Flipping the first one's flow direction has to land it on top of the second.
	Slot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Left);
	Slot->SetPadding(FMargin(20.0f, 0.0f, 0.0f, 0.0f));
	MirrorSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Right);
	MirrorSlot->SetPadding(FMargin(0.0f, 0.0f, 20.0f, 0.0f));
	Relayout(Manager, Root);
	Relayout(Manager, Mirror);
	TestTrue(TEXT("The two start on opposite sides"),
		Child->GetAnchoredPosition().X < 0.0 && MirrorChild->GetAnchoredPosition().X > 0.0);

	Root->SetFlowDirectionPreference(EDreamFlowDirectionPreference::RightToLeft);
	Relayout(Manager, Root);
	TestTrue(TEXT("Mirrored, Left with left padding is Right with right padding, to the unit"),
		FMath::IsNearlyEqual(Child->GetAnchoredPosition().X, MirrorChild->GetAnchoredPosition().X, 0.01));

	Root->DestroyWidget();
	Mirror->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamVerticalAndCanvasAreNotMirroredTest,
	"DreamGUI.Layout.FlowDirection.AVerticalStackKeepsItsOrderAndACanvasIsNeverMirrored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamVerticalAndCanvasAreNotMirroredTest::RunTest(const FString& Parameters)
{
	using namespace DreamFlowDirectionTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Column = MakeWidget(TestWorld.World, nullptr, TEXT("Column"), 300.0f, 200.0f);
	UDreamWidget* Top = MakeWidget(TestWorld.World, Column, TEXT("Top"), 80.0f, 40.0f);
	UDreamWidget* Bottom = MakeWidget(TestWorld.World, Column, TEXT("Bottom"), 80.0f, 40.0f);
	if (!TestNotNull(TEXT("Vertical box created"), Column->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		return false;
	}
	Relayout(Manager, Column);
	const FVector2D TopBefore = Top->GetAnchoredPosition();
	const FVector2D BottomBefore = Bottom->GetAnchoredPosition();

	Column->SetFlowDirectionPreference(EDreamFlowDirectionPreference::RightToLeft);
	Relayout(Manager, Column);
	TestEqual(TEXT("A vertical stack's order is not a horizontal statement, so it does not move"),
		Top->GetAnchoredPosition(), TopBefore);
	TestEqual(TEXT("and neither does the child below it"), Bottom->GetAnchoredPosition(), BottomBefore);

	// A canvas child states its own rect through its anchors and is never arranged horizontally by the
	// panel, so there is nothing for a mirror to act on -- Slate says the same about SConstraintCanvas.
	UDreamWidget* Canvas = MakeWidget(TestWorld.World, nullptr, TEXT("Canvas"), 300.0f, 200.0f);
	UDreamWidget* Placed = MakeWidget(TestWorld.World, Canvas, TEXT("Placed"), 80.0f, 40.0f);
	if (!TestNotNull(TEXT("Canvas created"), Canvas->CreateNewLayoutContainer<UDreamLayoutContainerCanvasPanel>()))
	{
		return false;
	}
	Placed->SetAnchoredPosition(FVector2D(-90.0, 30.0));
	Relayout(Manager, Canvas);
	const FVector2D PlacedBefore = Placed->GetAnchoredPosition();
	Canvas->SetFlowDirectionPreference(EDreamFlowDirectionPreference::RightToLeft);
	Relayout(Manager, Canvas);
	TestEqual(TEXT("A canvas child stays exactly where it put itself"), Placed->GetAnchoredPosition(), PlacedBefore);

	Column->DestroyWidget();
	Canvas->DestroyWidget();
	return true;
}

#endif
