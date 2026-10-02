// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RHIStaticStates.h"
#include "RHITextureReference.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "DreamUIRender/DreamUIBlendMode.h"

class UTexture;

/**
 * The textures of a built-in draw on their way to the render thread: taken on the game thread just before the render
 * command that carries them is enqueued, and read on the render thread by that command alone. The textures are alive
 * when it is enqueued, and whatever the collector enqueues to release them comes after it, so reading them there is
 * safe -- and nowhere else is.
 */
struct FDreamUIBuiltInTextures
{
	const UTexture* Main = nullptr;
	const UTexture* Font = nullptr;
	const UTexture* WidgetData = nullptr;
	const UTexture* ClipData = nullptr;
	const UTexture* RenderLayerTable = nullptr;
	const UTexture* RectBlockData = nullptr;
};

/**
 * What a built-in draw needs beyond its vertices: the textures the material used to carry as parameters, and the font
 * atlas's field geometry for the MTSDF decode.
 *
 * Two halves. The game thread names the textures (weakly: a section that waits in a pool keeps its parameters, and a
 * texture can be collected meanwhile). The render thread binds each texture's reference and sampler, taken from the
 * texture by the command that delivers these (ResolveTextures_RenderThread). A reference follows its texture through
 * every rebuild of the texture's resource -- a font atlas repacked, a data texture grown, an UpdateResource -- and
 * outlives the texture, pointing at a black one once it is gone. The resource pointers they replace were kept across
 * frames and left dangling by each of those until the next rebuild of the draw call, and the sampler, fixed to a
 * bilinear one, drew a texture filtered nearest soft where the material drew it sharp.
 *
 * A section a material draws carries them too, with bEnabled off: its vertex shader reads the widget data and the render
 * layer table as the built-in one does (DreamUIRenderLayer.ush), and nothing else here.
 */
struct FDreamUIBuiltInDrawParams
{
	bool bEnabled = false;
	/** Game thread: the textures, as the canvas names them. Never read on the render thread. */
	TWeakObjectPtr<const UTexture> MainTexture;
	TWeakObjectPtr<const UTexture> FontTexture;
	TWeakObjectPtr<const UTexture> WidgetDataTexture;
	TWeakObjectPtr<const UTexture> ClipDataTexture;
	/** The world's render layer table, which a render layer's vertices are placed on the canvas through; none without layers. */
	TWeakObjectPtr<const UTexture> RenderLayerTable;
	/**
	 * The world's rows of the default rect block data, which a rect block the built-in shader draws reads its shape
	 * from (UDreamRectBlock::IsDrawnByBuiltInShader): bound with every built-in draw of a world that has them, so that
	 * the draws of a world of panels bind the same textures one after another.
	 */
	TWeakObjectPtr<const UTexture> RectBlockData;
	/** Render thread: what a draw binds. Null where there is no texture; the draw then binds a fallback. */
	FTextureReferenceRHIRef MainTextureRHI;
	FSamplerStateRHIRef MainSamplerRHI;
	FTextureReferenceRHIRef FontTextureRHI;
	FSamplerStateRHIRef FontSamplerRHI;
	FTextureReferenceRHIRef WidgetDataTextureRHI;
	FTextureReferenceRHIRef ClipDataTextureRHI;
	FTextureReferenceRHIRef RenderLayerTableRHI;
	FTextureReferenceRHIRef RectBlockDataRHI;
	/** Atlas slice size in texels. */
	FVector2f FontAtlasSize = FVector2f(1.0f, 1.0f);
	/** Distance-field range in texels (twice the spread); 0 for non-field atlases. */
	float FontFieldRangeTexels = 0.0f;
	/** Texels per em at the atlas's sample size; negative when the small-text correction is off (the shader reads the sign). */
	float FontEmTexels = 0.0f;
	/** How this draw composites. One per draw-call, because the blend state is set once per draw. */
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;

	/** Game thread: the textures, for a render command about to be enqueued. A texture that is gone is null. */
	DREAMGUIRENDERER_API FDreamUIBuiltInTextures GetTexturesForRenderCommand() const;
	/** Render thread, inside the command that carries InTextures: take each one's reference and sampler. */
	DREAMGUIRENDERER_API void ResolveTextures_RenderThread(const FDreamUIBuiltInTextures& InTextures);
};

/**
 * Vertex shader of the built-in UI pass: the full DreamGUI vertex, its model-view-projection, and what takes a render
 * layer's vertices into canvas space first -- the widget data, whose records say which row of the render layer table each
 * element is placed through, and the table (DreamUIRenderLayer.ush). A draw with no layers binds black textures to both:
 * every record then reads row 0, no layer.
 */
class DREAMGUIRENDERER_API FDreamUIBaseVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIBaseVS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIBaseVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, DreamUI_MVP)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_RenderLayerTable)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_RenderLayerWidgetData)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

/**
 * Pixel shader of the built-in UI pass. Permutations: depth blend against the scene (world-space
 * canvases) and its multi-sample depth fade, the same two the material-based path has.
 */
class DREAMGUIRENDERER_API FDreamUIBasePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIBasePS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIBasePS, FGlobalShader);

	class FBlendDepth : SHADER_PERMUTATION_BOOL("LEXUI_BLEND_DEPTH");
	class FDepthFade : SHADER_PERMUTATION_BOOL("LEXUI_DEPTH_FADE");
	/** Vertex colour only: the editor's widget outline frames, which have no widget record to read. */
	class FPlainColor : SHADER_PERMUTATION_BOOL("DREAMUI_PLAIN_COLOR");
	using FPermutationDomain = TShaderPermutationDomain<FBlendDepth, FDepthFade, FPlainColor>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4f, DreamUI_GammaValues)
		SHADER_PARAMETER(FVector4f, DreamUI_FontAtlasInfo)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_MainTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_MainTexSampler)
		SHADER_PARAMETER_TEXTURE(Texture2DArray, DreamUI_FontTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_FontTexSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_WidgetDataTex)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_ClipDataTex)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_RectBlockDataTex)
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
		// The plain-colour variant is editor helper geometry, which is never depth-blended.
		if (PermutationVector.Get<FPlainColor>() && PermutationVector.Get<FBlendDepth>())
		{
			return false;
		}
		return true;
	}
};
