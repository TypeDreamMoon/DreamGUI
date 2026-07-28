// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/LexPixelSort.h"

#include "Core/Components/LexPixelSortRenderProxy.h"
#include "Core/LexUIRender/LexUIPostProcessShaders.h"
#include "Core/LexUIRender/LexUIRenderer.h"
#include "LGUI.h"
#include "PipelineStateCache.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"
#include "ScreenRendering.h"

DECLARE_CYCLE_STAT(TEXT("PostProcess PixelSort"), STAT_PixelSort, STATGROUP_LGUI);

BEGIN_SHADER_PARAMETER_STRUCT(FLexUIPixelSortPassParameters, )
	RDG_TEXTURE_ACCESS(SourceTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

//------------------------------------------------------------------------------------------------
// The arithmetic. Mirrored by LexUIPostProcessPixelSort.usf -- keep the two in step.
//------------------------------------------------------------------------------------------------
namespace LexPixelSort
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

	float ComputeKey(const FLinearColor& InColor, ELexPixelSortKey InKey)
	{
		switch (InKey)
		{
		case ELexPixelSortKey::Brightness:
			return FMath::Max3(InColor.R, InColor.G, InColor.B);
		case ELexPixelSortKey::Saturation:
		{
			const float MaxChannel = FMath::Max3(InColor.R, InColor.G, InColor.B);
			const float MinChannel = FMath::Min3(InColor.R, InColor.G, InColor.B);
			return MaxChannel > UE_SMALL_NUMBER ? (MaxChannel - MinChannel) / MaxChannel : 0.0f;
		}
		case ELexPixelSortKey::Alpha:
			return InColor.A;
		case ELexPixelSortKey::Luminance:
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

	bool ShouldExchange(float InLowerKey, float InUpperKey, bool bInDescending)
	{
		// ONE expression, evaluated identically by both members of a pair. Writing the mirrored form
		// on the other side is the natural thing to do and it duplicates a texel on every tie --
		// and flat UI backgrounds are nothing but ties.
		return bInDescending ? (InLowerKey < InUpperKey) : (InLowerKey > InUpperKey);
	}

	int32 GatherIndex(const TArray<float>& InKeys, int32 InIndex, int32 InPhase, const FVector2f& InBand, bool bInDescending)
	{
		const int32 Count = InKeys.Num();
		const bool bIsLower = ((InIndex + (InPhase & 1)) % 2) == 0;
		const int32 PartnerIndex = bIsLower ? InIndex + 1 : InIndex - 1;
		if (PartnerIndex < 0 || PartnerIndex >= Count)
		{
			return InIndex;//the ends of the line have no partner
		}
		const float SelfKey = InKeys[InIndex];
		const float PartnerKey = InKeys[PartnerIndex];
		if (!IsInBand(SelfKey, InBand) || !IsInBand(PartnerKey, InBand))
		{
			return InIndex;
		}
		// The same question about the same ORDERED pair, whichever side is asking. This is what makes
		// two independent invocations agree.
		const float LowerKey = bIsLower ? SelfKey : PartnerKey;
		const float UpperKey = bIsLower ? PartnerKey : SelfKey;
		return ShouldExchange(LowerKey, UpperKey, bInDescending) ? PartnerIndex : InIndex;
	}

	void ApplyPhase(TArray<float>& InOutKeys, int32 InPhase, const FVector2f& InBand, bool bInDescending)
	{
		const int32 Count = InOutKeys.Num();
		const int32 Parity = InPhase & 1;
		for (int32 Index = Parity; Index + 1 < Count; Index += 2)
		{
			// Both members must be in band. Dropping the partner's test still looks like a pixel
			// sort, but runs bleed through their own boundaries and the threshold stops meaning
			// anything at all.
			if (!IsInBand(InOutKeys[Index], InBand) || !IsInBand(InOutKeys[Index + 1], InBand))
			{
				continue;
			}
			if (ShouldExchange(InOutKeys[Index], InOutKeys[Index + 1], bInDescending))
			{
				Swap(InOutKeys[Index], InOutKeys[Index + 1]);
			}
		}
	}
}

//------------------------------------------------------------------------------------------------
// Render thread
//------------------------------------------------------------------------------------------------
void FLexPixelSortRenderProxy::OnRenderPostProcess_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	FLexUIRenderer* Renderer,
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
	// Held for the whole function and released only at the end, as blur does. The pool keeps the RHI
	// texture alive past a SafeRelease, but with two targets and many passes in flight there is no
	// reason to lean on that.
	auto ReleaseRenderTargets = [&] {
		if (ScreenResolvedTexture.IsValid())ScreenResolvedTexture.SafeRelease();
		if (SortTargetA.IsValid())SortTargetA.SafeRelease();
		if (SortTargetB.IsValid())SortTargetB.SafeRelease();
	};

	const uint8 NumSamples = ScreenTargetTexture->GetNumSamples();
	const auto ScreenSize = ScreenTargetTexture->GetSizeXY();
	if (NumSamples > 1)
	{
		// A multisampled screen texture cannot be sampled directly; resolve first, exactly as blur
		// and pixelate do.
		FPooledRenderTargetDesc ResolveDesc(FPooledRenderTargetDesc::Create2DDesc(ScreenSize, ScreenTargetTexture->GetFormat(),
			FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, ResolveDesc, ScreenResolvedTexture, TEXT("LexUIPixelSortResolveTarget"));
		if (!ScreenResolvedTexture.IsValid())
		{
			ReleaseRenderTargets();
			return;
		}
		Renderer->CopyRenderTarget(GraphBuilder, GlobalShaderMap, ScreenTargetTexture.GetReference(), ScreenResolvedTexture->GetRHI());
	}

	const int32 RegionWidth = FMath::Max((int32)RectSize.X, 1);
	const int32 RegionHeight = FMath::Max((int32)RectSize.Y, 1);
	const FIntPoint RegionSize(RegionWidth, RegionHeight);
	// NOTE: deliberately no bUseFullSize in-place shortcut. Blur can write straight into the
	// backbuffer because its own passes ping-pong internally; a sort pass reading and writing the
	// same texture produces per-tile garbage that varies by GPU and looks almost right locally.
	const bool bFullScreen = RegionSize == ScreenSize;

	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(RegionSize, ScreenTargetTexture->GetFormat(),
			FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SortTargetA, TEXT("LexUIPixelSortTargetA"));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SortTargetB, TEXT("LexUIPixelSortTargetB"));
		if (!SortTargetA.IsValid() || !SortTargetB.IsValid())
		{
			ReleaseRenderTargets();
			return;
		}
	}
	auto SortTextureA = SortTargetA->GetRHI();
	auto SortTextureB = SortTargetB->GetRHI();

	// Grab the widget's region out of the screen.
	const auto ModelViewProjectionMatrix = ObjectToWorldMatrix * ViewProjectionMatrix;
	auto SourceScreenTexture = NumSamples > 1 ? ScreenResolvedTexture->GetRHI() : ScreenTargetTexture.GetReference();
	if (!bFullScreen)
	{
		Renderer->CopyRenderTargetOnMeshRegion(GraphBuilder
			, RegisterExternalTexture(GraphBuilder, SortTextureA, TEXT("LexUIPixelSortRegionGrab"))
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

	// Registered ONCE per pooled target, outside the loop. Registering the same RHI texture twice
	// gives RDG two handles onto one resource, so it cannot see the dependency between passes and
	// they race -- which shows up as per-frame flicker that disappears under r.RDG.ImmediateMode.
	FRDGTextureRef TextureA = RegisterExternalTexture(GraphBuilder, SortTextureA, TEXT("LexUIPixelSortA"));
	FRDGTextureRef TextureB = RegisterExternalTexture(GraphBuilder, SortTextureB, TEXT("LexUIPixelSortB"));

	FRDGTextureRef ReadTexture = TextureA;
	FRDGTextureRef WriteTexture = TextureB;

	TShaderMapRef<FLexUISimplePostProcessVS> VertexShader(GlobalShaderMap);
	TShaderMapRef<FLexUIPostProcessPixelSortPS> PixelShader(GlobalShaderMap);

	const FVector2f RegionSizeFloat((float)RegionSize.X, (float)RegionSize.Y);
	const float AxisFlag = SortAxis == ELexPixelSortAxis::Horizontal ? 0.0f : 1.0f;
	const float KeyFlag = (float)(uint8)SortKey;
	const float DescendingFlag = bDescending ? 1.0f : 0.0f;

	for (int32 Phase = 0; Phase < SortPassCount; ++Phase)
	{
		// Fresh parameters per pass: the struct is owned by the graph and outlives this iteration.
		auto* PassParameters = GraphBuilder.AllocParameters<FLexUIPixelSortPassParameters>();
		PassParameters->SourceTexture = ReadTexture;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(WriteTexture, ERenderTargetLoadAction::ENoAction);

		// Captured BY VALUE. The lambda runs after this function returns, so reading members off
		// `this` inside it would race the game thread's parameter pushes.
		const int32 PhaseParity = Phase & 1;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("LexUIPixelSort_Phase_%d", Phase),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, VertexShader, PixelShader, Renderer, ReadTexture, WriteTexture, RegionSizeFloat,
			PhaseParity, Band = this->Band, AxisFlag, KeyFlag, DescendingFlag](FRHICommandListImmediate& RHICmdList)
			{
				ReadTexture->MarkResourceAsUsed();
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, ECompareFunction::CF_Always>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetLexUIPostProcessVertexDeclaration();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = EPrimitiveType::PT_TriangleList;
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0, EApplyRendertargetOption::CheckApply);
				VertexShader->SetParameters(RHICmdList);
				RHICmdList.SetViewport(0, 0, 0.0f, WriteTexture->Desc.Extent.X, WriteTexture->Desc.Extent.Y, 1.0f);

				// POINT sampling, and this is not a preference. With a bilinear sampler the two
				// halves of a pair read slightly different values, disagree about the exchange, and
				// one texel is duplicated while its partner is erased -- every pass, compounding.
				// The result still looks plausibly sorted, just progressively muddier.
				PixelShader->SetParameters(RHICmdList, ReadTexture->GetRHI(),
					TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
					RegionSizeFloat, (float)PhaseParity, Band, AxisFlag, KeyFlag, DescendingFlag);

				Renderer->DrawFullScreenQuad(RHICmdList);
			});

		Swap(ReadTexture, WriteTexture);
	}

	// The result is in whichever texture the last swap left as the READ target. Getting this wrong
	// shows either the raw grab, which reads as "the effect is off", or the second-to-last phase,
	// which reads as working and is subtly wrong.
	auto ResultTexture = (ReadTexture == TextureA) ? SortTextureA : SortTextureB;

	const auto PointSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
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
ULexPixelSort::ULexPixelSort(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
}

