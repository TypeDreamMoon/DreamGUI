// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUITimelineWriteBack.h"

#include "DreamWidgetBlueprint.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextWriteBack.h"
#include "Text/DreamUIValueFormat.h"

#include "Animation/DreamUIAnimEventTrack.h"
#include "Animation/DreamWidgetAnimation.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "DreamTweener.h"

#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneStringChannel.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneTimeHelpers.h"
#include "Sections/MovieSceneColorSection.h"
#include "Sections/MovieSceneDoubleSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneVectorSection.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieSceneDoubleTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneVectorTrack.h"

#define LOCTEXT_NAMESPACE "DreamUITimelineWriteBack"

namespace DreamUITimelineWriteBackLocal
{
	/** The display rate the builder samples an ease at (DreamUITextBuilder, TimelineDisplayRate). */
	constexpr double SampleRate = 60.0;

	enum class EValueFormat : uint8 { Number, Tuple, Hex };

	struct FReadKey
	{
		double Time = 0.0;
		double Channels[4] = { 0.0, 0.0, 0.0, 0.0 };
		FString Ease;
	};

	struct FReadTrack
	{
		FString NodePath;
		FString PropertyName;
		bool bIsEvent = false;
		int32 ChannelCount = 1;
		bool bSinglePrecision = false;
		EValueFormat Format = EValueFormat::Number;
		TArray<FReadKey> Keys;
		TArray<TPair<double, FString>> Events;
		/** The line of the block this track is, when it has one. */
		const FDreamUITimelineTrack* Existing = nullptr;
	};

	// ---------------------------------------------------------------------------------------------
	// Reading the sequence
	// ---------------------------------------------------------------------------------------------

	/**
	 * One channel's keys as seconds and values. A key with automatic tangents becomes linear -- the language has no word
	 * for them, and the sequence is set to what the file will build. One with tangents of its own, or stepped, cannot be
	 * said, unless it is the last (whose curve goes nowhere).
	 */
	template<typename ChannelType>
	bool ReadChannel(ChannelType& InChannel, double InTickRate, UMovieSceneSection* InSection, TArray<double>& OutTimes,
		TArray<double>& OutValues, FString& OutRefusal)
	{
		auto Data = InChannel.GetData();
		const TArrayView<const FFrameNumber> Times = Data.GetTimes();
		auto Values = Data.GetValues();
		for (int32 Index = 0; Index < Times.Num(); ++Index)
		{
			auto& Value = Values[Index];
			const bool bLast = Index + 1 == Times.Num();
			if (Value.InterpMode == RCIM_Cubic && (Value.TangentMode == RCTM_Auto || Value.TangentMode == RCTM_SmartAuto))
			{
				InSection->Modify();
				Value.InterpMode = RCIM_Linear;
			}
			else if (Value.InterpMode != RCIM_Linear && !bLast)
			{
				OutRefusal = Value.InterpMode == RCIM_Constant
					? TEXT("a stepped key has no spelling in a timeline")
					: TEXT("a key with tangents of its own has no spelling in a timeline (keys are linear, or run along a named ease)");
				return false;
			}
			OutTimes.Add(static_cast<double>(Times[Index].Value) / InTickRate);
			OutValues.Add(static_cast<double>(Value.Value));
		}
		return true;
	}

