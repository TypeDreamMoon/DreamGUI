// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"

#include "Editor.h"
#include "EditorViewportClient.h"
#include "Framework/Application/SlateApplication.h"

/*
 * The arrow keys, the wheel, the right button and F on the design surface, as an author's keyboard and mouse deliver them
 * to the viewport (FDreamDesignerDriver, through FSceneViewport's ISlateViewport entry points).
 *
 * The nudge is UMG's designer nudge (SDesignerView::OnKeyDown, NudgeSelectedWidget): an arrow moves the selection by the
 * grid's step and each press is an undo step of its own. UMG has no larger step for Shift, and neither does the level
 * editor's nudge (FLevelEditorViewportClient::NudgeSelectedObjects): Shift held changes nothing about the step, which is
 * what the test of it holds the designer to.
 *
 * The view follows UMG's design surface (SDesignSurface): the wheel zooms about the point under the pointer
 * (OnMouseWheel, ChangeZoomLevel at the cursor), the right button drags the view along with the pointer (OnMouseMove while
 * panning), and framing puts what is asked for in view. The designer gets the first two from the level editor's camera,
 * whose two preferences for them (bCenterZoomAroundCursor, bPanMovesCanvas) are pinned to the engine's defaults here.
 */
namespace DreamDesignerViewportViewTestLocal
{
	using namespace DreamTests;

