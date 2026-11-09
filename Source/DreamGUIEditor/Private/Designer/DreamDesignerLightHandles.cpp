// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamDesignerEffectHandles.h"

#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamLayerStyleTypes.h"
#include "Designer/DreamDesignerHandles.h"
#include "DreamDesignerHandleShapes.h"
#include "Extensions/Effects/DreamPointerLight.h"
#include "Workflow/DreamUILayerStyleDialog.h"

#define LOCTEXT_NAMESPACE "DreamDesignerLightHandles"

/*
 * The selection's lights on the canvas, two kinds of provider:
 *  - a pointer light (its LightPosition is tagged): where it stands -- in the designer, with no pointer to follow, a light
 *    stands at LightPosition whether it follows the pointer or not -- and the circle it reaches, with a handle on its rim;
 *  - a layer style's global light (its Style.GlobalLightAngle is tagged), while an effect of the style uses it: a disc about
 *    the widget's centre, an image editor's global light picker -- the handle's direction from the centre is where the light
 *    comes from, and its distance in from the rim its altitude (the rim 0, the centre 90). Shift snaps the angle to 15
 *    degrees; Alt keeps the altitude.
 */
namespace DreamDesignerLightHandlesLocal
{
	using namespace DreamDesignerHandleShapes;
	namespace Ids = DreamDesignerEffectHandles::LightIds;

	const FName StyleName(TEXT("Style"));
	const FName GlobalLightAngleName(TEXT("GlobalLightAngle"));
	const FName LightPositionName(TEXT("LightPosition"));
	const FName RadiusName(TEXT("Radius"));
	const FName LightColorName(TEXT("LightColor"));
	/** The global light's disc, in pixels across its radius. */
	constexpr double DiscPixels = 56.0;

	/** The widget's layer style, when it draws its own and something in it takes the global light. */
	/** The style a widget's light handles show and edit: the Layer Style dialog's working copy while it edits the widget. */
	const FDreamLayerEffects& StyleShown(const UDreamWidget* InWidget, const UDreamLayerStyle& InComponent)
	{
		const TSharedPtr<FDreamUILayerStyleSession> Dialog = FDreamUILayerStyleSession::FindEditing(InWidget);
		return Dialog.IsValid() ? Dialog->GetStyle() : InComponent.GetStyle();
	}

	UDreamLayerStyle* FindGloballyLitStyle(const UDreamWidget* InWidget)
	{
		UDreamLayerStyle* Component = UDreamLayerStyle::Find(InWidget);
		if (Component == nullptr || Component->GetPreset() != nullptr || !StyleShown(InWidget, *Component).bEnabled)
		{
			return nullptr;
		}
		const FDreamLayerEffects& Style = StyleShown(InWidget, *Component);
		const bool bUses = Style.DropShadows.ContainsByPredicate([](const FDreamStyleDropShadow& S) { return S.bEnabled && S.bUseGlobalLight; })
			|| Style.InnerShadows.ContainsByPredicate([](const FDreamStyleInnerShadow& S) { return S.bEnabled && S.bUseGlobalLight; })
			|| (Style.BevelEmboss.bEnabled && Style.BevelEmboss.bUseGlobalLight);
		return bUses ? Component : nullptr;
	}

	/** One pointer light: its place and its reach. */
	class FPointerLightHandles : public IDreamDesignerHandleProvider
	{
	public:
		explicit FPointerLightHandles(UDreamPointerLight* InLight) : Light(InLight) {}

		virtual FName GetName() const override { return FDreamDesignerHandles::MakeBoundName(DreamDesignerEffectHandles::LightKindName, LightPositionName); }
		virtual FText GetDisplayName() const override { return LOCTEXT("PointerLight", "Pointer Light"); }

