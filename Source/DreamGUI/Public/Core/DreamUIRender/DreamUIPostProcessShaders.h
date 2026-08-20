// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "ShaderParameterUtils.h"
#include "MaterialShaderType.h"
#include "Engine/Texture2D.h"
#include "RHIStaticStates.h"

// Uniform Buffer Declarations for Metal Shader Compilation
// Using BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT to properly bind textures/samplers
// PostProcess shaders uniform buffers
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDreamUIPostProcessMainTexUB, )
	SHADER_PARAMETER_TEXTURE(Texture2D, _MainTex)
	SHADER_PARAMETER_SAMPLER(SamplerState, _MainTexSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

// RenderMesh shaders uniform buffers
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDreamUIRenderMeshMainTexUB, )
	SHADER_PARAMETER_TEXTURE(Texture2D, _MainTex)
	SHADER_PARAMETER_SAMPLER(SamplerState, _MainTexSampler)
	/** RGB is the tint colour, A is its strength. Strength 0 leaves the source untouched in every mode. */
	SHADER_PARAMETER(FVector4f, _TintColor)
	/** EDreamPostProcessTintMode: 0 Multiply, 1 Blend, 2 Additive. */
	SHADER_PARAMETER(int, _TintMode)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDreamUIRenderMeshMaskTexUB, )
	SHADER_PARAMETER_TEXTURE(Texture2D, _MaskTex)
	SHADER_PARAMETER_SAMPLER(SamplerState, _MaskTexSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDreamUIRenderMeshClipDataTexUB, )
	SHADER_PARAMETER_TEXTURE(Texture2D, _ClipDataTex)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDreamUIRenderMeshDepthTexUB, )
	SHADER_PARAMETER_TEXTURE(Texture2D, _SceneDepthTex)
	SHADER_PARAMETER_SAMPLER(SamplerState, _SceneDepthTexSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

class FDreamUIPostProcessShader :public FGlobalShader
{
public:
	FDreamUIPostProcessShader() {}
	FDreamUIPostProcessShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:FGlobalShader(Initializer)
	{

	}
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	static bool ShouldCache(EShaderPlatform Platform)
	{
		return true;
	}
};
class FDreamUISimplePostProcessVS :public FDreamUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FDreamUISimplePostProcessVS, Global);
public:
	FDreamUISimplePostProcessVS() {}
	FDreamUISimplePostProcessVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIPostProcessShader(Initializer)
	{

	}
	void SetParameters(FRHICommandListImmediate& RHICmdList)
	{

	}