	/** A nudge, as a keyboard makes one: the arrow down, then up, with whatever modifiers are held. */
	void PressArrow(FDreamDesignerDriver& InDriver, const FKey& InArrow)
	{
		InDriver.KeyDown(InArrow);
		InDriver.KeyUp(InArrow);
		InDriver.PumpFrame();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerShiftNudgeTest,
	"DreamGUI.Designer.Driver.ShiftWithAnArrowNudgesByTheSameGridStepAsTheArrowAloneAndUndoesInOneStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A widget put on a gridline by a snapped drag, then Right with Shift held: it moves one grid step, the step Right alone
 * moves it -- UMG nudges by GridSnapSize whatever is held -- Shift is let go of cleanly, nothing is left open on the
 * transaction stack, and one undo takes the whole nudge back.
 */
bool FDreamDesignerShiftNudgeTest::RunTest(const FString&)
{
	using namespace DreamDesignerViewportViewTestLocal;
	constexpr float GridSize = 10.0f;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(true, GridSize);
	Preferences.SetGuides(false);
	FScopedDesignerSession Session(TEXT("DesignerShiftNudge"), /*bGiveRootAPanel*/ true);
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
	UDreamWidget* Template = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.4, 0.45));
	if (Template == nullptr)
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Template);
	Driver.PumpFrame();
	// Onto the grid by a snapped drag, so a step of the grid starting from here is a step on it, as UMG's would be.
	const TOptional<FBox2D> Rect = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
	if (!TestTrue(TEXT("The widget is on screen"), Rect.IsSet()))
	{
		return false;
	}
	const FIntPoint From = PointInBox(Rect.GetValue(), 0.25, 0.25);
	TestTrue(TEXT("The snapped drag onto the grid completes"), Driver.DragFromTo(From, From + FIntPoint(13, 7), /*Steps*/ 3));
	const FVector2D OnGrid = Template->GetAnchoredPosition();

	TestTrue(TEXT("Shift goes down"), Driver.HoldModifiers(EDreamDriverModifierKeys::Shift));
	PressArrow(Driver, EKeys::Right);
	TestTrue(TEXT("...and comes up"), Driver.ReleaseModifiers());
	Driver.PumpFrame();

	const FVector2D AfterShift = Template->GetAnchoredPosition();
	TestTrue(FString::Printf(TEXT("Shift+Right moves the widget one grid step right: %s -> %s"), *OnGrid.ToString(), *AfterShift.ToString()),
		AfterShift.Equals(OnGrid + FVector2D(GridSize, 0.0), 0.01));
	TestFalse(TEXT("No transaction is left open"), GEditor->IsTransactionActive());

	PressArrow(Driver, EKeys::Right);
	const FVector2D AfterPlain = Template->GetAnchoredPosition();
	TestTrue(FString::Printf(TEXT("...which is the step Right alone takes: %s -> %s"), *AfterShift.ToString(), *AfterPlain.ToString()),
		AfterPlain.Equals(AfterShift + FVector2D(GridSize, 0.0), 0.01));

	TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
	Driver.PumpFrame();
	TestTrue(TEXT("One undo takes back the last nudge alone"), Template->GetAnchoredPosition().Equals(AfterShift, 0.01));
	TestTrue(TEXT("There is something more to undo"), GEditor->UndoTransaction());
	Driver.PumpFrame();
	TestTrue(FString::Printf(TEXT("...and one more takes back the Shift nudge, whole: %s"), *Template->GetAnchoredPosition().ToString()),
		Template->GetAnchoredPosition().Equals(OnGrid, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerSeparateNudgesTest,
	"DreamGUI.Designer.Driver.ThreeSeparateArrowPressesAreThreeUndoStepsAndRedoReplaysThemInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Up, Up, Left, each pressed and let go: three nudges, and three undo steps, as UMG opens one transaction per nudge.
 * Undoing walks them back one at a time and redoing walks them forward in the order they were made.
 */
bool FDreamDesignerSeparateNudgesTest::RunTest(const FString&)
{
	using namespace DreamDesignerViewportViewTestLocal;
	constexpr float GridSize = 10.0f;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(true, GridSize);
	Preferences.SetGuides(false);
	FScopedDesignerSession Session(TEXT("DesignerSeparateNudges"), /*bGiveRootAPanel*/ true);
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
	UDreamWidget* Template = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.55, 0.5));
	if (Template == nullptr)
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Template);
	Driver.PumpFrame();
	const FVector2D Start = Template->GetAnchoredPosition();

	PressArrow(Driver, EKeys::Up);
	const FVector2D AfterFirst = Template->GetAnchoredPosition();
	PressArrow(Driver, EKeys::Up);
	const FVector2D AfterSecond = Template->GetAnchoredPosition();
	PressArrow(Driver, EKeys::Left);
	const FVector2D AfterThird = Template->GetAnchoredPosition();

	// An anchored position's Y grows upwards, which is the way Up moves.
	TestTrue(FString::Printf(TEXT("Up moves the widget up a grid step: %s -> %s"), *Start.ToString(), *AfterFirst.ToString()),
		AfterFirst.Equals(Start + FVector2D(0.0, GridSize), 0.01));
	TestTrue(TEXT("...and again"), AfterSecond.Equals(AfterFirst + FVector2D(0.0, GridSize), 0.01));
	TestTrue(TEXT("Left moves it left a grid step"), AfterThird.Equals(AfterSecond + FVector2D(-GridSize, 0.0), 0.01));

	const FVector2D Steps[] = { AfterSecond, AfterFirst, Start };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestTrue(*FString::Printf(TEXT("Undo %d has something to undo"), Index + 1), GEditor->UndoTransaction());
		Driver.PumpFrame();
		TestTrue(FString::Printf(TEXT("Undo %d takes one nudge back: %s, expected %s"), Index + 1,
			*Template->GetAnchoredPosition().ToString(), *Steps[Index].ToString()), Template->GetAnchoredPosition().Equals(Steps[Index], 0.01));
	}
	const FVector2D Forward[] = { AfterFirst, AfterSecond, AfterThird };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestTrue(*FString::Printf(TEXT("Redo %d has something to redo"), Index + 1), GEditor->RedoTransaction());
		Driver.PumpFrame();
		TestTrue(FString::Printf(TEXT("Redo %d replays one nudge: %s, expected %s"), Index + 1,
			*Template->GetAnchoredPosition().ToString(), *Forward[Index].ToString()), Template->GetAnchoredPosition().Equals(Forward[Index], 0.01));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerWheelZoomTest,
	"DreamGUI.Designer.Driver.TheWheelZoomsInAndOutAboutThePointUnderThePointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The pointer on a widget and the wheel turned away from the author: the view zooms in -- more pixels to a design unit,
 * the widget bigger on screen by the same ratio -- and the widget's middle stays under the pointer, as UMG's surface keeps
 * the point under the cursor where it is (SDesignSurface::OnMouseWheel). Turned back, the view zooms out to where it was.
 */
