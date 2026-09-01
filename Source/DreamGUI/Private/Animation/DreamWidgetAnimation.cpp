// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Animation/DreamWidgetAnimation.h"
#include "MovieScene.h"
#include "Core/Components/DreamWidget.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneMaterialParameterCollectionTrack.h"
#include "Tracks/MovieSceneTimeWarpTrack.h"
#include "Animation/DreamUIAnimEventTrack.h"

#if WITH_EDITOR
UDreamWidgetAnimation::FOnInitialize UDreamWidgetAnimation::OnInitializeSequenceEvent;
#endif

static TAutoConsoleVariable<int32> CVarDefaultEvaluationType(
	TEXT("DreamWidgetAnimation.DefaultEvaluationType"),
	0,
	TEXT("0: Playback locked to playback frames\n1: Unlocked playback with sub frame interpolation"),
	ECVF_Default);

static TAutoConsoleVariable<FString> CVarDefaultTickResolution(
	TEXT("DreamWidgetAnimation.DefaultTickResolution"),
	TEXT("24000fps"),
	TEXT("Specifies default a tick resolution for newly created level sequences. Examples: 30 fps, 120/1 (120 fps), 30000/1001 (29.97), 0.01s (10ms)."),
	ECVF_Default);

static TAutoConsoleVariable<FString> CVarDefaultDisplayRate(
	TEXT("DreamWidgetAnimation.DefaultDisplayRate"),
	TEXT("30fps"),
	TEXT("Specifies default a display frame rate for newly created level sequences; also defines frame locked frame rate where sequences are set to be frame locked. Examples: 30 fps, 120/1 (120 fps), 30000/1001 (29.97), 0.01s (10ms)."),
	ECVF_Default);

UDreamWidgetAnimation::UDreamWidgetAnimation(const FObjectInitializer& ObjectInitializer)
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

bool UDreamWidgetAnimation::IsEditable() const
{
	return true;
}

void UDreamWidgetAnimation::PostInitProperties()
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

void UDreamWidgetAnimation::BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context)
{
	FDreamWidgetAnimationObjectReference ObjectRef;
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
	if (FDreamWidgetAnimationObjectReference::CreateForObject(ContextWidget, &PossessedObject, ObjectRef))
	{
		ObjectReferences.CreateBinding(ObjectId, ObjectRef);
	}
}

bool UDreamWidgetAnimation::CanPossessObject(UObject& Object, UObject* InPlaybackContext) const
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

void UDreamWidgetAnimation::LocateBoundObjects(const FGuid& ObjectId,
	const UE::UniversalObjectLocator::FResolveParams& ResolveParams,
	TSharedPtr<const FSharedPlaybackState> SharedPlaybackState, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	// The CONTEXT first, and this is the whole of what makes animation work on a class. The stored
	// pointer names one particular widget; a class has one tree per instance, so resolving through it
	// makes every instance drive the authoring tree's widgets -- successfully, and invisibly, until
	// somebody notices one screen animating another's.
	//
	// And when the context has nothing there, NOTHING is the answer. A context is an assertion of
	// scope: falling back to the stored pointer under someone else's context is the cross-tree leak
	// above wearing a different hat, and it defeated the animation editor's compile-window
	// suspension outright -- a suspension parks the playback context on a childless widget exactly
	// so every binding resolves to nothing and the entity runtime holds no object keys while
	// re-instancing rewrites the world (see SDreamWidgetAnimationEditorWidget).
	if (UDreamWidget* ContextWidget = Cast<UDreamWidget>(ResolveParams.Context))
	{
		ObjectReferences.ResolveBindingInContext(ObjectId, ContextWidget, OutObjects);
		return;
	}
	// No context at all: the pointer still identifies the widget while the asset is being authored,
	// which is when there is exactly one tree and it is the right one.
	ObjectReferences.ResolveBinding(ObjectId, OutObjects);
}

UMovieScene* UDreamWidgetAnimation::GetMovieScene() const
{
	return MovieScene;
}

