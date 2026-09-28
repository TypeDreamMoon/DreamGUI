// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "ShaderParameterUtils.h"
#include "MaterialShaderType.h"
#include "MaterialShader.h"
#include "Engine/Texture2D.h"

class FDreamUIResolveShaderVS :public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDreamUIResolveShaderVS, Global);
public:
	FDreamUIResolveShaderVS() {}
	FDreamUIResolveShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

class FDreamUIResolveShader2xPS :public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDreamUIResolveShader2xPS, Global);
public:
	FDreamUIResolveShader2xPS() {}
	FDreamUIResolveShader2xPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Tex.Bind(Initializer.ParameterMap, TEXT("Tex"), SPF_Mandatory);
	}
	void SetParameters(FRHICommandList& RHICmdList, FRHITexture* Texture2DMS)
	{
		auto& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(BatchedParameters, Tex, Texture2DMS);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("LEXUI_RESOLVE_2X"), 1);
	}
protected:
	LAYOUT_FIELD(FShaderResourceParameter, Tex);
};
class FDreamUIResolveShader4xPS :public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDreamUIResolveShader4xPS, Global);
public:
	FDreamUIResolveShader4xPS() {}
	FDreamUIResolveShader4xPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Tex.Bind(Initializer.ParameterMap, TEXT("Tex"), SPF_Mandatory);
	}
	void SetParameters(FRHICommandList& RHICmdList, FRHITexture* Texture2DMS)
	{
		auto& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(BatchedParameters, Tex, Texture2DMS);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("LEXUI_RESOLVE_4X"), 1);
	}
protected:
	LAYOUT_FIELD(FShaderResourceParameter, Tex);
};
class FDreamUIResolveShader8xPS :public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDreamUIResolveShader8xPS, Global);
public:
	FDreamUIResolveShader8xPS() {}
	FDreamUIResolveShader8xPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Tex.Bind(Initializer.ParameterMap, TEXT("Tex"), SPF_Mandatory);
	}
	void SetParameters(FRHICommandList& RHICmdList, FRHITexture* Texture2DMS)
	{
		auto& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetTextureParameter(BatchedParameters, Tex, Texture2DMS);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundPixelShader(), BatchedParameters);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("LEXUI_RESOLVE_8X"), 1);
	}
protected:
	LAYOUT_FIELD(FShaderResourceParameter, Tex);
};
