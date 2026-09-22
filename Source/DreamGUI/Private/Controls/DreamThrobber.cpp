// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamThrobber.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"

namespace DreamThrobberLocal
{
	/** Centred on the control, absolute size: the ring menu's placement helper, for its reasons. */
	static void PlaceCentred(UDreamWidget* InNode, const FVector2D& InOffset, const FVector2D& InSize)
	{
		if (InNode == nullptr)
		{
			return;
		}
		InNode->SetPivot(FVector2D(0.5, 0.5));
		InNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5, 0.5), FVector2D(0.5, 0.5), false, false);
		InNode->SetAnchoredPositionAndSizeDelta(InOffset, InSize);
	}

	/** Where a radius and a clockwise-from-twelve angle land, in the control's own Y-up frame. */
	static FVector2D PolarToLocal(float InRadius, float InAngleDegrees)
	{
		const float Radians = FMath::DegreesToRadians(InAngleDegrees);
		return FVector2D(InRadius * FMath::Sin(Radians), InRadius * FMath::Cos(Radians));
	}
}

void UDreamThrobber::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	// Required: a throbber with nowhere to put its pieces draws nothing at all, so a template that
	// forgot the node should be told by name rather than come up empty and silent.
	OutParts.Emplace(TEXT("Pieces"), PieceRootNode);
}

void UDreamThrobber::RealizeBuiltIn()
{
	using namespace DreamUI;

	// ONE node, and it is not decoration: the pieces are made in ApplyStyle because their NUMBER is a
	// style property, and "made" needs somewhere to be made into. A control that realized nothing here
	// never gets a UDreamWidgetTree at all -- DreamUI::Realize's owner overload is what creates one --
	// so an empty RealizeBuiltIn left ApplyStyle with a null tree and built zero pieces, which is a
	// throbber that comes up blank everywhere its host does not happen to have made a tree first.
	//
	// Stretched, drawing nothing: it is the frame the pieces are centred in, and the control states
	// its own size from the style (see ApplyStyle), so the root follows that rather than deciding it.
	Realize(this, Widget("Pieces").Stretch());
}

void UDreamThrobber::WireParts()
{
	// Off by default for every widget, and this is the one control in the library that needs it: a
	// throbber has no start and no end, so it cannot be a tween (see the class header).
	SetWantsTick(bAnimate);
}

void UDreamThrobber::ApplyStyle()
{
	const FDreamThrobberStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ThrobberStyle);
	const int32 Count = FMath::Max(1, Active.NumberOfPieces);

	ResizePieces(Count, Active);
	for (int32 Index = 0; Index < PieceNodes.Num(); ++Index)
	{
		PlacePiece(Index, PieceNodes.Num(), Active);
	}
	ApplyPhase(Active);

	// The control's own size, stated: a throbber has no axis that "comes from whoever placed it" --
	// both of its dimensions follow from the pieces -- so it states both, the way the ring menu does
	// and for the same reason.
	if (Shape == EDreamThrobberShape::Circular)
	{
		const float Extent = (Active.Radius + FMath::Max(Active.PieceSize.X, Active.PieceSize.Y) * 0.5f) * 2.0f;
		SizeControl(FVector2D(Extent, Extent));
	}
	else
	{
		const float Width = Count * static_cast<float>(Active.PieceSize.X)
			+ FMath::Max(0, Count - 1) * FMath::Max(0.0f, Active.PieceSpacing);
		SizeControl(FVector2D(Width, Active.PieceSize.Y));
	}
	// Restated here as well as in WireParts: ApplyStyle is where a details-panel edit lands, and
	// bAnimate is a property an author can uncheck without going near a setter.
	SetWantsTick(bAnimate);
}

