// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIRender/DreamUIBackdropEffects.h"

#include "CommonRenderResources.h"
#include "Engine/Texture.h"
#include "GlobalRenderResources.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIResourceUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"
#include "DreamUIRender/DreamUICompositor.h"
#include "DreamUIRender/DreamUIPostProcessVertex.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "DreamUIRender/DreamVisualPostProcessRenderProxy.h"
#include "DreamUIBackdropEffectPasses.h"

// ---- the passes an image effect runs: DreamUIImageEffect.usf

class FDreamUIAdjustPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIAdjustPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIAdjustPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DreamUI_SourceTex)
		SHADER_PARAMETER(FVector4f, DreamUI_EffectFlags)
		SHADER_PARAMETER(FVector4f, DreamUI_EffectOrigin)
		SHADER_PARAMETER_ARRAY(FVector4f, DreamUI_AdjustRows, [DreamUIAdjustLayout::MaxOps * DreamUIAdjustLayout::RowsPerOp])
		SHADER_PARAMETER(int32, DreamUI_AdjustCount)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_AdjustTable)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_AdjustTableSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_AdjustLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_AdjustLutSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

class FDreamUIStylizePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIStylizePS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIStylizePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DreamUI_SourceTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_SourceSampler)
		SHADER_PARAMETER(FVector4f, DreamUI_EffectFlags)
		SHADER_PARAMETER(FVector4f, DreamUI_EffectOrigin)
		SHADER_PARAMETER_ARRAY(FVector4f, DreamUI_StylizeRows, [DreamUIStylizeLayout::RowsPerLayer])
		SHADER_PARAMETER(FVector4f, DreamUI_StylizeFrame)
		SHADER_PARAMETER(FVector4f, DreamUI_StylizeParams)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

class FDreamUIEffectMixPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIEffectMixPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIEffectMixPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DreamUI_SourceTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DreamUI_EffectTex)
		SHADER_PARAMETER(FVector4f, DreamUI_EffectFlags)
		SHADER_PARAMETER(FVector4f, DreamUI_EffectOrigin)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

class FDreamUIPyramidDownPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIPyramidDownPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIPyramidDownPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DreamUI_SourceTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_SourceSampler)
		SHADER_PARAMETER(FVector4f, DreamUI_FilterTexel)
		SHADER_PARAMETER(FVector4f, DreamUI_TargetTexel)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

class FDreamUIPyramidUpPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIPyramidUpPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIPyramidUpPS, FGlobalShader);
	using FParameters = FDreamUIPyramidDownPS::FParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

IMPLEMENT_GLOBAL_SHADER(FDreamUIAdjustPS, "/Plugin/DreamGUI/Private/DreamUIImageEffect.usf", "AdjustPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FDreamUIStylizePS, "/Plugin/DreamGUI/Private/DreamUIImageEffect.usf", "StylizePS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FDreamUIEffectMixPS, "/Plugin/DreamGUI/Private/DreamUIImageEffect.usf", "MixPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FDreamUIPyramidDownPS, "/Plugin/DreamGUI/Private/DreamUIImageEffect.usf", "DownPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FDreamUIPyramidUpPS, "/Plugin/DreamGUI/Private/DreamUIImageEffect.usf", "UpPS", SF_Pixel);

// ---- the element drawn back: DreamUIBackdropComposite.usf

class FDreamUIBackdropCompositeVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIBackdropCompositeVS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIBackdropCompositeVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, LocalToClip)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

class FDreamUIBackdropCompositePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIBackdropCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIBackdropCompositePS, FGlobalShader);

	class FEffect : SHADER_PERMUTATION_RANGE_INT("DREAMUI_BACKDROP_EFFECT", 0, 3);
	class FBlendDepth : SHADER_PERMUTATION_BOOL("LEXUI_BLEND_DEPTH");
	class FMask : SHADER_PERMUTATION_BOOL("LEXUI_MASK");
	using FPermutationDomain = TShaderPermutationDomain<FEffect, FBlendDepth, FMask>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_BackdropTex)
		SHADER_PARAMETER(FVector4f, DreamUI_BackdropOrigin)
		SHADER_PARAMETER(FVector4f, DreamUI_BackdropFlags)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_LevelSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_Level0)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_Level1)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_Level2)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_Level3)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_Level4)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_Level5)
		SHADER_PARAMETER_TEXTURE(Texture2D, ClipDataTex)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_MaskTex0)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_MaskTexSampler0)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_MaskTex1)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_MaskTexSampler1)
		SHADER_PARAMETER_TEXTURE(Texture2D, MaskTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, MaskTexSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, SceneDepthTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthTexSampler)
		SHADER_PARAMETER(FVector4f, SceneDepthTextureScaleOffset)
		SHADER_PARAMETER(float, SceneDepthBlend)
		SHADER_PARAMETER(int32, SceneDepthFade)
		SHADER_PARAMETER(FVector2f, ViewSizeInv)
		SHADER_PARAMETER(FVector4f, DreamUI_StrengthParams)
		SHADER_PARAMETER(FVector4f, DreamUI_StrengthPoints)
		SHADER_PARAMETER(FVector4f, DreamUI_StrengthRange)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_StrengthTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_StrengthSampler)
		SHADER_PARAMETER(FVector4f, DreamUI_GlassCorners)
		SHADER_PARAMETER(FVector4f, DreamUI_GlassRefraction)
		SHADER_PARAMETER(FVector4f, DreamUI_GlassTint)
		SHADER_PARAMETER(FVector4f, DreamUI_GlassTone)
		SHADER_PARAMETER(FVector4f, DreamUI_GlassHighlight)
		SHADER_PARAMETER(FVector4f, DreamUI_GlassLight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

IMPLEMENT_GLOBAL_SHADER(FDreamUIBackdropCompositeVS, "/Plugin/DreamGUI/Private/DreamUIBackdropComposite.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FDreamUIBackdropCompositePS, "/Plugin/DreamGUI/Private/DreamUIBackdropComposite.usf", "MainPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FDreamUIBackdropCompositePassParameters, )
	RDG_TEXTURE_ACCESS(Backdrop, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(Level0, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(Level1, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(Level2, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(Level3, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(Level4, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(Level5, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(SceneDepth, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

namespace DreamUIBackdropEffectsLocal
{
	/** A texture for an effect's work, InLike's format and sRGB flag at InSize: what is drawn into it reads back as it was. */
	FRDGTextureRef CreateLike(FRDGBuilder& GraphBuilder, FRDGTextureRef InLike, FIntPoint InSize, const TCHAR* InName)
	{
		const ETextureCreateFlags Srgb = EnumHasAnyFlags(InLike->Desc.Flags, TexCreate_SRGB) ? TexCreate_SRGB : TexCreate_None;
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(FMath::Max(InSize.X, 1), FMath::Max(InSize.Y, 1)), InLike->Desc.Format
			, FClearValueBinding::Transparent, Srgb | TexCreate_RenderTargetable | TexCreate_ShaderResource);
		return GraphBuilder.CreateTexture(Desc, InName);
	}

	FRHISamplerState* Bilinear()
	{
		return TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}

	FVector4f FlagsOf(const FDreamUIImageEffectContext& InContext, float InOpacity, EDreamUIBlendMode InMode)
	{
		return FVector4f(InContext.bLinear ? 1.0f : 0.0f, InContext.bOpaque ? 1.0f : 0.0f, FMath::Clamp(InOpacity, 0.0f, 1.0f), static_cast<float>(InMode));
	}

	FVector4f OriginOf(const FDreamUIImageEffectContext& InContext, FRDGTextureRef InSource)
	{
		return FVector4f(static_cast<float>(InContext.Origin.X), static_cast<float>(InContext.Origin.Y)
			, static_cast<float>(InSource->Desc.Extent.X), static_cast<float>(InSource->Desc.Extent.Y));
	}

	/** Where a world point lands on the view, in the bottom target's pixels; false behind the eye. */
	bool ToPixel(const FVector& InWorld, const FMatrix& InWorldToClip, const FIntRect& InViewRect, FVector2D& OutPixel)
	{
		const FVector4 Clip = InWorldToClip.TransformFVector4(FVector4(InWorld, 1.0));
		if (Clip.W <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		OutPixel = FVector2D(InViewRect.Min.X + (Clip.X / Clip.W * 0.5 + 0.5) * InViewRect.Width(), InViewRect.Min.Y + (0.5 - Clip.Y / Clip.W * 0.5) * InViewRect.Height());
		return true;
	}
}

// ---- the adjustment

class FDreamUIAdjustEffect final : public FDreamUIImageEffect
{
public:
	explicit FDreamUIAdjustEffect(FDreamUIAdjustPacked&& InPacked)
		: Packed(MoveTemp(InPacked))
	{
		PendingLookupTable = Packed.LookupTable;
		Packed.LookupTable = nullptr;
	}

	virtual void ResolveTextures_RenderThread() const override
	{
		check(IsInRenderingThread());
		if (PendingLookupTable != nullptr)
		{
			const FTextureResource* Resource = PendingLookupTable->GetResource();
			LookupTableRHI = PendingLookupTable->TextureReference.TextureReferenceRHI;
			LookupTableSampler = Resource != nullptr ? Resource->SamplerStateRHI : FSamplerStateRHIRef();
			PendingLookupTable = nullptr;
		}
	}

	virtual FRDGTextureRef Apply_RenderThread(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGTextureRef InSource, const FDreamUIImageEffectContext& InContext) const override
	{
		using namespace DreamUIBackdropEffectsLocal;
		if (Packed.NumOps <= 0 || InSource == nullptr)
		{
			return InSource;
		}
		const FIntPoint Size = InSource->Desc.Extent;
		FRDGTextureRef Output = CreateLike(GraphBuilder, InSource, Size, TEXT("DreamUI_Adjusted"));
		FDreamUIAdjustPS::FParameters* Parameters = GraphBuilder.AllocParameters<FDreamUIAdjustPS::FParameters>();
		Parameters->DreamUI_SourceTex = InSource;
		Parameters->DreamUI_EffectFlags = FlagsOf(InContext, Packed.Opacity, Packed.BlendMode);
		Parameters->DreamUI_EffectOrigin = OriginOf(InContext, InSource);
		for (int32 Row = 0; Row < DreamUIAdjustLayout::MaxOps * DreamUIAdjustLayout::RowsPerOp; ++Row)
		{
			Parameters->DreamUI_AdjustRows[Row] = Packed.Rows[Row];
		}
		Parameters->DreamUI_AdjustCount = Packed.NumOps;
		Parameters->DreamUI_AdjustTable = GetTable(GraphBuilder.RHICmdList);
		Parameters->DreamUI_AdjustTableSampler = Bilinear();
		Parameters->DreamUI_AdjustLut = LookupTableRHI.IsValid() ? static_cast<FRHITexture*>(LookupTableRHI.GetReference()) : GWhiteTexture->TextureRHI.GetReference();
		Parameters->DreamUI_AdjustLutSampler = LookupTableSampler.IsValid() ? LookupTableSampler.GetReference() : Bilinear();
		Parameters->RenderTargets[0] = FRenderTargetBinding(Output, ERenderTargetLoadAction::ENoAction);
		TShaderMapRef<FDreamUIAdjustPS> PixelShader(ShaderMap);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("DreamUIAdjust"), PixelShader, Parameters, FIntRect(FIntPoint::ZeroValue, Size));
		return Output;
	}

private:
	/** The table texture, made from the packed rows the first time it is asked for: render thread. */
	FRHITexture* GetTable(FRHICommandListImmediate& RHICmdList) const
	{
		if (!TableRHI.IsValid())
		{
			const int32 Width = DreamUIAdjustLayout::TableWidth;
			const int32 Height = DreamUIAdjustLayout::MaxOps;
			const FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("DreamUI_AdjustTable"), Width, Height, PF_FloatRGBA)
				.SetFlags(ETextureCreateFlags::ShaderResource)
				.SetInitialState(ERHIAccess::SRVMask);
			TableRHI = RHICmdList.CreateTexture(Desc);
			TArray<FFloat16Color> Texels = Packed.Table;
			Texels.SetNumZeroed(Width * Height);
			RHICmdList.UpdateTexture2D(TableRHI, 0, FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height), Width * sizeof(FFloat16Color), reinterpret_cast<const uint8*>(Texels.GetData()));
		}
		return TableRHI.GetReference();
	}

	FDreamUIAdjustPacked Packed;
	mutable const UTexture* PendingLookupTable = nullptr;
	mutable FTextureReferenceRHIRef LookupTableRHI;
	mutable FSamplerStateRHIRef LookupTableSampler;
	mutable FTextureRHIRef TableRHI;
};

// ---- the stylize effect

class FDreamUIStylizeEffect final : public FDreamUIImageEffect
{
public:
	explicit FDreamUIStylizeEffect(const FDreamUIStylizePacked& InPacked)
		: Packed(InPacked)
	{
	}

	virtual float GetReach() const override { return Packed.Reach; }

	virtual FRDGTextureRef Apply_RenderThread(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGTextureRef InSource, const FDreamUIImageEffectContext& InContext) const override
	{
		using namespace DreamUIBackdropEffectsLocal;
		if (Packed.NumLayers <= 0 || InSource == nullptr)
		{
			return InSource;
		}
		const FIntPoint Size = InSource->Desc.Extent;
		const FIntRect Viewport(FIntPoint::ZeroValue, Size);
		TShaderMapRef<FDreamUIStylizePS> StylizeShader(ShaderMap);
		// A layer after another reads what the one before made: each a pass of its own, into a texture of its own.
		FRDGTextureRef Current = InSource;
		for (int32 Layer = 0; Layer < Packed.NumLayers; ++Layer)
		{
			FRDGTextureRef Next = CreateLike(GraphBuilder, InSource, Size, TEXT("DreamUI_Stylized"));
			FDreamUIStylizePS::FParameters* Parameters = GraphBuilder.AllocParameters<FDreamUIStylizePS::FParameters>();
			Parameters->DreamUI_SourceTex = Current;
			Parameters->DreamUI_SourceSampler = Bilinear();
			Parameters->DreamUI_EffectFlags = FlagsOf(InContext, 1.0f, EDreamUIBlendMode::Alpha);
			Parameters->DreamUI_EffectOrigin = OriginOf(InContext, InSource);
			for (int32 Row = 0; Row < DreamUIStylizeLayout::RowsPerLayer; ++Row)
			{
				Parameters->DreamUI_StylizeRows[Row] = Packed.Rows[Layer * DreamUIStylizeLayout::RowsPerLayer + Row];
			}
			Parameters->DreamUI_StylizeFrame = InContext.Frame;
			Parameters->DreamUI_StylizeParams = FVector4f(InContext.PixelScale, InContext.TimeSeconds, 0.0f, 0.0f);
			Parameters->RenderTargets[0] = FRenderTargetBinding(Next, ERenderTargetLoadAction::ENoAction);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("DreamUIStylize"), StylizeShader, Parameters, Viewport);
			Current = Next;
		}
		if (Packed.Opacity >= 1.0f && Packed.BlendMode == EDreamUIBlendMode::Alpha)
		{
			return Current;
		}
		FRDGTextureRef Mixed = CreateLike(GraphBuilder, InSource, Size, TEXT("DreamUI_StylizeMixed"));
		FDreamUIEffectMixPS::FParameters* Parameters = GraphBuilder.AllocParameters<FDreamUIEffectMixPS::FParameters>();
		Parameters->DreamUI_SourceTex = InSource;
		Parameters->DreamUI_EffectTex = Current;
		Parameters->DreamUI_EffectFlags = FlagsOf(InContext, Packed.Opacity, Packed.BlendMode);
		Parameters->DreamUI_EffectOrigin = OriginOf(InContext, InSource);
		Parameters->RenderTargets[0] = FRenderTargetBinding(Mixed, ERenderTargetLoadAction::ENoAction);
		TShaderMapRef<FDreamUIEffectMixPS> MixShader(ShaderMap);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("DreamUIStylizeMix"), MixShader, Parameters, Viewport);
		return Mixed;
	}

private:
	FDreamUIStylizePacked Packed;
};

// ---- the stages

FDreamUIImageEffectContext DreamUIBackdropPasses::FRegion::MakeEffectContext(const FDreamUIPostProcessContext& InContext) const
{
	FDreamUIImageEffectContext Out;
	Out.bLinear = InContext.bLinearTarget;
	Out.bOpaque = InContext.Stack != nullptr && InContext.Stack->IsBackdropOpaque();
	Out.TimeSeconds = InContext.TimeSeconds;
	Out.Origin = TargetOrigin;
	Out.Frame = Frame;
	Out.PixelScale = PixelScale;
	return Out;
}

bool DreamUIBackdropPasses::CopyRegion(const FDreamUIPostProcessContext& InContext, const FDreamVisualPostProcessRenderProxy& InProxy, float InReachUnits, int32 InReachPixels, FRegion& OutRegion)
{
	using namespace DreamUIBackdropEffectsLocal;
	FDreamUICompositeStack* Stack = InContext.Stack;
	const TArray<FDreamUIPostProcessVertex>& Vertices = InProxy.RenderMeshRegionToScreenVertexArray;
	if (Stack == nullptr || !Stack->IsDrawing() || Vertices.Num() < 4)
	{
		return false;
	}
	const FMatrix WorldToClip(InContext.ViewProjectionMatrix);
	const FMatrix ObjectToWorld(InProxy.ObjectToWorldMatrix);
	const FIntRect& ViewRect = Stack->GetViewRect();
	// The element's corners on the view, and how many pixels a unit of the canvas is there.
	FBox WorldBox(ForceInit);
	FBox2D Pixels(ForceInit);
	FVector Centre = FVector::ZeroVector;
	for (const FDreamUIPostProcessVertex& Vertex : Vertices)
	{
		const FVector World = ObjectToWorld.TransformPosition(FVector(Vertex.Position));
		WorldBox += World;
		Centre += FVector(Vertex.Position) / Vertices.Num();
		FVector2D Pixel;
		if (ToPixel(World, WorldToClip, ViewRect, Pixel))
		{
			Pixels += Pixel;
		}
	}
	float PixelScale = 1.0f;
	{
		FVector2D A, B, C;
		if (ToPixel(ObjectToWorld.TransformPosition(Centre), WorldToClip, ViewRect, A)
			&& ToPixel(ObjectToWorld.TransformPosition(Centre + FVector(0.0, 1.0, 0.0)), WorldToClip, ViewRect, B)
			&& ToPixel(ObjectToWorld.TransformPosition(Centre + FVector(0.0, 0.0, 1.0)), WorldToClip, ViewRect, C))
		{
			PixelScale = static_cast<float>(FMath::Max((FVector2D::Distance(A, B) + FVector2D::Distance(A, C)) * 0.5, 1e-3));
		}
	}
	FIntRect Covered = FDreamUICompositor::ProjectBounds(WorldBox, WorldToClip, ViewRect);
	if (Covered.Width() <= 0 || Covered.Height() <= 0)
	{
		return false;
	}
	const int32 Reach = FMath::CeilToInt32(FMath::Max(InReachUnits, 0.0f) * PixelScale) + FMath::Max(InReachPixels, 0);
	Covered.InflateRect(Reach);
	Covered.Clip(ViewRect);
	if (Covered.Width() <= 0 || Covered.Height() <= 0)
	{
		return false;
	}
	FIntPoint CopyOrigin = FIntPoint::ZeroValue;
	FRDGTextureRef Copy = Stack->CopyBackdrop(Covered, CopyOrigin, TEXT("DreamUI_EffectBackdrop"));
	if (Copy == nullptr)
	{
		return false;
	}
	const FIntPoint CurrentOrigin = Stack->Current().Origin;
	OutRegion.Copy = Copy;
	OutRegion.Origin = CopyOrigin;
	OutRegion.TargetOrigin = CopyOrigin + CurrentOrigin;
	OutRegion.PixelScale = PixelScale;
	if (Pixels.bIsValid)
	{
		const FVector2D Min = Pixels.Min - FVector2D(OutRegion.TargetOrigin);
		OutRegion.Frame = FVector4f(static_cast<float>(Min.X), static_cast<float>(Min.Y), static_cast<float>(Pixels.GetSize().X), static_cast<float>(Pixels.GetSize().Y));
	}
	else
	{
		OutRegion.Frame = FVector4f(0.0f, 0.0f, static_cast<float>(Copy->Desc.Extent.X), static_cast<float>(Copy->Desc.Extent.Y));
	}
	return true;
}

void DreamUIBackdropPasses::BuildPyramid(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGTextureRef InSource, int32 InMaxLevel, FRDGTextureRef (&OutLevels)[DreamUIPostProcessEffects::BlurPyramidLevels])
{
	using namespace DreamUIBackdropEffectsLocal;
	const int32 MaxLevel = FMath::Clamp(InMaxLevel, 0, DreamUIPostProcessEffects::BlurPyramidLevels - 1);
	FRDGTextureRef Down[DreamUIPostProcessEffects::BlurPyramidLevels] = {};
	Down[0] = InSource;
	OutLevels[0] = InSource;
	TShaderMapRef<FDreamUIPyramidDownPS> DownShader(ShaderMap);
	TShaderMapRef<FDreamUIPyramidUpPS> UpShader(ShaderMap);
	for (int32 Level = 1; Level <= MaxLevel; ++Level)
	{
		const FIntPoint SourceSize = Down[Level - 1]->Desc.Extent;
		const FIntPoint Size(FMath::Max(SourceSize.X / 2, 1), FMath::Max(SourceSize.Y / 2, 1));
		Down[Level] = CreateLike(GraphBuilder, InSource, Size, TEXT("DreamUI_BlurDown"));
		FDreamUIPyramidDownPS::FParameters* Parameters = GraphBuilder.AllocParameters<FDreamUIPyramidDownPS::FParameters>();
		Parameters->DreamUI_SourceTex = Down[Level - 1];
		Parameters->DreamUI_SourceSampler = Bilinear();
		Parameters->DreamUI_FilterTexel = FVector4f(1.0f / SourceSize.X, 1.0f / SourceSize.Y, 0.0f, 0.0f);
		Parameters->DreamUI_TargetTexel = FVector4f(1.0f / Size.X, 1.0f / Size.Y, 0.0f, 0.0f);
		Parameters->RenderTargets[0] = FRenderTargetBinding(Down[Level], ERenderTargetLoadAction::ENoAction);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("DreamUIBlurDown"), DownShader, Parameters, FIntRect(FIntPoint::ZeroValue, Size));
	}
	// Each level back up to the first's size, the dual filter's up passes smoothing what the down passes left blocky.
	for (int32 Level = 1; Level <= MaxLevel; ++Level)
	{
		FRDGTextureRef Current = Down[Level];
		for (int32 Up = Level - 1; Up >= 1; --Up)
		{
			const FIntPoint SourceSize = Current->Desc.Extent;
			const FIntPoint Size = Down[Up]->Desc.Extent;
			FRDGTextureRef Next = CreateLike(GraphBuilder, InSource, Size, TEXT("DreamUI_BlurUp"));
			FDreamUIPyramidUpPS::FParameters* Parameters = GraphBuilder.AllocParameters<FDreamUIPyramidUpPS::FParameters>();
			Parameters->DreamUI_SourceTex = Current;
			Parameters->DreamUI_SourceSampler = Bilinear();
			Parameters->DreamUI_FilterTexel = FVector4f(0.5f / SourceSize.X, 0.5f / SourceSize.Y, 0.0f, 0.0f);
			Parameters->DreamUI_TargetTexel = FVector4f(1.0f / Size.X, 1.0f / Size.Y, 0.0f, 0.0f);
			Parameters->RenderTargets[0] = FRenderTargetBinding(Next, ERenderTargetLoadAction::ENoAction);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("DreamUIBlurUp"), UpShader, Parameters, FIntRect(FIntPoint::ZeroValue, Size));
			Current = Next;
		}
		OutLevels[Level] = Current;
	}
	for (int32 Level = MaxLevel + 1; Level < DreamUIPostProcessEffects::BlurPyramidLevels; ++Level)
	{
		OutLevels[Level] = OutLevels[MaxLevel];
	}
}

void DreamUIBackdropPasses::DrawComposite(const FDreamUIPostProcessContext& InContext, const FDreamVisualPostProcessRenderProxy& InProxy, const FRegion& InRegion, const FComposite& InComposite)
{
	using namespace DreamUIBackdropEffectsLocal;
	FRDGBuilder& GraphBuilder = InContext.GraphBuilder;
	FDreamUICompositeStack* Stack = InContext.Stack;
	const FDreamUICompositeStack::FFrame& Frame = Stack->Current();
	FDreamUIBackdropCompositePassParameters* PassParameters = GraphBuilder.AllocParameters<FDreamUIBackdropCompositePassParameters>();
	PassParameters->Backdrop = InRegion.Copy;
	FRDGTextureRef Levels[DreamUIPostProcessEffects::BlurPyramidLevels];
	for (int32 Level = 0; Level < DreamUIPostProcessEffects::BlurPyramidLevels; ++Level)
	{
		// What a permutation does not read is bound all the same: the copy, which costs nothing more.
		Levels[Level] = InComposite.Levels[Level] != nullptr ? InComposite.Levels[Level] : InRegion.Copy;
	}
	PassParameters->Level0 = Levels[0];
	PassParameters->Level1 = Levels[1];
	PassParameters->Level2 = Levels[2];
	PassParameters->Level3 = Levels[3];
	PassParameters->Level4 = Levels[4];
	PassParameters->Level5 = Levels[5];
	const bool bWorld = InContext.bIsWorldSpace;
	PassParameters->SceneDepth = bWorld ? InContext.SceneDepth : nullptr;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Frame.Texture, ERenderTargetLoadAction::ELoad);

	// Everything the pass draws with, by value: it runs when the graph does.
	const FMatrix44f LocalToClip = InProxy.ObjectToWorldMatrix * InContext.ViewProjectionMatrix;
	TArray<FDreamUIPostProcessVertex> Vertices = InProxy.RenderMeshRegionToScreenVertexArray;
	const FIntRect Viewport = InContext.ViewRect;
	const uint8 NumSamples = Frame.NumSamples;
	const bool bOpaque = Stack->IsBackdropOpaque();
	const FVector4f Origin(static_cast<float>(InRegion.Origin.X), static_cast<float>(InRegion.Origin.Y)
		, static_cast<float>(InRegion.Copy->Desc.Extent.X), static_cast<float>(InRegion.Copy->Desc.Extent.Y));
	const FVector4f Flags(bOpaque ? 1.0f : 0.0f, InContext.bLinearTarget ? 1.0f : 0.0f, InProxy.RectSize.X, InProxy.RectSize.Y);
	FTextureReferenceRHIRef ClipData = InProxy.ClipDataTextureRHI;
	FTextureReferenceRHIRef ClipMask0 = InProxy.ClipMaskTextureRHI[0];
	FTextureReferenceRHIRef ClipMask1 = InProxy.ClipMaskTextureRHI[1];
	FSamplerStateRHIRef ClipSampler0 = InProxy.ClipMaskSamplerRHI[0];
	FSamplerStateRHIRef ClipSampler1 = InProxy.ClipMaskSamplerRHI[1];
	FTextureRHIRef Mask = InProxy.MaskTextureRHI;
	FSamplerStateRHIRef MaskSampler = InProxy.MaskTextureSamplerState;
	const FVector4f DepthScaleOffset = InContext.DepthTextureScaleOffset;
	const float BlendDepth = InContext.BlendDepthForWorld;
	const int32 DepthFade = InContext.DepthFadeForWorld;
	const FComposite Composite = InComposite;
	FGlobalShaderMap* ShaderMap = InContext.GlobalShaderMap;
	FRDGTextureRef Copy = InRegion.Copy;
	FRDGTextureRef SceneDepth = PassParameters->SceneDepth;
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("DreamUIBackdropComposite"),
		PassParameters,
		ERDGPassFlags::Raster,
		[=, Vertices = MoveTemp(Vertices)](FRHICommandList& RHICmdList)
		{
			auto TextureOr = [](FRHITexture* InTexture, FTexture* InFallback) -> FRHITexture*
			{
				return InTexture != nullptr ? InTexture : InFallback->TextureRHI.GetReference();
			};
			RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
			FDreamUIBackdropCompositePS::FPermutationDomain Permutation;
			Permutation.Set<FDreamUIBackdropCompositePS::FEffect>(Composite.Effect);
			Permutation.Set<FDreamUIBackdropCompositePS::FBlendDepth>(bWorld);
			Permutation.Set<FDreamUIBackdropCompositePS::FMask>(Mask.IsValid());
			TShaderMapRef<FDreamUIBackdropCompositeVS> VertexShader(ShaderMap);
			TShaderMapRef<FDreamUIBackdropCompositePS> PixelShader(ShaderMap, Permutation);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			// No blend: the shader mixes what was behind with what the effect made of it.
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessVertexDeclaration();
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.NumSamples = NumSamples;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			FDreamUIBackdropCompositeVS::FParameters VertexParameters;
			VertexParameters.LocalToClip = LocalToClip;
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VertexParameters);

			FDreamUIBackdropCompositePS::FParameters Parameters;
			Parameters.DreamUI_BackdropTex = Copy->GetRHI();
			Parameters.DreamUI_BackdropOrigin = Origin;
			Parameters.DreamUI_BackdropFlags = Flags;
			Parameters.DreamUI_LevelSampler = Bilinear();
			Parameters.DreamUI_Level0 = Levels[0]->GetRHI();
			Parameters.DreamUI_Level1 = Levels[1]->GetRHI();
			Parameters.DreamUI_Level2 = Levels[2]->GetRHI();
			Parameters.DreamUI_Level3 = Levels[3]->GetRHI();
			Parameters.DreamUI_Level4 = Levels[4]->GetRHI();
			Parameters.DreamUI_Level5 = Levels[5]->GetRHI();
			Parameters.ClipDataTex = TextureOr(ClipData.GetReference(), GBlackTexture);
			Parameters.DreamUI_MaskTex0 = TextureOr(ClipMask0.GetReference(), GWhiteTexture);
			Parameters.DreamUI_MaskTexSampler0 = ClipSampler0.IsValid() ? ClipSampler0.GetReference() : Bilinear();
			Parameters.DreamUI_MaskTex1 = TextureOr(ClipMask1.GetReference(), GWhiteTexture);
			Parameters.DreamUI_MaskTexSampler1 = ClipSampler1.IsValid() ? ClipSampler1.GetReference() : Bilinear();
			Parameters.MaskTex = TextureOr(Mask.GetReference(), GWhiteTexture);
			Parameters.MaskTexSampler = MaskSampler.IsValid() ? MaskSampler.GetReference() : Bilinear();
			Parameters.SceneDepthTex = SceneDepth != nullptr ? SceneDepth->GetRHI() : GBlackTexture->TextureRHI.GetReference();
			Parameters.SceneDepthTexSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters.SceneDepthTextureScaleOffset = DepthScaleOffset;
			Parameters.SceneDepthBlend = BlendDepth;
			Parameters.SceneDepthFade = DepthFade;
			Parameters.ViewSizeInv = FVector2f(1.0f / FMath::Max(Viewport.Width(), 1), 1.0f / FMath::Max(Viewport.Height(), 1));
			const FDreamUIBlurStrengthMap& Map = Composite.Map;
			Parameters.DreamUI_StrengthParams = FVector4f(Composite.Strength, static_cast<float>(Map.Kind), static_cast<float>(FMath::Clamp(Map.MaxLevel, 0, 5)), static_cast<float>(Map.Channel));
			Parameters.DreamUI_StrengthPoints = Map.Kind == 4 ? FVector4f(Map.Seed, 0.0f, 0.0f, 0.0f) : Map.Points;
			Parameters.DreamUI_StrengthRange = FVector4f(Map.Strengths.X, Map.Strengths.Y, static_cast<float>(Map.Pattern), Map.Frequency);
			Parameters.DreamUI_StrengthTex = TextureOr(Composite.MapTexture.GetReference(), GWhiteTexture);
			Parameters.DreamUI_StrengthSampler = Composite.MapSampler.IsValid() ? Composite.MapSampler.GetReference() : Bilinear();
			const FDreamUIGlassParams& Glass = Composite.Glass;
			// A rect block's radii, in its order: the distance measures them as a rect block does.
			Parameters.DreamUI_GlassCorners = Glass.CornerRadii;
			Parameters.DreamUI_GlassRefraction = FVector4f(Glass.EdgeWidth, Glass.Refraction, static_cast<float>(Glass.Profile), Glass.Frost);
			Parameters.DreamUI_GlassTint = Glass.Tint;
			Parameters.DreamUI_GlassTone = FVector4f(Glass.Saturation, Glass.Brightness, Glass.Opacity, Glass.Dispersion);
			Parameters.DreamUI_GlassHighlight = Glass.HighlightColor;
			Parameters.DreamUI_GlassLight = FVector4f(Glass.HighlightWidth, Glass.LightAngle, Glass.ShadowWidth, Glass.ShadowStrength);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Parameters);

			FBufferRHIRef VertexBuffer = UE::RHIResourceUtils::CreateVertexBufferFromArray(RHICmdList, TEXT("DreamUIBackdropComposite"), EBufferUsageFlags::Volatile, MakeConstArrayView(Vertices));
			RHICmdList.SetStreamSource(0, VertexBuffer, 0);
			RHICmdList.DrawIndexedPrimitive(GDreamUIFullScreenQuadIndexBuffer.IndexBufferRHI, 0, 0, Vertices.Num(), 0, 2, 1);
		});
}

