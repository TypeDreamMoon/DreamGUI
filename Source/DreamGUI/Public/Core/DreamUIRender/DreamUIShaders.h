// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "ShaderParameterUtils.h"
#include "ShaderParameterStruct.h"
#include "Engine/Texture2D.h"
#include "MeshMaterialShader.h"
#include "RHIStaticStates.h"

// Uniform Buffer Declaration for Metal Shader Compilation
// Using BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT to properly bind textures/samplers
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDreamUIWorldRenderDepthTexUB, )
	SHADER_PARAMETER_TEXTURE(Texture2D, _SceneDepthTex)
	SHADER_PARAMETER_SAMPLER(SamplerState, _SceneDepthTexSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

class FDreamUIScreenRenderVS :public FMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FDreamUIScreenRenderVS, Material);

	FDreamUIScreenRenderVS() {}
	FDreamUIScreenRenderVS(const FMaterialShaderType::CompiledShaderInitializerType& Initializer);

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters);
	
	void SetMaterialShaderParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial* Material, const TUniformBuffer<FPrimitiveUniformShaderParameters>* PrimitiveUniformBuffer);
};
class FDreamUIScreenRenderPS : public FMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FDreamUIScreenRenderPS, Material);

	FDreamUIScreenRenderPS() {}
	FDreamUIScreenRenderPS(const FMaterialShaderType::CompiledShaderInitializerType& Initializer);
	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	void SetMaterialShaderParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial* Material, const TUniformBuffer<FPrimitiveUniformShaderParameters>* PrimitiveUniformBuffer);
	void SetGammaValue(FRHICommandList& RHICmdList, float value);
private:
	LAYOUT_FIELD(FShaderParameter, DreamUIGammaValuesParameter);
};

class FDreamUIWorldRenderPS : public FDreamUIScreenRenderPS
{
public:
	DECLARE_SHADER_TYPE(FDreamUIWorldRenderPS, Material);

	FDreamUIWorldRenderPS() {}
	FDreamUIWorldRenderPS(const FMaterialShaderType::CompiledShaderInitializerType& Initializer);
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	void SetDepthBlendParameter(FRHICommandList& RHICmdList, float DepthBlend, const FVector4f& DepthTextureScaleOffset, FRHITexture* DepthTexture, FRHISamplerState* DepthTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
private:
	LAYOUT_FIELD(FShaderParameter, SceneDepthTextureScaleOffsetParameter);
	LAYOUT_FIELD(FShaderParameter, SceneDepthBlendParameter);
};

class FDreamUIWorldRenderDepthFadePS : public FDreamUIWorldRenderPS
{
public:
	DECLARE_SHADER_TYPE(FDreamUIWorldRenderDepthFadePS, Material);

	FDreamUIWorldRenderDepthFadePS() {}
	FDreamUIWorldRenderDepthFadePS(const FMaterialShaderType::CompiledShaderInitializerType& Initializer);
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	void SetDepthFadeParameter(FRHICommandList& RHICmdList, int DepthFade);
private:
	LAYOUT_FIELD(FShaderParameter, SceneDepthFadeParameter);
};
