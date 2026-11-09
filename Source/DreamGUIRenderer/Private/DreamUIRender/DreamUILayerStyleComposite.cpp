// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIRender/DreamUILayerStyleComposite.h"
#include "CommonRenderResources.h"
#include "Engine/Texture.h"
#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

using namespace DreamUILayerStyleLayout;

namespace DreamUILayerStyleCompositeLocal
{
	bool ShouldCompile(const FGlobalShaderPermutationParameters& Parameters)
	{
		// The flood's float seeds are drawn into: the desktop feature levels. Elsewhere a group's style is not drawn.
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
}

class FDreamUIJumpFloodInitPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIJumpFloodInitPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIJumpFloodInitPS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DreamUI_FloodSource)
		SHADER_PARAMETER(FVector4f, DreamUI_FloodParams)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return DreamUILayerStyleCompositeLocal::ShouldCompile(Parameters); }
};
IMPLEMENT_GLOBAL_SHADER(FDreamUIJumpFloodInitPS, "/Plugin/DreamGUI/Private/DreamUIJumpFlood.usf", "InitPS", SF_Pixel);

class FDreamUIJumpFloodStepPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUIJumpFloodStepPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUIJumpFloodStepPS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DreamUI_FloodSource)
		SHADER_PARAMETER(FVector4f, DreamUI_FloodParams)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return DreamUILayerStyleCompositeLocal::ShouldCompile(Parameters); }
};
IMPLEMENT_GLOBAL_SHADER(FDreamUIJumpFloodStepPS, "/Plugin/DreamGUI/Private/DreamUIJumpFlood.usf", "StepPS", SF_Pixel);

class FDreamUILayerStyleCompositePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUILayerStyleCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUILayerStyleCompositePS, FGlobalShader);
	/** How many outer slots are drawn besides the layer: a render target each. */
	class FSlots : SHADER_PERMUTATION_RANGE_INT("DREAMUI_STYLE_SLOTS", 0, MaxOuterSlots + 1);
	using FPermutationDomain = TShaderPermutationDomain<FSlots>;
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DreamUI_StyleGroup)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DreamUI_StyleField)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_StyleRecord)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_StylePattern0)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_StylePatternSampler0)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_StylePattern1)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_StylePatternSampler1)
		SHADER_PARAMETER(FVector4f, DreamUI_StyleFrame)
		SHADER_PARAMETER(FVector4f, DreamUI_StyleParams)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return DreamUILayerStyleCompositeLocal::ShouldCompile(Parameters); }
};
IMPLEMENT_GLOBAL_SHADER(FDreamUILayerStyleCompositePS, "/Plugin/DreamGUI/Private/DreamUILayerStyleComposite.usf", "MainPS", SF_Pixel);

FDreamUILayerStyleComposite::FDreamUILayerStyleComposite(FDreamUILayerStyleRecord&& InRecord)
	: Record(MoveTemp(InRecord))
{
}

void FDreamUILayerStyleComposite::ResolveTextures_RenderThread() const
{
	for (int32 Slot = 0; Slot < MaxPatterns; ++Slot)
	{
		const UTexture* Pattern = Record.Patterns[Slot];
		const FTextureResource* Resource = Pattern != nullptr ? Pattern->GetResource() : nullptr;
		PatternRHI[Slot] = Resource != nullptr ? Resource->TextureRHI : nullptr;
	}
}