// ---- the proxies

namespace DreamUIBackdropEffectsLocal
{
	/** An image effect over what is behind its element: the adjustment layer's, the stylize effect's. */
	class FImageEffectProxy final : public FDreamVisualPostProcessRenderProxy
	{
	public:
		FDreamUIImageEffectRef Effect;

		virtual bool CanRender() const override { return Effect.IsValid(); }

		virtual void Render_RenderThread(const FDreamUIPostProcessContext& InContext) override
		{
			if (!Effect.IsValid())
			{
				return;
			}
			DreamUIBackdropPasses::FRegion Region;
			if (!DreamUIBackdropPasses::CopyRegion(InContext, *this, Effect->GetReach(), 1, Region))
			{
				return;
			}
			DreamUIBackdropPasses::FComposite Composite;
			Composite.Effect = 0;
			Composite.Levels[0] = Effect->Apply_RenderThread(InContext.GraphBuilder, InContext.GlobalShaderMap, Region.Copy, Region.MakeEffectContext(InContext));
			DreamUIBackdropPasses::DrawComposite(InContext, *this, Region, Composite);
		}
	};

	class FGlassProxy final : public FDreamVisualPostProcessRenderProxy
	{
	public:
		FDreamUIGlassParams Params;

		virtual bool CanRender() const override { return Params.Opacity > 0.0f; }

