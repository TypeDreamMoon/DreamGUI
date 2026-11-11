// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/Effects/DreamStylizeEffect.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "DreamUIRender/DreamUIBackdropEffects.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

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

bool UDreamStylizeEffect::SetLayerParameter(int32 LayerIndex, FName ParameterName, float Value)
{
	if (!Layers.IsValidIndex(LayerIndex) || !FMath::IsFinite(Value))
	{
		return false;
	}
	const FProperty* Property = FDreamStylizeLayer::StaticStruct()->FindPropertyByName(ParameterName);
	void* Address = Property != nullptr ? Property->ContainerPtrToValuePtr<void>(&Layers[LayerIndex]) : nullptr;
	if (Address == nullptr)
	{
		return false;
	}
	if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
	{
		// The details panel's limits, as it would apply them, where they are known (editor builds): a strength of 2 on a
		// 0..1 number is a mistake, not a look. The shader takes any number either way.
		float Clamped = Value;
#if WITH_METADATA
		if (Property->HasMetaData(TEXT("ClampMin")))
		{
			Clamped = FMath::Max(Clamped, Property->GetFloatMetaData(TEXT("ClampMin")));
		}
		if (Property->HasMetaData(TEXT("ClampMax")))
		{
			Clamped = FMath::Min(Clamped, Property->GetFloatMetaData(TEXT("ClampMax")));
		}
#endif
		Float->SetPropertyValue(Address, Clamped);
	}
	else if (const FIntProperty* Int = CastField<FIntProperty>(Property))
	{
		Int->SetPropertyValue(Address, FMath::RoundToInt32(Value));
	}
	else if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
	{
		Bool->SetPropertyValue(Address, Value != 0.0f);
	}
	else
	{
		return false;
	}
	// What SetLayers does with a new list, for one number of it.
	RefreshEffect();
	if (UDreamWidget* Widget = GetWidget())
	{
		Widget->MarkCanvasUpdate(false);
	}
	return true;
}

bool UDreamStylizeEffect::GetLayerParameter(int32 LayerIndex, FName ParameterName, float& OutValue) const
{
	OutValue = 0.0f;
	if (!Layers.IsValidIndex(LayerIndex))
	{
		return false;
	}
	const FProperty* Property = FDreamStylizeLayer::StaticStruct()->FindPropertyByName(ParameterName);
	const void* Address = Property != nullptr ? Property->ContainerPtrToValuePtr<void>(&Layers[LayerIndex]) : nullptr;
	if (Address == nullptr)
	{
		return false;
	}
	if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
	{
		OutValue = Float->GetPropertyValue(Address);
		return true;
	}
	if (const FIntProperty* Int = CastField<FIntProperty>(Property))
	{
		OutValue = static_cast<float>(Int->GetPropertyValue(Address));
		return true;
	}
	if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
	{
		OutValue = Bool->GetPropertyValue(Address) ? 1.0f : 0.0f;
		return true;
	}
	return false;
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
