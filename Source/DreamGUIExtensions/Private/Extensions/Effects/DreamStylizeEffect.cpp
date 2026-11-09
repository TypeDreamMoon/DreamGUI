// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/Effects/DreamStylizeEffect.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "DreamUIRender/DreamUIBackdropEffects.h"
#include "Engine/World.h"

namespace DreamStylizeEffectLocal
{
	float AlphaOf(const UDreamVisual& InVisual)
	{
		return InVisual.GetWidget() != nullptr ? InVisual.GetFinalAlpha01() : 1.0f;
	}
}

UDreamStylizeEffect::UDreamStylizeEffect(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

void UDreamStylizeEffect::PostLoad()
{
	Super::PostLoad();
	RefreshEffect();
}

#if WITH_EDITOR
void UDreamStylizeEffect::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshEffect();
	if (UDreamWidget* Widget = GetWidget())
	{
		Widget->MarkCanvasUpdate(true);
	}
}
#endif

void UDreamStylizeEffect::OnEnabledChanged()
{
	Super::OnEnabledChanged();
	RefreshEffect();
}

void UDreamStylizeEffect::MarkAllDirty()
{
	Super::MarkAllDirty();
	RefreshEffect();
}

void UDreamStylizeEffect::SetLayers(const TArray<FDreamStylizeLayer>& Value)
{
	Layers = Value;
	RefreshEffect();
	// How far the layers read can change what a copy of what is behind must hold.
	if (UDreamWidget* Widget = GetWidget())
	{
		Widget->MarkCanvasUpdate(false);
	}
}

void UDreamStylizeEffect::SetOpacity(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	if (Opacity != Value)
	{
		Opacity = Value;
		RefreshEffect();
	}
}

void UDreamStylizeEffect::SetEffectBlendMode(EDreamUIBlendMode Value)
{
	if (EffectBlendMode != Value)
	{
		EffectBlendMode = Value;
		RefreshEffect();
	}
}

void UDreamStylizeEffect::SetTarget(EDreamEffectTarget Value)
{
	if (Target != Value)
	{
		Target = Value;
		RefreshEffect();
		MarkVertexPositionDirty();
	}
}

void UDreamStylizeEffect::TriggerBurst(float InDuration, float InIntensity)
{
	const UWorld* World = GetWorld();
	BurstStart = World != nullptr ? World->GetTimeSeconds() : 0.0f;
	BurstDuration = FMath::Max(InDuration, 0.0f);
	BurstIntensity = FMath::Max(InIntensity, 0.0f);
	RefreshEffect();
}

void UDreamStylizeEffect::PackLayers(FDreamUIStylizePacked& OutPacked) const
{
	FDreamUIStylizeMath::Pack(Layers, Opacity * DreamStylizeEffectLocal::AlphaOf(*this), EffectBlendMode, BurstStart, BurstDuration, BurstIntensity, OutPacked);
}

void UDreamStylizeEffect::RefreshEffect()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}
	PackedAlpha = DreamStylizeEffectLocal::AlphaOf(*this);
	FDreamUIStylizePacked Packed;
	PackLayers(Packed);
	Effect = Packed.NumLayers > 0 ? DreamUIPostProcessEffects::MakeStylizeEffect(Packed) : FDreamUIImageEffectRef();
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

void UDreamStylizeEffect::SendRegionVertexDataToRenderProxy()
{
	Super::SendRegionVertexDataToRenderProxy();
	if (!FMath::IsNearlyEqual(PackedAlpha, DreamStylizeEffectLocal::AlphaOf(*this)))
	{
		RefreshEffect();
	}
}

FDreamVisualPostProcessRenderProxyPtr UDreamStylizeEffect::GetRenderProxy()
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

bool UDreamStylizeEffect::HaveValidData() const
{
	return Target == EDreamEffectTarget::Backdrop && Super::HaveValidData();
}

FDreamUIImageEffectRef UDreamStylizeEffect::GetChildrenEffect() const
{
	return Target == EDreamEffectTarget::Children && bEnabled ? Effect : FDreamUIImageEffectRef();
}

DECLARE_DREAM_GUI_VISUAL("StylizeEffect", UDreamStylizeEffect)
