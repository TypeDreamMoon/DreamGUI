// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "ShaderParameterUtils.h"
#include "MaterialShaderType.h"
#include "Engine/Texture2D.h"
#include "RHIStaticStates.h"

class FLexUIPostProcessShader :public FGlobalShader
{
public:
	FLexUIPostProcessShader() {}
	FLexUIPostProcessShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
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
class FLexUISimplePostProcessVS :public FLexUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FLexUISimplePostProcessVS, Global);
public:
	FLexUISimplePostProcessVS() {}
	FLexUISimplePostProcessVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIPostProcessShader(Initializer)
	{

	}
	void SetParameters(FRHICommandListImmediate& RHICmdList)
	{

	}
private:
};
class FLexUISimpleCopyTargetPS :public FLexUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FLexUISimpleCopyTargetPS, Global);
public:
	FLexUISimpleCopyTargetPS() {}
	FLexUISimpleCopyTargetPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIPostProcessShader(Initializer)
	{
		MainTextureParameter.Bind(Initializer.ParameterMap, TEXT("_MainTex"));
		MainTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("_MainTexSampler"));
	}
	void SetParameters(FRHICommandListImmediate& RHICmdList, FTextureRHIRef SceneTexture, FRHISamplerState* SceneTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI())
	{
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), MainTextureParameter, MainTextureSamplerParameter, SceneTextureSampler, SceneTexture);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, MainTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, MainTextureSamplerParameter);
};
class FLexUISimpleCopyTargetPS_ColorCorrect : public FLexUISimpleCopyTargetPS
{
	DECLARE_SHADER_TYPE(FLexUISimpleCopyTargetPS_ColorCorrect, Global);
public:
	FLexUISimpleCopyTargetPS_ColorCorrect() {}
	FLexUISimpleCopyTargetPS_ColorCorrect(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUISimpleCopyTargetPS(Initializer)
	{
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_COLORCORRECT"), true);
		FLexUISimpleCopyTargetPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
private:
};
class FLexUIPostProcessGaussianBlurPS :public FLexUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FLexUIPostProcessGaussianBlurPS, Global);
public:
	FLexUIPostProcessGaussianBlurPS() {}
	FLexUIPostProcessGaussianBlurPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIPostProcessShader(Initializer)
	{
		MainTextureParameter.Bind(Initializer.ParameterMap, TEXT("_MainTex"));
		MainTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("_MainTexSampler"));
		BlurStrengthParameter.Bind(Initializer.ParameterMap, TEXT("_BlurStrength"));
	}
	void SetMainTexture(FRHICommandListImmediate& RHICmdList, FTextureRHIRef MainTexture, FRHISamplerState* MainTextureSampler)
	{
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), MainTextureParameter, MainTextureSamplerParameter, MainTextureSampler, MainTexture);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLexUIPostProcessShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetBlurStrength(FRHICommandListImmediate& RHICmdList, const FVector2f& BlurStrength)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetShaderValue(BatchedParameters, BlurStrengthParameter, BlurStrength);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, MainTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, MainTextureSamplerParameter);
	LAYOUT_FIELD(FShaderParameter, BlurStrengthParameter);
};
class FLexUIPostProcessGaussianBlurWithStrengthTexturePS :public FLexUIPostProcessGaussianBlurPS
{
	DECLARE_SHADER_TYPE(FLexUIPostProcessGaussianBlurWithStrengthTexturePS, Global);
public:
	FLexUIPostProcessGaussianBlurWithStrengthTexturePS() {}
	FLexUIPostProcessGaussianBlurWithStrengthTexturePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIPostProcessGaussianBlurPS(Initializer)
	{
		StrengthTextureParameter.Bind(Initializer.ParameterMap, TEXT("_StrengthTex"));
		StrengthTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("_StrengthTexSampler"));
	}
	void SetStrengthTexture(FRHICommandListImmediate& RHICmdList, FTextureRHIRef StrengthTexture, FRHISamplerState* StrengthTextureSampler)
	{
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), StrengthTextureParameter, StrengthTextureSamplerParameter, StrengthTextureSampler, StrengthTexture);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("USE_STRENGTH_TEXTURE"), 1);
		FLexUIPostProcessGaussianBlurPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, StrengthTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, StrengthTextureSamplerParameter);
};





