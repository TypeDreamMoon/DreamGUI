// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Animation/DreamUISequence.h"
#include "Animation/DreamUIWidgetBinding.h"
#include "Animation/DreamUIAnimEventTrack.h"
#include "Animation/DreamUISequenceTrack.h"
#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"

#define LOCTEXT_NAMESPACE "DreamUISequence"

UDreamUISequence::UDreamUISequence(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	MovieScene = ObjInit.CreateDefaultSubobject<UMovieScene>(this, "MovieScene");
	MovieScene->SetFlags(RF_Transactional);
	MovieScene->SetDisplayRate(FFrameRate(30, 1));
	// Children resolve against the root's resolved object, which is what lets the subsequence
	// override re-root the whole tree by redirecting the root binding alone.
	bParentContextsAreSignificant = true;
}

bool UDreamUISequence::CanPossessObject(UObject& Object, UObject* InPlaybackContext) const
{
	return Object.IsA<UDreamWidget>() || Object.IsA<UDreamWidgetPresenterComponentBase>();
}

void UDreamUISequence::BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context)
{
	UDreamUIWidgetBinding* Binding = Cast<UDreamUIWidgetBinding>(
		GetMutableDefault<UDreamUIWidgetBinding>()->CreateNewCustomBinding(&PossessedObject, *MovieScene));
	if (Binding != nullptr)
	{
		BindingReferences.AddBinding(ObjectId, Binding);
	}
}

void UDreamUISequence::UnbindPossessableObjects(const FGuid& ObjectId)
{
	BindingReferences.RemoveBinding(ObjectId);
}

void UDreamUISequence::UnbindObjects(const FGuid& ObjectId, const TArray<UObject*>& InObjects, UObject* Context)
{
	BindingReferences.RemoveObjects(ObjectId, InObjects, Context);
}

void UDreamUISequence::UnbindInvalidObjects(const FGuid& ObjectId, UObject* Context)
{
	// FMovieSceneBindingReferences::RemoveInvalidObjects judges a reference by resolving its
	// universal object LOCATOR. Every binding in this sequence is a custom binding, and a custom
	// binding is stored with an empty locator, so that verdict came back "invalid" for all of them --
	// unconditionally, whether or not the widget path resolves. "Remove Missing Objects" therefore
	// deleted perfectly good bindings, and with them the widget path that is the only record of which
	// widget the track was for. A path is not a pointer: it resolves against whichever tree it is
	// played against, and "nothing resolves it at this instant" usually means no preview tree is up.
	bool bHoldsWidgetBinding = false;
	for (const FMovieSceneBindingReference& Reference : BindingReferences.GetReferences(ObjectId))
	{
		if (Cast<UDreamUIWidgetBinding>(Reference.CustomBinding.Get()) != nullptr)
		{
			bHoldsWidgetBinding = true;
			break;
		}
	}
	if (bHoldsWidgetBinding)
	{
		UE_LOG(DreamGUI, Warning,
			TEXT("'%s': binding %s was left alone. It is a DreamUI widget path, which resolves against the tree it is played against rather than against a locator, so it cannot be judged missing from here. Repoint or delete the track instead."),
			*GetPathName(), *ObjectId.ToString(EGuidFormats::DigitsWithHyphens));
		return;
	}
	BindingReferences.RemoveInvalidObjects(ObjectId, Context);
}

UObject* UDreamUISequence::GetParentObject(UObject* Object) const
{
	// Parenting every widget under the tree root is what lets the root's resolution re-root the
	// children: with parent contexts significant, a child resolves against the root's object.
	if (UDreamWidget* Widget = Cast<UDreamWidget>(Object))
	{
		UDreamWidget* Root = Widget;
		while (Root->GetParent() != nullptr)
		{
			Root = Root->GetParent();
		}
		return Root != Widget ? Root : nullptr;
	}
	return nullptr;
}

FGuid UDreamUISequence::EnsureRootBinding()
{
	if (RootBindingGuid.IsValid() && MovieScene->FindPossessable(RootBindingGuid) != nullptr)
	{
		return RootBindingGuid;
	}
	RootBindingGuid = AddWidgetBinding(FString(), TEXT("Root"));
	return RootBindingGuid;
}

FGuid UDreamUISequence::AddWidgetBinding(const FString& InWidgetPath, const FString& InDisplayName)
{
	const FGuid Guid = MovieScene->AddPossessable(InDisplayName, UDreamWidget::StaticClass());
	UDreamUIWidgetBinding* Binding = NewObject<UDreamUIWidgetBinding>(MovieScene, NAME_None, RF_Transactional);
	Binding->WidgetPath = InWidgetPath;
	BindingReferences.AddBinding(Guid, Binding);
	if (!InWidgetPath.IsEmpty() && RootBindingGuid.IsValid())
	{
		if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Guid))
		{
			Possessable->SetParent(RootBindingGuid, MovieScene);
		}
	}
	return Guid;
}

#if WITH_EDITOR
int32 UDreamUISequence::RenameWidgetPathSegments(const FString& InOldSegment, const FString& InNewSegment)
{
	int32 Renamed = 0;
	for (const FMovieSceneBindingReference& Reference : BindingReferences.GetAllReferences())
	{
		UDreamUIWidgetBinding* WidgetBinding = Cast<UDreamUIWidgetBinding>(Reference.CustomBinding.Get());
		if (WidgetBinding == nullptr)
		{
			continue;
		}
		// The binding Modify()s itself before writing, and it is outered to this sequence's
		// MovieScene, so the package dirties exactly when something changed and not before.
		Renamed += WidgetBinding->RenameWidgetPathSegment(InOldSegment, InNewSegment) ? 1 : 0;
	}
	return Renamed;
}


FText UDreamUISequence::GetDisplayName() const
{
	return FText::FromName(GetFName());
}

ETrackSupport UDreamUISequence::IsTrackSupportedImpl(TSubclassOf<UMovieSceneTrack> InTrackClass) const
{
	// The DreamUI event track, as on the embedded form: an asset played by a sequence component
	// reaches that component through the playback context, which is what its sections fire through.
	// And the sub-track, so one asset can be assembled out of others.
	if (InTrackClass == UDreamUIAnimEventTrack::StaticClass() ||
		InTrackClass == UDreamUISequenceTrack::StaticClass())
	{
		return ETrackSupport::Supported;
	}
	// Property tracks come through the default path; this mirrors the embedded sequence's opt-ins.
	return Super::IsTrackSupportedImpl(InTrackClass);
}

#endif

#undef LOCTEXT_NAMESPACE
