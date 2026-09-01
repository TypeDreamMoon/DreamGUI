// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamPixelSort.h"

#include "Core/Components/DreamPixelSortRenderProxy.h"
#include "Core/DreamUIRender/DreamUIPostProcessShaders.h"
#include "Core/DreamUIRender/DreamUIRenderer.h"
#include "DreamGUI.h"
#include "PipelineStateCache.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"
#include "ScreenRendering.h"
#include "Core/DreamUIWidgetRegistry.h"

DECLARE_CYCLE_STAT(TEXT("PostProcess PixelSort"), STAT_PixelSort, STATGROUP_DreamGUI);

BEGIN_SHADER_PARAMETER_STRUCT(FDreamUIPixelSortPassParameters, )
	RDG_TEXTURE_ACCESS(SourceTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

// The gather reads TWO textures, and both accesses must be declared or RDG inserts no barrier
// between the rank pass writing the destinations and this pass reading them.
BEGIN_SHADER_PARAMETER_STRUCT(FDreamUIPixelSortGatherParameters, )
	RDG_TEXTURE_ACCESS(SourceTexture, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(DestinationTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

//------------------------------------------------------------------------------------------------
// The arithmetic. Mirrored by DreamUIPostProcessPixelSort.usf -- keep the two in step.
//------------------------------------------------------------------------------------------------
namespace DreamPixelSort
{
	FVector2f ResolveBand(float InFirst, float InSecond)
	{
		const float Low = FMath::Clamp(FMath::Min(InFirst, InSecond), 0.0f, 1.0f);
		const float High = FMath::Clamp(FMath::Max(InFirst, InSecond), 0.0f, 1.0f);
		return FVector2f(Low, High);
	}

	int32 ResolvePassCount(float InStrength, int32 InMaxPasses)
	{
		const int32 MaxPasses = FMath::Max(InMaxPasses, 0);
		const float Strength = FMath::Clamp(InStrength, 0.0f, 1.0f);
		return FMath::Clamp(FMath::RoundToInt(Strength * MaxPasses), 0, MaxPasses);
	}

	float ComputeKey(const FLinearColor& InColor, EDreamPixelSortKey InKey)
	{
		switch (InKey)
		{
		case EDreamPixelSortKey::Brightness:
			return FMath::Max3(InColor.R, InColor.G, InColor.B);
		case EDreamPixelSortKey::Saturation:
		{
			const float MaxChannel = FMath::Max3(InColor.R, InColor.G, InColor.B);
			const float MinChannel = FMath::Min3(InColor.R, InColor.G, InColor.B);
			return MaxChannel > UE_SMALL_NUMBER ? (MaxChannel - MinChannel) / MaxChannel : 0.0f;
		}
		case EDreamPixelSortKey::Hue:
		{
			// 0..1 around the wheel. This WRAPS, so a run spanning red splits rather than sorting
			// smoothly -- inherent to ordering an angle, and the reason hue reads as bands of colour
			// rather than as a gradient.
			const float MaxChannel = FMath::Max3(InColor.R, InColor.G, InColor.B);
			const float MinChannel = FMath::Min3(InColor.R, InColor.G, InColor.B);
			const float Chroma = MaxChannel - MinChannel;
			if (Chroma <= UE_SMALL_NUMBER)
			{
				return 0.0f;//grey has no hue
			}
			float Hue;
			if (MaxChannel == InColor.R)      Hue = (InColor.G - InColor.B) / Chroma;
			else if (MaxChannel == InColor.G) Hue = 2.0f + (InColor.B - InColor.R) / Chroma;
			else                              Hue = 4.0f + (InColor.R - InColor.G) / Chroma;
			Hue /= 6.0f;
			return Hue < 0.0f ? Hue + 1.0f : Hue;
		}
		case EDreamPixelSortKey::Intensity:
			return (InColor.R + InColor.G + InColor.B) / 3.0f;
		case EDreamPixelSortKey::Minimum:
			return FMath::Min3(InColor.R, InColor.G, InColor.B);
		case EDreamPixelSortKey::Alpha:
			return InColor.A;
		case EDreamPixelSortKey::Luminance:
		default:
			return 0.2126f * InColor.R + 0.7152f * InColor.G + 0.0722f * InColor.B;
		}
	}

	bool IsInBand(float InKey, const FVector2f& InBand)
	{
		// Saturated for the band test ONLY. The band is authored in 0..1, but the key comes from
		// whatever format the backbuffer happens to be, and on a float target values above 1 are
		// ordinary. Clamping the comparison instead would freeze every highlight out of the sort.
		const float Clamped = FMath::Clamp(InKey, 0.0f, 1.0f);
		return Clamped >= InBand.X && Clamped <= InBand.Y;
	}

	FIntPoint ResolveRegionSize(bool bInUseFullSize, const FVector2f& InRectSize, const FIntPoint& InScreenSize)
	{
		if (bInUseFullSize)
		{
			return InScreenSize;
		}
		return FIntPoint(FMath::Max((int32)InRectSize.X, 1), FMath::Max((int32)InRectSize.Y, 1));
	}

	uint32 Hash(uint32 InValue)
	{
		// A stable 32-bit mixer, chosen because it is trivially reproducible in HLSL. The runs must
		// be identical on both sides or the tests describe a different image from the one drawn.
		InValue ^= InValue >> 16;
		InValue *= 0x7feb352du;
		InValue ^= InValue >> 15;
		InValue *= 0x846ca68bu;
		InValue ^= InValue >> 16;
		return InValue;
	}

	bool IsSortable(float InKey, int32 InIndex, const FDreamPixelSortRunRules& InRules)
	{
		const uint32 LineSeed = Hash((uint32)InRules.LineIndex * 0x9e3779b9u);
		const int32 Length = FMath::Max(InRules.IntervalLength, 2);

		// Which run this texel belongs to, and whether it is a wall. Threshold asks the image; the
		// other two ask only the position, which is exactly why they impose a pattern rather than
		// following one.
		int32 RunIndex = 0;
		switch (InRules.Interval)
		{
		case EDreamPixelSortInterval::Threshold:
			if (!IsInBand(InKey, InRules.Band))return false;
			// Runs are delimited by the image, so there is no index to speak of. Randomness falls
			// back to a coarse spatial block, which breaks the line up without needing a scan the
			// shader could not do anyway.
			RunIndex = InIndex / Length;
			break;
		case EDreamPixelSortInterval::Waves:
			RunIndex = InIndex / Length;
			// A wall every Length texels, jittered per line so rows do not align into a grid.
			if ((InIndex % Length) == (int32)(LineSeed % (uint32)Length))return false;
			break;
		case EDreamPixelSortInterval::Random:
		{
			// Walls scattered at an average spacing of Length. Position-only, like Waves, but
			// without the regularity.
			RunIndex = InIndex / Length;
			const uint32 Roll = Hash(LineSeed ^ ((uint32)InIndex * 0x85ebca6bu));
			if ((Roll % (uint32)Length) == 0u)return false;
			break;
		}
		case EDreamPixelSortInterval::None:
		default:
			RunIndex = 0;
			break;
		}

		if (InRules.Randomness > 0.0f)
		{
			// Dropped a RUN at a time rather than a pixel at a time -- per-pixel would dissolve into
			// noise instead of leaving recognisable stretches untouched, which is the look this is
			// borrowed from.
			const uint32 Roll = Hash(LineSeed ^ ((uint32)RunIndex * 0xc2b2ae35u));
			if ((Roll & 0xffffffu) < (uint32)(InRules.Randomness * (float)0xffffff))return false;
		}
		return true;
	}

	bool ShouldExchange(float InLowerKey, float InUpperKey, bool bInDescending)
	{
		// ONE expression, evaluated identically by both members of a pair. Writing the mirrored form
		// on the other side is the natural thing to do and it duplicates a texel on every tie --
		// and flat UI backgrounds are nothing but ties.
		return bInDescending ? (InLowerKey < InUpperKey) : (InLowerKey > InUpperKey);
	}

	int32 ComputeDestination(const TArray<float>& InKeys, int32 InIndex,
		const FDreamPixelSortRunRules& InRules, bool bInDescending, int32 InSearchRadius)
	{
		const int32 Count = InKeys.Num();
		if (InIndex < 0 || InIndex >= Count)
		{
			return InIndex;
		}
		const float SelfKey = InKeys[InIndex];
		if (!IsSortable(SelfKey, InIndex, InRules))
		{
			return InIndex;//a wall never moves
		}

		const int32 Radius = FMath::Max(InSearchRadius, 1);
		int32 Head = InIndex;
		int32 Tail = InIndex;
		int32 Rank = 0;

		for (int32 Step = 0; Step < Radius; ++Step)
		{
			const int32 Probe = InIndex - Step - 1;
			if (Probe < 0) { Head = Probe + 1; break; }
			if (!IsSortable(InKeys[Probe], Probe, InRules)) { Head = Probe + 1; break; }
			// <= going back, < going forward. See the header: this asymmetry is what stops two
			// equal-valued texels claiming the same destination.
			if (InKeys[Probe] <= SelfKey) { ++Rank; }
			Head = Probe;
		}
		for (int32 Step = 0; Step < Radius; ++Step)
		{
			const int32 Probe = InIndex + Step + 1;
			if (Probe > Count - 1) { Tail = Probe - 1; break; }
			if (!IsSortable(InKeys[Probe], Probe, InRules)) { Tail = Probe - 1; break; }
			if (InKeys[Probe] < SelfKey) { ++Rank; }
			Tail = Probe;
		}
		return bInDescending ? (Tail - Rank) : (Head + Rank);
	}
}

//------------------------------------------------------------------------------------------------
// Render thread
//------------------------------------------------------------------------------------------------
void FDreamPixelSortRenderProxy::OnRenderPostProcess_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	FDreamUIRenderer* Renderer,
	FTextureRHIRef ScreenTargetTexture,
	FGlobalShaderMap* GlobalShaderMap,
	const FMatrix44f& ViewProjectionMatrix,
	bool bIsWorldSpace,
	bool bIsRenderTarget,
	float BlendDepthForWorld,
	int DepthFadeForWorld,
	const FIntRect& ViewRect,
	const FVector4f& DepthTextureScaleOffset,
	const FVector4f& ViewTextureScaleOffset
)
{
	SCOPE_CYCLE_COUNTER(STAT_PixelSort);
	if (!CanRender())return;

	auto& RHICmdList = GraphBuilder.RHICmdList;

	TRefCountPtr<IPooledRenderTarget> ScreenResolvedTexture;
	TRefCountPtr<IPooledRenderTarget> SortTargetA;
	TRefCountPtr<IPooledRenderTarget> SortTargetB;
	TRefCountPtr<IPooledRenderTarget> IndexTarget;
	// Held for the whole function and released only at the end, as blur does. The pool keeps the RHI
	// texture alive past a SafeRelease, but with two targets and many passes in flight there is no
	// reason to lean on that.
	auto ReleaseRenderTargets = [&] {
		if (ScreenResolvedTexture.IsValid())ScreenResolvedTexture.SafeRelease();
		if (SortTargetA.IsValid())SortTargetA.SafeRelease();
		if (SortTargetB.IsValid())SortTargetB.SafeRelease();
		if (IndexTarget.IsValid())IndexTarget.SafeRelease();
	};

	const uint8 NumSamples = ScreenTargetTexture->GetNumSamples();
	const auto ScreenSize = ScreenTargetTexture->GetSizeXY();
	if (NumSamples > 1)
	{
		// A multisampled screen texture cannot be sampled directly; resolve first. This MUST be
		// AddResolvePass and not CopyRenderTarget: the copy shader declares its source as a plain
		// Texture2D, and binding an MSAA texture to a Texture2D slot is a dimension mismatch that
		// reads as zero on D3D12 -- the resolve silently comes back black and every grab downstream
		// is a grab of nothing. The resolve shader is the one place in the plugin that declares
		// Texture2DMS and loads each sample. Getting this wrong cost a day; see blur and pixelate,
		// which have always done it this way.
		FPooledRenderTargetDesc ResolveDesc(FPooledRenderTargetDesc::Create2DDesc(ScreenSize, ScreenTargetTexture->GetFormat(),
			FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, ResolveDesc, ScreenResolvedTexture, TEXT("DreamUIPixelSortResolveTarget"));
		if (!ScreenResolvedTexture.IsValid())
		{
			ReleaseRenderTargets();
			return;
		}
		auto ResolveSrc = RegisterExternalTexture(GraphBuilder, ScreenTargetTexture, TEXT("DreamUIPixelSortResolveSource"));
		auto ResolveDst = RegisterExternalTexture(GraphBuilder, ScreenResolvedTexture->GetRHI(), TEXT("DreamUIPixelSortResolveTarget"));
		Renderer->AddResolvePass(GraphBuilder, FRDGTextureMSAA(ResolveSrc, ResolveDst), FIntRect(0, 0, ScreenSize.X, ScreenSize.Y), NumSamples, GlobalShaderMap);
	}

	// Read the FLAG, not a coincidence of sizes. bUseFullSize makes RectSize the root canvas's
	// authored resolution, which almost never equals the screen -- so testing sizes takes the
	// widget-rect path while the author has asked for the screen, and the sort then runs in a buffer
	// whose texels are not pixels and whose edges sample past what is on screen.
	const bool bFullScreen = bUseFullSize;
	const FIntPoint RegionSize = DreamPixelSort::ResolveRegionSize(bUseFullSize, RectSize, ScreenSize);
	// Still no in-place shortcut, unlike blur: blur can write straight into the backbuffer because
	// its own passes ping-pong internally, but a sort pass reading and writing one texture produces
	// per-tile garbage that varies by GPU. Full screen here means a screen-sized SCRATCH buffer.

	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(RegionSize, ScreenTargetTexture->GetFormat(),
			FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SortTargetA, TEXT("DreamUIPixelSortTargetA"));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SortTargetB, TEXT("DreamUIPixelSortTargetB"));
		// One float channel for the destination index. R32F holds every integer up to 2^24 exactly,
		// far past any line length -- packing an index into 8-bit RGB, as the reference shader has to
		// on Shadertoy, is unnecessary here and would only add rounding to something that must be
		// compared for equality.
		FPooledRenderTargetDesc IndexDesc(FPooledRenderTargetDesc::Create2DDesc(RegionSize, PF_R32_FLOAT,
			FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, IndexDesc, IndexTarget, TEXT("DreamUIPixelSortIndexTarget"));
		if (!SortTargetA.IsValid() || !SortTargetB.IsValid() || !IndexTarget.IsValid())
		{
			ReleaseRenderTargets();
			return;
		}
	}
	auto SortTextureA = SortTargetA->GetRHI();
	auto SortTextureB = SortTargetB->GetRHI();
	auto IndexTexture = IndexTarget->GetRHI();

	// Grab the widget's region out of the screen.
	const auto ModelViewProjectionMatrix = ObjectToWorldMatrix * ViewProjectionMatrix;
	auto SourceScreenTexture = NumSamples > 1 ? ScreenResolvedTexture->GetRHI() : ScreenTargetTexture.GetReference();
	if (!bFullScreen)
	{
		Renderer->CopyRenderTargetOnMeshRegion(GraphBuilder
			, RegisterExternalTexture(GraphBuilder, SortTextureA, TEXT("DreamUIPixelSortRegionGrab"))
			, SourceScreenTexture
			, GlobalShaderMap
			, RenderScreenToMeshRegionVertexArray
			, ModelViewProjectionMatrix
			, bIsRenderTarget
			, FIntRect(0, 0, RegionSize.X, RegionSize.Y)
			, ViewTextureScaleOffset
		);
	}
	else
	{
		Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, SourceScreenTexture, SortTextureA);
	}

	// Registered once per pooled target. Registering the same RHI texture twice gives RDG two
	// handles onto one resource, so it cannot see the dependency between passes and they race.
	FRDGTextureRef SourceTexture = RegisterExternalTexture(GraphBuilder, SortTextureA, TEXT("DreamUIPixelSortSource"));
	FRDGTextureRef DestinationTexture = RegisterExternalTexture(GraphBuilder, IndexTexture, TEXT("DreamUIPixelSortDestinations"));
	FRDGTextureRef ResultRDGTexture = RegisterExternalTexture(GraphBuilder, SortTextureB, TEXT("DreamUIPixelSortResult"));

	TShaderMapRef<FDreamUISimplePostProcessVS> VertexShader(GlobalShaderMap);
	TShaderMapRef<FDreamUIPostProcessPixelSortRankPS> RankShader(GlobalShaderMap);
	TShaderMapRef<FDreamUIPostProcessPixelSortGatherPS> GatherShader(GlobalShaderMap);

	const FVector2f RegionSizeFloat((float)RegionSize.X, (float)RegionSize.Y);
	const float AxisFlag = SortAxis == EDreamPixelSortAxis::Horizontal ? 0.0f : 1.0f;
	const float KeyFlag = (float)(uint8)SortKey;
	const float DescendingFlag = bDescending ? 1.0f : 0.0f;
	const float RadiusFloat = (float)SearchRadius;
	const FVector4f IntervalParams((float)(uint8)IntervalMode, (float)IntervalLength, Randomness, 0.0f);
	// POINT sampling is mandatory, not a preference: the rank counts exact texels, and a blended
	// sample belongs to no texel at all -- it would make neighbouring invocations disagree about
	// which of them comes first and two texels would claim one destination.
	const auto SortSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	// Pass 1: every texel scans its own run and writes where it is going.
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FDreamUIPixelSortPassParameters>();
		PassParameters->SourceTexture = SourceTexture;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, ERenderTargetLoadAction::ENoAction);
		GraphBuilder.AddPass(RDG_EVENT_NAME("DreamUIPixelSort_Rank"), PassParameters, ERDGPassFlags::Raster,
			[VertexShader, RankShader, Renderer, SourceTexture, DestinationTexture, SortSampler,
			RegionSizeFloat, Band = this->Band, AxisFlag, KeyFlag, DescendingFlag, RadiusFloat, IntervalParams]
			(FRHICommandListImmediate& RHICmdList)
			{
				SourceTexture->MarkResourceAsUsed();
				FGraphicsPipelineStateInitializer PSOInit;
				RHICmdList.ApplyCachedRenderTargets(PSOInit);
				PSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
				PSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				PSOInit.BlendState = TStaticBlendState<>::GetRHI();
				PSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessVertexDeclaration();
				PSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				PSOInit.BoundShaderState.PixelShaderRHI = RankShader.GetPixelShader();
				PSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
				SetGraphicsPipelineState(RHICmdList, PSOInit, 0, EApplyRendertargetOption::CheckApply);
				VertexShader->SetParameters(RHICmdList);
				RHICmdList.SetViewport(0, 0, 0.0f, DestinationTexture->Desc.Extent.X, DestinationTexture->Desc.Extent.Y, 1.0f);
				RankShader->SetParameters(RHICmdList, SourceTexture->GetRHI(), SortSampler,
					RegionSizeFloat, Band, AxisFlag, KeyFlag, DescendingFlag, RadiusFloat, IntervalParams);
				Renderer->DrawFullScreenQuad(RHICmdList);
			});
	}

	// Pass 2: invert that into who comes here, because a pixel shader can only gather.
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FDreamUIPixelSortGatherParameters>();
		PassParameters->SourceTexture = SourceTexture;
		PassParameters->DestinationTexture = DestinationTexture;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(ResultRDGTexture, ERenderTargetLoadAction::ENoAction);
		GraphBuilder.AddPass(RDG_EVENT_NAME("DreamUIPixelSort_Gather"), PassParameters, ERDGPassFlags::Raster,
			[VertexShader, GatherShader, Renderer, SourceTexture, DestinationTexture, ResultRDGTexture,
			SortSampler, RegionSizeFloat, AxisFlag, RadiusFloat](FRHICommandListImmediate& RHICmdList)
			{
				SourceTexture->MarkResourceAsUsed();
				DestinationTexture->MarkResourceAsUsed();
				FGraphicsPipelineStateInitializer PSOInit;
				RHICmdList.ApplyCachedRenderTargets(PSOInit);
				PSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
				PSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				PSOInit.BlendState = TStaticBlendState<>::GetRHI();
				PSOInit.BoundShaderState.VertexDeclarationRHI = GetDreamUIPostProcessVertexDeclaration();
				PSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				PSOInit.BoundShaderState.PixelShaderRHI = GatherShader.GetPixelShader();
				PSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
				SetGraphicsPipelineState(RHICmdList, PSOInit, 0, EApplyRendertargetOption::CheckApply);
				VertexShader->SetParameters(RHICmdList);
				RHICmdList.SetViewport(0, 0, 0.0f, ResultRDGTexture->Desc.Extent.X, ResultRDGTexture->Desc.Extent.Y, 1.0f);
				GatherShader->SetParameters(RHICmdList, SourceTexture->GetRHI(), SortSampler,
					DestinationTexture->GetRHI(), RegionSizeFloat, AxisFlag, RadiusFloat);
				Renderer->DrawFullScreenQuad(RHICmdList);
			});
	}

	auto ResultTexture = SortTextureB;

	const auto PointSampler = SortSampler;
	if (RenderTargetResource == nullptr)
	{
		if (!bFullScreen)
		{
			RenderMeshOnScreen_RenderThread(GraphBuilder, SceneTextures, ScreenTargetTexture, GlobalShaderMap, ResultTexture,
				ModelViewProjectionMatrix, ObjectToWorldMatrix, bIsWorldSpace, BlendDepthForWorld, DepthFadeForWorld,
				DepthTextureScaleOffset, ViewRect, PointSampler);
		}
		else
		{
			Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, ResultTexture, ScreenTargetTexture, PointSampler);
		}
	}
	else
	{
		// Pixelate omits this branch from its GetRenderProxy push chain and the RenderTarget output
		// mode quietly does nothing as a result. Blur's version is the complete one.
		Renderer->CopyRenderTarget_ColorCorrect(GraphBuilder, GlobalShaderMap, ResultTexture,
			RenderTargetResource->GetRenderTargetTexture(), PointSampler);
	}

	ReleaseRenderTargets();
}