	/**
	 * The author's keys back out of a run of sampled ones: from each anchor, the next anchor whose keys in between are
	 * exactly the samples the builder makes of one ease -- the same count, frames and values -- or else the very next
	 * key, linear.
	 */
	void RecoverKeys(const TArray<double>& InTimes, const TArray<TArray<double>>& InChannels, int32 InChannelCount, double InTickRate,
		TArray<FReadKey>& OutKeys)
	{
		const UEnum* EaseEnum = StaticEnum<EDreamTweenEase>();
		auto MakeKey = [&](int32 InIndex)
		{
			FReadKey Key;
			Key.Time = InTimes[InIndex];
			for (int32 Channel = 0; Channel < InChannelCount; ++Channel)
			{
				Key.Channels[Channel] = InChannels[Channel][InIndex];
			}
			return Key;
		};
		auto ToFrame = [InTickRate](double InSeconds) { return FMath::RoundToInt64(InSeconds * InTickRate); };
		auto Near = [](double A, double B) { return FMath::Abs(A - B) <= 1.0e-4 * FMath::Max(1.0, FMath::Abs(B)); };

		const int32 Num = InTimes.Num();
		if (Num == 0)
		{
			return;
		}
		int32 Anchor = 0;
		OutKeys.Add(MakeKey(0));
		while (Anchor < Num - 1)
		{
			int32 Next = INDEX_NONE;
			FString NextEase;
			for (int32 Candidate = Anchor + 2; Candidate < Num && Candidate <= Anchor + 602 && Next == INDEX_NONE; ++Candidate)
			{
				const double Span = InTimes[Candidate] - InTimes[Anchor];
				const int32 SampleCount = FMath::Clamp(FMath::CeilToInt(Span * SampleRate) - 1, 0, 600);
				if (SampleCount != Candidate - Anchor - 1)
				{
					continue;
				}
				for (int32 EaseIndex = 0; EaseEnum != nullptr && EaseIndex < EaseEnum->NumEnums() - 1 && Next == INDEX_NONE; ++EaseIndex)
				{
					const EDreamTweenEase Ease = static_cast<EDreamTweenEase>(EaseEnum->GetValueByIndex(EaseIndex));
					if (Ease == EDreamTweenEase::Linear || Ease == EDreamTweenEase::CurveFloat)
					{
						continue;
					}
					const FDreamTweenFunction Curve = UDreamTweener::GetEaseFunction(Ease);
					if (!Curve.IsBound())
					{
						continue;
					}
					bool bMatches = true;
					for (int32 Sample = 1; Sample <= SampleCount && bMatches; ++Sample)
					{
						const double Alpha = static_cast<double>(Sample) / static_cast<double>(SampleCount + 1);
						bMatches = ToFrame(InTimes[Anchor] + Span * Alpha) == ToFrame(InTimes[Anchor + Sample]);
						const double Eased = static_cast<double>(Curve.Execute(1.0f, 0.0f, static_cast<float>(Alpha), 1.0f));
						for (int32 Channel = 0; Channel < InChannelCount && bMatches; ++Channel)
						{
							const double From = InChannels[Channel][Anchor];
							const double To = InChannels[Channel][Candidate];
							bMatches = Near(InChannels[Channel][Anchor + Sample], From + (To - From) * Eased);
						}
					}
					if (bMatches)
					{
						Next = Candidate;
						NextEase = EaseEnum->GetNameStringByIndex(EaseIndex);
					}
				}
			}
			if (Next == INDEX_NONE)
			{
				Next = Anchor + 1;
			}
			OutKeys.Last().Ease = NextEase;
			OutKeys.Add(MakeKey(Next));
			Anchor = Next;
		}
	}

	/** The widget a possessable's name names under the host: `Icon`, or `Icon.DreamImage` for its visual or a behaviour. */
	const UDreamWidget* FindWidgetUnder(const UDreamWidget* InHost, const FString& InDisplayName, FString& OutPath)
	{
		TArray<UDreamWidget*> Widgets;
		UDreamWidget::CollectChildrenWidgets(const_cast<UDreamWidget*>(InHost), Widgets, /*IncludeTarget*/true);
		for (const UDreamWidget* Widget : Widgets)
		{
			if (Widget != nullptr && Widget->GetDisplayName().Equals(InDisplayName, ESearchCase::IgnoreCase))
			{
				TArray<FString> Segments;
				for (const UDreamWidget* Step = Widget; Step != nullptr && Step != InHost; Step = Step->GetParent())
				{
					Segments.Insert(Step->GetDisplayName(), 0);
				}
				OutPath = FString::Join(Segments, TEXT("/"));
				return Widget;
			}
		}
		return nullptr;
	}

	/** The last segment of a node path, or the host's own name for the empty path: what the builder names the binding. */
	FString BindingWidgetOf(const FString& InNodePath, const UDreamWidget* InHost)
	{
		if (InNodePath.IsEmpty())
		{
			return InHost != nullptr ? InHost->GetDisplayName() : FString();
		}
		FString Last;
		return InNodePath.Split(TEXT("/"), nullptr, &Last, ESearchCase::IgnoreCase, ESearchDir::FromEnd) ? Last : InNodePath;
	}

