// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/World.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "DreamScopedWorld.h"

/*
 * Where a panel puts things and how big it says they are, held to the numbers UMG produces for the same
 * scene, and to the one number this plugin adds: a layout that settles in the pass that was asked for.
 *
 * Every scene is built from fixed-size widgets, so nothing here depends on a font.
 */

namespace DreamLayoutConformanceTestLocal
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

	/** A panel-arranged child's left edge, from the left edge of the parent that arranged it. */
	double LeftEdgeOf(const UDreamWidget* Child)
	{
		return Child->GetParent()->GetWidth() * 0.5 + Child->GetAnchoredPosition().X - Child->GetWidth() * Child->GetPivot().X;
	}

	/** A panel-arranged child's top edge, down from the top edge of the parent that arranged it. */
	double TopEdgeOf(const UDreamWidget* Child)
	{
		return Child->GetParent()->GetHeight() * 0.5 - Child->GetAnchoredPosition().Y - Child->GetHeight() * (1.0 - Child->GetPivot().Y);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFillSlotMeasuredAtItsShareTest,
	"DreamGUI.Layout.AWrapBoxInAFillSlotOfARowIsMeasuredAtItsShareOfTheRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A horizontal box measured its children with no limit along its own axis, so a wrap box in its Fill slot
 * answered "how tall am I" from the width it already had: ten, as authored here, which puts every item on a
 * line of its own, 120 tall. The vertical box above gave the row that height, the row then gave the wrap box
 * its real share of 500, where all three items fit on one line -- and nothing measured the row again, so it
 * stayed 120 tall around a 40-tall line. This checks the row is one line tall after one tick, which needs the
 * Fill child measured against its share of what the Auto children leave.
 */
bool FDreamFillSlotMeasuredAtItsShareTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 600.0f, 400.0f);
	UDreamWidget* Row = MakeWidget(TestWorld.World, Root, TEXT("Row"), 50.0f, 20.0f);
	MakeWidget(TestWorld.World, Row, TEXT("Icon"), 100.0f, 40.0f);
	UDreamWidget* Wrap = MakeWidget(TestWorld.World, Row, TEXT("Wrap"), 10.0f, 40.0f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		MakeWidget(TestWorld.World, Wrap, *FString::Printf(TEXT("Item%d"), Index), 120.0f, 40.0f);
	}
	if (!TestNotNull(TEXT("Wrap box created"), Wrap->CreateNewLayoutContainer<UDreamLayoutContainerWrapBox>())
		|| !TestNotNull(TEXT("Row created"), Row->CreateNewLayoutContainer<UDreamLayoutContainerHorizontalBox>())
		|| !TestNotNull(TEXT("Column created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		Root->DestroyWidget();
		return false;
	}
	UDreamPanelSlot* WrapSlot = Wrap->GetPanelSlot();
	if (!TestNotNull(TEXT("The row handed the wrap box a slot"), WrapSlot))
	{
		Root->DestroyWidget();
		return false;
	}
	WrapSlot->SetSizeRule(EDreamPanelSizeRule::Fill);

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("The row is as tall as the one line its wrap box holds at its share"), Row->GetHeight(), 40.0f);
	TestEqual(TEXT("...the share being the row less the icon"), Wrap->GetWidth(), 500.0f);
	TestEqual(TEXT("...and the wrap box is that one line tall"), Wrap->GetHeight(), 40.0f);

	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("The next tick does not move it"), Row->GetHeight(), 40.0f);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCurrentSizeReaderIsMeasuredAgainTest,
	"DreamGUI.Layout.AChildMeasuredFromItsOwnSizeIsMeasuredAgainOnceItsPanelHasResizedIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A grid asks its children how big they want to be with no constraint -- its tracks are made of the answers
 * -- so a wrap box in a Fill column can only answer from the width it already has. Narrowing the grid from
 * 600 to 300 had the wrap box answer for 600 (one line, 40 tall), the grid give it the 300-wide column and a
 * row sized for that one line, and the write stop at the grid, so nothing asked again: two lines of items
 * crammed into one line's height, for good. This checks that the grid goes round once more after resizing a
 * child that answered from its own size -- the row opens to the two lines -- and only once.
 */
