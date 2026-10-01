// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayoutFragment.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "Core/Components/DreamText.h"
#include "DreamScopedWorld.h"
#include "DreamTextTestFont.h"

/*
 * Measurement under a constraint, end to end.
 *
 * A panel whose size depends on the space it is given -- a wrap box, a scale box set to fit -- cannot
 * answer "how big do you want to be" on its own. Both used to answer by reading their OWN current
 * width, which is the width their parent gave them on the PREVIOUS pass: measurement reading layout
 * output. The visible cost was a frame of disagreement. A wrap box newly placed in a vertical box
 * counted its rows against its authored width, the box allocated height for that row count, and only
 * the second pass agreed with itself.
 *
 * FDreamMeasureSpec had been written for this and was wired to nothing. These tests pin both halves:
 * the constraint reaching the panel, and the first pass already being right.
 */

namespace DreamMeasureSpecTestLocal
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
	FDreamWrapBoxMeasuresAgainstTheWidthItIsOfferedTest,
	"DreamGUI.Measure.AWrapBoxCountsItsRowsAgainstTheWidthItIsOfferedNotTheWidthItAlreadyHas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWrapBoxMeasuresAgainstTheWidthItIsOfferedTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeasureSpecTestLocal;
	FScopedGameWorld TestWorld;

	// Authored a thousand wide on purpose: that is the number the old code read, and it is wide enough
	// to hold all three items on one line, so the two answers cannot be confused for each other.
	UDreamWidget* WrapWidget = MakeWidget(TestWorld.World, nullptr, TEXT("Wrap"), 1000.0f, 40.0f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		MakeWidget(TestWorld.World, WrapWidget, *FString::Printf(TEXT("Item%d"), Index), 120.0f, 40.0f);
	}
	UDreamLayoutContainerWrapBox* Wrap = WrapWidget->CreateNewLayoutContainer<UDreamLayoutContainerWrapBox>();
	if (!TestNotNull(TEXT("Wrap box created"), Wrap))
	{
		return false;
	}

	// Three 120-wide items inside 300: two fit on a line, the third wraps, so two 40-tall rows.
	const FVector2f Constrained = Wrap->GetLayoutPreferredSize(
		FDreamMeasureSpec::AtMost(300.0f), FDreamMeasureSpec::Undefined());
	TestTrue(TEXT("Asked inside 300 the wrap box reports two rows"),
		FMath::IsNearlyEqual(Constrained.Y, 80.0f, 0.01f));
	TestTrue(TEXT("...and a width no wider than the two items that shared a row"),
		FMath::IsNearlyEqual(Constrained.X, 240.0f, 0.01f));

	// Unconstrained is the root boundary: nobody named a width, so the widget's own is the only number
	// there is, and all three items fit on it. This is the ONE place that read survives, by design.
	const FVector2f Unconstrained = Wrap->GetLayoutPreferredSize();
	TestTrue(TEXT("Asked with no constraint at all it falls back to its own width and reports one row"),
		FMath::IsNearlyEqual(Unconstrained.Y, 40.0f, 0.01f));

	WrapWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWrapBoxInAStackIsRightOnTheFirstPassTest,
	"DreamGUI.Measure.AWrapBoxInsideAVerticalBoxIsGivenTheHeightItsRowsNeedOnTheVeryFirstPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWrapBoxInAStackIsRightOnTheFirstPassTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeasureSpecTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 400.0f);
	UDreamWidget* WrapWidget = MakeWidget(TestWorld.World, Root, TEXT("Wrap"), 1000.0f, 40.0f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		MakeWidget(TestWorld.World, WrapWidget, *FString::Printf(TEXT("Item%d"), Index), 120.0f, 40.0f);
	}
	if (!TestNotNull(TEXT("Wrap box created"), WrapWidget->CreateNewLayoutContainer<UDreamLayoutContainerWrapBox>())
		|| !TestNotNull(TEXT("Vertical box created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		return false;
	}

	UDreamWidget::MarkLayoutForRebuild(Root);
	// ONE tick. The second one was never in doubt: by then the wrap box has been given 300 and reading
	// its own width happens to produce the right answer. The whole defect lived in the first.
	Manager->TickDreamUI(0.016f);

	TestTrue(TEXT("The vertical box gave the wrap box room for the rows it actually has"),
		FMath::IsNearlyEqual(WrapWidget->GetHeight(), 80.0f, 0.01f));
	TestTrue(TEXT("...and the full width, which is what it wraps against"),
		FMath::IsNearlyEqual(WrapWidget->GetWidth(), 300.0f, 0.01f));

	// And it has to stay there: a value that is right once and moves on the next tick is not converged.
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("The second pass does not move it"),
		FMath::IsNearlyEqual(WrapWidget->GetHeight(), 80.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScaleBoxFitsTheWidthItIsOfferedTest,
	"DreamGUI.Measure.AScaleBoxSetToFitDerivesItsScaleFromTheRoomItIsOfferedNotTheRoomItHas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScaleBoxFitsTheWidthItIsOfferedTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeasureSpecTestLocal;
	FScopedGameWorld TestWorld;

	UDreamWidget* BoxWidget = MakeWidget(TestWorld.World, nullptr, TEXT("ScaleBox"), 1000.0f, 1000.0f);
	MakeWidget(TestWorld.World, BoxWidget, TEXT("Content"), 100.0f, 100.0f);
	UDreamLayoutContainerScaleBox* ScaleBox = BoxWidget->CreateNewLayoutContainer<UDreamLayoutContainerScaleBox>();
	if (!TestNotNull(TEXT("Scale box created"), ScaleBox))
	{
		return false;
	}
	ScaleBox->SetStretch(EDreamScaleBoxStretch::ScaleToFitX);

	// 100-wide content offered 200: scale 2, so the 100-tall content measures 200 tall. Reading its own
	// 1000-wide rect instead gave a scale of 10 and an answer of 1000 -- an order of magnitude, from one
	// stale number.
	const FVector2f Constrained = ScaleBox->GetLayoutPreferredSize(
		FDreamMeasureSpec::AtMost(200.0f), FDreamMeasureSpec::Undefined());
	TestTrue(TEXT("Offered 200 the scale box reports the content scaled to fit it"),
		FMath::IsNearlyEqual(Constrained.Y, 200.0f, 0.01f));

	BoxWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeasureMemoSeparatesConstraintsTest,
	"DreamGUI.Measure.TheDesiredSizeMemoKeepsTheAnswersForTwoDifferentConstraintsApart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeasureMemoSeparatesConstraintsTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeasureSpecTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 900.0f, 400.0f);
	UDreamWidget* WrapWidget = MakeWidget(TestWorld.World, Root, TEXT("Wrap"), 1000.0f, 40.0f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		MakeWidget(TestWorld.World, WrapWidget, *FString::Printf(TEXT("Item%d"), Index), 120.0f, 40.0f);
	}
	if (!TestNotNull(TEXT("Wrap box created"), WrapWidget->CreateNewLayoutContainer<UDreamLayoutContainerWrapBox>())
		|| !TestNotNull(TEXT("Overlay created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>()))
	{
		return false;
	}

	// The memo used to be keyed on the widget alone. With constraints in play that is a correctness bug
	// rather than a missed optimisation: whichever constraint asked first would answer for all of them.
	UDreamPanelLayoutBase::FDesiredSizeMemoScope Memo(WrapWidget);
	const FVector2f Narrow = WrapWidget->GetLayoutContainer()->GetLayoutPreferredSize(
		FDreamMeasureSpec::AtMost(300.0f), FDreamMeasureSpec::Undefined());
	const FVector2f Wide = WrapWidget->GetLayoutContainer()->GetLayoutPreferredSize(
		FDreamMeasureSpec::AtMost(900.0f), FDreamMeasureSpec::Undefined());

	TestTrue(TEXT("The narrow ask wraps"), FMath::IsNearlyEqual(Narrow.Y, 80.0f, 0.01f));
	TestTrue(TEXT("The wide ask does not, even though the narrow one ran first"),
		FMath::IsNearlyEqual(Wide.Y, 40.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

/*
 * The case FDreamMeasureSpec names first: "how tall is this wrapping text at 300 wide". A paragraph that wraps at its box,
 * with no WrapTextAt, in a Fill slot of a vertical box. Its own width before the box first arranges it is whatever it was
 * authored at -- here ten, a few characters -- and measured at that width it was a column of single characters, hundreds
 * of pixels tall, until the sample's heading carried a WrapTextAt to get round it. Measured with the box's offer it is as
 * tall as the same paragraph laid out at the box's width, on the first pass, and stays so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWrappingTextInAStackIsRightOnTheFirstPassTest,
	"DreamGUI.Measure.AWrappingTextInsideAVerticalBoxIsGivenTheHeightItsLinesNeedAtTheBoxsWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWrappingTextInAStackIsRightOnTheFirstPassTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeasureSpecTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(TestWorld.World);
	const FText Paragraph = FText::FromString(TEXT("The quick brown fox jumps over the lazy dog, and then over the lazy dog once again."));

	// The reference: the same paragraph in a widget 300 wide, under a canvas of its own, measured at its own width.
	UDreamWidget* ReferenceRoot = MakeWidget(TestWorld.World, nullptr, TEXT("ReferenceRoot"), 300.0f, 600.0f);
	ReferenceRoot->AddComponent<UDreamCanvas>();
	UDreamWidget* ReferenceWidget = MakeWidget(TestWorld.World, ReferenceRoot, TEXT("Reference"), 300.0f, 20.0f);
	UDreamText* Reference = ReferenceWidget->CreateNewVisual<UDreamText>();
	Reference->SetFont(Font);
	Reference->SetText(Paragraph);
	const float Expected = Reference->GetPreferredHeight();
	const float OneLine = Font->GetLineHeight(Reference->GetFontSize());
	if (!TestTrue(FString::Printf(TEXT("At 300 wide the paragraph takes more than one line and fewer than ten (%.1f, a line %.1f)"), Expected, OneLine),
		Expected > OneLine * 1.5f && Expected < OneLine * 10.0f))
	{
		ReferenceRoot->DestroyWidget();
		return false;
	}

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 600.0f);
	Root->AddComponent<UDreamCanvas>();
	// Ten wide, as authored: the width the old measurement read.
	UDreamWidget* TextWidget = MakeWidget(TestWorld.World, Root, TEXT("Paragraph"), 10.0f, 20.0f);
	UDreamText* Text = TextWidget->CreateNewVisual<UDreamText>();
	Text->SetFont(Font);
	Text->SetText(Paragraph);
	if (!TestNotNull(TEXT("Vertical box created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		Root->DestroyWidget();
		ReferenceRoot->DestroyWidget();
		return false;
	}

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestTrue(FString::Printf(TEXT("The box gave the paragraph the height its lines need at the box's width (%.1f, expected %.1f)"), TextWidget->GetHeight(), Expected),
		FMath::IsNearlyEqual(static_cast<float>(TextWidget->GetHeight()), Expected, 0.5f));
	TestTrue(TEXT("...and the box's whole width, which it wraps at"),
		FMath::IsNearlyEqual(static_cast<float>(TextWidget->GetWidth()), 300.0f, 0.01f));

	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("The second pass does not move it"),
		FMath::IsNearlyEqual(static_cast<float>(TextWidget->GetHeight()), Expected, 0.5f));

	// A WrapTextAt of its own is a width of its own: such a paragraph is measured at it, the offer aside.
	Text->SetWrapTextAt(100.0f);
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	TestTrue(FString::Printf(TEXT("With WrapTextAt 100 it is taller than at the box's width (%.1f)"), TextWidget->GetHeight()),
		TextWidget->GetHeight() > Expected + OneLine * 0.5f);

	Root->DestroyWidget();
	ReferenceRoot->DestroyWidget();
	return true;
}

#endif
