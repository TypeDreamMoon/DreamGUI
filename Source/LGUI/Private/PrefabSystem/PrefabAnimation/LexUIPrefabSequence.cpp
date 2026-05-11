// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequence.h"
#include "MovieScene.h"
#include "MovieSceneCommonHelpers.h"
#include "Core/Components/LexWidget.h"
#include "Modules/ModuleManager.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequenceComponent.h"
#include "Engine/LevelScriptActor.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneMaterialParameterCollectionTrack.h"

#if WITH_EDITOR
ULexUIPrefabSequence::FOnInitialize ULexUIPrefabSequence::OnInitializeSequenceEvent;
#endif

static TAutoConsoleVariable<int32> CVarDefaultEvaluationType(
	TEXT("LGUIPrefabSequence.DefaultEvaluationType"),
	0,
	TEXT("0: Playback locked to playback frames\n1: Unlocked playback with sub frame interpolation"),
	ECVF_Default);

static TAutoConsoleVariable<FString> CVarDefaultTickResolution(
	TEXT("LGUIPrefabSequence.DefaultTickResolution"),
	TEXT("24000fps"),
	TEXT("Specifies default a tick resolution for newly created level sequences. Examples: 30 fps, 120/1 (120 fps), 30000/1001 (29.97), 0.01s (10ms)."),
	ECVF_Default);

static TAutoConsoleVariable<FString> CVarDefaultDisplayRate(
	TEXT("LGUIPrefabSequence.DefaultDisplayRate"),
	TEXT("30fps"),
	TEXT("Specifies default a display frame rate for newly created level sequences; also defines frame locked frame rate where sequences are set to be frame locked. Examples: 30 fps, 120/1 (120 fps), 30000/1001 (29.97), 0.01s (10ms)."),
	ECVF_Default);

ULexUIPrefabSequence::ULexUIPrefabSequence(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MovieScene(nullptr)
#if WITH_EDITORONLY_DATA
	, bHasBeenInitialized(false)
#endif
{
	bParentContextsAreSignificant = true;

	MovieScene = ObjectInitializer.CreateDefaultSubobject<UMovieScene>(this, "MovieScene");
	MovieScene->SetFlags(RF_Transactional);

	DisplayNameString = this->GetName();
}

bool ULexUIPrefabSequence::IsEditable() const
{
	return true;
	//UObject* Template = GetArchetype();

	//if (Template == GetDefault<ULGUIPrefabSequence>())
	//{
	//	return false;
	//}

	//return !Template || Template->GetTypedOuter<ULGUIPrefabSequenceComponent>() == GetDefault<ULGUIPrefabSequenceComponent>();
}

void ULexUIPrefabSequence::PostInitProperties()
{
#if WITH_EDITOR && WITH_EDITORONLY_DATA

	// We do not run the default initialization for actor sequences that are CDOs, or that are going to be loaded (since they will have already been initialized in that case)
	EObjectFlags ExcludeFlags = RF_ClassDefaultObject | RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_WasLoaded;

	UActorComponent* OwnerComponent = Cast<UActorComponent>(GetOuter());
	if (!bHasBeenInitialized && !HasAnyFlags(ExcludeFlags) && OwnerComponent && !OwnerComponent->HasAnyFlags(ExcludeFlags))
	{
		const bool bFrameLocked = CVarDefaultEvaluationType.GetValueOnGameThread() != 0;

		MovieScene->SetEvaluationType(bFrameLocked ? EMovieSceneEvaluationType::FrameLocked : EMovieSceneEvaluationType::WithSubFrames);

		FFrameRate TickResolution(60000, 1);
		TryParseString(TickResolution, *CVarDefaultTickResolution.GetValueOnGameThread());
		MovieScene->SetTickResolutionDirectly(TickResolution);

		FFrameRate DisplayRate(30, 1);
		TryParseString(DisplayRate, *CVarDefaultDisplayRate.GetValueOnGameThread());
		MovieScene->SetDisplayRate(DisplayRate);

		OnInitializeSequenceEvent.Broadcast(this);
		bHasBeenInitialized = true;
	}
#endif

	Super::PostInitProperties();
}

void ULexUIPrefabSequence::BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context)
{
	FLexUIPrefabSequenceObjectReference ObjectRef;
	auto ActorContext = CastChecked<ULexWidget>(Context);
	auto* Actor = Cast<ULexWidget>(&PossessedObject);
	if (Actor == nullptr)
	{
		Actor = PossessedObject.GetTypedOuter<ULexWidget>();
	}
	check(Actor != nullptr);
	if (FLexUIPrefabSequenceObjectReference::CreateForObject(Actor, &PossessedObject, ObjectRef))
	{
		ObjectReferences.CreateBinding(ObjectId, ObjectRef);
	}
}

