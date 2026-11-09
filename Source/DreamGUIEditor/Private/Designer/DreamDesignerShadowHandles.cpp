// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamDesignerEffectHandles.h"

#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamLayerStyleTypes.h"
#include "Designer/DreamDesignerHandles.h"
#include "DreamDesignerHandleShapes.h"

#define LOCTEXT_NAMESPACE "DreamDesignerShadowHandles"

/*
 * The selection's shadows on the canvas, each a handle where it falls -- the widget's rect moved by the shadow's offset --
 * with the rect's outline there and a line from the widget's centre: a layer style's drop shadows (circles) and inner shadows
 * (diamonds), and a rect block's own outer and inner shadow. Dragging one sets its angle and its distance together, as an
 * image editor's canvas does while its style dialog is open; a style's shadow that uses the global light turns the global
 * light (DreamLayerStyleLight), so every effect that uses it turns with it. Shift snaps the angle to 15 degrees. A style that
 * follows a preset is the preset's to change, and has no handles here.
 */
namespace DreamDesignerShadowHandlesLocal
{
	using namespace DreamDesignerHandleShapes;
	namespace Ids = DreamDesignerEffectHandles::ShadowIds;

	const FName StyleName(TEXT("Style"));
	const FName OuterShadowAngleName(TEXT("OuterShadowAngle"));
	const FName OuterShadowDistanceName(TEXT("OuterShadowDistance"));
	const FName InnerShadowAngleName(TEXT("InnerShadowAngle"));
	const FName InnerShadowDistanceName(TEXT("InnerShadowDistance"));

	/** The widget's layer style, when it draws its own style. */
	UDreamLayerStyle* FindStyle(const UDreamWidget* InWidget)
	{
		UDreamLayerStyle* Style = UDreamLayerStyle::Find(InWidget);
		return Style != nullptr && Style->GetPreset() == nullptr && Style->GetStyle().bEnabled ? Style : nullptr;
	}

	UDreamRectBlock* FindBlock(const UDreamWidget* InWidget)
	{
		return InWidget != nullptr ? Cast<UDreamRectBlock>(InWidget->GetVisual()) : nullptr;
	}

	/** A rect block's shadow distance in units, and back into its unit mode. */
	double BlockDistanceScale(const UDreamWidget* InWidget, EDreamRectBlockUnitMode InMode)
	{
		return InMode == EDreamRectBlockUnitMode::Value ? 1.0 : FMath::Min(InWidget->GetWidth(), InWidget->GetHeight()) * 0.5;
	}

	/** A shadow as the handles see it: where it lies from the widget's centre, and its colour. */
	struct FShadow
	{
		int32 Id = INDEX_NONE;
		FVector2D Offset = FVector2D::ZeroVector;
		FColor Color = FColor::Black;
		bool bInner = false;
		bool bGlobal = false;
	};

	/** Whose shadows a provider shows: a layer style's (tagged on its Style), or a rect block's own two (on OuterShadowAngle). */
	enum class ESource : uint8
	{
		Style,
		Block,
	};

	void CollectShadows(const UDreamWidget* InWidget, ESource InSource, TArray<FShadow>& OutShadows)
	{
		if (const UDreamLayerStyle* Component = InSource == ESource::Style ? FindStyle(InWidget) : nullptr)
		{
			const FDreamLayerEffects& Style = Component->GetStyle();
			const float Scale = FMath::Max(Style.Scale, 0.01f);
			for (int32 Index = 0; Index < Style.DropShadows.Num(); ++Index)
			{
				const FDreamStyleDropShadow& Drop = Style.DropShadows[Index];
				if (Drop.bEnabled)
				{
					const FVector2f Offset = DreamLayerStyleLight::GetShadowOffset(DreamLayerStyleLight::GetAngle(Style, Drop.bUseGlobalLight, Drop.Angle), Drop.Distance * Scale);
					OutShadows.Add(FShadow{ Ids::FirstDropShadow + Index, FVector2D(Offset), Drop.Color, false, Drop.bUseGlobalLight });
				}
			}
			for (int32 Index = 0; Index < Style.InnerShadows.Num(); ++Index)
			{
				const FDreamStyleInnerShadow& Inner = Style.InnerShadows[Index];
				if (Inner.bEnabled)
				{
					const FVector2f Offset = DreamLayerStyleLight::GetShadowOffset(DreamLayerStyleLight::GetAngle(Style, Inner.bUseGlobalLight, Inner.Angle), Inner.Distance * Scale);
					OutShadows.Add(FShadow{ Ids::FirstInnerShadow + Index, FVector2D(Offset), Inner.Color, true, Inner.bUseGlobalLight });
				}
			}
		}
		if (const UDreamRectBlock* Block = InSource == ESource::Block ? FindBlock(InWidget) : nullptr)
		{
			// A rect block's shadow angle is a light's angle as a style's is: 0 from the right, counter-clockwise.
			if (Block->GetEnableOuterShadow())
			{
				const double Distance = Block->GetOuterShadowDistance() * BlockDistanceScale(InWidget, Block->GetOuterShadowDistanceUnitMode());
				const FVector2f Offset = DreamLayerStyleLight::GetShadowOffset(Block->GetOuterShadowAngle(), static_cast<float>(Distance));
				OutShadows.Add(FShadow{ Ids::RectBlockOuterShadow, FVector2D(Offset), Block->GetOuterShadowColor(), false, false });
			}
			if (Block->GetEnableInnerShadow())
			{
				const double Distance = Block->GetInnerShadowDistance() * BlockDistanceScale(InWidget, Block->GetInnerShadowDistanceUnitMode());
				const FVector2f Offset = DreamLayerStyleLight::GetShadowOffset(Block->GetInnerShadowAngle(), static_cast<float>(Distance));
				OutShadows.Add(FShadow{ Ids::RectBlockInnerShadow, FVector2D(Offset), Block->GetInnerShadowColor(), true, false });
			}
		}
	}

