// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Animation/DreamUIDirectAnimationEvaluation.h"

#include "Channels/IMovieSceneChannelOverrideProvider.h"
#include "CoreGlobals.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "IMovieScenePlayer.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneSequence.h"
#include "MovieSceneTracksPropertyTypes.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Sections/MovieSceneColorSection.h"
#include "Sections/MovieSceneDoubleSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneRotatorSection.h"
#include "Sections/MovieSceneVectorSection.h"
#include "TrackInstancePropertyBindings.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieSceneDoubleTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneRotatorTrack.h"
#include "Tracks/MovieSceneVectorTrack.h"

namespace DreamUIDirectAnimationLocal
{
	/** The track kinds evaluated here, matched by exact class: a subclass may evaluate its own way. */
	enum class ETrackKind : uint8
	{
		Float,
		Double,
		Bool,
		Rotator,
		DoubleVector,
		FloatVector,
		Color,
	};

	bool FindTrackKind(const UMovieSceneTrack& InTrack, ETrackKind& OutKind)
	{
		const UClass* Class = InTrack.GetClass();
		if (Class == UMovieSceneFloatTrack::StaticClass()) { OutKind = ETrackKind::Float; return true; }
		if (Class == UMovieSceneDoubleTrack::StaticClass()) { OutKind = ETrackKind::Double; return true; }
		if (Class == UMovieSceneBoolTrack::StaticClass()) { OutKind = ETrackKind::Bool; return true; }
		if (Class == UMovieSceneRotatorTrack::StaticClass()) { OutKind = ETrackKind::Rotator; return true; }
		if (Class == UMovieSceneDoubleVectorTrack::StaticClass()) { OutKind = ETrackKind::DoubleVector; return true; }
		if (Class == UMovieSceneFloatVectorTrack::StaticClass()) { OutKind = ETrackKind::FloatVector; return true; }
		if (Class == UMovieSceneColorTrack::StaticClass()) { OutKind = ETrackKind::Color; return true; }
		return false;
	}

	/** The section's class is the one its track makes, and it has no procedural overrides on its channels. */
	bool IsPlainSection(ETrackKind InKind, const UMovieSceneSection& InSection)
	{
		const UClass* Class = InSection.GetClass();
		switch (InKind)
		{
		case ETrackKind::Float:
		{
			// Asked through the interface: the section keeps its override of it protected.
			IMovieSceneChannelOverrideProvider* Overrides = Cast<IMovieSceneChannelOverrideProvider>(const_cast<UMovieSceneSection*>(&InSection));
			return Class == UMovieSceneFloatSection::StaticClass()
				&& (Overrides == nullptr || Overrides->GetChannelOverrideRegistry(false) == nullptr);
		}
		case ETrackKind::Double: return Class == UMovieSceneDoubleSection::StaticClass();
		case ETrackKind::Bool: return Class == UMovieSceneBoolSection::StaticClass();
		case ETrackKind::Rotator: return Class == UMovieSceneRotatorSection::StaticClass();
		case ETrackKind::DoubleVector: return Class == UMovieSceneDoubleVectorSection::StaticClass();
		case ETrackKind::FloatVector: return Class == UMovieSceneFloatVectorSection::StaticClass();
		case ETrackKind::Color: return Class == UMovieSceneColorSection::StaticClass();
		}
		return false;
	}

	// Struct names are compared rather than struct objects: the float vector variants have no base-structure accessor.
	const FName Vector2fName(TEXT("Vector2f"));
	const FName Vector3fName(TEXT("Vector3f"));
	const FName Vector4fName(TEXT("Vector4f"));

	template<typename ChannelType>
	bool HasValues(const ChannelType* InChannel)
	{
		return InChannel != nullptr && (InChannel->GetNumKeys() > 0 || InChannel->GetDefault().IsSet());
	}