private:
};
class FDreamUISimpleCopyTargetPS :public FDreamUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FDreamUISimpleCopyTargetPS, Global);
public:
	FDreamUISimpleCopyTargetPS() {}
	FDreamUISimpleCopyTargetPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIPostProcessShader(Initializer)
	{
	}
	void SetParameters(FRHICommandListImmediate& RHICmdList, FTextureRHIRef SceneTexture, FRHISamplerState* SceneTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI())
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		
		FDreamUIPostProcessMainTexUB UB;
		UB._MainTex = SceneTexture;
		UB._MainTexSampler = SceneTextureSampler;
		auto UniformBuffer = TUniformBufferRef<FDreamUIPostProcessMainTexUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
		SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FDreamUIPostProcessMainTexUB>(), UniformBuffer);
		
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
};
class FDreamUISimpleCopyTargetPS_ColorCorrect : public FDreamUISimpleCopyTargetPS
{
	DECLARE_SHADER_TYPE(FDreamUISimpleCopyTargetPS_ColorCorrect, Global);
public:
	FDreamUISimpleCopyTargetPS_ColorCorrect() {}
	FDreamUISimpleCopyTargetPS_ColorCorrect(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUISimpleCopyTargetPS(Initializer)
	{
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_COLORCORRECT"), true);
		FDreamUISimpleCopyTargetPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
private:
};
class FDreamUISimpleCopyTargetPS_BlendAlpha : public FDreamUISimpleCopyTargetPS
{
	DECLARE_SHADER_TYPE(FDreamUISimpleCopyTargetPS_BlendAlpha, Global);
public:
	FDreamUISimpleCopyTargetPS_BlendAlpha() {}
	FDreamUISimpleCopyTargetPS_BlendAlpha(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUISimpleCopyTargetPS(Initializer)
	{
		BlendAlphaParameter.Bind(Initializer.ParameterMap, TEXT("_BlendAlpha"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_BLENDALPHA"), true);
		FDreamUISimpleCopyTargetPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetBlendAlpha(FRHICommandListImmediate& RHICmdList, float BlendAlpha)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetShaderValue(BatchedParameters, BlendAlphaParameter, BlendAlpha);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderParameter, BlendAlphaParameter);
};
class FDreamUIPostProcessGaussianBlurPS :public FDreamUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FDreamUIPostProcessGaussianBlurPS, Global);
public:
	FDreamUIPostProcessGaussianBlurPS() {}
	FDreamUIPostProcessGaussianBlurPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIPostProcessShader(Initializer)
	{
		BlurStrengthParameter.Bind(Initializer.ParameterMap, TEXT("_BlurStrength"));
	}
	void SetMainTexture(FRHICommandListImmediate& RHICmdList, FTextureRHIRef MainTexture, FRHISamplerState* MainTextureSampler)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		
		FDreamUIPostProcessMainTexUB UB;
		UB._MainTex = MainTexture;
		UB._MainTexSampler = MainTextureSampler;
		auto UniformBuffer = TUniformBufferRef<FDreamUIPostProcessMainTexUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
		SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FDreamUIPostProcessMainTexUB>(), UniformBuffer);
		
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FDreamUIPostProcessShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetBlurStrength(FRHICommandListImmediate& RHICmdList, const FVector2f& BlurStrength)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetShaderValue(BatchedParameters, BlurStrengthParameter, BlurStrength);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderParameter, BlurStrengthParameter);
};

/**
 * One compare-exchange phase of an odd-even transposition pixel sort.
 *
 * Every parameter is pushed in a single call on purpose: they are all consumed by the same pass, and
 * splitting them into separate setters is how one of them ends up silently unbound after a later
 * edit. The .usf mirrors namespace DreamPixelSort on the C++ side.
 */
/**
 * Pass 1 of the pixel sort: each texel scans its own run and writes WHERE IT IS GOING.
 *
 * Replaces an odd-even transposition that needed one pass per texel of travel. Computing the
 * destination directly is two passes instead of up to a hundred and produces an exact sort rather
 * than a partial one.
 */
class FDreamUIPostProcessPixelSortRankPS :public FDreamUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FDreamUIPostProcessPixelSortRankPS, Global);
public:
	FDreamUIPostProcessPixelSortRankPS() {}
	FDreamUIPostProcessPixelSortRankPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIPostProcessShader(Initializer)
	{
		RegionSizeParameter.Bind(Initializer.ParameterMap, TEXT("_RegionSize"));
		BandParameter.Bind(Initializer.ParameterMap, TEXT("_Band"));
		SortAxisParameter.Bind(Initializer.ParameterMap, TEXT("_SortAxis"));
		SortKeyParameter.Bind(Initializer.ParameterMap, TEXT("_SortKey"));
		DescendingParameter.Bind(Initializer.ParameterMap, TEXT("_Descending"));
		SearchRadiusParameter.Bind(Initializer.ParameterMap, TEXT("_SearchRadius"));
		IntervalParamsParameter.Bind(Initializer.ParameterMap, TEXT("_IntervalParams"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FDreamUIPostProcessShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	/** @param MainTextureSampler MUST be POINT -- the rank counts exact texels, not blended ones. */
	void SetParameters(FRHICommandListImmediate& RHICmdList
		, FTextureRHIRef MainTexture, FRHISamplerState* MainTextureSampler
		, const FVector2f& InRegionSize, const FVector2f& InBand
		, float InSortAxis, float InSortKey, float InDescending, float InSearchRadius
		, const FVector4f& InIntervalParams)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		FDreamUIPostProcessMainTexUB UB;
		UB._MainTex = MainTexture;
		UB._MainTexSampler = MainTextureSampler;
		auto UniformBuffer = TUniformBufferRef<FDreamUIPostProcessMainTexUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
		SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FDreamUIPostProcessMainTexUB>(), UniformBuffer);
		SetShaderValue(BatchedParameters, RegionSizeParameter, InRegionSize);
		SetShaderValue(BatchedParameters, BandParameter, InBand);
		SetShaderValue(BatchedParameters, SortAxisParameter, InSortAxis);
		SetShaderValue(BatchedParameters, SortKeyParameter, InSortKey);
		SetShaderValue(BatchedParameters, DescendingParameter, InDescending);
		SetShaderValue(BatchedParameters, SearchRadiusParameter, InSearchRadius);
		SetShaderValue(BatchedParameters, IntervalParamsParameter, InIntervalParams);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderParameter, RegionSizeParameter);
	LAYOUT_FIELD(FShaderParameter, BandParameter);
	LAYOUT_FIELD(FShaderParameter, SortAxisParameter);
	LAYOUT_FIELD(FShaderParameter, SortKeyParameter);
	LAYOUT_FIELD(FShaderParameter, DescendingParameter);
	LAYOUT_FIELD(FShaderParameter, SearchRadiusParameter);
	LAYOUT_FIELD(FShaderParameter, IntervalParamsParameter);
};

