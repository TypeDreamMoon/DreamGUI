// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "DreamUIControlRegistry.h"

#include "Editor.h"

/*
 * The designer's own gestures on the design surface, made with a pointer: the resize handles, the snapping a drag does
 * to the grid, to a sibling's edge and to an anchor's quarter lines, and a palette row named and dropped.
 *
 * The same edits have tests that call the arithmetic behind them -- SolveEdgeSnap, ResolveMoveDrag, SnapAnchorFraction,
 * SetAnchorsPreservingRect -- which say what the rules are. These say the rules are what a hand meets: every press, move
 * and release goes in through FSceneViewport's ISlateViewport entry points (FDreamDesignerDriver), the designer's
 * viewport client hit-tests its own handles against the pixel, and the client's tick turns the held press into the drag.
 * The handles are the designer's answer to UMG's (STransformHandle, the anchor medallion); the rotate handle, which UMG
 * has no counterpart of, is in DreamDesignerRotateHandleAutomationTests.cpp, and the 3D view's transform gizmo in
 * DreamDesignerGizmoAutomationTests.cpp.
 *
 * Synchronous, like the rest of the headless designer suite: the driver pumps the designer's frames itself, and the
 * preferences every gesture reads -- grid snapping and its size, the guides that switch sibling snapping on -- are pinned
 * for each test and put back after it, unsaved.
 */
namespace DreamDesignerViewportGestureTestLocal
{
	using namespace DreamTests;

	/** Half a pixel's worth of design units, the error a press landing on a whole pixel can carry, plus rounding. */
	double HalfPixelIn(double InPixelsPerUnit)
	{
		return 0.5 / FMath::Max(InPixelsPerUnit, UE_SMALL_NUMBER) + 0.01;
	}

	/** The pixels a widget's own unit covers on screen, along X: its projected width over its width. */
	double OwnPixelsPerUnit(const FBox2D& InRect, const UDreamWidget* InWidget)
	{
		return (InRect.Max.X - InRect.Min.X) / FMath::Max(static_cast<double>(InWidget->GetWidth()), 1.0);
	}

	/** The widget's rect along X in its parent's frame, as the snapping measures it: the anchored position names the pivot. */
	void HorizontalSpan(const UDreamWidget* InWidget, double& OutMin, double& OutMax)
	{
		const double Width = InWidget->GetWidth();
		OutMin = InWidget->GetAnchoredPosition().X - InWidget->GetPivot().X * Width;
		OutMax = OutMin + Width;
	}

	FIntPoint RoundPixel(const FVector2D& InPixel)
	{
		return FIntPoint(FMath::RoundToInt32(InPixel.X), FMath::RoundToInt32(InPixel.Y));
	}

