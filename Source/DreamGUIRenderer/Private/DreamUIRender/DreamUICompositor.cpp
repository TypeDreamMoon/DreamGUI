// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIRender/DreamUICompositor.h"

#include "CommonRenderResources.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "DreamUIRender/DreamUIRenderStats.h"

/** One triangle over the viewport, from the vertex index alone. */
class FDreamUICompositeVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUICompositeVS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUICompositeVS, FGlobalShader);
	using FParameters = FEmptyShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

/** A group's texture put down: DreamUIComposite.usf. Permutations: blended over a copy of what is behind, and masked. */
class FDreamUICompositePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDreamUICompositePS);
	SHADER_USE_PARAMETER_STRUCT(FDreamUICompositePS, FGlobalShader);

	class FBackdrop : SHADER_PERMUTATION_BOOL("DREAMUI_COMPOSITE_BACKDROP");
	class FMask : SHADER_PERMUTATION_BOOL("DREAMUI_COMPOSITE_MASK");
	/** The group's texture is one kept from frame to frame, sampled over a rect of the canvas (FDreamUICompositeDesc::bExternal). */
	class FExternal : SHADER_PERMUTATION_BOOL("DREAMUI_COMPOSITE_EXTERNAL");
	using FPermutationDomain = TShaderPermutationDomain<FBackdrop, FMask, FExternal>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_GroupTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_GroupTexSampler)
		SHADER_PARAMETER(FMatrix44f, DreamUI_CanvasToUV)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_UnderlayTex)
		SHADER_PARAMETER(FVector4f, DreamUI_CompositeOrigins)
		SHADER_PARAMETER(FVector4f, DreamUI_CompositeParams)
		SHADER_PARAMETER(FVector4f, DreamUI_CompositeFlags)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_BackdropTex)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_ClipDataTex)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_MaskTex0)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_MaskTexSampler0)
		SHADER_PARAMETER_TEXTURE(Texture2D, DreamUI_MaskTex1)
		SHADER_PARAMETER_SAMPLER(SamplerState, DreamUI_MaskTexSampler1)
		SHADER_PARAMETER(float, DreamUI_MaskClipCoordinate)
		SHADER_PARAMETER(FMatrix44f, DreamUI_ClipToCanvas)
		SHADER_PARAMETER(FVector4f, DreamUI_ViewportRect)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

