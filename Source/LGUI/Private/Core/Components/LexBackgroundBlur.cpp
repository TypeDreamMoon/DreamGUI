// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexBackgroundBlur.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Core/LexUIRender/LexUIPostProcessShaders.h"
#include "Core/LexUIRender/LexUIVertex.h"
#include "PipelineStateCache.h"
#include "Core/LexUIRender/LexUIRenderer.h"
#include "RenderTargetPool.h"
#include "Core/LexVisualPostProcessRenderProxy.h"
#include "RHIStaticStates.h"
#include "Engine/TextureRenderTarget2D.h"

ULexBackgroundBlur::ULexBackgroundBlur(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	
}

#if WITH_EDITOR
void ULexBackgroundBlur::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULexBackgroundBlur, MaxDownSampleLevel))
		{
			MaxDownSampleLevel += 1;//just make it work
			SetMaxDownSampleLevel(MaxDownSampleLevel - 1);
		}
	}
}
#endif


void ULexBackgroundBlur::MarkAllDirty()
{
	Super::MarkAllDirty();

	SendRegionVertexDataToRenderProxy();
	SendMaskTextureToRenderProxy();
	SendOthersDataToRenderProxy();
}

#define MAX_BlurStrength 100.0f
#define INV_MAX_BlurStrength 0.01f