		virtual void CollectHandles(UDreamWidget* InWidget, FDreamDesignerHandleSet& OutSet) const override
		{
			const UDreamPointerLight* Found = Get(InWidget);
			if (Found == nullptr)
			{
				return;
			}
			const FVector2D At = InWidget->GetLocalSpaceCenter() + Found->GetLightPosition();
			const double Reach = FMath::Max(Read<float>(Found, RadiusName, 1.0f), 1.0f);
			OutSet.AddEllipse(At, FVector2D(Reach, Reach), 0.0f, FDreamDesignerHandles::GetGuideColor().CopyWithNewOpacity(0.5f), 64);
			FDreamDesignerHandle& Place = OutSet.AddHandle(Ids::PointerLightPlace, At);
			Place.Shape = EDreamDesignerHandleShape::Circle;
			Place.Fill = FLinearColor::FromSRGBColor(Read<FColor>(Found, LightColorName, FColor::White)).CopyWithNewOpacity(1.0f);
			Place.Radius = 6.0f;
			Place.Priority = 1;
			FDreamDesignerHandle& Rim = OutSet.AddHandle(Ids::PointerLightReach, At + FVector2D(Reach, 0.0));
			Rim.Radius = 3.5f;
		}

		virtual void GetEditedProperties(UDreamWidget* InWidget, int32 InHandleId, TArray<FDreamDesignerEditedProperty>& OutProperties) const override
		{
			if (UDreamPointerLight* Found = Get(InWidget))
			{
				if (InHandleId == Ids::PointerLightPlace || InHandleId == Ids::PointerLightReach)
				{
					OutProperties.Emplace(Found, InHandleId == Ids::PointerLightPlace ? LightPositionName : RadiusName);
				}
			}
		}

		virtual FText GetTransactionName(int32 InHandleId) const override
		{
			return InHandleId == Ids::PointerLightPlace ? LOCTEXT("MoveLight", "Move Pointer Light") : LOCTEXT("LightReach", "Change Pointer Light Reach");
		}

		virtual bool UpdateDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag) override
		{
			UDreamPointerLight* Found = Get(InWidget);
			if (Found == nullptr)
			{
				return false;
			}
			const FVector2D Centre = InWidget->GetLocalSpaceCenter();
			if (InDrag.HandleId == Ids::PointerLightPlace)
			{
				const FVector2D Place = InDrag.GetTarget() - Centre;
				if (Place.Equals(Found->GetLightPosition()))
				{
					return false;
				}
				Found->SetLightPosition(Place);
				return true;
			}
			if (InDrag.HandleId == Ids::PointerLightReach)
			{
				const float Reach = FMath::Max(static_cast<float>(FVector2D::Distance(InDrag.GetTarget(), Centre + Found->GetLightPosition())), 1.0f);
				if (Reach == Read<float>(Found, RadiusName))
				{
					return false;
				}
				Found->SetRadius(Reach);
				return true;
			}
			return false;
		}

	private:
		TWeakObjectPtr<UDreamPointerLight> Light;