IMPLEMENT_GLOBAL_SHADER(FDreamUICompositeVS, "/Plugin/DreamGUI/Private/DreamUIComposite.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FDreamUICompositePS, "/Plugin/DreamGUI/Private/DreamUIComposite.usf", "MainPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FDreamUICompositePassParameters, )
	RDG_TEXTURE_ACCESS(GroupTexture, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(UnderlayTexture, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(BackdropTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

namespace DreamUICompositorLocal
{
	/** Where A and B overlap; empty (Min == Max) where they do not. */
	FIntRect Intersect(const FIntRect& A, const FIntRect& B)
	{
		const FIntPoint Min(FMath::Max(A.Min.X, B.Min.X), FMath::Max(A.Min.Y, B.Min.Y));
		const FIntPoint Max(FMath::Min(A.Max.X, B.Max.X), FMath::Min(A.Max.Y, B.Max.Y));
		if (Max.X <= Min.X || Max.Y <= Min.Y)
		{
			return FIntRect();
		}
		return FIntRect(Min, Max);
	}

	bool IsEmpty(const FIntRect& R)
	{
		return R.Width() <= 0 || R.Height() <= 0;
	}

	/** A texture of InSize for a group or a copy, in InLike's format, sRGB when it is. */
	ETextureCreateFlags SrgbOf(FRDGTextureRef InLike)
	{
		return EnumHasAnyFlags(InLike->Desc.Flags, TexCreate_SRGB) ? TexCreate_SRGB : TexCreate_None;
	}

	/** How many of the view's pixels a unit of the group's canvas is, at the middle of what the group covers: 1 where unknown. */
	float PixelsPerCanvasUnit(const FDreamUICompositeDesc& InDesc, const FMatrix& InWorldToClip, const FIntRect& InViewRect)
	{
		const FVector Centre = InDesc.WorldBounds.IsValid ? InDesc.WorldBounds.GetCenter() : InDesc.CanvasToWorld.GetOrigin();
		auto ToPixel = [&InWorldToClip, &InViewRect](const FVector& InWorld, FVector2D& OutPixel)
		{
			const FVector4 Clip = InWorldToClip.TransformFVector4(FVector4(InWorld, 1.0));
			if (Clip.W <= UE_KINDA_SMALL_NUMBER)
			{
				return false;
			}
			OutPixel = FVector2D((Clip.X / Clip.W * 0.5 + 0.5) * InViewRect.Width(), (0.5 - Clip.Y / Clip.W * 0.5) * InViewRect.Height());
			return true;
		};
		FVector2D A, B, C;
		if (!ToPixel(Centre, A) || !ToPixel(Centre + InDesc.CanvasToWorld.TransformVector(FVector(0.0, 1.0, 0.0)), B)
			|| !ToPixel(Centre + InDesc.CanvasToWorld.TransformVector(FVector(0.0, 0.0, 1.0)), C))
		{
			return 1.0f;
		}
		return static_cast<float>(FMath::Max((FVector2D::Distance(A, B) + FVector2D::Distance(A, C)) * 0.5, 1e-3));
	}
}

FIntRect FDreamUICompositor::ProjectBounds(const FBox& InWorldBox, const FMatrix& InWorldToClip, const FIntRect& InViewRect)
{
	if (!InWorldBox.IsValid)
	{
		return InViewRect;
	}
	FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D Max(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Point((Corner & 1) ? InWorldBox.Max.X : InWorldBox.Min.X, (Corner & 2) ? InWorldBox.Max.Y : InWorldBox.Min.Y,
			(Corner & 4) ? InWorldBox.Max.Z : InWorldBox.Min.Z);
		const FVector4 Clip = InWorldToClip.TransformFVector4(FVector4(Point, 1.0));
		// A corner at or behind the eye has no place on the screen: the box may cover any of it.
		if (Clip.W <= UE_KINDA_SMALL_NUMBER)
		{
			return InViewRect;
		}
		const FVector2D Ndc(Clip.X / Clip.W, Clip.Y / Clip.W);
		const FVector2D Pixel(InViewRect.Min.X + (Ndc.X * 0.5 + 0.5) * InViewRect.Width(), InViewRect.Min.Y + (0.5 - Ndc.Y * 0.5) * InViewRect.Height());
		Min = FVector2D(FMath::Min(Min.X, Pixel.X), FMath::Min(Min.Y, Pixel.Y));
		Max = FVector2D(FMath::Max(Max.X, Pixel.X), FMath::Max(Max.Y, Pixel.Y));
	}
	// A pixel of room all round: an edge's antialiasing reaches past the box by up to a pixel.
	const FIntRect Covered(FMath::FloorToInt(Min.X) - 1, FMath::FloorToInt(Min.Y) - 1, FMath::CeilToInt(Max.X) + 1, FMath::CeilToInt(Max.Y) + 1);
	return DreamUICompositorLocal::Intersect(Covered, InViewRect);
}

FDreamUICompositeStack::FDreamUICompositeStack(FRDGBuilder& InGraphBuilder, FDreamUIRenderer* InRenderer, FRDGTextureRef InTarget, FTextureRHIRef InTargetRHI,
	const FIntRect& InViewRect, uint8 InNumSamples, const FMatrix& InWorldToClip, bool bInOpaqueBackdrop, bool bInLinearTarget,
	ERHIFeatureLevel::Type InFeatureLevel)
	: GraphBuilder(InGraphBuilder)
	, Renderer(InRenderer)
	, ViewRect(InViewRect)
	, WorldToClip(InWorldToClip)
	, bOpaqueBackdrop(bInOpaqueBackdrop)
	, bLinearTarget(bInLinearTarget)
	, FeatureLevel(InFeatureLevel)
{
	FFrame& Bottom = Frames.AddDefaulted_GetRef();
	Bottom.Texture = InTarget;
	Bottom.TextureRHI = InTargetRHI;
	Bottom.Rect = FIntRect(FIntPoint::ZeroValue, InTarget != nullptr ? InTarget->Desc.Extent : InViewRect.Max);
	Bottom.NumSamples = InNumSamples;
}

FVector4f FDreamUICompositeStack::GetCurrentTextureScaleOffset(const FVector4f& InBottom) const
{
	if (IsAtBottom())
	{
		return InBottom;
	}
	const FIntPoint BottomSize = Frames[0].Texture->Desc.Extent;
	const FIntPoint Size = Current().Texture->Desc.Extent;
	const FIntPoint Origin = Current().Origin;
	return FVector4f(
		InBottom.X * BottomSize.X / Size.X,
		InBottom.Y * BottomSize.Y / Size.Y,
		(InBottom.Z * BottomSize.X - Origin.X) / Size.X,
		(InBottom.W * BottomSize.Y - Origin.Y) / Size.Y);
}

void FDreamUICompositeStack::Begin(const FDreamUICompositeDesc& InDesc)
{
	using namespace DreamUICompositorLocal;
	const FFrame& Parent = Frames.Last();
	FFrame Frame;
	Frame.Desc = InDesc;
	Frame.bCulled = Parent.bCulled;
	// What clips to a layer below shows only where that layer was put down: nowhere, when it was not.
	if (InDesc.Role == FDreamUICompositeDesc::ERole::ClipBody && (Parent.HeldBase == nullptr || Parent.HeldBaseId != InDesc.Id))
	{
		Frame.bCulled = true;
	}
	if (!Frame.bCulled)
	{
		Frame.Rect = Intersect(FDreamUICompositor::ProjectBounds(InDesc.WorldBounds, WorldToClip, ViewRect), Parent.Rect);
		Frame.bCulled = IsEmpty(Frame.Rect);
	}
	if (!Frame.bCulled && InDesc.bExternal)
	{
		// Drawn already: nothing goes into it here, and it is put down as it is when it closes.
		Frame.Origin = Frame.Rect.Min;
		Frame.bCulled = !InDesc.Textures.MainTextureRHI.IsValid();
		Frames.Add(MoveTemp(Frame));
		return;
	}
	if (!Frame.bCulled)
	{
		const FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(Frame.Rect.Size(), Parent.Texture->Desc.Format, FClearValueBinding::Transparent
			, SrgbOf(Parent.Texture), TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, Frame.Pooled, TEXT("DreamUI_GroupTexture"));
		if (Frame.Pooled.IsValid())
		{
			// The pool's element itself, so that it is not handed out again before the passes recorded here have run.
			Frame.Texture = GraphBuilder.RegisterExternalTexture(Frame.Pooled, TEXT("DreamUI_GroupTexture"));
			Frame.TextureRHI = Frame.Pooled->GetRHI();
			Frame.Origin = Frame.Rect.Min;
			Frame.NumSamples = 1;
			AddClearRenderTargetPass(GraphBuilder, Frame.Texture, FLinearColor::Transparent);
		}
		else
		{
			Frame.bCulled = true;
		}
	}
	Frames.Add(MoveTemp(Frame));
}

void FDreamUICompositeStack::End(uint32 InId)
{
	int32 Index = Frames.Num() - 1;
	while (Index > 0 && Frames[Index].Desc.Id != InId)
	{
		--Index;
	}
	// A close with nothing of its own open is left alone: the group it closes was never opened in this recording.
	if (Index <= 0)
	{
		return;
	}
	while (Frames.Num() > Index)
	{
		PopAndComposite();
	}
}

void FDreamUICompositeStack::EndAll()
{
	while (Frames.Num() > 1)
	{
		PopAndComposite();
	}
}

FRDGTextureRef FDreamUICompositeStack::CopyBackdrop(const FIntRect& InRect, FIntPoint& OutOrigin, const TCHAR* InName)
{
	return CopyFrom(Frames.Last(), InRect, OutOrigin, InName);
}

FRDGTextureRef FDreamUICompositeStack::CopyFrom(const FFrame& InFrame, const FIntRect& InRect, FIntPoint& OutOrigin, const TCHAR* InName)
{
	using namespace DreamUICompositorLocal;
	OutOrigin = FIntPoint::ZeroValue;
	if (InFrame.bCulled || InFrame.Texture == nullptr)
	{
		return nullptr;
	}
	const FIntRect Local = Intersect(InRect, InFrame.Rect) - InFrame.Origin;
	if (IsEmpty(Local))
	{
		return nullptr;
	}
	DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::BackdropCopies, 1);
	const FRDGTextureDesc& Source = InFrame.Texture->Desc;
	if (Source.NumSamples > 1)
	{
		// A multisampled target is resolved where it is read, not copied a region at a time: the whole of it, its first texel
		// the target's.
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Source.Extent, Source.Format, FClearValueBinding::Transparent
			, SrgbOf(InFrame.Texture) | TexCreate_RenderTargetable | TexCreate_ShaderResource);
		FRDGTextureRef Resolved = GraphBuilder.CreateTexture(Desc, InName);
		if (Renderer != nullptr)
		{
			Renderer->AddResolvePass(GraphBuilder, FRDGTextureMSAA(InFrame.Texture, Resolved), Local, Source.NumSamples, GetGlobalShaderMap(FeatureLevel));
		}
		return Resolved;
	}
	const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Local.Size(), Source.Format, FClearValueBinding::Transparent
		, SrgbOf(InFrame.Texture) | TexCreate_ShaderResource);
	FRDGTextureRef Copy = GraphBuilder.CreateTexture(Desc, InName);
	AddCopyTexturePass(GraphBuilder, InFrame.Texture, Copy, Local.Min, FIntPoint::ZeroValue, Local.Size());
	OutOrigin = Local.Min;
	return Copy;
}

