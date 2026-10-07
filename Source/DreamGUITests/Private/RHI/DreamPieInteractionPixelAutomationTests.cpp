// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDropdown.h"
#include "Controls/DreamScrollBox.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Event/DreamEventSystem.h"
#include "DreamUICaptureLibrary.h"
#include "Engine/World.h"
#include "Interaction/UIButton.h"
#include "Interaction/UINavigationInputSelectionHandler.h"
#include "UnrealClient.h"
#include "Utils/DreamUIUtils.h"

#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "RHI/DreamPixelProbe.h"

/*
 * What a player sees of an interaction: a play session's viewport read back, the screen-space UI over the scene, while
 * the pointer and the pad act on it through the player's controller.
 *
 * Each test asserts geometry and colour on the picture, rather than holding it to a golden image: a scroll box draws
 * none of a half-shown row past its edge and all of it inside; a button's face is its hovered colour on screen once the
 * pointer is over it; an open dropdown's list covers what is under it; the focus ring hugs the button the D-pad moved the
 * focus to. Which colours those are is read off the controls themselves -- the style's hovered colour, the picture from
 * before the ring arrived -- so a change of theme moves the expectation with it. Every picture is written to
 * Saved/DreamGUITests/Captures for a person to look at.
 *
 * NonNullRHI, as every picture is: under -nullrhi a viewport has no pixels. In the PIE layer, because only a play
 * session's viewport is the picture a player gets.
 */
namespace DreamPieInteractionPixelTestLocal
{
	/** A colour nothing in the default theme or the blank map draws, so a pixel of it is the test's own block. */
	const FColor BlockGreen(0, 220, 0, 255);
	/** Per channel. A pixel's own colour comes back within this; antialiasing and blending past it. */
	constexpr uint8 SameColour = 10;
	/** How long a picture may take to show what a test waits for: a material's first draw compiles its shaders. */
	FWaitTimeout DrawnLimit() { return FWaitTimeout::InSeconds(60.0); }
	FWaitTimeout StateLimit() { return FWaitTimeout::InSeconds(3.0); }

	struct FPicture
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;