	/**
	 * A channel's value at a time, as its last evaluation in this frame found it. A wall of widgets playing one animation
	 * together evaluates the same channels at the same time, one player after another: this way each channel's keys are
	 * searched and interpolated once a frame rather than once a player, a few channels at a time.
	 *
	 * Each widget made from a blueprint has a copy of the blueprint's animations of its own -- the component's animations
	 * are instanced -- and so sections and channels of its own; but a copy keeps its section's signature, which every edit
	 * of the section's channels changes, so the signature and the channel's place in the section say which channel of which
	 * content it is, whichever copy holds it. A section with no signature is its own: its channels are kept by their address.
	 * An edit made between two evaluations of one frame changes the signature, and is not answered with the value before
	 * it. Game thread only, as the players are.
	 */
	struct FChannelMemo
	{
		FGuid Signature;
		/** The channel itself for a section with no signature; null for one with a signature, whose copies share. */
		const void* Channel = nullptr;
		uint32 Offset = 0;
		uint64 Frame = 0;
		FFrameTime Time;
		double Value = 0.0;
		bool bHasValue = false;
	};
	FChannelMemo ChannelMemos[64];

	template<typename ChannelType, typename ValueType>
	bool EvaluateShared(const ChannelType& InChannel, const FGuid& InSignature, uint32 InOffset, FFrameTime InTime, double& OutValue)
	{
		ValueType Value{};
		if (!IsInGameThread())
		{
			const bool bHasValue = InChannel.Evaluate(InTime, Value);
			OutValue = bHasValue ? static_cast<double>(Value) : OutValue;
			return bHasValue;
		}
		const bool bShared = InSignature.IsValid();
		const void* const Channel = bShared ? nullptr : static_cast<const void*>(&InChannel);
		const uint32 Hash = bShared ? HashCombineFast(GetTypeHash(InSignature), InOffset) : PointerHash(&InChannel);
		FChannelMemo& Memo = ChannelMemos[Hash % UE_ARRAY_COUNT(ChannelMemos)];
		if (Memo.Offset != InOffset || Memo.Channel != Channel || Memo.Frame != GFrameCounter || Memo.Time != InTime || Memo.Signature != InSignature)
		{
			Memo.bHasValue = InChannel.Evaluate(InTime, Value);
			Memo.Value = static_cast<double>(Value);
			Memo.Signature = InSignature;
			Memo.Channel = Channel;
			Memo.Offset = InOffset;
			Memo.Frame = GFrameCounter;
			Memo.Time = InTime;
		}
		if (Memo.bHasValue)
		{
			OutValue = Memo.Value;
		}
		return Memo.bHasValue;
	}
}

TSharedPtr<FDreamUIDirectAnimationEvaluation> FDreamUIDirectAnimationEvaluation::TryCreate(const UMovieSceneSequence& InSequence)
{
	using namespace DreamUIDirectAnimationLocal;
	const UMovieScene* MovieScene = InSequence.GetMovieScene();
	// Unbound tracks (events, sub-sequences, time warps, audio) and spawned objects are the sequencer's.
	if (MovieScene == nullptr || MovieScene->GetTracks().Num() > 0 || MovieScene->GetCameraCutTrack() != nullptr
		|| MovieScene->GetSpawnableCount() > 0)
	{
		return nullptr;
	}
	const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
	TSharedPtr<FDreamUIDirectAnimationEvaluation> Plan = MakeShared<FDreamUIDirectAnimationEvaluation>();
	Plan->Sequence = &InSequence;
	Plan->TickResolution = MovieScene->GetTickResolution();
	Plan->MovieSceneSignature = MovieScene->GetSignature();
	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		for (const UMovieSceneTrack* Track : Binding.GetTracks())
		{
			if (Track == nullptr || Track->IsEvalDisabled())
			{
				// A muted track does nothing in the sequencer either.
				continue;
			}
			ETrackKind Kind;
			if (!FindTrackKind(*Track, Kind))
			{
				return nullptr;
			}
			// One section with a say, absolute, fully weighted and there for the whole playback range: its channels are
			// the value. Anything that blends, eases or starts and stops is the sequencer's.
			const UMovieSceneSection* Active = nullptr;
			for (const UMovieSceneSection* Section : Track->GetAllSections())
			{
				if (Section == nullptr || !Section->IsActive())
				{
					continue;
				}
				if (Active != nullptr)
				{
					return nullptr;
				}
				Active = Section;
			}
			if (Active == nullptr)
			{
				continue;
			}
			const FOptionalMovieSceneBlendType BlendType = Active->GetBlendType();
			if (!IsPlainSection(Kind, *Active)
				|| (BlendType.IsValid() && BlendType.Get() != EMovieSceneBlendType::Absolute)
				|| Active->Easing.GetEaseInDuration() > 0 || Active->Easing.GetEaseOutDuration() > 0
				|| !Active->GetRange().Contains(PlaybackRange))
			{
				return nullptr;
			}
			const UMovieScenePropertyTrack* PropertyTrack = static_cast<const UMovieScenePropertyTrack*>(Track);
			FAnimatedProperty& Property = Plan->Properties.AddDefaulted_GetRef();
			Property.bDirectProperty = PropertyTrack->GetPropertyPath().ToString() == PropertyTrack->GetPropertyName().ToString();
			Property.BindingId = Binding.GetObjectGuid();
			Property.TrackKind = static_cast<uint8>(Kind);
			Property.Section = Active;
			Property.Bindings = MakeShared<FTrackInstancePropertyBindings>(PropertyTrack->GetPropertyName(), PropertyTrack->GetPropertyPath().ToString());
		}
	}
	return Plan->Properties.Num() > 0 ? Plan : nullptr;
}

