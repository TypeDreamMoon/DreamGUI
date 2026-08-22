// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RHIStaticStates.h"

/**
 * What a built-in draw needs beyond its vertices: the textures the material used to carry as
 * parameters, and the font atlas's field geometry for the MTSDF decode. Game-thread side holds
 * texture resources (stable pointers); the render thread reads their RHI at draw time.
 */
struct FDreamUIBuiltInDrawParams
{
	bool bEnabled = false;
	class FTextureResource* MainTexture = nullptr;
	class FTextureResource* FontTexture = nullptr;
	class FTextureResource* WidgetDataTexture = nullptr;
	class FTextureResource* ClipDataTexture = nullptr;
	/** Atlas slice size in texels. */
	FVector2f FontAtlasSize = FVector2f(1.0f, 1.0f);
	/** Distance-field range in texels (twice the spread); 0 for non-field atlases. */
	float FontFieldRangeTexels = 0.0f;
	/** Texels per em at the atlas's sample size. */
	float FontEmTexels = 0.0f;
};

/** Vertex shader of the built-in UI pass: the full DreamGUI vertex, model and model-view-projection. */
class DREAMGUI_API FDreamUIBaseVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIBaseVS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIBaseVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, DreamUI_MVP)
		SHADER_PARAMETER(FMatrix44f, DreamUI_M)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

/**
 * Pixel shader of the built-in UI pass. Permutations: depth blend against the scene (world-space
 * canvases) and its multi-sample depth fade, the same two the material-based path has.
 */
class DREAMGUI_API FDreamUIBasePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIBasePS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIBasePS, FGlobalShader);

	class FBlendDepth : SHADER_PERMUTATION_BOOL("LEXUI_BLEND_DEPTH");
	class FDepthFade : SHADER_PERMUTATION_BOOL("LEXUI_DEPTH_FADE");
	using FPermutationDomain = TShaderPermutationDomain<FBlendDepth, FDepthFade>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, DreamUI_InvM)
		SHADER_PARAMETER(FVector4f, DreamUI_GammaValues)
		SHADER_PARAMETER(FVector4f, DreamUI_FontAtlasInfo)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_MainTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_MainTexSampler)
		SHADER_PARAMETER_TEXTURE(Texture2DArray, DreamUI_FontTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_FontTexSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_WidgetDataTex)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_ClipDataTex)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_SceneDepthTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_SceneDepthTexSampler)
		SHADER_PARAMETER(FVector4f, DreamUI_SceneDepthTextureScaleOffset)
		SHADER_PARAMETER(float, DreamUI_SceneDepthBlend)
		SHADER_PARAMETER(int32, DreamUI_SceneDepthFade)
		SHADER_PARAMETER(FVector2f, DreamUI_ViewSizeInv)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		// Depth fade only means something when blending against depth.
		if (PermutationVector.Get<FDepthFade>() && !PermutationVector.Get<FBlendDepth>())
		{
			return false;
		}
		return true;
	}
};