void UDreamThrobber::ResizePieces(int32 InCount, const FDreamThrobberStyle& InStyle)
{
	using namespace DreamUI;

	// Kept whenever it can be, for UDreamListViewBase::RebuildRows' reason rather than for the frame
	// rate: creating or destroying a widget marks the UI outliner dirty, the designer answers that by
	// force-refreshing the details view, and ApplyStyle runs on every PostEditChangeProperty -- so a
	// control that rebuilt its pieces on each edit would cost tens of milliseconds per click.
	for (int32 Index = PieceNodes.Num() - 1; Index >= InCount; --Index)
	{
		if (IsValid(PieceNodes[Index]))
		{
			PieceNodes[Index]->DestroyWidget();
		}
		PieceNodes.RemoveAt(Index);
	}
	while (PieceNodes.Num() < InCount)
	{
		if (!IsValid(WidgetTree) || !IsValid(PieceRootNode))
		{
			// A class default object edited in a Blueprint's defaults panel has neither: it is a class,
			// not a hierarchy. Nothing to draw and nothing to correct -- the instance builds its pieces
			// when its own RealizeBuiltIn has made the root, which is before the first ApplyStyle.
			break;
		}
		const int32 Index = PieceNodes.Num();
		UDreamWidget* Piece = Realize(WidgetTree,
			Node<UDreamRectBlock>(*FString::Printf(TEXT("Piece_%d"), Index)),
			PieceRootNode);
		if (!IsValid(Piece))
		{
			// Going round again would spin. Stop short rather than never returning -- the list's pool
			// makes the same call in the same words.
			break;
		}
		if (PieceRootNode->HasRegistered() && !Piece->HasRegistered())
		{
			// Built UNDER something already live: the first pass runs inside NativeOnInitialized, ahead
			// of registration, and the whole tree registers together -- but a details-panel edit that
			// raises the piece count builds into a control that is already on screen, and a widget that
			// never registered is laid out by nobody and drawn by nobody. UDreamTabView::AttachPage
			// draws the same branch for the same reason.
			RegisterDreamWidgetHierarchy(Piece);
		}
		PieceNodes.Add(Piece);
	}

	for (UDreamWidget* Piece : PieceNodes)
	{
		ShapeFace(Piece, InStyle.CornerRadius);
		SkinFace(Piece, InStyle.PieceBrush);
		if (UDreamVisual* PieceVisual = IsValid(Piece) ? Piece->GetVisual() : nullptr)
		{
			PieceVisual->SetColor(InStyle.PieceColor);
		}
	}
}

void UDreamThrobber::PlacePiece(int32 InIndex, int32 InCount, const FDreamThrobberStyle& InStyle)
{
	using namespace DreamThrobberLocal;

	UDreamWidget* Piece = PieceNodes.IsValidIndex(InIndex) ? PieceNodes[InIndex].Get() : nullptr;
	if (!IsValid(Piece))
	{
		return;
	}
	if (Shape == EDreamThrobberShape::Circular)
	{
		// Evenly round the circle, clockwise from twelve -- the family's angle convention, the same
		// one UDreamRingMenu's wedges use.
		const float Angle = InCount > 0 ? (360.0f * InIndex) / InCount : 0.0f;
		PlaceCentred(Piece, PolarToLocal(InStyle.Radius, Angle), InStyle.PieceSize);
		return;
	}
	// A row, centred on the control: piece i sits (i - (N-1)/2) pitches from the middle, which is the
	// spelling that needs no separate case for an odd or an even count.
	const float Pitch = static_cast<float>(InStyle.PieceSize.X) + FMath::Max(0.0f, InStyle.PieceSpacing);
	const float Offset = (InIndex - (InCount - 1) * 0.5f) * Pitch;
	PlaceCentred(Piece, FVector2D(Offset, 0.0), InStyle.PieceSize);
}

float UDreamThrobber::GetPhase() const
{
	const FDreamThrobberStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ThrobberStyle);
	const float Period = FMath::Max(Active.Period, KINDA_SMALL_NUMBER);
	return FMath::Fmod(ElapsedInCycle, Period) / Period;
}

void UDreamThrobber::ApplyPhase(const FDreamThrobberStyle& InStyle)
{
	const int32 Count = PieceNodes.Num();
	if (Count == 0)
	{
		return;
	}
	const float Period = FMath::Max(InStyle.Period, KINDA_SMALL_NUMBER);
	const float Phase = FMath::Fmod(ElapsedInCycle, Period) / Period;
	const float Floor = FMath::Clamp(InStyle.MinOpacity, 0.0f, 1.0f);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		UDreamWidget* Piece = PieceNodes[Index].Get();
		if (!IsValid(Piece))
		{
			continue;
		}
		// A cosine rather than a sawtooth, and one full turn spread across the pieces: a piece is
		// brightest when the wave reaches it and dimmest half a cycle later, with no seam where the
		// cycle wraps. The circular shape animates the same way instead of rotating the ring -- the
		// pieces are identical, so a travelling highlight and a turning ring are the same picture,
		// and this one costs no render transform.
		const float PieceTurns = Phase - (static_cast<float>(Index) / static_cast<float>(Count));
		const float Wave = 0.5f * (1.0f + FMath::Cos(2.0f * PI * PieceTurns));
		Piece->SetRenderOpacity(FMath::Lerp(Floor, 1.0f, Wave));
		if (bAnimateHorizontally || bAnimateVertically)
		{
			// Slate's rule for the same two flags: the piece is SCALED on the ticked axis by the very
			// wave that is driving its opacity, so it swells as it brightens. One wave for both rather
			// than a second clock, because two clocks on one piece is how a pulse starts to shimmer.
			//
			// Touched only while a flag is on, which is what keeps every existing throbber's pieces
			// exactly the size the placement gave them.
			Piece->SetWidth(bAnimateHorizontally
				? static_cast<float>(InStyle.PieceSize.X) * Wave
				: static_cast<float>(InStyle.PieceSize.X));
			Piece->SetHeight(bAnimateVertically
				? static_cast<float>(InStyle.PieceSize.Y) * Wave
				: static_cast<float>(InStyle.PieceSize.Y));
		}
	}
}

