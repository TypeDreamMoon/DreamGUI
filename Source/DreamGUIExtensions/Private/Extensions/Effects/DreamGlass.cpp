// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/Effects/DreamGlass.h"

#include "Core/Components/DreamShapeBlendGroup.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIClipMask.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Event/DreamPointerEventData.h"

static_assert(DreamUIGlassShapeLayout::MaxTexels == DreamUIShapeBlendLayout::MaxTexels, "The renderer's rows for glass's melted shape are a shape blend's record");

namespace DreamGlassLocal
{
	/** The melted shape's bounds are its members' grown by the smoothness; this much more takes in its edge's antialiasing. */
	constexpr float EdgeRoom = 2.0f;
}

UDreamGlass::UDreamGlass(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void UDreamGlass::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ListenForShapeBlend();
	SendGlassToRenderProxy();
}
#endif

void UDreamGlass::OnRegister()
{
	Super::OnRegister();
	ListenForShapeBlend();
}

void UDreamGlass::OnUnregister()
{
	StopListeningForShapeBlend();
	Super::OnUnregister();
}

UDreamShapeBlendGroup* UDreamGlass::FindShapeBlendGroup() const
{
	if (Shape != EDreamGlassShape::ShapeBlend)
	{
		return nullptr;
	}
	const UDreamWidget* Source = ShapeBlendGroup.IsValid() ? ShapeBlendGroup.Get() : GetWidget();
	UDreamShapeBlendGroup* Group = Source != nullptr ? Source->GetComponent<UDreamShapeBlendGroup>() : nullptr;
	return Group != nullptr && Group->IsActive() ? Group : nullptr;
}

void UDreamGlass::ListenForShapeBlend()
{
	UDreamUIManagerWorldSubsystem* Manager = Shape == EDreamGlassShape::ShapeBlend && GetWidget() != nullptr ? UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()) : nullptr;
	if (ListenedManager.Get() != Manager)
	{
		StopListeningForShapeBlend();
		if (Manager != nullptr)
		{
			// The group's members move by layout and by their own animation, and the glass's frame moves on its own: the shape is
			// made again every frame, after layout, and sent only when it changed.
			ListenedManager = Manager;
			BeforeCanvasesUpdateHandle = Manager->GetOnBeforeRootCanvasesUpdate().AddWeakLambda(this, [this]() { RefreshShapeBlend(); });
		}
	}
	RefreshShapeBlend();
}

void UDreamGlass::StopListeningForShapeBlend()
{
	if (UDreamUIManagerWorldSubsystem* Manager = ListenedManager.Get())
	{
		Manager->GetOnBeforeRootCanvasesUpdate().Remove(BeforeCanvasesUpdateHandle);
	}
	ListenedManager.Reset();
	BeforeCanvasesUpdateHandle.Reset();
}

void UDreamGlass::RefreshShapeBlend()
{
	FDreamUIShapeBlendShape NewShape;
	FBox2f NewBounds(ForceInit);
	if (const UDreamShapeBlendGroup* Group = FindShapeBlendGroup())
	{
		Group->MakeShapeInFrame(GetWidget(), NewShape, &NewBounds);
		if (NewShape.IsEmpty() || !NewBounds.bIsValid)
		{
			NewShape.Texels.Reset();
			NewBounds = FBox2f(ForceInit);
		}
		else
		{
			NewBounds = NewBounds.ExpandBy(DreamGlassLocal::EdgeRoom);
		}
	}
	const bool bBoundsChanged = NewBounds.bIsValid != DrawnShapeBounds.bIsValid || (NewBounds.bIsValid && !(NewBounds == DrawnShapeBounds));
	if (!bBoundsChanged && NewShape == DrawnShape)
	{
		return;
	}
	DrawnShape = MoveTemp(NewShape);
	DrawnShapeBounds = NewBounds;
	if (bBoundsChanged && GetWidget() != nullptr)
	{
		// The quad is drawn over the shape, wherever it reaches: its corners, their UVs and the bounds batching reads.
		MarkVertexPositionDirty();
		MarkUVDirty();
	}
	SendGlassToRenderProxy();
}

void UDreamGlass::GetGeometryBoundsInLocalSpace(FVector2D& OutMinPoint, FVector2D& OutMaxPoint) const
{
	const UDreamWidget* Widget = GetWidget();
	if (!DrawsShapeBlend() || Widget == nullptr || bUseFullSize)
	{
		Super::GetGeometryBoundsInLocalSpace(OutMinPoint, OutMaxPoint);
		return;
	}
	// The shape's bounds, from its frame (the rect's centre, +Y down) into the widget's local space (+Y up).
	const FVector2D Centre = Widget->GetLocalSpaceCenter();
	OutMinPoint = FVector2D(Centre.X + DrawnShapeBounds.Min.X, Centre.Y - DrawnShapeBounds.Max.Y);
	OutMaxPoint = FVector2D(Centre.X + DrawnShapeBounds.Max.X, Centre.Y - DrawnShapeBounds.Min.Y);
}

