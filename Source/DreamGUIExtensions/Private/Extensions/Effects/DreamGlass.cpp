// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/Effects/DreamGlass.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIClipMask.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Event/DreamPointerEventData.h"

UDreamGlass::UDreamGlass(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void UDreamGlass::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SendGlassToRenderProxy();
}
#endif

void UDreamGlass::MarkAllDirty()
{
	Super::MarkAllDirty();
	SendGlassToRenderProxy();
}

void UDreamGlass::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
	Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	// Corners given as a fraction of the size follow it.
	SendGlassToRenderProxy();
}

void UDreamGlass::SendRegionVertexDataToRenderProxy()
{
	Super::SendRegionVertexDataToRenderProxy();
	// Its alpha fades it: sent with the rest whenever its colour, its rect or where it is changes.
	SendGlassToRenderProxy();
}

FVector4f UDreamGlass::GetResolvedCornerRadius() const
{
	const UDreamWidget* Widget = GetWidget();
	if (CornerRadiusUnitMode == EDreamRectBlockUnitMode::Value || Widget == nullptr)
	{
		return CornerRadius;
	}
	// A rect block's percentage: of half the smaller side.
	return CornerRadius * FMath::Min(Widget->GetWidth(), Widget->GetHeight()) * 0.5f;
}

FDreamUIGlassParams UDreamGlass::MakeParams() const
{
	FDreamUIGlassParams Params;
	Params.ShapeKind = static_cast<int32>(Shape);
	Params.CornerRadii = GetResolvedCornerRadius();
	Params.EdgeWidth = FMath::Max(EdgeWidth, 0.5f);
	Params.Refraction = Refraction;
	Params.Profile = static_cast<int32>(Profile);
	Params.Dispersion = FMath::Max(Dispersion, 0.0f);
	Params.Frost = FMath::Clamp(Frost, 0.0f, 1.0f);
	Params.Tint = FVector4f(GlassTint.R / 255.0f, GlassTint.G / 255.0f, GlassTint.B / 255.0f, FMath::Clamp(GlassTintStrength, 0.0f, 1.0f));
	Params.Saturation = FMath::Max(Saturation, 0.0f);
	Params.Brightness = Brightness;
	Params.HighlightColor = FVector4f(HighlightColor.R / 255.0f, HighlightColor.G / 255.0f, HighlightColor.B / 255.0f, FMath::Clamp(HighlightStrength, 0.0f, 1.0f));
	Params.HighlightWidth = FMath::Max(HighlightWidth, 0.0f);
	Params.LightAngle = LightAngle;
	Params.ShadowWidth = FMath::Max(ShadowWidth, 0.0f);
	Params.ShadowStrength = FMath::Clamp(ShadowStrength, 0.0f, 1.0f);
	Params.Opacity = GetWidget() != nullptr ? GetFinalAlpha01() : 1.0f;
	return Params;
}

void UDreamGlass::SendGlassToRenderProxy()
{
	if (RenderProxy.IsValid() && !HasAnyFlags(RF_ClassDefaultObject))
	{
		DreamUIPostProcessEffects::SetGlass_GameThread(RenderProxy, MakeParams());
	}
}

FDreamVisualPostProcessRenderProxyPtr UDreamGlass::GetRenderProxy()
{
	if (!RenderProxy.IsValid())
	{
		RenderProxy = DreamUIPostProcessEffects::CreateGlassProxy();
		SendRegionVertexDataToRenderProxy();
		SendMaskTextureToRenderProxy();
		SendRenderTargetToRenderProxy();
	}
	return RenderProxy;
}

bool UDreamGlass::IsPointOnGlass(const FVector2D& InFromCentre) const
{
	const UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr)
	{
		return false;
	}
	// Measured as the shader measures it: +Y down the element, a rect block's corners.
	const FVector2f HalfSize(Widget->GetWidth() * 0.5f, Widget->GetHeight() * 0.5f);
	return FDreamUIClipMaskMath::SdRoundRect(FVector2f(static_cast<float>(InFromCentre.X), static_cast<float>(-InFromCentre.Y)), HalfSize, GetResolvedCornerRadius()) <= 0.0f;
}

bool UDreamGlass::LineTraceUI(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End) const
{
	if (!Super::LineTraceUI(OutHit, Start, End))
	{
		return false;
	}
	if (RaycastType != EDreamVisualRaycastType::Rect)
	{
		return true;
	}
	// Inside the rect: on the glass only where its rounded corners leave it.
	const UDreamWidget* Widget = GetWidget();
	const FVector Local = Widget->GetWorldTransform().InverseTransformPosition(OutHit.Location);
	const FVector2D Centre = Widget->GetLocalSpaceCenter();
	return IsPointOnGlass(FVector2D(Local.Y - Centre.X, Local.Z - Centre.Y));
}

void UDreamGlass::SetCornerRadius(const FVector4f& Value)
{
	CornerRadius = Value;
	SendGlassToRenderProxy();
}

void UDreamGlass::SetCornerRadiusUnitMode(EDreamRectBlockUnitMode Value)
{
	CornerRadiusUnitMode = Value;
	SendGlassToRenderProxy();
}

void UDreamGlass::SetEdgeWidth(float Value)
{
	EdgeWidth = FMath::Max(Value, 0.5f);
	SendGlassToRenderProxy();
}

void UDreamGlass::SetRefraction(float Value)
{
	Refraction = Value;
	SendGlassToRenderProxy();
	// How far it bends is how much room a copy of what is behind needs.
	if (UDreamWidget* Widget = GetWidget())
	{
		Widget->MarkCanvasUpdate(false);
	}
}

void UDreamGlass::SetProfile(EDreamGlassProfile Value)
{
	Profile = Value;
	SendGlassToRenderProxy();
}

void UDreamGlass::SetDispersion(float Value)
{
	Dispersion = FMath::Max(Value, 0.0f);
	SendGlassToRenderProxy();
}

void UDreamGlass::SetFrost(float Value)
{
	Frost = FMath::Clamp(Value, 0.0f, 1.0f);
	SendGlassToRenderProxy();
}

void UDreamGlass::SetGlassTint(FColor InColor, float InStrength)
{
	GlassTint = InColor;
	GlassTintStrength = FMath::Clamp(InStrength, 0.0f, 1.0f);
	SendGlassToRenderProxy();
}

void UDreamGlass::SetSaturation(float Value)
{
	Saturation = FMath::Max(Value, 0.0f);
	SendGlassToRenderProxy();
}

void UDreamGlass::SetBrightness(float Value)
{
	Brightness = Value;
	SendGlassToRenderProxy();
}

void UDreamGlass::SetHighlight(FColor InColor, float InStrength, float InWidth, float InLightAngle)
{
	HighlightColor = InColor;
	HighlightStrength = FMath::Clamp(InStrength, 0.0f, 1.0f);
	HighlightWidth = FMath::Max(InWidth, 0.0f);
	LightAngle = InLightAngle;
	SendGlassToRenderProxy();
}

void UDreamGlass::SetInnerShadow(float InWidth, float InStrength)
{
	ShadowWidth = FMath::Max(InWidth, 0.0f);
	ShadowStrength = FMath::Clamp(InStrength, 0.0f, 1.0f);
	SendGlassToRenderProxy();
}

DECLARE_DREAM_GUI_VISUAL("Glass", UDreamGlass)
