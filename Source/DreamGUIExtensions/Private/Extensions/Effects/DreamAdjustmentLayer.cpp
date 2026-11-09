// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/Effects/DreamAdjustmentLayer.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "DreamUIRender/DreamUIBackdropEffects.h"

UDreamAdjustmentLayer::UDreamAdjustmentLayer(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

void UDreamAdjustmentLayer::PostLoad()
{
	Super::PostLoad();
	RefreshEffect();
}

#if WITH_EDITOR
void UDreamAdjustmentLayer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshEffect();
	// Its target decides whether it draws over what is behind or makes its widget a group: the canvas makes its draws again.
	if (UDreamWidget* Widget = GetWidget())
	{
		Widget->MarkCanvasUpdate(true);
	}
}
#endif

void UDreamAdjustmentLayer::OnEnabledChanged()
{
	Super::OnEnabledChanged();
	RefreshEffect();
}

void UDreamAdjustmentLayer::MarkAllDirty()
{
	Super::MarkAllDirty();
	RefreshEffect();
}

void UDreamAdjustmentLayer::SetAdjustments(const TArray<FDreamAdjustment>& Value)
{
	Adjustments = Value;
	RefreshEffect();
}

void UDreamAdjustmentLayer::SetOpacity(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	if (Opacity != Value)
	{
		Opacity = Value;
		RefreshEffect();
	}
}

void UDreamAdjustmentLayer::SetEffectBlendMode(EDreamUIBlendMode Value)
{
	if (EffectBlendMode != Value)
	{
		EffectBlendMode = Value;
		RefreshEffect();
	}
}

void UDreamAdjustmentLayer::SetTarget(EDreamEffectTarget Value)
{
	if (Target != Value)
	{
		Target = Value;
		RefreshEffect();
		MarkVertexPositionDirty();
	}
}

namespace DreamAdjustmentLayerLocal
{
	/** The visual's alpha, its widgets' opacity in it; 1 before it is on a widget. */
	float AlphaOf(const UDreamVisual& InVisual)
	{
		return InVisual.GetWidget() != nullptr ? InVisual.GetFinalAlpha01() : 1.0f;
	}
}

void UDreamAdjustmentLayer::PackAdjustments(FDreamUIAdjustPacked& OutPacked) const
{
	FDreamUIAdjustMath::Pack(Adjustments, Opacity * DreamAdjustmentLayerLocal::AlphaOf(*this), EffectBlendMode, OutPacked);
}

void UDreamAdjustmentLayer::RefreshEffect()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}
	PackedAlpha = DreamAdjustmentLayerLocal::AlphaOf(*this);
	FDreamUIAdjustPacked Packed;
	PackAdjustments(Packed);
	Effect = Packed.NumOps > 0 ? DreamUIPostProcessEffects::MakeAdjustEffect(MoveTemp(Packed)) : FDreamUIImageEffectRef();
	const bool bChildren = Target == EDreamEffectTarget::Children;
	if (RenderProxy.IsValid())
	{
		DreamUIPostProcessEffects::SetBackdropEffect_GameThread(RenderProxy, bChildren ? FDreamUIImageEffectRef() : Effect);
	}
	const bool bDrawsChildren = bChildren && Effect.IsValid() && bEnabled;
	if (bDrawsChildrenThroughEffect != bDrawsChildren)
	{
		bDrawsChildrenThroughEffect = bDrawsChildren;
		if (UDreamWidget* Widget = GetWidget())
		{
			Widget->MarkCanvasUpdate(true);
		}
	}
}

void UDreamAdjustmentLayer::SendRegionVertexDataToRenderProxy()
{
	Super::SendRegionVertexDataToRenderProxy();
	// Its alpha (its colour's, its widgets' opacity) scales its opacity: made again when that changed.
	if (!FMath::IsNearlyEqual(PackedAlpha, DreamAdjustmentLayerLocal::AlphaOf(*this)))
	{
		RefreshEffect();
	}
}

FDreamVisualPostProcessRenderProxyPtr UDreamAdjustmentLayer::GetRenderProxy()
{
	if (!RenderProxy.IsValid())
	{
		RenderProxy = DreamUIPostProcessEffects::CreateBackdropEffectProxy();
		SendRegionVertexDataToRenderProxy();
		SendMaskTextureToRenderProxy();
		SendRenderTargetToRenderProxy();
		RefreshEffect();
	}
	return RenderProxy;
}

bool UDreamAdjustmentLayer::HaveValidData() const
{
	// Working on its children, it draws nothing of its own: its widget's group puts the effect down.
	return Target == EDreamEffectTarget::Backdrop && Super::HaveValidData();
}

FDreamUIImageEffectRef UDreamAdjustmentLayer::GetChildrenEffect() const
{
	return Target == EDreamEffectTarget::Children && bEnabled ? Effect : FDreamUIImageEffectRef();
}

DECLARE_DREAM_GUI_VISUAL("AdjustmentLayer", UDreamAdjustmentLayer)