FDreamUIDirectAnimationEvaluation::~FDreamUIDirectAnimationEvaluation() = default;

bool FDreamUIDirectAnimationEvaluation::BindChannels(FAnimatedProperty& InOutProperty)
{
	using namespace DreamUIDirectAnimationLocal;
	const UMovieSceneSection* Section = InOutProperty.Section.Get();
	if (Section == nullptr)
	{
		return false;
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		InOutProperty.FloatChannels[Index] = nullptr;
		InOutProperty.DoubleChannels[Index] = nullptr;
	}
	InOutProperty.BoolChannel = nullptr;
	switch (static_cast<ETrackKind>(InOutProperty.TrackKind))
	{
	case ETrackKind::Float:
		InOutProperty.NumChannels = 1;
		InOutProperty.FloatChannels[0] = &static_cast<const UMovieSceneFloatSection*>(Section)->GetChannel();
		break;
	case ETrackKind::Double:
		InOutProperty.NumChannels = 1;
		InOutProperty.DoubleChannels[0] = &static_cast<const UMovieSceneDoubleSection*>(Section)->GetChannel();
		break;
	case ETrackKind::Bool:
		InOutProperty.NumChannels = 1;
		InOutProperty.BoolChannel = &static_cast<const UMovieSceneBoolSection*>(Section)->GetChannel();
		break;
	case ETrackKind::Rotator:
	{
		// Channel order is the value's: roll, pitch, yaw (see ToValues).
		const UMovieSceneRotatorSection* Rotator = static_cast<const UMovieSceneRotatorSection*>(Section);
		InOutProperty.NumChannels = 3;
		InOutProperty.DoubleChannels[0] = &Rotator->GetChannelX();
		InOutProperty.DoubleChannels[1] = &Rotator->GetChannelY();
		InOutProperty.DoubleChannels[2] = &Rotator->GetChannelZ();
		break;
	}
	case ETrackKind::DoubleVector:
	{
		const UMovieSceneDoubleVectorSection* Vector = static_cast<const UMovieSceneDoubleVectorSection*>(Section);
		InOutProperty.NumChannels = Vector->GetChannelsUsed();
		for (int32 Index = 0; Index < InOutProperty.NumChannels; ++Index)
		{
			InOutProperty.DoubleChannels[Index] = &Vector->GetChannel(Index);
		}
		break;
	}
	case ETrackKind::FloatVector:
	{
		const UMovieSceneFloatVectorSection* Vector = static_cast<const UMovieSceneFloatVectorSection*>(Section);
		InOutProperty.NumChannels = Vector->GetChannelsUsed();
		for (int32 Index = 0; Index < InOutProperty.NumChannels; ++Index)
		{
			InOutProperty.FloatChannels[Index] = &Vector->GetChannel(Index);
		}
		break;
	}
	case ETrackKind::Color:
	{
		const UMovieSceneColorSection* Color = static_cast<const UMovieSceneColorSection*>(Section);
		InOutProperty.NumChannels = 4;
		InOutProperty.FloatChannels[0] = &Color->GetRedChannel();
		InOutProperty.FloatChannels[1] = &Color->GetGreenChannel();
		InOutProperty.FloatChannels[2] = &Color->GetBlueChannel();
		InOutProperty.FloatChannels[3] = &Color->GetAlphaChannel();
		break;
	}
	}
	InOutProperty.BoundSection = Section;
	for (int32 Index = 0; Index < InOutProperty.NumChannels; ++Index)
	{
		const void* Channel = InOutProperty.DoubleChannels[Index] != nullptr ? static_cast<const void*>(InOutProperty.DoubleChannels[Index])
			: static_cast<const void*>(InOutProperty.FloatChannels[Index]);
		InOutProperty.ChannelOffsets[Index] = Channel != nullptr
			? static_cast<uint32>(static_cast<const uint8*>(Channel) - reinterpret_cast<const uint8*>(Section)) : 0;
	}
	InOutProperty.bEveryChannelAnimated = true;
	for (int32 Index = 0; Index < InOutProperty.NumChannels; ++Index)
	{
		InOutProperty.bEveryChannelAnimated &= HasValues(InOutProperty.FloatChannels[Index]) || HasValues(InOutProperty.DoubleChannels[Index])
			|| HasValues(InOutProperty.BoolChannel);
	}
	return InOutProperty.NumChannels > 0;
}

