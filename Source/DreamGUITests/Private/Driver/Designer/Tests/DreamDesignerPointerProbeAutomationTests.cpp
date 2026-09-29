// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/SDreamWidgetDesignerViewport.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"

/*
 * The designer's own pointer gestures, arriving the way a mouse's do.
 *
 * Every event here goes in through FSceneViewport's ISlateViewport entry points -- the functions
 * Slate itself calls -- so the viewport's cursor cache, its key state and the client's InputKey all
 * run exactly as they do under a hand, and the tick that turns a held press into a drag is the
 * client's own. What these add over the drop probes is the other half of the viewport: the gestures
 * that are decided in the viewport client rather than handed to it whole by Slate's drag-and-drop.
 *
 * Synchronous, like the rest of the headless suite: the driver pumps the designer's frames itself.
 *
 * The widgets placed here are the palette's plain Widget row, not a control. A control measures
 * itself from its content, and whether that measure has run by the time a pixel is asked of it is a
 * question about the control; what is being asked here is where the pointer goes. A plain widget is
 * a hundred units square and says so before anything has laid it out.
 *
 * Clicking the backdrop to clear the selection is not here, and cannot honestly be: a click that
 * misses every widget is resolved by the viewport's hit-proxy map, which the null RHI reads back as
 * zeros -- the id of whatever hit proxy happens to own slot zero, often a level actor. It lives with
 * the RHI scenarios, where the map is really rendered.
 */
namespace DreamDesignerPointerProbeLocal
{
	/**
	 * Grid snapping off for one test, restored however the test ends.
	 *
	 * Snapping is a per-user preference, on by default and at whatever size the author chose, and it
	 * moves a dragged widget onto the nearest gridline. The move below is about where the pointer took
	 * the widget, not about the grid; the preference is put back without being saved.
	 */
	struct FScopedGridSnapOff
	{
		bool bWasEnabled = false;

		FScopedGridSnapOff()
		{
			UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
			bWasEnabled = Settings->bGridSnapEnabled;
			Settings->bGridSnapEnabled = false;
		}

		~FScopedGridSnapOff()
		{
			GetMutableDefault<UDreamUIDesignerSettings>()->bGridSnapEnabled = bWasEnabled;
		}
	};

	/** A pixel inside a box, as fractions of it from its top-left corner. */
	FIntPoint PointIn(const FBox2D& InBox, double InFractionX, double InFractionY)
	{
		return FIntPoint(
			FMath::RoundToInt32(FMath::Lerp(InBox.Min.X, InBox.Max.X, InFractionX)),
			FMath::RoundToInt32(FMath::Lerp(InBox.Min.Y, InBox.Max.Y, InFractionY)));
	}

	/**
	 * One design unit per pixel, and the part of the Blueprint root that is on screen.
	 *
	 * One to one so that a widget is big on screen and every handle is far from every other; the work
	 * area is the root's rect clipped to the viewport less a margin, because at that zoom a large design
	 * canvas runs off the edges and a pixel off the viewport reaches nothing.
	 */
	bool PrepareOneToOne(FAutomationTestBase& InTest, DreamTests::FDreamDesignerDriver& InDriver, FBox2D& OutWorkArea)
	{
		FDreamWidgetBlueprintEditor* Toolkit = InDriver.Toolkit();
		if (Toolkit == nullptr)
		{
			InTest.AddError(TEXT("The designer has no toolkit to zoom."));
			return false;
		}
		Toolkit->SetDesignerPixelsPerUnit(1.0f);
		InDriver.PumpFrame();
		const TOptional<FBox2D> RootRect = InDriver.WidgetPixelRect(InDriver.BlueprintRoot());
		if (!RootRect.IsSet())
		{
			InTest.AddError(TEXT("The Blueprint root does not project onto the viewport, so there is nowhere to put anything."));
			return false;
		}
		const FIntPoint Size = InDriver.ViewportPixelSize();
		const FVector2D Margin(Size.X * 0.1, Size.Y * 0.1);
		OutWorkArea = FBox2D(
			FVector2D(FMath::Max(RootRect->Min.X, Margin.X), FMath::Max(RootRect->Min.Y, Margin.Y)),
			FVector2D(FMath::Min(RootRect->Max.X, Size.X - Margin.X), FMath::Min(RootRect->Max.Y, Size.Y - Margin.Y)));
		if (OutWorkArea.Max.X - OutWorkArea.Min.X < 200.0 || OutWorkArea.Max.Y - OutWorkArea.Min.Y < 200.0)
		{
			InTest.AddError(FString::Printf(TEXT("Too little of the Blueprint root is on screen to work in: %s."), *OutWorkArea.ToString()));
			return false;
		}
		return true;
	}