bool FDreamCurrentSizeReaderIsMeasuredAgainTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 600.0f, 400.0f);
	UDreamWidget* Wrap = MakeWidget(TestWorld.World, Root, TEXT("Wrap"), 10.0f, 40.0f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		MakeWidget(TestWorld.World, Wrap, *FString::Printf(TEXT("Item%d"), Index), 120.0f, 40.0f);
	}
	UDreamLayoutContainerGridPanel* Grid = Root->CreateNewLayoutContainer<UDreamLayoutContainerGridPanel>();
	if (!TestNotNull(TEXT("Wrap box created"), Wrap->CreateNewLayoutContainer<UDreamLayoutContainerWrapBox>())
		|| !TestNotNull(TEXT("Grid created"), Grid))
	{
		Root->DestroyWidget();
		return false;
	}
	Grid->SetColumnFill({ 1.0f });

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("At 600 the wrap box fills the column on one line"), Wrap->GetSize(), FVector2D(600.0, 40.0));

	Root->SetWidth(300.0f);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("At 300 the row opens to the two lines the items now take"), Wrap->GetSize(), FVector2D(300.0, 80.0));
	TestEqual(TEXT("...for one pass more than the resize itself, not until the pass cap"), Manager->GetLastLayoutPassCount(), 2);

	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("The next tick does not move it"), Wrap->GetSize(), FVector2D(300.0, 80.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCollapsedPanelTakesNoRoomTest,
	"DreamGUI.Layout.APanelWhoseChildrenAreAllCollapsedTakesNoRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A panel that measured zero was read as having no opinion, and the measurement fell back to its authored
 * rect: a list whose only row was collapsed went on holding its designer height of 200 open, so the footer
 * below it sat at 240 where UMG puts it at 40 -- while the same list given one pixel of padding top and
 * bottom did collapse, to two. This checks the list takes no room and the footer follows the header.
 */
bool FDreamCollapsedPanelTakesNoRoomTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 400.0f);
	MakeWidget(TestWorld.World, Root, TEXT("Header"), 100.0f, 40.0f);
	UDreamWidget* List = MakeWidget(TestWorld.World, Root, TEXT("List"), 100.0f, 200.0f);
	UDreamWidget* Row = MakeWidget(TestWorld.World, List, TEXT("Row"), 100.0f, 30.0f);
	UDreamWidget* Footer = MakeWidget(TestWorld.World, Root, TEXT("Footer"), 100.0f, 30.0f);
	if (!TestNotNull(TEXT("List created"), List->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>())
		|| !TestNotNull(TEXT("Column created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		Root->DestroyWidget();
		return false;
	}
	Row->SetVisibility(EDreamWidgetVisibility::Collapsed);

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("The list with nothing visible in it is no height at all"), List->GetHeight(), 0.0f);
	TestEqual(TEXT("...so the footer sits straight under the header"), TopEdgeOf(Footer), 40.0, 0.01);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSizeBoxWithCollapsedContentTakesNoRoomTest,
	"DreamGUI.Layout.ASizeBoxTakesNoRoomOnlyWhileItsContentIsCollapsed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A size box applied its height override whether or not its content was taking part, so a box overridden to
 * 48 kept 48 open around a collapsed child. UMG's SBox wants no room at all while its content is collapsed,
 * overrides included -- and only then: content that is there but places itself (bIgnoreLayout) leaves the box
 * its override, as an empty box keeps its own. This checks the box is no height while the content is collapsed,
 * with the widget below it moved up to the top, and the override's height again once the content is back but
 * placing itself.
 */
bool FDreamSizeBoxWithCollapsedContentTakesNoRoomTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 400.0f);
	UDreamWidget* BoxWidget = MakeWidget(TestWorld.World, Root, TEXT("Box"), 100.0f, 100.0f);
	UDreamWidget* Content = MakeWidget(TestWorld.World, BoxWidget, TEXT("Content"), 50.0f, 50.0f);
	UDreamWidget* Below = MakeWidget(TestWorld.World, Root, TEXT("Below"), 100.0f, 30.0f);
	UDreamLayoutContainerSizeBox* Box = BoxWidget->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>();
	if (!TestNotNull(TEXT("Size box created"), Box)
		|| !TestNotNull(TEXT("Column created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		Root->DestroyWidget();
		return false;
	}
	Box->SetHeightOverride(48.0f);
	Box->SetOverrideHeight(true);

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("With its content showing the box is the override's height"), BoxWidget->GetHeight(), 48.0f);

	Content->SetVisibility(EDreamWidgetVisibility::Collapsed);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("With its content collapsed the box is no height at all"), BoxWidget->GetHeight(), 0.0f);
	TestEqual(TEXT("...so what follows it moves up to the top"), TopEdgeOf(Below), 0.0, 0.01);

	Content->SetVisibility(EDreamWidgetVisibility::Visible);
	Content->SetIgnoreLayout(true);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("With its content showing but placing itself the box is the override's height again"),
		BoxWidget->GetHeight(), 48.0f);
	TestEqual(TEXT("...so what follows it sits below it"), TopEdgeOf(Below), 48.0, 0.01);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDimensionChangeAnnouncedOnceTest,
	"DreamGUI.Layout.AResizeIsAnnouncedToItsDimensionListenersOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * MarkDimensionChanged broadcast OnDimensionChangedEvent at its start and then again from
 * Call_DimensionsChanged at its end, so every listener -- each behaviour's OnUIDimensionsChanged, a list view
 * rebinding its visible rows -- ran twice for one resize. This checks one SetWidth reaches a listener once,
 * and as a width change.
 */