bool FDreamUIDirectAnimationEvaluation::FindValueKind(FAnimatedProperty& InOutProperty, UObject& InObject)
{
	using namespace DreamUIDirectAnimationLocal;
	const FProperty* Property = InOutProperty.Bindings->GetProperty(InObject);
	if (Property == nullptr)
	{
		return false;
	}
	if (InOutProperty.bDirectProperty && Property->HasSetter())
	{
		InOutProperty.SetterClass = InObject.GetClass();
		InOutProperty.SetterProperty = Property;
	}
	const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
	const FName StructName = StructProperty != nullptr && StructProperty->Struct != nullptr ? StructProperty->Struct->GetFName() : NAME_None;
	switch (static_cast<ETrackKind>(InOutProperty.TrackKind))
	{
	case ETrackKind::Float:
	case ETrackKind::Double:
		if (Property->IsA<FFloatProperty>()) { InOutProperty.Kind = EValueKind::Float; return true; }
		if (Property->IsA<FDoubleProperty>()) { InOutProperty.Kind = EValueKind::Double; return true; }
		return false;
	case ETrackKind::Bool:
		if (Property->IsA<FBoolProperty>()) { InOutProperty.Kind = EValueKind::Bool; return true; }
		return false;
	case ETrackKind::Rotator:
		if (StructName == NAME_Rotator) { InOutProperty.Kind = EValueKind::Rotator; return true; }
		return false;
	case ETrackKind::DoubleVector:
		if (StructName == NAME_Vector2D && InOutProperty.NumChannels == 2) { InOutProperty.Kind = EValueKind::Vector2d; return true; }
		if (StructName == NAME_Vector && InOutProperty.NumChannels == 3) { InOutProperty.Kind = EValueKind::Vector3d; return true; }
		if (StructName == NAME_Vector4 && InOutProperty.NumChannels == 4) { InOutProperty.Kind = EValueKind::Vector4d; return true; }
		return false;
	case ETrackKind::FloatVector:
		if (StructName == Vector2fName && InOutProperty.NumChannels == 2) { InOutProperty.Kind = EValueKind::Vector2f; return true; }
		if (StructName == Vector3fName && InOutProperty.NumChannels == 3) { InOutProperty.Kind = EValueKind::Vector3f; return true; }
		if (StructName == Vector4fName && InOutProperty.NumChannels == 4) { InOutProperty.Kind = EValueKind::Vector4f; return true; }
		return false;
	case ETrackKind::Color:
		if (StructName == NAME_LinearColor) { InOutProperty.Kind = EValueKind::LinearColor; return true; }
		if (StructName == NAME_Color) { InOutProperty.Kind = EValueKind::Color; return true; }
		return false;
	}
	return false;
}

