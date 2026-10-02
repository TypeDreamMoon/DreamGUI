// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "Interaction/DreamContentWidget.h"
#include "DreamScopedWorld.h"

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
	FDreamScaleBoxStretchDirectionMeasureTest,
	"DreamGUI.Panel.AScaleBoxMeasuresItsContentAtTheScaleItsStretchDirectionAllows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The scale box bounded its scale by StretchDirection when it arranged but not when it measured. Set to fit
 * its width and to shrink only, it measured 100x50 art in a 400-wide column at four times its size, 200 tall,
 * then drew it at scale one and left 150 of empty space below it; set to grow only, it measured 800x100 art
 * squeezed to 50 tall and then drew it full size, cut off by its own clip. SScaleBox folds the direction into
 * the one scale its desired size comes from. This checks both against what the art is drawn at.
 */
bool FDreamScaleBoxStretchDirectionMeasureTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 600.0f);
	UDreamWidget* BoxWidget = MakeWidget(TestWorld.World, Root, TEXT("ScaleBox"), 50.0f, 50.0f);
	UDreamWidget* Art = MakeWidget(TestWorld.World, BoxWidget, TEXT("Art"), 100.0f, 50.0f);
	UDreamWidget* Below = MakeWidget(TestWorld.World, Root, TEXT("Below"), 100.0f, 30.0f);
	UDreamLayoutContainerScaleBox* ScaleBox = BoxWidget->CreateNewLayoutContainer<UDreamLayoutContainerScaleBox>();
	if (!TestNotNull(TEXT("Scale box created"), ScaleBox)
		|| !TestNotNull(TEXT("Column created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		Root->DestroyWidget();
		return false;
	}
	ScaleBox->SetStretch(EDreamScaleBoxStretch::ScaleToFitX);
	ScaleBox->SetStretchDirection(EDreamScaleBoxStretchDirection::DownOnly);
	auto TopOf = [](const UDreamWidget* Widget)
	{
		return Widget->GetParent()->GetHeight() * 0.5 - Widget->GetAnchoredPosition().Y - Widget->GetHeight() * (1.0 - Widget->GetPivot().Y);
	};

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	// Fitting 100 into 400 asks for four times; shrinking only allows one.
	TestEqual(TEXT("Shrinking only, the box is as tall as the art at scale one"), BoxWidget->GetHeight(), 50.0f);
	TestEqual(TEXT("...so what follows it starts right under the art"), TopOf(Below), 50.0, 0.01);

	// Fitting 800 into 400 asks for half; growing only allows one.
	Art->SetWidth(800.0f);
	Art->SetHeight(100.0f);
	ScaleBox->SetStretchDirection(EDreamScaleBoxStretchDirection::UpOnly);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("Growing only, the box is as tall as the art at scale one"), BoxWidget->GetHeight(), 100.0f);
	TestEqual(TEXT("...and what follows it starts right under the art"), TopOf(Below), 100.0, 0.01);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGridColumnsIndependentOfChildOrderTest,
	"DreamGUI.Panel.AGridSizesItsColumnsTheSameWhicheverOrderItsChildrenComeIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A child spanning two columns spread only its SHORTFALL across them, against the columns as the children
 * before it had left them, so the columns depended on sibling order: a 200-wide child over two columns and a
 * 150-wide one in the first column made them 150 and 100 in one order and 175 and 25 in the other, and the
 * ZOrder re-sort ahead of placement reorders exactly that. SGridPanel takes, per column, the largest share any
 * child asks of it. This checks both orders, and a re-sort, come out at UMG's 150 and 100.
 */
bool FDreamGridColumnsIndependentOfChildOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	// Spanning: two columns of the first row, 200 wide. First: the first column of the second row, 150 wide.
	// Second: the second column of the second row, 20 wide and filling its cell, so its width IS the column's.
	for (const bool bSpanningFirst : { true, false })
	{
		const FString Order = bSpanningFirst ? TEXT("spanning child first") : TEXT("spanning child second");
		UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 200.0f);
		UDreamWidget* Spanning = nullptr;
		UDreamWidget* First = nullptr;
		if (bSpanningFirst)
		{
			Spanning = MakeWidget(TestWorld.World, Root, TEXT("Spanning"), 200.0f, 40.0f);
			First = MakeWidget(TestWorld.World, Root, TEXT("First"), 150.0f, 40.0f);
		}
		else
		{
			First = MakeWidget(TestWorld.World, Root, TEXT("First"), 150.0f, 40.0f);
			Spanning = MakeWidget(TestWorld.World, Root, TEXT("Spanning"), 200.0f, 40.0f);
		}
		UDreamWidget* Second = MakeWidget(TestWorld.World, Root, TEXT("Second"), 20.0f, 40.0f);
		if (!TestNotNull(*FString::Printf(TEXT("%s: grid created"), *Order), Root->CreateNewLayoutContainer<UDreamLayoutContainerGridPanel>()))
		{
			Root->DestroyWidget();
			return false;
		}
		UDreamPanelSlot* SpanningSlot = Spanning->GetPanelSlot();
		UDreamPanelSlot* FirstSlot = First->GetPanelSlot();
		UDreamPanelSlot* SecondSlot = Second->GetPanelSlot();
		if (!SpanningSlot || !FirstSlot || !SecondSlot)
		{
			AddError(FString::Printf(TEXT("%s: the grid handed out no slot"), *Order));
			Root->DestroyWidget();
			return false;
		}
		SpanningSlot->SetColumnSpan(2);
		FirstSlot->SetRow(1);
		SecondSlot->SetRow(1);
		SecondSlot->SetColumn(1);

		UDreamWidget::MarkLayoutForRebuild(Root);
		Manager->TickDreamUI(0.016f);
		Manager->TickDreamUI(0.016f);
		TestEqual(*FString::Printf(TEXT("%s: the first column is the widest single child in it"), *Order), First->GetWidth(), 150.0f);
		TestEqual(*FString::Printf(TEXT("%s: the second column is the spanning child's half"), *Order), Second->GetWidth(), 100.0f);

		// Raising the spanning child's ZOrder moves it to the back of the sibling list, and the next relayout
		// for any other reason must come out the same.
		SpanningSlot->SetZOrder(1);
		Manager->TickDreamUI(0.016f);
		Root->SetHeight(210.0f);
		Manager->TickDreamUI(0.016f);
		TestEqual(*FString::Printf(TEXT("%s: re-sorted, the first column is unchanged"), *Order), First->GetWidth(), 150.0f);
		TestEqual(*FString::Printf(TEXT("%s: re-sorted, the second column is unchanged"), *Order), Second->GetWidth(), 100.0f);

		Root->DestroyWidget();
	}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSafeZoneScalesThePlatformMarginTest,
	"DreamGUI.Panel.ASafeZoneTakesThePlatformMarginInScreenPixelsAndAppliesItInItsOwnUnits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Outside the editor the platform's safe margin comes from GetSafeZoneSize in pixels of the display, and the safe
 * zone added those pixels to its padding as they were, in its own units. Under a canvas scaled to twice its reference
 * resolution that is twice the inset UMG applies: SSafeZone divides the margin by its geometry's scale. That branch is
 * compiled out of an editor build, so this checks the two pieces it is now made of: the arithmetic -- each side over
 * its axis's scale, rounded to a whole unit, SafeAreaScale after -- and the scale of a widget under a screen-space
 * canvas, the scaler's and its own.
 */