/** Pass 2: inverts pass 1's "where I am going" into "who comes here", which is all a PS can do. */
class FDreamUIPostProcessPixelSortGatherPS :public FDreamUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FDreamUIPostProcessPixelSortGatherPS, Global);
public:
	FDreamUIPostProcessPixelSortGatherPS() {}
	FDreamUIPostProcessPixelSortGatherPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIPostProcessShader(Initializer)
	{
		RegionSizeParameter.Bind(Initializer.ParameterMap, TEXT("_RegionSize"));
		SortAxisParameter.Bind(Initializer.ParameterMap, TEXT("_SortAxis"));
		SearchRadiusParameter.Bind(Initializer.ParameterMap, TEXT("_SearchRadius"));
		DestinationTexParameter.Bind(Initializer.ParameterMap, TEXT("_DestinationTex"));
		DestinationTexSamplerParameter.Bind(Initializer.ParameterMap, TEXT("_DestinationTexSampler"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FDreamUIPostProcessShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetParameters(FRHICommandListImmediate& RHICmdList
		, FTextureRHIRef MainTexture, FRHISamplerState* MainTextureSampler
		, FTextureRHIRef DestinationTexture
		, const FVector2f& InRegionSize, float InSortAxis, float InSearchRadius)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		FDreamUIPostProcessMainTexUB UB;
		UB._MainTex = MainTexture;
		UB._MainTexSampler = MainTextureSampler;
		auto UniformBuffer = TUniformBufferRef<FDreamUIPostProcessMainTexUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
		SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FDreamUIPostProcessMainTexUB>(), UniformBuffer);
		SetTextureParameter(BatchedParameters, DestinationTexParameter, DestinationTexSamplerParameter, MainTextureSampler, DestinationTexture);
		SetShaderValue(BatchedParameters, RegionSizeParameter, InRegionSize);
		SetShaderValue(BatchedParameters, SortAxisParameter, InSortAxis);
		SetShaderValue(BatchedParameters, SearchRadiusParameter, InSearchRadius);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderParameter, RegionSizeParameter);
	LAYOUT_FIELD(FShaderParameter, SortAxisParameter);
	LAYOUT_FIELD(FShaderParameter, SearchRadiusParameter);
	LAYOUT_FIELD(FShaderResourceParameter, DestinationTexParameter);
	LAYOUT_FIELD(FShaderResourceParameter, DestinationTexSamplerParameter);
};




//render mesh region 
class FDreamUICopyMeshRegionVS :public FDreamUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FDreamUICopyMeshRegionVS, Global);
public:
	FDreamUICopyMeshRegionVS() {}
	FDreamUICopyMeshRegionVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIPostProcessShader(Initializer)
	{
		
	}
};