bool FDreamUIDirectAnimationEvaluation::Read(FAnimatedProperty& InProperty, UObject& InObject, FChannelValues& OutValues)
{
	FTrackInstancePropertyBindings& Bindings = *InProperty.Bindings;
	switch (InProperty.Kind)
	{
	case EValueKind::Float: OutValues.Values[0] = Bindings.GetCurrentValue<float>(InObject); return true;
	case EValueKind::Double: OutValues.Values[0] = Bindings.GetCurrentValue<double>(InObject); return true;
	case EValueKind::Bool: OutValues.bValue = Bindings.GetCurrentValue<bool>(InObject); return true;
	case EValueKind::Rotator:
	{
		const FRotator Value = Bindings.GetCurrentValue<FRotator>(InObject);
		OutValues.Values[0] = Value.Roll;
		OutValues.Values[1] = Value.Pitch;
		OutValues.Values[2] = Value.Yaw;
		return true;
	}
	case EValueKind::Vector2d:
	{
		const FVector2D Value = Bindings.GetCurrentValue<FVector2D>(InObject);
		OutValues.Values[0] = Value.X;
		OutValues.Values[1] = Value.Y;
		return true;
	}
	case EValueKind::Vector3d:
	{
		const FVector Value = Bindings.GetCurrentValue<FVector>(InObject);
		OutValues.Values[0] = Value.X;
		OutValues.Values[1] = Value.Y;
		OutValues.Values[2] = Value.Z;
		return true;
	}
	case EValueKind::Vector4d:
	{
		const FVector4 Value = Bindings.GetCurrentValue<FVector4>(InObject);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			OutValues.Values[Index] = Value[Index];
		}
		return true;
	}
	case EValueKind::Vector2f:
	{
		const FVector2f Value = Bindings.GetCurrentValue<FVector2f>(InObject);
		OutValues.Values[0] = Value.X;
		OutValues.Values[1] = Value.Y;
		return true;
	}
	case EValueKind::Vector3f:
	{
		const FVector3f Value = Bindings.GetCurrentValue<FVector3f>(InObject);
		OutValues.Values[0] = Value.X;
		OutValues.Values[1] = Value.Y;
		OutValues.Values[2] = Value.Z;
		return true;
	}
	case EValueKind::Vector4f:
	{
		const FVector4f Value = Bindings.GetCurrentValue<FVector4f>(InObject);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			OutValues.Values[Index] = Value[Index];
		}
		return true;
	}
	case EValueKind::LinearColor:
	{
		const FLinearColor Value = Bindings.GetCurrentValue<FLinearColor>(InObject);
		OutValues.Values[0] = Value.R;
		OutValues.Values[1] = Value.G;
		OutValues.Values[2] = Value.B;
		OutValues.Values[3] = Value.A;
		return true;
	}
	case EValueKind::Color:
	{
		// Channels hold linear values; the property holds sRGB. Converted as the sequencer's colour system converts.
		const UE::MovieScene::FIntermediateColor Value(Bindings.GetCurrentValue<FColor>(InObject));
		OutValues.Values[0] = Value.R;
		OutValues.Values[1] = Value.G;
		OutValues.Values[2] = Value.B;
		OutValues.Values[3] = Value.A;
		return true;
	}
	}
	return false;
}

template<typename ValueType>
void FDreamUIDirectAnimationEvaluation::WriteValue(FAnimatedProperty& InProperty, UObject& InObject, const ValueType& InValue)
{
	// What FTrackInstancePropertyBindings::CallFunction does first for a property with a native setter, without its map.
	if (InProperty.SetterProperty != nullptr && InObject.GetClass() == InProperty.SetterClass)
	{
		InProperty.SetterProperty->CallSetter(&InObject, &InValue);
		return;
	}
	InProperty.Bindings->CallFunction<ValueType>(InObject, InValue);
}

