// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Animation/DreamWidgetAnimation.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "Generators/MovieSceneEasingFunction.h"
#include "UObject/UObjectGlobals.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "DreamGUI.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneMaterialParameterCollectionTrack.h"
#include "Tracks/MovieSceneTimeWarpTrack.h"
#include "Animation/DreamUIAnimEventTrack.h"

#if WITH_EDITOR
UDreamWidgetAnimation::FOnInitialize UDreamWidgetAnimation::OnInitializeSequenceEvent;
#endif

namespace DreamWidgetAnimationLocal
{
	/**
	 * Aim one of a section's two easing functions at an object that section OWNS.
	 *
	 * The same hole the empty channel proxy came out of, one property along. An instance of a widget
	 * class gets its animations by TEMPLATE INSTANCING, and FObjectInstancingGraph re-points only
	 * FObjectProperty members carrying CPF_InstancedReference. FMovieSceneEasingSettings::EaseIn is a
	 * TScriptInterface -- an FInterfaceProperty -- so it is copied verbatim: the new section arrives
	 * with an EaseIn still naming the TEMPLATE's "EaseInFunction", which lives inside the authoring
	 * tree the rebuild was supposed to replace.
	 *
	 * Two costs, and the second is the one that was visible. Editing the ease writes the template's
	 * object rather than this section's; and the pointer keeps the whole old tree reachable, so a
	 * .dui rebuilt N times leaves N authoring trees in the package -- WBP_ControlsGallery had
	 * accumulated DreamWidgetTree_0/1/2/7 by the time anyone counted.
	 *
	 * The section's OWN default subobject is preferred over a duplicate, and that is not a
	 * micro-optimisation: UMovieSceneSection's constructor makes "EaseInFunction"/"EaseOutFunction"
	 * with ObjectInitializer::CreateDefaultSubobject, so on a template instantiation the engine has
	 * ALREADY created it here with the archetype's one as its archetype -- values and all. Re-aiming
	 * is the whole fix, and it leaves no second object behind. Duplicating is the fallback for a
	 * section whose easing function is not one of those two subobjects.
	 */
	void ReHomeEasingFunction(UMovieSceneSection* InSection, TScriptInterface<IMovieSceneEasingFunction>& InOutEasing)
	{
		UObject* Source = InOutEasing.GetObject();
		if (Source == nullptr || Source->IsIn(InSection))
		{
			return;
		}

		// ...Safe, not the plain one: the plain StaticFindObjectFast is fatal if it is ever reached
		// while a package is saving or the object hash is locked for GC, and this runs from
		// PostInitProperties, which is a hook neither of those states is supposed to reach but
		// neither of them announces either. The Safe variant answers null instead, and null here
		// falls through to the duplicate.
		UObject* Local = StaticFindObjectFastSafe(Source->GetClass(), InSection, Source->GetFName());
		if (Local == nullptr)
		{
			// An explicit unique name rather than NAME_None: the section usually already holds an
			// object under the source's name, and asking the duplicator for that name again is how a
			// re-home turns into a name collision.
			Local = DuplicateObject<UObject>(Source, InSection,
				MakeUniqueObjectName(InSection, Source->GetClass(), Source->GetFName()));
		}
		if (Local == nullptr)
		{
			return;
		}

		InOutEasing.SetObject(Local);
		InOutEasing.SetInterface(Cast<IMovieSceneEasingFunction>(Local));
	}
}

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

	// An instance of a class gets its animation by template instancing -- NewObject with the
	// archetype's animation as template -- which copies every section's key data and NOTHING else.
	// A section's channel proxy is not a property; the engine rebuilds it on Serialize, on
	// PostEditImport and, for the vector sections, on SetChannelsUsed, none of which runs here.
	// So a vector section arrives with its keys and an EMPTY proxy, and the compiler, which asks
	// the proxy how many channels a property section has before it emits an entity for it, emits
	// none: every translate and scale track on an instanced widget was silently dead while every
	// float and rotator track (whose proxies are built in their constructors) played fine.
	// PostEditImport is the engine's own "rebuild your proxy" hook and is harmless on the rest.
	// Loading is excluded because Serialize does this itself, and the CDO has nothing to play.
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad | RF_WasLoaded))
	{
		if (MovieScene != nullptr)
		{
			for (UMovieSceneSection* Section : MovieScene->GetAllSections())
			{
				if (IsValid(Section))
				{
					// UMovieSceneSection re-declares the hook protected; UObject's is public and the
					// dispatch is virtual, so the base pointer reaches the section's own override.
					static_cast<UObject*>(Section)->PostEditImport();
					// The other half of the same instancing gap: the proxy is not a property at all, and
					// these two ARE properties that the instancing graph does not follow. See
					// DreamWidgetAnimationLocal::ReHomeEasingFunction.
					DreamWidgetAnimationLocal::ReHomeEasingFunction(Section, Section->Easing.EaseIn);
					DreamWidgetAnimationLocal::ReHomeEasingFunction(Section, Section->Easing.EaseOut);
				}
			}
		}

		// The third property the instancing graph leaves pointing at the template, and the one that
		// outlives the other two. FDreamWidgetAnimationObjectReference::HelperWidget is a plain
		// TObjectPtr<UDreamWidget>, so an instanced animation arrives holding the CLASS TEMPLATE's
		// widget. Playback itself survives that -- LocateBoundObjects resolves by PATH against its
		// context and never reads the pointer -- but the pointer is a strong reference from a live
		// instance into the authoring tree, and it is what keeps a rebuilt .dui's superseded trees
		// reachable, the same accumulation ReHomeEasingFunction describes above.
		//
		// CLEARED HERE, NOT RE-RESOLVED, because there is nothing yet to resolve against: this runs
		// from inside NewObject while the tree is still being instanced, and
		// UDreamWidgetGeneratedClass::InitializeWidgetStatic does not link Parent or finish the
		// hierarchy until its step 2, after that NewObject returns. Walking a path down from the
		// context widget now would meet a half-built tree and answer wrongly, which is worse than
		// answering nothing: the path is the binding and survives untouched, and the pointer is
		// refilled at the first resolve that arrives with a live tree (RebindObjectReferencesOnce).
		//
		// Keyed on the widget TREE rather than on the widget, and reached through the outer chain
		// rather than through Parent, because outers exist from the moment an object is constructed
		// and Parent does not.
		if (const UDreamWidgetTree* OwnTree = GetTypedOuter<UDreamWidgetTree>())
		{
			const int32 DetachedCount = ObjectReferences.DetachHelpersOutsideTree(OwnTree);
			UE_CLOG(DetachedCount > 0, DreamGUI, Verbose,
				TEXT("[%s].%d Animation '%s' dropped %d binding pointer(s) that named another widget tree; the recorded paths are unchanged."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName(), DetachedCount);
		}
	}
}