DECLARE_CYCLE_STAT(TEXT("PostProcess_BackgroundBlur"), STAT_BackgroundBlur, STATGROUP_LGUI);
class FUIBackgroundBlurRenderProxy : public FLexVisualPostProcessRenderProxy
{
public:
	float Inv_SampleLevelInterval = 0.0f;
	float BlurStrength = 0.0f;
public:
	FUIBackgroundBlurRenderProxy()
		:FLexVisualPostProcessRenderProxy()
	{

	}
	virtual bool CanRender()const override
	{
		return BlurStrength > 0.0f;
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
	) override
	{
		SCOPE_CYCLE_COUNTER(STAT_BackgroundBlur);
		if (BlurStrength <= 0.0f)return;

		auto& RHICmdList = GraphBuilder.RHICmdList;

		TRefCountPtr<IPooledRenderTarget> ScreenResolvedTexture;
		TRefCountPtr<IPooledRenderTarget> BlurEffectRenderTarget1;
		TRefCountPtr<IPooledRenderTarget> BlurEffectRenderTarget2;
		auto ReleaseRenderTarget = [&] {
			if (ScreenResolvedTexture.IsValid())
			{
				ScreenResolvedTexture.SafeRelease();
			}
			if (BlurEffectRenderTarget1.IsValid())
			{
				BlurEffectRenderTarget1.SafeRelease();
			}
			if (BlurEffectRenderTarget2.IsValid())
			{
				BlurEffectRenderTarget2.SafeRelease();
			}
		};

		uint8 NumSamples = ScreenTargetTexture->GetNumSamples();
		auto ScreenSize = ScreenTargetTexture->GetSizeXY();
		if (NumSamples > 1)
		{
			FPooledRenderTargetDesc desc(FPooledRenderTargetDesc::Create2DDesc(ScreenSize, ScreenTargetTexture->GetFormat(), FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, desc, ScreenResolvedTexture, TEXT("LexUIBlurEffectResolveTarget"));
			if (!ScreenResolvedTexture.IsValid())
				return;
			auto ResolveSrc = RegisterExternalTexture(GraphBuilder, ScreenTargetTexture, TEXT("LexUIBlurEffectResolveSource"));
			auto ResolveDst = RegisterExternalTexture(GraphBuilder, ScreenResolvedTexture->GetRHI(), TEXT("LexUIBlurEffectResolveTarget"));
			Renderer->AddResolvePass(GraphBuilder, FRDGTextureMSAA(ResolveSrc, ResolveDst), FIntRect(0, 0, ScreenSize.X, ScreenSize.Y), NumSamples, GlobalShaderMap);
		}

		float width = bFullScreen ? ScreenSize.X : RectSize.X;
		float height = bFullScreen ? ScreenSize.Y : RectSize.Y;
		width = FMath::Max(width, 1.0f);
		height = FMath::Max(height, 1.0f);
		FVector2f inv_TextureSize(1.0f / width, 1.0f / height);
		FIntPoint TextureSize(width, height);
		//get render target
		{
			FPooledRenderTargetDesc desc(FPooledRenderTargetDesc::Create2DDesc(TextureSize, ScreenTargetTexture->GetFormat(), FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
			if (RenderTargetResource == nullptr)
			{
				if (!bFullScreen)
				{
					GRenderTargetPool.FindFreeElement(RHICmdList, desc, BlurEffectRenderTarget1, TEXT("LexUIBlurEffectRenderTarget1"));
					if (!BlurEffectRenderTarget1.IsValid())
					{
						ReleaseRenderTarget();
						return;
					}
				}//full screen don't need it
			}
			else
			{
				GRenderTargetPool.FindFreeElement(RHICmdList, desc, BlurEffectRenderTarget1, TEXT("LexUIBlurEffectRenderTarget1"));
				if (!BlurEffectRenderTarget1.IsValid())
				{
					ReleaseRenderTarget();
					return;
				}
			}
			GRenderTargetPool.FindFreeElement(RHICmdList, desc, BlurEffectRenderTarget2, TEXT("LexUIBlurEffectRenderTarget2"));
			if (!BlurEffectRenderTarget2.IsValid())
			{
				ReleaseRenderTarget();
				return;
			}
		}
		FRHITexture* BlurEffectRenderTexture1 = nullptr;
		if (RenderTargetResource == nullptr)
		{
			if (bFullScreen)//full screen just use it directly
			{
				BlurEffectRenderTexture1 = NumSamples > 1 ? ScreenResolvedTexture->GetRHI() : ScreenTargetTexture.GetReference();
			}
			else
			{
				BlurEffectRenderTexture1 = BlurEffectRenderTarget1->GetRHI();
			}
		}
		else
		{
			BlurEffectRenderTexture1 = BlurEffectRenderTarget1->GetRHI();
		}
		auto BlurEffectRenderTexture2 = BlurEffectRenderTarget2->GetRHI();

		auto ModelViewProjectionMatrix = ObjectToWorldMatrix * ViewProjectionMatrix;
		if (!bFullScreen)
		{
			Renderer->CopyRenderTargetOnMeshRegion(GraphBuilder
				, RegisterExternalTexture(GraphBuilder, BlurEffectRenderTexture1, TEXT("LexUIBlurEffectRenderTexture1_ExternalTexture"))
				, NumSamples > 1 ? ScreenResolvedTexture->GetRHI() : ScreenTargetTexture.GetReference()
				, GlobalShaderMap
				, RenderScreenToMeshRegionVertexArray
				, ModelViewProjectionMatrix
				, FIntRect(0, 0, BlurEffectRenderTexture1->GetSizeXYZ().X, BlurEffectRenderTexture1->GetSizeXYZ().Y)
				, ViewTextureScaleOffset
			);
		}
		//do the blur process on the area
		{
			TShaderMapRef<FLexUISimplePostProcessVS> VertexShader(GlobalShaderMap);
			TShaderMapRef<FLexUIPostProcessGaussianBlurPS> PixelShader(GlobalShaderMap);

			auto samplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			float calculatedBlurStrength = FMath::Pow(BlurStrength * INV_MAX_BlurStrength, 0.5f) * MAX_BlurStrength;//this can make the blur effect transition feel more smooth
			calculatedBlurStrength = calculatedBlurStrength * Inv_SampleLevelInterval;
			float calculatedBlurStrength2 = 1.0f;
			int sampleCount = (int)calculatedBlurStrength + 1;
			for (int i = 0; i < sampleCount; i++)
			{
				float tempBlurStrength = 0.0f;
				if (i + 1 == sampleCount)
				{
					float fracValue = (calculatedBlurStrength - (int)calculatedBlurStrength);
					fracValue = FMath::FastAsin(fracValue * 2.0f - 1.0f) * INV_PI + 0.5f;//another thing to make the blur transition feel more smooth
					tempBlurStrength = calculatedBlurStrength2 * fracValue;
				}
				else
				{
					tempBlurStrength = calculatedBlurStrength2;
				}

				auto* VerticalPassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
				VerticalPassParameters->RenderTargets[0] = FRenderTargetBinding(RegisterExternalTexture(GraphBuilder, BlurEffectRenderTexture2, TEXT("Vertical_BlurEffectRenderTexture2")), ERenderTargetLoadAction::ELoad);
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("LexUIBackgroundBlur_Pass_Vertical"),
					VerticalPassParameters,
					ERDGPassFlags::Raster,
					[this, VertexShader, PixelShader, Renderer, BlurEffectRenderTexture1, TextureSize, samplerState, inv_TextureSize, tempBlurStrength](FRHICommandListImmediate& RHICmdList)
					{
						FGraphicsPipelineStateInitializer GraphicsPSOInit;
						RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
						GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
						GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
						GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
						GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetLexUIPostProcessVertexDeclaration();
						GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
						GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
						GraphicsPSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
						SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::CheckApply);
						VertexShader->SetParameters(RHICmdList);
						//render vertical
						RHICmdList.SetViewport(0, 0, 0.0f, TextureSize.X, TextureSize.Y, 1.0f);
						PixelShader->SetMainTexture(RHICmdList, BlurEffectRenderTexture1, samplerState);
						PixelShader->SetBlurStrength(RHICmdList, FVector2f(0, tempBlurStrength * inv_TextureSize.Y));
						Renderer->DrawFullScreenQuad(RHICmdList);
					});

				auto* HorizontalPassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
				HorizontalPassParameters->RenderTargets[0] = FRenderTargetBinding(RegisterExternalTexture(GraphBuilder, BlurEffectRenderTexture1, TEXT("Vertical_BlurEffectRenderTexture1")), ERenderTargetLoadAction::ELoad);
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("LexUIBackgroundBlur_Pass_Horizontal"),
					HorizontalPassParameters,
					ERDGPassFlags::Raster,
					[this, VertexShader, PixelShader, Renderer, BlurEffectRenderTexture2, TextureSize, samplerState, inv_TextureSize, tempBlurStrength](FRHICommandListImmediate& RHICmdList)
					{
						FGraphicsPipelineStateInitializer GraphicsPSOInit;
						RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
						GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
						GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
						GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
						GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetLexUIPostProcessVertexDeclaration();
						GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
						GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
						GraphicsPSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
						SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::CheckApply);
						VertexShader->SetParameters(RHICmdList);
						//render horizontal
						RHICmdList.SetViewport(0, 0, 0.0f, TextureSize.X, TextureSize.Y, 1.0f);
						PixelShader->SetMainTexture(RHICmdList, BlurEffectRenderTexture2, samplerState);
						PixelShader->SetBlurStrength(RHICmdList, FVector2f(tempBlurStrength * inv_TextureSize.X, 0));
						Renderer->DrawFullScreenQuad(RHICmdList);
					});

				calculatedBlurStrength2 *= 2;
			}
		}