void ULexPixelSort::BeginPlay()
{
	Super::BeginPlay();
	SendOthersDataToRenderProxy();
}

#if WITH_EDITOR
void ULexPixelSort::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// The details panel writes property memory and then calls this; it does NOT call the setters.
	// Without this the panel shows the new value and the GPU keeps the old one.
	SendOthersDataToRenderProxy();
}
#endif

void ULexPixelSort::SetSortAxis(ELexPixelSortAxis Value)
{
	if (SortAxis != Value) { SortAxis = Value; SendOthersDataToRenderProxy(); }
}
void ULexPixelSort::SetSortKey(ELexPixelSortKey Value)
{
	if (SortKey != Value) { SortKey = Value; SendOthersDataToRenderProxy(); }
}
void ULexPixelSort::SetSortStrength(float Value)
{
	if (SortStrength != Value) { SortStrength = Value; SendOthersDataToRenderProxy(); }
}
void ULexPixelSort::SetMaxSortPasses(int32 Value)
{
	if (MaxSortPasses != Value) { MaxSortPasses = Value; SendOthersDataToRenderProxy(); }
}
void ULexPixelSort::SetThresholdMin(float Value)
{
	if (ThresholdMin != Value) { ThresholdMin = Value; SendOthersDataToRenderProxy(); }
}
void ULexPixelSort::SetThresholdMax(float Value)
{
	if (ThresholdMax != Value) { ThresholdMax = Value; SendOthersDataToRenderProxy(); }
}
void ULexPixelSort::SetDescending(bool Value)
{
	if (bDescending != Value) { bDescending = Value; SendOthersDataToRenderProxy(); }
}

