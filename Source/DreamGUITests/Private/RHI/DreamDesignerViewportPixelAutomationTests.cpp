// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"
#include "RHI/DreamPixelProbe.h"

#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "DreamUICaptureLibrary.h"

#include "Slate/SceneViewport.h"

/*
 * The designer's viewport, drawn by a real RHI and read back: what an author sees of a widget is where the designer
 * placed it.
 *
 * The designer scenarios that render (DreamDesignerRhiScenarioAutomationTests.cpp) only ask that nothing breaks while
 * the viewport draws. These read the viewport's own target after FDreamDesignerDriver::DrawFrame and hold the picture to
 * the designer's geometry: the pixels a dropped block changed are the pixels of the rect the designer projects it to
 * (WidgetPixelRect, the projection its own overlay draws with), and once the block is dragged elsewhere the pixels
 * follow it and the place it left is the empty canvas again. UMG's designer draws its preview where its hit testing and
 * its handles say the widget is; this is the same promise for the self-rendered preview.
 *
 * The assertions are about where the picture changed, not about a colour the canvas or the block happens to have: the
 * picture before the drop is the reference. Nothing is selected when a picture is taken, so no selection outline or
 * handle is drawn over the block, and the pointer is kept off it, so no hover outline is either.
 */
namespace DreamDesignerViewportPixelTestLocal
{
	using namespace DreamTests;

	/** How far a channel has to move for a pixel to count as changed: well past antialiasing and dithering. */
	constexpr int32 ChangedBy = 40;
	/** How long the first draw may take: the preview's materials compile on first use. */
	constexpr double DrawnSeconds = 60.0;

	struct FPicture
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;