void FDreamUIDirectAnimationEvaluation::Write(FAnimatedProperty& InProperty, UObject& InObject, const FChannelValues& InValues)
{
	const double* V = InValues.Values;
	switch (InProperty.Kind)
	{
	case EValueKind::Float: WriteValue<float>(InProperty, InObject, static_cast<float>(V[0])); break;
	case EValueKind::Double: WriteValue<double>(InProperty, InObject, V[0]); break;
	case EValueKind::Bool: InProperty.Bindings->CallFunction<bool>(InObject, InValues.bValue); break;
	case EValueKind::Rotator: WriteValue<FRotator>(InProperty, InObject, FRotator(V[1], V[2], V[0])); break;
	case EValueKind::Vector2d: WriteValue<FVector2D>(InProperty, InObject, FVector2D(V[0], V[1])); break;
	case EValueKind::Vector3d: WriteValue<FVector>(InProperty, InObject, FVector(V[0], V[1], V[2])); break;
	case EValueKind::Vector4d: WriteValue<FVector4>(InProperty, InObject, FVector4(V[0], V[1], V[2], V[3])); break;
	case EValueKind::Vector2f: WriteValue<FVector2f>(InProperty, InObject, FVector2f(V[0], V[1])); break;
	case EValueKind::Vector3f: WriteValue<FVector3f>(InProperty, InObject, FVector3f(V[0], V[1], V[2])); break;
	case EValueKind::Vector4f: WriteValue<FVector4f>(InProperty, InObject, FVector4f(V[0], V[1], V[2], V[3])); break;
	case EValueKind::LinearColor: WriteValue<FLinearColor>(InProperty, InObject, FLinearColor(V[0], V[1], V[2], V[3])); break;
	case EValueKind::Color:
		WriteValue<FColor>(InProperty, InObject, UE::MovieScene::FIntermediateColor(V[0], V[1], V[2], V[3]).GetColor());
		break;
	}
}

void FDreamUIDirectAnimationEvaluation::EvaluateChannels(const FAnimatedProperty& InProperty, FFrameTime InTime, FChannelValues& InOutValues)
{
	using namespace DreamUIDirectAnimationLocal;
	if (InProperty.BoolChannel != nullptr)
	{
		bool bValue = false;
		if (InProperty.BoolChannel->Evaluate(InTime, bValue))
		{
			InOutValues.bValue = bValue;
		}
		return;
	}
	// Channels are only read while their section is alive -- its sequence is, see Evaluate -- and its signature says which
	// content they are: see EvaluateShared.
	const FGuid Signature = InProperty.BoundSection != nullptr ? InProperty.BoundSection->GetSignature() : FGuid();
	for (int32 Index = 0; Index < InProperty.NumChannels; ++Index)
	{
		if (const FMovieSceneDoubleChannel* Channel = InProperty.DoubleChannels[Index])
		{
			EvaluateShared<FMovieSceneDoubleChannel, double>(*Channel, Signature, InProperty.ChannelOffsets[Index], InTime, InOutValues.Values[Index]);
		}
		else if (const FMovieSceneFloatChannel* FloatChannel = InProperty.FloatChannels[Index])
		{
			EvaluateShared<FMovieSceneFloatChannel, float>(*FloatChannel, Signature, InProperty.ChannelOffsets[Index], InTime, InOutValues.Values[Index]);
		}
	}
}