bool FDreamSafeZoneScalesThePlatformMarginTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	using Zone = UDreamLayoutContainerSafeZone;
	auto MarginIs = [](const FMargin& InActual, const FMargin& InExpected)
	{
		return FMath::IsNearlyEqual(InActual.Left, InExpected.Left, 0.001f)
			&& FMath::IsNearlyEqual(InActual.Top, InExpected.Top, 0.001f)
			&& FMath::IsNearlyEqual(InActual.Right, InExpected.Right, 0.001f)
			&& FMath::IsNearlyEqual(InActual.Bottom, InExpected.Bottom, 0.001f);
	};

	TestTrue(TEXT("Each side is divided by its own axis's scale"),
		MarginIs(Zone::ScalePlatformSafeMargin(FMargin(40.0f, 30.0f, 80.0f, 60.0f), FVector2D(2.0, 3.0), FMargin(1.0f)),
			FMargin(20.0f, 10.0f, 40.0f, 20.0f)));
	TestTrue(TEXT("...and rounded to a whole unit, as SSafeZone rounds it"),
		MarginIs(Zone::ScalePlatformSafeMargin(FMargin(41.0f, 0.0f, 0.0f, 0.0f), FVector2D(2.0, 2.0), FMargin(1.0f)),
			FMargin(21.0f, 0.0f, 0.0f, 0.0f)));
	// Rounded first and scaled after: 30 pixels at a scale of two is 15 units, half of which is 7.5. Scaling first
	// would have rounded the 7.5 to 8.
	TestTrue(TEXT("SafeAreaScale applies after the rounding, where SSafeZone applies it"),
		MarginIs(Zone::ScalePlatformSafeMargin(FMargin(0.0f, 30.0f, 0.0f, 0.0f), FVector2D(2.0, 2.0), FMargin(1.0f, 0.5f, 1.0f, 1.0f)),
			FMargin(0.0f, 7.5f, 0.0f, 0.0f)));
	TestTrue(TEXT("A scale of zero counts as one"),
		MarginIs(Zone::ScalePlatformSafeMargin(FMargin(10.0f, 10.0f, 10.0f, 10.0f), FVector2D(0.0, 0.0), FMargin(1.0f)),
			FMargin(10.0f, 10.0f, 10.0f, 10.0f)));

	// A 2560 x 1440 screen under a canvas that scales a 1280 x 720 reference resolution up to it: every unit of the
	// canvas is two pixels.
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 100.0f, 100.0f);
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("A canvas on the root"), Canvas))
	{
		Root->DestroyWidget();
		return false;
	}
	// The viewport last, the order a headless canvas needs: setting the render mode applies the headless world's
	// two-pixel fallback, and the substitute is what undoes it.
	Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	Canvas->SetScaleMode(EDreamCanvasScaleMode::ScaleWithScreenSize);
	Canvas->SetReferenceResolution(FVector2D(1280.0, 720.0));
	Canvas->SetViewportSizeOverride(FIntPoint(2560, 1440));
	if (!TestTrue(TEXT("The canvas sizes its root to the reference resolution"),
		FMath::IsNearlyEqual(Root->GetWidth(), 1280.0f, 0.01f) && FMath::IsNearlyEqual(Root->GetHeight(), 720.0f, 0.01f)))
	{
		Root->DestroyWidget();
		return false;
	}
	UDreamWidget* Plain = MakeWidget(TestWorld.World, Root, TEXT("Plain"), 300.0f, 200.0f);
	UDreamWidget* Halved = MakeWidget(TestWorld.World, Root, TEXT("Halved"), 300.0f, 200.0f);
	Halved->SetRelativeScale(FVector(1.0, 0.5, 0.25));

	TestTrue(TEXT("A unit of a widget straight under the canvas is two pixels across and down"),
		Zone::GetScreenPixelsPerUnit(Plain).Equals(FVector2D(2.0, 2.0), 0.001));
	TestTrue(TEXT("...and a widget's own scale multiplies in, per axis"),
		Zone::GetScreenPixelsPerUnit(Halved).Equals(FVector2D(1.0, 0.5), 0.001));
	TestTrue(TEXT("So a 48-pixel notch and a 64-pixel home bar pad the plain widget by 24 and 32"),
		MarginIs(Zone::ScalePlatformSafeMargin(FMargin(48.0f, 0.0f, 48.0f, 64.0f), Zone::GetScreenPixelsPerUnit(Plain), FMargin(1.0f)),
			FMargin(24.0f, 0.0f, 24.0f, 32.0f)));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWrapBoxFillAlignmentTest,
	"DreamGUI.Panel.AWrapBoxSetToFillStretchesEachLineInProportionToItsSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A wrap box set to Fill was laid out exactly as one set to Left. SWrapBox's HAlign_Fill multiplies every slot of a
 * line by one factor, (allotted width - gaps) / (line length - gaps), so the line spans the box and the gaps stay as
 * they were. A 300-wide box holding slots of 80 and 40 with a gap of 10 grows them by 290 / 120, to 193.3 and 96.7,
 * the second starting at 203.3; what the box measures does not change. Also checked: a slot that asks for a line of
 * its own takes exactly the line, as SWrapBox sizes it, and not its own width when that is more.
 */