FRDGTextureRef FDreamUILayerStyleComposite::JumpFlood(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGTextureRef InGroup)
{
	TShaderMapRef<FDreamUIJumpFloodInitPS> InitShader(ShaderMap);
	TShaderMapRef<FDreamUIJumpFloodStepPS> StepShader(ShaderMap);
	if (InGroup == nullptr || !InitShader.IsValid() || !StepShader.IsValid())
	{
		return nullptr;
	}
	const FIntPoint Size = InGroup->Desc.Extent;
	const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Size, PF_A32B32G32R32F, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource);
	FRDGTextureRef Seeds = GraphBuilder.CreateTexture(Desc, TEXT("DreamUI_StyleSeeds"));
	const FVector4f SizeParams(0.0f, static_cast<float>(Size.X), static_cast<float>(Size.Y), 0.0f);
	{
		FDreamUIJumpFloodInitPS::FParameters* Parameters = GraphBuilder.AllocParameters<FDreamUIJumpFloodInitPS::FParameters>();
		Parameters->DreamUI_FloodSource = InGroup;
		Parameters->DreamUI_FloodParams = SizeParams;
		Parameters->RenderTargets[0] = FRenderTargetBinding(Seeds, ERenderTargetLoadAction::ENoAction);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("DreamUIJumpFloodInit"), InitShader, Parameters, FIntRect(FIntPoint::ZeroValue, Size));
	}
	// Halving from the largest power of two under the texture's size down to one, and one more step of one: the seeds' last
	// few errors, where a nearer seed was passed over at a long step, are mostly mended by it.
	TArray<int32, TInlineAllocator<16>> Steps;
	for (int32 Step = static_cast<int32>(FMath::RoundUpToPowerOfTwo(static_cast<uint32>(FMath::Max(Size.X, Size.Y)))) / 2; Step >= 1; Step /= 2)
	{
		Steps.Add(Step);
	}
	Steps.Add(1);
	FRDGTextureRef Other = GraphBuilder.CreateTexture(Desc, TEXT("DreamUI_StyleSeeds"));
	for (const int32 Step : Steps)
	{
		FDreamUIJumpFloodStepPS::FParameters* Parameters = GraphBuilder.AllocParameters<FDreamUIJumpFloodStepPS::FParameters>();
		Parameters->DreamUI_FloodSource = Seeds;
		Parameters->DreamUI_FloodParams = FVector4f(static_cast<float>(Step), SizeParams.Y, SizeParams.Z, 0.0f);
		Parameters->RenderTargets[0] = FRenderTargetBinding(Other, ERenderTargetLoadAction::ENoAction);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("DreamUIJumpFloodStep(%d)", Step), StepShader, Parameters, FIntRect(FIntPoint::ZeroValue, Size));
		Swap(Seeds, Other);
	}
	return Seeds;
}

