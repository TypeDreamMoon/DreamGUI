// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUITimelineWriteBack.h"

#include "DreamWidgetBlueprint.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextWriteBack.h"
#include "Text/DreamUITimelineSampling.h"
#include "Text/DreamUIValueFormat.h"

#include "Animation/DreamUIAnimEventTrack.h"
#include "Animation/DreamWidgetAnimation.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"

#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneStringChannel.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneTimeHelpers.h"
#include "ScopedTransaction.h"
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
	using DreamUITimelineSampling::FLineKey;
	using DreamUITimelineSampling::FSample;

	enum class EValueFormat : uint8 { Number, Tuple, Hex };

	/** One key of a track as it is going to be written. */
	struct FReadKey
	{
		/** Where the sequence holds it: its frame and, in seconds, its time. */
		FFrameNumber Frame;
		double Time = 0.0;
		double Channels[4] = { 0.0, 0.0, 0.0, 0.0 };
		/** The curve to the next key, as the line will say it. */
		FString Ease;
		/** The key of the line this one is -- moved, changed or as it was -- or INDEX_NONE for one the line did not have. */
		int32 LineKey = INDEX_NONE;
		/** Its index in the section's channels, which all key the same frames. */
		int32 SequenceKey = INDEX_NONE;
		/** The time the line says once written, which is where the next compile stands the key. */
		double WrittenTime = 0.0;
	};

	struct FReadTrack
	{
		FString NodePath;
		FString PropertyName;
		bool bIsEvent = false;
		int32 ChannelCount = 1;
		bool bSinglePrecision = false;
		EValueFormat Format = EValueFormat::Number;
		/** The author's keys, in time order; the samples of the line's eases are not among them. */
		TArray<FReadKey> Keys;
		TArray<TPair<double, FString>> Events;
		/** The line of the block this track is, when it has one. */
		const FDreamUITimelineTrack* Existing = nullptr;
		/** Where the keys were read from, so the line's new samples can be put back there (Rebake). */
		UMovieSceneSection* Section = nullptr;
		TArray<FMovieSceneFloatChannel*> FloatChannels;
		TArray<FMovieSceneDoubleChannel*> DoubleChannels;
		/** The line was written differently from how the file had it. */
		bool bRewritten = false;
	};

	// ---------------------------------------------------------------------------------------------
	// Reading the sequence
	// ---------------------------------------------------------------------------------------------

	/**
	 * One channel's keys as frames and values. A key with automatic tangents becomes linear -- the language has no word
	 * for them, and the sequence is set to what the file will build. One with tangents of its own, or stepped, cannot be
	 * said, unless it is the last (whose curve goes nowhere).
	 */
	template<typename ChannelType>
	bool ReadChannel(ChannelType& InChannel, UMovieSceneSection* InSection, TArray<FFrameNumber>& OutFrames,
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
			OutFrames.Add(Times[Index]);
			OutValues.Add(static_cast<double>(Value.Value));
		}
		return true;
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

	/** Whether two sets of channel values are the same value, to the grain of the track's spelling. */
	bool NearValues(const FReadTrack& InTrack, const double InA[4], const double InB[4])
	{
		for (int32 Channel = 0; Channel < InTrack.ChannelCount; ++Channel)
		{
			// A colour's channels went through an sRGB byte each way: a step of 1/255 is the spelling's own grain.
			const double Tolerance = InTrack.Format == EValueFormat::Hex ? 0.5 / 255.0 + 1.0e-4 : 1.0e-4 * FMath::Max(1.0, FMath::Abs(InB[Channel]));
			if (FMath::Abs(InA[Channel] - InB[Channel]) > Tolerance)
			{
				return false;
			}
		}
		return true;
	}

	bool SameValue(const FReadTrack& InTrack, const FReadKey& InKey, const FDreamUITimelineKey& InLine)
	{
		double Channels[4] = { 0.0, 0.0, 0.0, 0.0 };
		return ParseChannels(InLine.Value, InTrack.ChannelCount, Channels) && NearValues(InTrack, InKey.Channels, Channels);
	}

	bool SameEase(const FString& InA, const FString& InB)
	{
		auto Normal = [](const FString& InEase) { return InEase.IsEmpty() || InEase.Equals(TEXT("Linear"), ESearchCase::IgnoreCase) ? FString() : InEase; };
		return Normal(InA).Equals(Normal(InB), ESearchCase::IgnoreCase);
	}

	/** The line's keys as the builder read them, or false when one of its values is a spelling this cannot read. */
	bool LineKeysOf(const FReadTrack& InTrack, TArray<FLineKey>& OutKeys)
	{
		if (InTrack.Existing == nullptr)
		{
			return false;
		}
		for (const FDreamUITimelineKey& Line : InTrack.Existing->Keys)
		{
			FLineKey& Key = OutKeys.AddDefaulted_GetRef();
			Key.Time = Line.Time;
			Key.EaseName = Line.EaseName;
			if (!ParseChannels(Line.Value, InTrack.ChannelCount, Key.Channels))
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * The author's keys out of what the sequence holds, read against the line the animation was built from.
	 *
	 * The line says exactly which frames and values its eases were sampled to (DreamUITimelineSampling), so the sequence
	 * is read as an edit of that: a key standing where one of those samples stands, with its value, is the sample; every
	 * other key is the author's. The samples of an ease stay put when the key at either end of it is dragged -- Sequencer
	 * moves the key it was handed -- so the key a hand moved is the one that is no longer where the line put it, and the
	 * ease between it and its neighbour is the line's still. A segment one of whose samples is missing or changed has had
	 * its curve edited by hand: its keys are what is there now, each a linear key, and the ease into them goes.
	 *
	 * Which author's key is which of the line's: those still on their frame with their value are the keys they were; the
	 * ones between two such that moved or changed are paired with the line's keys between the same two, in order. A key
	 * keeps the ease the line gave it, unless the curve out of it was edited, or it was the last key of the line -- whose
	 * ease went nowhere -- and now has a key after it, which Sequencer joined to it with a straight line. A key that is
	 * last keeps whatever its text said.
	 */
	void ReadKeysAgainstLine(const TArray<FFrameNumber>& InFrames, const TArray<TArray<double>>& InChannels, double InTickRate,
		FReadTrack& InOutTrack)
	{
		const int32 Num = InFrames.Num();
		auto ValuesOf = [&InChannels](int32 InKey, double OutValues[4])
		{
			for (int32 Channel = 0; Channel < 4; ++Channel)
			{
				OutValues[Channel] = InChannels.IsValidIndex(Channel) ? InChannels[Channel][InKey] : 0.0;
			}
		};

		TArray<FLineKey> LineKeys;
		TArray<FSample> Expected;
		FString BadEase;
		const bool bAgainstLine = LineKeysOf(InOutTrack, LineKeys)
			&& DreamUITimelineSampling::Sample(LineKeys, InOutTrack.ChannelCount, InTickRate, Expected, BadEase);
		if (!bAgainstLine)
		{
			LineKeys.Reset();
		}

		TSet<int32> Samples;
		TArray<bool> CurveKept;
		CurveKept.Init(true, LineKeys.Num());
		{
			TMap<int32, int32> KeyAtFrame;
			for (int32 Key = 0; Key < Num; ++Key)
			{
				KeyAtFrame.FindOrAdd(InFrames[Key].Value, Key);
			}
			TArray<TArray<int32>> SampleKeysOf;
			SampleKeysOf.SetNum(LineKeys.Num());
			for (const FSample& Sample : Expected)
			{
				if (Sample.bIsLineKey)
				{
					continue;
				}
				const int32* Found = KeyAtFrame.Find(Sample.Frame.Value);
				double Values[4];
				if (Found != nullptr)
				{
					ValuesOf(*Found, Values);
				}
				if (Found == nullptr || !NearValues(InOutTrack, Values, Sample.Channels))
				{
					CurveKept[Sample.LineKey] = false;
				}
				else
				{
					SampleKeysOf[Sample.LineKey].Add(*Found);
				}
			}
			for (int32 Line = 0; Line < LineKeys.Num(); ++Line)
			{
				if (CurveKept[Line])
				{
					Samples.Append(SampleKeysOf[Line]);
				}
			}
		}
		TArray<int32> Authored;
		for (int32 Key = 0; Key < Num; ++Key)
		{
			if (!Samples.Contains(Key))
			{
				Authored.Add(Key);
			}
		}

		// The longest run of keys still as the line has them, in order.
		const int32 AuthoredNum = Authored.Num();
		const int32 LineNum = LineKeys.Num();
		auto StillAsWritten = [&](int32 InAuthored, int32 InLine)
		{
			double Values[4];
			ValuesOf(Authored[InAuthored], Values);
			return InFrames[Authored[InAuthored]] == DreamUITimelineSampling::TimeToFrame(LineKeys[InLine].Time, InTickRate)
				&& NearValues(InOutTrack, Values, LineKeys[InLine].Channels);
		};
		TArray<int32> Run;
		Run.SetNumZeroed((AuthoredNum + 1) * (LineNum + 1));
		auto RunAt = [&Run, LineNum](int32 InAuthored, int32 InLine) -> int32& { return Run[InAuthored * (LineNum + 1) + InLine]; };
		for (int32 A = AuthoredNum - 1; A >= 0; --A)
		{
			for (int32 L = LineNum - 1; L >= 0; --L)
			{
				RunAt(A, L) = StillAsWritten(A, L) ? RunAt(A + 1, L + 1) + 1 : FMath::Max(RunAt(A + 1, L), RunAt(A, L + 1));
			}
		}
		TArray<TPair<int32, int32>> Kept;
		for (int32 A = 0, L = 0; A < AuthoredNum && L < LineNum;)
		{
			if (StillAsWritten(A, L) && RunAt(A, L) == RunAt(A + 1, L + 1) + 1)
			{
				Kept.Add({ A, L });
				++A;
				++L;
			}
			else if (RunAt(A + 1, L) >= RunAt(A, L + 1))
			{
				++A;
			}
			else
			{
				++L;
			}
		}
		Kept.Add({ AuthoredNum, LineNum });
		TArray<int32> LineOf;
		LineOf.Init(INDEX_NONE, AuthoredNum);
		int32 PreviousA = -1;
		int32 PreviousL = -1;
		for (const TPair<int32, int32>& Pair : Kept)
		{
			for (int32 Step = 0; PreviousA + 1 + Step < Pair.Key && PreviousL + 1 + Step < Pair.Value; ++Step)
			{
				LineOf[PreviousA + 1 + Step] = PreviousL + 1 + Step;
			}
			if (Pair.Key < AuthoredNum)
			{
				LineOf[Pair.Key] = Pair.Value;
			}
			PreviousA = Pair.Key;
			PreviousL = Pair.Value;
		}

		for (int32 A = 0; A < AuthoredNum; ++A)
		{
			FReadKey& Key = InOutTrack.Keys.AddDefaulted_GetRef();
			Key.SequenceKey = Authored[A];
			Key.Frame = InFrames[Authored[A]];
			Key.Time = static_cast<double>(Key.Frame.Value) / InTickRate;
			Key.WrittenTime = Key.Time;
			ValuesOf(Authored[A], Key.Channels);
			Key.LineKey = LineOf[A];
			if (Key.LineKey == INDEX_NONE)
			{
				continue;
			}
			const bool bLast = A + 1 == AuthoredNum;
			const bool bHadCurve = Key.LineKey + 1 < LineNum;
			if (bLast || (bHadCurve && CurveKept[Key.LineKey]))
			{
				Key.Ease = LineKeys[Key.LineKey].EaseName;
			}
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

		const UMovieScene* ConstMovieScene = MovieScene;
		for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
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
				Read.Section = Section;
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

				TArray<FFrameNumber> Frames;
				TArray<TArray<double>> Channels;
				auto ReadOne = [&](auto& InChannel)
				{
					TArray<FFrameNumber> ChannelFrames;
					TArray<double>& Values = Channels.AddDefaulted_GetRef();
					if (!ReadChannel(InChannel, Section, ChannelFrames, Values, OutRefusal))
					{
						return false;
					}
					if (Channels.Num() == 1)
					{
						Frames = ChannelFrames;
						return true;
					}
					if (ChannelFrames != Frames)
					{
						OutRefusal = FString::Printf(TEXT("the channels of '%s' are keyed apart, and a timeline key sets all of them at once"), *Read.PropertyName);
						return false;
					}
					return true;
				};

				bool bRead = true;
				if (UMovieSceneColorSection* Color = Cast<UMovieSceneColorSection>(Section))
				{
					Read.ChannelCount = 4;
					Read.bSinglePrecision = true;
					Read.Format = EValueFormat::Hex;
					Read.FloatChannels = { &Color->GetRedChannel(), &Color->GetGreenChannel(), &Color->GetBlueChannel(), &Color->GetAlphaChannel() };
				}
				else if (UMovieSceneFloatSection* Float = Cast<UMovieSceneFloatSection>(Section))
				{
					Read.bSinglePrecision = true;
					TArrayView<FMovieSceneFloatChannel*> FloatChannels = Float->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
					if (FloatChannels.Num() > 0)
					{
						Read.FloatChannels = { FloatChannels[0] };
					}
				}
				else if (UMovieSceneDoubleSection* Double = Cast<UMovieSceneDoubleSection>(Section))
				{
					TArrayView<FMovieSceneDoubleChannel*> DoubleChannels = Double->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
					if (DoubleChannels.Num() > 0)
					{
						Read.DoubleChannels = { DoubleChannels[0] };
					}
				}
				else if (UMovieSceneDoubleVectorSection* Vector = Cast<UMovieSceneDoubleVectorSection>(Section))
				{
					Read.ChannelCount = Vector->GetChannelsUsed();
					Read.Format = EValueFormat::Tuple;
					TArrayView<FMovieSceneDoubleChannel*> DoubleChannels = Vector->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
					for (int32 Index = 0; Index < Read.ChannelCount; ++Index)
					{
						if (!DoubleChannels.IsValidIndex(Index))
						{
							bRead = false;
							break;
						}
						Read.DoubleChannels.Add(DoubleChannels[Index]);
					}
				}
				else
				{
					OutRefusal = FString::Printf(TEXT("'%s' is a kind of track a timeline line cannot spell"), *Read.PropertyName);
					return false;
				}
				if (Read.FloatChannels.Num() + Read.DoubleChannels.Num() != Read.ChannelCount)
				{
					bRead = false;
				}
				for (FMovieSceneFloatChannel* Channel : Read.FloatChannels)
				{
					bRead = bRead && ReadOne(*Channel);
				}
				for (FMovieSceneDoubleChannel* Channel : Read.DoubleChannels)
				{
					bRead = bRead && ReadOne(*Channel);
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
				ReadKeysAgainstLine(Frames, Channels, TickRate, Read);
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

	/** The decimals a number was written with: `0.30` has two, `1` and `1e-3` none. */
	int32 DecimalsOf(const FString& InNumber)
	{
		int32 Dot = INDEX_NONE;
		if (!InNumber.FindChar(TEXT('.'), Dot) || InNumber.Contains(TEXT("e")) || InNumber.Contains(TEXT("E")))
		{
			return 0;
		}
		return InNumber.Len() - Dot - 1;
	}

	/**
	 * A time a hand set, spelled to within half a display frame -- the hand's precision, where Sequencer snapped it -- with
	 * no fewer decimals than the token it replaces, so a row of keys written to two places keeps reading in its columns:
	 * frame 28 of 60 is 0.47, and 0.58 dragged to 0.5 reads 0.50. OutWritten is what the spelling reads back as.
	 */
	FString SpellTime(double InSeconds, const FString& InReplacedToken, double InDisplayRate, double& OutWritten)
	{
		const double HalfFrame = InDisplayRate > 0.0 ? 0.5 / InDisplayRate : 0.0;
		return DreamUIValueFormat::PrintWithin(InSeconds, HalfFrame, DecimalsOf(InReplacedToken), &OutWritten);
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

	/** A key's value as the file will say it: the spelling the line already had for this very value, or a new one. */
	FString SpellValue(const FReadTrack& InTrack, const FReadKey& InKey)
	{
		if (InTrack.Existing != nullptr)
		{
			for (const FDreamUITimelineKey& Line : InTrack.Existing->Keys)
			{
				if (SameValue(InTrack, InKey, Line))
				{
					return Line.Value.Kind == EDreamUIValueKind::HexColor ? TEXT("#") + Line.Value.Raw : Line.Value.Raw;
				}
			}
		}
		return PrintValue(InTrack, InKey);
	}

	/** Whether a track says what its line says: the same keys, frames, values and eases (the last key's ease goes nowhere). */
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
			if (Key.LineKey != Index || Key.Frame != DreamUITimelineSampling::TimeToFrame(Line.Time, InTickRate) || !SameValue(InTrack, Key, Line)
				|| (Index + 1 < InTrack.Keys.Num() && !SameEase(Key.Ease, Line.EaseName)))
			{
				return false;
			}
		}
		return true;
	}

	/** A whole track line, spelled anew: for a track the block has no line for, or a line this cannot read token by token. */
	FString RenderTrack(FReadTrack& InTrack, double InDisplayRate)
	{
		TArray<FString> Keys;
		for (int32 Index = 0; Index < InTrack.Keys.Num(); ++Index)
		{
			FReadKey& Key = InTrack.Keys[Index];
			FString Spelled = FString::Printf(TEXT("%s = %s"), *SpellTime(Key.Time, FString(), InDisplayRate, Key.WrittenTime), *SpellValue(InTrack, Key));
			if (Index + 1 < InTrack.Keys.Num() && !Key.Ease.IsEmpty())
			{
				Spelled += TEXT(" ease ") + Key.Ease;
			}
			Keys.Add(Spelled);
		}
		const FString Path = InTrack.NodePath.IsEmpty() ? InTrack.PropertyName : InTrack.NodePath + TEXT(".") + InTrack.PropertyName;
		return FString::Printf(TEXT("%s : %s"), *Path, *FString::Join(Keys, TEXT(", ")));
	}

	/** Where each key of a track line stands in its text, so an edit can replace the token it changed and nothing else. */
	struct FKeySpan
	{
		/** The whole key: its time to the end of its value, or of its ease when it has one. */
		int32 Start = 0;
		int32 End = 0;
		int32 TimeStart = 0;
		int32 TimeEnd = 0;
		int32 ValueStart = 0;
		int32 ValueEnd = 0;
		/** ` ease Name`, from the blank in front of the word; INDEX_NONE when the key has none. */
		int32 EaseStart = INDEX_NONE;
		int32 EaseEnd = INDEX_NONE;
	};

	/** The keys of a one-line track, `Path : 0.0 = (0, 0) ease OutCubic, 0.3 = (1, 1)`, as spans of InBody; false if it is not that. */
	bool ScanKeys(const FString& InBody, TArray<FKeySpan>& OutSpans)
	{
		const int32 Len = InBody.Len();
		int32 Colon = INDEX_NONE;
		if (!InBody.FindChar(TEXT(':'), Colon))
		{
			return false;
		}
		auto IsBlank = [&InBody, Len](int32 InAt) { return InAt < Len && (InBody[InAt] == TEXT(' ') || InBody[InAt] == TEXT('\t')); };
		int32 At = Colon + 1;
		while (IsBlank(At))
		{
			++At;
		}
		for (;;)
		{
			FKeySpan& Span = OutSpans.AddDefaulted_GetRef();
			Span.Start = Span.TimeStart = At;
			while (At < Len && (FChar::IsDigit(InBody[At]) || InBody[At] == TEXT('.') || InBody[At] == TEXT('-') || InBody[At] == TEXT('+')
				|| InBody[At] == TEXT('e') || InBody[At] == TEXT('E')))
			{
				++At;
			}
			Span.TimeEnd = At;
			while (IsBlank(At))
			{
				++At;
			}
			if (Span.TimeEnd == Span.TimeStart || At >= Len || InBody[At] != TEXT('='))
			{
				return false;
			}
			++At;
			while (IsBlank(At))
			{
				++At;
			}
			Span.ValueStart = At;
			if (At < Len && InBody[At] == TEXT('('))
			{
				int32 Close = At;
				while (Close < Len && InBody[Close] != TEXT(')'))
				{
					++Close;
				}
				if (Close >= Len)
				{
					return false;
				}
				At = Close + 1;
			}
			else
			{
				while (At < Len && !IsBlank(At) && InBody[At] != TEXT(','))
				{
					++At;
				}
			}
			Span.ValueEnd = Span.End = At;
			if (Span.ValueEnd == Span.ValueStart)
			{
				return false;
			}
			int32 Word = At;
			while (IsBlank(Word))
			{
				++Word;
			}
			if (Word > At && InBody.Mid(Word, 4).Equals(TEXT("ease"), ESearchCase::CaseSensitive) && IsBlank(Word + 4))
			{
				int32 Name = Word + 4;
				while (IsBlank(Name))
				{
					++Name;
				}
				int32 NameEnd = Name;
				while (NameEnd < Len && (FChar::IsAlnum(InBody[NameEnd]) || InBody[NameEnd] == TEXT('_')))
				{
					++NameEnd;
				}
				if (NameEnd == Name)
				{
					return false;
				}
				Span.EaseStart = At;
				Span.EaseEnd = Span.End = At = NameEnd;
			}
			int32 Next = At;
			while (IsBlank(Next))
			{
				++Next;
			}
			if (Next < Len && InBody[Next] == TEXT(','))
			{
				At = Next + 1;
				while (IsBlank(At))
				{
					++At;
				}
				continue;
			}
			return true;
		}
	}

	/**
	 * InBody, a track line, saying what InTrack holds now with the least of it changed: a key that moved has its time
	 * replaced, one that changed its value its value, one whose ease went its ease -- and every other byte of the line
	 * stays, the padding that lines its keys up with the next line's, `0.0` written as `0.0`, the comment after it. A key
	 * added is spelled where it goes; a key taken out takes its text and the separator before it. False when the line is
	 * not one this can read key by key, for the caller to spell it whole.
	 */
	bool PatchLine(const FString& InBody, FReadTrack& InTrack, double InTickRate, double InDisplayRate, FString& OutBody)
	{
		TArray<FKeySpan> Spans;
		if (InTrack.Existing == nullptr || InTrack.Keys.IsEmpty() || !ScanKeys(InBody, Spans) || Spans.Num() != InTrack.Existing->Keys.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Spans.Num(); ++Index)
		{
			double Written = 0.0;
			if (!LexTryParseString(Written, *InBody.Mid(Spans[Index].TimeStart, Spans[Index].TimeEnd - Spans[Index].TimeStart))
				|| !FMath::IsNearlyEqual(Written, InTrack.Existing->Keys[Index].Time, 1.0e-9))
			{
				return false;
			}
		}
		auto Slice = [&InBody](int32 InStart, int32 InEnd) { return InBody.Mid(InStart, InEnd - InStart); };
		auto KeyText = [&](FReadKey& InKey, bool bInLast) -> FString
		{
			if (InKey.LineKey == INDEX_NONE)
			{
				FString Spelled = SpellTime(InKey.Time, FString(), InDisplayRate, InKey.WrittenTime) + TEXT(" = ") + SpellValue(InTrack, InKey);
				if (!bInLast && !InKey.Ease.IsEmpty())
				{
					Spelled += TEXT(" ease ") + InKey.Ease;
				}
				return Spelled;
			}
			const FKeySpan& Span = Spans[InKey.LineKey];
			const FDreamUITimelineKey& Line = InTrack.Existing->Keys[InKey.LineKey];
			FString Time = Slice(Span.TimeStart, Span.TimeEnd);
			InKey.WrittenTime = Line.Time;
			if (InKey.Frame != DreamUITimelineSampling::TimeToFrame(Line.Time, InTickRate))
			{
				Time = SpellTime(InKey.Time, Time, InDisplayRate, InKey.WrittenTime);
			}
			const FString Value = SameValue(InTrack, InKey, Line) ? Slice(Span.ValueStart, Span.ValueEnd) : SpellValue(InTrack, InKey);
			FString Ease = Span.EaseStart != INDEX_NONE ? Slice(Span.EaseStart, Span.EaseEnd) : FString();
			if (!SameEase(InKey.Ease, Line.EaseName))
			{
				Ease = InKey.Ease.IsEmpty() ? FString() : TEXT(" ease ") + InKey.Ease;
			}
			return Time + Slice(Span.TimeEnd, Span.ValueStart) + Value + Ease;
		};
		const FString FirstSeparator = Spans.Num() > 1 ? Slice(Spans[0].End, Spans[1].Start) : FString(TEXT(", "));
		FString Keys;
		for (int32 Index = 0; Index < InTrack.Keys.Num(); ++Index)
		{
			if (Index > 0)
			{
				// The separator the line had between these two, when they stood side by side in it.
				const int32 Before = InTrack.Keys[Index - 1].LineKey;
				const int32 Here = InTrack.Keys[Index].LineKey;
				Keys += Before != INDEX_NONE && Here == Before + 1 ? Slice(Spans[Before].End, Spans[Here].Start) : FirstSeparator;
			}
			Keys += KeyText(InTrack.Keys[Index], Index + 1 == InTrack.Keys.Num());
		}
		OutBody = InBody.Left(Spans[0].Start) + Keys + InBody.Mid(Spans.Last().End);
		return true;
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

	bool Rewrite(const FString& InText, const FDreamUITimeline& InExisting, TArray<FReadTrack>& InTracks, double InDuration,
		double InTickRate, double InDisplayRate, FString& OutText, FString& OutError)
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
		double Unused = 0.0;
		for (int32 Index = Header + 1; Index < Close; ++Index)
		{
			const FTextLine& Line = Lines[Index];
			if (Indent.IsEmpty() && !Line.Body.TrimStartAndEnd().IsEmpty())
			{
				Indent = LeadingWhitespace(Line.Body);
			}
			if (const FDreamUITimelineTrack* const* Track = TrackLines.Find(Index))
			{
				FReadTrack* Read = InTracks.FindByPredicate([Track](const FReadTrack& InRead) { return InRead.Existing == *Track && !InRead.bIsEvent; });
				if (Read == nullptr)
				{
					continue;
				}
				Kept.Add(*Track);
				if (Unchanged(*Read, InTickRate))
				{
					Body.Add(Line.Body);
					continue;
				}
				FString Patched;
				if (!PatchLine(Line.Body, *Read, InTickRate, InDisplayRate, Patched))
				{
					Patched = LeadingWhitespace(Line.Body) + RenderTrack(*Read, InDisplayRate);
				}
				Read->bRewritten = true;
				Body.Add(Patched);
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
					FString Written;
					Line.Body.Split(TEXT("="), nullptr, &Written);
					Body.Add(LeadingWhitespace(Line.Body) + TEXT("duration = ") + SpellTime(InDuration, Written.TrimStartAndEnd(), InDisplayRate, Unused));
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
			Body.Insert(Indent + TEXT("duration = ") + SpellTime(InDuration, FString(), InDisplayRate, Unused), 0);
		}
		for (FReadTrack& Track : InTracks)
		{
			if (!Track.bIsEvent && (Track.Existing == nullptr || !Kept.Contains(Track.Existing)))
			{
				Track.bRewritten = true;
				Body.Add(Indent + RenderTrack(Track, InDisplayRate));
			}
		}
		if (EventTrack != nullptr)
		{
			for (int32 Index = 0; Index < EventTrack->Events.Num(); ++Index)
			{
				if (!KeptEvents.Contains(Index))
				{
					Body.Add(FString::Printf(TEXT("%s@%s -> %s"), *Indent, *SpellTime(EventTrack->Events[Index].Key, FString(), InDisplayRate, Unused),
						*EventTrack->Events[Index].Value));
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

	/**
	 * A rewritten track's keys put where its new line builds them: its keys at the times the line now says, the samples
	 * of its eases taken out and made again between them. Without it the samples stayed where the old line had put them
	 * -- around a key that was dragged off -- until the next compile, and the next edit, read against the new line,
	 * found them in the wrong places and wrote the eased run out as a page of linear keys. The keys the author has hold
	 * their handles, so what Sequencer has selected stays selected.
	 */
	void Rebake(const FReadTrack& InTrack, double InTickRate)
	{
		if (InTrack.Section == nullptr || InTrack.Keys.IsEmpty())
		{
			return;
		}
		TArray<FLineKey> LineKeys;
		for (const FReadKey& Key : InTrack.Keys)
		{
			FLineKey& LineKey = LineKeys.AddDefaulted_GetRef();
			LineKey.Time = Key.WrittenTime;
			LineKey.EaseName = Key.Ease;
			FMemory::Memcpy(LineKey.Channels, Key.Channels, sizeof(LineKey.Channels));
		}
		TArray<FSample> Samples;
		FString BadEase;
		if (!DreamUITimelineSampling::Sample(LineKeys, InTrack.ChannelCount, InTickRate, Samples, BadEase))
		{
			return;
		}
		InTrack.Section->Modify();
		TSet<int32> Authored;
		for (const FReadKey& Key : InTrack.Keys)
		{
			Authored.Add(Key.SequenceKey);
		}
		auto Settle = [&](auto& InChannel, int32 InChannelIndex)
		{
			auto Data = InChannel.GetData();
			TArray<FKeyHandle> Keys;
			for (const FReadKey& Key : InTrack.Keys)
			{
				Keys.Add(Data.GetHandle(Key.SequenceKey));
			}
			TArray<FKeyHandle> Doomed;
			for (int32 Index = 0; Index < Data.GetTimes().Num(); ++Index)
			{
				if (!Authored.Contains(Index))
				{
					Doomed.Add(Data.GetHandle(Index));
				}
			}
			for (const FKeyHandle& Handle : Doomed)
			{
				const int32 Index = Data.GetIndex(Handle);
				if (Index != INDEX_NONE)
				{
					Data.RemoveKey(Index);
				}
			}
			for (int32 Key = 0; Key < Keys.Num(); ++Key)
			{
				const int32 Index = Data.GetIndex(Keys[Key]);
				const FFrameNumber Frame = DreamUITimelineSampling::TimeToFrame(InTrack.Keys[Key].WrittenTime, InTickRate);
				if (Index != INDEX_NONE && Data.GetTimes()[Index] != Frame)
				{
					Data.MoveKey(Index, Frame);
				}
			}
			for (const FSample& Sample : Samples)
			{
				if (!Sample.bIsLineKey)
				{
					InChannel.AddLinearKey(Sample.Frame, static_cast<decltype(Data.GetValues()[0].Value)>(Sample.Channels[InChannelIndex]));
				}
			}
		};
		int32 ChannelIndex = 0;
		for (FMovieSceneFloatChannel* Channel : InTrack.FloatChannels)
		{
			Settle(*Channel, ChannelIndex++);
		}
		for (FMovieSceneDoubleChannel* Channel : InTrack.DoubleChannels)
		{
			Settle(*Channel, ChannelIndex++);
		}
	}

	/** The block of InAnimation's timeline in InText, rewritten; OutTracks is what was read, for Rebake. */
	bool Plan(const FString& InText, UDreamWidgetAnimation* InAnimation, const UDreamWidget* InHost, TArray<FReadTrack>& OutTracks,
		FString& OutText, FString& OutError)
	{
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
		double Duration = 0.0;
		FString Refusal;
		if (!ReadTracks(InAnimation, InHost, *Existing, OutTracks, Duration, Refusal))
		{
			OutError = FString::Printf(TEXT("%s; edit the block in the .dui, or write 'timeline %s external' to hand the animation to Sequencer for good"),
				*Refusal, *Name);
			return false;
		}
		const UMovieScene* MovieScene = InAnimation->GetMovieScene();
		return Rewrite(InText, *Existing, OutTracks, Duration, MovieScene->GetTickResolution().AsDecimal(),
			MovieScene->GetDisplayRate().AsDecimal(), OutText, OutError);
	}
}

bool FDreamUITimelineWriteBack::RewriteText(const FString& InText, UDreamWidgetAnimation* InAnimation, const UDreamWidget* InHost,
	FString& OutText, FString& OutError)
{
	TArray<DreamUITimelineWriteBackLocal::FReadTrack> Tracks;
	return DreamUITimelineWriteBackLocal::Plan(InText, InAnimation, InHost, Tracks, OutText, OutError);
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
	const FString Current = WriteBack->GetDocumentText();
	TArray<DreamUITimelineWriteBackLocal::FReadTrack> Tracks;
	FString Updated;
	if (!DreamUITimelineWriteBackLocal::Plan(Current, InAnimation, Host, Tracks, Updated, Error))
	{
		OutReason = FText::FromString(Error);
		return false;
	}
	if (Updated.Equals(Current, ESearchCase::CaseSensitive))
	{
		return true;
	}
	// The file and the keys it now puts elsewhere are one edit, and one undo step -- inside the gesture's own, when the
	// animation editor writes as that closes (it folds into it by the transaction counter).
	FScopedTransaction Transaction(LOCTEXT("WriteTimeline", "Edit Timeline"));
	if (!WriteBack->ApplyTextChange(Updated, Error))
	{
		OutReason = FText::FromString(Error);
		return false;
	}
	const double TickRate = InAnimation->GetMovieScene()->GetTickResolution().AsDecimal();
	for (const DreamUITimelineWriteBackLocal::FReadTrack& Track : Tracks)
	{
		if (Track.bRewritten && !Track.bIsEvent)
		{
			DreamUITimelineWriteBackLocal::Rebake(Track, TickRate);
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
