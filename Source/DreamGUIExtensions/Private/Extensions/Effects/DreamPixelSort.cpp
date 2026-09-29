// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/Effects/DreamPixelSort.h"
#include "DreamUIRender/DreamUIPostProcessEffects.h"

#include "DreamGUI.h"
#include "Core/DreamUIWidgetRegistry.h"



// The gather reads TWO textures, and both accesses must be declared or RDG inserts no barrier
// between the rank pass writing the destinations and this pass reading them.

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
	if (!RenderProxy.IsValid())return;
	// Resolved on the game thread and shipped by value, so the render thread never reads a UPROPERTY.
	const int32 PassCount = DreamPixelSort::ResolvePassCount(SortStrength, MaxSortPasses);
	const FVector2f ResolvedBand = DreamPixelSort::ResolveBand(ThresholdMin, ThresholdMax);
	DreamUIPostProcessEffects::FPixelSortParams Params;
	Params.SearchRadius = PassCount;
	Params.Band = ResolvedBand;
	// The renderer's enums mirror these value for value.
	Params.SortAxis = static_cast<DreamUIPostProcessEffects::EPixelSortAxis>(SortAxis);
	Params.SortKey = static_cast<DreamUIPostProcessEffects::EPixelSortKey>(SortKey);
	Params.IntervalMode = static_cast<DreamUIPostProcessEffects::EPixelSortInterval>(IntervalMode);
	Params.IntervalLength = FMath::Max(IntervalLength, 2);
	Params.Randomness = FMath::Clamp(Randomness, 0.0f, 1.0f);
	Params.bDescending = bDescending;
	DreamUIPostProcessEffects::SetPixelSort_GameThread(RenderProxy, Params);
}

FDreamVisualPostProcessRenderProxyPtr UDreamPixelSort::GetRenderProxy()
{
	if (!RenderProxy.IsValid())
	{
		RenderProxy = DreamUIPostProcessEffects::CreatePixelSortProxy();
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