FDreamUILayerStyleContext FDreamUILayerStyleComposite::MakeContext(const FVector4f& InStyleRect, const FMatrix& InCanvasToWorld, const FMatrix& InWorldToClip,
	const FIntRect& InViewRect, FIntPoint InTextureOrigin, bool bInLinear, FIntPoint InTextureSize)
{
	FDreamUILayerStyleContext Context;
	Context.bLinear = bInLinear;
	// A canvas point on the bottom target, in pixels: as FDreamUICompositor::ProjectBounds puts it there.
	auto ToPixel = [&InCanvasToWorld, &InWorldToClip, &InViewRect](const FVector& InCanvas, FVector2D& OutPixel)
	{
		const FVector World = InCanvasToWorld.TransformPosition(InCanvas);
		const FVector4 Clip = InWorldToClip.TransformFVector4(FVector4(World, 1.0));
		if (Clip.W <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		OutPixel = FVector2D(InViewRect.Min.X + (Clip.X / Clip.W * 0.5 + 0.5) * InViewRect.Width(), InViewRect.Min.Y + (0.5 - Clip.Y / Clip.W * 0.5) * InViewRect.Height());
		return true;
	};
	const FVector Centre(0.0, InStyleRect.X, InStyleRect.Y);
	FVector2D A, B, C;
	if (ToPixel(Centre, A) && ToPixel(Centre + FVector(0.0, 1.0, 0.0), B) && ToPixel(Centre + FVector(0.0, 0.0, 1.0), C))
	{
		Context.PixelScale = static_cast<float>(FMath::Max((FVector2D::Distance(A, B) + FVector2D::Distance(A, C)) * 0.5, 1e-3));
		const FVector2D Texel = A - FVector2D(InTextureOrigin);
		Context.LayerRect = FVector4f(static_cast<float>(Texel.X), static_cast<float>(Texel.Y), InStyleRect.Z * Context.PixelScale, InStyleRect.W * Context.PixelScale);
	}
	if (InStyleRect.Z <= 0.0f || InStyleRect.W <= 0.0f)
	{
		// No rect of its own: the texture's.
		const FVector2f Half(FMath::Max(InTextureSize.X, 1) * 0.5f, FMath::Max(InTextureSize.Y, 1) * 0.5f);
		Context.LayerRect = FVector4f(Half.X, Half.Y, Half.X, Half.Y);
	}
	return Context;
}
void FDreamUILayerStyleComposite::Apply_RenderThread(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGTextureRef InGroup,
	const FDreamUILayerStyleContext& InContext, TArray<FDreamUILayerStyleLayer, TInlineAllocator<4>>& OutLayers) const
{
	OutLayers.Reset();
	auto ContentOnly = [&OutLayers, InGroup]()
	{
		FDreamUILayerStyleLayer& Content = OutLayers.AddDefaulted_GetRef();
		Content.Texture = InGroup;
		Content.bContent = true;
	};
	const int32 NumSlots = FMath::Clamp(Record.NumOuterSlots, 0, MaxOuterSlots);
	FDreamUILayerStyleCompositePS::FPermutationDomain Permutation;
	Permutation.Set<FDreamUILayerStyleCompositePS::FSlots>(NumSlots);
	TShaderMapRef<FDreamUILayerStyleCompositePS> PixelShader(ShaderMap, Permutation);
	if (InGroup == nullptr || Record.IsEmpty() || !PixelShader.IsValid())
	{
		ContentOnly();
		return;
	}
	FRDGTextureRef Field = JumpFlood(GraphBuilder, ShaderMap, InGroup);
	if (Field == nullptr)
	{
		ContentOnly();
		return;
	}
	if (!RecordRHI.IsValid())
	{
		// Made from the record the first time it is drawn, and kept: the record never changes.
		const int32 Width = Record.Texels.Num();
		const FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("DreamUI_StyleRecord"), Width, 1, PF_A32B32G32R32F)
			.SetFlags(ETextureCreateFlags::ShaderResource)
			.SetInitialState(ERHIAccess::SRVMask);
		RecordRHI = GraphBuilder.RHICmdList.CreateTexture(Desc);
		GraphBuilder.RHICmdList.UpdateTexture2D(RecordRHI, 0, FUpdateTextureRegion2D(0, 0, 0, 0, Width, 1), Width * sizeof(FVector4f),
			reinterpret_cast<const uint8*>(Record.Texels.GetData()));
	}
	const FIntPoint Size = InGroup->Desc.Extent;
	const ETextureCreateFlags Srgb = EnumHasAnyFlags(InGroup->Desc.Flags, TexCreate_SRGB) ? TexCreate_SRGB : TexCreate_None;
	const FRDGTextureDesc LayerDesc = FRDGTextureDesc::Create2D(Size, InGroup->Desc.Format, FClearValueBinding::Transparent, Srgb | TexCreate_RenderTargetable | TexCreate_ShaderResource);
	FDreamUILayerStyleCompositePS::FParameters* Parameters = GraphBuilder.AllocParameters<FDreamUILayerStyleCompositePS::FParameters>();
	Parameters->DreamUI_StyleGroup = InGroup;
	Parameters->DreamUI_StyleField = Field;
	Parameters->DreamUI_StyleRecord = RecordRHI;
	FRHISamplerState* Wrap = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	Parameters->DreamUI_StylePattern0 = PatternRHI[0].IsValid() ? PatternRHI[0].GetReference() : GWhiteTexture->TextureRHI.GetReference();
	Parameters->DreamUI_StylePatternSampler0 = Wrap;
	Parameters->DreamUI_StylePattern1 = PatternRHI[1].IsValid() ? PatternRHI[1].GetReference() : GWhiteTexture->TextureRHI.GetReference();
	Parameters->DreamUI_StylePatternSampler1 = Wrap;
	Parameters->DreamUI_StyleFrame = InContext.LayerRect;
	Parameters->DreamUI_StyleParams = FVector4f(FMath::Max(InContext.PixelScale, 1e-3f), InContext.bLinear ? 1.0f : 0.0f, static_cast<float>(Size.X), static_cast<float>(Size.Y));
	FRDGTextureRef Content = GraphBuilder.CreateTexture(LayerDesc, TEXT("DreamUI_StyledGroup"));
	Parameters->RenderTargets[0] = FRenderTargetBinding(Content, ERenderTargetLoadAction::ENoAction);
	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		FDreamUILayerStyleLayer& Layer = OutLayers.AddDefaulted_GetRef();
		Layer.Texture = GraphBuilder.CreateTexture(LayerDesc, TEXT("DreamUI_StyleOuterSlot"));
		Layer.BlendMode = Record.OuterSlotModes[Slot];
		Parameters->RenderTargets[Slot + 1] = FRenderTargetBinding(Layer.Texture, ERenderTargetLoadAction::ENoAction);
	}
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("DreamUILayerStyle"), PixelShader, Parameters, FIntRect(FIntPoint::ZeroValue, Size));
	FDreamUILayerStyleLayer& Last = OutLayers.AddDefaulted_GetRef();
	Last.Texture = Content;
	Last.bContent = true;
}
