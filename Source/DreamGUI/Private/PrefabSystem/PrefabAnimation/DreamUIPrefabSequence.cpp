// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequence.h"
#include "MovieScene.h"
#include "Core/Components/DreamWidget.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequenceComponent.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneMaterialParameterCollectionTrack.h"
#include "Tracks/MovieSceneTimeWarpTrack.h"
#include "PrefabSystem/PrefabAnimation/DreamUIAnimEventTrack.h"

#if WITH_EDITOR
UDreamUIPrefabSequence::FOnInitialize UDreamUIPrefabSequence::OnInitializeSequenceEvent;
#endif

static TAutoConsoleVariable<int32> CVarDefaultEvaluationType(
	TEXT("DreamGUIPrefabSequence.DefaultEvaluationType"),
	0,
	TEXT("0: Playback locked to playback frames\n1: Unlocked playback with sub frame interpolation"),
	ECVF_Default);

static TAutoConsoleVariable<FString> CVarDefaultTickResolution(
	TEXT("DreamGUIPrefabSequence.DefaultTickResolution"),
	TEXT("24000fps"),
	TEXT("Specifies default a tick resolution for newly created level sequences. Examples: 30 fps, 120/1 (120 fps), 30000/1001 (29.97), 0.01s (10ms)."),
	ECVF_Default);

static TAutoConsoleVariable<FString> CVarDefaultDisplayRate(
	TEXT("DreamGUIPrefabSequence.DefaultDisplayRate"),
	TEXT("30fps"),
	TEXT("Specifies default a display frame rate for newly created level sequences; also defines frame locked frame rate where sequences are set to be frame locked. Examples: 30 fps, 120/1 (120 fps), 30000/1001 (29.97), 0.01s (10ms)."),
	ECVF_Default);

UDreamUIPrefabSequence::UDreamUIPrefabSequence(const FObjectInitializer& ObjectInitializer)
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

bool UDreamUIPrefabSequence::IsEditable() const
{
	return true;
}

void UDreamUIPrefabSequence::PostInitProperties()
{
#if WITH_EDITOR && WITH_EDITORONLY_DATA

	// We do not run the default initialization for widget sequences that are CDOs, or that are going to be loaded (since they will have already been initialized in that case)
	EObjectFlags ExcludeFlags = RF_ClassDefaultObject | RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_WasLoaded;

	auto OwnerComponent = Cast<UDreamUIBehaviour>(GetOuter());
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

void UDreamUIPrefabSequence::BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context)
{
	FDreamUIPrefabSequenceObjectReference ObjectRef;
	auto Widget = Cast<UDreamWidget>(&PossessedObject);
	if (Widget == nullptr)
	{
		Widget = PossessedObject.GetTypedOuter<UDreamWidget>();
	}
	check(Widget != nullptr);
	// Relative to the CONTEXT -- the widget that owns this animation -- not to the widget being bound.
	// Passing the bound widget made every recorded path "/", meaning "the context widget itself", so
	// the path was never an identity for anything and playback had only the stored pointer to go on.
	UDreamWidget* ContextWidget = Cast<UDreamWidget>(Context);
	if (ContextWidget == nullptr)
	{
		ContextWidget = Widget;
	}
	if (FDreamUIPrefabSequenceObjectReference::CreateForObject(ContextWidget, &PossessedObject, ObjectRef))
	{
		ObjectReferences.CreateBinding(ObjectId, ObjectRef);
	}
}

bool UDreamUIPrefabSequence::CanPossessObject(UObject& Object, UObject* InPlaybackContext) const
{
	if (InPlaybackContext == nullptr)
	{
		return false;
	}

	auto ContextWidget = CastChecked<UDreamWidget>(InPlaybackContext);
	auto Widget = Cast<UDreamWidget>(&Object);
	if (Widget == nullptr)
	{
		Widget = Object.GetTypedOuter<UDreamWidget>();
	}

	if (Widget != nullptr)
	{
		return Widget->GetWorld() == ContextWidget->GetWorld()
			&& (Widget == ContextWidget || Widget->IsChildOf(ContextWidget))//only allow widget self or child widget
			;
	}

	return false;
}

