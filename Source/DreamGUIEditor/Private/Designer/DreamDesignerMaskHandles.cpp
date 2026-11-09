// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamDesignerEffectHandles.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIClipMask.h"
#include "Designer/DreamDesignerHandles.h"
#include "DreamDesignerHandleShapes.h"

#define LOCTEXT_NAMESPACE "DreamDesignerMaskHandles"

/*
 * A clipping widget's clip and the mask on it, on the canvas: the outline the mask cuts, the band its feather fades over,
 * and handles for the clip rect's sides (its margins, which the mask's shape is fitted to), a rect mask's corners, a
 * polygon's roundness, a star's inner corners or a ring's hole, and the feather. The outline is the mask's own distance
 * function (FDreamUIClipMaskMath, the shader's twin) traced from the clip's centre, so it is where the cut is.
 */
namespace DreamDesignerMaskHandlesLocal
{
	using namespace DreamDesignerHandleShapes;
	namespace Ids = DreamDesignerEffectHandles::MaskIds;

	const FName ClipMaskName(TEXT("ClipMask"));
	const FName ClippingMarginName(TEXT("ClippingMargin"));
	const FName ClippingCornerRadiusName(TEXT("ClippingCornerRadius"));
	const FName UniformCornersName(TEXT("bUniformSetClippingCornerRadius"));

	/** The sides' outward directions: left, top, right, bottom. */
	const FVector2D SideNormals[4] = { FVector2D(-1.0, 0.0), FVector2D(0.0, 1.0), FVector2D(1.0, 0.0), FVector2D(0.0, -1.0) };

	float& MarginSide(FMargin& InMargin, int32 InSide)
	{
		return InSide == 0 ? InMargin.Left : InSide == 1 ? InMargin.Top : InSide == 2 ? InMargin.Right : InMargin.Bottom;
	}

	float GetMarginSide(const FMargin& InMargin, int32 InSide)
	{
		return InSide == 0 ? InMargin.Left : InSide == 1 ? InMargin.Top : InSide == 2 ? InMargin.Right : InMargin.Bottom;
	}

	/** A widget whose mask the details panel lets you edit: clipping to its bounds, the mask not driven by a behaviour. */
	bool HasEditableMask(const UDreamWidget* InWidget)
	{
		if (InWidget == nullptr || InWidget->IsClipMaskDriven())
		{
			return false;
		}
		const EDreamWidgetClipping Clipping = InWidget->GetAuthoredClipping();
		return Clipping == EDreamWidgetClipping::ClipToBounds || Clipping == EDreamWidgetClipping::ClipToBoundsWithoutIntersecting;
	}

	/** The clip rect in the widget's local space, as the clip data frames it: the widget's rect grown by its margins. */
	void GetClipRect(const UDreamWidget* InWidget, FVector2D& OutCentre, FVector2D& OutHalf)
	{
		const FMargin Margin = InWidget->GetClippingMargin();
		OutCentre = InWidget->GetLocalSpaceCenter() + FVector2D(Margin.Right - Margin.Left, Margin.Top - Margin.Bottom) * 0.5;
		OutHalf = FVector2D(InWidget->GetWidth() + Margin.Left + Margin.Right, InWidget->GetHeight() + Margin.Top + Margin.Bottom) * 0.5;
		OutHalf.X = FMath::Max(OutHalf.X, 0.0);
		OutHalf.Y = FMath::Max(OutHalf.Y, 0.0);
	}

	/** The mask's shape about the clip's centre (+Y up), as the shader measures it. */
	struct FShape
	{
		FDreamClipMask Mask;
		FVector2f Half = FVector2f::ZeroVector;
		/** The clip rect's corners in DreamUIClip_sdRoundRect's order. */
		FVector4f Radius = FVector4f::Zero();
		float ParamA = 0.0f;
		float ParamB = 0.0f;

