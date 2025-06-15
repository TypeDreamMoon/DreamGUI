// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "ShaderParameterUtils.h"
#include "MaterialShaderType.h"
#include "MaterialShader.h"
#include "Engine/Texture2D.h"

class FLexUIResolveShaderVS :public FGlobalShader
{
	DECLARE_SHADER_TYPE(FLexUIResolveShaderVS, Global);
public:
	FLexUIResolveShaderVS() {}
	FLexUIResolveShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

class FLexUIResolveShader2xPS :public FGlobalShader
{
	DECLARE_SHADER_TYPE(FLexUIResolveShader2xPS, Global);
public:
	FLexUIResolveShader2xPS() {}
	FLexUIResolveShader2xPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
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
class FLexUIResolveShader4xPS :public FGlobalShader
{
	DECLARE_SHADER_TYPE(FLexUIResolveShader4xPS, Global);
public:
	FLexUIResolveShader4xPS() {}
	FLexUIResolveShader4xPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
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
class FLexUIResolveShader8xPS :public FGlobalShader
{
	DECLARE_SHADER_TYPE(FLexUIResolveShader8xPS, Global);
public:
	FLexUIResolveShader8xPS() {}
	FLexUIResolveShader8xPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
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
