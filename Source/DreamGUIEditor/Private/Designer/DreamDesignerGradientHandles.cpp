// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamDesignerGradientHandles.h"

#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamDesignerHandles.h"
#include "Designer/DreamGradientHandleMath.h"

#define LOCTEXT_NAMESPACE "DreamDesignerGradientHandles"

/*
 * A rect block's body or border gradient on the canvas, as an image editor's gradient tool shows one: a Linear's or a
 * Reflected's start and end, a Radial's, a Diamond's or a Conic's centre and end (and a Radial ellipse's second radius), the
 * line between them, and each stop as a square of its colour beside the line. Dragging a point moves the gradient; dragging
 * a stop moves it along the line, and away from it, takes it out (two stops always stay); a double click on the line adds a
 * stop of the colour already there. A rect block whose gradient is still the two-colour one is drawn from the two stops it
 * makes, and the first change writes them into its gradient.
 */
namespace DreamDesignerGradientHandlesLocal
{
	/** A handle's number: its gradient's slot, and the point or the stop it is. */
	constexpr int32 SlotStride = 1000;
	constexpr int32 FirstStopId = 100;
	/** How far beside the line a stop's square sits, and how far off the line a stop has to be dragged to come out, in pixels. */
	constexpr double StopOffsetPixels = 14.0;
	constexpr double TakeOutPixels = 40.0;
	/** How near the line a double click has to be to add a stop there, in pixels. */
	constexpr double AddStopPixels = 8.0;
	/** Stops a gradient keeps whatever is dragged off it. */
	constexpr int32 MinStops = 2;

	bool IsSlotShown(const UDreamRectBlock* InBlock, EDreamRectBlockGradientSlot InSlot)
	{
		if (InBlock == nullptr || InBlock->IsMeltedIntoShapeBlend())
		{
			return false;
		}
		return InSlot == EDreamRectBlockGradientSlot::Body
			? InBlock->GetEnableBody() && InBlock->GetEnableBodyGradient()
			: InBlock->GetEnableBorder() && InBlock->GetEnableBorderGradient();
	}

	FDreamGradient GetGradient(const UDreamRectBlock* InBlock, EDreamRectBlockGradientSlot InSlot)
	{
		return InSlot == EDreamRectBlockGradientSlot::Body ? InBlock->GetEffectiveBodyGradient() : InBlock->GetEffectiveBorderGradient();
	}

	void SetGradient(UDreamRectBlock* InBlock, EDreamRectBlockGradientSlot InSlot, const FDreamGradient& InGradient)
	{
		if (InSlot == EDreamRectBlockGradientSlot::Body)
		{
			InBlock->SetBodyGradient(InGradient);
		}
		else
		{
			InBlock->SetBorderGradient(InGradient);
		}
	}

	FName GetPropertyName(EDreamRectBlockGradientSlot InSlot)
	{
		return InSlot == EDreamRectBlockGradientSlot::Body ? FName(TEXT("BodyGradient")) : FName(TEXT("BorderGradient"));
	}

	float GetAspect(const UDreamWidget* InWidget)
	{
		const float Height = InWidget->GetHeight();
		return Height > UE_KINDA_SMALL_NUMBER ? InWidget->GetWidth() / Height : 1.0f;
	}

	FLinearColor StopFill(const FColor& InColor)
	{
		return FLinearColor::FromSRGBColor(InColor);
	}

	/** A point of the line in the widget's local space. */
	FVector2D ToLocal(const UDreamWidget* InWidget, const FVector2D& InFraction)
	{
		return FDreamDesignerHandles::RectFractionToLocal(InWidget, InFraction);
	}

	/**
	 * Where a stop's square goes: beside the line (outside the circle, for a Conic), InOffsetUnits from the point of its
	 * position, on the side the line's left is.
	 */
	FVector2D StopHandleLocal(const UDreamWidget* InWidget, const FDreamGradient& InGradient, const FDreamGradientLine& InLine, float InPosition, float InAspect, double InOffsetUnits)
	{
		const FVector2D On = ToLocal(InWidget, DreamGradientHandles::GetPointAt(InGradient, InLine, InPosition, InAspect));
		FVector2D Away;
		if (InLine.bAround)
		{
			Away = (On - ToLocal(InWidget, InLine.Center)).GetSafeNormal();
		}
		else
		{
			const FVector2D Along = (ToLocal(InWidget, InLine.End) - ToLocal(InWidget, InLine.Start)).GetSafeNormal();
			Away = FVector2D(-Along.Y, Along.X);
		}
		if (Away.IsNearlyZero())
		{
			Away = FVector2D(0.0, 1.0);
		}
		return On + Away * InOffsetUnits;
	}