	/** Drop a plain widget and hand back what it authored, saying so when it did not. */
	UDreamWidget* DropPlainWidget(FAutomationTestBase& InTest, DreamTests::FDreamDesignerDriver& InDriver, FIntPoint InPixel)
	{
		UDreamWidget* Template = DreamTests::DropOntoRootAndFindTemplate(InDriver, /*Plain Widget row*/nullptr, InPixel);
		if (Template == nullptr)
		{
			InTest.AddError(FString::Printf(TEXT("A plain widget dropped at (%d, %d) did not arrive under the Blueprint root."), InPixel.X, InPixel.Y));
			return nullptr;
		}
		InDriver.PumpFrame();
		return Template;
	}

	/** Start from nothing selected, the way the toolkit's own Select None leaves it. */
	void SelectNothing(DreamTests::FDreamDesignerDriver& InDriver)
	{
		if (FDreamWidgetBlueprintEditor* Toolkit = InDriver.Toolkit())
		{
			Toolkit->SelectWidgets(TSet<UDreamWidget*>(), /*bAppendOrToggle*/false);
		}
		InDriver.PumpFrame();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPointerMoveDragTest,
	"DreamGUI.Designer.Driver.DraggingASelectedWidgetMovesItByThePointersTravelAndUndoPutsItBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerPointerMoveDragTest::RunTest(const FString&)
{
	using namespace DreamDesignerPointerProbeLocal;

	FScopedGridSnapOff GridSnapOff;
	DreamTests::FScopedDesignerSession Session(TEXT("DesignerPointerMove"), /*bGiveRootAPanel*/true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	DreamTests::FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	// Off the canvas centre, where an unselected widget's anchor markers sit.
	UDreamWidget* Template = DropPlainWidget(*this, Driver, PointIn(WorkArea, 0.3, 0.3));
	if (Template == nullptr)
	{
		return false;
	}
	// Selected the way the hierarchy panel selects: a move needs a selection to grab, and HOW it came to
	// be selected is the next test's subject, not this one's.
	UDreamWidget* Preview = Driver.PreviewFor(Template);
	if (!TestNotNull(TEXT("The dropped widget has a preview to drag"), Preview))
	{
		return false;
	}
	Driver.Toolkit()->SelectWidgets(TSet<UDreamWidget*>({ Preview }), /*bAppendOrToggle*/false);
	Driver.PumpFrame();
	Preview = Driver.PreviewFor(Template);

	const TOptional<FBox2D> RectBefore = Driver.WidgetPixelRect(Preview);
	UDreamWidget* PreviewParent = Preview != nullptr ? Preview->GetParent() : nullptr;
	const TOptional<FBox2D> ParentRect = Driver.WidgetPixelRect(PreviewParent);
	if (!TestTrue(TEXT("The widget and its parent are both on screen"), RectBefore.IsSet() && ParentRect.IsSet()))
	{
		return false;
	}
	// The designer's scale as it applies to THIS widget: an anchored position is measured in the
	// parent's own units, so pixels-per-unit is the parent's rect on screen over the parent's size.
	// Measured rather than assumed, which keeps a canvas scaler between the view and the page honest.
	const FVector2D PixelsPerUnit(
		(ParentRect->Max.X - ParentRect->Min.X) / FMath::Max(PreviewParent->GetWidth(), 1.0f),
		(ParentRect->Max.Y - ParentRect->Min.Y) / FMath::Max(PreviewParent->GetHeight(), 1.0f));
	AddInfo(FString::Printf(TEXT("The designer reports %.3f pixels per unit; the parent measures (%.3f, %.3f)."),
		Driver.Toolkit()->GetDesignerPixelsPerUnit(), PixelsPerUnit.X, PixelsPerUnit.Y));

	// A quarter of the way in from the top-left corner: inside the rectangle, which is the Move handle,
	// and a quarter of the widget away from every resize handle and from the pivot, each of which wins
	// a press within nine pixels of it.
	const FIntPoint From = PointIn(RectBefore.GetValue(), 0.25, 0.25);
	const FIntPoint Travel(60, 40);
	const FVector2D PreviewBefore = Preview->GetAnchoredPosition();
	const FVector2D TemplateBefore = Template->GetAnchoredPosition();

	Driver.DragFromTo(From, From + Travel, /*Steps*/4);

	// Pixel Y grows downwards and an anchored position's Y grows upwards.
	const FVector2D ExpectedDelta(Travel.X / PixelsPerUnit.X, -Travel.Y / PixelsPerUnit.Y);
	const FVector2D Expected = PreviewBefore + ExpectedDelta;
	// Half a pixel's worth, since the press and every move land on whole pixels.
	const double Tolerance = 0.5 / FMath::Min(PixelsPerUnit.X, PixelsPerUnit.Y) + 0.01;
	const FVector2D Moved = Template->GetAnchoredPosition();
	TestTrue(FString::Printf(TEXT("The asset records the move in design units: expected (%.2f, %.2f), holds (%.2f, %.2f)"),
		Expected.X, Expected.Y, Moved.X, Moved.Y), Moved.Equals(Expected, Tolerance));

	const TOptional<FBox2D> RectAfter = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
	if (TestTrue(TEXT("The moved widget is still on screen"), RectAfter.IsSet()))
	{
		const FVector2D ScreenTravel = RectAfter->GetCenter() - RectBefore->GetCenter();
		TestTrue(FString::Printf(TEXT("and on screen it travelled with the pointer: (%.1f, %.1f) for (60, 40)"),
			ScreenTravel.X, ScreenTravel.Y), ScreenTravel.Equals(FVector2D(Travel), 1.5));
	}

	// One gesture, one undo step: the transaction the handle opened is the one Ctrl+Z takes back.
	TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
	Driver.PumpFrame();
	// A drag that recorded nothing would hand this undo to the drop before it, which takes the widget
	// away rather than back -- so the widget has to still be there for the next line to mean anything.
	TestTrue(TEXT("Undo took back the move, not the drop"),
		DreamTests::LiveChildrenOf(Session.GetTemplateRoot()).Contains(Template));
	TestTrue(FString::Printf(TEXT("Undo puts it back where it was: (%.2f, %.2f), holds (%.2f, %.2f)"),
		TemplateBefore.X, TemplateBefore.Y, Template->GetAnchoredPosition().X, Template->GetAnchoredPosition().Y),
		Template->GetAnchoredPosition().Equals(TemplateBefore, 0.01));
	TestTrue(TEXT("There is something to redo"), GEditor->RedoTransaction());
	Driver.PumpFrame();
	TestTrue(TEXT("and redo moves it again"), Template->GetAnchoredPosition().Equals(Expected, Tolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPointerClickSelectsTest,
	"DreamGUI.Designer.Driver.ClickingAWidgetSelectsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerPointerClickSelectsTest::RunTest(const FString&)
{
	using namespace DreamDesignerPointerProbeLocal;

	DreamTests::FScopedDesignerSession Session(TEXT("DesignerPointerClick"), /*bGiveRootAPanel*/true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	DreamTests::FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Template = DropPlainWidget(*this, Driver, PointIn(WorkArea, 0.35, 0.4));
	if (Template == nullptr)
	{
		return false;
	}
	SelectNothing(Driver);
	if (!TestEqual(TEXT("Nothing is selected before the click"), Driver.SelectedWidgets().Num(), 0))
	{
		return false;
	}
	const TOptional<FBox2D> Rect = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
	if (!TestTrue(TEXT("The widget is on screen"), Rect.IsSet()))
	{
		return false;
	}

	// Nothing is selected, so there are no handles to grab and the press arms a marquee that never
	// travels. The release is then the engine's click, and the designer's ProcessClick picks the
	// topmost widget under it -- the dropped one, which sits above the root it was dropped on.
	Driver.ClickAt(PointIn(Rect.GetValue(), 0.5, 0.5));

	const TArray<UDreamWidget*> Selected = Driver.SelectedWidgets();
	TestEqual(TEXT("One widget is selected"), Selected.Num(), 1);
	TestTrue(TEXT("and it is the one under the click"), Selected.Num() == 1 && Selected[0] == Driver.PreviewFor(Template));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPointerMarqueeTest,
	"DreamGUI.Designer.Driver.AMarqueeDraggedAcrossTwoWidgetsSelectsBoth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerPointerMarqueeTest::RunTest(const FString&)
{
	using namespace DreamDesignerPointerProbeLocal;

	DreamTests::FScopedDesignerSession Session(TEXT("DesignerPointerMarquee"), /*bGiveRootAPanel*/true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	DreamTests::FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Left = DropPlainWidget(*this, Driver, PointIn(WorkArea, 0.3, 0.5));
	UDreamWidget* Right = DropPlainWidget(*this, Driver, PointIn(WorkArea, 0.7, 0.5));
	if (Left == nullptr || Right == nullptr)
	{
		return false;
	}
	SelectNothing(Driver);

	const TOptional<FBox2D> LeftRect = Driver.WidgetPixelRect(Driver.PreviewFor(Left));
	const TOptional<FBox2D> RightRect = Driver.WidgetPixelRect(Driver.PreviewFor(Right));
	if (!TestTrue(TEXT("Both widgets are on screen"), LeftRect.IsSet() && RightRect.IsSet()))
	{
		return false;
	}
	// From empty page above and to the left of both, to below and to the right of both. The press lands
	// on the root's background: with nothing selected there is no handle to grab, so it arms a marquee.
	FBox2D Around = LeftRect.GetValue();
	Around += RightRect.GetValue();
	const FIntPoint Size = Driver.ViewportPixelSize();
	const FIntPoint From(FMath::Max(FMath::RoundToInt32(Around.Min.X) - 24, 1), FMath::Max(FMath::RoundToInt32(Around.Min.Y) - 24, 1));
	const FIntPoint To(FMath::Min(FMath::RoundToInt32(Around.Max.X) + 24, Size.X - 2), FMath::Min(FMath::RoundToInt32(Around.Max.Y) + 24, Size.Y - 2));

	Driver.DragFromTo(From, To, /*Steps*/6);

	const TArray<UDreamWidget*> Selected = Driver.SelectedWidgets();
	AddInfo(FString::Printf(TEXT("The marquee left %d widget(s) selected."), Selected.Num()));
	TestTrue(TEXT("The widget on the left is selected"), Selected.Contains(Driver.PreviewFor(Left)));
	TestTrue(TEXT("and so is the widget on the right"), Selected.Contains(Driver.PreviewFor(Right)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPointerContextMenuTest,
	"DreamGUI.Designer.Driver.RightClickingAWidgetSelectsItAndAsksForTheContextMenu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerPointerContextMenuTest::RunTest(const FString&)
{
	using namespace DreamDesignerPointerProbeLocal;

	DreamTests::FScopedDesignerSession Session(TEXT("DesignerPointerMenu"), /*bGiveRootAPanel*/true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	DreamTests::FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Template = DropPlainWidget(*this, Driver, PointIn(WorkArea, 0.35, 0.4));
	if (Template == nullptr)
	{
		return false;
	}
	SelectNothing(Driver);
	const TOptional<FBox2D> Rect = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
	if (!TestTrue(TEXT("The widget is on screen"), Rect.IsSet()))
	{
		return false;
	}

	// The menu is summoned only if Slate can find a path to the viewport, which is the first thing
	// SDreamWidgetDesignerViewport::SummonContextMenu asks; an unattended editor that never laid the
	// window out may have none. Asked the same way here, beforehand, so a missing menu can be told apart
	// from a menu that was never asked for.
	FWidgetPath PathToViewport;
	const TSharedPtr<SDreamWidgetDesignerViewport> Shell = Driver.ViewportShell();
	const bool bCanHostAMenu = Shell.IsValid()
		&& FSlateApplication::Get().GeneratePathToWidgetUnchecked(Shell.ToSharedRef(), PathToViewport);
	FSlateApplication::Get().DismissAllMenus();

	Driver.ClickAt(PointIn(Rect.GetValue(), 0.5, 0.5), EKeys::RightMouseButton);

	// The engine's right-click rule, which ProcessClick states: a widget that is not selected yet
	// becomes the selection, so the menu that opens is about the thing under the cursor.
	const TArray<UDreamWidget*> Selected = Driver.SelectedWidgets();
	TestTrue(TEXT("The widget under the right-click is selected"),
		Selected.Num() == 1 && Selected[0] == Driver.PreviewFor(Template));
	if (bCanHostAMenu)
	{
		TestTrue(TEXT("and a context menu is up: the release asked for one and Slate had somewhere to put it"),
			FSlateApplication::Get().AnyMenusVisible());
	}
	else
	{
		AddInfo(TEXT("Slate finds no path to the designer viewport in this session, so no menu can be hosted and ")
			TEXT("whether one was asked for is not observable here. Only the selection half is asserted."));
	}
	FSlateApplication::Get().DismissAllMenus();
	return true;
}

#endif