	bool IsStyleShadow(int32 InId)
	{
		return InId >= Ids::FirstDropShadow && InId < Ids::RectBlockOuterShadow;
	}

	class FShadowHandles : public IDreamDesignerHandleProvider
	{
	public:
		explicit FShadowHandles(ESource InSource) : Source(InSource) {}

		virtual FName GetName() const override
		{
			return FDreamDesignerHandles::MakeBoundName(DreamDesignerEffectHandles::ShadowKindName, Source == ESource::Style ? StyleName : OuterShadowAngleName);
		}
		virtual FText GetDisplayName() const override
		{
			return Source == ESource::Style ? LOCTEXT("StyleName", "Style Shadows") : LOCTEXT("Name", "Shadows");
		}

		virtual void CollectHandles(UDreamWidget* InWidget, FDreamDesignerHandleSet& OutSet) const override
		{
			TArray<FShadow> Shadows;
			CollectShadows(InWidget, Source, Shadows);
			if (Shadows.IsEmpty())
			{
				return;
			}
			const FVector2D Centre = InWidget->GetLocalSpaceCenter();
			const FVector2D Half(InWidget->GetWidth() * 0.5, InWidget->GetHeight() * 0.5);
			const FLinearColor Guide = FDreamDesignerHandles::GetGuideColor();
			for (const FShadow& Shadow : Shadows)
			{
				const FVector2D At = Centre + Shadow.Offset;
				OutSet.AddRoundedRect(At, Half, FVector4f::Zero(), Guide.CopyWithNewOpacity(Shadow.bInner ? 0.3f : 0.45f));
				OutSet.AddLine(Centre, At, Guide.CopyWithNewOpacity(0.6f));
				FDreamDesignerHandle& Handle = OutSet.AddHandle(Shadow.Id, At);
				Handle.Shape = Shadow.bInner ? EDreamDesignerHandleShape::Diamond : EDreamDesignerHandleShape::Circle;
				Handle.Fill = FLinearColor::FromSRGBColor(FColor(Shadow.Color.R, Shadow.Color.G, Shadow.Color.B, 255));
				Handle.Radius = 5.0f;
				// The global light's shadows move together: they are drawn on top, the first of them grabbed.
				Handle.Priority = Shadow.bGlobal ? 1 : 0;
			}
		}

		virtual void GetEditedProperties(UDreamWidget* InWidget, int32 InHandleId, TArray<FDreamDesignerEditedProperty>& OutProperties) const override
		{
			if (IsStyleShadow(InHandleId))
			{
				if (UDreamLayerStyle* Style = FindStyle(InWidget))
				{
					OutProperties.Emplace(Style, StyleName);
				}
				return;
			}
			if (UDreamRectBlock* Block = FindBlock(InWidget))
			{
				if (InHandleId == Ids::RectBlockOuterShadow)
				{
					OutProperties.Emplace(Block, OuterShadowAngleName);
					OutProperties.Emplace(Block, OuterShadowDistanceName);
				}
				else if (InHandleId == Ids::RectBlockInnerShadow)
				{
					OutProperties.Emplace(Block, InnerShadowAngleName);
					OutProperties.Emplace(Block, InnerShadowDistanceName);
				}
			}
		}

		virtual FText GetTransactionName(int32 InHandleId) const override
		{
			return LOCTEXT("MoveShadow", "Move Shadow");
		}