		FShape(const UDreamWidget* InWidget, const FVector2D& InHalf)
			: Mask(InWidget->GetClipMask()), Half(InHalf)
		{
			const FVector4f Corners = InWidget->GetClippingCornerRadius();
			Radius = FVector4f(Corners.Y, Corners.X, Corners.W, Corners.Z);
			FVector4f Packed[DreamUIClipLayout::MaskTexels];
			if (FDreamUIClipMaskMath::PackMask(Mask, Half, INDEX_NONE, Packed))
			{
				ParamA = Packed[0].Y;
				ParamB = Packed[0].Z;
			}
		}

		/** The distance to the outer outline: a ring's outer ellipse, any other shape's own. */
		float Outer(const FVector2f& P) const
		{
			switch (Mask.Shape)
			{
			case EDreamClipMaskShape::Rect:
				return FDreamUIClipMaskMath::SdRoundRect(P, Half, Radius);
			case EDreamClipMaskShape::Ring:
				return FDreamUIClipMaskMath::SdEllipse(P, Half);
			default:
				return FDreamUIClipMaskMath::ShapeDistance(static_cast<int32>(Mask.Shape), P, Half, ParamA, ParamB);
			}
		}

		/** Where the distance to the outer outline is InOffset along the ray from the centre at InDirection; false where it never is. */
		bool Crossing(const FVector2f& InDirection, float InOffset, FVector2f& OutPoint) const
		{
			if (Outer(FVector2f::ZeroVector) - InOffset > 0.0f)
			{
				return false;
			}
			float Low = 0.0f;
			float High = Half.Size() * 1.5f + FMath::Abs(InOffset) + 1.0f;
			for (int32 Iteration = 0; Iteration < 28; ++Iteration)
			{
				const float Mid = (Low + High) * 0.5f;
				(Outer(InDirection * Mid) - InOffset > 0.0f ? High : Low) = Mid;
			}
			OutPoint = InDirection * High;
			return true;
		}