	/**
	 * Whether a line's property names a track's: the same path, or the label the animation editor puts on the row for its
	 * head -- a line may write `Width` for the field `AnimatableWidth` (DisplayName "Width"), as the builder reads it, and
	 * the track keeps the field.
	 */
	bool LineNamesTrack(const FString& InLineProperty, const FString& InTrackPath, const UClass* InClass)
	{
		if (InLineProperty.Equals(InTrackPath, ESearchCase::IgnoreCase))
		{
			return true;
		}
		FString LineHead = InLineProperty;
		FString LineRest;
		InLineProperty.Split(TEXT("."), &LineHead, &LineRest);
		FString TrackHead = InTrackPath;
		FString TrackRest;
		InTrackPath.Split(TEXT("."), &TrackHead, &TrackRest);
		const FProperty* Head = InClass != nullptr ? InClass->FindPropertyByName(FName(*TrackHead)) : nullptr;
		return Head != nullptr && LineRest.Equals(TrackRest, ESearchCase::IgnoreCase)
			&& (Head->GetMetaData(TEXT("DisplayName")).Equals(LineHead, ESearchCase::IgnoreCase)
				|| Head->GetDisplayNameText().ToString().Equals(LineHead, ESearchCase::IgnoreCase));
	}

	bool ReadTracks(UDreamWidgetAnimation* InAnimation, const UDreamWidget* InHost, const FDreamUITimeline& InExisting,
		TArray<FReadTrack>& OutTracks, double& OutDuration, FString& OutRefusal)
	{
		UMovieScene* MovieScene = InAnimation->GetMovieScene();
		if (MovieScene == nullptr)
		{
			OutRefusal = TEXT("the animation has no sequence");
			return false;
		}
		const double TickRate = MovieScene->GetTickResolution().AsDecimal();
		const TRange<FFrameNumber> Playback = MovieScene->GetPlaybackRange();
		OutDuration = Playback.HasUpperBound()
			? static_cast<double>(UE::MovieScene::DiscreteExclusiveUpper(Playback).Value - 1) / TickRate : 0.0;

		for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		{
			const FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Binding.GetObjectGuid());
			if (Possessable == nullptr)
			{
				OutRefusal = TEXT("a track is bound to a spawned object, which a timeline cannot name");
				return false;
			}
			FString WidgetName = Possessable->GetName();
			WidgetName.Split(TEXT("."), &WidgetName, nullptr);
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track);
				if (PropertyTrack == nullptr)
				{
					OutRefusal = FString::Printf(TEXT("'%s' is not a property track, and a timeline drives properties"), *Track->GetDisplayName().ToString());
					return false;
				}
				const TArray<UMovieSceneSection*>& Sections = PropertyTrack->GetAllSections();
				if (Sections.Num() != 1)
				{
					OutRefusal = FString::Printf(TEXT("the track of '%s' has %d sections, and a timeline line is one"), *PropertyTrack->GetPropertyPath().ToString(), Sections.Num());
					return false;
				}
				UMovieSceneSection* Section = Sections[0];

				FReadTrack& Read = OutTracks.AddDefaulted_GetRef();
				Read.PropertyName = PropertyTrack->GetPropertyPath().ToString();
				const UClass* PossessedClass = Possessable->GetPossessedObjectClass();
				Read.Existing = InExisting.Tracks.FindByPredicate([&](const FDreamUITimelineTrack& InLine)
				{
					return !InLine.bIsEvent && LineNamesTrack(InLine.PropertyName, Read.PropertyName, PossessedClass)
						&& BindingWidgetOf(InLine.NodePath, InHost).Equals(WidgetName, ESearchCase::IgnoreCase);
				});
				if (Read.Existing != nullptr)
				{
					// The line's own spelling of the property: the label on the row (`Width`) when that is what it wrote.
					Read.NodePath = Read.Existing->NodePath;
					Read.PropertyName = Read.Existing->PropertyName;
				}
				else if (FindWidgetUnder(InHost, WidgetName, Read.NodePath) == nullptr)
				{
					OutRefusal = FString::Printf(TEXT("'%s' is no widget under the animation's host"), *WidgetName);
					return false;
				}

				TArray<double> Times;
				TArray<TArray<double>> Channels;
				auto ReadOne = [&](auto& InChannel)
				{
					TArray<double> ChannelTimes;
					TArray<double>& Values = Channels.AddDefaulted_GetRef();
					if (!ReadChannel(InChannel, TickRate, Section, ChannelTimes, Values, OutRefusal))
					{
						return false;
					}
					if (Channels.Num() == 1)
					{
						Times = ChannelTimes;
						return true;
					}
					if (ChannelTimes.Num() != Times.Num())
					{
						OutRefusal = FString::Printf(TEXT("the channels of '%s' are keyed apart, and a timeline key sets all of them at once"), *Read.PropertyName);
						return false;
					}
					for (int32 Index = 0; Index < Times.Num(); ++Index)
					{
						if (FMath::RoundToInt64(ChannelTimes[Index] * TickRate) != FMath::RoundToInt64(Times[Index] * TickRate))
						{
							OutRefusal = FString::Printf(TEXT("the channels of '%s' are keyed apart, and a timeline key sets all of them at once"), *Read.PropertyName);
							return false;
						}
					}
					return true;
				};