bool ULexUIPrefabSequence::CanPossessObject(UObject& Object, UObject* InPlaybackContext) const
{
	if (InPlaybackContext == nullptr)
	{
		return false;
	}

	AActor* ActorContext = CastChecked<AActor>(InPlaybackContext);
	AActor* Actor = Cast<AActor>(&Object);
	if (Actor == nullptr)
	{
		if (auto Component = Cast<UActorComponent>(&Object))
		{
			Actor = Component->GetOwner();
		}
		if (Actor == nullptr)
		{
			Actor = Object.GetTypedOuter<AActor>();
		}
	}

	if (Actor != nullptr)
	{
		return Actor->GetLevel() == ActorContext->GetLevel()
			&& (Actor == ActorContext || Actor->IsAttachedTo(ActorContext))//only allow actor self or child actor
			;
	}

	return false;
}

void ULexUIPrefabSequence::LocateBoundObjects(const FGuid& ObjectId, UObject* Context, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	ObjectReferences.ResolveBinding(ObjectId, OutObjects);
}

UMovieScene* ULexUIPrefabSequence::GetMovieScene() const
{
	return MovieScene;
}

UObject* ULexUIPrefabSequence::GetParentObject(UObject* Object) const
{
	if (UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		return Component->GetOwner();
	}

	return nullptr;
}

void ULexUIPrefabSequence::UnbindPossessableObjects(const FGuid& ObjectId)
{
	ObjectReferences.RemoveBinding(ObjectId);
}

UObject* ULexUIPrefabSequence::CreateDirectorInstance(TSharedRef<const FSharedPlaybackState> SharedPlaybackState, FMovieSceneSequenceID SequenceID)
{
	if (auto ContextWidget = CastChecked<ULexWidget>(SharedPlaybackState->GetPlaybackContext()))
	{
		if (auto Comp = ContextWidget->GetComponent<ULexUIPrefabSequenceComponent>())
		{
			return Comp->GetSequenceBlueprintInstance();
		}
	}
	return nullptr;
}

#if WITH_EDITOR

ETrackSupport ULexUIPrefabSequence::IsTrackSupportedImpl(TSubclassOf<class UMovieSceneTrack> InTrackClass) const
{
	if (InTrackClass == UMovieSceneAudioTrack::StaticClass() ||
		InTrackClass == UMovieSceneEventTrack::StaticClass() ||
		InTrackClass == UMovieSceneMaterialParameterCollectionTrack::StaticClass())
	{
		return ETrackSupport::Supported;
	}

	return Super::IsTrackSupportedImpl(InTrackClass);
}

bool ULexUIPrefabSequence::IsObjectReferencesGood(ULexWidget* InContextActor)const
{
	return ObjectReferences.IsObjectReferencesGood(InContextActor);
}
bool ULexUIPrefabSequence::IsEditorHelpersGood(ULexWidget* InContextActor)const
{
	return ObjectReferences.IsEditorHelpersGood(InContextActor);
}
void ULexUIPrefabSequence::FixObjectReferences(ULexWidget* InContextActor)
{
	if (ObjectReferences.FixObjectReferences(InContextActor))
	{
		this->Modify();
	}
}
void ULexUIPrefabSequence::FixEditorHelpers(ULexWidget* InContextActor)
{
	if (ObjectReferences.FixEditorHelpers(InContextActor))
	{
		this->Modify();
	}
}
#endif