		bool IsSet() const { return Size.X > 0 && Size.Y > 0 && Pixels.Num() == Size.X * Size.Y; }
		FColor At(FIntPoint InPixel) const
		{
			return Pixels[FMath::Clamp(InPixel.Y, 0, Size.Y - 1) * Size.X + FMath::Clamp(InPixel.X, 0, Size.X - 1)];
		}
		FColor At(const FVector2D& InPixel) const { return At(FIntPoint(FMath::FloorToInt32(InPixel.X), FMath::FloorToInt32(InPixel.Y))); }
	};

	bool ReadPlay(UWorld* InWorld, FPicture& OutPicture)
	{
		FViewport* Viewport = UDreamUICaptureLibrary::FindViewportOf(InWorld);
		return Viewport != nullptr && UDreamUICaptureLibrary::ReadViewportPixels(Viewport, OutPicture.Pixels, OutPicture.Size) && OutPicture.IsSet();
	}

	/** Red, green and blue each within InTolerance; alpha is the viewport's, which ReadViewportPixels makes opaque. */
	bool Near(const FColor& InLeft, const FColor& InRight, uint8 InTolerance)
	{
		return FMath::Abs(InLeft.R - InRight.R) <= InTolerance && FMath::Abs(InLeft.G - InRight.G) <= InTolerance
			&& FMath::Abs(InLeft.B - InRight.B) <= InTolerance;
	}

	/** A block of InColour drawn by a texture visual, as the picture probes draw theirs. */
	UDreamWidget* MakeBlock(FDreamDriverPieRig& InRig, const TCHAR* InName, UDreamWidget* InParent, const FVector2D& InSize,
		const FVector2D& InPosition, const FColor& InColour)
	{
		UDreamWidget* Block = InRig.MakeWidgetWithVisual(UDreamTexture::StaticClass(), InName, InParent, InSize, InPosition);
		if (UDreamTexture* Visual = Block != nullptr ? Cast<UDreamTexture>(Block->GetVisual()) : nullptr)
		{
			Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
			Visual->SetColor(InColour);
		}
		return Block;
	}

	/**
	 * A solid block that asks for no size of its own, so a panel lays it out at the size it was made with: a texture's
	 * visual asks for its texture's size (UDreamTextureBase::GetPreferredHeight), as UMG's Image asks for its brush's,
	 * and in a stack box the four-pixel white texture made every block four pixels tall.
	 */
	UDreamWidget* MakeSolidBlock(FDreamDriverPieRig& InRig, const TCHAR* InName, UDreamWidget* InParent, const FVector2D& InSize,
		const FColor& InColour)
	{
		UDreamWidget* Block = InRig.MakeWidgetWithVisual(UDreamRectBlock::StaticClass(), InName, InParent, InSize, FVector2D::ZeroVector);
		if (UDreamVisual* Visual = Block != nullptr ? Block->GetVisual() : nullptr)
		{
			Visual->SetColor(InColour);
		}
		return Block;
	}

	/** Wait, reading the picture each frame, until InHolds says yes of it. */
	void WaitForPicture(FDreamDriverSequence& InSteps, const TSharedRef<FDreamDriverPieRig>& InRig, TFunction<bool(const FPicture&, UWorld*)> InHolds,
		const FString& InWhat)
	{
		InSteps.Wait(FDreamUntil::Lambda([InRig, InHolds](const FTimespan& InWaited)
		{
			// The play world asked of the rig each frame: nothing here holds it between frames.
			UWorld* World = InRig->GetWorld();
			FPicture Now;
			if (World != nullptr && ReadPlay(World, Now) && InHolds(Now, World))
			{
				return FDriverWaitResponse::Passed();
			}
			return InWaited.GetTotalSeconds() > DrawnLimit().Timespan.GetTotalSeconds() ? FDriverWaitResponse::Failed() : FDriverWaitResponse::Wait();
		}), DrawnLimit(), InWhat);
	}

	TOptional<FBox2D> RectOf(const UDreamWidget* InWidget)
	{
		return InWidget != nullptr ? FDreamDriverProjection::WidgetToPixelRect(InWidget) : TOptional<FBox2D>();
	}

	TOptional<FVector2D> CentreOf(const UDreamWidget* InWidget)
	{
		return InWidget != nullptr ? FDreamDriverProjection::WidgetCentrePixel(InWidget) : TOptional<FVector2D>();
	}

	/** The pixel InOutward pixels outside InRect's side InSide (0 left, 1 right, 2 top, 3 bottom), on that side's middle. */
	FIntPoint OutsideSide(const FBox2D& InRect, int32 InSide, double InOutward)
	{
		const FVector2D Centre = InRect.GetCenter();
		switch (InSide)
		{
		case 0: return FIntPoint(FMath::FloorToInt32(InRect.Min.X - InOutward), FMath::FloorToInt32(Centre.Y));
		case 1: return FIntPoint(FMath::FloorToInt32(InRect.Max.X + InOutward), FMath::FloorToInt32(Centre.Y));
		case 2: return FIntPoint(FMath::FloorToInt32(Centre.X), FMath::FloorToInt32(InRect.Min.Y - InOutward));
		default: return FIntPoint(FMath::FloorToInt32(Centre.X), FMath::FloorToInt32(InRect.Max.Y + InOutward));
		}
	}

	const TCHAR* SideName(int32 InSide)
	{
		static const TCHAR* const Names[] = { TEXT("left"), TEXT("right"), TEXT("top"), TEXT("bottom") };
		return Names[FMath::Clamp(InSide, 0, 3)];
	}

	/** Whether any pixel from InFrom to InTo pixels outside InRect's side differs between two pictures by more than InBy. */
	bool ChangedJustOutside(const FPicture& InBefore, const FPicture& InAfter, const FBox2D& InRect, int32 InSide, int32 InFrom, int32 InTo, uint8 InBy)
	{
		for (int32 Out = InFrom; Out <= InTo; ++Out)
		{
			const FIntPoint Pixel = OutsideSide(InRect, InSide, Out);
			if (!Near(InBefore.At(Pixel), InAfter.At(Pixel), InBy))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Whether any pixel of InPicture from InFrom to InTo pixels inside InRect's side InSide is something other than the
	 * colour at InRect's middle, by more than InBy: a line drawn along that edge over a face of one colour.
	 */
	bool DrawnJustInside(const FPicture& InPicture, const FBox2D& InRect, int32 InSide, int32 InFrom, int32 InTo, uint8 InBy)
	{
		const FColor Face = InPicture.At(InRect.GetCenter());
		for (int32 In = InFrom; In <= InTo; ++In)
		{
			if (!Near(InPicture.At(OutsideSide(InRect, InSide, -In)), Face, InBy))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieScrollClipPixelTest,
	"DreamGUI.Pie.RHI.AScrollBoxDrawsNothingOfAHalfShownRowPastItsEdgeAndAllOfItInside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

/*
 * A scroll box two hundred units tall holding green rows sixty tall: one of them stands across the box's lower edge.
 * Inside the edge that row's pixels are green; just past it they are not -- the box clips its content to its viewport,
 * as SScrollBox clips its panel (EWidgetClipping::ClipToBounds on the scroll panel). A row wholly past the edge shows
 * nowhere.
 */
bool FDreamPieScrollClipPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieInteractionPixelTestLocal;
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([](FDreamDriverPieRig& InRig)
	{
		UDreamScrollBox* Box = InRig.MakeControl<UDreamScrollBox>(TEXT("ClipBox"), nullptr, FVector2D(320.0, 200.0));
		if (Box == nullptr || Box->GetContentNode() == nullptr)
		{
			return;
		}
		for (int32 Index = 0; Index < 6; ++Index)
		{
			MakeSolidBlock(InRig, *FString::Printf(TEXT("ClipRow%d"), Index), Box->GetContentNode(), FVector2D(300.0, 60.0), BlockGreen);
		}
		Box->RefreshContentExtent();
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.MoveToPixel(FVector2D(8.0, 8.0));
	WaitForPicture(Steps, Rig, [Rig](const FPicture& InPicture, UWorld*)
	{
		const TOptional<FVector2D> First = CentreOf(Rig->FindMade(TEXT("ClipRow0")));
		return First.IsSet() && Near(InPicture.At(First.GetValue()), BlockGreen, SameColour);
	}, TEXT("the scroll box's first row drawn"));
	Steps.WaitFrames(2);
	Steps.Then([this, Rig](FDreamDriverContext& InContext)
	{
		FPicture Picture;
		const UDreamScrollBox* Box = Cast<UDreamScrollBox>(Rig->FindMade(TEXT("ClipBox")));
		const TOptional<FBox2D> Clip = Box != nullptr ? RectOf(Box->ViewportNode) : TOptional<FBox2D>();
		if (!TestTrue(TEXT("The play session's viewport reads back"), ReadPlay(InContext.World, Picture))
			|| !TestTrue(TEXT("The scroll box's viewport is on screen"), Clip.IsSet()))
		{
			return;
		}
		FDreamPixelProbe::SaveCapture(Picture.Pixels, Picture.Size, TEXT("Pie_ScrollBoxClip"));
		int32 Straddling = INDEX_NONE;
		int32 Hidden = INDEX_NONE;
		TOptional<FBox2D> StraddlingRect;
		TOptional<FBox2D> HiddenRect;
		TArray<FString> Rows;
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const TOptional<FBox2D> Row = RectOf(Rig->FindMade(FString::Printf(TEXT("ClipRow%d"), Index)));
			Rows.Add(Row.IsSet() ? FString::Printf(TEXT("%.0f-%.0f"), Row->Min.Y, Row->Max.Y) : FString(TEXT("not on screen")));
			if (!Row.IsSet())
			{
				continue;
			}
			if (Straddling == INDEX_NONE && Row->Min.Y < Clip->Max.Y - 8.0 && Row->Max.Y > Clip->Max.Y + 8.0)
			{
				Straddling = Index;
				StraddlingRect = Row;
			}
			if (Hidden == INDEX_NONE && Row->Min.Y > Clip->Max.Y + 2.0)
			{
				Hidden = Index;
				HiddenRect = Row;
			}
		}
		if (!TestTrue(FString::Printf(TEXT("A row stands across the box's lower edge at %.1f (the box's viewport %.0f-%.0f, the rows %s)"),
			Clip->Max.Y, Clip->Min.Y, Clip->Max.Y, *FString::Join(Rows, TEXT(", "))), StraddlingRect.IsSet()))
		{
			return;
		}
		const double X = StraddlingRect->GetCenter().X;
		const FColor Inside = Picture.At(FVector2D(X, Clip->Max.Y - 4.0));
		const FColor Outside = Picture.At(FVector2D(X, Clip->Max.Y + 4.0));
		TestTrue(FString::Printf(TEXT("Inside the edge the half-shown row is drawn: %s"), *FDreamPixelProbe::Describe(Inside)), Near(Inside, BlockGreen, SameColour));
		TestFalse(FString::Printf(TEXT("...and just past the edge none of it is: %s"), *FDreamPixelProbe::Describe(Outside)), Near(Outside, BlockGreen, 60));
		if (HiddenRect.IsSet())
		{
			const FColor Below = Picture.At(HiddenRect->GetCenter());
			TestFalse(FString::Printf(TEXT("A row wholly past the edge shows nowhere: %s at its middle"), *FDreamPixelProbe::Describe(Below)), Near(Below, BlockGreen, 60));
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieHoverTintPixelTest,
	"DreamGUI.Pie.RHI.ADefaultButtonsFaceIsItsHoveredColourOnScreenOnceThePointerIsOverIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

/*
 * A default button, the pointer off it: the middle of its face on screen is the style's normal colour. The pointer over
 * it: once the face has faded, the middle of it on screen is the style's hovered colour -- UMG's button draws its Hovered
 * brush tint where its Normal one was. Both to within ten levels a channel, which is what the scene's composition leaves
 * of an opaque UI colour.
 */
bool FDreamPieHoverTintPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieInteractionPixelTestLocal;
	struct FColours
	{
		FColor Normal = FColor::Black;
		FColor Hovered = FColor::Black;
	};
	const TSharedRef<FColours> Colours = MakeShared<FColours>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Colours](FDreamDriverPieRig& InRig)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(TEXT("HoverFace"), nullptr, FVector2D(240.0, 80.0));
		if (Button != nullptr && Button->ButtonBehaviour != nullptr)
		{
			Colours->Normal = Button->ButtonBehaviour->GetNormalColor();
			Colours->Hovered = Button->ButtonBehaviour->GetHoveredColor();
		}
	});
	const auto FaceCentre = [Rig]() -> TOptional<FVector2D>
	{
		const UDreamButton* Button = Cast<UDreamButton>(Rig->FindMade(TEXT("HoverFace")));
		return Button != nullptr ? CentreOf(Button->FaceNode) : TOptional<FVector2D>();
	};
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.MoveToPixel(FVector2D(8.0, 8.0));
	Steps.Then([this, Colours](FDreamDriverContext&)
	{
		TestTrue(TEXT("The button's style has two different colours for rest and hover"), Colours->Normal != Colours->Hovered);
	});
	WaitForPicture(Steps, Rig, [FaceCentre, Colours](const FPicture& InPicture, UWorld*)
	{
		const TOptional<FVector2D> Centre = FaceCentre();
		return Centre.IsSet() && Near(InPicture.At(Centre.GetValue()), Colours->Normal, SameColour);
	}, TEXT("the button's face drawn in its normal colour"));
	Steps.MoveTo(Rig->Made(TEXT("HoverFace")));
	WaitForPicture(Steps, Rig, [FaceCentre, Colours](const FPicture& InPicture, UWorld*)
	{
		const TOptional<FVector2D> Centre = FaceCentre();
		return Centre.IsSet() && Near(InPicture.At(Centre.GetValue()), Colours->Hovered, SameColour);
	}, TEXT("the button's face drawn in its hovered colour under the pointer"));
	Steps.Then([this, Colours, FaceCentre](FDreamDriverContext& InContext)
	{
		FPicture Picture;
		const TOptional<FVector2D> Centre = FaceCentre();
		if (!TestTrue(TEXT("The play session's viewport reads back"), ReadPlay(InContext.World, Picture)) || !TestTrue(TEXT("The face is on screen"), Centre.IsSet()))
		{
			return;
		}
		FDreamPixelProbe::SaveCapture(Picture.Pixels, Picture.Size, TEXT("Pie_HoveredButton"));
		const FColor Seen = Picture.At(Centre.GetValue());
		TestTrue(FString::Printf(TEXT("Under the pointer the face is the style's hovered colour %s on screen: %s"),
			*FDreamPixelProbe::Describe(Colours->Hovered), *FDreamPixelProbe::Describe(Seen)), Near(Seen, Colours->Hovered, SameColour));
		TestFalse(TEXT("...and no longer its normal colour"), Near(Seen, Colours->Normal, 4));
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPiePopupOverPixelTest,
	"DreamGUI.Pie.RHI.AnOpenDropdownListIsDrawnOverTheWidgetBelowItWhereTheyOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

/*
 * A dropdown, and a green block made after it -- so drawn after it in the ordinary order -- where the dropdown's list
 * will open. Opened by a click, the list covers the block: where they overlap the picture is the list's own colour, the
 * colour of the same row of the list beside the block, and none of the block's green. UMG's combo box list is a menu on
 * the Slate menu stack, a layer above the widgets it opened from.
 */
bool FDreamPiePopupOverPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieInteractionPixelTestLocal;
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([](FDreamDriverPieRig& InRig)
	{
		UDreamDropdown* Dropdown = InRig.MakeControl<UDreamDropdown>(TEXT("Quality"), nullptr, FVector2D(300.0, 40.0), FVector2D(0.0, 200.0));
		if (Dropdown != nullptr)
		{
			Dropdown->SetOptions({ FText::AsCultureInvariant(TEXT("Low")), FText::AsCultureInvariant(TEXT("Medium")), FText::AsCultureInvariant(TEXT("High")) });
			Dropdown->SetSelectedIndex(0);
		}
		// Under the right half of where the list opens, clear of the options' text, which starts at the left.
		MakeBlock(InRig, TEXT("Beneath"), nullptr, FVector2D(80.0, 40.0), FVector2D(40.0, 140.0), BlockGreen);
	});
	const auto BlockCentre = [Rig]() { return CentreOf(Rig->FindMade(TEXT("Beneath"))); };
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.MoveToPixel(FVector2D(8.0, 8.0));
	WaitForPicture(Steps, Rig, [BlockCentre](const FPicture& InPicture, UWorld*)
	{
		const TOptional<FVector2D> Centre = BlockCentre();
		return Centre.IsSet() && Near(InPicture.At(Centre.GetValue()), BlockGreen, SameColour);
	}, TEXT("the green block drawn before the list opens"));
	Steps.Click(Rig->Made(TEXT("Quality")));
	Steps.Wait(FDreamUntil::Condition([Rig]()
	{
		const UDreamDropdown* Dropdown = Cast<UDreamDropdown>(Rig->FindMade(TEXT("Quality")));
		return Dropdown != nullptr && Dropdown->IsOpen() && Dropdown->ListNode != nullptr && Dropdown->ListNode->GetRenderOpacity() > 0.999f;
	}, StateLimit()), StateLimit(), TEXT("the dropdown's list open and faded in"));
	WaitForPicture(Steps, Rig, [BlockCentre](const FPicture& InPicture, UWorld*)
	{
		const TOptional<FVector2D> Centre = BlockCentre();
		return Centre.IsSet() && !Near(InPicture.At(Centre.GetValue()), BlockGreen, 60);
	}, TEXT("the open list drawn over the green block"));
	Steps.Then([this, Rig, BlockCentre](FDreamDriverContext& InContext)
	{
		FPicture Picture;
		const UDreamDropdown* Dropdown = Cast<UDreamDropdown>(Rig->FindMade(TEXT("Quality")));
		const TOptional<FBox2D> List = Dropdown != nullptr ? RectOf(Dropdown->ListNode) : TOptional<FBox2D>();
		const TOptional<FBox2D> Block = RectOf(Rig->FindMade(TEXT("Beneath")));
		const TOptional<FVector2D> Centre = BlockCentre();
		if (!TestTrue(TEXT("The play session's viewport reads back"), ReadPlay(InContext.World, Picture))
			|| !TestTrue(TEXT("The list and the block are on screen"), List.IsSet() && Block.IsSet() && Centre.IsSet()))
		{
			return;
		}
		FDreamPixelProbe::SaveCapture(Picture.Pixels, Picture.Size, TEXT("Pie_DropdownOverBlock"));
		if (!TestTrue(FString::Printf(TEXT("The list %s opened over the block %s"), *List->ToString(), *Block->ToString()), List->IsInside(Centre.GetValue())))
		{
			return;
		}
		// The same row of the list, between the block's right edge and the list's.
		const FVector2D Beside((Block->Max.X + List->Max.X) * 0.5, Centre->Y);
		const FColor Over = Picture.At(Centre.GetValue());
		const FColor Reference = Picture.At(Beside);
		TestFalse(FString::Printf(TEXT("Where the list and the block overlap there is none of the block's green: %s"), *FDreamPixelProbe::Describe(Over)),
			Near(Over, BlockGreen, 60));
		TestTrue(FString::Printf(TEXT("...it is the list's own colour, as beside the block: %s over the block, %s beside it"),
			*FDreamPixelProbe::Describe(Over), *FDreamPixelProbe::Describe(Reference)), Near(Over, Reference, SameColour));
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieFocusRingPixelTest,
	"DreamGUI.Pie.RHI.TheFocusRingIsDrawnHuggingTheButtonTheDPadMovedTheFocusTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

/*
 * Two buttons side by side and nothing focused: the picture before. The screen opens with the focus on the first, and the
 * D-pad moves it to the second. The ring is drawn along the second button's own edge on every side, over its face, and
 * nothing outside the button changed -- the ring hugs the button, as Slate draws its keyboard focus outline on the
 * focused widget's own geometry (SWidget::Paint, the focus brush at the widget's AllottedGeometry). The first button
 * shows no ring, and its surroundings are as they were: the ring left it.
 */
bool FDreamPieFocusRingPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieInteractionPixelTestLocal;
	/** A ring along an edge is drawn this far inside it; up to Far pixels outside a button nothing may change. */
	constexpr int32 RingFrom = 2;
	constexpr int32 RingTo = 8;
	constexpr int32 Hug = 14;
	constexpr int32 Far = 40;
	const TSharedRef<FPicture> Before = MakeShared<FPicture>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	// Small enough, and near enough the middle, that both buttons and Far pixels round each of them are on the play
	// viewport even when it is the editor's own, a few hundred pixels wide.
	Rig->WhenReady([](FDreamDriverPieRig& InRig)
	{
		InRig.MakeControl<UDreamButton>(TEXT("RingFirst"), nullptr, FVector2D(120.0, 48.0), FVector2D(-90.0, 0.0));
		InRig.MakeControl<UDreamButton>(TEXT("RingSecond"), nullptr, FVector2D(120.0, 48.0), FVector2D(90.0, 0.0));
	});
	const auto FaceOf = [Rig](const TCHAR* InName) -> UDreamWidget*
	{
		const UDreamButton* Button = Cast<UDreamButton>(Rig->FindMade(InName));
		return Button != nullptr ? Button->FaceNode.Get() : nullptr;
	};
	/** Whether the screen's ring is shown and sits on InFace -- the same rect, within a pixel and a half. */
	const auto RingSitsOn = [](const UDreamWidget* InFace)
	{
		const UUINavigationInputSelectionHandler* Ring = InFace != nullptr ? UUINavigationInputSelectionHandler::FindFor(InFace) : nullptr;
		const UDreamWidget* RingWidget = Ring != nullptr ? Ring->GetWidget() : nullptr;
		const TOptional<FBox2D> RingRect = RectOf(RingWidget);
		const TOptional<FBox2D> FaceRect = RectOf(InFace);
		return RingRect.IsSet() && FaceRect.IsSet() && RingWidget->GetRenderOpacity() > 0.99f
			&& RingRect->GetCenter().Equals(FaceRect->GetCenter(), 1.5);
	};
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.MoveToPixel(FVector2D(8.0, 8.0));
	WaitForPicture(Steps, Rig, [FaceOf](const FPicture& InPicture, UWorld*)
	{
		const UDreamWidget* Face = FaceOf(TEXT("RingSecond"));
		const TOptional<FVector2D> Centre = CentreOf(Face);
		// Drawn: the face's middle is not what the corner, which nothing covers, shows.
		return Centre.IsSet() && !Near(InPicture.At(Centre.GetValue()), InPicture.At(FIntPoint(4, 4)), SameColour);
	}, TEXT("both buttons drawn"));
	Steps.WaitFrames(2);
	Steps.Then([this, Before](FDreamDriverContext& InContext)
	{
		TestTrue(TEXT("The picture with nothing focused reads back"), ReadPlay(InContext.World, *Before));
	});
	// The screen's initial focus, as a page gives it when it opens, to a player on the pad; from here on it is the pad's.
	// On the pad first: code that moves the focus draws it only for a player last on the keys or the pad, as CSS's
	// :focus-visible and CommonUI's focus visual do (UDreamUIInputUser::IsFocusVisible), and the last input here was the
	// mouse put out of the way.
	Steps.Then([this, FaceOf](FDreamDriverContext& InContext)
	{
		UDreamEventSystem* Events = UDreamEventSystem::GetDreamEventSystemInstance(InContext.World, 0);
		TestTrue(TEXT("The player's event system is there to hear the pad"), Events != nullptr);
		if (Events != nullptr)
		{
			Events->ReportInputDevice(EDreamUIInputDevice::Gamepad);
		}
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InContext.World);
		TestTrue(TEXT("The first button takes the screen's initial focus"), Services != nullptr && Services->FocusForNavigation(FaceOf(TEXT("RingFirst")), 0));
	});
	Steps.Wait(FDreamUntil::Condition([FaceOf, RingSitsOn]() { return RingSitsOn(FaceOf(TEXT("RingFirst"))); }, StateLimit()), StateLimit(),
		TEXT("the focus ring sitting on the first button"));
	Steps.Key(EKeys::Gamepad_DPad_Right);
	Steps.Wait(FDreamUntil::Condition([FaceOf, RingSitsOn]() { return RingSitsOn(FaceOf(TEXT("RingSecond"))); }, StateLimit()), StateLimit(),
		TEXT("the D-pad taking the focus ring to the second button"));
	// The ring's flight has landed in the tree; a few frames for the picture to catch up with it.
	Steps.WaitFrames(4);
	Steps.Then([this, Before, FaceOf](FDreamDriverContext& InContext)
	{
		FPicture After;
		const TOptional<FBox2D> Second = RectOf(FaceOf(TEXT("RingSecond")));
		const TOptional<FBox2D> First = RectOf(FaceOf(TEXT("RingFirst")));
		if (!TestTrue(TEXT("The picture with the second button focused reads back"), ReadPlay(InContext.World, After))
			|| !TestTrue(TEXT("Both buttons are on screen"), Second.IsSet() && First.IsSet() && Before->Size == After.Size))
		{
			return;
		}
		FDreamPixelProbe::SaveCapture(Before->Pixels, Before->Size, TEXT("Pie_FocusRing_Before"));
		FDreamPixelProbe::SaveCapture(After.Pixels, After.Size, TEXT("Pie_FocusRing_OnSecond"));
		const FBox2D Picture(FVector2D::ZeroVector, FVector2D(After.Size));
		TestTrue(FString::Printf(TEXT("Both buttons, and %d pixels round them, are inside the %dx%d picture"), Far, After.Size.X, After.Size.Y),
			Picture.IsInside(First->ExpandBy(Far)) && Picture.IsInside(Second->ExpandBy(Far)));
		for (int32 Side = 0; Side < 4; ++Side)
		{
			TestTrue(FString::Printf(TEXT("%d to %d pixels inside the second button's %s edge the ring is drawn over its face"), RingFrom, RingTo, SideName(Side)),
				DrawnJustInside(After, Second.GetValue(), Side, RingFrom, RingTo, 24));
			TestFalse(FString::Printf(TEXT("...where before the focus the face was one colour to its %s edge"), SideName(Side)),
				DrawnJustInside(*Before, Second.GetValue(), Side, RingFrom, RingTo, 24));
			TestFalse(FString::Printf(TEXT("...and from 2 to %d pixels outside its %s edge nothing changed: the ring stays on the button"), Far, SideName(Side)),
				ChangedJustOutside(*Before, After, Second.GetValue(), Side, 2, Far, 24));
			TestFalse(FString::Printf(TEXT("The first button shows no ring along its %s edge"), SideName(Side)),
				DrawnJustInside(After, First.GetValue(), Side, RingFrom, RingTo, 24));
			TestFalse(FString::Printf(TEXT("Outside the first button's %s edge it is as it was before anything had the focus"), SideName(Side)),
				ChangedJustOutside(*Before, After, First.GetValue(), Side, 2, Hug, 24));
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