				bool bRead = true;
				if (UMovieSceneColorSection* Color = Cast<UMovieSceneColorSection>(Section))
				{
					Read.ChannelCount = 4;
					Read.bSinglePrecision = true;
					Read.Format = EValueFormat::Hex;
					bRead = ReadOne(Color->GetRedChannel()) && ReadOne(Color->GetGreenChannel()) && ReadOne(Color->GetBlueChannel()) && ReadOne(Color->GetAlphaChannel());
				}
				else if (UMovieSceneFloatSection* Float = Cast<UMovieSceneFloatSection>(Section))
				{
					Read.bSinglePrecision = true;
					TArrayView<FMovieSceneFloatChannel*> FloatChannels = Float->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
					bRead = FloatChannels.Num() > 0 && ReadOne(*FloatChannels[0]);
				}
				else if (UMovieSceneDoubleSection* Double = Cast<UMovieSceneDoubleSection>(Section))
				{
					TArrayView<FMovieSceneDoubleChannel*> DoubleChannels = Double->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
					bRead = DoubleChannels.Num() > 0 && ReadOne(*DoubleChannels[0]);
				}
				else if (UMovieSceneDoubleVectorSection* Vector = Cast<UMovieSceneDoubleVectorSection>(Section))
				{
					Read.ChannelCount = Vector->GetChannelsUsed();
					Read.Format = EValueFormat::Tuple;
					TArrayView<FMovieSceneDoubleChannel*> DoubleChannels = Vector->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
					for (int32 Index = 0; Index < Read.ChannelCount && bRead; ++Index)
					{
						bRead = DoubleChannels.IsValidIndex(Index) && ReadOne(*DoubleChannels[Index]);
					}
				}
				else
				{
					OutRefusal = FString::Printf(TEXT("'%s' is a kind of track a timeline line cannot spell"), *Read.PropertyName);
					return false;
				}
				if (!bRead)
				{
					if (OutRefusal.IsEmpty())
					{
						OutRefusal = FString::Printf(TEXT("'%s' could not be read"), *Read.PropertyName);
					}
					return false;
				}
				// The spelling the line already used, when it has one: a colour written as a tuple stays a tuple.
				if (Read.Existing != nullptr && Read.Existing->Keys.Num() > 0)
				{
					switch (Read.Existing->Keys[0].Value.Kind)
					{
					case EDreamUIValueKind::HexColor: Read.Format = EValueFormat::Hex; break;
					case EDreamUIValueKind::Tuple: Read.Format = EValueFormat::Tuple; break;
					case EDreamUIValueKind::Number: Read.Format = EValueFormat::Number; break;
					default: break;
					}
				}
				RecoverKeys(Times, Channels, Read.ChannelCount, TickRate, Read.Keys);
			}
		}

		for (UMovieSceneTrack* Track : MovieScene->GetTracks())
		{
			UDreamUIAnimEventTrack* EventTrack = Cast<UDreamUIAnimEventTrack>(Track);
			if (EventTrack == nullptr)
			{
				OutRefusal = FString::Printf(TEXT("'%s' is a track a timeline cannot spell"), *Track->GetDisplayName().ToString());
				return false;
			}
			FReadTrack& Read = OutTracks.AddDefaulted_GetRef();
			Read.bIsEvent = true;
			Read.Existing = InExisting.Tracks.FindByPredicate([](const FDreamUITimelineTrack& InLine) { return InLine.bIsEvent; });
			for (UMovieSceneSection* Section : EventTrack->GetAllSections())
			{
				UDreamUIAnimEventSection* Events = Cast<UDreamUIAnimEventSection>(Section);
				if (Events == nullptr)
				{
					continue;
				}
				auto Data = Events->EventChannel.GetData();
				const TArrayView<const FFrameNumber> Times = Data.GetTimes();
				const auto Names = Data.GetValues();
				for (int32 Index = 0; Index < Times.Num(); ++Index)
				{
					Read.Events.Add({ static_cast<double>(Times[Index].Value) / TickRate, Names[Index] });
				}
			}
		}
		return true;
	}

	// ---------------------------------------------------------------------------------------------
	// Spelling it
	// ---------------------------------------------------------------------------------------------

	FString PrintTime(double InSeconds)
	{
		return DreamUIValueFormat::PrintScalar(InSeconds, /*bSinglePrecision*/false);
	}

	FString PrintValue(const FReadTrack& InTrack, const FReadKey& InKey)
	{
		switch (InTrack.Format)
		{
		case EValueFormat::Hex:
			return DreamUIValueFormat::PrintColorHex(FLinearColor(static_cast<float>(InKey.Channels[0]), static_cast<float>(InKey.Channels[1]),
				static_cast<float>(InKey.Channels[2]), static_cast<float>(InKey.Channels[3])));
		case EValueFormat::Tuple:
		{
			TArray<FString> Parts;
			for (int32 Channel = 0; Channel < InTrack.ChannelCount; ++Channel)
			{
				Parts.Add(DreamUIValueFormat::PrintScalar(InKey.Channels[Channel], InTrack.bSinglePrecision));
			}
			return FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(", ")));
		}
		default:
			return DreamUIValueFormat::PrintScalar(InKey.Channels[0], InTrack.bSinglePrecision);
		}
	}

	/** A key value the file spelled, as the channels the builder read from it -- false for a spelling this cannot read. */
	bool ParseChannels(const FDreamUIValue& InValue, int32 InChannelCount, double OutChannels[4])
	{
		switch (InValue.Kind)
		{
		case EDreamUIValueKind::Number:
			return LexTryParseString(OutChannels[0], *InValue.Raw);
		case EDreamUIValueKind::HexColor:
		{
			FLinearColor Color;
			if (!DreamUIValueFormat::ParseColorHex(InValue.Raw, Color))
			{
				return false;
			}
			OutChannels[0] = Color.R;
			OutChannels[1] = Color.G;
			OutChannels[2] = Color.B;
			OutChannels[3] = Color.A;
			return true;
		}
		case EDreamUIValueKind::Tuple:
		{
			FString Inner = InValue.Raw.TrimStartAndEnd();
			Inner.RemoveFromStart(TEXT("("));
			Inner.RemoveFromEnd(TEXT(")"));
			TArray<FString> Parts;
			Inner.ParseIntoArray(Parts, TEXT(","), /*InCullEmpty*/true);
			if (Parts.Num() < InChannelCount)
			{
				return false;
			}
			for (int32 Channel = 0; Channel < InChannelCount; ++Channel)
			{
				if (!LexTryParseString(OutChannels[Channel], *Parts[Channel].TrimStartAndEnd()))
				{
					return false;
				}
			}
			return true;
		}
		default:
			return false;
		}
	}

	bool SameValue(const FReadTrack& InTrack, const FReadKey& InKey, const FDreamUITimelineKey& InLine)
	{
		double Channels[4] = { 0.0, 0.0, 0.0, 0.0 };
		if (!ParseChannels(InLine.Value, InTrack.ChannelCount, Channels))
		{
			return false;
		}
		for (int32 Channel = 0; Channel < InTrack.ChannelCount; ++Channel)
		{
			// A colour's channels went through an sRGB byte each way: a step of 1/255 is the spelling's own grain.
			const double Tolerance = InTrack.Format == EValueFormat::Hex ? 0.5 / 255.0 + 1.0e-4 : 1.0e-4 * FMath::Max(1.0, FMath::Abs(Channels[Channel]));
			if (FMath::Abs(InKey.Channels[Channel] - Channels[Channel]) > Tolerance)
			{
				return false;
			}
		}
		return true;
	}

	bool SameEase(const FString& InA, const FString& InB)
	{
		auto Normal = [](const FString& InEase) { return InEase.IsEmpty() || InEase.Equals(TEXT("Linear"), ESearchCase::IgnoreCase) ? FString() : InEase; };
		return Normal(InA).Equals(Normal(InB), ESearchCase::IgnoreCase);
	}

	/** Whether a track says what its line says: the same keys, times, values and eases (the last key's ease goes nowhere). */
	bool Unchanged(const FReadTrack& InTrack, double InTickRate)
	{
		if (InTrack.Existing == nullptr || InTrack.Existing->Keys.Num() != InTrack.Keys.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < InTrack.Keys.Num(); ++Index)
		{
			const FReadKey& Key = InTrack.Keys[Index];
			const FDreamUITimelineKey& Line = InTrack.Existing->Keys[Index];
			if (FMath::Abs(Key.Time - Line.Time) > 0.5 / InTickRate || !SameValue(InTrack, Key, Line)
				|| (Index + 1 < InTrack.Keys.Num() && !SameEase(Key.Ease, Line.EaseName)))
			{
				return false;
			}
		}
		return true;
	}

	FString RenderTrack(const FReadTrack& InTrack)
	{
		TArray<FString> Keys;
		for (int32 Index = 0; Index < InTrack.Keys.Num(); ++Index)
		{
			const FReadKey& Key = InTrack.Keys[Index];
			FString Value = PrintValue(InTrack, Key);
			// The spelling the file already had for this very value, kept.
			if (InTrack.Existing != nullptr)
			{
				for (const FDreamUITimelineKey& Line : InTrack.Existing->Keys)
				{
					if (SameValue(InTrack, Key, Line))
					{
						Value = Line.Value.Kind == EDreamUIValueKind::HexColor ? TEXT("#") + Line.Value.Raw : Line.Value.Raw;
						break;
					}
				}
			}
			FString Spelled = FString::Printf(TEXT("%s = %s"), *PrintTime(Key.Time), *Value);
			if (Index + 1 < InTrack.Keys.Num() && !Key.Ease.IsEmpty())
			{
				Spelled += TEXT(" ease ") + Key.Ease;
			}
			Keys.Add(Spelled);
		}
		const FString Path = InTrack.NodePath.IsEmpty() ? InTrack.PropertyName : InTrack.NodePath + TEXT(".") + InTrack.PropertyName;
		return FString::Printf(TEXT("%s : %s"), *Path, *FString::Join(Keys, TEXT(", ")));
	}

	// ---------------------------------------------------------------------------------------------
	// The block, line by line
	// ---------------------------------------------------------------------------------------------

	struct FTextLine
	{
		FString Body;
		FString Ending;
	};

	TArray<FTextLine> SplitLines(const FString& InText)
	{
		TArray<FTextLine> Lines;
		int32 Start = 0;
		for (int32 Index = 0; Index < InText.Len(); ++Index)
		{
			if (InText[Index] == TEXT('\n'))
			{
				const bool bCr = Index > Start && InText[Index - 1] == TEXT('\r');
				Lines.Add({ InText.Mid(Start, Index - Start - (bCr ? 1 : 0)), bCr ? TEXT("\r\n") : TEXT("\n") });
				Start = Index + 1;
			}
		}
		Lines.Add({ InText.Mid(Start), FString() });
		return Lines;
	}

	FString LeadingWhitespace(const FString& InLine)
	{
		int32 Index = 0;
		while (Index < InLine.Len() && (InLine[Index] == TEXT(' ') || InLine[Index] == TEXT('\t')))
		{
			++Index;
		}
		return InLine.Left(Index);
	}

	/** The line a word starts, `duration` or `loop` -- followed by a space or '=' -- or nothing. */
	FString SettingOf(const FString& InLine)
	{
		const FString Trimmed = InLine.TrimStart();
		for (const TCHAR* Word : { TEXT("duration"), TEXT("loop") })
		{
			const int32 Length = FCString::Strlen(Word);
			if (Trimmed.StartsWith(Word, ESearchCase::CaseSensitive) && Trimmed.Len() > Length
				&& (Trimmed[Length] == TEXT(' ') || Trimmed[Length] == TEXT('\t') || Trimmed[Length] == TEXT('=')))
			{
				return Word;
			}
		}
		return FString();
	}

	bool Rewrite(const FString& InText, const FDreamUITimeline& InExisting, const TArray<FReadTrack>& InTracks, double InDuration,
		double InTickRate, FString& OutText, FString& OutError)
	{
		TArray<FTextLine> Lines = SplitLines(InText);
		const int32 Header = InExisting.Location.Line - 1;
		if (!Lines.IsValidIndex(Header) || !Lines[Header].Body.TrimEnd().EndsWith(TEXT("{")))
		{
			OutError = FString::Printf(TEXT("timeline '%s' does not open its block at the end of its line %d; write it across lines to edit it from the animation editor"),
				*InExisting.Name, InExisting.Location.Line);
			return false;
		}
		int32 Close = INDEX_NONE;
		for (int32 Index = Header + 1; Index < Lines.Num(); ++Index)
		{
			if (Lines[Index].Body.TrimStartAndEnd() == TEXT("}"))
			{
				Close = Index;
				break;
			}
		}
		if (Close == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("timeline '%s' does not close its block on a line of its own"), *InExisting.Name);
			return false;
		}
		// Every line a track or a key of the block stands on, so a line the block holds is known for what it is.
		TMap<int32, const FDreamUITimelineTrack*> TrackLines;
		TMap<int32, const FDreamUITimelineKey*> EventLines;
		for (const FDreamUITimelineTrack& Track : InExisting.Tracks)
		{
			for (const FDreamUITimelineKey& Key : Track.Keys)
			{
				if (Track.bIsEvent)
				{
					EventLines.Add(Key.Location.Line - 1, &Key);
				}
				else if (Key.Location.Line != Track.Location.Line)
				{
					OutError = FString::Printf(TEXT("the track on line %d of timeline '%s' runs across lines; write it on one to edit it from the animation editor"),
						Track.Location.Line, *InExisting.Name);
					return false;
				}
			}
			if (!Track.bIsEvent)
			{
				TrackLines.Add(Track.Location.Line - 1, &Track);
			}
		}

		const FReadTrack* EventTrack = InTracks.FindByPredicate([](const FReadTrack& InTrack) { return InTrack.bIsEvent; });
		double LastKey = 0.0;
		for (const FReadTrack& Track : InTracks)
		{
			for (const FReadKey& Key : Track.Keys)
			{
				LastKey = FMath::Max(LastKey, Key.Time);
			}
			for (const TPair<double, FString>& Event : Track.Events)
			{
				LastKey = FMath::Max(LastKey, Event.Key);
			}
		}
		const bool bDurationWritten = InExisting.Duration > 0.0;
		const bool bDurationNeeded = FMath::Abs(InDuration - LastKey) > 0.5 / InTickRate;
		const bool bDurationChanged = bDurationWritten ? FMath::Abs(InDuration - InExisting.Duration) > 0.5 / InTickRate : bDurationNeeded;

		FString Indent;
		TArray<FString> Body;
		TSet<const FDreamUITimelineTrack*> Kept;
		TSet<int32> KeptEvents;
		bool bDurationLine = false;
		for (int32 Index = Header + 1; Index < Close; ++Index)
		{
			const FTextLine& Line = Lines[Index];
			if (Indent.IsEmpty() && !Line.Body.TrimStartAndEnd().IsEmpty())
			{
				Indent = LeadingWhitespace(Line.Body);
			}
			if (const FDreamUITimelineTrack* const* Track = TrackLines.Find(Index))
			{
				const FReadTrack* Read = InTracks.FindByPredicate([Track](const FReadTrack& InRead) { return InRead.Existing == *Track && !InRead.bIsEvent; });
				if (Read == nullptr)
				{
					continue;
				}
				Kept.Add(*Track);
				Body.Add(Unchanged(*Read, InTickRate) ? Line.Body : LeadingWhitespace(Line.Body) + RenderTrack(*Read));
				continue;
			}
			if (const FDreamUITimelineKey* const* Event = EventLines.Find(Index))
			{
				const int32 Found = EventTrack == nullptr ? INDEX_NONE : EventTrack->Events.IndexOfByPredicate([Event, InTickRate](const TPair<double, FString>& InEvent)
				{
					return FMath::Abs(InEvent.Key - (*Event)->Time) <= 0.5 / InTickRate && InEvent.Value == (*Event)->EventName;
				});
				if (Found != INDEX_NONE && !KeptEvents.Contains(Found))
				{
					KeptEvents.Add(Found);
					Body.Add(Line.Body);
				}
				continue;
			}
			const FString Setting = SettingOf(Line.Body);
			if (Setting == TEXT("duration"))
			{
				bDurationLine = true;
				if (!bDurationChanged)
				{
					Body.Add(Line.Body);
				}
				else if (bDurationNeeded || bDurationWritten)
				{
					Body.Add(LeadingWhitespace(Line.Body) + TEXT("duration = ") + PrintTime(InDuration));
				}
				continue;
			}
			// Comments, blank lines, `loop`: nobody's but the author's.
			Body.Add(Line.Body);
		}
		if (Indent.IsEmpty())
		{
			Indent = LeadingWhitespace(Lines[Header].Body) + TEXT("    ");
		}
		if (!bDurationLine && bDurationChanged)
		{
			Body.Insert(Indent + TEXT("duration = ") + PrintTime(InDuration), 0);
		}
		for (const FReadTrack& Track : InTracks)
		{
			if (!Track.bIsEvent && (Track.Existing == nullptr || !Kept.Contains(Track.Existing)))
			{
				Body.Add(Indent + RenderTrack(Track));
			}
		}
		if (EventTrack != nullptr)
		{
			for (int32 Index = 0; Index < EventTrack->Events.Num(); ++Index)
			{
				if (!KeptEvents.Contains(Index))
				{
					Body.Add(FString::Printf(TEXT("%s@%s -> %s"), *Indent, *PrintTime(EventTrack->Events[Index].Key), *EventTrack->Events[Index].Value));
				}
			}
		}

		const FString Ending = Lines[Header].Ending.IsEmpty() ? FString(TEXT("\n")) : Lines[Header].Ending;
		FString Result;
		for (int32 Index = 0; Index <= Header; ++Index)
		{
			Result += Lines[Index].Body + Lines[Index].Ending;
		}
		for (const FString& Line : Body)
		{
			Result += Line + Ending;
		}
		for (int32 Index = Close; Index < Lines.Num(); ++Index)
		{
			Result += Lines[Index].Body + Lines[Index].Ending;
		}
		OutText = MoveTemp(Result);
		return true;
	}
}

