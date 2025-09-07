// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexBackgroundPixelate.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Core/LexUISpriteData.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Core/LexUIRender/LexUIPostProcessShaders.h"
#include "Core/LexUIRender/LexUIVertex.h"
#include "PipelineStateCache.h"
#include "Core/LexUIRender/LexUIRenderer.h"
#include "Core/Components/LexCanvas.h"
#include "Core/LexUISettings.h"
#include "RenderTargetPool.h"
#include "Core/LexVisualPostProcessRenderProxy.h"
#include "RHIStaticStates.h"

ULexBackgroundPixelate::ULexBackgroundPixelate(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	
}

void ULexBackgroundPixelate::BeginPlay()
{
	Super::BeginPlay();
}

#if WITH_EDITOR
void ULexBackgroundPixelate::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		
	}
}
#endif
void ULexBackgroundPixelate::MarkAllDirty()
{
	Super::MarkAllDirty();

	SendRegionVertexDataToRenderProxy();
	SendMaskTextureToRenderProxy();
}



void ULexBackgroundPixelate::SetPixelateStrength(float newValue)
{
	if (pixelateStrength != newValue)
	{
		pixelateStrength = newValue;
		GetWidget()->MarkCanvasUpdate(false, false, false, false);
		SendOthersDataToRenderProxy();
	}
}

void ULexBackgroundPixelate::SetApplyAlphaToStrength(bool newValue)
{
	if (applyAlphaToStrength != newValue)
	{
		applyAlphaToStrength = newValue;
		GetWidget()->MarkCanvasUpdate(false, false, false, false);
		SendOthersDataToRenderProxy();
	}
}

float ULexBackgroundPixelate::GetStrengthInternal()
{
	if (applyAlphaToStrength)
	{
		return GetFinalAlpha01() * pixelateStrength;
	}
	return pixelateStrength;
}


#define MAX_PixelateStrength 100.0f
#define INV_MAX_PixelateStrength 0.01f