		/** The outline InOffset out from the outer one (in, below zero), traced round, in local space about InCentre. */
		void AddOutline(FDreamDesignerHandleSet& OutSet, const FVector2D& InCentre, float InOffset, const FLinearColor& InColor) const
		{
			constexpr int32 Steps = 96;
			TArray<FVector2D> Points;
			Points.Reserve(Steps);
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				const float Angle = UE_TWO_PI * Step / Steps;
				FVector2f Point;
				if (!Crossing(FVector2f(FMath::Sin(Angle), FMath::Cos(Angle)), InOffset, Point))
				{
					return;
				}
				Points.Add(InCentre + FVector2D(Point));
			}
			OutSet.AddPolyline(Points, /*bInClosed*/ true, InColor);
		}
	};

	/** A star's first inner corner, clockwise from its point straight up: its direction in the unit frame. */
	FVector2D StarInnerDirection(int32 InPoints)
	{
		const double HalfSector = UE_DOUBLE_PI / FMath::Max(InPoints, 2);
		return FVector2D(FMath::Sin(HalfSector), FMath::Cos(HalfSector));
	}

	class FMaskHandles : public IDreamDesignerHandleProvider
	{
	public:
		virtual FName GetName() const override { return FDreamDesignerHandles::MakeBoundName(DreamDesignerEffectHandles::MaskKindName, ClipMaskName); }
		virtual FText GetDisplayName() const override { return LOCTEXT("Name", "Mask"); }

		virtual void CollectHandles(UDreamWidget* InWidget, FDreamDesignerHandleSet& OutSet) const override
		{
			if (!HasEditableMask(InWidget))
			{
				return;
			}
			FVector2D Centre;
			FVector2D Half;
			GetClipRect(InWidget, Centre, Half);
			const FShape Shape(InWidget, Half);
			const FDreamClipMask& Mask = Shape.Mask;
			const double UnitsPerPixel = 1.0 / FMath::Max(OutSet.PixelsPerUnit, UE_KINDA_SMALL_NUMBER);
			const double MinInset = MinInsetPixels * UnitsPerPixel;
			const FLinearColor Guide = FDreamDesignerHandles::GetGuideColor();
			const FLinearColor Faint = Guide.CopyWithNewOpacity(0.45f);

			// The cut, and the feather's band: a rect's sides fade in from each side; any other shape about its outline.
			if (Mask.Shape == EDreamClipMaskShape::Rect)
			{
				OutSet.AddRoundedRect(Centre, Half, InWidget->GetClippingCornerRadius(), Guide);
				for (int32 Side = 0; Side < 4; ++Side)
				{
					const float Feather = GetMarginSide(Mask.Feather, Side);
					if (Feather > 0.0f)
					{
						const FVector2D Normal = SideNormals[Side];
						const FVector2D Along(-Normal.Y, Normal.X);
						const FVector2D Mid = Centre + Normal * (FMath::Abs(Normal.X) * Half.X + FMath::Abs(Normal.Y) * Half.Y - Feather);
						const double Reach = FMath::Abs(Along.X) * Half.X + FMath::Abs(Along.Y) * Half.Y;
						OutSet.AddLine(Mid - Along * Reach, Mid + Along * Reach, Faint);
					}
				}
			}
			else
			{
				Shape.AddOutline(OutSet, Centre, 0.0f, Guide);
				if (Mask.Shape == EDreamClipMaskShape::Ring)
				{
					OutSet.AddEllipse(Centre, Half * FMath::Clamp(Mask.InnerRatio, 0.0f, 1.0f), 0.0f, Guide, 64);
				}
				if (Mask.Feather.Left > 0.0f)
				{
					Shape.AddOutline(OutSet, Centre, Mask.Feather.Left * 0.5f, Faint);
					Shape.AddOutline(OutSet, Centre, -Mask.Feather.Left * 0.5f, Faint);
				}
			}

			// The clip rect's sides: where the shape is fitted.
			for (int32 Side = 0; Side < 4; ++Side)
			{
				const FVector2D Normal = SideNormals[Side];
				FDreamDesignerHandle& Handle = OutSet.AddHandle(Ids::FirstSide + Side, Centre + Normal * (FMath::Abs(Normal.X) * Half.X + FMath::Abs(Normal.Y) * Half.Y));
				Handle.Shape = EDreamDesignerHandleShape::Diamond;
			}
			switch (Mask.Shape)
			{
			case EDreamClipMaskShape::Rect:
			{
				const FVector4f Radii = InWidget->GetClippingCornerRadius();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					FDreamDesignerHandle& Handle = OutSet.AddHandle(Ids::FirstCorner + Corner, CornerHandle(Centre, Half, GetCorner(Radii, Corner), Corner, OutSet.PixelsPerUnit));
					Handle.Shape = EDreamDesignerHandleShape::Circle;
					Handle.Radius = 3.5f;
				}
				break;
			}
			case EDreamClipMaskShape::Polygon:
			{
				FVector2f Top;
				if (Shape.Crossing(FVector2f(0.0f, 1.0f), 0.0f, Top))
				{
					FDreamDesignerHandle& Handle = OutSet.AddHandle(Ids::Roundness, Centre + FVector2D(0.0, Top.Y - FMath::Max(static_cast<double>(Mask.Roundness), MinInset)));
					Handle.Shape = EDreamDesignerHandleShape::Circle;
					Handle.Radius = 3.5f;
				}
				break;
			}
			case EDreamClipMaskShape::Star:
			case EDreamClipMaskShape::Ring:
			{
				const double Ratio = FMath::Clamp(Mask.InnerRatio, 0.0f, 1.0f);
				const FVector2D Direction = Mask.Shape == EDreamClipMaskShape::Star ? StarInnerDirection(Mask.Sides) : FVector2D(1.0, 0.0);
				FDreamDesignerHandle& Handle = OutSet.AddHandle(Ids::InnerRatio, Centre + Direction * Half * Ratio);
				Handle.Shape = EDreamDesignerHandleShape::Circle;
				Handle.Radius = 3.5f;
				break;
			}
			default:
				break;
			}

			// The feather: a rect's per side, in from each; any other shape's one, out from its top.
			if (Mask.Shape == EDreamClipMaskShape::Rect)
			{
				for (int32 Side = 0; Side < 4; ++Side)
				{
					const FVector2D Normal = SideNormals[Side];
					const double Feather = FMath::Max(static_cast<double>(GetMarginSide(Mask.Feather, Side)), MinInset);
					FDreamDesignerHandle& Handle = OutSet.AddHandle(Ids::FirstFeather + Side,
						Centre + Normal * (FMath::Abs(Normal.X) * Half.X + FMath::Abs(Normal.Y) * Half.Y - Feather));
					Handle.Radius = 3.0f;
					Handle.Fill = Faint;
				}
			}
			else
			{
				FVector2f Top;
				if (Shape.Crossing(FVector2f(0.0f, 1.0f), 0.0f, Top))
				{
					const double Out = FMath::Max(Mask.Feather.Left * 0.5, MinInset);
					FDreamDesignerHandle& Handle = OutSet.AddHandle(Ids::FirstFeather, Centre + FVector2D(0.0, Top.Y + Out));
					Handle.Radius = 3.0f;
					Handle.Fill = Faint;
				}
			}
		}

		virtual void GetEditedProperties(UDreamWidget* InWidget, int32 InHandleId, TArray<FDreamDesignerEditedProperty>& OutProperties) const override
		{
			if (!HasEditableMask(InWidget) || InHandleId == INDEX_NONE)
			{
				return;
			}
			const FName Property = InHandleId < Ids::FirstCorner ? ClippingMarginName
				: InHandleId < Ids::Roundness ? ClippingCornerRadiusName
				: ClipMaskName;
			OutProperties.Emplace(InWidget, Property);
		}

		virtual FText GetTransactionName(int32 InHandleId) const override
		{
			if (InHandleId < Ids::FirstCorner)
			{
				return LOCTEXT("MoveSide", "Move Clip Edge");
			}
			if (InHandleId < Ids::Roundness)
			{
				return LOCTEXT("RoundCorners", "Round Clip Corners");
			}
			if (InHandleId == Ids::Roundness)
			{
				return LOCTEXT("RoundPolygon", "Round Mask Polygon");
			}
			if (InHandleId == Ids::InnerRatio)
			{
				return LOCTEXT("InnerRatio", "Resize Mask Inner Size");
			}
			return LOCTEXT("Feather", "Feather Mask");
		}

		virtual bool UpdateDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag) override
		{
			if (!HasEditableMask(InWidget))
			{
				return false;
			}
			// Every step from the values as they were when the handle was grabbed.
			if (!Grabbed.IsSet() || Grabbed->Widget != InWidget || Grabbed->HandleId != InDrag.HandleId)
			{
				Grabbed = FGrab{ InWidget, InDrag.HandleId, InWidget->GetClippingMargin(), InWidget->GetClippingCornerRadius(), InWidget->GetClipMask() };
			}
			const FDreamDesignerHandleDrag& Drag = InDrag;
			const FVector2D Delta = Drag.PointerLocal - Drag.GrabLocal;
			const int32 Id = Drag.HandleId;
			if (Id >= Ids::FirstSide && Id < Ids::FirstSide + 4)
			{
				// A side moves out with the pointer; with Shift its opposite side moves the same way out, the centre staying put.
				const int32 Side = Id - Ids::FirstSide;
				FMargin Margin = Grabbed->Margin;
				const double Moved = FVector2D::DotProduct(Delta, SideNormals[Side]);
				MarginSide(Margin, Side) = GetMarginSide(Grabbed->Margin, Side) + static_cast<float>(Moved);
				if (Drag.bShift)
				{
					const int32 Opposite = (Side + 2) % 4;
					MarginSide(Margin, Opposite) = GetMarginSide(Grabbed->Margin, Opposite) + static_cast<float>(Moved);
				}
				// The clip rect never turns inside out.
				const float Width = InWidget->GetWidth() + Margin.Left + Margin.Right;
				const float Height = InWidget->GetHeight() + Margin.Top + Margin.Bottom;
				if (Width < 0.0f || Height < 0.0f || Margin == InWidget->GetClippingMargin())
				{
					return false;
				}
				InWidget->SetClippingMargin(Margin);
				return true;
			}
			FVector2D Centre;
			FVector2D Half;
			GetClipRect(InWidget, Centre, Half);
			if (Id >= Ids::FirstCorner && Id < Ids::FirstCorner + 4)
			{
				const int32 Corner = Id - Ids::FirstCorner;
				const float Radius = DragCorner(GetCorner(Grabbed->Radii, Corner), Drag, Corner, static_cast<float>(FMath::Min(Half.X, Half.Y)));
				// All four together while the widget sets its corners as one; Alt turns that around for the drag.
				const bool bUniform = ReadBool(InWidget, UniformCornersName, true);
				const FVector4f Radii = WithCorner(Grabbed->Radii, Corner, Radius, bUniform == Drag.bAlt);
				if (Radii == InWidget->GetClippingCornerRadius())
				{
					return false;
				}
				InWidget->SetClippingCornerRadius(Radii);
				return true;
			}
			FDreamClipMask Mask = Grabbed->Mask;
			if (Id == Ids::Roundness)
			{
				Mask.Roundness = FMath::Clamp(static_cast<float>(Grabbed->Mask.Roundness - Delta.Y), 0.0f, static_cast<float>(FMath::Min(Half.X, Half.Y)));
			}
			else if (Id == Ids::InnerRatio)
			{
				const FVector2D Target = Drag.GetTarget() - Centre;
				const FVector2D Unit(Half.X > UE_KINDA_SMALL_NUMBER ? Target.X / Half.X : 0.0, Half.Y > UE_KINDA_SMALL_NUMBER ? Target.Y / Half.Y : 0.0);
				const FVector2D Direction = Mask.Shape == EDreamClipMaskShape::Star ? StarInnerDirection(Mask.Sides) : FVector2D(1.0, 0.0);
				Mask.InnerRatio = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(Unit, Direction)), 0.0f, 1.0f);
			}
			else if (Id >= Ids::FirstFeather && Id < Ids::FirstFeather + 4)
			{
				if (Mask.Shape == EDreamClipMaskShape::Rect)
				{
					// A side's feather goes in with the pointer: all four together, or that side alone with Alt.
					const int32 Side = Id - Ids::FirstFeather;
					const float Feather = FMath::Max(static_cast<float>(GetMarginSide(Mask.Feather, Side) - FVector2D::DotProduct(Delta, SideNormals[Side])), 0.0f);
					if (Drag.bAlt)
					{
						MarginSide(Mask.Feather, Side) = Feather;
					}
					else
					{
						Mask.Feather = FMargin(Feather);
					}
				}
				else
				{
					// Centred on the outline: the band's outer edge is half the feather out.
					Mask.Feather = FMargin(FMath::Max(static_cast<float>(Grabbed->Mask.Feather.Left + 2.0 * Delta.Y), 0.0f));
				}
			}
			else
			{
				return false;
			}
			if (Mask == InWidget->GetClipMask())
			{
				return false;
			}
			InWidget->SetClipMask(Mask);
			return true;
		}

		virtual bool EndDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag, bool bInCancelled) override
		{
			Grabbed.Reset();
			return false;
		}

	private:
		struct FGrab
		{
			TWeakObjectPtr<UDreamWidget> Widget;
			int32 HandleId = INDEX_NONE;
			FMargin Margin;
			FVector4f Radii;
			FDreamClipMask Mask;
		};
		TOptional<FGrab> Grabbed;
	};
}

const FName DreamDesignerEffectHandles::MaskKindName(TEXT("MaskShape"));

FDreamDesignerHandleKindFactory DreamDesignerEffectHandles::MakeMaskKindFactory()
{
	return [](const FDreamDesignerHandleBinding& InBinding) -> TSharedPtr<IDreamDesignerHandleProvider>
	{
		const bool bClipMask = Cast<UDreamWidget>(InBinding.Owner.Get()) != nullptr && InBinding.HeadProperty == InBinding.Property
			&& InBinding.Property == DreamDesignerMaskHandlesLocal::ClipMaskName;
		if (!bClipMask)
		{
			return nullptr;
		}
		return MakeShared<DreamDesignerMaskHandlesLocal::FMaskHandles>();
	};
}

#undef LOCTEXT_NAMESPACE