		virtual bool UpdateDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag) override
		{
			const FVector2D Offset = InDrag.GetTarget() - InWidget->GetLocalSpaceCenter();
			const int32 Id = InDrag.HandleId;
			if (IsStyleShadow(Id))
			{
				UDreamLayerStyle* Component = FindStyle(InWidget);
				if (Component == nullptr)
				{
					return false;
				}
				FDreamLayerEffects Style = Component->GetStyle();
				const float Scale = FMath::Max(Style.Scale, 0.01f);
				auto Place = [&InDrag, &Offset, &Style, Scale](bool bInGlobal, float& InOutAngle, float& InOutDistance)
				{
					float Angle = DreamLayerStyleLight::GetAngle(Style, bInGlobal, InOutAngle);
					float Distance = 0.0f;
					DreamLayerStyleLight::GetAngleAndDistance(FVector2f(Offset), Angle, Distance);
					DreamLayerStyleLight::SetAngle(Style, bInGlobal, InOutAngle, static_cast<float>(Snap(Angle, InDrag.bShift)));
					InOutDistance = Distance / Scale;
				};
				if (Id < Ids::FirstInnerShadow)
				{
					if (!Style.DropShadows.IsValidIndex(Id - Ids::FirstDropShadow))
					{
						return false;
					}
					FDreamStyleDropShadow& Drop = Style.DropShadows[Id - Ids::FirstDropShadow];
					Place(Drop.bUseGlobalLight, Drop.Angle, Drop.Distance);
				}
				else
				{
					if (!Style.InnerShadows.IsValidIndex(Id - Ids::FirstInnerShadow))
					{
						return false;
					}
					FDreamStyleInnerShadow& Inner = Style.InnerShadows[Id - Ids::FirstInnerShadow];
					Place(Inner.bUseGlobalLight, Inner.Angle, Inner.Distance);
				}
				if (Style == Component->GetStyle())
				{
					return false;
				}
				Component->SetStyle(Style);
				return true;
			}
			UDreamRectBlock* Block = FindBlock(InWidget);
			if (Block == nullptr || (Id != Ids::RectBlockOuterShadow && Id != Ids::RectBlockInnerShadow))
			{
				return false;
			}
			const bool bOuter = Id == Ids::RectBlockOuterShadow;
			float Angle = bOuter ? Block->GetOuterShadowAngle() : Block->GetInnerShadowAngle();
			float Distance = 0.0f;
			DreamLayerStyleLight::GetAngleAndDistance(FVector2f(Offset), Angle, Distance);
			// A rect block keeps its angle in [0, 360) and its distance in its unit mode.
			Angle = static_cast<float>(FMath::Fmod(Snap(Angle, InDrag.bShift) + 360.0, 360.0));
			const double Scale = FMath::Max(BlockDistanceScale(InWidget, bOuter ? Block->GetOuterShadowDistanceUnitMode() : Block->GetInnerShadowDistanceUnitMode()), UE_KINDA_SMALL_NUMBER);
			const float Written = static_cast<float>(Distance / Scale);
			const float OldAngle = bOuter ? Block->GetOuterShadowAngle() : Block->GetInnerShadowAngle();
			const float OldDistance = bOuter ? Block->GetOuterShadowDistance() : Block->GetInnerShadowDistance();
			if (Angle == OldAngle && Written == OldDistance)
			{
				return false;
			}
			if (bOuter)
			{
				Block->SetOuterShadowAngle(Angle);
				Block->SetOuterShadowDistance(Written);
			}
			else
			{
				Block->SetInnerShadowAngle(Angle);
				Block->SetInnerShadowDistance(Written);
			}
			return true;
		}

	private:
		ESource Source;
	};
}

const FName DreamDesignerEffectHandles::ShadowKindName(TEXT("Shadow"));

FDreamDesignerHandleKindFactory DreamDesignerEffectHandles::MakeShadowKindFactory()
{
	return [](const FDreamDesignerHandleBinding& InBinding) -> TSharedPtr<IDreamDesignerHandleProvider>
	{
		using namespace DreamDesignerShadowHandlesLocal;
		const UObject* Owner = InBinding.Owner.Get();
		if (InBinding.HeadProperty != InBinding.Property)
		{
			return nullptr;
		}
		if (Cast<UDreamLayerStyle>(Owner) != nullptr && InBinding.Property == StyleName)
		{
			return MakeShared<FShadowHandles>(ESource::Style);
		}
		if (Cast<UDreamRectBlock>(Owner) != nullptr && InBinding.Property == OuterShadowAngleName)
		{
			return MakeShared<FShadowHandles>(ESource::Block);
		}
		return nullptr;
	};
}

#undef LOCTEXT_NAMESPACE