void FDreamUICompositeStack::PopAndComposite()
{
	FFrame Top = Frames.Pop();
	FFrame& Parent = Frames.Last();
	const FDreamUICompositeDesc& Desc = Top.Desc;
	FRDGTextureRef Underlay = nullptr;
	FIntPoint UnderlayOrigin = FIntPoint::ZeroValue;
	if (Desc.Role == FDreamUICompositeDesc::ERole::ClipBase)
	{
		// Held for what clips to it, which comes next among the parent's draws; put down as it is meanwhile.
		Parent.HeldBase = Top.bCulled ? nullptr : Top.Texture;
		Parent.HeldBaseOrigin = Top.Origin;
		Parent.HeldBaseId = Desc.Id;
		Parent.bHeldBaseCulled = Top.bCulled;
	}
	else if (Desc.Role == FDreamUICompositeDesc::ERole::ClipBody)
	{
		if (Parent.HeldBaseId == Desc.Id)
		{
			Underlay = Parent.HeldBase;
			UnderlayOrigin = Parent.HeldBaseOrigin;
		}
		Parent.HeldBase = nullptr;
		Parent.HeldBaseId = 0;
	}
	const bool bExternal = Desc.bExternal;
	if (Top.bCulled || (Top.Texture == nullptr && !bExternal) || Parent.bCulled || Parent.Texture == nullptr)
	{
		return;
	}
	if (Desc.Role == FDreamUICompositeDesc::ERole::ClipBody && Underlay == nullptr)
	{
		return;
	}
	++CompositeCount;
	DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::GroupsComposited, 1);

	// Every texture here is on the target's pixel grid; in the parent's texels the group's rect is its rect less the
	// parent's origin, and so is the view's.
	const FIntRect DestRect = Top.Rect - Parent.Origin;
	const FIntRect ParentViewport = ViewRect - Parent.Origin;
	FIntPoint BackdropOrigin = FIntPoint::ZeroValue;
	FRDGTextureRef Backdrop = Desc.ReadsBackdrop() ? CopyFrom(Parent, Top.Rect, BackdropOrigin, TEXT("DreamUI_GroupBackdrop")) : nullptr;
	const bool bReadBackdrop = Backdrop != nullptr;
	// A group of a mode that reads what is behind is put down Normal where there is nothing to read.
	const EDreamUIBlendMode Mode = Desc.ReadsBackdrop() && !bReadBackdrop ? EDreamUIBlendMode::Alpha : Desc.BlendMode;
	const bool bOpaque = bOpaqueBackdrop && Frames.Num() == 1;

	FDreamUICompositePassParameters* PassParameters = GraphBuilder.AllocParameters<FDreamUICompositePassParameters>();
	PassParameters->GroupTexture = bExternal ? nullptr : Top.Texture;
	PassParameters->UnderlayTexture = Underlay;
	PassParameters->BackdropTexture = Backdrop;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Parent.Texture, ERenderTargetLoadAction::ELoad);

	const FMatrix44f ClipToCanvas = FMatrix44f((Desc.CanvasToWorld * WorldToClip).Inverse());
	const FVector4f Origins(Top.Origin.X - Parent.Origin.X, Top.Origin.Y - Parent.Origin.Y
		, Underlay != nullptr ? UnderlayOrigin.X - Parent.Origin.X : 0.0f, Underlay != nullptr ? UnderlayOrigin.Y - Parent.Origin.Y : 0.0f);
	const FVector4f Params(static_cast<float>(Mode), FMath::Clamp(Desc.Opacity, 0.0f, 1.0f), bLinearTarget ? 1.0f : 0.0f, bOpaque ? 1.0f : 0.0f);
	const FVector4f Flags(Underlay != nullptr ? 1.0f : 0.0f, static_cast<float>(BackdropOrigin.X), static_cast<float>(BackdropOrigin.Y), 0.0f);
	const FVector4f ViewportRect(ParentViewport.Min.X, ParentViewport.Min.Y, FMath::Max(ParentViewport.Width(), 1), FMath::Max(ParentViewport.Height(), 1));
	const bool bMask = Desc.HasMask() && Desc.Textures.ClipDataTextureRHI.IsValid();
	FRDGTextureRef GroupTexture = Top.Texture;
	if (Desc.Effect.IsValid() && !bExternal)
	{
		// The group's picture through its effect first: the effect's own opacity and mode mix it with what it was, and the
		// group puts down what comes out.
		FDreamUIImageEffectContext EffectContext;
		EffectContext.bLinear = bLinearTarget;
		EffectContext.bOpaque = false;
		EffectContext.TimeSeconds = TimeSeconds;
		EffectContext.Origin = Top.Origin;
		EffectContext.Frame = FVector4f(0.0f, 0.0f, static_cast<float>(Top.Rect.Width()), static_cast<float>(Top.Rect.Height()));
		EffectContext.PixelScale = DreamUICompositorLocal::PixelsPerCanvasUnit(Desc, WorldToClip, ViewRect);
		GroupTexture = Desc.Effect->Apply_RenderThread(GraphBuilder, GetGlobalShaderMap(FeatureLevel), GroupTexture, EffectContext);
		PassParameters->GroupTexture = GroupTexture;
	}
	const uint8 NumSamples = Parent.NumSamples;
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("DreamUIComposite"),
		PassParameters,
		ERDGPassFlags::Raster,
		[GroupTexture, Underlay, Backdrop, DestRect, Origins, Params, Flags, ViewportRect, ClipToCanvas, bMask, Mode, NumSamples, bReadBackdrop
			, Textures = Desc.Textures, MaskClipCoordinate = Desc.MaskClipCoordinate, FeatureLevel = FeatureLevel, bExternal, CanvasToUV = Desc.CanvasToUV](FRHICommandList& RHICmdList)
		{
			if (!bExternal)
			{
				GroupTexture->MarkResourceAsUsed();
			}
			RHICmdList.SetViewport(DestRect.Min.X, DestRect.Min.Y, 0.0f, DestRect.Max.X, DestRect.Max.Y, 1.0f);
			FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
			TShaderMapRef<FDreamUICompositeVS> VertexShader(GlobalShaderMap);
			FDreamUICompositePS::FPermutationDomain Permutation;
			Permutation.Set<FDreamUICompositePS::FBackdrop>(bReadBackdrop);
			Permutation.Set<FDreamUICompositePS::FMask>(bMask);
			Permutation.Set<FDreamUICompositePS::FExternal>(bExternal);
			TShaderMapRef<FDreamUICompositePS> PixelShader(GlobalShaderMap, Permutation);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.BlendState = FDreamUIRenderer::GetBuiltInBlendState(Mode);
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.NumSamples = NumSamples;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			auto TextureOr = [](FRHITextureReference* InReference, FTexture* InFallback) -> FRHITexture*
			{
				return InReference != nullptr ? static_cast<FRHITexture*>(InReference) : InFallback->TextureRHI.GetReference();
			};
			FRHISamplerState* Bilinear = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			FDreamUICompositePS::FParameters Parameters;
			Parameters.DreamUI_GroupTex = bExternal ? TextureOr(Textures.MainTextureRHI.GetReference(), GBlackTexture) : GroupTexture->GetRHI();
			Parameters.DreamUI_GroupTexSampler = Textures.MainSamplerRHI.IsValid() ? Textures.MainSamplerRHI.GetReference() : Bilinear;
			Parameters.DreamUI_CanvasToUV = CanvasToUV;
			Parameters.DreamUI_UnderlayTex = Underlay != nullptr ? Underlay->GetRHI() : GBlackTexture->TextureRHI.GetReference();
			Parameters.DreamUI_CompositeOrigins = Origins;
			Parameters.DreamUI_CompositeParams = Params;
			Parameters.DreamUI_CompositeFlags = Flags;
			Parameters.DreamUI_BackdropTex = Backdrop != nullptr ? Backdrop->GetRHI() : GBlackTexture->TextureRHI.GetReference();
			Parameters.DreamUI_ClipDataTex = TextureOr(Textures.ClipDataTextureRHI.GetReference(), GBlackTexture);
			Parameters.DreamUI_MaskTex0 = TextureOr(Textures.MaskTexture0RHI.GetReference(), GWhiteTexture);
			Parameters.DreamUI_MaskTexSampler0 = Textures.MaskSampler0RHI.IsValid() ? Textures.MaskSampler0RHI.GetReference() : Bilinear;
			Parameters.DreamUI_MaskTex1 = TextureOr(Textures.MaskTexture1RHI.GetReference(), GWhiteTexture);
			Parameters.DreamUI_MaskTexSampler1 = Textures.MaskSampler1RHI.IsValid() ? Textures.MaskSampler1RHI.GetReference() : Bilinear;
			Parameters.DreamUI_MaskClipCoordinate = MaskClipCoordinate;
			Parameters.DreamUI_ClipToCanvas = ClipToCanvas;
			Parameters.DreamUI_ViewportRect = ViewportRect;
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Parameters);

			RHICmdList.DrawPrimitive(0, 1, 1);
		});
}
