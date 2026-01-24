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
	int MaxDownSampleLevel = 0;
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
		bool bIsWorldSpace,
		bool bIsRenderTarget,
		float BlendDepthForWorld,
		int DepthFadeForWorld,
		const FIntRect& ViewRect,
		const FVector4f& DepthTextureScaleOffset,
		const FVector4f& ViewTextureScaleOffset
	) override
	{
		SCOPE_CYCLE_COUNTER(STAT_BackgroundBlur);
		if (BlurStrength <= 0.0f && RenderTargetResource == nullptr)return;

		auto& RHICmdList = GraphBuilder.RHICmdList;

		TRefCountPtr<IPooledRenderTarget> ScreenResolvedTexture;
		TRefCountPtr<IPooledRenderTarget> BlurEffectRenderTarget;
		auto ReleaseRenderTarget = [&] {
			if (ScreenResolvedTexture.IsValid())
			{
				ScreenResolvedTexture.SafeRelease();
			}
			if (BlurEffectRenderTarget.IsValid())
			{
				BlurEffectRenderTarget.SafeRelease();
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
		
		//get render target
		{
			float RectWidth = RectSize.X;
			float RectHeight = RectSize.Y;
			RectWidth = FMath::Max(RectWidth, 1.0f);
			RectHeight = FMath::Max(RectHeight, 1.0f);
			FPooledRenderTargetDesc desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(RectWidth, RectHeight), ScreenTargetTexture->GetFormat(), FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
			if (RenderTargetResource == nullptr)
			{
				if (!bUseFullSize)
				{
					GRenderTargetPool.FindFreeElement(RHICmdList, desc, BlurEffectRenderTarget, TEXT("LexUIBlurEffectRenderTarget1"));
					if (!BlurEffectRenderTarget.IsValid())
					{
						ReleaseRenderTarget();
						return;
					}
				}//full screen don't need it
			}
			else
			{
				GRenderTargetPool.FindFreeElement(RHICmdList, desc, BlurEffectRenderTarget, TEXT("LexUIBlurEffectRenderTarget1"));
				if (!BlurEffectRenderTarget.IsValid())
				{
					ReleaseRenderTarget();
					return;
				}
			}
		}
		FRHITexture* BlurEffectRenderTexture = nullptr;
		if (RenderTargetResource == nullptr)
		{
			if (bUseFullSize)//full screen just use it directly
			{
				BlurEffectRenderTexture = NumSamples > 1 ? ScreenResolvedTexture->GetRHI() : ScreenTargetTexture.GetReference();
			}
			else
			{
				BlurEffectRenderTexture = BlurEffectRenderTarget->GetRHI();
			}
		}
		else
		{
			BlurEffectRenderTexture = BlurEffectRenderTarget->GetRHI();
		}

		auto ModelViewProjectionMatrix = ObjectToWorldMatrix * ViewProjectionMatrix;
		if (!bUseFullSize)
		{
			Renderer->CopyRenderTargetOnMeshRegion(GraphBuilder
				, RegisterExternalTexture(GraphBuilder, BlurEffectRenderTexture, TEXT("LexUIBlurEffectRenderTexture_ExternalTexture"))
				, NumSamples > 1 ? ScreenResolvedTexture->GetRHI() : ScreenTargetTexture.GetReference()
				, GlobalShaderMap
				, RenderScreenToMeshRegionVertexArray
				, ModelViewProjectionMatrix
				, bIsRenderTarget
				, FIntRect(0, 0, BlurEffectRenderTexture->GetSizeXYZ().X, BlurEffectRenderTexture->GetSizeXYZ().Y)
				, ViewTextureScaleOffset
			);
		}

		float MagicNumber = 1.0f / 2.2f;//this is a magic number which can make blur transition feel smooth
		uint32 SourceWidth = BlurEffectRenderTexture->GetSizeX();
		uint32 SourceHeight = BlurEffectRenderTexture->GetSizeY();
		int DownSampleCount = 0;
		while (SourceWidth > 1 && SourceHeight > 1 && DownSampleCount < MaxDownSampleLevel)
		{
			SourceWidth >>= 1;
			SourceHeight >>= 1;
			DownSampleCount++;
		}
		float FilteredBlurStrength = FMath::Pow(BlurStrength, MagicNumber) * DownSampleCount;
		FRHITexture* PrevRT = BlurEffectRenderTexture;
		SourceWidth = BlurEffectRenderTexture->GetSizeX();
		SourceHeight = BlurEffectRenderTexture->GetSizeY();
		TArray<TRefCountPtr<IPooledRenderTarget>> DownSampleRenderTargetArray;
		for (int i = DownSampleCount; i >= 1; i--)
		{
			if (FilteredBlurStrength >= i)
			{
				SourceWidth >>= 1;
				SourceHeight >>= 1;
				TRefCountPtr<IPooledRenderTarget> DownSampleRT;
				FPooledRenderTargetDesc RenderTargetDesc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(SourceWidth, SourceHeight)
					, BlurEffectRenderTexture->GetFormat(), FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
				GRenderTargetPool.FindFreeElement(RHICmdList, RenderTargetDesc, DownSampleRT, *FString::Printf(TEXT("LexUI_DownsampleRT_%d"), DownSampleCount));
				DownSampleRenderTargetArray.Add(DownSampleRT);
				Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, PrevRT, DownSampleRT->GetRHI());
			
				PrevRT = DownSampleRT->GetRHI();
			}
		}
		for (int i = DownSampleCount; i >= 1; i--)
		{
			if (FilteredBlurStrength >= i)
			{
				auto RenderTarget = DownSampleRenderTargetArray[i - 1];
				DoBlur(RenderTarget->GetRHI(), FilteredBlurStrength - i, MagicNumber, GraphBuilder, Renderer, GlobalShaderMap);
				auto NextRT = i == 1 ? BlurEffectRenderTexture : DownSampleRenderTargetArray[i - 2]->GetRHI();
				if (FilteredBlurStrength >= i + 1)
				{
					Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, RenderTarget->GetRHI(), NextRT);
				}
				else
				{
					auto BlendValue = FMath::Clamp(FilteredBlurStrength - i, 0.0f, 1.0f);
					BlendValue = FMath::Pow(BlendValue, MagicNumber);
					Renderer->CopyRenderTarget_BlendAlpha(GraphBuilder, GlobalShaderMap, RenderTarget->GetRHI(), NextRT, BlendValue);
				}
			}
		}
		DoBlur(BlurEffectRenderTexture, FilteredBlurStrength, MagicNumber, GraphBuilder, Renderer, GlobalShaderMap);

		if (RenderTargetResource == nullptr)
		{
			//after blur process, copy the blur result image back to screen image of the area
			if (!bUseFullSize)
			{
				//copy on mesh region
				RenderMeshOnScreen_RenderThread(GraphBuilder, SceneTextures, ScreenTargetTexture, GlobalShaderMap, BlurEffectRenderTexture, ModelViewProjectionMatrix, ObjectToWorldMatrix, bIsWorldSpace, BlendDepthForWorld, DepthFadeForWorld, DepthTextureScaleOffset, ViewRect);
			}//full screen don't need it
		}
		else
		{
			Renderer->CopyRenderTarget_ColorCorrect(GraphBuilder, GlobalShaderMap, BlurEffectRenderTexture, RenderTargetResource->GetRenderTargetTexture());
		}

		//release render target
		ReleaseRenderTarget();
		for (auto& RenderTarget : DownSampleRenderTargetArray)
		{
			RenderTarget.SafeRelease();
		}
	}
	void DoBlur(FRHITexture* RenderTargetTexture
		, float BlurAmount
		, float MagicNumber
		, FRDGBuilder& GraphBuilder
		, FLexUIRenderer* Renderer
		, FGlobalShaderMap* GlobalShaderMap
		)
	{
		TRefCountPtr<IPooledRenderTarget> DownSampleRT_Blur;
		FPooledRenderTargetDesc desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(RenderTargetTexture->GetSizeX(), RenderTargetTexture->GetSizeY())
			, RenderTargetTexture->GetFormat(), FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, desc, DownSampleRT_Blur, *FString::Printf(TEXT("LexUI_DownsampleRT_Blur")));
		auto RenderTargetTexture_Blur = DownSampleRT_Blur->GetRHI();
		
		TShaderMapRef<FLexUISimplePostProcessVS> VertexShader(GlobalShaderMap);
		TShaderMapRef<FLexUIPostProcessGaussianBlurPS> PixelShader(GlobalShaderMap);
		auto SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		
		BlurAmount = FMath::Clamp(BlurAmount, 0.0f, 1.0f);
		BlurAmount = FMath::Pow(BlurAmount, MagicNumber);
				
		auto* VerticalPassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		VerticalPassParameters->RenderTargets[0] = FRenderTargetBinding(RegisterExternalTexture(GraphBuilder, RenderTargetTexture_Blur, TEXT("Horizontal_BlurEffectRenderTexture")), ERenderTargetLoadAction::ELoad);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("LexUIBackgroundBlur_Pass_Horizontal"),
			VerticalPassParameters,
			ERDGPassFlags::Raster,
			[this, VertexShader, PixelShader, Renderer, MainTexture = RenderTargetTexture, SamplerState, BlurAmount](FRHICommandListImmediate& RHICmdList)
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
				RHICmdList.SetViewport(0, 0, 0.0f, MainTexture->GetSizeX(), MainTexture->GetSizeY(), 1.0f);
				PixelShader->SetMainTexture(RHICmdList, MainTexture, SamplerState);
				PixelShader->SetBlurStrength(RHICmdList, FVector2f(1.0f / MainTexture->GetSizeX() * BlurAmount, 0));
				Renderer->DrawFullScreenQuad(RHICmdList);
			});

		auto* HorizontalPassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		HorizontalPassParameters->RenderTargets[0] = FRenderTargetBinding(RegisterExternalTexture(GraphBuilder, RenderTargetTexture, TEXT("Vertical_BlurEffectRenderTexture")), ERenderTargetLoadAction::ELoad);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("LexUIBackgroundBlur_Pass_Vertical"),
			HorizontalPassParameters,
			ERDGPassFlags::Raster,
			[this, VertexShader, PixelShader, Renderer, MainTexture = RenderTargetTexture_Blur, SamplerState, BlurAmount](FRHICommandListImmediate& RHICmdList)
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
				RHICmdList.SetViewport(0, 0, 0.0f, MainTexture->GetSizeX(), MainTexture->GetSizeY(), 1.0f);
				PixelShader->SetMainTexture(RHICmdList, MainTexture, SamplerState);
				PixelShader->SetBlurStrength(RHICmdList, FVector2f(0, 1.0f / MainTexture->GetSizeY() * BlurAmount));
				Renderer->DrawFullScreenQuad(RHICmdList);
			});

		DownSampleRT_Blur.SafeRelease();
	}
};