//render mesh region 
class FLexUICopyMeshRegionVS :public FLexUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FLexUICopyMeshRegionVS, Global);
public:
	FLexUICopyMeshRegionVS() {}
	FLexUICopyMeshRegionVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIPostProcessShader(Initializer)
	{
		
	}
};

//render mesh pixel shader
class FLexUICopyMeshRegionPS :public FLexUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FLexUICopyMeshRegionPS, Global);
public:
	FLexUICopyMeshRegionPS() {}
	FLexUICopyMeshRegionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIPostProcessShader(Initializer)
	{
		MainTextureParameter.Bind(Initializer.ParameterMap, TEXT("_MainTex"));
		MainTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("_MainTexSampler"));
		MainTextureScaleOffsetParameter.Bind(Initializer.ParameterMap, TEXT("_MainTextureScaleOffset"));
		MVPParameter.Bind(Initializer.ParameterMap, TEXT("_MVP"));
	}
	void SetParameters(FRHICommandListImmediate& RHICmdList, const FMatrix44f& MVP, const FVector4f& MainTextureScaleOffset, FTextureRHIRef MainTexture, FRHISamplerState* MainTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI())
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(BatchedParameters, MainTextureParameter, MainTextureSamplerParameter, MainTextureSampler, MainTexture);
		SetShaderValue(BatchedParameters, MVPParameter, MVP);
		SetShaderValue(BatchedParameters, MainTextureScaleOffsetParameter, MainTextureScaleOffset);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, MainTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, MainTextureSamplerParameter);
	LAYOUT_FIELD(FShaderParameter, MainTextureScaleOffsetParameter);
	LAYOUT_FIELD(FShaderParameter, MVPParameter);
};
class FLexUICopyMeshRegionPS_ColorCorrect : public FLexUICopyMeshRegionPS
{
	DECLARE_SHADER_TYPE(FLexUICopyMeshRegionPS_ColorCorrect, Global);
public:
	FLexUICopyMeshRegionPS_ColorCorrect() {}
	FLexUICopyMeshRegionPS_ColorCorrect(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUICopyMeshRegionPS(Initializer)
	{
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_COLORCORRECT"), true);
		FLexUISimpleCopyTargetPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
private:
};




//common render mesh vertex shader
class FLexUIRenderMeshVS :public FLexUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FLexUIRenderMeshVS, Global);
public:
	FLexUIRenderMeshVS() {}
	FLexUIRenderMeshVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIPostProcessShader(Initializer)
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
class FLexUIRenderMeshWorldVS : public FLexUIRenderMeshVS
{
public:
	DECLARE_SHADER_TYPE(FLexUIRenderMeshWorldVS, Global);

	FLexUIRenderMeshWorldVS() {}
	FLexUIRenderMeshWorldVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIRenderMeshVS(Initializer)
	{

	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_BLEND_DEPTH"), true);
		FLexUIRenderMeshVS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
private:
};




//render mesh pixel shader
class FLexUIRenderMeshPS :public FLexUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FLexUIRenderMeshPS, Global);
public:
	FLexUIRenderMeshPS() {}
	FLexUIRenderMeshPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIPostProcessShader(Initializer)
	{
		MainTextureParameter.Bind(Initializer.ParameterMap, TEXT("_MainTex"));
		MainTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("_MainTexSampler"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_MASK"), 0);
		FLexUIPostProcessShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetParameters(FRHICommandListImmediate& RHICmdList, FTextureRHIRef MainTexture, FRHISamplerState* MainTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI())
	{
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), MainTextureParameter, MainTextureSamplerParameter, MainTextureSampler, MainTexture);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, MainTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, MainTextureSamplerParameter);
};

