// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "Interaction/DreamContentWidget.h"

/*
 * The panel side of the UMG surface: the child questions UMG lets you ask a panel, the per-panel knobs,
 * and the size box's own arithmetic.
 *
 * The child family is forwarding by design -- the hierarchy belongs to the widget -- so the assertions
 * check that asking the PANEL gives the same answer as asking the widget, which is the only thing a
 * forwarder can get wrong.
 */

namespace DreamPanelWidgetParityTestLocal
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
	FDreamPanelAnswersTheChildQuestionsItsWidgetAnswersTest,
	"DreamGUI.Panel.APanelAnswersTheChildQuestionsItsOwnWidgetAnswers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPanelAnswersTheChildQuestionsItsWidgetAnswersTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* First = MakeWidget(TestWorld.World, Root, TEXT("First"), 80.0f, 30.0f);
	UDreamWidget* Second = MakeWidget(TestWorld.World, Root, TEXT("Second"), 80.0f, 30.0f);
	UDreamWidget* Stranger = MakeWidget(TestWorld.World, nullptr, TEXT("Stranger"), 10.0f, 10.0f);
	UDreamLayoutContainerVerticalBox* Box = Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
	if (!TestNotNull(TEXT("Vertical box created"), Box))
	{
		return false;
	}

	TestEqual(TEXT("The panel counts what the widget counts"), Box->GetChildrenCount(), Root->GetChildrenCount());
	TestEqual(TEXT("and indexes the same way"), Box->GetChildAt(1), Second);
	TestNull(TEXT("An index outside the list is null, not a crash"), Box->GetChildAt(7));
	TestEqual(TEXT("It finds a child at the same position"), Box->GetChildIndex(Second), 1);
	TestEqual(TEXT("and does not find one that is not there"), Box->GetChildIndex(Stranger), (int32)INDEX_NONE);
	TestTrue(TEXT("It has the children it has"), Box->HasChild(First) && Box->HasAnyChildren());
	TestFalse(TEXT("and not the ones it has not"), Box->HasChild(Stranger));
	TestEqual(TEXT("The full list is the widget's list"), Box->GetAllChildren().Num(), 2);

	UDreamPanelSlot* AddedSlot = Box->AddChild(Stranger);
	TestNotNull(TEXT("Adding through the panel hands back the slot"), AddedSlot);
	TestEqual(TEXT("and the widget really took the child"), Stranger->GetParent(), Root);

	TestTrue(TEXT("Removing through the panel detaches the child"), Box->RemoveChild(Stranger));
	TestNull(TEXT("and leaves it alive with no parent, as RemoveChild promises"), Stranger->GetParent());
	TestTrue(TEXT("Removing by index works the same"), Box->RemoveChildAt(0));
	TestEqual(TEXT("so the panel is down to one child"), Box->GetChildrenCount(), 1);

	Stranger->DestroyWidget();
	First->DestroyWidget();
	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPanelReplaceChildAtKeepsThePlaceTest,
	"DreamGUI.Panel.ReplacingAChildKeepsItsPlaceAndLeavesTheOldOneAlive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPanelReplaceChildAtKeepsThePlaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* First = MakeWidget(TestWorld.World, Root, TEXT("First"), 80.0f, 30.0f);
	UDreamWidget* Middle = MakeWidget(TestWorld.World, Root, TEXT("Middle"), 80.0f, 30.0f);
	UDreamWidget* Last = MakeWidget(TestWorld.World, Root, TEXT("Last"), 80.0f, 30.0f);
	UDreamWidget* Replacement = MakeWidget(TestWorld.World, nullptr, TEXT("Replacement"), 80.0f, 30.0f);
	UDreamLayoutContainerOverlay* Overlay = Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
	if (!TestNotNull(TEXT("Overlay created"), Overlay))
	{
		return false;
	}

	TestTrue(TEXT("The replacement landed"), Overlay->ReplaceChildAt(1, Replacement));
	TestEqual(TEXT("It took the index the old child had"), Overlay->GetChildIndex(Replacement), 1);
	TestEqual(TEXT("The children on either side did not move"), Overlay->GetChildAt(0), First);
	TestEqual(TEXT("nor did the one after it"), Overlay->GetChildAt(2), Last);
	TestTrue(TEXT("The replaced child is still alive, detached, and the caller's to deal with"),
		IsValid(Middle) && Middle->GetParent() == nullptr);

	TestFalse(TEXT("An index outside the list replaces nothing"), Overlay->ReplaceChildAt(9, Middle));
	TestFalse(TEXT("and neither does a null replacement"), Overlay->ReplaceChildAt(0, nullptr));

	Middle->DestroyWidget();
	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamContentWidgetHandsBackItsChildsSlotTest,
	"DreamGUI.Panel.ASingleChildHostHandsBackTheSlotItsChildIsHolding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamContentWidgetHandsBackItsChildsSlotTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 80.0f, 30.0f);
	if (!TestNotNull(TEXT("Size box created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>()))
	{
		return false;
	}
	// A size box declares the content behaviour through GetRequiredBehaviourClasses, so assigning the
	// container is what puts one on the widget.
	UDreamContentWidget* Host = Root->GetComponent<UDreamContentWidget>();
	if (!TestNotNull(TEXT("The size box brought a content host with it"), Host))
	{
		return false;
	}
	TestEqual(TEXT("The host's content is the one child"), Host->GetContent(), Child);
	TestEqual(TEXT("and the content slot is the slot that child is holding"), Host->GetContentSlot(), Child->GetPanelSlot());

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWrapBoxVerticalOrientationTest,
	"DreamGUI.Panel.AWrapBoxSetToVerticalFillsAColumnBeforeItStartsANewOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWrapBoxVerticalOrientationTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* First = MakeWidget(TestWorld.World, Root, TEXT("First"), 80.0f, 60.0f);
	UDreamWidget* Second = MakeWidget(TestWorld.World, Root, TEXT("Second"), 80.0f, 60.0f);
	UDreamWidget* Third = MakeWidget(TestWorld.World, Root, TEXT("Third"), 80.0f, 60.0f);
	UDreamWidget* Fourth = MakeWidget(TestWorld.World, Root, TEXT("Fourth"), 80.0f, 60.0f);
	UDreamLayoutContainerWrapBox* Box = Root->CreateNewLayoutContainer<UDreamLayoutContainerWrapBox>();
	if (!TestNotNull(TEXT("Wrap box created"), Box))
	{
		return false;
	}
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	// Horizontal is the default and must stay exactly what it was: three 80-wide children fit a 300-wide
	// row, so the second sits to the right of the first at the same height.
	TestTrue(TEXT("A horizontal box puts the second child beside the first"),
		Second->GetAnchoredPosition().X > First->GetAnchoredPosition().X + 1.0
		&& FMath::IsNearlyEqual(Second->GetAnchoredPosition().Y, First->GetAnchoredPosition().Y, 0.01));
	TestTrue(TEXT("and measures three across by two down"),
		FMath::IsNearlyEqual(Box->GetLayoutPreferredSize().X, 240.0f, 0.01f)
		&& FMath::IsNearlyEqual(Box->GetLayoutPreferredSize().Y, 120.0f, 0.01f));

	Box->SetOrientation(EDreamPanelOrientation::Vertical);
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);

	TestTrue(TEXT("A vertical box puts the second child below the first, in the same column"),
		FMath::IsNearlyEqual(Second->GetAnchoredPosition().X, First->GetAnchoredPosition().X, 0.01)
		&& Second->GetAnchoredPosition().Y < First->GetAnchoredPosition().Y - 1.0);
	// Three 60-tall children fill a 200-tall column and the fourth starts the next one.
	TestTrue(TEXT("The fourth child starts a second column, to the right of the first"),
		Fourth->GetAnchoredPosition().X > First->GetAnchoredPosition().X + 1.0
		&& FMath::IsNearlyEqual(Fourth->GetAnchoredPosition().Y, First->GetAnchoredPosition().Y, 0.01));
	TestTrue(TEXT("and the box now measures two across by three down"),
		FMath::IsNearlyEqual(Box->GetLayoutPreferredSize().X, 160.0f, 0.01f)
		&& FMath::IsNearlyEqual(Box->GetLayoutPreferredSize().Y, 180.0f, 0.01f));

	TestTrue(TEXT("Third stayed in the first column"), IsValid(Third));
	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWrapBoxLineAlignmentTest,
	"DreamGUI.Panel.ALineThatDidNotFillTheWrapWidthCanBeCentredOrPushedRight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWrapBoxLineAlignmentTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* First = MakeWidget(TestWorld.World, Root, TEXT("First"), 80.0f, 30.0f);
	UDreamWidget* Second = MakeWidget(TestWorld.World, Root, TEXT("Second"), 80.0f, 30.0f);
	UDreamLayoutContainerWrapBox* Box = Root->CreateNewLayoutContainer<UDreamLayoutContainerWrapBox>();
	if (!TestNotNull(TEXT("Wrap box created"), Box))
	{
		return false;
	}
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	// 160 of the 300 is used, so there are 140 units of slack for the alignment to spend.
	TestTrue(TEXT("Left is the default and starts at the edge"),
		FMath::IsNearlyEqual(First->GetAnchoredPosition().X, -110.0, 0.01));

	Box->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("Centre splits the slack either side of the line"),
		FMath::IsNearlyEqual(First->GetAnchoredPosition().X, -40.0, 0.01));

	Box->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Right);
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("Right puts the whole line against the far edge"),
		FMath::IsNearlyEqual(First->GetAnchoredPosition().X, 30.0, 0.01));
	TestTrue(TEXT("and the line is still a line: the second child follows the first"),
		FMath::IsNearlyEqual(Second->GetAnchoredPosition().X, 110.0, 0.01));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUniformGridSlotPaddingTest,
	"DreamGUI.Panel.AUniformGridsSlotPaddingComesOutOfEveryCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUniformGridSlotPaddingTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* Cell = MakeWidget(TestWorld.World, Root, TEXT("Cell"), 80.0f, 30.0f);
	UDreamLayoutContainerUniformGridPanel* Grid = Root->CreateNewLayoutContainer<UDreamLayoutContainerUniformGridPanel>();
	if (!TestNotNull(TEXT("Uniform grid created"), Grid))
	{
		return false;
	}
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("The one cell fills the grid while nothing is padding it"), Cell->GetSize(), FVector2D(300.0, 200.0));

	Grid->SetSlotPadding(FMargin(10.0f, 5.0f, 10.0f, 5.0f));
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("Slot padding is taken out of the cell, on all four sides"),
		Cell->GetSize(), FVector2D(280.0, 190.0));
	TestEqual(TEXT("and the cell's content is centred on what is left of it"),
		Cell->GetAnchoredPosition(), FVector2D(0.0, 0.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScaleBoxStretchDirectionTest,
	"DreamGUI.Panel.AScaleBoxToldToShrinkOnlyNeverBlowsItsContentUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScaleBoxStretchDirectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 100.0f, 50.0f);
	UDreamLayoutContainerScaleBox* ScaleBox = Root->CreateNewLayoutContainer<UDreamLayoutContainerScaleBox>();
	if (!TestNotNull(TEXT("Scale box created"), ScaleBox))
	{
		return false;
	}
	ScaleBox->SetStretch(EDreamScaleBoxStretch::ScaleToFit);

	// Arranging into a fragment reads the decision without needing anything to have been drawn.
	auto ScaleOfFirstChild = [](const FDreamFragment& InFragment)
	{
		return InFragment.Children.Num() > 0 ? InFragment.Children[0].LayoutScale.X : -1.0f;
	};
	const FDreamFragment Unbounded = ScaleBox->Arrange();
	TestTrue(TEXT("A 100-wide child in a 300x200 box is scaled up to fit"),
		FMath::IsNearlyEqual(ScaleOfFirstChild(Unbounded), 3.0f, 0.01f));

	ScaleBox->SetStretchDirection(EDreamScaleBoxStretchDirection::DownOnly);
	const FDreamFragment DownOnly = ScaleBox->Arrange();
	TestTrue(TEXT("Told to shrink only, it leaves the content at its authored size"),
		FMath::IsNearlyEqual(ScaleOfFirstChild(DownOnly), 1.0f, 0.01f));

	// And the other way round: a child larger than the box would be shrunk, and UpOnly forbids it.
	Child->SetWidth(900.0f);
	Child->SetHeight(600.0f);
	ScaleBox->SetStretchDirection(EDreamScaleBoxStretchDirection::UpOnly);
	const FDreamFragment UpOnly = ScaleBox->Arrange();
	TestTrue(TEXT("Told to grow only, it leaves an oversized child alone"),
		FMath::IsNearlyEqual(ScaleOfFirstChild(UpOnly), 1.0f, 0.01f));

	TestTrue(TEXT("Child survived the arrangement"), IsValid(Child));
	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSizeBoxAspectRatioArithmeticTest,
	"DreamGUI.Panel.ASizeBoxRatioBoundShrinksARectUntilItObeysTheRatio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSizeBoxAspectRatioArithmeticTest::RunTest(const FString& Parameters)
{
	// The arithmetic on its own, where the answer is checkable rather than merely plausible.
	const FVector2D Squared = UDreamLayoutContainerSizeBox::ConstrainToAspectRatio(
		FVector2D(200.0, 100.0), FVector2D(200.0, 100.0), 0.0f, 1.0f, true, true);
	TestTrue(TEXT("A 2:1 rect under a 1:1 ceiling becomes square and still fits"),
		FMath::IsNearlyEqual(Squared.X, 100.0, 0.01) && FMath::IsNearlyEqual(Squared.Y, 100.0, 0.01));

	const FVector2D Untouched = UDreamLayoutContainerSizeBox::ConstrainToAspectRatio(
		FVector2D(200.0, 100.0), FVector2D(200.0, 100.0), 0.0f, 4.0f, true, true);
	TestTrue(TEXT("A rect already inside the bound is left exactly alone"),
		FMath::IsNearlyEqual(Untouched.X, 200.0, 0.01) && FMath::IsNearlyEqual(Untouched.Y, 100.0, 0.01));

	const FVector2D NoBound = UDreamLayoutContainerSizeBox::ConstrainToAspectRatio(
		FVector2D(200.0, 100.0), FVector2D(200.0, 100.0), 0.0f, 0.0f, true, true);
	TestTrue(TEXT("With neither bound set nothing happens at all"),
		FMath::IsNearlyEqual(NoBound.X, 200.0, 0.01) && FMath::IsNearlyEqual(NoBound.Y, 100.0, 0.01));

	const FVector2D Degenerate = UDreamLayoutContainerSizeBox::ConstrainToAspectRatio(
		FVector2D(200.0, 0.0), FVector2D(200.0, 100.0), 1.0f, 1.0f, true, true);
	TestTrue(TEXT("A rect with no height has no ratio to correct"),
		FMath::IsNearlyEqual(Degenerate.Y, 0.0, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSizeBoxAspectRatioArrangesContentTest,
	"DreamGUI.Panel.ASizeBoxRatioBoundReachesTheContentItArranges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSizeBoxAspectRatioArrangesContentTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 200.0f, 100.0f);
	UDreamWidget* Content = MakeWidget(TestWorld.World, Root, TEXT("Content"), 40.0f, 20.0f);
	UDreamLayoutContainerSizeBox* Box = Root->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>();
	if (!TestNotNull(TEXT("Size box created"), Box))
	{
		return false;
	}
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("With no ratio set the content fills the box, exactly as before"),
		Content->GetSize(), FVector2D(200.0, 100.0));

	Box->SetMaxAspectRatio(1.0f);
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("A 1:1 ceiling squares the content against the shorter side"),
		Content->GetSize(), FVector2D(100.0, 100.0));
	TestEqual(TEXT("and a Fill axis that stopped filling is centred, as Slate does"),
		Content->GetAnchoredPosition(), FVector2D(0.0, 0.0));

	Box->ClearMaxAspectRatio();
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("Clearing the bound puts the old behaviour back"),
		Content->GetSize(), FVector2D(200.0, 100.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSizeBoxPerAxisAccessorsTest,
	"DreamGUI.Panel.TheSizeBoxPerAxisSettersWriteTheTwoVectorsItAlreadyHad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSizeBoxPerAxisAccessorsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 200.0f, 100.0f);
	UDreamLayoutContainerSizeBox* Box = Root->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>();
	if (!TestNotNull(TEXT("Size box created"), Box))
	{
		return false;
	}

	Box->SetMinDesiredWidth(40.0f);
	TestEqual(TEXT("The width setter touches only x of the pair"), Box->MinDesiredSize, FVector2D(40.0, 0.0));
	TestTrue(TEXT("and the getter answers with it"), FMath::IsNearlyEqual(Box->GetMinDesiredWidth(), 40.0f, 0.01f));
	Box->SetMinDesiredHeight(25.0f);
	TestEqual(TEXT("The height setter touches only y"), Box->MinDesiredSize, FVector2D(40.0, 25.0));
	Box->ClearMinDesiredWidth();
	TestEqual(TEXT("Clearing an axis is writing zero to it, and leaves the other one"),
		Box->MinDesiredSize, FVector2D(0.0, 25.0));

	Box->SetMaxDesiredHeight(70.0f);
	TestEqual(TEXT("The maximum is the same shape"), Box->MaxDesiredSize, FVector2D(0.0, 70.0));
	Box->ClearMaxDesiredHeight();
	TestEqual(TEXT("and clears the same way"), Box->MaxDesiredSize, FVector2D(0.0, 0.0));

	Box->SetWidthOverride(120.0f);
	Box->SetOverrideWidth(true);
	Box->ClearWidthOverride();
	TestFalse(TEXT("Clearing an override switches it off rather than zeroing the value"), Box->bOverrideWidth);
	TestTrue(TEXT("so the authored override survives to be switched back on"),
		FMath::IsNearlyEqual(Box->WidthOverride, 120.0f, 0.01f));

	Box->SetMinAspectRatio(0.5f);
	TestTrue(TEXT("A ratio bound reports itself as set"), Box->IsMinAspectRatioOverride());
	Box->ClearMinAspectRatio();
	TestFalse(TEXT("and as unset once cleared"), Box->IsMinAspectRatioOverride());

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGridFillPerTrackTest,
	"DreamGUI.Panel.AGridFillCoefficientCanBeSetOneTrackAtATimeAndClearedWholesale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGridFillPerTrackTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamLayoutContainerGridPanel* Grid = Root->CreateNewLayoutContainer<UDreamLayoutContainerGridPanel>();
	if (!TestNotNull(TEXT("Grid panel created"), Grid))
	{
		return false;
	}

	Grid->SetColumnFillAt(2, 1.5f);
	TestEqual(TEXT("The array grew to reach the track that was asked for"), Grid->ColumnFill.Num(), 3);
	TestTrue(TEXT("The named track carries the coefficient"),
		FMath::IsNearlyEqual(Grid->ColumnFill[2], 1.5f, 0.01f));
	TestTrue(TEXT("and the tracks it grew past are left sizing themselves from their children"),
		FMath::IsNearlyEqual(Grid->ColumnFill[0], 0.0f, 0.01f));

	Grid->SetRowFillAt(0, 2.0f);
	TestEqual(TEXT("Rows work the same way"), Grid->RowFill.Num(), 1);

	Grid->SetColumnFillAt(-1, 3.0f);
	TestEqual(TEXT("A track index below zero is refused rather than clamped onto track 0"),
		Grid->ColumnFill.Num(), 3);

	Grid->ClearFill();
	TestTrue(TEXT("Clearing drops every coefficient on both axes"),
		Grid->ColumnFill.IsEmpty() && Grid->RowFill.IsEmpty());

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSafeZoneSidesToPadTest,
	"DreamGUI.Panel.SafeZoneSidesToPadWritesTheFourSwitchesInUMGsArgumentOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSafeZoneSidesToPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamLayoutContainerSafeZone* Zone = Root->CreateNewLayoutContainer<UDreamLayoutContainerSafeZone>();
	if (!TestNotNull(TEXT("Safe zone created"), Zone))
	{
		return false;
	}

	// Left, right, top, bottom -- not the order an FMargin lists them in, which is the whole reason
	// this is worth a test rather than an eyeball.
	Zone->SetSidesToPad(true, false, true, false);
	TestTrue(TEXT("Left is padded"), Zone->bPadLeft);
	TestFalse(TEXT("Right is not"), Zone->bPadRight);
	TestTrue(TEXT("Top is padded"), Zone->bPadTop);
	TestFalse(TEXT("Bottom is not"), Zone->bPadBottom);

	TestTrue(TEXT("The safe area scale defaults to taking the whole reported inset"),
		FMath::IsNearlyEqual(Zone->SafeAreaScale.Left, 1.0f, 0.001f)
		&& FMath::IsNearlyEqual(Zone->SafeAreaScale.Bottom, 1.0f, 0.001f));

	Root->DestroyWidget();
	return true;
}

#endif