	/** One of a rect block's gradients: its body's or its border's, each a kind of handles of its own. */
	class FGradientHandles : public IDreamDesignerHandleProvider
	{
	public:
		explicit FGradientHandles(EDreamRectBlockGradientSlot InSlot) : OwnSlot(InSlot) {}

		virtual FName GetName() const override { return DreamDesignerGradientHandles::GetProviderName(OwnSlot); }
		virtual FText GetDisplayName() const override
		{
			return OwnSlot == EDreamRectBlockGradientSlot::Body ? LOCTEXT("BodyGradient", "Body Gradient") : LOCTEXT("BorderGradient", "Border Gradient");
		}

		virtual void CollectHandles(UDreamWidget* InWidget, FDreamDesignerHandleSet& OutSet) const override
		{
			const UDreamRectBlock* Block = Cast<UDreamRectBlock>(InWidget->GetVisual());
			const EDreamRectBlockGradientSlot Slot = OwnSlot;
			if (!IsSlotShown(Block, Slot))
			{
				return;
			}
			const FDreamGradient Gradient = GetGradient(Block, Slot);
			const float Aspect = GetAspect(InWidget);
			FDreamGradientLine Line;
			if (!Gradient.IsPainting() || !DreamGradientHandles::GetLine(Gradient, Aspect, Line))
			{
				return;
			}
			const double Height = FMath::Max(static_cast<double>(InWidget->GetHeight()), UE_KINDA_SMALL_NUMBER);
			const double UnitsPerPixel = 1.0 / FMath::Max(OutSet.PixelsPerUnit, UE_KINDA_SMALL_NUMBER);
			const FLinearColor Guide = FDreamDesignerHandles::GetGuideColor();
			const FLinearColor Shadow(0.0f, 0.0f, 0.0f, 0.55f);
			auto AddGuide = [&OutSet, &Guide, &Shadow](const FVector2D& InFrom, const FVector2D& InTo)
			{
				OutSet.AddLine(InFrom, InTo, Shadow, 3.0f);
				OutSet.AddLine(InFrom, InTo, Guide, 1.0f);
			};
			const FVector2D Centre = ToLocal(InWidget, Line.Center);
			const FVector2D Start = ToLocal(InWidget, Line.Start);
			const FVector2D End = ToLocal(InWidget, Line.End);

			// The guides: the line, and the shape a T of 1 makes.
			if (Line.bAround)
			{
				const double Radius = DreamGradientHandles::ConicRadius * Height;
				OutSet.AddEllipse(Centre, FVector2D(Radius, Radius), 0.0f, Shadow);
				OutSet.AddEllipse(Centre, FVector2D(Radius, Radius), 0.0f, Guide);
				AddGuide(Centre, End);
			}
			else
			{
				AddGuide(Start, End);
				if (Gradient.Type == EDreamPaintType::Reflected)
				{
					// Its mirror: the gradient runs the same way on the other side of its start.
					OutSet.AddLine(Start, Start - (End - Start), FLinearColor(Guide.R, Guide.G, Guide.B, 0.4f));
				}
				else if (Gradient.Type == EDreamPaintType::Radial)
				{
					const FVector2D First = End - Centre;
					const FVector2D Second = Line.bHasSecondEnd ? ToLocal(InWidget, Line.SecondEnd) - Centre : FVector2D(-First.Y, First.X);
					const float Turn = FMath::RadiansToDegrees(FMath::Atan2(-First.Y, First.X));
					OutSet.AddEllipse(Centre, FVector2D(First.Size(), Second.Size()), Turn, FLinearColor(Guide.R, Guide.G, Guide.B, 0.6f));
					if (Line.bHasSecondEnd)
					{
						AddGuide(Centre, Centre + Second);
					}
				}
				else if (Gradient.Type == EDreamPaintType::Diamond)
				{
					const FVector2D First = End - Centre;
					const FVector2D Radii(Gradient.GetEndingRadii(Aspect));
					const double SecondLength = Radii.X > 1e-9 ? First.Size() * Radii.Y / Radii.X : First.Size();
					const FVector2D Second = FVector2D(-First.Y, First.X).GetSafeNormal() * SecondLength;
					OutSet.AddPolyline({ Centre + First, Centre + Second, Centre - First, Centre - Second }, /*bInClosed*/ true, FLinearColor(Guide.R, Guide.G, Guide.B, 0.6f));
				}
			}

			// The points that move the gradient.
			for (const EDreamGradientPoint Point : DreamGradientHandles::GetHandlePoints(Gradient))
			{
				FDreamDesignerHandle& Handle = OutSet.AddHandle(static_cast<int32>(Slot) * SlotStride + static_cast<int32>(Point),
					ToLocal(InWidget, DreamGradientHandles::GetPoint(Line, Point)));
				Handle.Shape = Point == EDreamGradientPoint::Center && Gradient.Type != EDreamPaintType::Diamond
					? EDreamDesignerHandleShape::Diamond : EDreamDesignerHandleShape::Circle;
				Handle.Radius = 5.0f;
				Handle.Outline = Guide;
				Handle.Fill = FLinearColor(0.1f, 0.1f, 0.12f, 0.9f);
			}

			// The stops, each beside the line where it is, joined to it by a tick.
			const double Offset = StopOffsetPixels * UnitsPerPixel;
			for (int32 Index = 0; Index < Gradient.Stops.Num() && Index < FDreamGradient::MaxStops; ++Index)
			{
				const FDreamGradientStop& Stop = Gradient.Stops[Index];
				const FVector2D On = ToLocal(InWidget, DreamGradientHandles::GetPointAt(Gradient, Line, Stop.Position, Aspect));
				const FVector2D Beside = StopHandleLocal(InWidget, Gradient, Line, Stop.Position, Aspect, Offset);
				OutSet.AddLine(On, Beside, Guide);
				FDreamDesignerHandle& Handle = OutSet.AddHandle(static_cast<int32>(Slot) * SlotStride + FirstStopId + Index, Beside);
				Handle.Shape = EDreamDesignerHandleShape::Square;
				Handle.Radius = 5.0f;
				Handle.Outline = Guide;
				Handle.Fill = StopFill(Stop.Color);
				Handle.Priority = 1;
			}
		}