void UDreamWidgetAnimation::RebindObjectReferencesOnce(UDreamWidget* InContextWidget) const
{
	if (bObjectReferencesRebound)
	{
		return;
	}
	// ONLY this animation's own widget, and this guard is load-bearing rather than tidy. The animation
	// editor scrubs the AUTHORED animation against a PREVIEW instance, and parks it on a childless
	// sentinel widget for the length of an evacuation (see SDreamWidgetAnimationEditorWidget); both
	// arrive here as a context that is not this animation's owner. Re-homing an asset's stored
	// pointers at either would be an edit nobody asked for -- and against the sentinel, under which
	// every path resolves to nothing on purpose, it would be one that erased them.
	if (InContextWidget == nullptr || InContextWidget != GetTypedOuter<UDreamWidget>())
	{
		return;
	}
	bObjectReferencesRebound = true;

	TArray<FString> UnresolvedPaths;
	const int32 ReboundCount = ObjectReferences.RebindHelpersToContext(InContextWidget, UnresolvedPaths);
	UE_CLOG(ReboundCount > 0, DreamGUI, Verbose,
		TEXT("[%s].%d Animation '%s' re-pointed %d binding(s) at its own widget tree."),
		ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName(), ReboundCount);
	for (const FString& Path : UnresolvedPaths)
	{
		// Warning, not Verbose: this binding will find nothing when the animation plays, and the
		// recorded path is the only thing that can say which widget was meant.
		UE_LOG(DreamGUI, Warning,
			TEXT("[%s].%d Animation '%s' has a binding whose path '%s' names no widget under '%s'; that track will not play."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName(), *Path, *InContextWidget->GetPathDisplayName());
	}
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
		// The first resolve is also the first moment a live hierarchy exists to resolve against, so it
		// is where the pointers PostInitProperties had to drop get refilled. Resolution below does not
		// need them -- it walks the path -- but Resolve(), the no-context fallback, and the editor's
		// IsObjectReferenceGood all read the pointer, and on an instance it must name the instance.
		RebindObjectReferencesOnce(ContextWidget);
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
