// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

/** Vertex shader of the multi-sample resolve: one triangle over the whole target, made from the vertex index alone. */
class FDreamUIResolveShaderVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIResolveShaderVS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIResolveShaderVS, FGlobalShader);
	using FParameters = FEmptyShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

/** Each pixel of a multi-sampled target, the mean of its samples. Permutation: the sample count, 2, 4 or 8. */
class FDreamUIResolveShaderPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIResolveShaderPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIResolveShaderPS, FGlobalShader);

	class FSampleCount : SHADER_PERMUTATION_SPARSE_INT("LEXUI_RESOLVE_SAMPLES", 2, 4, 8);
	using FPermutationDomain = TShaderPermutationDomain<FSampleCount>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2DMS<float4>, Tex)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};