UObject* UDreamWidgetAnimation::GetParentObject(UObject* Object) const
{
	// Sequencer uses this to OVERRIDE the binding context: whatever this returns is what
	// BindPossessableObject records the widget path against, and what a child binding resolves
	// through. A widget must answer nothing, so its context stays the playback context -- the
	// widget this animation lives on, the same widget the compiler validates against and the
	// runtime resolves from. Answering with the OUTER here handed every widget binding the
	// instance shell instead (a widget's outer chain skips its parent widgets), so every
	// recorded path gained a leading segment naming the animation owner itself, which no
	// resolver could walk: tracks compiled as errors and went unbound on the first rebuild.
	//
	// A sub-object (a component, a visual) does hang under its owning widget: parenting it
	// there is what lets it resolve relative to that widget's own binding.
	if (Cast<UDreamWidget>(Object) != nullptr)
	{
		return nullptr;
	}
	return Object->GetTypedOuter<UDreamWidget>();
}

void UDreamWidgetAnimation::UnbindPossessableObjects(const FGuid& ObjectId)
{
	ObjectReferences.RemoveBinding(ObjectId);
}

UObject* UDreamWidgetAnimation::CreateDirectorInstance(TSharedRef<const FSharedPlaybackState> SharedPlaybackState, FMovieSceneSequenceID SequenceID)
{
	return nullptr;
}

#if WITH_EDITOR

ETrackSupport UDreamWidgetAnimation::IsTrackSupportedImpl(TSubclassOf<class UMovieSceneTrack> InTrackClass) const
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

bool UDreamWidgetAnimation::IsFilterSupportedImpl(const FString& InFilterName) const
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

bool UDreamWidgetAnimation::IsObjectReferencesGood(UDreamWidget* InContextWidget)const
{
	return ObjectReferences.IsObjectReferencesGood(InContextWidget);
}
void UDreamWidgetAnimation::GetInvalidObjectBindingIds(UDreamWidget* InContextWidget, TArray<FGuid>& OutBindingIds) const
{
	ObjectReferences.GetInvalidBindingIds(InContextWidget, OutBindingIds);
}
void UDreamWidgetAnimation::GetUnresolvableBindingPaths(UDreamWidget* InContextWidget, TArray<TPair<FGuid, FString>>& OutBindings) const
{
	ObjectReferences.GetUnresolvableBindingPaths(InContextWidget, OutBindings);
}
int32 UDreamWidgetAnimation::RenameWidgetPathSegment(const FString& InOldSegment, const FString& InNewSegment)
{
	// Modify() before the edit, for the same reason FixObjectReferences says so: it snapshots what
	// the object holds NOW, and a snapshot taken afterwards records the repaired value as the state
	// to undo back to.
	this->Modify();
	const int32 ChangedCount = ObjectReferences.RenameWidgetPathSegment(InOldSegment, InNewSegment);
	if (ChangedCount == 0)
	{
		return 0;
	}

	// The track labels, on the movie scene rather than on the reference map -- a different object,
	// hence a second Modify(). Only the ones spelled exactly like the old id: a possessable's name is
	// free text the sequencer lets an author change, so rewriting one that says something else would
	// be discarding a label somebody chose. UMG's widget rename makes the same restriction.
	if (UMovieScene* Scene = GetMovieScene())
	{
		bool bRenamedALabel = false;
		for (int32 Index = 0; Index < Scene->GetPossessableCount(); ++Index)
		{
			FMovieScenePossessable& Possessable = Scene->GetPossessable(Index);
			if (Possessable.GetName() == InOldSegment)
			{
				if (!bRenamedALabel)
				{
					Scene->Modify();
					bRenamedALabel = true;
				}
				Possessable.SetName(InNewSegment);
			}
		}
	}
	return ChangedCount;
}
bool UDreamWidgetAnimation::HasObjectBindingCountMismatch() const
{
	return ObjectReferences.HasBindingCountMismatch();
}
bool UDreamWidgetAnimation::IsEditorHelpersGood(UDreamWidget* InContextWidget)const
{
	return ObjectReferences.IsEditorHelpersGood(InContextWidget);
}
void UDreamWidgetAnimation::FixObjectReferences(UDreamWidget* InContextWidget)
{
	// Modify() snapshots what the object holds right now, so it has to run before the repair.
	// Recording afterwards stores the already-repaired value as the thing to undo back to, which
	// is why the transaction around this used to come out empty.
	this->Modify();
	ObjectReferences.FixObjectReferences(InContextWidget);
}
void UDreamWidgetAnimation::FixEditorHelpers(UDreamWidget* InContextWidget)
{
	this->Modify();
	ObjectReferences.FixEditorHelpers(InContextWidget);
}
#endif
