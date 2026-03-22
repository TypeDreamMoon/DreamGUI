// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "ShaderParameterUtils.h"

class FLexUIHelperGizmoShader :public FGlobalShader
{
public:
	FLexUIHelperGizmoShader() {}
	FLexUIHelperGizmoShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
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


class FLexUIHelperGizmoShaderVS :public FLexUIHelperGizmoShader
{
	DECLARE_SHADER_TYPE(FLexUIHelperGizmoShaderVS, Global);
public:
	FLexUIHelperGizmoShaderVS() {}
	FLexUIHelperGizmoShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIHelperGizmoShader(Initializer)
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
class FLexUIHelperGizmoShaderPS :public FLexUIHelperGizmoShader
{
	DECLARE_SHADER_TYPE(FLexUIHelperGizmoShaderPS, Global);
public:
	FLexUIHelperGizmoShaderPS() {}
	FLexUIHelperGizmoShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FLexUIHelperGizmoShader(Initializer)
	{

	}
private:
};