//render mesh pixel shader
class FDreamUICopyMeshRegionPS :public FDreamUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FDreamUICopyMeshRegionPS, Global);
public:
	FDreamUICopyMeshRegionPS() {}
	FDreamUICopyMeshRegionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIPostProcessShader(Initializer)
	{
		MainTextureScaleOffsetParameter.Bind(Initializer.ParameterMap, TEXT("_MainTextureScaleOffset"));
		MVPParameter.Bind(Initializer.ParameterMap, TEXT("_MVP"));
		IsRenderTargetParameter.Bind(Initializer.ParameterMap, TEXT("_IsRenderTarget"));
	}
	void SetParameters(FRHICommandListImmediate& RHICmdList, const FMatrix44f& MVP
		, bool bIsRenderTarget
		, FTextureRHIRef MainTexture, const FVector4f& MainTextureScaleOffset
		, FRHISamplerState* MainTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI()
		)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		
		FDreamUIPostProcessMainTexUB UB;
		UB._MainTex = MainTexture;
		UB._MainTexSampler = MainTextureSampler;
		auto UniformBuffer = TUniformBufferRef<FDreamUIPostProcessMainTexUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
		SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FDreamUIPostProcessMainTexUB>(), UniformBuffer);
		
		SetShaderValue(BatchedParameters, MVPParameter, MVP);
		SetShaderValue(BatchedParameters, MainTextureScaleOffsetParameter, MainTextureScaleOffset);
		SetShaderValue(BatchedParameters, IsRenderTargetParameter, bIsRenderTarget ? 1.0f : 0.0f);
		
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderParameter, MainTextureScaleOffsetParameter);
	LAYOUT_FIELD(FShaderParameter, MVPParameter);
	LAYOUT_FIELD(FShaderParameter, IsRenderTargetParameter);
};
class FDreamUICopyMeshRegionPS_ColorCorrect : public FDreamUICopyMeshRegionPS
{
	DECLARE_SHADER_TYPE(FDreamUICopyMeshRegionPS_ColorCorrect, Global);
public:
	FDreamUICopyMeshRegionPS_ColorCorrect() {}
	FDreamUICopyMeshRegionPS_ColorCorrect(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUICopyMeshRegionPS(Initializer)
	{
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_COLORCORRECT"), true);
		FDreamUICopyMeshRegionPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
private:
};




//common render mesh vertex shader
class FDreamUIRenderMeshVS :public FDreamUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FDreamUIRenderMeshVS, Global);
public:
	FDreamUIRenderMeshVS() {}
	FDreamUIRenderMeshVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIPostProcessShader(Initializer)
	{
		MVPParameter.Bind(Initializer.ParameterMap, TEXT("_MVP"));
		MParameter.Bind(Initializer.ParameterMap, TEXT("_M"));
	}
	void SetParameters(FRHICommandListImmediate& RHICmdList, const FMatrix44f& MVP, const FMatrix44f& M)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetShaderValue(BatchedParameters, MVPParameter, MVP);
		SetShaderValue(BatchedParameters, MParameter, M);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundVertexShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderParameter, MVPParameter);
	LAYOUT_FIELD(FShaderParameter, MParameter);
};
class FDreamUIRenderMeshWorldVS : public FDreamUIRenderMeshVS
{
public:
	DECLARE_SHADER_TYPE(FDreamUIRenderMeshWorldVS, Global);

	FDreamUIRenderMeshWorldVS() {}
	FDreamUIRenderMeshWorldVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIRenderMeshVS(Initializer)
	{

	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_BLEND_DEPTH"), true);
		FDreamUIRenderMeshVS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
private:
};




//render mesh pixel shader
class FDreamUIRenderMeshPS :public FDreamUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FDreamUIRenderMeshPS, Global);
public:
	FDreamUIRenderMeshPS() {}
	FDreamUIRenderMeshPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIPostProcessShader(Initializer)
	{
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_MASK"), 0);
		FDreamUIPostProcessShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetParameters(FRHICommandListImmediate& RHICmdList, FTextureRHIRef MainTexture, FRHISamplerState* MainTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI()
		, const FVector4f& TintColor = FVector4f(1, 1, 1, 1), int TintMode = 0)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();

		FDreamUIRenderMeshMainTexUB UB;
		UB._MainTex = MainTexture;
		UB._MainTexSampler = MainTextureSampler;
		UB._TintColor = TintColor;
		UB._TintMode = TintMode;
		auto UniformBuffer = TUniformBufferRef<FDreamUIRenderMeshMainTexUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
		SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FDreamUIRenderMeshMainTexUB>(), UniformBuffer);

		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
};

//render mesh pixel shader, use a mask texture
class FDreamUIRenderMeshWithMaskPS :public FDreamUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FDreamUIRenderMeshWithMaskPS, Global);
public:
	FDreamUIRenderMeshWithMaskPS() {}
	FDreamUIRenderMeshWithMaskPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIPostProcessShader(Initializer)
	{
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_MASK"), 1);
		FDreamUIPostProcessShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetParameters(FRHICommandListImmediate& RHICmdList
		, FTextureRHIRef MainTexture
		, FTextureRHIRef MaskTexture
		, FRHISamplerState* MainTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI()
		, FRHISamplerState* MaskTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI()
		, const FVector4f& TintColor = FVector4f(1, 1, 1, 1)
		, int TintMode = 0
	)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();

		{
			FDreamUIRenderMeshMainTexUB UB;
			UB._MainTex = MainTexture;
			UB._MainTexSampler = MainTextureSampler;
			UB._TintColor = TintColor;
			UB._TintMode = TintMode;
			auto UniformBuffer = TUniformBufferRef<FDreamUIRenderMeshMainTexUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
			SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FDreamUIRenderMeshMainTexUB>(), UniformBuffer);
		}

		{
			FDreamUIRenderMeshMaskTexUB UB;
			UB._MaskTex = MaskTexture;
			UB._MaskTexSampler = MaskTextureSampler;
			auto UniformBuffer = TUniformBufferRef<FDreamUIRenderMeshMaskTexUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
			SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FDreamUIRenderMeshMaskTexUB>(), UniformBuffer);
		}
		
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
};