bool FDreamDimensionChangeAnnouncedOnceTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Widget = MakeWidget(TestWorld.World, nullptr, TEXT("Widget"), 100.0f, 50.0f);
	int32 Announcements = 0;
	bool bAnnouncedWidth = false;
	const FDelegateHandle Listening = Widget->GetDimensionChangedEvent().AddLambda(
		[&Announcements, &bAnnouncedWidth](bool, bool bWidthChanged, bool)
		{
			++Announcements;
			bAnnouncedWidth |= bWidthChanged;
		});

	Widget->SetWidth(110.0f);
	TestEqual(TEXT("One resize is announced once"), Announcements, 1);
	TestTrue(TEXT("...as a change of width"), bAnnouncedWidth);

	Widget->GetDimensionChangedEvent().Remove(Listening);
	Widget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCentredChildWithUnevenPaddingTest,
	"DreamGUI.Layout.ACentredChildWithUnevenSlotPaddingSitsWhereUMGPutsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Slate centres a child in the whole slot and then moves it by the difference between the two paddings,
 * (Area - Child) / 2 + Pre - Post (LayoutUtils.h, AlignChild). This plugin centred it inside the padded box
 * instead, which lands (Pre - Post) / 2 short: a 100-wide child in a 200-wide overlay with 20 of left padding
 * sat at 60 where UMG puts it at 70. This checks both axes against UMG, and that the right-to-left mirror of
 * the result is UMG's right-to-left answer, where the two paddings trade places.
 */