void UDreamThrobber::NativeOnTick(float DeltaTime)
{
	Super::NativeOnTick(DeltaTime);
	if (!bAnimate)
	{
		return;
	}
	const FDreamThrobberStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ThrobberStyle);
	const float Period = FMath::Max(Active.Period, KINDA_SMALL_NUMBER);
	// Wrapped rather than accumulated: a clock that only ever grows loses its fractional precision
	// after a few hours on screen, and a throbber is exactly the widget somebody leaves up.
	ElapsedInCycle = FMath::Fmod(ElapsedInCycle + DeltaTime, Period);
	ApplyPhase(Active);
}

void UDreamThrobber::SetStyle(const FDreamThrobberStyle& InStyle)
{
	Style = InStyle;
	// The push is what resizes the piece pool, re-places every piece and re-writes the opacities, so
	// a replacement style with a different NumberOfPieces lands whole rather than as a colour change
	// over the previous count.
	ApplyStyle();
}

void UDreamThrobber::SetShape(EDreamThrobberShape InShape)
{
	if (Shape == InShape)
	{
		return;
	}
	Shape = InShape;
	// Through the style push, because the shape decides the control's own size as well as where the
	// pieces go, and a caller should not have to know that.
	ApplyStyle();
}

void UDreamThrobber::SetAnimateHorizontally(bool bInAnimateHorizontally)
{
	if (bAnimateHorizontally == bInAnimateHorizontally)
	{
		return;
	}
	bAnimateHorizontally = bInAnimateHorizontally;
	// Through the style push, which re-places every piece at its authored size: a piece frozen
	// mid-pulse would otherwise keep whatever width the last tick gave it.
	ApplyStyle();
}

void UDreamThrobber::SetAnimateVertically(bool bInAnimateVertically)
{
	if (bAnimateVertically == bInAnimateVertically)
	{
		return;
	}
	bAnimateVertically = bInAnimateVertically;
	ApplyStyle();
}

int32 UDreamThrobber::GetNumberOfPieces() const
{
	return ResolveStyle(Style, &UDreamUIStyleSheet::ThrobberStyle).NumberOfPieces;
}

void UDreamThrobber::SetNumberOfPieces(int32 InNumberOfPieces)
{
	// Clamped where the style clamps it. The push is what actually makes or destroys pieces.
	Style.NumberOfPieces = FMath::Clamp(InNumberOfPieces, 1, 32);
	ApplyStyle();
}

float UDreamThrobber::GetPeriod() const
{
	return ResolveStyle(Style, &UDreamUIStyleSheet::ThrobberStyle).Period;
}

void UDreamThrobber::SetPeriod(float InPeriod)
{
	Style.Period = FMath::Max(InPeriod, 0.01f);
	ApplyStyle();
}

float UDreamThrobber::GetRadius() const
{
	return ResolveStyle(Style, &UDreamUIStyleSheet::ThrobberStyle).Radius;
}

void UDreamThrobber::SetRadius(float InRadius)
{
	Style.Radius = FMath::Max(InRadius, 1.0f);
	ApplyStyle();
}

bool UDreamThrobber::GetAnimateOpacity() const
{
	// A floor of 1 is every piece staying solid, which is UMG's flag being off.
	return ResolveStyle(Style, &UDreamUIStyleSheet::ThrobberStyle).MinOpacity < 1.0f;
}

void UDreamThrobber::SetAnimateOpacity(bool bInAnimateOpacity)
{
	if (!bInAnimateOpacity)
	{
		Style.MinOpacity = 1.0f;
	}
	else if (Style.MinOpacity >= 1.0f)
	{
		// Only from a pinned floor, so switching off and on again is not a way to lose a hand-tuned
		// value -- and a throbber that is already fading keeps fading exactly as far as it did.
		Style.MinOpacity = FDreamThrobberStyle().MinOpacity;
	}
	ApplyStyle();
}

void UDreamThrobber::SetAnimate(bool bInAnimate)
{
	if (bAnimate == bInAnimate)
	{
		return;
	}
	bAnimate = bInAnimate;
	// Stopping leaves the pieces where they are rather than resetting them: a throbber that snapped
	// back to phase zero would read as a glitch, not as a pause.
	SetWantsTick(bAnimate);
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "Throbber", UDreamThrobber)
