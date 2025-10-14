// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "ShaderParameterUtils.h"
#include "MaterialShaderType.h"
#include "Engine/Texture2D.h"

class FLexUIHelperLineShader :public FGlobalShader
{
public:
	FLexUIHelperLineShader() {}
	FLexUIHelperLineShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
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


class FLexUIHelperLineShaderVS :public FLexUIHelperLineShader
{
	DECLARE_SHADER_TYPE(FLexUIHelperLineShaderVS, Global);
public:
	FLexUIHelperLineShaderVS() {}
	FLexUIHelperLineShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIHelperLineShader(Initializer)
	{
		MVPParameter.Bind(Initializer.ParameterMap, TEXT("_MVP"));
	}
	void SetParameters(FRHICommandListImmediate& RHICmdList, const FMatrix44f& MVP)
	{
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetShaderValue(BatchedParameters, MVPParameter, MVP);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundVertexShader(), BatchedParameters);
	}
private:
	LAYOUT_FIELD(FShaderParameter, MVPParameter);
};
class FLexUIHelperLineShaderPS :public FLexUIHelperLineShader
{
	DECLARE_SHADER_TYPE(FLexUIHelperLineShaderPS, Global);
public:
	FLexUIHelperLineShaderPS() {}
	FLexUIHelperLineShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIHelperLineShader(Initializer)
	{

	}
private:
};