bool FDreamCentredChildWithUnevenPaddingTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 200.0f, 100.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 100.0f, 40.0f);
	if (!TestNotNull(TEXT("Overlay created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>()))
	{
		Root->DestroyWidget();
		return false;
	}
	UDreamPanelSlot* Slot = Child->GetPanelSlot();
	if (!TestNotNull(TEXT("The overlay handed the child a slot"), Slot))
	{
		Root->DestroyWidget();
		return false;
	}
	Slot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
	Slot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
	Slot->SetPadding(FMargin(20.0f, 10.0f, 0.0f, 0.0f));

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	// (200 - 100) / 2 + 20 - 0 across, and (100 - 40) / 2 + 10 - 0 down.
	TestEqual(TEXT("Across, the child sits where UMG centres it"), LeftEdgeOf(Child), 70.0, 0.01);
	TestEqual(TEXT("...and down"), TopEdgeOf(Child), 40.0, 0.01);
	TestEqual(TEXT("...at its own size"), Child->GetSize(), FVector2D(100.0, 40.0));

	Root->SetFlowDirectionPreference(EDreamFlowDirectionPreference::RightToLeft);
	Manager->TickDreamUI(0.016f);
	// Right to left the paddings trade places: (200 - 100) / 2 + 0 - 20.
	TestEqual(TEXT("Right to left it sits where UMG centres it with the paddings swapped"), LeftEdgeOf(Child), 30.0, 0.01);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBorderCentresWithItsPaddingTest,
	"DreamGUI.Layout.ABorderCentresItsContentWithItsPaddingAsSBorderDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * UBorder::Padding is what SBorder holds as its content slot's padding, so a centred child is centred over the whole
 * border and then moved by the difference between the margins, (Width - Child) / 2 + Pre - Post. The border took its
 * padding off its rect first and centred inside what was left, which lands (Pre - Post) / 2 short: a 100-wide child
 * in a 300-wide border with 20 of left padding sat at 110 where UMG puts it at 120. This checks UMG's numbers with the
 * border's padding alone, with the content slot's padding added to it, and right to left, where the margins trade
 * places.
 */
bool FDreamBorderCentresWithItsPaddingTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* BorderWidget = MakeWidget(TestWorld.World, nullptr, TEXT("Border"), 300.0f, 100.0f);
	UDreamWidget* Content = MakeWidget(TestWorld.World, BorderWidget, TEXT("Content"), 100.0f, 40.0f);
	UDreamLayoutContainerBorder* Border = BorderWidget->CreateNewLayoutContainer<UDreamLayoutContainerBorder>();
	if (!TestNotNull(TEXT("Border created"), Border) || !TestNotNull(TEXT("The border handed its content a slot"), Content->GetPanelSlot()))
	{
		BorderWidget->DestroyWidget();
		return false;
	}
	Border->SetPadding(FMargin(20.0f, 0.0f, 0.0f, 0.0f));
	Border->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
	Border->SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);

	UDreamWidget::MarkLayoutForRebuild(BorderWidget);
	Manager->TickDreamUI(0.016f);
	// (300 - 100) / 2 + 20 - 0.
	TestEqual(TEXT("The content sits where SBorder centres it"), LeftEdgeOf(Content), 120.0, 0.01);
	TestEqual(TEXT("...at its own size"), Content->GetSize(), FVector2D(100.0, 40.0));

	// (300 - 100) / 2 + 20 - 10: the slot's own padding is part of the same margins.
	Content->GetPanelSlot()->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	UDreamWidget::MarkLayoutForRebuild(BorderWidget);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("With the slot's padding added the margins are the two together"), LeftEdgeOf(Content), 110.0, 0.01);

	BorderWidget->SetFlowDirectionPreference(EDreamFlowDirectionPreference::RightToLeft);
	Manager->TickDreamUI(0.016f);
	// Right to left the margins trade places: (300 - 100) / 2 + 10 - 20.
	TestEqual(TEXT("Right to left it sits where SBorder centres it with the margins swapped"), LeftEdgeOf(Content), 90.0, 0.01);

	BorderWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasDockedChildMeasureTest,
	"DreamGUI.Layout.ACanvasMeasuresAChildDockedRightOrBottomFromTheEdgeThatFacesItsAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A canvas measures a docked child as its size plus its offset from the edge it is docked to, as
 * SConstraintCanvas does. It read the LEFT and TOP edge offsets for every dock, and for a child docked right
 * (or bottom) with its pivot on that side the left (bottom-up: top) edge offset already contains the child's
 * own width (height), so it was counted twice: 100 wide and 10 in from the right made the canvas 210 wide
 * where UMG makes it 110. This checks a right-docked child and a bottom-docked one in one canvas that sizes
 * itself to its content.
 */
bool FDreamCanvasDockedChildMeasureTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	UDreamWidget* CanvasWidget = MakeWidget(TestWorld.World, Root, TEXT("Canvas"), 50.0f, 50.0f);
	UDreamWidget* DockedTopRight = MakeWidget(TestWorld.World, CanvasWidget, TEXT("DockedTopRight"), 100.0f, 40.0f);
	UDreamWidget* DockedBottomLeft = MakeWidget(TestWorld.World, CanvasWidget, TEXT("DockedBottomLeft"), 60.0f, 30.0f);
	if (!TestNotNull(TEXT("Canvas created"), CanvasWidget->CreateNewLayoutContainer<UDreamLayoutContainerCanvasPanel>())
		|| !TestNotNull(TEXT("Column created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		Root->DestroyWidget();
		return false;
	}
	// Anchors are measured from the bottom here, so (1, 1) is the top-right corner. Each child's pivot sits on
	// the corner it is docked to, and each is 10 in from both of that corner's edges.
	FDreamUIAnchorData TopRight;
	TopRight.AnchorMin = FVector2D(1.0, 1.0);
	TopRight.AnchorMax = FVector2D(1.0, 1.0);
	TopRight.Pivot = FVector2D(1.0, 1.0);
	TopRight.AnchoredPosition = FVector2D(-10.0, -10.0);
	TopRight.SizeDelta = FVector2D(100.0, 40.0);
	DockedTopRight->SetAnchorData(TopRight);
	FDreamUIAnchorData BottomLeft;
	BottomLeft.AnchorMin = FVector2D(0.0, 0.0);
	BottomLeft.AnchorMax = FVector2D(0.0, 0.0);
	BottomLeft.Pivot = FVector2D(0.0, 0.0);
	BottomLeft.AnchoredPosition = FVector2D(10.0, 10.0);
	BottomLeft.SizeDelta = FVector2D(60.0, 30.0);
	DockedBottomLeft->SetAnchorData(BottomLeft);
	UDreamPanelSlot* CanvasSlot = CanvasWidget->GetPanelSlot();
	if (!TestNotNull(TEXT("The column handed the canvas a slot"), CanvasSlot))
	{
		Root->DestroyWidget();
		return false;
	}
	// Left-aligned, so the canvas is exactly as wide as it measures.
	CanvasSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Left);

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	// Across: max(100 + 10, 60 + 10). Down: max(40 + 10, 30 + 10).
	TestEqual(TEXT("The canvas is as wide as the right-docked child and its inset"), CanvasWidget->GetWidth(), 110.0f);
	TestEqual(TEXT("...and as tall as the top-docked child and its inset, the bottom-docked one counted once"), CanvasWidget->GetHeight(), 50.0f);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamContentInsetsMeasuredTest,
	"DreamGUI.Layout.AWidgetWithoutAPanelIsMeasuredWithTheInsetsOfAContentChildStretchedAcrossIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A widget with no layout container is measured from its content children, and it took a stretched child's
 * answer as its own: a label inset 12 either side of its button made the button exactly the label's width in
 * an Auto slot, and the stretch then took the insets out of that, squeezing the label 24 narrower than it asked
 * for. This checks the button is measured wide and tall enough for the label plus its insets, which leaves the
 * label exactly the size it wanted.
 */
bool FDreamContentInsetsMeasuredTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 400.0f);
	UDreamWidget* Button = MakeWidget(TestWorld.World, Root, TEXT("Button"), 50.0f, 50.0f);
	UDreamWidget* Label = MakeWidget(TestWorld.World, Button, TEXT("Label"), 50.0f, 50.0f);
	// The label's content: a column holding one 100x20 line, so the label wants 100x20.
	MakeWidget(TestWorld.World, Label, TEXT("Line"), 100.0f, 20.0f);
	if (!TestNotNull(TEXT("Label column created"), Label->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>())
		|| !TestNotNull(TEXT("Root column created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		Root->DestroyWidget();
		return false;
	}
	// Stretched across the button, inset 12 at the sides and 4 at top and bottom.
	Label->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), false, false);
	Label->SetAnchorOffset(FMargin(12.0f, 4.0f, 12.0f, 4.0f));
	UDreamPanelSlot* ButtonSlot = Button->GetPanelSlot();
	if (!TestNotNull(TEXT("The column handed the button a slot"), ButtonSlot))
	{
		Root->DestroyWidget();
		return false;
	}
	// Left-aligned, so the button is exactly as wide as it measures.
	ButtonSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Left);

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("The button is the label's width plus its two side insets"), Button->GetWidth(), 124.0f);
	TestEqual(TEXT("...and the label's height plus the insets above and below it"), Button->GetHeight(), 28.0f);
	TestEqual(TEXT("...which leaves the label the size it asked for"), Label->GetSize(), FVector2D(100.0, 20.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPanelSurvivesListenerAddingChildrenTest,
	"DreamGUI.Layout.APanelArrangingSurvivesCodeThatAddsChildrenToItWhileItRestoresOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A single-child panel walks its children to pick the content and hand the rest back their authored rects,
 * and a restore announces the new size to whatever is listening, which can add or take away children of
 * that same panel. The walk was over the live array, so a listener that added children changed it under the
 * iteration (UE's ranged-for check fires; past that, the loop reads freed memory). This checks a size box
 * whose surplus child's listener adds children mid-arrange still arranges its content and collapses the rest.
 */
bool FDreamPanelSurvivesListenerAddingChildrenTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 200.0f, 100.0f);
	UDreamWidget* First = MakeWidget(TestWorld.World, Root, TEXT("First"), 50.0f, 50.0f);
	if (!TestNotNull(TEXT("Size box created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerSizeBox>()))
	{
		Root->DestroyWidget();
		return false;
	}
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	if (!TestEqual(TEXT("The first child is the content and fills the box"), First->GetSize(), FVector2D(200.0, 100.0)))
	{
		Root->DestroyWidget();
		return false;
	}

	// A second child moved in front makes the first one surplus, so the next arrange restores its authored
	// rect -- which is a resize its listener hears, and the listener answers it by adding more children.
	UDreamWidget* Second = MakeWidget(TestWorld.World, nullptr, TEXT("Second"), 40.0f, 40.0f);
	if (!TestTrue(TEXT("The second child attaches in front through the capacity-ignoring path"),
		Second->SetParentIgnoringCapacity(Root, false, 0)))
	{
		Second->DestroyWidget();
		Root->DestroyWidget();
		return false;
	}
	TArray<UDreamWidget*> Added;
	UWorld* World = TestWorld.World;
	const FDelegateHandle Listening = First->GetDimensionChangedEvent().AddLambda(
		[Root, World, &Added](bool, bool, bool)
		{
			if (Added.Num() > 0)
			{
				return;
			}
			for (int32 Index = 0; Index < 8; ++Index)
			{
				UDreamWidget* Extra = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
				Extra->SetWidth(10.0f);
				Extra->SetHeight(10.0f);
				Extra->OnRegister();
				Extra->SetParentIgnoringCapacity(Root);
				Added.Add(Extra);
			}
		});
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	First->GetDimensionChangedEvent().Remove(Listening);

	TestEqual(TEXT("The listener ran, from inside the arrange"), Added.Num(), 8);
	TestEqual(TEXT("The child in front is the content and fills the box"), Second->GetSize(), FVector2D(200.0, 100.0));
	TestFalse(TEXT("The child behind it is collapsed for layout"), First->GetLayoutVisibleInHierarchy());
	bool bAnyAddedVisible = false;
	for (const UDreamWidget* Extra : Added)
	{
		bAnyAddedVisible |= Extra->GetLayoutVisibleInHierarchy();
	}
	TestFalse(TEXT("...and so is every child the listener added"), bAnyAddedVisible);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamResizeReachesEveryChildWhileAListenerAddsSiblingsTest,
	"DreamGUI.Layout.AResizeReachesEveryChildWhileAListenerAddsSiblings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A resize walks the widget's children to resize the ones stretched across it, and each of those announces
 * its own new size to whatever is listening -- code that can add children to the widget being walked. The
 * walk was over the live array, so a listener that added a sibling changed it under the iteration (UE's
 * ranged-for check fires; past that, the loop reads freed memory). This checks that when the first
 * stretched child's listener adds siblings, the second stretched child still follows the new width.
 */
bool FDreamResizeReachesEveryChildWhileAListenerAddsSiblingsTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 200.0f, 100.0f);
	UDreamWidget* First = MakeWidget(TestWorld.World, Root, TEXT("First"), 200.0f, 30.0f);
	UDreamWidget* Second = MakeWidget(TestWorld.World, Root, TEXT("Second"), 200.0f, 30.0f);
	// Both stretched across the root, so a new root width is a new width for each of them.
	for (UDreamWidget* Child : {First, Second})
	{
		FDreamUIAnchorData Stretched = Child->GetAnchorData();
		Stretched.AnchorMin = FVector2D(0.0, 0.5);
		Stretched.AnchorMax = FVector2D(1.0, 0.5);
		Stretched.SizeDelta = FVector2D(0.0, 30.0);
		Child->SetAnchorData(Stretched);
	}
	if (!TestEqual(TEXT("The second child starts as wide as the root"), Second->GetWidth(), 200.0f))
	{
		Root->DestroyWidget();
		return false;
	}

	TArray<UDreamWidget*> Added;
	UWorld* World = TestWorld.World;
	const FDelegateHandle Listening = First->GetDimensionChangedEvent().AddLambda(
		[Root, World, &Added](bool, bool, bool)
		{
			if (Added.Num() > 0)
			{
				return;
			}
			for (int32 Index = 0; Index < 8; ++Index)
			{
				UDreamWidget* Extra = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
				Extra->SetWidth(10.0f);
				Extra->SetHeight(10.0f);
				Extra->OnRegister();
				Extra->TrySetParent(Root, false);
				Added.Add(Extra);
			}
		});
	Root->SetWidth(300.0f);
	First->GetDimensionChangedEvent().Remove(Listening);

	TestEqual(TEXT("The listener ran, from inside the resize"), Added.Num(), 8);
	TestEqual(TEXT("The first child follows the new width"), First->GetWidth(), 300.0f);
	TestEqual(TEXT("...and so does the child after it, which the walk reached after the listener ran"),
		Second->GetWidth(), 300.0f);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamKeepSizeAnchorChangeReportsNewOffsetsTest,
	"DreamGUI.Layout.ChangingAnchorsWhileKeepingTheSizeReportsTheNewEdgeOffsets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * SetHorizontalAnchorMinMax and SetVerticalAnchorMinMax read the two edge offsets, which caches them, then rewrite
 * the anchors, the size delta and the anchored position in place. Without the size kept that leaves both offsets
 * where they were, and the cache stays right. With it kept, a change of span moves both edges' distances from the
 * anchors, and the getters went on answering the old ones. This checks a 50 x 30 child centred in a 200 x 100
 * parent and stretched across it on each axis, size kept: it sits 75 from either side and 35 from top and bottom.
 */
bool FDreamKeepSizeAnchorChangeReportsNewOffsetsTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Parent = MakeWidget(TestWorld.World, nullptr, TEXT("Parent"), 200.0f, 100.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Parent, TEXT("Child"), 50.0f, 30.0f);
	if (!TestTrue(TEXT("The child starts centred, 25 short of the anchor line on either side"),
		FMath::IsNearlyEqual(Child->GetAnchorOffsetLeft(), -25.0f, 0.01f)
		&& FMath::IsNearlyEqual(Child->GetAnchorOffsetRight(), -25.0f, 0.01f)))
	{
		Parent->DestroyWidget();
		return false;
	}

	Child->SetHorizontalAnchorMinMax(FVector2D(0.0, 1.0), /*bKeepSize*/true, /*bKeepRelativeLocation*/false);
	TestTrue(TEXT("The width is kept"), FMath::IsNearlyEqual(Child->GetWidth(), 50.0f, 0.01f));
	TestTrue(TEXT("...the left edge is reported 75 in from the parent's left"),
		FMath::IsNearlyEqual(Child->GetAnchorOffsetLeft(), 75.0f, 0.01f));
	TestTrue(TEXT("...and the right edge 75 in from its right"),
		FMath::IsNearlyEqual(Child->GetAnchorOffsetRight(), 75.0f, 0.01f));

	Child->SetVerticalAnchorMinMax(FVector2D(0.0, 1.0), /*bKeepSize*/true, /*bKeepRelativeLocation*/false);
	TestTrue(TEXT("The height is kept"), FMath::IsNearlyEqual(Child->GetHeight(), 30.0f, 0.01f));
	TestTrue(TEXT("...the bottom edge is reported 35 up from the parent's bottom"),
		FMath::IsNearlyEqual(Child->GetAnchorOffsetBottom(), 35.0f, 0.01f));
	TestTrue(TEXT("...and the top edge 35 down from its top"),
		FMath::IsNearlyEqual(Child->GetAnchorOffsetTop(), 35.0f, 0.01f));

	Parent->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuFitsTheRootWhateverItsPivotAndScaleTest,
	"DreamGUI.Layout.AMenuFitsInsideTheRootWhateverTheRootsPivotAndTheAnchorsScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A menu anchor fitting its menu into the root found its own place in the root by taking the root's pivot to be its
 * centre, and fitted the menu's size, which is in the anchor's units, as if it were in the root's. A root pivoted
 * anywhere else put the anchor half a root away from where it was, and an anchor drawn at half scale had its menu
 * fitted as if twice as big. This checks a half-scale anchor near the bottom-right corner of a 1000 x 600 root
 * pivoted at its top-left corner: its menu is pulled back exactly far enough to end at the root's right and bottom
 * edges.
 */
bool FDreamMenuFitsTheRootWhateverItsPivotAndScaleTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 1000.0f, 600.0f);
	Root->SetPivot(FVector2D(0.0, 1.0));
	UDreamWidget* AnchorWidget = MakeWidget(TestWorld.World, Root, TEXT("Anchor"), 80.0f, 40.0f);
	// Docked to the root's top-left corner, its own top-left corner 950 across and 570 down, drawn at half scale:
	// 40 x 20 of the root's units, from 950 to 990 across and 570 to 590 down.
	AnchorWidget->SetAnchorMin(FVector2D(0.0, 1.0));
	AnchorWidget->SetAnchorMax(FVector2D(0.0, 1.0));
	AnchorWidget->SetPivot(FVector2D(0.0, 1.0));
	AnchorWidget->SetAnchoredPosition(FVector2D(950.0, -570.0));
	AnchorWidget->SetRelativeScale(FVector(1.0, 0.5, 0.5));
	MakeWidget(TestWorld.World, AnchorWidget, TEXT("Face"), 80.0f, 40.0f);
	UDreamWidget* Menu = MakeWidget(TestWorld.World, AnchorWidget, TEXT("Menu"), 200.0f, 60.0f);
	UDreamLayoutContainerMenuAnchor* MenuAnchor = AnchorWidget->CreateNewLayoutContainer<UDreamLayoutContainerMenuAnchor>();
	if (!TestNotNull(TEXT("Menu anchor created"), MenuAnchor))
	{
		Root->DestroyWidget();
		return false;
	}
	MenuAnchor->SetPlacement(EDreamMenuPlacement::BelowAnchor);
	MenuAnchor->SetFitInWindow(true);
	MenuAnchor->SetIsOpen(true);
	UDreamWidget::MarkLayoutForRebuild(AnchorWidget);
	Manager->TickDreamUI(0.016f);

	// Hung below the anchor, the menu would span 950 to 1050 across and 590 to 620 down in the root: 50 too far
	// right and 20 too low. Pulled back by that, in the anchor's own units it starts 100 left of the anchor's left
	// edge and level with its top.
	TestEqual(TEXT("The menu keeps its own size"), Menu->GetSize(), FVector2D(200.0, 60.0));
	TestTrue(TEXT("...is pulled back to end at the root's right edge"),
		FMath::IsNearlyEqual(LeftEdgeOf(Menu), -100.0, 0.01));
	TestTrue(TEXT("...and up to end at its bottom edge"),
		FMath::IsNearlyEqual(TopEdgeOf(Menu), 0.0, 0.01));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBuiltVerticalBoxLaysOutLikeCodeTest,
	"DreamGUI.Layout.AVerticalBoxBuiltFromTextLaysOutExactlyLikeTheSameSceneBuiltInCode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The text builder attaches each widget before it writes the file's lines, and writes AnchorData's fields in place.
 * The size the widget had worked out from the class default when it was attached -- 100 x 100 -- was then the size
 * it went on reporting, and the size its panel slot captured as the one to measure it by, which the class template
 * saves and every instance inherits. A vertical box built from text stacked 100-tall rows where the file said 40 and
 * 60. This checks that the box built from text, copied into a world the way an instance is, lays out exactly like the
 * same box built in code, and that the code-built one is the layout the file describes.
 */
bool FDreamTextBuiltVerticalBoxLaysOutLikeCodeTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConformanceTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	const FString Source = TEXT(
		"Widget Root {\n"
		"    AnchorData.SizeDelta = (300, 200)\n"
		"    + VerticalBox {}\n"
		"    Widget Header {\n"
		"        AnchorData.SizeDelta = (120, 40)\n"
		"        @slot HorizontalAlignment = Left\n"
		"    }\n"
		"    Widget Body {\n"
		"        AnchorData.SizeDelta = (80, 60)\n"
		"        @slot HorizontalAlignment = Center\n"
		"    }\n"
		"    Widget Footer {\n"
		"        AnchorData.SizeDelta = (50, 30)\n"
		"        @slot SizeRule = Fill\n"
		"    }\n"
		"}\n");
	FDreamUIDiagnosticBag Diagnostics;
	FDreamUIAst Ast;
	TArray<FDreamWidgetPropertyBinding> Bindings;
	const bool bParsed = FDreamUISourceFile::Parse(Source, TEXT("LayoutConformance.dui"), Ast, Diagnostics);
	TStrongObjectPtr<UDreamWidgetTree> Tree(bParsed
		? FDreamUITextBuilder::Build(Ast, GetTransientPackage(), Diagnostics, Bindings)
		: nullptr);
	if (!TestTrue(FString::Printf(TEXT("The scene parses and builds without an error: %s"), *Diagnostics.ToString()),
		Tree.IsValid() && IsValid(Tree->RootWidget) && !Diagnostics.HasErrors()))
	{
		return false;
	}
	// Into the world the way an instance of the class comes: copied from the template, so whatever the template
	// saved is what the copy has.
	UDreamWidget* FromText = DuplicateDreamWidgetHierarchy(TestWorld.World, Tree->RootWidget.Get(), nullptr);
	if (!TestNotNull(TEXT("The built tree is copied into the world"), FromText))
	{
		return false;
	}

	UDreamWidget* InCode = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 300.0f, 200.0f);
	if (!TestNotNull(TEXT("Vertical box created"), InCode->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		FromText->DestroyWidget();
		InCode->DestroyWidget();
		return false;
	}
	UDreamWidget* Header = MakeWidget(TestWorld.World, InCode, TEXT("Header"), 120.0f, 40.0f);
	UDreamWidget* Body = MakeWidget(TestWorld.World, InCode, TEXT("Body"), 80.0f, 60.0f);
	UDreamWidget* Footer = MakeWidget(TestWorld.World, InCode, TEXT("Footer"), 50.0f, 30.0f);
	Header->GetPanelSlot()->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Left);
	Body->GetPanelSlot()->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
	Footer->GetPanelSlot()->SetSizeRule(EDreamPanelSizeRule::Fill);

	UDreamWidget::MarkLayoutForRebuild(FromText);
	UDreamWidget::MarkLayoutForRebuild(InCode);
	Manager->TickDreamUI(0.016f);

	// What the file describes: Header 120 x 40 at the left, Body 80 x 60 centred under it, Footer the 100 left over.
	TestEqual(TEXT("The code-built header is as the file says"), Header->GetSize(), FVector2D(120.0, 40.0));
	TestTrue(TEXT("...the body is centred under it"),
		FMath::IsNearlyEqual(LeftEdgeOf(Body), 110.0, 0.01) && FMath::IsNearlyEqual(TopEdgeOf(Body), 40.0, 0.01));
	TestEqual(TEXT("...and the footer takes the 100 left over"), Footer->GetSize(), FVector2D(300.0, 100.0));

	for (const UDreamWidget* Coded : { Header, Body, Footer })
	{
		UDreamWidget* const* Found = FromText->GetChildren().FindByPredicate(
			[Coded](const UDreamWidget* Child) { return IsValid(Child) && Child->GetDisplayName() == Coded->GetDisplayName(); });
		if (!TestTrue(FString::Printf(TEXT("The text-built tree has a %s"), *Coded->GetDisplayName()), Found != nullptr))
		{
			continue;
		}
		const UDreamWidget* Built = *Found;
		TestEqual(FString::Printf(TEXT("%s is the same size built from text"), *Coded->GetDisplayName()),
			Built->GetSize(), Coded->GetSize());
		TestTrue(FString::Printf(TEXT("%s is in the same place built from text"), *Coded->GetDisplayName()),
			FMath::IsNearlyEqual(LeftEdgeOf(Built), LeftEdgeOf(Coded), 0.01)
			&& FMath::IsNearlyEqual(TopEdgeOf(Built), TopEdgeOf(Coded), 0.01));
	}

	FromText->DestroyWidget();
	InCode->DestroyWidget();
	return true;
}

#endif