		virtual void Render_RenderThread(const FDreamUIPostProcessContext& InContext) override
		{
			// What the edge bends in from, and what frost blurs in from.
			const int32 FrostLevels = Params.Frost > 0.0f ? DreamUIPostProcessEffects::BlurPyramidLevels - 1 : 0;
			DreamUIBackdropPasses::FRegion Region;
			if (!DreamUIBackdropPasses::CopyRegion(InContext, *this, FMath::Abs(Params.Refraction) * (1.0f + FMath::Abs(Params.Dispersion)), DreamUIPostProcessEffects::GetBlurPyramidReach(FrostLevels) + 1, Region))
			{
				return;
			}
			DreamUIBackdropPasses::FComposite Composite;
			Composite.Effect = 2;
			Composite.Glass = Params;
			DreamUIBackdropPasses::BuildPyramid(InContext.GraphBuilder, InContext.GlobalShaderMap, Region.Copy, FrostLevels, Composite.Levels);
			DreamUIBackdropPasses::DrawComposite(InContext, *this, Region, Composite);
		}
	};
}

FDreamUIImageEffectRef DreamUIPostProcessEffects::MakeAdjustEffect(FDreamUIAdjustPacked&& InPacked)
{
	return MakeShared<FDreamUIAdjustEffect, ESPMode::ThreadSafe>(MoveTemp(InPacked));
}

