// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/Effects/DreamBackgroundPixelate.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "DreamUIRender/DreamUIPostProcessEffects.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWidgetRegistry.h"

UDreamBackgroundPixelate::UDreamBackgroundPixelate(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	
}

void UDreamBackgroundPixelate::BeginPlay()
{
	Super::BeginPlay();
}

#if WITH_EDITOR
void UDreamBackgroundPixelate::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		
	}
}
#endif
void UDreamBackgroundPixelate::MarkAllDirty()
{
	Super::MarkAllDirty();

	SendRegionVertexDataToRenderProxy();
	SendMaskTextureToRenderProxy();
}



void UDreamBackgroundPixelate::SetPixelateStrength(float Value)
{
	if (PixelateStrength != Value)
	{
		PixelateStrength = Value;
		SendOthersDataToRenderProxy();
	}
}

void UDreamBackgroundPixelate::SetApplyAlphaToStrength(bool Value)
{
	if (ApplyAlphaToStrength != Value)
	{
		ApplyAlphaToStrength = Value;
		SendOthersDataToRenderProxy();
	}
}

float UDreamBackgroundPixelate::GetStrengthInternal()
{
	if (ApplyAlphaToStrength)
	{
		return GetFinalAlpha01() * PixelateStrength;
	}
	return PixelateStrength;
}




void UDreamBackgroundPixelate::SendOthersDataToRenderProxy()
{
	if (RenderProxy.IsValid())
	{
		DreamUIPostProcessEffects::SetBackgroundPixelateStrength_GameThread(RenderProxy, this->GetStrengthInternal());
	}
}

FDreamVisualPostProcessRenderProxyPtr UDreamBackgroundPixelate::GetRenderProxy()
{
	if (!RenderProxy.IsValid())
	{
		RenderProxy = DreamUIPostProcessEffects::CreateBackgroundPixelateProxy();
		SendRegionVertexDataToRenderProxy();
		SendMaskTextureToRenderProxy();
	}
	return RenderProxy;
}

void UDreamBackgroundPixelate::SendRegionVertexDataToRenderProxy()
{
	Super::SendRegionVertexDataToRenderProxy();
	SendOthersDataToRenderProxy();
}

DECLARE_DREAM_GUI_VISUAL("BackgroundPixelate", UDreamBackgroundPixelate)