//render mesh pixel shader, use a mask texture
class FLexUIRenderMeshWithMaskPS :public FLexUIPostProcessShader
{
	DECLARE_SHADER_TYPE(FLexUIRenderMeshWithMaskPS, Global);
public:
	FLexUIRenderMeshWithMaskPS() {}
	FLexUIRenderMeshWithMaskPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIPostProcessShader(Initializer)
	{
		MainTextureParameter.Bind(Initializer.ParameterMap, TEXT("_MainTex"));
		MainTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("_MainTexSampler"));
		MaskTextureParameter.Bind(Initializer.ParameterMap, TEXT("_MaskTex"));
		MaskTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("_MaskTexSampler"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_MASK"), 1);
		FLexUIPostProcessShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetParameters(FRHICommandListImmediate& RHICmdList
		, FTextureRHIRef MainTexture
		, FTextureRHIRef MaskTexture
		, FRHISamplerState* MainTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI()
		, FRHISamplerState* MaskTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI()
	)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(BatchedParameters, MainTextureParameter, MainTextureSamplerParameter, MainTextureSampler, MainTexture);
		SetTextureParameter(BatchedParameters, MaskTextureParameter, MaskTextureSamplerParameter, MaskTextureSampler, MaskTexture);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, MainTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, MainTextureSamplerParameter);
	LAYOUT_FIELD(FShaderResourceParameter, MaskTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, MaskTextureSamplerParameter);
};