bool FDreamWrapBoxFillAlignmentTest::RunTest(const FString& Parameters)
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
	UDreamWidget* Second = MakeWidget(TestWorld.World, Root, TEXT("Second"), 40.0f, 30.0f);
	// Wider than the box, so it always takes a line of its own and leaves the first line to the two above.
	UDreamWidget* Wide = MakeWidget(TestWorld.World, Root, TEXT("Wide"), 400.0f, 30.0f);
	UDreamLayoutContainerWrapBox* Box = Root->CreateNewLayoutContainer<UDreamLayoutContainerWrapBox>();
	UDreamPanelSlot* WideSlot = Wide->GetPanelSlot();
	if (!TestNotNull(TEXT("Wrap box created"), Box) || !TestNotNull(TEXT("The wide child has a slot"), WideSlot))
	{
		Root->DestroyWidget();
		return false;
	}
	auto LeftOf = [](const UDreamWidget* InWidget)
	{
		return InWidget->GetParent()->GetWidth() * 0.5 + InWidget->GetAnchoredPosition().X - InWidget->GetWidth() * InWidget->GetPivot().X;
	};
	Box->SetSpacing(FVector2D(10.0, 0.0));
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	const FVector2f MeasuredAsLeft = Box->GetLayoutPreferredSize();
	TestEqual(TEXT("Fixture: aligned left, the second slot keeps its own width"), Second->GetWidth(), 40.0f, 0.01f);

	Box->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("The first slot grows by 290 / 120"), First->GetWidth(), 193.333f, 0.01f);
	TestEqual(TEXT("...and the second by the same factor"), Second->GetWidth(), 96.667f, 0.01f);
	TestEqual(TEXT("The first starts at the edge"), LeftOf(First), 0.0, 0.01);
	TestEqual(TEXT("The second starts after the first's new width and a gap that is still 10"), LeftOf(Second), 203.333, 0.01);
	TestTrue(TEXT("What the box measures does not change"),
		FMath::IsNearlyEqual(Box->GetLayoutPreferredSize().X, MeasuredAsLeft.X, 0.01f)
		&& FMath::IsNearlyEqual(Box->GetLayoutPreferredSize().Y, MeasuredAsLeft.Y, 0.01f));

	// The slot wider than the line asks for a line of its own below a wrap length of 1000, which this box is.
	Box->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Left);
	WideSlot->SetFillSpanWhenLessThan(1000.0f);
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("A slot spanning the line takes exactly the line"), Wide->GetWidth(), 300.0f, 0.01f);
	TestEqual(TEXT("...from its start"), LeftOf(Wide), 0.0, 0.01);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScaleBoxSafeZoneScaleTest,
	"DreamGUI.Panel.ASafeZoneScaleShrinksByTheLargerSideOfTheSafeMargin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * UMG's ScaleBySafeZone and UserSpecifiedWithClipping were missing from the stretch enum, and the setter turned any
 * value it did not know into ScaleToFit. SScaleBox's safe-zone scale is 1 - max(max(L, R) / W, max(T, B) / H) of the
 * game viewport's safe margin: a 192 / 27 / 96 / 54 margin on a 1920 x 1080 screen takes a tenth off the width and a
 * twentieth off the height, so the content is drawn at 0.9. The arrangement uses that one cached scale whatever room
 * the box has. UserSpecifiedWithClipping scales as UserSpecified does, and clips as the fitting modes do.
 */
