// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUITimelineSampling.h"

#include "DreamTweener.h"

namespace DreamUITimelineSamplingLocal
{
	/** The ease of that name, or false. The word list is EDreamTweenEase's, read through reflection. */
	bool FindEase(const FString& InName, EDreamTweenEase& OutEase)
	{
		const UEnum* EaseEnum = StaticEnum<EDreamTweenEase>();
		const int64 Value = EaseEnum != nullptr ? EaseEnum->GetValueByNameString(InName) : INDEX_NONE;
		if (Value == INDEX_NONE || static_cast<EDreamTweenEase>(Value) == EDreamTweenEase::CurveFloat)
		{
			// CurveFloat is excluded on purpose rather than missing: it names a curve ASSET, and a timeline key has
			// nowhere to put one. An author who needs a hand-drawn curve marks the timeline `external` and draws it in
			// Sequencer, which is the layer that exists for it.
			return false;
		}
		OutEase = static_cast<EDreamTweenEase>(Value);
		return true;
	}
}

FFrameNumber DreamUITimelineSampling::TimeToFrame(const double InSeconds, const double InTickRate)
{
	return FFrameNumber(static_cast<int32>(FMath::RoundToDouble(InSeconds * InTickRate)));
}

bool DreamUITimelineSampling::Sample(TConstArrayView<FLineKey> InKeys, const int32 InChannelCount, const double InTickRate,
	TArray<FSample>& OutSamples, FString& OutBadEaseName)
{
	const int32 ChannelCount = FMath::Clamp(InChannelCount, 0, 4);
	const double SecondsPerSample = 1.0 / static_cast<double>(DisplayRate);
	for (int32 KeyIndex = 0; KeyIndex < InKeys.Num(); ++KeyIndex)
	{
		const FLineKey& Key = InKeys[KeyIndex];
		FSample& Start = OutSamples.AddDefaulted_GetRef();
		Start.Frame = TimeToFrame(Key.Time, InTickRate);
		Start.LineKey = KeyIndex;
		Start.bIsLineKey = true;
		for (int32 Channel = 0; Channel < ChannelCount; ++Channel)
		{
			Start.Channels[Channel] = Key.Channels[Channel];
		}

		if (KeyIndex + 1 >= InKeys.Num())
		{
			break;
		}
		const FLineKey& NextKey = InKeys[KeyIndex + 1];
		const double Span = NextKey.Time - Key.Time;
		if (Span <= 0.0 || Key.EaseName.IsEmpty() || Key.EaseName.Equals(TEXT("Linear")))
		{
			// Linear needs no samples between: two keys and a straight line is exactly the curve.
			continue;
		}

		EDreamTweenEase Ease = EDreamTweenEase::Linear;
		if (!DreamUITimelineSamplingLocal::FindEase(Key.EaseName, Ease))
		{
			OutBadEaseName = Key.EaseName;
			return false;
		}
		const FDreamTweenFunction Curve = UDreamTweener::GetEaseFunction(Ease);
		if (!Curve.IsBound())
		{
			OutBadEaseName = Key.EaseName;
			return false;
		}

		const int32 SampleCount = FMath::Clamp(FMath::CeilToInt(Span / SecondsPerSample) - 1, 0, 600);
		for (int32 Index = 1; Index <= SampleCount; ++Index)
		{
			const double Alpha = static_cast<double>(Index) / static_cast<double>(SampleCount + 1);
			// c = 1, b = 0, d = 1: the curve library's own normalised 0..1 shape, so `ease OutBounce` here and a
			// DreamTween OutBounce are the same motion by construction.
			const double Eased = static_cast<double>(Curve.Execute(1.0f, 0.0f, static_cast<float>(Alpha), 1.0f));
			FSample& Between = OutSamples.AddDefaulted_GetRef();
			Between.Frame = TimeToFrame(Key.Time + Span * Alpha, InTickRate);
			Between.LineKey = KeyIndex;
			for (int32 Channel = 0; Channel < ChannelCount; ++Channel)
			{
				const double From = Key.Channels[Channel];
				const double To = NextKey.Channels[Channel];
				Between.Channels[Channel] = From + (To - From) * Eased;
			}
		}
	}
	return true;
}