	const FDreamUIControlDescriptor* FindRow(FName InRowName)
	{
		return FDreamUIControlRegistry::Get().GetDescriptors().FindByPredicate(
			[InRowName](const FDreamUIControlDescriptor& InRow) { return InRow.Name == InRowName; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRightEdgeHandleTest,
	"DreamGUI.Designer.Driver.DraggingTheRightEdgeHandleWidensTheWidgetAndOneUndoPutsTheWidthBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A selected widget's right-edge handle, the middle of its right side, dragged sixty pixels to the right with the grid off.
 * The right edge follows the pointer and the left one stays where it is -- UMG's STransformHandle for the right side
 * resizes rightwards from a fixed left edge -- the asset records the new width, and the drag is one undo step that puts
 * the width back.
 */
bool FDreamDesignerRightEdgeHandleTest::RunTest(const FString&)
{
	using namespace DreamDesignerViewportGestureTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDesignerSession Session(TEXT("DesignerRightEdgeHandle"), /*bGiveRootAPanel*/ true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Template = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.3, 0.4));
	if (Template == nullptr)
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Template);
	Driver.PumpFrame();
	const TOptional<FBox2D> Before = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
	if (!TestTrue(TEXT("The selected widget is on screen"), Before.IsSet()))
	{
		return false;
	}
	const double WidthBefore = Template->GetWidth();
	const double PixelsPerUnit = OwnPixelsPerUnit(Before.GetValue(), Template);
	const FIntPoint Handle = RoundPixel(FVector2D(Before->Max.X, Before->GetCenter().Y));
	const FIntPoint Travel(60, 0);

	TestTrue(TEXT("The drag of the right-edge handle completes"), Driver.DragFromTo(Handle, Handle + Travel, /*Steps*/ 4));

	// The edge goes where the pointer let go, which is the handle's own pixel plus the travel.
	const double Expected = WidthBefore + (Handle.X + Travel.X - Before->Max.X) / PixelsPerUnit;
	TestTrue(FString::Printf(TEXT("The asset holds the widened width: expected %.2f, holds %.2f"), Expected, Template->GetWidth()),
		FMath::IsNearlyEqual(static_cast<double>(Template->GetWidth()), Expected, HalfPixelIn(PixelsPerUnit)));
	const UDreamWidget* Preview = Driver.PreviewFor(Template);
	TestTrue(TEXT("...and so does the preview"), Preview != nullptr && FMath::IsNearlyEqual(Preview->GetWidth(), Template->GetWidth(), 0.01f));
	const TOptional<FBox2D> After = Driver.WidgetPixelRect(Preview);
	if (TestTrue(TEXT("The widened widget is on screen"), After.IsSet()))
	{
		TestTrue(FString::Printf(TEXT("Its left edge stayed where it was: %.1f, was %.1f"), After->Min.X, Before->Min.X),
			FMath::IsNearlyEqual(After->Min.X, Before->Min.X, 1.5));
		TestTrue(FString::Printf(TEXT("...and its height did not change: %.1f, was %.1f"), After->Max.Y - After->Min.Y, Before->Max.Y - Before->Min.Y),
			FMath::IsNearlyEqual(After->Max.Y - After->Min.Y, Before->Max.Y - Before->Min.Y, 1.5));
	}

	TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
	Driver.PumpFrame();
	TestTrue(TEXT("Undo takes the resize back, not the drop"), LiveChildrenOf(Session.GetTemplateRoot()).Contains(Template));
	TestTrue(FString::Printf(TEXT("One undo puts the width back: %.2f, was %.2f"), Template->GetWidth(), WidthBefore),
		FMath::IsNearlyEqual(static_cast<double>(Template->GetWidth()), WidthBefore, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerCornerHandleTest,
	"DreamGUI.Designer.Driver.DraggingTheTopLeftCornerHandleResizesBothWaysAndLeavesTheOppositeCornerWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The top-left corner handle dragged up and to the left: the widget grows on both axes by what the pointer travelled, and
 * its bottom-right corner -- the corner the handle is opposite -- does not move, as UMG's corner handle resizes about the
 * opposite corner.
 */
bool FDreamDesignerCornerHandleTest::RunTest(const FString&)
{
	using namespace DreamDesignerViewportGestureTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDesignerSession Session(TEXT("DesignerCornerHandle"), /*bGiveRootAPanel*/ true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Template = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.4, 0.55));
	if (Template == nullptr)
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Template);
	Driver.PumpFrame();
	const TOptional<FBox2D> Before = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
	if (!TestTrue(TEXT("The selected widget is on screen"), Before.IsSet()))
	{
		return false;
	}
	const double WidthBefore = Template->GetWidth();
	const double HeightBefore = Template->GetHeight();
	const double PixelsPerUnit = OwnPixelsPerUnit(Before.GetValue(), Template);
	// Pixel Y grows downwards: the top-left corner is the box's minimum on both axes.
	const FIntPoint Handle = RoundPixel(Before->Min);
	const FIntPoint Travel(-30, -20);

	TestTrue(TEXT("The drag of the top-left handle completes"), Driver.DragFromTo(Handle, Handle + Travel, /*Steps*/ 4));

	const double ExpectedWidth = WidthBefore + (Before->Min.X - (Handle.X + Travel.X)) / PixelsPerUnit;
	const double ExpectedHeight = HeightBefore + (Before->Min.Y - (Handle.Y + Travel.Y)) / PixelsPerUnit;
	TestTrue(FString::Printf(TEXT("The asset holds the wider width: expected %.2f, holds %.2f"), ExpectedWidth, Template->GetWidth()),
		FMath::IsNearlyEqual(static_cast<double>(Template->GetWidth()), ExpectedWidth, HalfPixelIn(PixelsPerUnit)));
	TestTrue(FString::Printf(TEXT("...and the taller height: expected %.2f, holds %.2f"), ExpectedHeight, Template->GetHeight()),
		FMath::IsNearlyEqual(static_cast<double>(Template->GetHeight()), ExpectedHeight, HalfPixelIn(PixelsPerUnit)));
	const TOptional<FBox2D> After = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
	if (TestTrue(TEXT("The resized widget is on screen"), After.IsSet()))
	{
		TestTrue(FString::Printf(TEXT("The bottom-right corner stayed where it was: %s, was %s"), *After->Max.ToString(), *Before->Max.ToString()),
			After->Max.Equals(Before->Max, 1.5));
	}
	TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
	Driver.PumpFrame();
	TestTrue(TEXT("One undo puts both sides back"),
		FMath::IsNearlyEqual(static_cast<double>(Template->GetWidth()), WidthBefore, 0.01)
		&& FMath::IsNearlyEqual(static_cast<double>(Template->GetHeight()), HeightBefore, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerSiblingSnapDragTest,
	"DreamGUI.Designer.Driver.ADragEndingJustShortOfASiblingsEdgeLandsTheWidgetOnThatEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Two widgets side by side, the grid on at fifty units and the guides on. The right-hand one is dragged left until its
 * left edge is three units short of its sibling's right edge -- inside the six screen pixels UMG's designer forgives --
 * and it lands edge to edge: the sibling's edge wins the axis over the gridline that is nearer the pointer's own answer.
 */
bool FDreamDesignerSiblingSnapDragTest::RunTest(const FString&)
{
	using namespace DreamDesignerViewportGestureTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(true, 50.0f);
	Preferences.SetGuides(true);
	FScopedDesignerSession Session(TEXT("DesignerSiblingSnap"), /*bGiveRootAPanel*/ true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Anchor = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.25, 0.5));
	UDreamWidget* Mover = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.65, 0.5));
	if (Anchor == nullptr || Mover == nullptr)
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Mover);
	Driver.PumpFrame();
	UDreamWidget* MoverPreview = Driver.PreviewFor(Mover);
	const TOptional<FBox2D> MoverRect = Driver.WidgetPixelRect(MoverPreview);
	const FVector2D PixelsPerUnit = PixelsPerUnitFor(Driver, MoverPreview);
	if (!TestTrue(TEXT("The dragged widget and its parent are on screen"), MoverRect.IsSet() && PixelsPerUnit.X > 0.0))
	{
		return false;
	}
	double AnchorMin = 0.0, AnchorMax = 0.0, MoverMin = 0.0, MoverMax = 0.0;
	HorizontalSpan(Anchor, AnchorMin, AnchorMax);
	HorizontalSpan(Mover, MoverMin, MoverMax);
	if (!TestTrue(FString::Printf(TEXT("The two start apart, the dragged one on the right (%.1f..%.1f and %.1f..%.1f)"), AnchorMin, AnchorMax, MoverMin, MoverMax),
		MoverMin > AnchorMax + 50.0))
	{
		return false;
	}
	// Three units short of the sibling's right edge, in whole pixels.
	const double TravelUnits = (AnchorMax + 3.0) - MoverMin;
	const FIntPoint Travel(FMath::RoundToInt32(TravelUnits * PixelsPerUnit.X), 0);
	// Inside the rect, a quarter of the way in: the Move handle, and away from every resize handle and the pivot.
	const FIntPoint From = PointInBox(MoverRect.GetValue(), 0.25, 0.25);
	const double PivotBefore = Mover->GetAnchoredPosition().X;
	// Where the pointer alone takes the pivot, and the gridline nearest that, which the grid alone would have chosen.
	const double PointerPivot = PivotBefore + Travel.X / PixelsPerUnit.X;
	const double GridPivot = FMath::GridSnap(PointerPivot, 50.0);

	TestTrue(TEXT("The drag completes"), Driver.DragFromTo(From, From + Travel, /*Steps*/ 6));

	HorizontalSpan(Mover, MoverMin, MoverMax);
	TestTrue(FString::Printf(TEXT("The dragged widget's left edge is on the sibling's right edge: %.2f, the edge is %.2f"), MoverMin, AnchorMax),
		FMath::IsNearlyEqual(MoverMin, AnchorMax, 0.02));
	if (!FMath::IsNearlyEqual(GridPivot, Mover->GetAnchoredPosition().X, 0.5))
	{
		AddInfo(FString::Printf(TEXT("The grid alone would have put the pivot at %.2f; the sibling's edge put it at %.2f."),
			GridPivot, Mover->GetAnchoredPosition().X));
	}
	TestTrue(TEXT("The drag is one undo step"), GEditor->UndoTransaction());
	Driver.PumpFrame();
	TestTrue(FString::Printf(TEXT("...that puts the widget back where it was: %.2f, was %.2f"), Mover->GetAnchoredPosition().X, PivotBefore),
		FMath::IsNearlyEqual(Mover->GetAnchoredPosition().X, PivotBefore, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerGridSnapDragTest,
	"DreamGUI.Designer.Driver.WithSnappingOnADragLandsTheWidgetOnTheNearestGridline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * One widget, the grid on at twenty-five units and the guides off, so no sibling line is in play. A drag of an arbitrary
 * distance lands the widget's anchored position on the gridline nearest where the pointer took it, on both axes -- UMG's
 * canvas slot snaps a dragged position to the grid the same way (GridSnapSize, UCanvasPanelSlot::MoveByDesigner).
 */
bool FDreamDesignerGridSnapDragTest::RunTest(const FString&)
{
	using namespace DreamDesignerViewportGestureTestLocal;
	constexpr float GridSize = 25.0f;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(true, GridSize);
	Preferences.SetGuides(false);
	FScopedDesignerSession Session(TEXT("DesignerGridSnap"), /*bGiveRootAPanel*/ true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Template = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.35, 0.4));
	if (Template == nullptr)
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Template);
	Driver.PumpFrame();
	UDreamWidget* Preview = Driver.PreviewFor(Template);
	const TOptional<FBox2D> Rect = Driver.WidgetPixelRect(Preview);
	const FVector2D PixelsPerUnit = PixelsPerUnitFor(Driver, Preview);
	if (!TestTrue(TEXT("The widget and its parent are on screen"), Rect.IsSet() && PixelsPerUnit.X > 0.0 && PixelsPerUnit.Y > 0.0))
	{
		return false;
	}
	const FVector2D Start = Template->GetAnchoredPosition();
	const FIntPoint From = PointInBox(Rect.GetValue(), 0.25, 0.25);
	const FIntPoint Travel(37, 23);

	TestTrue(TEXT("The drag completes"), Driver.DragFromTo(From, From + Travel, /*Steps*/ 4));

	// Pixel Y grows downwards and an anchored position's grows upwards.
	const FVector2D Raw = Start + FVector2D(Travel.X / PixelsPerUnit.X, -Travel.Y / PixelsPerUnit.Y);
	const FVector2D Expected(FMath::GridSnap(Raw.X, static_cast<double>(GridSize)), FMath::GridSnap(Raw.Y, static_cast<double>(GridSize)));
	const FVector2D Landed = Template->GetAnchoredPosition();
	TestTrue(FString::Printf(TEXT("The widget lands on the nearest gridline: expected %s, holds %s (the pointer alone said %s)"),
		*Expected.ToString(), *Landed.ToString(), *Raw.ToString()), Landed.Equals(Expected, 0.01));
	TestTrue(TEXT("...a whole number of grid steps on both axes"),
		FMath::IsNearlyZero(Landed.X - GridSize * FMath::RoundToDouble(Landed.X / GridSize), 0.01)
		&& FMath::IsNearlyZero(Landed.Y - GridSize * FMath::RoundToDouble(Landed.Y / GridSize), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerAnchorSnapDragTest,
	"DreamGUI.Designer.Driver.AnAnchorMarkerDraggedNearAQuarterLineSnapsOntoItAndTheRectStaysWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The anchor medallion of a selected widget: its top-right marker, eleven pixels out along its diagonal from the anchor
 * point on the parent (where the designer draws it so four coincident markers stay apart), dragged until the anchor's
 * right line is just past three quarters of the parent. It snaps onto the quarter line, as UMG's anchor drag snaps to its
 * quarter stops, and the widget's rect stays where it was on screen: moving an anchor re-measures the offsets against it
 * rather than dragging the rect along.
 */
bool FDreamDesignerAnchorSnapDragTest::RunTest(const FString&)
{
	using namespace DreamDesignerViewportGestureTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDesignerSession Session(TEXT("DesignerAnchorSnap"), /*bGiveRootAPanel*/ true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Template = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.2, 0.25));
	if (Template == nullptr)
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Template);
	Driver.PumpFrame();
	UDreamWidget* Preview = Driver.PreviewFor(Template);
	UDreamWidget* PreviewParent = Preview != nullptr ? Preview->GetParent() : nullptr;
	const TOptional<FBox2D> ParentRect = Driver.WidgetPixelRect(PreviewParent);
	const TOptional<FBox2D> RectBefore = Driver.WidgetPixelRect(Preview);
	if (!TestTrue(TEXT("The widget and its parent are on screen"), ParentRect.IsSet() && RectBefore.IsSet()))
	{
		return false;
	}
	const FVector2D AnchorMaxBefore = Template->GetAnchorMax();
	const FVector2D AnchorMinBefore = Template->GetAnchorMin();
	const double ParentWidthPixels = ParentRect->Max.X - ParentRect->Min.X;
	const double ParentHeightPixels = ParentRect->Max.Y - ParentRect->Min.Y;
	// The anchor point on the parent, pixel Y downwards and the anchor's fraction upwards, then the marker's own offset.
	const FVector2D AnchorPoint(
		ParentRect->Min.X + AnchorMaxBefore.X * ParentWidthPixels,
		ParentRect->Max.Y - AnchorMaxBefore.Y * ParentHeightPixels);
	const FIntPoint Marker = RoundPixel(AnchorPoint + FVector2D(11.0, -11.0));
	const FIntPoint Size = Driver.ViewportPixelSize();
	if (!TestTrue(FString::Printf(TEXT("The top-right anchor marker is on screen at (%d, %d)"), Marker.X, Marker.Y),
		Marker.X > 0 && Marker.Y > 0 && Marker.X < Size.X && Marker.Y < Size.Y))
	{
		return false;
	}
	// Just past three quarters: inside the quarter stops' pull, and well away from a half.
	constexpr double Target = 0.757;
	const FIntPoint Travel(FMath::RoundToInt32((Target - AnchorMaxBefore.X) * ParentWidthPixels), 0);
	if (!TestTrue(TEXT("The marker's travel stays on screen"), Marker.X + Travel.X < Size.X - 2))
	{
		return false;
	}

	TestTrue(TEXT("The drag of the anchor marker completes"), Driver.DragFromTo(Marker, Marker + Travel, /*Steps*/ 6));

	const FVector2D AnchorMaxAfter = Template->GetAnchorMax();
	TestTrue(FString::Printf(TEXT("The anchor's right line snapped onto three quarters: %.4f"), AnchorMaxAfter.X),
		FMath::IsNearlyEqual(AnchorMaxAfter.X, 0.75, 1e-4));
	TestTrue(FString::Printf(TEXT("...its top line stayed where it was: %.4f, was %.4f"), AnchorMaxAfter.Y, AnchorMaxBefore.Y),
		FMath::IsNearlyEqual(AnchorMaxAfter.Y, AnchorMaxBefore.Y, 1e-4));
	TestTrue(TEXT("...and the other anchor did not move"), Template->GetAnchorMin().Equals(AnchorMinBefore, 1e-4));
	const TOptional<FBox2D> RectAfter = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
	if (TestTrue(TEXT("The widget is still on screen"), RectAfter.IsSet()))
	{
		TestTrue(FString::Printf(TEXT("...exactly where it was: %s, was %s"), *RectAfter->ToString(), *RectBefore->ToString()),
			RectAfter->Min.Equals(RectBefore->Min, 1.0) && RectAfter->Max.Equals(RectBefore->Max, 1.0));
	}
	TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
	Driver.PumpFrame();
	TestTrue(TEXT("One undo puts the anchor back"), Template->GetAnchorMax().Equals(AnchorMaxBefore, 1e-4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPaletteRowByNameTest,
	"DreamGUI.Designer.Driver.EachPaletteRowDroppedByItsNameLandsUnderThePointerAsWhatThatRowMakes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Three kinds of palette row, each named as the palette lists it and dropped at a pixel of its own: a control ("Button"),
 * a panel ("VerticalBox") and a visual ("Texture"). Each makes what its row says -- an instance of the control's class, a
 * widget carrying the panel's layout container, a widget carrying the visual -- under the root, centred under the pointer
 * it was let go at, as a UMG palette drop onto a canvas panel places its widget where it was dropped.
 */
bool FDreamDesignerPaletteRowByNameTest::RunTest(const FString&)
{
	using namespace DreamDesignerViewportGestureTestLocal;
	// The grid off, as for every gesture here: a drop with it on lands on the nearest gridline, as UMG's does
	// (SDesignerView::ProcessDropAndAddWidget hands the slot the grid size), which is a test of the grid, not of the row.
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDesignerSession Session(TEXT("DesignerPaletteRowByName"), /*bGiveRootAPanel*/ true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	struct FRowCase
	{
		FName Row;
		double FractionX;
		double FractionY;
	};
	const FRowCase Cases[] = {
		{ FName(TEXT("Button")), 0.2, 0.3 },
		{ FName(TEXT("VerticalBox")), 0.5, 0.6 },
		{ FName(TEXT("Texture")), 0.8, 0.3 },
	};
	for (const FRowCase& Case : Cases)
	{
		const FDreamUIControlDescriptor* Row = FindRow(Case.Row);
		if (!TestNotNull(*FString::Printf(TEXT("The palette has a row named %s"), *Case.Row.ToString()), Row))
		{
			continue;
		}
		UDreamWidget* Root = Session.GetTemplateRoot();
		const TArray<UDreamWidget*> Before = LiveChildrenOf(Root);
		const FIntPoint Pixel = PointInBox(WorkArea, Case.FractionX, Case.FractionY);
		TestTrue(*FString::Printf(TEXT("The %s row's drop is taken"), *Case.Row.ToString()), Driver.DropFromPalette(Case.Row, Pixel));
		Driver.PumpFrame();
		UDreamWidget* Made = nullptr;
		for (UDreamWidget* Child : LiveChildrenOf(Root))
		{
			if (!Before.Contains(Child))
			{
				Made = Child;
			}
		}
		if (!TestNotNull(*FString::Printf(TEXT("The %s row made a widget under the root"), *Case.Row.ToString()), Made))
		{
			continue;
		}
		if (UClass* ControlClass = Row->ControlClass.Get())
		{
			TestTrue(*FString::Printf(TEXT("...an instance of %s"), *ControlClass->GetName()), Made->IsA(ControlClass));
		}
		else if (UClass* ContainerClass = Row->LayoutContainerClass.Get())
		{
			TestTrue(*FString::Printf(TEXT("...carrying a %s"), *ContainerClass->GetName()),
				Made->GetLayoutContainer() != nullptr && Made->GetLayoutContainer()->IsA(ContainerClass));
		}
		else if (UClass* VisualClass = Row->VisualClass.Get())
		{
			TestTrue(*FString::Printf(TEXT("...carrying a %s"), *VisualClass->GetName()),
				Made->GetVisual() != nullptr && Made->GetVisual()->IsA(VisualClass));
		}
		const TOptional<FIntPoint> Landed = Driver.WidgetPixel(Driver.PreviewFor(Made));
		if (TestTrue(*FString::Printf(TEXT("The %s widget is on screen"), *Case.Row.ToString()), Landed.IsSet()))
		{
			TestTrue(FString::Printf(TEXT("...centred under the pointer: (%d, %d), dropped at (%d, %d)"), Landed->X, Landed->Y, Pixel.X, Pixel.Y),
				FMath::Abs(Landed->X - Pixel.X) <= 3 && FMath::Abs(Landed->Y - Pixel.Y) <= 3);
		}
	}
	return true;
}

#endif