bool FDreamScaleBoxSafeZoneScaleTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	using Box = UDreamLayoutContainerScaleBox;
	TestEqual(TEXT("The larger side of the margin decides: 192 of 1920 is more than 54 of 1080"),
		Box::ComputeSafeZoneScale(FMargin(192.0f, 27.0f, 96.0f, 54.0f), FVector2D(1920.0, 1080.0)), 0.9f, 0.0001f);
	TestEqual(TEXT("A deeper margin top or bottom decides the other way"),
		Box::ComputeSafeZoneScale(FMargin(0.0f, 216.0f, 0.0f, 0.0f), FVector2D(1920.0, 1080.0)), 0.8f, 0.0001f);
	TestEqual(TEXT("No viewport leaves the scale at one"),
		Box::ComputeSafeZoneScale(FMargin(192.0f, 27.0f, 96.0f, 54.0f), FVector2D::ZeroVector), 1.0f);
	TestEqual(TEXT("A margin wider than the screen scales to nothing, never below"),
		Box::ComputeSafeZoneScale(FMargin(4000.0f, 0.0f, 0.0f, 0.0f), FVector2D(1920.0, 1080.0)), 0.0f);

	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 100.0f, 50.0f);
	UDreamLayoutContainerScaleBox* ScaleBox = Root->CreateNewLayoutContainer<UDreamLayoutContainerScaleBox>();
	if (!TestNotNull(TEXT("Scale box created"), ScaleBox))
	{
		Root->DestroyWidget();
		return false;
	}
	auto ScaleOfFirstChild = [](const FDreamFragment& InFragment)
	{
		return InFragment.Children.Num() > 0 ? InFragment.Children[0].LayoutScale.X : -1.0f;
	};

	ScaleBox->SetStretch(EDreamScaleBoxStretch::ScaleBySafeZone);
	TestTrue(TEXT("The new value survives the setter rather than turning into ScaleToFit"),
		ScaleBox->Stretch == EDreamScaleBoxStretch::ScaleBySafeZone);
	// The scale is the platform's, read from the game viewport; a run with no game viewport reads one. Either way
	// the room in the box takes no part: fitting this child would have tripled it.
	TestEqual(TEXT("ScaleBySafeZone draws at the safe-zone scale, not at a fit"),
		ScaleOfFirstChild(ScaleBox->Arrange()), ScaleBox->GetSafeZoneScale(), 0.001f);
	TestTrue(TEXT("...and does not clip"), Root->GetClipping() == EDreamWidgetClipping::Inherit);

	ScaleBox->SetUserSpecifiedScale(2.0f);
	ScaleBox->SetStretch(EDreamScaleBoxStretch::UserSpecifiedWithClipping);
	TestEqual(TEXT("UserSpecifiedWithClipping scales by the stated scale"), ScaleOfFirstChild(ScaleBox->Arrange()), 2.0f, 0.001f);
	TestTrue(TEXT("...and clips to the box"), Root->GetClipping() == EDreamWidgetClipping::ClipToBounds);
	// StretchDirection bounds a scale worked out from the room, not one the box is told.
	ScaleBox->SetStretchDirection(EDreamScaleBoxStretchDirection::DownOnly);
	TestEqual(TEXT("A stated scale is not bounded by the stretch direction"), ScaleOfFirstChild(ScaleBox->Arrange()), 2.0f, 0.001f);

	ScaleBox->SetStretch(EDreamScaleBoxStretch::UserSpecified);
	TestTrue(TEXT("Plain UserSpecified does not clip"), Root->GetClipping() == EDreamWidgetClipping::Inherit);
	TestTrue(TEXT("Child survived the arrangement"), IsValid(Child));
	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScaleBoxIgnoringInheritedScaleMeasureTest,
	"DreamGUI.Panel.AScaleBoxIgnoringItsInheritedScaleAsksForTheRoomItsContentTakes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The measurement divided by the inherited scale for UserSpecified and the two FitX/FitY modes only, while the
 * arrangement divided every mode. A box set to None under a parent scaled by two, ignoring that scale, drew its
 * 100 x 50 art at half size and still asked for the room of the full size. SScaleBox divides whatever scale it worked
 * out, in both places.
 */