		virtual void GetEditedProperties(UDreamWidget* InWidget, int32 InHandleId, TArray<FDreamDesignerEditedProperty>& OutProperties) const override
		{
			UDreamRectBlock* Block = Cast<UDreamRectBlock>(InWidget->GetVisual());
			EDreamRectBlockGradientSlot Slot;
			if (Block != nullptr && ResolveSlot(Block, InHandleId, Slot))
			{
				OutProperties.Emplace(Block, GetPropertyName(Slot));
			}
		}

		virtual FText GetTransactionName(int32 InHandleId) const override
		{
			if (InHandleId == INDEX_NONE)
			{
				return LOCTEXT("AddStop", "Add Gradient Stop");
			}
			return InHandleId % SlotStride >= FirstStopId
				? LOCTEXT("MoveStop", "Move Gradient Stop")
				: LOCTEXT("MovePoint", "Move Gradient");
		}

		virtual bool UpdateDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag) override
		{
			UDreamRectBlock* Block = Cast<UDreamRectBlock>(InWidget->GetVisual());
			EDreamRectBlockGradientSlot Slot;
			if (Block == nullptr || !ResolveSlot(Block, InDrag.HandleId, Slot))
			{
				return false;
			}
			// Every step starts from the gradient as it was when the handle was grabbed: a stop dragged past another and back
			// comes back where it was, in its place among the others.
			if (!Grabbed.IsSet() || Grabbed->Block != Block || Grabbed->HandleId != InDrag.HandleId)
			{
				Grabbed = FGrab{ Block, InDrag.HandleId, GetGradient(Block, Slot) };
			}
			const float Aspect = GetAspect(InWidget);
			FDreamGradient Edited = Grabbed->Gradient;
			const int32 Kind = InDrag.HandleId % SlotStride;
			if (Kind >= FirstStopId)
			{
				FDreamGradientLine Line;
				const int32 StopIndex = Kind - FirstStopId;
				if (!Edited.Stops.IsValidIndex(StopIndex) || !DreamGradientHandles::GetLine(Edited, Aspect, Line))
				{
					return false;
				}
				const FVector2D Target = FDreamDesignerHandles::LocalToRectFraction(InWidget, InDrag.GetTarget());
				const FVector2D Pointer = FDreamDesignerHandles::LocalToRectFraction(InWidget, InDrag.PointerLocal);
				double TargetOff = 0.0;
				const float Position = FMath::Clamp(DreamGradientHandles::Project(Edited, Line, Target, Aspect, TargetOff), 0.0f, 1.0f);
				double PointerOff = 0.0;
				DreamGradientHandles::Project(Edited, Line, Pointer, Aspect, PointerOff);
				const double PointerOffPixels = PointerOff * InWidget->GetHeight() * InDrag.PixelsPerUnit;
				if (PointerOffPixels > TakeOutPixels && Edited.Stops.Num() > MinStops)
				{
					Edited.Stops.RemoveAt(StopIndex);
				}
				else
				{
					const FDreamGradientStop Moved(Position, Edited.Stops[StopIndex].Color);
					Edited.Stops.RemoveAt(StopIndex);
					DreamGradientHandles::InsertStop(Edited, Moved.Position, Moved.Color);
				}
			}
			else
			{
				DreamGradientHandles::MovePoint(Edited, Aspect, static_cast<EDreamGradientPoint>(Kind),
					FDreamDesignerHandles::LocalToRectFraction(InWidget, InDrag.GetTarget()), InDrag.bShift);
			}
			if (Edited == GetGradient(Block, Slot))
			{
				return false;
			}
			SetGradient(Block, Slot, Edited);
			return true;
		}