bool FDreamDesignerWheelZoomTest::RunTest(const FString&)
{
	using namespace DreamDesignerViewportViewTestLocal;
	FScopedViewportCameraPreferences CameraPreferences(/*bCenterZoomAroundCursor*/ true, /*bPanMovesCanvas*/ true);
	FScopedDesignerSession Session(TEXT("DesignerWheelZoom"), /*bGiveRootAPanel*/ true);
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
	UDreamWidget* Template = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.3, 0.35));
	if (Template == nullptr)
	{
		return false;
	}
	SelectNothingInDesigner(Driver);
	Driver.PumpFrame();
	FDreamWidgetBlueprintEditor* Toolkit = Driver.Toolkit();
	const TOptional<FBox2D> Before = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
	if (!TestTrue(TEXT("The widget is on screen"), Before.IsSet() && Toolkit != nullptr))
	{
		return false;
	}
	const float ZoomBefore = Toolkit->GetDesignerPixelsPerUnit();
	const FIntPoint Pointer(FMath::RoundToInt32(Before->GetCenter().X), FMath::RoundToInt32(Before->GetCenter().Y));

	TestTrue(TEXT("The wheel turn away from the author reaches the viewport"), Driver.Wheel(Pointer, 1.0f));
	Driver.PumpFrame();

	const float ZoomIn = Toolkit->GetDesignerPixelsPerUnit();
	const TOptional<FBox2D> In = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
	TestTrue(FString::Printf(TEXT("The view zoomed in: %.4f pixels a unit, was %.4f"), ZoomIn, ZoomBefore), ZoomIn > ZoomBefore * 1.01f);
	if (TestTrue(TEXT("The widget is still on screen"), In.IsSet()))
	{
		const double Ratio = (In->Max.X - In->Min.X) / FMath::Max(Before->Max.X - Before->Min.X, 1.0);
		TestTrue(FString::Printf(TEXT("...bigger by the zoom's own ratio: %.3f, the zoom went up %.3f"), Ratio, ZoomIn / ZoomBefore),
			FMath::IsNearlyEqual(Ratio, static_cast<double>(ZoomIn / ZoomBefore), 0.02));
		TestTrue(FString::Printf(TEXT("...with its middle still under the pointer: %s, the pointer at (%d, %d)"), *In->GetCenter().ToString(), Pointer.X, Pointer.Y),
			In->GetCenter().Equals(FVector2D(Pointer), 2.0));
	}

	TestTrue(TEXT("The wheel turn back reaches the viewport"), Driver.Wheel(Pointer, -1.0f));
	Driver.PumpFrame();
	const float ZoomOut = Toolkit->GetDesignerPixelsPerUnit();
	TestTrue(FString::Printf(TEXT("The view zoomed back out: %.4f pixels a unit, was %.4f before either turn"), ZoomOut, ZoomBefore),
		ZoomOut < ZoomIn && FMath::IsNearlyEqual(ZoomOut, ZoomBefore, ZoomBefore * 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRightButtonPanTest,
	"DreamGUI.Designer.Driver.DraggingWithTheRightButtonPansTheViewAndChangesNothingInTheAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A right-button drag across empty canvas: the view moves with the pointer -- the widget on screen goes the way the
 * pointer went, by about as far -- the zoom does not change, the asset is untouched, and the release brings up no
 * context menu, because a right button that travelled was a pan (UMG: SDesignSurface pans on the right button, and its
 * context menu needs a right click that did not move).
 */
bool FDreamDesignerRightButtonPanTest::RunTest(const FString&)
{
	using namespace DreamDesignerViewportViewTestLocal;
	FScopedViewportCameraPreferences CameraPreferences(/*bCenterZoomAroundCursor*/ true, /*bPanMovesCanvas*/ true);
	FScopedDesignerSession Session(TEXT("DesignerRightButtonPan"), /*bGiveRootAPanel*/ true);
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
	UDreamWidget* Template = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.3, 0.3));
	if (Template == nullptr)
	{
		return false;
	}
	SelectNothingInDesigner(Driver);
	Driver.PumpFrame();
	FDreamWidgetBlueprintEditor* Toolkit = Driver.Toolkit();
	const TOptional<FBox2D> Before = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
	if (!TestTrue(TEXT("The widget is on screen"), Before.IsSet() && Toolkit != nullptr))
	{
		return false;
	}
	const float ZoomBefore = Toolkit->GetDesignerPixelsPerUnit();
	const FVector2D PositionBefore = Template->GetAnchoredPosition();
	FSlateApplication::Get().DismissAllMenus();
	// From empty canvas well away from the widget.
	const FIntPoint From = PointInBox(WorkArea, 0.7, 0.7);
	const FIntPoint Travel(80, 50);

	TestTrue(TEXT("The right-button drag completes"), Driver.DragWithButton(EKeys::RightMouseButton, From, From + Travel, /*Steps*/ 5));

	const TOptional<FBox2D> After = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
	if (TestTrue(TEXT("The widget is still on screen"), After.IsSet()))
	{
		const FVector2D Moved = After->GetCenter() - Before->GetCenter();
		TestTrue(FString::Printf(TEXT("The view went with the pointer: the widget moved (%.1f, %.1f) on screen for a drag of (%d, %d)"),
			Moved.X, Moved.Y, Travel.X, Travel.Y),
			FMath::IsNearlyEqual(Moved.X, static_cast<double>(Travel.X), Travel.X * 0.25 + 2.0)
			&& FMath::IsNearlyEqual(Moved.Y, static_cast<double>(Travel.Y), Travel.Y * 0.25 + 2.0));
	}
	TestTrue(TEXT("The zoom did not change"), FMath::IsNearlyEqual(Toolkit->GetDesignerPixelsPerUnit(), ZoomBefore, ZoomBefore * 0.001f));
	TestTrue(TEXT("The asset did not change: the widget is where it was in its parent"), Template->GetAnchoredPosition().Equals(PositionBefore, 0.001));
	TestFalse(TEXT("No context menu came up for a right button that travelled"), FSlateApplication::Get().AnyMenusVisible());
	FSlateApplication::Get().DismissAllMenus();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerFrameKeyTest,
	"DreamGUI.Designer.Driver.TheFKeyFramesTheSelectionAndWithNothingSelectedTheWholeDesignCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Zoomed far in and panned off to one side, F on the viewport frames what is selected -- the selected widget comes to the
 * middle of the view, whole -- and with nothing selected F frames the design canvas, all of it in view: UMG's surface
 * zooms to fit its content on the same request. The camera gets there over a short flight timed in real time
 * (FViewportCameraTransform's transition curve), so this runs across engine frames and waits for the flight to land.
 */
bool FDreamDesignerFrameKeyTest::RunTest(const FString&)
{
	using namespace DreamTests;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerFrameKey"));
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		FDreamWidgetBlueprintEditor* Toolkit = Driver.Toolkit();
		UDreamWidget* Template = DropOntoRootAndFindTemplate(Driver, /*Plain Widget row*/ nullptr, PointInBox(
			FBox2D(FVector2D::ZeroVector, FVector2D(Driver.ViewportPixelSize())), 0.3, 0.3));
		if (!TestNotNull(TEXT("A plain widget was dropped under the root"), Template) || Toolkit == nullptr)
		{
			State->bAlive = false;
			return;
		}
		State->Made.Add(TEXT("Target"), Template);
		SelectOnlyInDesigner(Driver, Template);
		// Somewhere the widget is not in the middle: zoomed in four times over, which leaves it off to one side.
		Toolkit->SetDesignerPixelsPerUnit(Toolkit->GetDesignerPixelsPerUnit() * 4.0f);
	});
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		TestTrue(TEXT("F reaches the viewport"), State->Driver->KeyDown(EKeys::F));
		State->Driver->KeyUp(EKeys::F);
	});
	const auto CameraLanded = [State]()
	{
		FEditorViewportClient* Client = State->Driver.IsValid() ? State->Driver->ViewportClient() : nullptr;
		return Client != nullptr && !Client->GetViewTransform().IsPlaying();
	};
	EnqueueDesignerUntil(State, this, CameraLanded, 3.0, TEXT("the camera's flight to the selection to land"));
	EnqueueDesignerAction(State, [this, State]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		const TOptional<FBox2D> Rect = Driver.WidgetPixelRect(Driver.PreviewFor(State->Get(TEXT("Target"))));
		const FVector2D Size(Driver.ViewportPixelSize());
		if (TestTrue(TEXT("The selected widget is on screen after F"), Rect.IsSet()))
		{
			TestTrue(FString::Printf(TEXT("...in the middle of the view: its middle at %s, the view's at %s"), *Rect->GetCenter().ToString(), *(Size * 0.5).ToString()),
				Rect->GetCenter().Equals(Size * 0.5, FMath::Max(Size.X, Size.Y) * 0.02));
			TestTrue(TEXT("...and whole"), Rect->Min.X >= 0.0 && Rect->Min.Y >= 0.0 && Rect->Max.X <= Size.X && Rect->Max.Y <= Size.Y);
			if (!Rect->GetCenter().Equals(Size * 0.5, FMath::Max(Size.X, Size.Y) * 0.02))
			{
				// Diagnose the readiness check without changing the assertions above: the curve's
				// clock may have ended before the viewport applied its final camera position.
				FEditorViewportClient* Client = Driver.ViewportClient();
				FBoxSphereBounds Bounds(EForceInit::ForceInitToZero);
				const bool bHasBounds = Driver.Toolkit()->GetSelectedObjectsBounds(Bounds);
				const auto DescribeCamera = [&Driver, Client, State, Bounds, bHasBounds]()
				{
					const TOptional<FBox2D> CurrentRect = Driver.WidgetPixelRect(Driver.PreviewFor(State->Get(TEXT("Target"))));
					return FString::Printf(TEXT("camera=%s selectedBoundsCenter=%s curvePlaying=%d pixelsPerUnit=%.6f widgetCenter=%s"),
						Client != nullptr ? *Client->GetViewTransform().GetLocation().ToString() : TEXT("unavailable"),
						bHasBounds ? *Bounds.Origin.ToString() : TEXT("unavailable"),
						Client != nullptr && Client->GetViewTransform().IsPlaying() ? 1 : 0,
						Driver.Toolkit()->GetDesignerPixelsPerUnit(),
						CurrentRect.IsSet() ? *CurrentRect->GetCenter().ToString() : TEXT("unavailable"));
				};
				AddInfo(TEXT("After the curve ended, before a driver viewport tick: ") + DescribeCamera());
				Driver.PumpFrame(0.0f);
				AddInfo(TEXT("After one driver viewport tick: ") + DescribeCamera());
				Driver.PumpFrame(0.0f);
				AddInfo(TEXT("After two driver viewport ticks: ") + DescribeCamera());
			}

		}
		SelectNothingInDesigner(Driver);
		Driver.Toolkit()->SetDesignerPixelsPerUnit(Driver.Toolkit()->GetDesignerPixelsPerUnit() * 4.0f);
	});
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		TestTrue(TEXT("F with nothing selected reaches the viewport"), State->Driver->KeyDown(EKeys::F));
		State->Driver->KeyUp(EKeys::F);
	});
	EnqueueDesignerUntil(State, this, CameraLanded, 3.0, TEXT("the camera's flight to the whole canvas to land"));
	EnqueueDesignerAction(State, [this, State]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		const TOptional<FBox2D> Canvas = Driver.WidgetPixelRect(Driver.BlueprintRoot());
		const FVector2D Size(Driver.ViewportPixelSize());
		if (TestTrue(TEXT("The design canvas is on screen after F"), Canvas.IsSet()))
		{
			TestTrue(FString::Printf(TEXT("...all of it in view: %s in a view of %s"), *Canvas->ToString(), *Size.ToString()),
				Canvas->Min.X >= -1.0 && Canvas->Min.Y >= -1.0 && Canvas->Max.X <= Size.X + 1.0 && Canvas->Max.Y <= Size.Y + 1.0);
			TestTrue(TEXT("...filling the view on at least one axis, as a fit does"),
				(Canvas->Max.X - Canvas->Min.X) >= Size.X * 0.5 || (Canvas->Max.Y - Canvas->Min.Y) >= Size.Y * 0.5);
		}
	});
	EnqueueDesignerTeardown(State);
	return true;
}

#endif