void ULexBackgroundBlur::SendOthersDataToRenderProxy()
{
	if (RenderProxy != nullptr)
	{
		auto BackgroundBlurRenderProxy = (FUIBackgroundBlurRenderProxy*)RenderProxy;
		struct FUIBackgroundBlurUpdateOthersData
		{
			float BlurStrengthWithAlpha;
			float MaxDownSampleLevel;
		};
		auto updateData = new FUIBackgroundBlurUpdateOthersData();
		updateData->BlurStrengthWithAlpha = this->GetBlurStrengthInternal();
		updateData->MaxDownSampleLevel = this->MaxDownSampleLevel;
		ENQUEUE_RENDER_COMMAND(FLexBackgroundBlur_UpdateData)
			([BackgroundBlurRenderProxy, updateData](FRHICommandListImmediate& RHICmdList)
			{
				BackgroundBlurRenderProxy->MaxDownSampleLevel = updateData->MaxDownSampleLevel;
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
		GetWidget()->MarkCanvasUpdate(false, false, false);
		SendOthersDataToRenderProxy();
	}
}

void ULexBackgroundBlur::SetApplyAlphaToBlur(bool Value)
{
	if (ApplyAlphaToBlur != Value)
	{
		ApplyAlphaToBlur = Value;
		GetWidget()->MarkCanvasUpdate(false, false, false);
		SendOthersDataToRenderProxy();
	}
}

void ULexBackgroundBlur::SetMaxDownSampleLevel(int Value)
{
	if (MaxDownSampleLevel != Value)
	{
		MaxDownSampleLevel = Value;
		GetWidget()->MarkCanvasUpdate(false, false, false);
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

FLexVisualPostProcessRenderProxy* ULexBackgroundBlur::GetRenderProxy()
{
	if (RenderProxy == nullptr)
	{
		RenderProxy = new FUIBackgroundBlurRenderProxy();
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
	if (RenderProxy != nullptr)
	{
		auto BackgroundBlurRenderProxy = (FUIBackgroundBlurRenderProxy*)RenderProxy;
		auto blurStrengthWithAlpha = this->GetBlurStrengthInternal();
		ENQUEUE_RENDER_COMMAND(FLexBackgroundBlur_UpdateData)
			([BackgroundBlurRenderProxy, blurStrengthWithAlpha](FRHICommandListImmediate& RHICmdList)
				{
					BackgroundBlurRenderProxy->BlurStrength = blurStrengthWithAlpha;
				});
	}
}
