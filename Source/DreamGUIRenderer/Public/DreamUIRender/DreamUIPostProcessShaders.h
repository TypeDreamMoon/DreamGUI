// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RHIStaticStates.h"

/**
 * DreamGUI's post-process shaders. Each takes its parameters from its parameter struct, set with SetShaderParameters:
 * a texture is a parameter of the shader itself, where each used to go up in a uniform buffer made for every draw. A
 * parameter a shader reads that its struct does not name is reported when the shader loads, and a texture it reads
 * left null when it is set -- a parameter a setter forgot used to go unnoticed.
 */

/** Vertex shader of a full-screen pass: the quad's positions and uvs, as they are. */
class DREAMGUIRENDERER_API FDreamUISimplePostProcessVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUISimplePostProcessVS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUISimplePostProcessVS, FGlobalShader);
	using FParameters = FEmptyShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

/**
 * Copies a texture over the whole target. Permutations: the copied colour linearized, and the copied alpha scaled by
 * BlendAlpha, for a copy blended over what the target holds.
 */
class DREAMGUIRENDERER_API FDreamUISimpleCopyTargetPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUISimpleCopyTargetPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUISimpleCopyTargetPS, FGlobalShader);

	class FColorCorrect : SHADER_PERMUTATION_BOOL("LEXUI_COLORCORRECT");
	class FBlendAlpha : SHADER_PERMUTATION_BOOL("LEXUI_BLENDALPHA");
	using FPermutationDomain = TShaderPermutationDomain<FColorCorrect, FBlendAlpha>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, MainTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, MainTexSampler)
		SHADER_PARAMETER(float, BlendAlpha)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// No copy both linearizes and blends.
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		return !(PermutationVector.Get<FColorCorrect>() && PermutationVector.Get<FBlendAlpha>());
	}
};

/** One direction of a separable Gaussian blur. BlurStrength is a texel along that direction, scaled by the blur's strength. */
class DREAMGUIRENDERER_API FDreamUIPostProcessGaussianBlurPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIPostProcessGaussianBlurPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIPostProcessGaussianBlurPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, MainTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, MainTexSampler)
		SHADER_PARAMETER(FVector2f, BlurStrength)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

/**
 * Pass 1 of the pixel sort: each texel scans its own run and writes WHERE IT IS GOING.
 *
 * Replaces an odd-even transposition that needed one pass per texel of travel. Computing the
 * destination directly is two passes instead of up to a hundred and produces an exact sort rather
 * than a partial one. The .usf mirrors namespace DreamPixelSort on the C++ side.
 */
class DREAMGUIRENDERER_API FDreamUIPostProcessPixelSortRankPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIPostProcessPixelSortRankPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIPostProcessPixelSortRankPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, MainTex)
		/** MUST be POINT -- the rank counts exact texels, not blended ones. */
		SHADER_PARAMETER_SAMPLER(SamplerState, MainTexSampler)
		SHADER_PARAMETER(FVector2f, RegionSize)
		SHADER_PARAMETER(FVector2f, Band)
		SHADER_PARAMETER(float, SortAxis)
		SHADER_PARAMETER(float, SortKey)
		SHADER_PARAMETER(float, Descending)
		SHADER_PARAMETER(float, SearchRadius)
		SHADER_PARAMETER(FVector4f, IntervalParams)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

/** Pass 2: inverts pass 1's "where I am going" into "who comes here", which is all a PS can do. */
class DREAMGUIRENDERER_API FDreamUIPostProcessPixelSortGatherPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIPostProcessPixelSortGatherPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIPostProcessPixelSortGatherPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, MainTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, MainTexSampler)
		/** Pass 1's result: where each texel is going. */
		SHADER_PARAMETER_TEXTURE(Texture2D, DestinationTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, DestinationTexSampler)
		SHADER_PARAMETER(FVector2f, RegionSize)
		SHADER_PARAMETER(float, SortAxis)
		SHADER_PARAMETER(float, SearchRadius)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