#pragma region Clip
//render mesh pixel shader
class FDreamUIRenderMeshPS_Clip :public FDreamUIRenderMeshPS
{
	DECLARE_SHADER_TYPE(FDreamUIRenderMeshPS_Clip, Global);
public:
	FDreamUIRenderMeshPS_Clip() {}
	FDreamUIRenderMeshPS_Clip(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIRenderMeshPS(Initializer)
	{
		InvMParameter.Bind(Initializer.ParameterMap, TEXT("_Inv_M"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_CLIP"), true);
		FDreamUIRenderMeshPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetClipParameters(FRHICommandListImmediate& RHICmdList
		, const FMatrix44f& InvM
		, FTextureRHIRef ClipTexture)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();

		FDreamUIRenderMeshClipDataTexUB UB;
		UB._ClipDataTex = ClipTexture;
		auto UniformBuffer = TUniformBufferRef<FDreamUIRenderMeshClipDataTexUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
		SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FDreamUIRenderMeshClipDataTexUB>(), UniformBuffer);
		
		SetShaderValue(BatchedParameters, InvMParameter, InvM);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderParameter, InvMParameter);
};
class FDreamUIRenderMeshWorldPS_Clip : public FDreamUIRenderMeshPS_Clip
{
public:
	DECLARE_SHADER_TYPE(FDreamUIRenderMeshWorldPS_Clip, Global);

	FDreamUIRenderMeshWorldPS_Clip() {}
	FDreamUIRenderMeshWorldPS_Clip(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIRenderMeshPS_Clip(Initializer)
	{
		SceneDepthTextureScaleOffsetParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthTextureScaleOffset"));
		SceneDepthBlendParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthBlend"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_BLEND_DEPTH"), true);
		FDreamUIRenderMeshPS_Clip::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetDepthBlendParameter(FRHICommandList& RHICmdList, float DepthBlend, const FVector4f& DepthTextureScaleOffset, FRHITexture* DepthTexture, FRHISamplerState* DepthTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI())
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();

		FDreamUIRenderMeshDepthTexUB UB;
		UB._SceneDepthTex = DepthTexture;
		UB._SceneDepthTexSampler = DepthTextureSampler;
		auto UniformBuffer = TUniformBufferRef<FDreamUIRenderMeshDepthTexUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
		SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FDreamUIRenderMeshDepthTexUB>(), UniformBuffer);
		
		SetShaderValue(BatchedParameters, SceneDepthBlendParameter, DepthBlend);
		SetShaderValue(BatchedParameters, SceneDepthTextureScaleOffsetParameter, DepthTextureScaleOffset);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderParameter, SceneDepthTextureScaleOffsetParameter);
	LAYOUT_FIELD(FShaderParameter, SceneDepthBlendParameter);
};
class FDreamUIRenderMeshWorldDepthFadePS_Clip : public FDreamUIRenderMeshWorldPS_Clip
{
public:
	DECLARE_SHADER_TYPE(FDreamUIRenderMeshWorldDepthFadePS_Clip, Global);

