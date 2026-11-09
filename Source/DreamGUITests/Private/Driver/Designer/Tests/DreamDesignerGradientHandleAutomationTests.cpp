// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerDuiFixture.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"

#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamDesignerGradientHandles.h"
#include "Designer/DreamDesignerHandles.h"
#include "Designer/DreamGradientHandleMath.h"
#include "DreamUICaptureLibrary.h"
#include "RenderingThread.h"
#include "RHI/DreamPixelProbe.h"
#include "Slate/SceneViewport.h"

/*
 * A rect block's gradient handles in the designer's viewport, on a class whose hierarchy is a .dui: a gradient's end dragged
 * moves it in the preview and in the asset, writes the gradient into the file when the button comes up and is one undo
 * step, file and all; Esc during the drag puts it back and leaves no step; a double click on the line adds a stop; and a
 * rect block still written the two-colour way is drawn with handles too, its first drag writing the gradient's line in
 * place of the two-colour ones.
 *
 * Every press, move, key and release goes in through the viewport's own entry points (FDreamDesignerDriver). Where a handle
 * stands is worked out as the gradient handles work it out (DreamGradientHandles), from the preview's gradient, and put on
 * screen through the widget's transform.
 */
namespace DreamDesignerGradientHandleTestLocal
{
	using namespace DreamTests;

	/** The .dui of one rect block with InLines on it, in a canvas panel. */
	TArray<FString> MakeFile(TConstArrayView<const TCHAR*> InLines)
	{
		TArray<FString> Lines = {
			TEXT("Widget Root {"),
			TEXT("    + CanvasPanel"),
			TEXT("    RectBlock Card {"),
			TEXT("        AnchorData.SizeDelta = (300, 160)"),
		};
		for (const TCHAR* Line : InLines)
		{
			Lines.Add(FString(TEXT("        ")) + Line);
		}
		Lines.Add(TEXT("    }"));
		Lines.Add(TEXT("}"));
		return Lines;
	}

	/** Where point InPoint of the preview rect block's body gradient line is on screen. */
	bool FindGradientPoint(const FDreamDesignerDriver& InDriver, const UDreamWidget* InPreview, EDreamGradientPoint InPoint, FVector2D& OutPixel)
	{
		const UDreamRectBlock* Block = ::IsValid(InPreview) ? Cast<UDreamRectBlock>(InPreview->GetVisual()) : nullptr;
		if (Block == nullptr)
		{
			return false;
		}
		const FDreamGradient Gradient = Block->GetEffectiveBodyGradient();
		FDreamGradientLine Line;
		const float Aspect = InPreview->GetWidth() / FMath::Max(InPreview->GetHeight(), 1.0f);
		if (!DreamGradientHandles::GetLine(Gradient, Aspect, Line))
		{
			return false;
		}
		const FVector2D Local = FDreamDesignerHandles::RectFractionToLocal(InPreview, DreamGradientHandles::GetPoint(Line, InPoint));
		TArray<FVector2D> Pixels;
		if (!InDriver.WorldToPixels(TArray<FVector>({ InPreview->GetWorldTransform().TransformPosition(FVector(0.0, Local.X, Local.Y)) }), Pixels))
		{
			return false;
		}
		OutPixel = Pixels[0];
		return true;
	}

	/** Where the line is half way along, on screen. */
	bool FindLineMiddle(const FDreamDesignerDriver& InDriver, const UDreamWidget* InPreview, FVector2D& OutPixel)
	{
		FVector2D Start;
		FVector2D End;
		if (!FindGradientPoint(InDriver, InPreview, EDreamGradientPoint::Start, Start) || !FindGradientPoint(InDriver, InPreview, EDreamGradientPoint::End, End))
		{
			return false;
		}
		OutPixel = (Start + End) * 0.5;
		return true;
	}

	FIntPoint RoundPixel(const FVector2D& InPixel)
	{
		return FIntPoint(FMath::RoundToInt32(InPixel.X), FMath::RoundToInt32(InPixel.Y));
	}