/** Vertex shader of a copy of the screen under a mesh: each vertex where it is on the target, and its local position. */
class DREAMGUIRENDERER_API FDreamUICopyMeshRegionVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUICopyMeshRegionVS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUICopyMeshRegionVS, FGlobalShader);
	using FParameters = FEmptyShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

/**
 * Copies what is on the screen under a mesh: each pixel takes its local position to clip space with LocalToClip and
 * samples the screen there. Permutation: the copied colour linearized.
 */
class DREAMGUIRENDERER_API FDreamUICopyMeshRegionPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUICopyMeshRegionPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUICopyMeshRegionPS, FGlobalShader);

	class FColorCorrect : SHADER_PERMUTATION_BOOL("LEXUI_COLORCORRECT");
	using FPermutationDomain = TShaderPermutationDomain<FColorCorrect>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, MainTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, MainTexSampler)
		SHADER_PARAMETER(FVector4f, MainTextureScaleOffset)
		SHADER_PARAMETER(FMatrix44f, LocalToClip)
		/** 1 when the canvas draws into a render target, whose alpha the copy keeps; 0 on the screen, where it is 1. */
		SHADER_PARAMETER(float, IsRenderTarget)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

/** Vertex shader of a mesh drawn with a post-process result. Permutation: its clip position passed on, for the depth blend. */
class DREAMGUIRENDERER_API FDreamUIRenderMeshVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIRenderMeshVS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIRenderMeshVS, FGlobalShader);

	class FBlendDepth : SHADER_PERMUTATION_BOOL("LEXUI_BLEND_DEPTH");
	using FPermutationDomain = TShaderPermutationDomain<FBlendDepth>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, LocalToClip)
		SHADER_PARAMETER(FMatrix44f, LocalToWorld)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

/**
 * Pixel shader of a mesh drawn with a post-process result: the result, tinted and clipped by the canvas's clip data.
 * Permutations: a mask texture whose red channel scales alpha; a blend against the scene's depth, for a world-space
 * canvas; and that blend's fade over neighbouring depths, which only means something with it.
 */
class DREAMGUIRENDERER_API FDreamUIRenderMeshPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIRenderMeshPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIRenderMeshPS, FGlobalShader);

	class FMask : SHADER_PERMUTATION_BOOL("LEXUI_MASK");
	class FBlendDepth : SHADER_PERMUTATION_BOOL("LEXUI_BLEND_DEPTH");
	class FDepthFade : SHADER_PERMUTATION_BOOL("LEXUI_DEPTH_FADE");
	using FPermutationDomain = TShaderPermutationDomain<FMask, FBlendDepth, FDepthFade>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, MainTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, MainTexSampler)
		/** RGB is the tint colour, A is its strength. Strength 0 leaves the source untouched in every mode. */
		SHADER_PARAMETER(FVector4f, TintColor)
		/** EDreamPostProcessTintMode: 0 Multiply, 1 Blend, 2 Additive. */
		SHADER_PARAMETER(int32, TintMode)
		SHADER_PARAMETER_TEXTURE(Texture2D, MaskTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, MaskTexSampler)
		/** The canvas's clip data. A black texture clips nothing: its first clip reads as the end of the list. */
		SHADER_PARAMETER_TEXTURE(Texture2D, ClipDataTex)
		SHADER_PARAMETER_TEXTURE(Texture2D, SceneDepthTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthTexSampler)
		SHADER_PARAMETER(FVector4f, SceneDepthTextureScaleOffset)
		SHADER_PARAMETER(float, SceneDepthBlend)
		SHADER_PARAMETER(int32, SceneDepthFade)
		SHADER_PARAMETER(FVector2f, ViewSizeInv)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// The fade only means something with the blend.
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		return !PermutationVector.Get<FDepthFade>() || PermutationVector.Get<FBlendDepth>();
	}
};