bool FDreamUITimelineWriteBack::RewriteText(const FString& InText, UDreamWidgetAnimation* InAnimation, const UDreamWidget* InHost,
	FString& OutText, FString& OutError)
{
	using namespace DreamUITimelineWriteBackLocal;
	if (!IsValid(InAnimation) || InAnimation->GetMovieScene() == nullptr || !IsValid(InHost))
	{
		OutError = TEXT("there is no animation to write");
		return false;
	}
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	if (!FDreamUISourceFile::Parse(InText, FString(), Ast, Diagnostics, FDreamUISourceFile::MakeFileImportReader()))
	{
		OutError = TEXT("the file does not parse, so the animation was not written into it");
		return false;
	}
	const FString Name = InAnimation->GetDisplayNameString();
	const FDreamUITimeline* Existing = Ast.Timelines.FindByPredicate([&Name](const FDreamUITimeline& InTimeline)
	{
		return !InTimeline.bExternal && InTimeline.Name.Equals(Name, ESearchCase::CaseSensitive) && InTimeline.SourceName.IsEmpty();
	});
	if (Existing == nullptr)
	{
		OutError = FString::Printf(TEXT("the file has no 'timeline %s { }' block of its own to write into"), *Name);
		return false;
	}
	TArray<FReadTrack> Tracks;
	double Duration = 0.0;
	FString Refusal;
	if (!ReadTracks(InAnimation, InHost, *Existing, Tracks, Duration, Refusal))
	{
		OutError = FString::Printf(TEXT("%s; edit the block in the .dui, or write 'timeline %s external' to hand the animation to Sequencer for good"),
			*Refusal, *Name);
		return false;
	}
	return Rewrite(InText, *Existing, Tracks, Duration, InAnimation->GetMovieScene()->GetTickResolution().AsDecimal(), OutText, OutError);
}