		if (RenderTargetResource == nullptr)
		{
			//after blur process, copy the blur result image back to screen image of the area
			if (!bFullScreen)
			{
				RenderMeshOnScreen_RenderThread(GraphBuilder, SceneTextures, ScreenTargetTexture, GlobalShaderMap, BlurEffectRenderTexture1, ModelViewProjectionMatrix, ObjectToWorldMatrix, IsWorldSpace, BlendDepthForWorld, DepthFadeForWorld, DepthTextureScaleOffset, ViewRect);
			}//full screen don't need it
		}
		else
		{
			Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, BlurEffectRenderTexture1, RenderTargetResource->GetRenderTargetTexture(), true);
		}

		//release render target
		ReleaseRenderTarget();
	}
};


void ULexBackgroundBlur::SendOthersDataToRenderProxy()
{
	if (RenderProxy.IsValid())
	{
		auto BackgroundBlurRenderProxy = (FUIBackgroundBlurRenderProxy*)(RenderProxy.Get());
		struct FUIBackgroundBlurUpdateOthersData
		{
			float BlurStrengthWithAlpha;
			float Inv_SampleLevelInterval;
		};
		auto updateData = new FUIBackgroundBlurUpdateOthersData();
		updateData->BlurStrengthWithAlpha = this->GetBlurStrengthInternal();
		updateData->Inv_SampleLevelInterval = this->Inv_SampleLevelInterval;
		ENQUEUE_RENDER_COMMAND(FLexBackgroundBlur_UpdateData)
			([BackgroundBlurRenderProxy, updateData](FRHICommandListImmediate& RHICmdList)
			{
				BackgroundBlurRenderProxy->Inv_SampleLevelInterval = updateData->Inv_SampleLevelInterval;
				BackgroundBlurRenderProxy->BlurStrength = updateData->BlurStrengthWithAlpha;
				delete updateData;
			});
	}
}