		/** The light, while it is on and InWidget's. */
		UDreamPointerLight* Get(const UDreamWidget* InWidget) const
		{
			UDreamPointerLight* Found = Light.Get();
			return Found != nullptr && Found->IsEnabled() && Found->GetWidget() == InWidget ? Found : nullptr;
		}
	};

	/** A layer style's global light, as a disc. */
	class FGlobalLightHandles : public IDreamDesignerHandleProvider
	{
	public:
		virtual FName GetName() const override { return FDreamDesignerHandles::MakeBoundName(DreamDesignerEffectHandles::LightKindName, GlobalLightAngleName); }
		virtual FText GetDisplayName() const override { return LOCTEXT("GlobalLight", "Global Light"); }

		virtual void CollectHandles(UDreamWidget* InWidget, FDreamDesignerHandleSet& OutSet) const override
		{
			const UDreamLayerStyle* Component = FindGloballyLitStyle(InWidget);
			if (Component == nullptr)
			{
				return;
			}
			const FDreamLayerEffects& Style = StyleShown(InWidget, *Component);
			const FVector2D Centre = InWidget->GetLocalSpaceCenter();
			const FLinearColor Guide = FDreamDesignerHandles::GetGuideColor();
			const double Disc = DiscPixels / FMath::Max(OutSet.PixelsPerUnit, UE_KINDA_SMALL_NUMBER);
			const double Angle = FMath::DegreesToRadians(static_cast<double>(Style.GlobalLightAngle));
			const double In = 1.0 - FMath::Clamp(Style.GlobalLightAltitude, 0.0f, 90.0f) / 90.0;
			const FVector2D At = Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * (Disc * In);
			OutSet.AddEllipse(Centre, FVector2D(Disc, Disc), 0.0f, Guide.CopyWithNewOpacity(0.5f), 48);
			OutSet.AddLine(Centre, At, Guide.CopyWithNewOpacity(0.7f));
			FDreamDesignerHandle& Handle = OutSet.AddHandle(Ids::GlobalLight, At);
			Handle.Shape = EDreamDesignerHandleShape::Circle;
			Handle.Fill = FLinearColor(1.0f, 0.92f, 0.55f, 1.0f);
			Handle.Radius = 5.0f;
		}

		virtual void GetEditedProperties(UDreamWidget* InWidget, int32 InHandleId, TArray<FDreamDesignerEditedProperty>& OutProperties) const override
		{
			UDreamLayerStyle* Component = FindGloballyLitStyle(InWidget);
			if (Component != nullptr && InHandleId == Ids::GlobalLight)
			{
				OutProperties.Emplace(Component, StyleName);
			}
		}

		virtual FText GetTransactionName(int32 InHandleId) const override
		{
			return LOCTEXT("MoveGlobalLight", "Move Global Light");
		}

		virtual bool UpdateDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag) override
		{
			UDreamLayerStyle* Component = FindGloballyLitStyle(InWidget);
			if (Component == nullptr || InDrag.HandleId != Ids::GlobalLight)
			{
				return false;
			}
			const TSharedPtr<FDreamUILayerStyleSession> Dialog = FDreamUILayerStyleSession::FindEditing(InWidget);
			FDreamLayerEffects Style = Dialog.IsValid() ? Dialog->GetStyle() : Component->GetStyle();
			const FVector2D Spoke = InDrag.GetTarget() - InWidget->GetLocalSpaceCenter();
			const double Disc = DiscPixels / FMath::Max(InDrag.PixelsPerUnit, UE_KINDA_SMALL_NUMBER);
			float Unused = 0.0f;
			if (!Spoke.IsNearlyZero())
			{
				const double Degrees = Snap(FMath::RadiansToDegrees(FMath::Atan2(Spoke.Y, Spoke.X)), InDrag.bShift);
				DreamLayerStyleLight::SetAngle(Style, /*bInUseGlobalLight*/ true, Unused, static_cast<float>(Degrees));
			}
			if (!InDrag.bAlt)
			{
				const double In = FMath::Clamp(Spoke.Size() / FMath::Max(Disc, UE_KINDA_SMALL_NUMBER), 0.0, 1.0);
				DreamLayerStyleLight::SetAltitude(Style, /*bInUseGlobalLight*/ true, Unused, static_cast<float>(90.0 * (1.0 - In)));
			}
			if (Dialog.IsValid())
			{
				// The dialog's to keep or put back: nothing here for the viewport to write.
				if (!(Style == Dialog->GetStyle()))
				{
					Dialog->TryStyle(Style);
				}
				return false;
			}
			if (Style == Component->GetStyle())
			{
				return false;
			}
			Component->SetStyle(Style);
			return true;
		}

		virtual bool EndDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag, bool bInCancelled) override
		{
			if (const TSharedPtr<FDreamUILayerStyleSession> Dialog = FDreamUILayerStyleSession::FindEditing(InWidget))
			{
				Dialog->EndTry(!bInCancelled);
			}
			return false;
		}
	};
}

const FName DreamDesignerEffectHandles::LightKindName(TEXT("Light"));

FDreamDesignerHandleKindFactory DreamDesignerEffectHandles::MakeLightKindFactory()
{
	return [](const FDreamDesignerHandleBinding& InBinding) -> TSharedPtr<IDreamDesignerHandleProvider>
	{
		using namespace DreamDesignerLightHandlesLocal;
		UObject* Owner = InBinding.Owner.Get();
		if (UDreamPointerLight* Light = Cast<UDreamPointerLight>(Owner))
		{
			if (InBinding.Property == LightPositionName)
			{
				return MakeShared<FPointerLightHandles>(Light);
			}
			return nullptr;
		}
		if (Cast<UDreamLayerStyle>(Owner) != nullptr && InBinding.HeadProperty == StyleName && InBinding.Property == GlobalLightAngleName)
		{
			return MakeShared<FGlobalLightHandles>();
		}
		return nullptr;
	};
}

#undef LOCTEXT_NAMESPACE