void UDreamUIPrefabSequence::LocateBoundObjects(const FGuid& ObjectId,
	const UE::UniversalObjectLocator::FResolveParams& ResolveParams,
	TSharedPtr<const FSharedPlaybackState> SharedPlaybackState, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	// The CONTEXT first, and this is the whole of what makes animation work on a class. The stored
	// pointer names one particular widget; a class has one tree per instance, so resolving through it
	// makes every instance drive the authoring tree's widgets -- successfully, and invisibly, until
	// somebody notices one screen animating another's.
	if (UDreamWidget* ContextWidget = Cast<UDreamWidget>(ResolveParams.Context))
	{
		ObjectReferences.ResolveBindingInContext(ObjectId, ContextWidget, OutObjects);
		if (OutObjects.Num() > 0)
		{
			return;
		}
	}
	// No context, or nothing of that name under it: the pointer still identifies the widget while the
	// asset is being authored, which is when there is exactly one tree and it is the right one.
	ObjectReferences.ResolveBinding(ObjectId, OutObjects);
}

UMovieScene* UDreamUIPrefabSequence::GetMovieScene() const
{
	return MovieScene;
}

UObject* UDreamUIPrefabSequence::GetParentObject(UObject* Object) const
{
	return Object->GetTypedOuter<UDreamWidget>();
}

void UDreamUIPrefabSequence::UnbindPossessableObjects(const FGuid& ObjectId)
{
	ObjectReferences.RemoveBinding(ObjectId);
}

UObject* UDreamUIPrefabSequence::CreateDirectorInstance(TSharedRef<const FSharedPlaybackState> SharedPlaybackState, FMovieSceneSequenceID SequenceID)
{
	return nullptr;
}

#if WITH_EDITOR

ETrackSupport UDreamUIPrefabSequence::IsTrackSupportedImpl(TSubclassOf<class UMovieSceneTrack> InTrackClass) const
{
	if (InTrackClass == UMovieSceneAudioTrack::StaticClass() ||
		// InTrackClass == UMovieSceneEventTrack::StaticClass() ||
		InTrackClass == UMovieSceneMaterialParameterCollectionTrack::StaticClass() ||
		InTrackClass == UMovieSceneTimeWarpTrack::StaticClass() ||
		InTrackClass == UDreamUIAnimEventTrack::StaticClass())
	{
		return ETrackSupport::Supported;
	}

	return Super::IsTrackSupportedImpl(InTrackClass);
}

bool UDreamUIPrefabSequence::IsFilterSupportedImpl(const FString& InFilterName) const
{
	// The default answer of "every filter" fills the dropdown with Camera Cuts and Skeletal Mesh
	// entries nothing here can ever produce; this is UWidgetAnimation's list minus Event (the event
	// track is DreamUI's own broadcast track when it lands).
	static const TArray<FString> SupportedFilters = {
		TEXT("Audio"),
		TEXT("Keyed"),
		TEXT("Folder"),
		TEXT("Group"),
		TEXT("TimeDilation"),
		TEXT("TimeWarp"),
		TEXT("Unbound")
	};
	return SupportedFilters.Contains(InFilterName);
}

bool UDreamUIPrefabSequence::IsObjectReferencesGood(UDreamWidget* InContextWidget)const
{
	return ObjectReferences.IsObjectReferencesGood(InContextWidget);
}
void UDreamUIPrefabSequence::GetInvalidObjectBindingIds(UDreamWidget* InContextWidget, TArray<FGuid>& OutBindingIds) const
{
	ObjectReferences.GetInvalidBindingIds(InContextWidget, OutBindingIds);
}
bool UDreamUIPrefabSequence::HasObjectBindingCountMismatch() const
{
	return ObjectReferences.HasBindingCountMismatch();
}
bool UDreamUIPrefabSequence::IsEditorHelpersGood(UDreamWidget* InContextWidget)const
{
	return ObjectReferences.IsEditorHelpersGood(InContextWidget);
}
void UDreamUIPrefabSequence::FixObjectReferences(UDreamWidget* InContextWidget)
{
	// Modify() snapshots what the object holds right now, so it has to run before the repair.
	// Recording afterwards stores the already-repaired value as the thing to undo back to, which
	// is why the transaction around this used to come out empty.
	this->Modify();
	ObjectReferences.FixObjectReferences(InContextWidget);
}
void UDreamUIPrefabSequence::FixEditorHelpers(UDreamWidget* InContextWidget)
{
	this->Modify();
	ObjectReferences.FixEditorHelpers(InContextWidget);
}
#endif
