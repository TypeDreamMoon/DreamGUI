// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamDesignerEffectHandles.h"

#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamDesignerHandles.h"
#include "DreamDesignerHandleShapes.h"
#include "Extensions/Effects/DreamGlass.h"

#define LOCTEXT_NAMESPACE "DreamDesignerGlassHandles"

/*
 * A glass's shape on the canvas while it is the rounded rect (a shape blend's melted shape is its rect blocks' to change):
 * its outline, the line its refracting edge reaches in to, and handles for its corners, that edge's width and where its
 * highlight's light comes from. Corners are written in the glass's own unit mode.
 */
namespace DreamDesignerGlassHandlesLocal
{
	using namespace DreamDesignerHandleShapes;
	namespace Ids = DreamDesignerEffectHandles::GlassIds;

	const FName CornerRadiusName(TEXT("CornerRadius"));
	const FName CornerRadiusUnitModeName(TEXT("CornerRadiusUnitMode"));
	const FName EdgeWidthName(TEXT("EdgeWidth"));
	const FName LightAngleName(TEXT("LightAngle"));
	const FName HighlightColorName(TEXT("HighlightColor"));
	const FName HighlightStrengthName(TEXT("HighlightStrength"));
	const FName HighlightWidthName(TEXT("HighlightWidth"));

	UDreamGlass* FindGlass(const UDreamWidget* InWidget)
	{
		UDreamGlass* Glass = InWidget != nullptr ? Cast<UDreamGlass>(InWidget->GetVisual()) : nullptr;
		return Glass != nullptr && Glass->IsEnabled() && Glass->GetShape() == EDreamGlassShape::RoundedRect ? Glass : nullptr;
	}

	/** Units per unit of the corners as written: one, or the share of half the shorter side a percentage is of. */
	double CornerScale(const UDreamGlass* InGlass, const UDreamWidget* InWidget)
	{
		const EDreamRectBlockUnitMode Mode = Read<EDreamRectBlockUnitMode>(InGlass, CornerRadiusUnitModeName, EDreamRectBlockUnitMode::Value);
		return Mode == EDreamRectBlockUnitMode::Value ? 1.0 : FMath::Min(InWidget->GetWidth(), InWidget->GetHeight()) * 0.5;
	}

	/** Where the light comes from, as a direction (degrees, 0 up, clockwise), and back. */
	FVector2D LightDirection(double InDegrees)
	{
		const double Radians = FMath::DegreesToRadians(InDegrees);
		return FVector2D(FMath::Sin(Radians), FMath::Cos(Radians));
	}

	class FGlassHandles : public IDreamDesignerHandleProvider
	{
	public:
		virtual FName GetName() const override { return FDreamDesignerHandles::MakeBoundName(DreamDesignerEffectHandles::GlassKindName, CornerRadiusName); }
		virtual FText GetDisplayName() const override { return LOCTEXT("Name", "Glass"); }

		virtual void CollectHandles(UDreamWidget* InWidget, FDreamDesignerHandleSet& OutSet) const override
		{
			const UDreamGlass* Glass = FindGlass(InWidget);
			if (Glass == nullptr)
			{
				return;
			}
			const FVector2D Centre = InWidget->GetLocalSpaceCenter();
			const FVector2D Half(InWidget->GetWidth() * 0.5, InWidget->GetHeight() * 0.5);
			const double Scale = CornerScale(Glass, InWidget);
			const FVector4f Radii = Read<FVector4f>(Glass, CornerRadiusName) * static_cast<float>(Scale);
			const float Edge = Read<float>(Glass, EdgeWidthName);
			const FLinearColor Guide = FDreamDesignerHandles::GetGuideColor();
			const FLinearColor Faint = Guide.CopyWithNewOpacity(0.45f);
			const double MinInset = MinInsetPixels / FMath::Max(OutSet.PixelsPerUnit, UE_KINDA_SMALL_NUMBER);

			OutSet.AddRoundedRect(Centre, Half, Radii, Guide);
			// The refracting edge reaches this far in: the rect inset by it, its corners by as much.
			const FVector2D Inner(FMath::Max(Half.X - Edge, 0.0), FMath::Max(Half.Y - Edge, 0.0));
			if (Inner.X > 0.0 && Inner.Y > 0.0)
			{
				const FVector4f InnerRadii(FMath::Max(Radii.X - Edge, 0.0f), FMath::Max(Radii.Y - Edge, 0.0f), FMath::Max(Radii.Z - Edge, 0.0f), FMath::Max(Radii.W - Edge, 0.0f));
				OutSet.AddRoundedRect(Centre, Inner, InnerRadii, Faint);
			}
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				FDreamDesignerHandle& Handle = OutSet.AddHandle(Ids::FirstCorner + Corner, CornerHandle(Centre, Half, GetCorner(Radii, Corner), Corner, OutSet.PixelsPerUnit));
				Handle.Shape = EDreamDesignerHandleShape::Circle;
				Handle.Radius = 3.5f;
			}
			{
				FDreamDesignerHandle& Handle = OutSet.AddHandle(Ids::EdgeWidth, Centre + FVector2D(0.0, Half.Y - FMath::Max(static_cast<double>(Edge), MinInset)));
				Handle.Radius = 3.0f;
				Handle.Fill = Faint;
			}
			// The light on a spoke from the centre, most of the way to the nearer side.
			const FVector2D Light = Centre + LightDirection(Read<float>(Glass, LightAngleName)) * (FMath::Min(Half.X, Half.Y) * 0.6);
			OutSet.AddLine(Centre, Light, Faint);
			FDreamDesignerHandle& Handle = OutSet.AddHandle(Ids::LightAngle, Light);
			Handle.Shape = EDreamDesignerHandleShape::Circle;
			Handle.Fill = FLinearColor::FromSRGBColor(Read<FColor>(Glass, HighlightColorName, FColor::White));
			Handle.Radius = 5.0f;
		}