void ULexPixelSort::SendOthersDataToRenderProxy()
{
	if (RenderProxy == nullptr)return;
	auto TempRenderProxy = (FLexPixelSortRenderProxy*)RenderProxy;
	// Resolved on the game thread and shipped by value, so the render thread never reads a UPROPERTY.
	const int32 PassCount = LexPixelSort::ResolvePassCount(SortStrength, MaxSortPasses);
	const FVector2f ResolvedBand = LexPixelSort::ResolveBand(ThresholdMin, ThresholdMax);
	const ELexPixelSortAxis Axis = SortAxis;
	const ELexPixelSortKey Key = SortKey;
	const bool bDesc = bDescending;
	ENQUEUE_RENDER_COMMAND(FLexPixelSort_UpdateData)
		([TempRenderProxy, PassCount, ResolvedBand, Axis, Key, bDesc](FRHICommandListImmediate& RHICmdList)
			{
				TempRenderProxy->SortPassCount = PassCount;
				TempRenderProxy->Band = ResolvedBand;
				TempRenderProxy->SortAxis = Axis;
				TempRenderProxy->SortKey = Key;
				TempRenderProxy->bDescending = bDesc;
			});
}

FLexVisualPostProcessRenderProxy* ULexPixelSort::GetRenderProxy()
{
	if (RenderProxy == nullptr)
	{
		RenderProxy = new FLexPixelSortRenderProxy();
		SendRegionVertexDataToRenderProxy();
		SendMaskTextureToRenderProxy();
		SendRenderTargetToRenderProxy();
		SendOthersDataToRenderProxy();
	}
	return RenderProxy;
}

void ULexPixelSort::MarkAllDirty()
{
	Super::MarkAllDirty();
	SendOthersDataToRenderProxy();
}

void ULexPixelSort::SendRegionVertexDataToRenderProxy()
{
	Super::SendRegionVertexDataToRenderProxy();
	SendOthersDataToRenderProxy();
}
