// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIRender/DreamUIShaders.h"
#include "DreamGUI.h"
#include "PipelineStateCache.h"
#include "Materials/Material.h"
#include "ShaderParameterUtils.h"
#include "PrimitiveUniformShaderParameters.h"
#include "MeshBatch.h"
#include "MaterialDomain.h"

IMPLEMENT_MATERIAL_SHADER_TYPE(, FDreamUIScreenRenderVS, TEXT("/Plugin/DreamGUI/Private/DreamUIShader.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FDreamUIScreenRenderPS, TEXT("/Plugin/DreamGUI/Private/DreamUIShader.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FDreamUIWorldRenderPS, TEXT("/Plugin/DreamGUI/Private/DreamUIShader.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FDreamUIWorldRenderDepthFadePS, TEXT("/Plugin/DreamGUI/Private/DreamUIShader.usf"), TEXT("MainPS"), SF_Pixel);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDreamUIWorldRenderDepthTexUB, "DreamUIWorldRenderDepthTexUB");

FDreamUIScreenRenderVS::FDreamUIScreenRenderVS(const FMaterialShaderType::CompiledShaderInitializerType& Initializer)
	: FMaterialShader(Initializer)
{
	
}
bool FDreamUIScreenRenderVS::ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
{
	return
		(Parameters.MaterialParameters.MaterialDomain == MD_Surface && (Parameters.MaterialParameters.ShadingModels.CountShadingModels() == 1 && Parameters.MaterialParameters.ShadingModels.GetFirstShadingModel() == EMaterialShadingModel::MSM_Unlit))
		|| Parameters.MaterialParameters.MaterialDomain == MD_UI
		;
}
void FDreamUIScreenRenderVS::ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	//OutEnvironment.SetDefine(TEXT("NUM_CUSTOMIZED_UVS"), Material->GetNumCustomizedUVs());
	OutEnvironment.SetDefine(TEXT("HAS_PRIMITIVE_UNIFORM_BUFFER"), true);
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), false);
	OutEnvironment.SetDefine(TEXT("NEEDS_WORLD_POSITION_EXCLUDING_SHADER_OFFSETS"), true);
}
void FDreamUIScreenRenderVS::SetMaterialShaderParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial* Material, const TUniformBuffer<FPrimitiveUniformShaderParameters>* PrimitiveUniformBuffer)
{
	FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FPrimitiveUniformShaderParameters>(), *PrimitiveUniformBuffer);
	SetViewParameters(BatchedParameters, View, View.ViewUniformBuffer);
	RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundVertexShader(), BatchedParameters);
	FMaterialShader::SetParameters(RHICmdList, RHICmdList.GetBoundVertexShader(), MaterialRenderProxy, *Material, View);
}



FDreamUIScreenRenderPS::FDreamUIScreenRenderPS(const FMaterialShaderType::CompiledShaderInitializerType& Initializer)
	:FMaterialShader(Initializer)
{
	DreamUIGammaValuesParameter.Bind(Initializer.ParameterMap, TEXT("_DreamUIGammaValues"));
}
bool FDreamUIScreenRenderPS::ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
{
	return
		(Parameters.MaterialParameters.MaterialDomain == MD_Surface && (Parameters.MaterialParameters.ShadingModels.CountShadingModels() == 1 && Parameters.MaterialParameters.ShadingModels.GetFirstShadingModel() == EMaterialShadingModel::MSM_Unlit))
		|| Parameters.MaterialParameters.MaterialDomain == MD_UI
		;
}
void FDreamUIScreenRenderPS::ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	//OutEnvironment.SetDefine(TEXT("NUM_CUSTOMIZED_UVS"), Material->GetNumCustomizedUVs());
	OutEnvironment.SetDefine(TEXT("HAS_PRIMITIVE_UNIFORM_BUFFER"), true);
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), false);
	OutEnvironment.SetDefine(TEXT("NEEDS_WORLD_POSITION_EXCLUDING_SHADER_OFFSETS"), true);
}
void FDreamUIScreenRenderPS::SetMaterialShaderParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial* Material, const TUniformBuffer<FPrimitiveUniformShaderParameters>* PrimitiveUniformBuffer)
{
	FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FPrimitiveUniformShaderParameters>(), *PrimitiveUniformBuffer);
	SetViewParameters(BatchedParameters, View, View.ViewUniformBuffer);
	RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	FMaterialShader::SetParameters(RHICmdList, RHICmdList.GetBoundPixelShader(), MaterialRenderProxy, *Material, View);
}
void FDreamUIScreenRenderPS::SetGammaValue(FRHICommandList& RHICmdList, float value)
{
	FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	FVector4f GammaValues(2.2f / value, 1.0f / value, 0.0f, 0.0f);
	SetShaderValue(BatchedParameters, DreamUIGammaValuesParameter, GammaValues);
	RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
}


FDreamUIWorldRenderPS::FDreamUIWorldRenderPS(const FMaterialShaderType::CompiledShaderInitializerType& Initializer)
	:FDreamUIScreenRenderPS(Initializer)
{
	SceneDepthTextureScaleOffsetParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthTextureScaleOffset"));
	SceneDepthBlendParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthBlend"));
}
void FDreamUIWorldRenderPS::ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("LEXUI_BLEND_DEPTH"), true);
	FDreamUIScreenRenderPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
}
void FDreamUIWorldRenderPS::SetDepthBlendParameter(FRHICommandList& RHICmdList, float DepthBlend, const FVector4f& DepthTextureScaleOffset, FRHITexture* DepthTexture, FRHISamplerState* DepthTextureSampler)
{
	FDreamUIWorldRenderDepthTexUB UB;
	UB._SceneDepthTex = DepthTexture;
	UB._SceneDepthTexSampler = DepthTextureSampler;
	TUniformBufferRef<FDreamUIWorldRenderDepthTexUB> UniformBuffer = TUniformBufferRef<FDreamUIWorldRenderDepthTexUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
	FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FDreamUIWorldRenderDepthTexUB>(), UniformBuffer);
	SetShaderValue(BatchedParameters, SceneDepthBlendParameter, DepthBlend);
	SetShaderValue(BatchedParameters, SceneDepthTextureScaleOffsetParameter, DepthTextureScaleOffset);
	RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
}


FDreamUIWorldRenderDepthFadePS::FDreamUIWorldRenderDepthFadePS(const FMaterialShaderType::CompiledShaderInitializerType& Initializer)
	:FDreamUIWorldRenderPS(Initializer)
{
	SceneDepthFadeParameter.Bind(Initializer.ParameterMap, TEXT("_SceneDepthFade"));
}
void FDreamUIWorldRenderDepthFadePS::ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("LEXUI_DEPTH_FADE"), true);
	FDreamUIWorldRenderPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
}
void FDreamUIWorldRenderDepthFadePS::SetDepthFadeParameter(FRHICommandList& RHICmdList, int DepthFade)
{
	FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
	SetShaderValue(BatchedParameters, SceneDepthFadeParameter, DepthFade);
	RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
}