//------------------------------------------------------------------------------------------------
// Game thread
//------------------------------------------------------------------------------------------------
UDreamPixelSort::UDreamPixelSort(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
}

void UDreamPixelSort::BeginPlay()
{
	Super::BeginPlay();
	SendOthersDataToRenderProxy();
}

#if WITH_EDITOR
void UDreamPixelSort::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// The details panel writes property memory and then calls this; it does NOT call the setters.
	// Without this the panel shows the new value and the GPU keeps the old one.
	SendOthersDataToRenderProxy();
}
#endif

void UDreamPixelSort::SetSortAxis(EDreamPixelSortAxis Value)
{
	if (SortAxis != Value) { SortAxis = Value; SendOthersDataToRenderProxy(); }
}
void UDreamPixelSort::SetSortKey(EDreamPixelSortKey Value)
{
	if (SortKey != Value) { SortKey = Value; SendOthersDataToRenderProxy(); }
}
void UDreamPixelSort::SetIntervalMode(EDreamPixelSortInterval Value)
{
	if (IntervalMode != Value) { IntervalMode = Value; SendOthersDataToRenderProxy(); }
}
void UDreamPixelSort::SetIntervalLength(int32 Value)
{
	if (IntervalLength != Value) { IntervalLength = Value; SendOthersDataToRenderProxy(); }
}
void UDreamPixelSort::SetRandomness(float Value)
{
	if (Randomness != Value) { Randomness = Value; SendOthersDataToRenderProxy(); }
}
void UDreamPixelSort::SetSortStrength(float Value)
{
	if (SortStrength != Value) { SortStrength = Value; SendOthersDataToRenderProxy(); }
}
void UDreamPixelSort::SetMaxSortPasses(int32 Value)
{
	if (MaxSortPasses != Value) { MaxSortPasses = Value; SendOthersDataToRenderProxy(); }
}
void UDreamPixelSort::SetThresholdMin(float Value)
{
	if (ThresholdMin != Value) { ThresholdMin = Value; SendOthersDataToRenderProxy(); }
}
void UDreamPixelSort::SetThresholdMax(float Value)
{
	if (ThresholdMax != Value) { ThresholdMax = Value; SendOthersDataToRenderProxy(); }
}
void UDreamPixelSort::SetDescending(bool Value)
{
	if (bDescending != Value) { bDescending = Value; SendOthersDataToRenderProxy(); }
}