DECLARE_CYCLE_STAT(TEXT("PostProcess_BackgroundPixelate"), STAT_BackgroundPixelate, STATGROUP_LGUI);
class FUIBackgroundPixelateRenderProxy :public FLexVisualPostProcessRenderProxy
{
public:
	float pixelateStrength = 0.0f;
public:
	FUIBackgroundPixelateRenderProxy()
		:FLexVisualPostProcessRenderProxy()
	{

	}
	virtual bool CanRender()const override
	{
		return pixelateStrength > 0.0f;
	}
	virtual void OnRenderPostProcess_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FLexUIRenderer* Renderer,
		FTextureRHIRef ScreenTargetTexture,
		FGlobalShaderMap* GlobalShaderMap,
		const FMatrix44f& ViewProjectionMatrix,
		bool IsWorldSpace,
		float BlendDepthForWorld,
		int DepthFadeForWorld,
		const FIntRect& ViewRect,
		const FVector4f& DepthTextureScaleOffset,
		const FVector4f& ViewTextureScaleOffset
	)override
	{
		SCOPE_CYCLE_COUNTER(STAT_BackgroundPixelate);
		if (pixelateStrength <= 0.0f)return;

		auto& RHICmdList = GraphBuilder.RHICmdList;

		TRefCountPtr<IPooledRenderTarget> ScreenResolvedTexture;
		TRefCountPtr<IPooledRenderTarget> PixelateEffectRenderTarget;
		auto ReleaseRenderTarget = [&] {
			if (ScreenResolvedTexture.IsValid())
			{
				ScreenResolvedTexture.SafeRelease();
			}
			if (PixelateEffectRenderTarget.IsValid())
			{
				PixelateEffectRenderTarget.SafeRelease();
			}
		};

		uint8 NumSamples = ScreenTargetTexture->GetNumSamples();
		if (NumSamples > 1)
		{
			auto Size = ScreenTargetTexture->GetSizeXY();
			FPooledRenderTargetDesc desc(FPooledRenderTargetDesc::Create2DDesc(Size, ScreenTargetTexture->GetFormat(), FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, desc, ScreenResolvedTexture, TEXT("LGUIBlurEffectResolveTarget"));
			if (!ScreenResolvedTexture.IsValid())
				return;
			auto ResolveSrc = RegisterExternalTexture(GraphBuilder, ScreenTargetTexture, TEXT("LGUIBlurEffectResolveSource"));
			auto ResolveDst = RegisterExternalTexture(GraphBuilder, ScreenResolvedTexture->GetRHI(), TEXT("LGUIBlurEffectResolveTarget"));
			Renderer->AddResolvePass(GraphBuilder, FRDGTextureMSAA(ResolveSrc, ResolveDst), FIntRect(0, 0, Size.X, Size.Y), NumSamples, GlobalShaderMap);
		}

		float calculatedStrength = FMath::Pow(pixelateStrength * INV_MAX_PixelateStrength, 2) * MAX_PixelateStrength;//this can make the pixelate effect transition feel more linear
		calculatedStrength = FMath::Clamp(calculatedStrength, 0.0f, 100.0f);
		calculatedStrength += 1;

		auto width = (int)(RectSize.X / calculatedStrength);
		auto height = (int)(RectSize.Y / calculatedStrength);
		width = FMath::Clamp(width, 1, (int)RectSize.X);
		height = FMath::Clamp(height, 1, (int)RectSize.Y);

		//get render target
		{
			FPooledRenderTargetDesc desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(width, height), ScreenTargetTexture->GetFormat(), FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, desc, PixelateEffectRenderTarget, TEXT("LGUIPixelateEffectRenderTarget"));
			if (!PixelateEffectRenderTarget.IsValid())
			{
				ReleaseRenderTarget();
				return;
			}
		}
		auto PixelateEffectRenderTargetTexture = PixelateEffectRenderTarget->GetRHI();

		//copy rect area from screen image to a render target, so we can just process this area
		auto modelViewProjectionMatrix = ObjectToWorldMatrix * ViewProjectionMatrix;
		Renderer->CopyRenderTargetOnMeshRegion(GraphBuilder
			, RegisterExternalTexture(GraphBuilder, PixelateEffectRenderTargetTexture, TEXT("LGUI_PixelateEffectRenderTargetTexture"))
			, NumSamples > 1 ? ScreenResolvedTexture->GetRHI() : ScreenTargetTexture.GetReference()
			, GlobalShaderMap
			, RenderScreenToMeshRegionVertexArray
			, modelViewProjectionMatrix
			, FIntRect(0, 0, PixelateEffectRenderTargetTexture->GetSizeXYZ().X, PixelateEffectRenderTargetTexture->GetSizeXYZ().Y)
			, ViewTextureScaleOffset
		);
		//after pixelate process, copy the area back to screen image
		RenderMeshOnScreen_RenderThread(GraphBuilder, SceneTextures, ScreenTargetTexture, GlobalShaderMap, PixelateEffectRenderTargetTexture, modelViewProjectionMatrix, ObjectToWorldMatrix, IsWorldSpace, BlendDepthForWorld, BlendDepthForWorld, DepthTextureScaleOffset, ViewRect, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

		//release render target
		ReleaseRenderTarget();
	}
};

void ULexBackgroundPixelate::SendOthersDataToRenderProxy()
{
	if (RenderProxy.IsValid())
	{
		auto TempRenderProxy = (FUIBackgroundPixelateRenderProxy*)(RenderProxy.Get());
		float pixelateStrengthWidthAlpha = this->GetStrengthInternal();
		ENQUEUE_RENDER_COMMAND(FUIBackgroundPixelate_UpdateData)
			([TempRenderProxy, pixelateStrengthWidthAlpha](FRHICommandListImmediate& RHICmdList)
				{
					TempRenderProxy->pixelateStrength = pixelateStrengthWidthAlpha;
				});
	}
}

TSharedPtr<FLexVisualPostProcessRenderProxy> ULexBackgroundPixelate::GetRenderProxy()
{
	if (!RenderProxy.IsValid())
	{
		RenderProxy = MakeShared<FUIBackgroundPixelateRenderProxy>();
		SendRegionVertexDataToRenderProxy();
		SendMaskTextureToRenderProxy();
	}
	return RenderProxy;
}

void ULexBackgroundPixelate::SendRegionVertexDataToRenderProxy()
{
	Super::SendRegionVertexDataToRenderProxy();
	SendOthersDataToRenderProxy();
}