bool FDreamUITimelineWriteBack::CanWriteBack(const UDreamWidgetAnimation* InAnimation)
{
	return IsValid(InAnimation) && InAnimation->IsLanguageOwned()
		&& DreamUITextAuthoring::IsTextAuthored(InAnimation->GetTypedOuter<UDreamWidgetBlueprint>());
}

bool FDreamUITimelineWriteBack::WriteAnimation(UDreamWidgetBlueprint* InBlueprint, UDreamWidgetAnimation* InAnimation, FText& OutReason)
{
	if (!DreamUITextAuthoring::IsTextAuthored(InBlueprint) || !IsValid(InAnimation) || !InAnimation->IsLanguageOwned())
	{
		OutReason = LOCTEXT("NotAFileTimeline", "this animation is not a 'timeline' block of a .dui");
		return false;
	}
	const UDreamWidget* Host = InAnimation->GetTypedOuter<UDreamWidget>();
	FString Error;
	TSharedPtr<FDreamUITextWriteBack> WriteBack;
	if (FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindEditorForBlueprint(InBlueprint))
	{
		WriteBack = Designer->GetTextWriteBack();
	}
	if (!WriteBack.IsValid())
	{
		WriteBack = FDreamUITextWriteBack::Create(UDreamTextUserWidget::ResolveDuiFilePath(DreamUITextAuthoring::GetAuthoredSourcePath(InBlueprint)), nullptr, Error);
	}
	if (!WriteBack.IsValid())
	{
		OutReason = FText::FromString(Error);
		return false;
	}
	FString Updated;
	if (!RewriteText(WriteBack->GetDocumentText(), InAnimation, Host, Updated, Error) || !WriteBack->ApplyTextChange(Updated, Error))
	{
		OutReason = FText::FromString(Error);
		return false;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