	FDreamUIRenderMeshWorldDepthFadePS_Clip() {}
	FDreamUIRenderMeshWorldDepthFadePS_Clip(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIRenderMeshWorldPS_Clip(Initializer)
	{
		SceneDepthFadeParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthFade"));
		ViewSizeInvParameter.Bind(Initializer.ParameterMap, TEXT("_ViewSizeInv"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_DEPTH_FADE"), true);
		FDreamUIRenderMeshWorldPS_Clip::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetDepthFadeParameter(FRHICommandList& RHICmdList, int DepthFade, const FVector2f& ViewSizeInv)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetShaderValue(BatchedParameters, SceneDepthFadeParameter, DepthFade);
		SetShaderValue(BatchedParameters, ViewSizeInvParameter, ViewSizeInv);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderParameter, SceneDepthFadeParameter);
	LAYOUT_FIELD(FShaderParameter, ViewSizeInvParameter);
};
//render mesh pixel shader, use a mask texture
class FDreamUIRenderMeshWithMaskPS_Clip :public FDreamUIRenderMeshWithMaskPS
{
	DECLARE_SHADER_TYPE(FDreamUIRenderMeshWithMaskPS_Clip, Global);
public:
	FDreamUIRenderMeshWithMaskPS_Clip() {}
	FDreamUIRenderMeshWithMaskPS_Clip(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIRenderMeshWithMaskPS(Initializer)
	{
		InvMParameter.Bind(Initializer.ParameterMap, TEXT("_Inv_M"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters & Parameters, FShaderCompilerEnvironment & OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_CLIP"), true);
		FDreamUIRenderMeshWithMaskPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetClipParameters(FRHICommandListImmediate & RHICmdList
		, const FMatrix44f& InvM
		, FTextureRHIRef ClipTexture
		, FRHISamplerState * ClipTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI())
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();

		FDreamUIRenderMeshClipDataTexUB UB;
		UB._ClipDataTex = ClipTexture;
		auto UniformBuffer = TUniformBufferRef<FDreamUIRenderMeshClipDataTexUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
		SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FDreamUIRenderMeshClipDataTexUB>(), UniformBuffer);
		
		SetShaderValue(BatchedParameters, InvMParameter, InvM);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderParameter, InvMParameter);
};
class FDreamUIRenderMeshWithMaskWorldPS_Clip : public FDreamUIRenderMeshWithMaskPS_Clip
{
public:
	DECLARE_SHADER_TYPE(FDreamUIRenderMeshWithMaskWorldPS_Clip, Global);

	FDreamUIRenderMeshWithMaskWorldPS_Clip() {}
	FDreamUIRenderMeshWithMaskWorldPS_Clip(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIRenderMeshWithMaskPS_Clip(Initializer)
	{
		SceneDepthTextureScaleOffsetParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthTextureScaleOffset"));
		SceneDepthBlendParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthBlend"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_BLEND_DEPTH"), true);
		FDreamUIRenderMeshWithMaskPS_Clip::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetDepthBlendParameter(FRHICommandList& RHICmdList, float DepthBlend, const FVector4f& DepthTextureScaleOffset, FRHITexture* DepthTexture, FRHISamplerState* DepthTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI())
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		
		FDreamUIRenderMeshDepthTexUB UB;
		UB._SceneDepthTex = DepthTexture;
		UB._SceneDepthTexSampler = DepthTextureSampler;
		auto UniformBuffer = TUniformBufferRef<FDreamUIRenderMeshDepthTexUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
		SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FDreamUIRenderMeshDepthTexUB>(), UniformBuffer);
		
		SetShaderValue(BatchedParameters, SceneDepthBlendParameter, DepthBlend);
		SetShaderValue(BatchedParameters, SceneDepthTextureScaleOffsetParameter, DepthTextureScaleOffset);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderParameter, SceneDepthTextureScaleOffsetParameter);
	LAYOUT_FIELD(FShaderParameter, SceneDepthBlendParameter);
};
class FDreamUIRenderMeshWithMaskWorldDepthFadePS_Clip : public FDreamUIRenderMeshWithMaskWorldPS_Clip
{
public:
	DECLARE_SHADER_TYPE(FDreamUIRenderMeshWithMaskWorldDepthFadePS_Clip, Global);

	FDreamUIRenderMeshWithMaskWorldDepthFadePS_Clip() {}
	FDreamUIRenderMeshWithMaskWorldDepthFadePS_Clip(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FDreamUIRenderMeshWithMaskWorldPS_Clip(Initializer)
	{
		SceneDepthFadeParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthFade"));
		ViewSizeInvParameter.Bind(Initializer.ParameterMap, TEXT("_ViewSizeInv"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_DEPTH_FADE"), true);
		FDreamUIRenderMeshWithMaskWorldPS_Clip::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetDepthFadeParameter(FRHICommandList& RHICmdList, int DepthFade, const FVector2f& ViewSizeInv)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetShaderValue(BatchedParameters, SceneDepthFadeParameter, DepthFade);
		SetShaderValue(BatchedParameters, ViewSizeInvParameter, ViewSizeInv);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderParameter, SceneDepthFadeParameter);
	LAYOUT_FIELD(FShaderParameter, ViewSizeInvParameter);
};
#pragma endregion