		bool IsSet() const { return Size.X > 0 && Size.Y > 0 && Pixels.Num() == Size.X * Size.Y; }
		FColor At(FIntPoint InPixel) const
		{
			return Pixels[FMath::Clamp(InPixel.Y, 0, Size.Y - 1) * Size.X + FMath::Clamp(InPixel.X, 0, Size.X - 1)];
		}
	};

	bool Read(FDreamDesignerDriver& InDriver, FPicture& OutPicture)
	{
		const TSharedPtr<FSceneViewport> Viewport = InDriver.SceneViewport();
		return Viewport.IsValid() && UDreamUICaptureLibrary::ReadViewportPixels(Viewport.Get(), OutPicture.Pixels, OutPicture.Size)
			&& OutPicture.IsSet();
	}

	bool Differs(const FColor& InLeft, const FColor& InRight)
	{
		return FMath::Abs(InLeft.R - InRight.R) > ChangedBy || FMath::Abs(InLeft.G - InRight.G) > ChangedBy || FMath::Abs(InLeft.B - InRight.B) > ChangedBy;
	}

	/** The box of every pixel of InRegion that differs between two pictures of the same size, or an invalid box. */
	FBox2D ChangedBox(const FPicture& InBefore, const FPicture& InAfter, const FIntRect& InRegion)
	{
		FBox2D Box(ForceInit);
		if (InBefore.Size != InAfter.Size)
		{
			return Box;
		}
		const FIntRect Clipped(
			FMath::Max(InRegion.Min.X, 0), FMath::Max(InRegion.Min.Y, 0),
			FMath::Min(InRegion.Max.X, InAfter.Size.X), FMath::Min(InRegion.Max.Y, InAfter.Size.Y));
		for (int32 Y = Clipped.Min.Y; Y < Clipped.Max.Y; ++Y)
		{
			for (int32 X = Clipped.Min.X; X < Clipped.Max.X; ++X)
			{
				if (Differs(InBefore.At(FIntPoint(X, Y)), InAfter.At(FIntPoint(X, Y))))
				{
					Box += FVector2D(X, Y);
					Box += FVector2D(X + 1, Y + 1);
				}
			}
		}
		return Box;
	}

	/** InRect grown by InMargin pixels each way, as an integer region. */
	FIntRect Around(const FBox2D& InRect, int32 InMargin)
	{
		return FIntRect(
			FMath::FloorToInt32(InRect.Min.X) - InMargin, FMath::FloorToInt32(InRect.Min.Y) - InMargin,
			FMath::CeilToInt32(InRect.Max.X) + InMargin, FMath::CeilToInt32(InRect.Max.Y) + InMargin);
	}

	/** Where the pointer rests when a picture is taken: a corner of the viewport, off every widget. */
	FIntPoint RestingPixel(const FDreamDesignerDriver& InDriver)
	{
		const FIntPoint Size = InDriver.ViewportPixelSize();
		return FIntPoint(Size.X - 6, Size.Y - 6);
	}

	/** Whether the changed box is InRect, to within InSlack pixels on every side. */
	bool Matches(const FBox2D& InChanged, const FBox2D& InRect, double InSlack)
	{
		return InChanged.bIsValid && InChanged.Min.Equals(InRect.Min, InSlack) && InChanged.Max.Equals(InRect.Max, InSlack);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPixelDroppedBlockTest,
	"DreamGUI.Designer.RHI.ADroppedBlockIsDrawnOnThePixelsTheDesignerPlacesItOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * The canvas drawn and read; a rect block dropped from the palette's Basic group and the selection cleared; drawn and read
 * again. The pixels that changed are the block's rect as the designer projects it, to within a few pixels of
 * antialiasing, and nothing changed outside it.
 */
bool FDreamDesignerPixelDroppedBlockTest::RunTest(const FString&)
{
	using namespace DreamDesignerViewportPixelTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerPixelDroppedBlock"));
	TSharedRef<FPicture> Before = MakeShared<FPicture>();
	TSharedRef<FPicture> After = MakeShared<FPicture>();
	EnqueueDesignerFrames(State, 3, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, Before]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		Driver.MoveTo(RestingPixel(Driver));
		Driver.DrawFrame();
		if (!TestTrue(TEXT("The designer viewport reads back"), Read(Driver, *Before)))
		{
			State->bAlive = false;
			return;
		}
		const FIntPoint Size = Driver.ViewportPixelSize();
		UDreamWidget* Root = DesignerTemplateRoot(State->Asset.Blueprint);
		const TArray<UDreamWidget*> Existing = LiveChildrenOf(Root);
		if (!TestTrue(TEXT("A rect block's drop is taken"), Driver.DropBasicFromPalette(UDreamRectBlock::StaticClass(), FIntPoint(Size.X * 2 / 5, Size.Y * 2 / 5))))
		{
			State->bAlive = false;
			return;
		}
		for (UDreamWidget* Child : LiveChildrenOf(Root))
		{
			if (!Existing.Contains(Child))
			{
				State->Made.Add(TEXT("Block"), Child);
			}
		}
		if (!TestNotNull(TEXT("The block arrived under the root"), State->Get(TEXT("Block"))))
		{
			State->bAlive = false;
			return;
		}
		SelectNothingInDesigner(Driver);
	});
	EnqueueDesignerUntil(State, this, [State, Before]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		const TOptional<FIntPoint> Centre = Driver.WidgetPixel(Driver.PreviewFor(State->Get(TEXT("Block"))));
		FPicture Now;
		return Centre.IsSet() && Read(Driver, Now) && Differs(Before->At(Centre.GetValue()), Now.At(Centre.GetValue()));
	}, DrawnSeconds, TEXT("the dropped block drawn in the designer viewport"), /*bInDraw*/ true);
	EnqueueDesignerFrames(State, 2, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, Before, After]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		Driver.DrawFrame();
		const TOptional<FBox2D> Rect = Driver.WidgetPixelRect(Driver.PreviewFor(State->Get(TEXT("Block"))));
		if (!TestTrue(TEXT("The block is on screen"), Rect.IsSet()) || !TestTrue(TEXT("The viewport reads back again"), Read(Driver, *After)))
		{
			return;
		}
		const FIntRect Region = Around(Rect.GetValue(), 40);
		const FBox2D Changed = ChangedBox(*Before, *After, Region);
		FDreamPixelProbe::SaveCapture(After->Pixels, After->Size, TEXT("Designer_DroppedBlock"));
		TestTrue(FString::Printf(TEXT("The pixels the drop changed are the block's rect: changed %s, the rect %s"),
			Changed.bIsValid ? *Changed.ToString() : TEXT("(none)"), *Rect->ToString()), Matches(Changed, Rect.GetValue(), 3.0));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPixelDraggedBlockTest,
	"DreamGUI.Designer.RHI.ABlockDraggedAcrossTheViewportIsDrawnWhereItWasLeftAndTheCanvasShowsWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * A rect block dropped and drawn, then grabbed and dragged two hundred pixels right and eighty down with the pointer, the
 * selection cleared and the pointer put aside. Against the picture from before the drop, the pixels that differ are the
 * rect where the block is now -- the place it was dragged from shows the empty canvas it showed before the drop.
 */