		virtual bool EndDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag, bool bInCancelled) override
		{
			Grabbed.Reset();
			return false;
		}

		virtual bool DoubleClick(UDreamWidget* InWidget, const FVector2D& InLocal, int32 InHandleId, double InPixelsPerUnit) override
		{
			UDreamRectBlock* Block = Cast<UDreamRectBlock>(InWidget->GetVisual());
			EDreamRectBlockGradientSlot Slot;
			if (Block == nullptr || !ResolveSlot(Block, InHandleId, Slot) || (InHandleId != INDEX_NONE && InHandleId % SlotStride >= FirstStopId))
			{
				return false;
			}
			FDreamGradient Edited = GetGradient(Block, Slot);
			const float Aspect = GetAspect(InWidget);
			FDreamGradientLine Line;
			if (!Edited.IsPainting() || Edited.Stops.Num() >= FDreamGradient::MaxStops || !DreamGradientHandles::GetLine(Edited, Aspect, Line))
			{
				return false;
			}
			double Off = 0.0;
			const float Position = DreamGradientHandles::Project(Edited, Line, FDreamDesignerHandles::LocalToRectFraction(InWidget, InLocal), Aspect, Off);
			if (Position < 0.0f || Position > 1.0f || Off * InWidget->GetHeight() * InPixelsPerUnit > AddStopPixels)
			{
				return false;
			}
			DreamGradientHandles::InsertStop(Edited, Position, DreamGradientHandles::GetStopColorAt(Edited, Position));
			SetGradient(Block, Slot, Edited);
			return true;
		}

	private:
		struct FGrab
		{
			TWeakObjectPtr<UDreamRectBlock> Block;
			int32 HandleId = INDEX_NONE;
			FDreamGradient Gradient;
		};
		/** The gradient as it was when the held handle was grabbed. */
		TOptional<FGrab> Grabbed;

		EDreamRectBlockGradientSlot OwnSlot;

		/** This provider's slot, when the handle (or, for INDEX_NONE, the click) is its own and its gradient is shown. */
		bool ResolveSlot(const UDreamRectBlock* InBlock, int32 InHandleId, EDreamRectBlockGradientSlot& OutSlot) const
		{
			if (InHandleId != INDEX_NONE && InHandleId / SlotStride != static_cast<int32>(OwnSlot))
			{
				return false;
			}
			OutSlot = OwnSlot;
			return IsSlotShown(InBlock, OutSlot);
		}
	};
}

const FName DreamDesignerGradientHandles::KindName(TEXT("Gradient"));

FDreamDesignerHandleKindFactory DreamDesignerGradientHandles::MakeKindFactory()
{
	return [](const FDreamDesignerHandleBinding& InBinding) -> TSharedPtr<IDreamDesignerHandleProvider>
	{
		using namespace DreamDesignerGradientHandlesLocal;
		if (Cast<UDreamRectBlock>(InBinding.Owner.Get()) == nullptr || InBinding.HeadProperty != InBinding.Property)
		{
			return nullptr;
		}
		for (const EDreamRectBlockGradientSlot Slot : { EDreamRectBlockGradientSlot::Body, EDreamRectBlockGradientSlot::Border })
		{
			if (InBinding.Property == GetPropertyName(Slot))
			{
				return MakeShared<FGradientHandles>(Slot);
			}
		}
		return nullptr;
	};
}

FName DreamDesignerGradientHandles::GetProviderName(EDreamRectBlockGradientSlot InSlot)
{
	static const FName Body = FDreamDesignerHandles::MakeBoundName(KindName, TEXT("BodyGradient"));
	static const FName Border = FDreamDesignerHandles::MakeBoundName(KindName, TEXT("BorderGradient"));
	return InSlot == EDreamRectBlockGradientSlot::Body ? Body : Border;
}

#undef LOCTEXT_NAMESPACE