	/** A hand's drag: down at InFrom, InSteps moves to InTo with a frame after each; the release is the caller's. */
	void PressAndCarry(FDreamDesignerDriver& InDriver, FIntPoint InFrom, FIntPoint InTo, int32 InSteps)
	{
		InDriver.MoveTo(InFrom);
		InDriver.PumpFrame();
		InDriver.Press(EKeys::LeftMouseButton);
		InDriver.PumpFrame();
		for (int32 Step = 1; Step <= InSteps; ++Step)
		{
			InDriver.MoveTo(InFrom + (InTo - InFrom) * Step / InSteps);
			InDriver.PumpFrame();
		}
	}

	void LetGo(FDreamDesignerDriver& InDriver)
	{
		InDriver.Release(EKeys::LeftMouseButton);
		InDriver.PumpFrame();
	}

	/** The text-authored designer, one to one, with the file's rect block selected; its template and preview. */
	bool Prepare(FAutomationTestBase& InTest, FScopedDuiDesigner& InScoped, UDreamWidget*& OutTemplate, UDreamWidget*& OutPreview)
	{
		if (!InScoped.IsReady())
		{
			InTest.AddError(FString::Printf(TEXT("No text-authored designer to drive: %s."), *InScoped.Failure));
			return false;
		}
		FDreamDesignerDriver& Driver = *InScoped.Driver;
		FBox2D WorkArea(ForceInit);
		if (!PrepareDesignerOneToOne(InTest, Driver, WorkArea))
		{
			return false;
		}
		OutTemplate = InScoped.FindTemplate(TEXT("Card"));
		if (!InTest.TestNotNull(TEXT("The file's rect block is in the asset"), OutTemplate))
		{
			return false;
		}
		SelectOnlyInDesigner(Driver, OutTemplate);
		Driver.PumpFrame();
		OutPreview = Driver.PreviewFor(OutTemplate);
		return InTest.TestNotNull(TEXT("and in the preview"), OutPreview);
	}