void UDreamPixelSort::SendOthersDataToRenderProxy()
{
	if (RenderProxy == nullptr)return;
	auto TempRenderProxy = (FDreamPixelSortRenderProxy*)RenderProxy;
	// Resolved on the game thread and shipped by value, so the render thread never reads a UPROPERTY.
	const int32 PassCount = DreamPixelSort::ResolvePassCount(SortStrength, MaxSortPasses);
	const FVector2f ResolvedBand = DreamPixelSort::ResolveBand(ThresholdMin, ThresholdMax);
	const EDreamPixelSortAxis Axis = SortAxis;
	const EDreamPixelSortKey Key = SortKey;
	const EDreamPixelSortInterval Interval = IntervalMode;
	const int32 Length = FMath::Max(IntervalLength, 2);
	const float RandomAmount = FMath::Clamp(Randomness, 0.0f, 1.0f);
	const bool bDesc = bDescending;
	ENQUEUE_RENDER_COMMAND(FDreamPixelSort_UpdateData)
		([TempRenderProxy, PassCount, ResolvedBand, Axis, Key, Interval, Length, RandomAmount, bDesc](FRHICommandListImmediate& RHICmdList)
			{
				TempRenderProxy->SearchRadius = PassCount;
				TempRenderProxy->Band = ResolvedBand;
				TempRenderProxy->SortAxis = Axis;
				TempRenderProxy->SortKey = Key;
				TempRenderProxy->IntervalMode = Interval;
				TempRenderProxy->IntervalLength = Length;
				TempRenderProxy->Randomness = RandomAmount;
				TempRenderProxy->bDescending = bDesc;
			});
}

FDreamVisualPostProcessRenderProxy* UDreamPixelSort::GetRenderProxy()
{
	if (RenderProxy == nullptr)
	{
		RenderProxy = new FDreamPixelSortRenderProxy();
		SendRegionVertexDataToRenderProxy();
		SendMaskTextureToRenderProxy();
		SendRenderTargetToRenderProxy();
		SendOthersDataToRenderProxy();
	}
	return RenderProxy;
}

void UDreamPixelSort::MarkAllDirty()
{
	Super::MarkAllDirty();
	SendOthersDataToRenderProxy();
}

void UDreamPixelSort::SendRegionVertexDataToRenderProxy()
{
	Super::SendRegionVertexDataToRenderProxy();
	SendOthersDataToRenderProxy();
}

DECLARE_DREAM_GUI_VISUAL("PixelSort", UDreamPixelSort)