void UDreamGlass::OnUpdateGeometry(bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	Super::OnUpdateGeometry(InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
	const UDreamWidget* Widget = GetWidget();
	if (!DrawsShapeBlend() || Widget == nullptr || bUseFullSize || !(InVertexPositionChanged || InVertexUVChanged))
	{
		return;
	}
	// The quad over the melted shape's bounds, its UVs still the rect's: 0..1 across the rect, so that the shader's point,
	// (UV - 0.5) times the rect's size, is the shape's frame wherever the quad reaches.
	TArray<FDreamUIOriginVertexData>& OriginVertices = Geometry->OriginVertices;
	TArray<FDreamUIMeshVertex>& Vertices = Geometry->Vertices;
	if (OriginVertices.Num() < 4 || Vertices.Num() < 4)
	{
		return;
	}
	const FVector2D Centre = Widget->GetLocalSpaceCenter();
	const float Width = FMath::Max(Widget->GetWidth(), 1e-3f);
	const float Height = FMath::Max(Widget->GetHeight(), 1e-3f);
	// In the base's order: bottom left, bottom right, top left, top right; in the shape's frame bottom is +Y.
	const FVector2f Corners[4] = {
		FVector2f(DrawnShapeBounds.Min.X, DrawnShapeBounds.Max.Y), FVector2f(DrawnShapeBounds.Max.X, DrawnShapeBounds.Max.Y),
		FVector2f(DrawnShapeBounds.Min.X, DrawnShapeBounds.Min.Y), FVector2f(DrawnShapeBounds.Max.X, DrawnShapeBounds.Min.Y) };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector2f& P = Corners[Index];
		OriginVertices[Index].Position = FVector3f(0.0f, static_cast<float>(Centre.X) + P.X, static_cast<float>(Centre.Y) - P.Y);
		Vertices[Index].TextureCoordinate[0] = FVector2f(P.X / Width + 0.5f, P.Y / Height + 0.5f);
	}
}

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
	// The melted shape while there is one; the rounded rect while the group is off or has nothing to melt.
	if (DrawsShapeBlend())
	{
		Params.ShapeKind = static_cast<int32>(EDreamGlassShape::ShapeBlend);
		Params.ShapeTexels = DrawnShape.Texels;
	}
	else
	{
		Params.ShapeKind = static_cast<int32>(EDreamGlassShape::RoundedRect);
	}
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
	// Measured as the shader measures it: +Y down the element.
	const FVector2f P(static_cast<float>(InFromCentre.X), static_cast<float>(-InFromCentre.Y));
	if (const UDreamShapeBlendGroup* Group = FindShapeBlendGroup())
	{
		// The melted shape as the group has it now, in this widget's frame.
		FDreamUIShapeBlendShape Melted;
		Group->MakeShapeInFrame(Widget, Melted);
		if (!Melted.IsEmpty())
		{
			return FDreamUIShapeBlendMath::Distance(Melted.Texels.GetData(), P) <= 0.0f;
		}
	}
	// A rect block's corners.
	const FVector2f HalfSize(Widget->GetWidth() * 0.5f, Widget->GetHeight() * 0.5f);
	return FDreamUIClipMaskMath::SdRoundRect(P, HalfSize, GetResolvedCornerRadius()) <= 0.0f;
}

bool UDreamGlass::GetHitGeometryFitsWidgetRect() const
{
	// A melted shape reaches as far as its members do, past the widget's rect.
	return Shape != EDreamGlassShape::ShapeBlend && Super::GetHitGeometryFitsWidgetRect();
}

bool UDreamGlass::LineTraceShapeBlend(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End) const
{
	// The widget's plane as UDreamVisual::LineTraceUIRect meets it, perspective included, but anywhere on it.
	UDreamWidget* Widget = GetWidget();
	const bool bPerspective = Widget->HasPerspectiveApplied();
	const FMatrix WidgetToWorld = bPerspective ? Widget->GetWorldMatrix() : FMatrix::Identity;
	const FMatrix WorldToWidget = bPerspective ? WidgetToWorld.Inverse() : FMatrix::Identity;
	const FTransform InverseTransform = Widget->GetWorldTransform().Inverse();
	const FVector LocalStart = bPerspective ? FVector(WorldToWidget.TransformPosition(Start)) : InverseTransform.TransformPosition(Start);
	const FVector LocalEnd = bPerspective ? FVector(WorldToWidget.TransformPosition(End)) : InverseTransform.TransformPosition(End);
	if (FMath::Sign(LocalStart.X) == FMath::Sign(LocalEnd.X))
	{
		return false;
	}
	const FVector Local = FMath::LinePlaneIntersection(LocalStart, LocalEnd, FVector::ZeroVector, FVector(1.0, 0.0, 0.0));
	const FVector2D Centre = Widget->GetLocalSpaceCenter();
	if (!IsPointOnGlass(FVector2D(Local.Y - Centre.X, Local.Z - Centre.Y)))
	{
		return false;
	}
	OutHit.TraceStart = Start;
	OutHit.TraceEnd = End;
	OutHit.Widget = Widget;
	OutHit.Location = bPerspective ? FVector(WidgetToWorld.TransformPosition(Local)) : Widget->GetWorldTransform().TransformPosition(Local);
	OutHit.Normal = (bPerspective ? FVector(WidgetToWorld.TransformVector(FVector(1.0, 0.0, 0.0))) : Widget->GetWorldTransform().TransformVector(FVector(1.0, 0.0, 0.0))).GetSafeNormal();
	OutHit.Distance = FVector::Distance(Start, OutHit.Location);
	OutHit.ImpactPoint = OutHit.Location;
	return true;
}

bool UDreamGlass::LineTraceUI(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End) const
{
	if (RaycastType == EDreamVisualRaycastType::Rect && FindShapeBlendGroup() != nullptr)
	{
		return LineTraceShapeBlend(OutHit, Start, End);
	}
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

void UDreamGlass::SetShape(EDreamGlassShape Value)
{
	if (Shape != Value)
	{
		Shape = Value;
		ListenForShapeBlend();
		SendGlassToRenderProxy();
	}
}

void UDreamGlass::SetShapeBlendGroup(UDreamWidget* Value)
{
	if (ShapeBlendGroup.Get() != Value)
	{
		ShapeBlendGroup = Value;
		RefreshShapeBlend();
	}
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