bool FDreamScaleBoxIgnoringInheritedScaleMeasureTest::RunTest(const FString& Parameters)
{
	using namespace DreamPanelWidgetParityTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 400.0f);
	UDreamWidget* BoxWidget = MakeWidget(TestWorld.World, Root, TEXT("ScaleBox"), 300.0f, 200.0f);
	UDreamWidget* Art = MakeWidget(TestWorld.World, BoxWidget, TEXT("Art"), 100.0f, 50.0f);
	UDreamLayoutContainerScaleBox* ScaleBox = BoxWidget->CreateNewLayoutContainer<UDreamLayoutContainerScaleBox>();
	if (!TestNotNull(TEXT("Scale box created"), ScaleBox))
	{
		Root->DestroyWidget();
		return false;
	}
	Root->SetRelativeScale(FVector(1.0, 2.0, 2.0));
	ScaleBox->SetStretch(EDreamScaleBoxStretch::None);
	ScaleBox->SetIgnoreInheritedScale(true);

	const FDreamFragment Arranged = ScaleBox->Arrange();
	TestTrue(TEXT("The art is drawn at half size, undoing the parent's two"),
		Arranged.Children.Num() > 0 && FMath::IsNearlyEqual(Arranged.Children[0].LayoutScale.X, 0.5f, 0.001f)
		&& FMath::IsNearlyEqual(Arranged.Children[0].LayoutScale.Y, 0.5f, 0.001f));
	const FVector2f Preferred = ScaleBox->GetLayoutPreferredSize();
	TestEqual(TEXT("...and the box asks for the width it draws the art at"), Preferred.X, 50.0f, 0.01f);
	TestEqual(TEXT("...and the height"), Preferred.Y, 25.0f, 0.01f);

	TestTrue(TEXT("Art survived the arrangement"), IsValid(Art));
	Root->DestroyWidget();
	return true;
}

#endif