		virtual void GetEditedProperties(UDreamWidget* InWidget, int32 InHandleId, TArray<FDreamDesignerEditedProperty>& OutProperties) const override
		{
			UDreamGlass* Glass = FindGlass(InWidget);
			if (Glass == nullptr || InHandleId == INDEX_NONE)
			{
				return;
			}
			OutProperties.Emplace(Glass, InHandleId == Ids::EdgeWidth ? EdgeWidthName : InHandleId == Ids::LightAngle ? LightAngleName : CornerRadiusName);
		}

		virtual FText GetTransactionName(int32 InHandleId) const override
		{
			return InHandleId == Ids::EdgeWidth ? LOCTEXT("EdgeWidth", "Change Glass Edge Width")
				: InHandleId == Ids::LightAngle ? LOCTEXT("LightAngle", "Turn Glass Light")
				: LOCTEXT("Corners", "Round Glass Corners");
		}

		virtual bool UpdateDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag) override
		{
			UDreamGlass* Glass = FindGlass(InWidget);
			if (Glass == nullptr)
			{
				return false;
			}
			if (!Grabbed.IsSet() || Grabbed->Glass != Glass || Grabbed->HandleId != InDrag.HandleId)
			{
				Grabbed = FGrab{ Glass, InDrag.HandleId, Read<FVector4f>(Glass, CornerRadiusName), Read<float>(Glass, EdgeWidthName) };
			}
			const FVector2D Centre = InWidget->GetLocalSpaceCenter();
			const FVector2D Half(InWidget->GetWidth() * 0.5, InWidget->GetHeight() * 0.5);
			const int32 Id = InDrag.HandleId;
			if (Id >= Ids::FirstCorner && Id < Ids::FirstCorner + 4)
			{
				// Measured in units, written back in the glass's unit mode; all four together, or this one with Alt.
				const int32 Corner = Id - Ids::FirstCorner;
				const double Scale = FMath::Max(CornerScale(Glass, InWidget), UE_KINDA_SMALL_NUMBER);
				const float Units = DragCorner(static_cast<float>(GetCorner(Grabbed->Radii, Corner) * Scale), InDrag, Corner, static_cast<float>(FMath::Min(Half.X, Half.Y)));
				const FVector4f Radii = WithCorner(Grabbed->Radii, Corner, static_cast<float>(Units / Scale), InDrag.bAlt);
				if (Radii == Read<FVector4f>(Glass, CornerRadiusName))
				{
					return false;
				}
				Glass->SetCornerRadius(Radii);
				return true;
			}
			if (Id == Ids::EdgeWidth)
			{
				// In from the top side with the pointer; the glass keeps half a unit of edge at the least.
				const float Edge = FMath::Clamp(static_cast<float>(Grabbed->Edge - (InDrag.PointerLocal.Y - InDrag.GrabLocal.Y)), 0.5f, static_cast<float>(FMath::Max(FMath::Min(Half.X, Half.Y), 0.5)));
				if (Edge == Read<float>(Glass, EdgeWidthName))
				{
					return false;
				}
				Glass->SetEdgeWidth(Edge);
				return true;
			}
			if (Id == Ids::LightAngle)
			{
				const FVector2D Spoke = InDrag.GetTarget() - Centre;
				if (Spoke.IsNearlyZero())
				{
					return false;
				}
				const double Degrees = Snap(FMath::RadiansToDegrees(FMath::Atan2(Spoke.X, Spoke.Y)), InDrag.bShift);
				const float Angle = static_cast<float>(FMath::Fmod(Degrees + 360.0, 360.0));
				if (Angle == Read<float>(Glass, LightAngleName))
				{
					return false;
				}
				Glass->SetHighlight(Read<FColor>(Glass, HighlightColorName, FColor::White), Read<float>(Glass, HighlightStrengthName),
					Read<float>(Glass, HighlightWidthName), Angle);
				return true;
			}
			return false;
		}

		virtual bool EndDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag, bool bInCancelled) override
		{
			Grabbed.Reset();
			return false;
		}

	private:
		struct FGrab
		{
			TWeakObjectPtr<UDreamGlass> Glass;
			int32 HandleId = INDEX_NONE;
			/** As written, in the glass's unit mode. */
			FVector4f Radii;
			float Edge = 0.0f;
		};
		TOptional<FGrab> Grabbed;
	};
}

const FName DreamDesignerEffectHandles::GlassKindName(TEXT("GlassShape"));

FDreamDesignerHandleKindFactory DreamDesignerEffectHandles::MakeGlassKindFactory()
{
	return [](const FDreamDesignerHandleBinding& InBinding) -> TSharedPtr<IDreamDesignerHandleProvider>
	{
		const bool bCorners = Cast<UDreamGlass>(InBinding.Owner.Get()) != nullptr && InBinding.HeadProperty == InBinding.Property
			&& InBinding.Property == DreamDesignerGlassHandlesLocal::CornerRadiusName;
		if (!bCorners)
		{
			return nullptr;
		}
		return MakeShared<DreamDesignerGlassHandlesLocal::FGlassHandles>();
	};
}

#undef LOCTEXT_NAMESPACE