bool FDreamDesignerPixelDraggedBlockTest::RunTest(const FString&)
{
	using namespace DreamDesignerViewportPixelTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerPixelDraggedBlock"));
	State->Preferences = MakeShared<FScopedDesignerPreferences>();
	State->Preferences->SetGridSnap(false);
	State->Preferences->SetGuides(false);
	TSharedRef<FPicture> Empty = MakeShared<FPicture>();
	TSharedRef<FIntPoint> Grab = MakeShared<FIntPoint>(FIntPoint::ZeroValue);
	TSharedRef<FBox2D> FirstRect = MakeShared<FBox2D>(ForceInit);
	const FIntPoint Travel(200, 80);
	EnqueueDesignerFrames(State, 3, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, Empty]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		Driver.MoveTo(RestingPixel(Driver));
		Driver.DrawFrame();
		if (!TestTrue(TEXT("The designer viewport reads back"), Read(Driver, *Empty)))
		{
			State->bAlive = false;
			return;
		}
		const FIntPoint Size = Driver.ViewportPixelSize();
		UDreamWidget* Root = DesignerTemplateRoot(State->Asset.Blueprint);
		const TArray<UDreamWidget*> Existing = LiveChildrenOf(Root);
		Driver.DropBasicFromPalette(UDreamRectBlock::StaticClass(), FIntPoint(Size.X / 3, Size.Y / 3));
		for (UDreamWidget* Child : LiveChildrenOf(Root))
		{
			if (!Existing.Contains(Child))
			{
				State->Made.Add(TEXT("Block"), Child);
			}
		}
		if (!TestNotNull(TEXT("A rect block was dropped under the root"), State->Get(TEXT("Block"))))
		{
			State->bAlive = false;
		}
	});
	EnqueueDesignerFrames(State, 2, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, Grab, FirstRect]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		UDreamWidget* Block = State->Get(TEXT("Block"));
		SelectOnlyInDesigner(Driver, Block);
		const TOptional<FBox2D> Rect = Driver.WidgetPixelRect(Driver.PreviewFor(Block));
		if (!TestTrue(TEXT("The block is on screen"), Rect.IsSet()))
		{
			State->bAlive = false;
			return;
		}
		*FirstRect = Rect.GetValue();
		*Grab = PointInBox(Rect.GetValue(), 0.25, 0.25);
	});
	EnqueueDesignerFrames(State, 1, /*bInDraw*/ true);
	EnqueueDesignerPointerDrag(State, [Grab]() { return *Grab; }, [Grab, Travel]() { return *Grab + Travel; }, /*Steps*/ 5);
	EnqueueDesignerAction(State, [State]()
	{
		SelectNothingInDesigner(*State->Driver);
		State->Driver->MoveTo(RestingPixel(*State->Driver));
	});
	EnqueueDesignerUntil(State, this, [State, Empty]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		const TOptional<FIntPoint> Centre = Driver.WidgetPixel(Driver.PreviewFor(State->Get(TEXT("Block"))));
		FPicture Now;
		return Centre.IsSet() && Read(Driver, Now) && Differs(Empty->At(Centre.GetValue()), Now.At(Centre.GetValue()));
	}, DrawnSeconds, TEXT("the dragged block drawn where it was left"), /*bInDraw*/ true);
	EnqueueDesignerFrames(State, 2, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, Empty, FirstRect, Travel]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		Driver.DrawFrame();
		FPicture Now;
		const TOptional<FBox2D> Rect = Driver.WidgetPixelRect(Driver.PreviewFor(State->Get(TEXT("Block"))));
		if (!TestTrue(TEXT("The block is on screen after the drag"), Rect.IsSet()) || !TestTrue(TEXT("The viewport reads back"), Read(Driver, Now)))
		{
			return;
		}
		FDreamPixelProbe::SaveCapture(Now.Pixels, Now.Size, TEXT("Designer_DraggedBlock"));
		TestTrue(FString::Printf(TEXT("The block moved by the drag: %s, it was at %s"), *Rect->ToString(), *FirstRect->ToString()),
			Rect->GetCenter().Equals(FirstRect->GetCenter() + FVector2D(Travel), 2.0));
		// One region covering both places, so a block drawn twice -- or not moved in the picture -- shows up as a box too big.
		FBox2D Both = *FirstRect;
		Both += Rect.GetValue();
		const FBox2D Changed = ChangedBox(*Empty, Now, Around(Both, 40));
		TestTrue(FString::Printf(TEXT("The only pixels that differ from the empty canvas are the block's new rect: changed %s, the rect %s"),
			Changed.bIsValid ? *Changed.ToString() : TEXT("(none)"), *Rect->ToString()), Matches(Changed, Rect.GetValue(), 3.0));
		const FIntPoint OldCentre(FMath::RoundToInt32(FirstRect->GetCenter().X), FMath::RoundToInt32(FirstRect->GetCenter().Y));
		TestFalse(FString::Printf(TEXT("...and where it was dragged from shows the empty canvas again: %s, the canvas was %s"),
			*FDreamPixelProbe::Describe(Now.At(OldCentre)), *FDreamPixelProbe::Describe(Empty->At(OldCentre))), Differs(Empty->At(OldCentre), Now.At(OldCentre)));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

#endif