FDreamUIImageEffectRef DreamUIPostProcessEffects::MakeStylizeEffect(const FDreamUIStylizePacked& InPacked)
{
	return MakeShared<FDreamUIStylizeEffect, ESPMode::ThreadSafe>(InPacked);
}

FDreamVisualPostProcessRenderProxyPtr DreamUIPostProcessEffects::CreateBackdropEffectProxy()
{
	return MakeShared<DreamUIBackdropEffectsLocal::FImageEffectProxy, ESPMode::ThreadSafe>();
}

void DreamUIPostProcessEffects::SetBackdropEffect_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, const FDreamUIImageEffectRef& InEffect)
{
	if (!InProxy.IsValid())
	{
		return;
	}
	TSharedPtr<DreamUIBackdropEffectsLocal::FImageEffectProxy, ESPMode::ThreadSafe> Proxy = StaticCastSharedPtr<DreamUIBackdropEffectsLocal::FImageEffectProxy>(InProxy);
	ENQUEUE_RENDER_COMMAND(FDreamBackdropEffect_Update)([Proxy, InEffect](FRHICommandListImmediate& RHICmdList)
	{
		if (InEffect.IsValid())
		{
			InEffect->ResolveTextures_RenderThread();
		}
		Proxy->Effect = InEffect;
	});
}