bool FDreamUIDirectAnimationEvaluation::Evaluate(IMovieScenePlayer& InPlayer, FFrameTime InTime)
{
	// No timing scope of its own: a wall of thousands of players pays for one per player, and the sequence tick manager's
	// scopes already say what the players cost together. The sequence is not looked up: the player holds it.
	/**
	 * A write runs the property's setter, and whatever listens to it may stop the animation, which the player then does on
	 * the spot -- the component's stop tears the player down as well. Nothing more is written after that. A restored play
	 * ends as the sequencer's would, which finishes the frame's writes and stops after; a kept one keeps the rest of that
	 * frame's properties at the previous frame's values, the price of never reading a torn-down player.
	 */
	TGuardValue<bool> Evaluating(bEvaluating, true);
	bStoppedWhileEvaluating = false;
	for (FAnimatedProperty& Property : Properties)
	{
		if (Property.NumChannels == 0 && !BindChannels(Property))
		{
			continue;
		}
		// See FAnimatedProperty::BoundObjects: the player's lookup, again only once an object it found is gone. Each object
		// is looked up once a frame, here, and written below as found here -- as the sequencer writes the objects its
		// bindings resolved to when the evaluation began. Copied: a listener of a write may start an evaluation of its own
		// that looks the objects up again.
		TArray<TPair<TWeakObjectPtr<UObject>, UObject*>, TInlineAllocator<4>> BoundObjects;
		bool bLookUp = !Property.bBoundObjectsFound;
		if (!bLookUp && Property.BoundHost != nullptr)
		{
			// See FAnimatedProperty::BoundHost.
			BoundObjects.Emplace(Property.BoundObjects[0], Property.BoundHost);
		}
		for (int32 Index = 0; !bLookUp && Property.BoundHost == nullptr && Index < Property.BoundObjects.Num(); ++Index)
		{
			UObject* const Found = Property.BoundObjects[Index].Get();
			bLookUp = Found == nullptr;
			BoundObjects.Emplace(Property.BoundObjects[Index], Found);
		}
		if (bLookUp)
		{
			Property.BoundObjects = TArray<TWeakObjectPtr<UObject>, TInlineAllocator<1>>(InPlayer.FindBoundObjects(Property.BindingId, MovieSceneSequenceID::Root));
			Property.bBoundObjectsFound = true;
			BoundObjects.Reset();
			for (const TWeakObjectPtr<UObject>& WeakObject : Property.BoundObjects)
			{
				BoundObjects.Emplace(WeakObject, WeakObject.Get());
			}
			UObject* const Host = InPlayer.GetPlaybackContext();
			Property.BoundHost = BoundObjects.Num() == 1 && Host != nullptr && BoundObjects[0].Value == Host ? Host : nullptr;
		}
		for (const TPair<TWeakObjectPtr<UObject>, UObject*>& Bound : BoundObjects)
		{
			UObject* Object = Bound.Value;
			if (Object == nullptr)
			{
				continue;
			}
			// The value the object had before the first write: what an unanimated channel keeps, as the sequencer keeps
			// it, and what a restore puts back. Found by the weak pointer's identity: the object is alive, so only a
			// pointer to it can have its index and serial number, and no entry's pointer is looked up for it.
			FChannelValues* Initial = nullptr;
			for (TPair<TWeakObjectPtr<UObject>, FChannelValues>& Entry : Property.InitialValues)
			{
				if (Entry.Key.HasSameIndexAndSerialNumber(Bound.Key))
				{
					Initial = &Entry.Value;
					break;
				}
			}
			if (Initial == nullptr)
			{
				if (!Property.bKindKnown)
				{
					if (!FindValueKind(Property, *Object))
					{
						// A property of a type only the sequencer converts to. Before anything is written it takes over
						// entirely; after, this object is simply left to its value.
						if (!bWrittenAnything)
						{
							return false;
						}
						continue;
					}
					Property.bKindKnown = true;
				}
				FChannelValues Values;
				if (!Read(Property, *Object, Values))
				{
					continue;
				}
				Initial = &Property.InitialValues.Emplace_GetRef(Bound.Key, Values).Value;
			}
			FChannelValues Values = *Initial;
			EvaluateChannels(Property, InTime, Values);
			Write(Property, *Object, Values);
			bWrittenAnything = true;
			if (bStoppedWhileEvaluating)
			{
				return true;
			}
		}
	}
	return true;
}

bool FDreamUIDirectAnimationEvaluation::IsStillPlanFor(const UMovieSceneSequence& InSequence) const
{
	const UMovieScene* MovieScene = InSequence.GetMovieScene();
	return Sequence.Get() == &InSequence && MovieScene != nullptr && MovieScene->GetSignature() == MovieSceneSignature
		&& MovieScene->GetTickResolution() == TickResolution;
}

void FDreamUIDirectAnimationEvaluation::RestoreInitialValues()
{
	bStoppedWhileEvaluating |= bEvaluating;
	for (FAnimatedProperty& Property : Properties)
	{
		for (TPair<TWeakObjectPtr<UObject>, FChannelValues>& Entry : Property.InitialValues)
		{
			if (UObject* Object = Entry.Key.Get())
			{
				Write(Property, *Object, Entry.Value);
			}
		}
		Property.InitialValues.Reset();
	}
	bWrittenAnything = false;
}

void FDreamUIDirectAnimationEvaluation::DiscardInitialValues()
{
	bStoppedWhileEvaluating |= bEvaluating;
	for (FAnimatedProperty& Property : Properties)
	{
		Property.InitialValues.Reset();
	}
	bWrittenAnything = false;
}
