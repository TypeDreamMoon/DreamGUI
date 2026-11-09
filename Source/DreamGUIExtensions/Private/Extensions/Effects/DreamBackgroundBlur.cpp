// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Extensions/Effects/DreamBackgroundBlur.h"

#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "DreamUIRender/DreamUIPostProcessEffects.h"
#include "DreamUIRender/DreamUIBackdropEffects.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWidgetRegistry.h"


UDreamBackgroundBlur::UDreamBackgroundBlur(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	
}

#if WITH_EDITOR
void UDreamBackgroundBlur::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UDreamBackgroundBlur, MaxDownSampleLevel))
		{
			MaxDownSampleLevel += 1;//just make it work
			SetMaxDownSampleLevel(MaxDownSampleLevel - 1);
		}
	}
}
#endif


void UDreamBackgroundBlur::MarkAllDirty()
{
	Super::MarkAllDirty();

	SendRegionVertexDataToRenderProxy();
	SendMaskTextureToRenderProxy();
	SendOthersDataToRenderProxy();
	SendStrengthMapToRenderProxy();
}

void UDreamBackgroundBlur::SetStrengthMap(const FDreamBlurStrengthMap& Value)
{
	StrengthMap = Value;
	SendStrengthMapToRenderProxy();
}

void UDreamBackgroundBlur::SendStrengthMapToRenderProxy()
{
	if (!RenderProxy.IsValid())
	{
		return;
	}
	FDreamUIBlurStrengthMap Map;
	Map.Kind = static_cast<int32>(StrengthMap.Kind);
	switch (StrengthMap.Kind)
	{
	case EDreamBlurStrengthMapKind::Linear:
		Map.Points = FVector4f(StrengthMap.Start.X, StrengthMap.Start.Y, StrengthMap.End.X, StrengthMap.End.Y);
		break;
	case EDreamBlurStrengthMapKind::Radial:
		Map.Points = FVector4f(StrengthMap.Start.X, StrengthMap.Start.Y, StrengthMap.InnerRadius, FMath::Max(StrengthMap.OuterRadius, StrengthMap.InnerRadius));
		break;
	default:
		break;
	}
	Map.Strengths = FVector2f(StrengthMap.StartStrength, StrengthMap.EndStrength);
	// The shader's channels: red, green, blue, alpha, luminosity.
	switch (StrengthMap.Channel)
	{
	case EDreamClipMaskChannel::Red: Map.Channel = 0; break;
	case EDreamClipMaskChannel::Green: Map.Channel = 1; break;
	case EDreamClipMaskChannel::Blue: Map.Channel = 2; break;
	case EDreamClipMaskChannel::Luminance: Map.Channel = 4; break;
	default: Map.Channel = 3; break;
	}
	Map.Pattern = static_cast<int32>(StrengthMap.Pattern);
	Map.Frequency = FMath::Max(StrengthMap.Frequency, 0.01f);
	Map.Seed = static_cast<float>(StrengthMap.Seed);
	Map.Texture = StrengthMap.Kind == EDreamBlurStrengthMapKind::Texture ? StrengthMap.Texture.Get() : nullptr;
	Map.MaxLevel = FMath::Clamp(StrengthMap.MaxLevel, 1, 5);
	DreamUIPostProcessEffects::SetBackgroundBlurStrengthMap_GameThread(RenderProxy, Map);
	// A map draws the blur over its own copy of what is behind, which is more of it than the region the uniform blur copies.
	if (UDreamWidget* Widget = GetWidget())
	{
		Widget->MarkCanvasUpdate(false);
	}
}



void UDreamBackgroundBlur::SendOthersDataToRenderProxy()
{
	if (RenderProxy.IsValid())
	{
		DreamUIPostProcessEffects::SetBackgroundBlur_GameThread(RenderProxy, this->GetBlurStrengthInternal(), this->MaxDownSampleLevel);
	}
}

void UDreamBackgroundBlur::SetBlurStrength(float Value)
{
	if (BlurStrength != Value)
	{
		BlurStrength = Value;
		SendOthersDataToRenderProxy();
	}
}

void UDreamBackgroundBlur::SetApplyAlphaToBlur(bool Value)
{
	if (ApplyAlphaToBlur != Value)
	{
		ApplyAlphaToBlur = Value;
		SendOthersDataToRenderProxy();
	}
}

void UDreamBackgroundBlur::SetMaxDownSampleLevel(int Value)
{
	if (MaxDownSampleLevel != Value)
	{
		MaxDownSampleLevel = Value;
		SendOthersDataToRenderProxy();
	}
}

float UDreamBackgroundBlur::GetBlurStrengthInternal()
{
	if (ApplyAlphaToBlur)
	{
		return GetFinalAlpha01() * BlurStrength;
	}
	return BlurStrength;
}

FDreamVisualPostProcessRenderProxyPtr UDreamBackgroundBlur::GetRenderProxy()
{
	if (!RenderProxy.IsValid())
	{
		RenderProxy = DreamUIPostProcessEffects::CreateBackgroundBlurProxy();
		SendRegionVertexDataToRenderProxy();
		SendMaskTextureToRenderProxy();
		SendRenderTargetToRenderProxy();
		SendOthersDataToRenderProxy();
		SendStrengthMapToRenderProxy();
	}
	return RenderProxy;
}

void UDreamBackgroundBlur::SendRegionVertexDataToRenderProxy()
{
	Super::SendRegionVertexDataToRenderProxy();
	if (RenderProxy.IsValid())
	{
		DreamUIPostProcessEffects::SetBackgroundBlurStrength_GameThread(RenderProxy, this->GetBlurStrengthInternal());
	}
}

DECLARE_DREAM_GUI_VISUAL("BackgroundBlur", UDreamBackgroundBlur)