void ULexBackgroundBlur::SetBlurStrength(float Value)
{
	if (BlurStrength != Value)
	{
		BlurStrength = Value;
		GetWidget()->MarkCanvasUpdate(false, false, false, false);
		SendOthersDataToRenderProxy();
	}
}

void ULexBackgroundBlur::SetApplyAlphaToBlur(bool Value)
{
	if (ApplyAlphaToBlur != Value)
	{
		ApplyAlphaToBlur = Value;
		GetWidget()->MarkCanvasUpdate(false, false, false, false);
		SendOthersDataToRenderProxy();
	}
}

void ULexBackgroundBlur::SetMaxDownSampleLevel(int Value)
{
	if (MaxDownSampleLevel != Value)
	{
		MaxDownSampleLevel = Value;
		Inv_SampleLevelInterval = 1.0f / MAX_BlurStrength * MaxDownSampleLevel;
		GetWidget()->MarkCanvasUpdate(false, false, false, false);
		SendOthersDataToRenderProxy();
	}
}

float ULexBackgroundBlur::GetBlurStrengthInternal()
{
	if (ApplyAlphaToBlur)
	{
		return GetFinalAlpha01() * BlurStrength;
	}
	return BlurStrength;
}

TSharedPtr<FLexVisualPostProcessRenderProxy> ULexBackgroundBlur::GetRenderProxy()
{
	if (!RenderProxy.IsValid())
	{
		RenderProxy = MakeShared<FUIBackgroundBlurRenderProxy>();
		Inv_SampleLevelInterval = 1.0f / MAX_BlurStrength * MaxDownSampleLevel;
		SendRegionVertexDataToRenderProxy();
		SendMaskTextureToRenderProxy();
		SendRenderTargetToRenderProxy();
		SendOthersDataToRenderProxy();
	}
	return RenderProxy;
}

void ULexBackgroundBlur::SendRegionVertexDataToRenderProxy()
{
	Super::SendRegionVertexDataToRenderProxy();
	if (RenderProxy.IsValid())
	{
		auto BackgroundBlurRenderProxy = (FUIBackgroundBlurRenderProxy*)(RenderProxy.Get());
		auto blurStrengthWithAlpha = this->GetBlurStrengthInternal();
		ENQUEUE_RENDER_COMMAND(FLexBackgroundBlur_UpdateData)
			([BackgroundBlurRenderProxy, blurStrengthWithAlpha](FRHICommandListImmediate& RHICmdList)
				{
					BackgroundBlurRenderProxy->BlurStrength = blurStrengthWithAlpha;
				});
	}
}