#pragma region Clip
//render mesh pixel shader
class FLexUIRenderMeshPS_Clip :public FLexUIRenderMeshPS
{
	DECLARE_SHADER_TYPE(FLexUIRenderMeshPS_Clip, Global);
public:
	FLexUIRenderMeshPS_Clip() {}
	FLexUIRenderMeshPS_Clip(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIRenderMeshPS(Initializer)
	{
		ClipDataTexParameter.Bind(Initializer.ParameterMap, TEXT("_ClipDataTex"));
		ClipDataTexSamplerParameter.Bind(Initializer.ParameterMap, TEXT("_ClipDataTexSampler"));
		InvMParameter.Bind(Initializer.ParameterMap, TEXT("_Inv_M"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_CLIP"), true);
		FLexUIRenderMeshPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetClipParameters(FRHICommandListImmediate& RHICmdList
		, const FMatrix44f& InvM
		, FTextureRHIRef ClipTexture
		, FRHISamplerState* ClipTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI())
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(BatchedParameters, ClipDataTexParameter, ClipDataTexSamplerParameter, ClipTextureSampler, ClipTexture);
		SetShaderValue(BatchedParameters, InvMParameter, InvM);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, ClipDataTexParameter);
	LAYOUT_FIELD(FShaderResourceParameter, ClipDataTexSamplerParameter);
	LAYOUT_FIELD(FShaderParameter, InvMParameter);
};
class FLexUIRenderMeshWorldPS_Clip : public FLexUIRenderMeshPS_Clip
{
public:
	DECLARE_SHADER_TYPE(FLexUIRenderMeshWorldPS_Clip, Global);

	FLexUIRenderMeshWorldPS_Clip() {}
	FLexUIRenderMeshWorldPS_Clip(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIRenderMeshPS_Clip(Initializer)
	{
		SceneDepthTextureParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthTex"));
		SceneDepthTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthTexSampler"));
		SceneDepthTextureScaleOffsetParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthTextureScaleOffset"));
		SceneDepthBlendParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthBlend"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_BLEND_DEPTH"), true);
		FLexUIRenderMeshPS_Clip::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetDepthBlendParameter(FRHICommandList& RHICmdList, float DepthBlend, const FVector4f& DepthTextureScaleOffset, FRHITexture* DepthTexture, FRHISamplerState* DepthTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI())
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(BatchedParameters, SceneDepthTextureParameter, SceneDepthTextureSamplerParameter, DepthTextureSampler, DepthTexture);
		SetShaderValue(BatchedParameters, SceneDepthBlendParameter, DepthBlend);
		SetShaderValue(BatchedParameters, SceneDepthTextureScaleOffsetParameter, DepthTextureScaleOffset);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, SceneDepthTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, SceneDepthTextureSamplerParameter);
	LAYOUT_FIELD(FShaderParameter, SceneDepthTextureScaleOffsetParameter);
	LAYOUT_FIELD(FShaderParameter, SceneDepthBlendParameter);
};
class FLexUIRenderMeshWorldDepthFadePS_Clip : public FLexUIRenderMeshWorldPS_Clip
{
public:
	DECLARE_SHADER_TYPE(FLexUIRenderMeshWorldDepthFadePS_Clip, Global);

	FLexUIRenderMeshWorldDepthFadePS_Clip() {}
	FLexUIRenderMeshWorldDepthFadePS_Clip(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIRenderMeshWorldPS_Clip(Initializer)
	{
		SceneDepthFadeParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthFade"));
		ViewSizeInvParameter.Bind(Initializer.ParameterMap, TEXT("_ViewSizeInv"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_DEPTH_FADE"), true);
		FLexUIRenderMeshWorldPS_Clip::ModifyCompilationEnvironment(Parameters, OutEnvironment);
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
class FLexUIRenderMeshWithMaskPS_Clip :public FLexUIRenderMeshWithMaskPS
{
	DECLARE_SHADER_TYPE(FLexUIRenderMeshWithMaskPS_Clip, Global);
public:
	FLexUIRenderMeshWithMaskPS_Clip() {}
	FLexUIRenderMeshWithMaskPS_Clip(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIRenderMeshWithMaskPS(Initializer)
	{
		ClipDataTexParameter.Bind(Initializer.ParameterMap, TEXT("_ClipDataTex"));
		ClipDataTexSamplerParameter.Bind(Initializer.ParameterMap, TEXT("_ClipDataTexSampler"));
		InvMParameter.Bind(Initializer.ParameterMap, TEXT("_Inv_M"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters & Parameters, FShaderCompilerEnvironment & OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_CLIP"), true);
		FLexUIRenderMeshWithMaskPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetClipParameters(FRHICommandListImmediate & RHICmdList
		, const FMatrix44f& InvM
		, FTextureRHIRef ClipTexture
		, FRHISamplerState * ClipTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI())
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(BatchedParameters, ClipDataTexParameter, ClipDataTexSamplerParameter, ClipTextureSampler, ClipTexture);
		SetShaderValue(BatchedParameters, InvMParameter, InvM);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, ClipDataTexParameter);
	LAYOUT_FIELD(FShaderResourceParameter, ClipDataTexSamplerParameter);
	LAYOUT_FIELD(FShaderParameter, InvMParameter);
};
class FLexUIRenderMeshWithMaskWorldPS_Clip : public FLexUIRenderMeshWithMaskPS_Clip
{
public:
	DECLARE_SHADER_TYPE(FLexUIRenderMeshWithMaskWorldPS_Clip, Global);

	FLexUIRenderMeshWithMaskWorldPS_Clip() {}
	FLexUIRenderMeshWithMaskWorldPS_Clip(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIRenderMeshWithMaskPS_Clip(Initializer)
	{
		SceneDepthTextureParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthTex"));
		SceneDepthTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthTexSampler"));
		SceneDepthTextureScaleOffsetParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthTextureScaleOffset"));
		SceneDepthBlendParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthBlend"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_BLEND_DEPTH"), true);
		FLexUIRenderMeshWithMaskPS_Clip::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	void SetDepthBlendParameter(FRHICommandList& RHICmdList, float DepthBlend, const FVector4f& DepthTextureScaleOffset, FRHITexture* DepthTexture, FRHISamplerState* DepthTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI())
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(BatchedParameters, SceneDepthTextureParameter, SceneDepthTextureSamplerParameter, DepthTextureSampler, DepthTexture);
		SetShaderValue(BatchedParameters, SceneDepthBlendParameter, DepthBlend);
		SetShaderValue(BatchedParameters, SceneDepthTextureScaleOffsetParameter, DepthTextureScaleOffset);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderResourceParameter, SceneDepthTextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, SceneDepthTextureSamplerParameter);
	LAYOUT_FIELD(FShaderParameter, SceneDepthTextureScaleOffsetParameter);
	LAYOUT_FIELD(FShaderParameter, SceneDepthBlendParameter);
};
class FLexUIRenderMeshWithMaskWorldDepthFadePS_Clip : public FLexUIRenderMeshWithMaskWorldPS_Clip
{
public:
	DECLARE_SHADER_TYPE(FLexUIRenderMeshWithMaskWorldDepthFadePS_Clip, Global);

	FLexUIRenderMeshWithMaskWorldDepthFadePS_Clip() {}
	FLexUIRenderMeshWithMaskWorldDepthFadePS_Clip(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIRenderMeshWithMaskWorldPS_Clip(Initializer)
	{
		SceneDepthFadeParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthFade"));
		ViewSizeInvParameter.Bind(Initializer.ParameterMap, TEXT("_ViewSizeInv"));
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LEXUI_DEPTH_FADE"), true);
		FLexUIRenderMeshWithMaskWorldPS_Clip::ModifyCompilationEnvironment(Parameters, OutEnvironment);
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