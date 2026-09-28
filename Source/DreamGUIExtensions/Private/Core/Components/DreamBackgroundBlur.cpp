// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamBackgroundBlur.h"

#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIRender/DreamUIPostProcessEffects.h"
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