	const FDreamGradient& BodyGradientOf(const UDreamWidget* InWidget)
	{
		static const FDreamGradient None;
		const UDreamRectBlock* Block = ::IsValid(InWidget) ? Cast<UDreamRectBlock>(InWidget->GetVisual()) : nullptr;
		return Block != nullptr ? Block->GetBodyGradient() : None;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerGradientHandleDragTest,
	"DreamGUI.Designer.Driver.DraggingAGradientsEndMovesItWritesTheDuiAndIsOneUndoStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerGradientHandleDragTest::RunTest(const FString&)
{
	using namespace DreamDesignerGradientHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDuiDesigner Scoped(TEXT("DesignerGradientDrag"), MakeFile({
		TEXT("bEnableBodyGradient = true"),
		TEXT("BodyGradient = \"linear-gradient(90deg, #FF0000, #0000FF)\"") }));
	UDreamWidget* Template = nullptr;
	UDreamWidget* Preview = nullptr;
	if (!Prepare(*this, Scoped, Template, Preview))
	{
		return false;
	}
	FDreamDesignerDriver& Driver = *Scoped.Driver;
	const FString TextBefore = Scoped.ReadFile();
	const FDreamGradient GradientBefore = BodyGradientOf(Template);
	FVector2D End;
	if (!TestTrue(TEXT("The gradient's end is on screen"), FindGradientPoint(Driver, Preview, EDreamGradientPoint::End, End)))
	{
		return false;
	}

	// The end carried down: the gradient turns towards it.
	PressAndCarry(Driver, RoundPixel(End), RoundPixel(End + FVector2D(0.0, 60.0)), 5);
	TestTrue(TEXT("While dragged, the preview's gradient follows"), BodyGradientOf(Driver.PreviewFor(Template)).Angle > 91.0f);
	LetGo(Driver);
	Driver.PumpFrame();
	const FDreamGradient After = BodyGradientOf(Template);
	TestTrue(*FString::Printf(TEXT("The asset's gradient turned: %.2f degrees"), After.Angle), After.Angle > 91.0f && After.Angle < 180.0f);
	TestTrue(TEXT("its stops as they were"), After.Stops == GradientBefore.Stops);
	const FString TextAfter = Scoped.ReadFile();
	if (!TestTrue(TEXT("The file takes the gradient"), TextAfter != TextBefore && TextAfter.Contains(TEXT("BodyGradient = \"linear-gradient(")) && !TextAfter.Contains(TEXT("(90deg,"))))
	{
		AddInfo(FString::Printf(TEXT("The file reads:\n%s"), *TextAfter));
	}

	TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
	Driver.PumpFrame();
	TestEqualSensitive(TEXT("One undo takes the file back"), Scoped.ReadFile(), TextBefore);
	TestTrue(TEXT("and the asset's gradient"), BodyGradientOf(Template) == GradientBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerGradientHandleEscapeTest,
	"DreamGUI.Designer.Driver.EscDuringAGradientHandleDragPutsItBackAndLeavesNoStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerGradientHandleEscapeTest::RunTest(const FString&)
{
	using namespace DreamDesignerGradientHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDuiDesigner Scoped(TEXT("DesignerGradientEscape"), MakeFile({
		TEXT("bEnableBodyGradient = true"),
		TEXT("BodyGradient = \"radial-gradient(#FFFFFF, #000000)\"") }));
	UDreamWidget* Template = nullptr;
	UDreamWidget* Preview = nullptr;
	if (!Prepare(*this, Scoped, Template, Preview))
	{
		return false;
	}
	FDreamDesignerDriver& Driver = *Scoped.Driver;
	const FString TextBefore = Scoped.ReadFile();
	const FDreamGradient GradientBefore = BodyGradientOf(Preview);
	FVector2D Centre;
	if (!TestTrue(TEXT("The gradient's centre is on screen"), FindGradientPoint(Driver, Preview, EDreamGradientPoint::Center, Centre)))
	{
		return false;
	}
	const FUndoMark Mark = FUndoMark::Now();
	PressAndCarry(Driver, RoundPixel(Centre), RoundPixel(Centre + FVector2D(50.0, 30.0)), 4);
	TestFalse(TEXT("While dragged, the centre moves"), BodyGradientOf(Driver.PreviewFor(Template)) == GradientBefore);
	Driver.KeyDown(EKeys::Escape);
	Driver.PumpFrame();
	Driver.KeyUp(EKeys::Escape);
	LetGo(Driver);
	Driver.PumpFrame();
	TestTrue(TEXT("Esc puts the preview's gradient back"), BodyGradientOf(Driver.PreviewFor(Template)) == GradientBefore);
	TestTrue(TEXT("the asset's is untouched"), BodyGradientOf(Template) == GradientBefore);
	TestTrue(TEXT("no undo step is left"), FUndoMark::Now() == Mark);
	TestEqualSensitive(TEXT("and the file is as it was"), Scoped.ReadFile(), TextBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerGradientHandleStopsTest,
	"DreamGUI.Designer.Driver.ADoubleClickOnAGradientsLineAddsAStopAndAStopDraggedOffItIsTakenOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerGradientHandleStopsTest::RunTest(const FString&)
{
	using namespace DreamDesignerGradientHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDuiDesigner Scoped(TEXT("DesignerGradientStops"), MakeFile({
		TEXT("bEnableBodyGradient = true"),
		TEXT("BodyGradient = \"linear-gradient(90deg, #FF0000, #0000FF)\"") }));
	UDreamWidget* Template = nullptr;
	UDreamWidget* Preview = nullptr;
	if (!Prepare(*this, Scoped, Template, Preview))
	{
		return false;
	}
	FDreamDesignerDriver& Driver = *Scoped.Driver;
	FVector2D Middle;
	if (!TestTrue(TEXT("The line's middle is on screen"), FindLineMiddle(Driver, Preview, Middle)))
	{
		return false;
	}
	TestTrue(TEXT("A double click on the line is taken"), Driver.DoubleClickAt(RoundPixel(Middle)));
	Driver.PumpFrame();
	const FDreamGradient Added = BodyGradientOf(Template);
	if (!TestEqual(TEXT("A third stop is added"), Added.Stops.Num(), 3))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("half way along: %.3f"), Added.Stops[1].Position), FMath::IsNearlyEqual(Added.Stops[1].Position, 0.5f, 0.03f));
	TestTrue(TEXT("in the colour already there, between red and blue"), Added.Stops[1].Color.R > 30 && Added.Stops[1].Color.B > 30);
	TestTrue(TEXT("The file says three stops"), Scoped.ReadFile().Contains(FString::Printf(TEXT("#%s"), *Added.Stops[1].Color.ToHex().Left(6))));

	// The new stop's square stands beside the line: dragged well away from it, the stop comes out.
	const UDreamWidget* PreviewNow = Driver.PreviewFor(Template);
	FDreamDesignerHandleSet Set;
	Set.PixelsPerUnit = PixelsPerUnitFor(Driver, PreviewNow).X;
	const TSharedPtr<IDreamDesignerHandleProvider> Provider = FDreamDesignerHandles::FindProviderFor(PreviewNow, DreamDesignerGradientHandles::GetProviderName(EDreamRectBlockGradientSlot::Body));
	if (!TestTrue(TEXT("the gradient handles are registered"), Provider.IsValid()))
	{
		return false;
	}
	Provider->CollectHandles(const_cast<UDreamWidget*>(PreviewNow), Set);
	const FDreamDesignerHandle* Square = Set.Handles.FindByPredicate([](const FDreamDesignerHandle& InHandle) { return InHandle.Id % 1000 == 101; });
	TArray<FVector2D> Pixels;
	if (!TestNotNull(TEXT("the middle stop has a square"), Square)
		|| !Driver.WorldToPixels(TArray<FVector>({ PreviewNow->GetWorldTransform().TransformPosition(FVector(0.0, Square->Local.X, Square->Local.Y)) }), Pixels))
	{
		return false;
	}
	PressAndCarry(Driver, RoundPixel(Pixels[0]), RoundPixel(Pixels[0] + FVector2D(0.0, 120.0)), 6);
	LetGo(Driver);
	Driver.PumpFrame();
	TestEqual(TEXT("Dragged off the line, the stop is taken out"), BodyGradientOf(Template).Stops.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerGradientHandleTwoColourTest,
	"DreamGUI.Designer.Driver.DraggingATwoColourGradientWritesItsLineInPlaceOfTheTwoColourLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerGradientHandleTwoColourTest::RunTest(const FString&)
{
	using namespace DreamDesignerGradientHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDuiDesigner Scoped(TEXT("DesignerGradientTwoColour"), MakeFile({
		TEXT("bEnableBodyGradient = true"),
		TEXT("BodyGradientColor = #FF8000FF"),
		TEXT("BodyGradientRadius = (0.4, 0.4)") }));
	UDreamWidget* Template = nullptr;
	UDreamWidget* Preview = nullptr;
	if (!Prepare(*this, Scoped, Template, Preview))
	{
		return false;
	}
	FDreamDesignerDriver& Driver = *Scoped.Driver;
	const FString TextBefore = Scoped.ReadFile();
	TestTrue(TEXT("The rect block has no stops of its own"), BodyGradientOf(Template).Stops.IsEmpty());
	FVector2D End;
	if (!TestTrue(TEXT("its two-colour gradient's end is on screen"), FindGradientPoint(Driver, Preview, EDreamGradientPoint::End, End)))
	{
		return false;
	}
	PressAndCarry(Driver, RoundPixel(End), RoundPixel(End + FVector2D(30.0, 0.0)), 4);
	LetGo(Driver);
	Driver.PumpFrame();
	const FDreamGradient After = BodyGradientOf(Template);
	TestEqual(TEXT("The asset's gradient has the two stops now"), After.Stops.Num(), 2);
	TestTrue(TEXT("the first the two-colour gradient's colour"), After.Stops.Num() == 2 && After.Stops[0].Color == FColor(255, 128, 0, 255));
	const FString TextAfter = Scoped.ReadFile();
	TestTrue(TEXT("The file says the gradient"), TextAfter.Contains(TEXT("BodyGradient = \"radial-gradient(")));
	TestFalse(TEXT("and no longer its two-colour lines"), TextAfter.Contains(TEXT("BodyGradientColor")) || TextAfter.Contains(TEXT("BodyGradientRadius")));
	TestTrue(TEXT("its switch stays"), TextAfter.Contains(TEXT("bEnableBodyGradient = true")));
	AddInfo(FString::Printf(TEXT("The file reads:\n%s"), *TextAfter));

	TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
	Driver.PumpFrame();
	TestEqualSensitive(TEXT("One undo brings the two-colour lines back"), Scoped.ReadFile(), TextBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerGradientHandlePixelTest,
	"DreamGUI.Designer.RHI.ASelectedRectBlocksGradientHandlesAreDrawnOverIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * The designer's viewport drawn by a real RHI with a rect block of a three-stop linear gradient selected, then with nothing
 * selected: where the gradient's end handle stands, the picture with the selection differs from the one without -- the
 * handle is drawn there. Both pictures are kept in Saved/DreamGUITests/Captures (Designer_GradientHandles*.png).
 */
bool FDreamDesignerGradientHandlePixelTest::RunTest(const FString&)
{
	using namespace DreamDesignerGradientHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDuiDesigner Scoped(TEXT("DesignerGradientPixels"), MakeFile({
		TEXT("bEnableBodyGradient = true"),
		TEXT("BodyGradient = \"linear-gradient(120deg, #FF2D55, #FFD60A 45%, #0A84FF)\"") }));
	UDreamWidget* Template = nullptr;
	UDreamWidget* Preview = nullptr;
	if (!Prepare(*this, Scoped, Template, Preview))
	{
		return false;
	}
	FDreamDesignerDriver& Driver = *Scoped.Driver;
	FVector2D End;
	if (!TestTrue(TEXT("The gradient's end is on screen"), FindGradientPoint(Driver, Preview, EDreamGradientPoint::End, End)))
	{
		return false;
	}
	// Away from the block, so that no hover outline is drawn over it.
	Driver.MoveTo(FIntPoint(4, 4));
	auto Draw = [&Driver](TArray<FColor>& OutPixels, FIntPoint& OutSize)
	{
		for (int32 Frame = 0; Frame < 6; ++Frame)
		{
			Driver.PumpFrame();
			Driver.DrawFrame();
		}
		FlushRenderingCommands();
		const TSharedPtr<FSceneViewport> Viewport = Driver.SceneViewport();
		return Viewport.IsValid() && UDreamUICaptureLibrary::ReadViewportPixels(Viewport.Get(), OutPixels, OutSize) && OutPixels.Num() == OutSize.X * OutSize.Y;
	};
	TArray<FColor> Selected;
	FIntPoint Size = FIntPoint::ZeroValue;
	if (!TestTrue(TEXT("The viewport reads back with the block selected"), Draw(Selected, Size)))
	{
		return false;
	}
	FDreamPixelProbe::SaveCapture(Selected, Size, TEXT("Designer_GradientHandles"));
	SelectNothingInDesigner(Driver);
	TArray<FColor> Unselected;
	FIntPoint UnselectedSize = FIntPoint::ZeroValue;
	if (!TestTrue(TEXT("and with nothing selected"), Draw(Unselected, UnselectedSize)) || !TestEqual(TEXT("of one size"), UnselectedSize, Size))
	{
		return false;
	}
	FDreamPixelProbe::SaveCapture(Unselected, Size, TEXT("Designer_GradientHandles_Unselected"));
	const FIntPoint At(FMath::Clamp(FMath::RoundToInt32(End.X), 0, Size.X - 1), FMath::Clamp(FMath::RoundToInt32(End.Y), 0, Size.Y - 1));
	const FColor& With = Selected[At.Y * Size.X + At.X];
	const FColor& Without = Unselected[At.Y * Size.X + At.X];
	TestTrue(*FString::Printf(TEXT("The end handle is drawn at %s: %s with the selection, %s without"), *At.ToString(), *With.ToString(), *Without.ToString()),
		FMath::Max3(FMath::Abs(With.R - Without.R), FMath::Abs(With.G - Without.G), FMath::Abs(With.B - Without.B)) > 40);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