FDreamVisualPostProcessRenderProxyPtr DreamUIPostProcessEffects::CreateGlassProxy()
{
	return MakeShared<DreamUIBackdropEffectsLocal::FGlassProxy, ESPMode::ThreadSafe>();
}

void DreamUIPostProcessEffects::SetGlass_GameThread(const FDreamVisualPostProcessRenderProxyPtr& InProxy, const FDreamUIGlassParams& InParams)
{
	if (!InProxy.IsValid())
	{
		return;
	}
	TSharedPtr<DreamUIBackdropEffectsLocal::FGlassProxy, ESPMode::ThreadSafe> Proxy = StaticCastSharedPtr<DreamUIBackdropEffectsLocal::FGlassProxy>(InProxy);
	ENQUEUE_RENDER_COMMAND(FDreamGlass_Update)([Proxy, InParams](FRHICommandListImmediate& RHICmdList)
	{
		Proxy->Params = InParams;
	});
}

int32 DreamUIPostProcessEffects::GetBlurPyramidReach(int32 InLevel)
{
	// Each down pass reads a texel and a half further at its scale, each up pass as much again: about three times the
	// level's scale in all.
	return InLevel <= 0 ? 0 : 3 << FMath::Min(InLevel, BlurPyramidLevels - 1);
}
