// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Animation/DreamUISequence.h"
#include "Animation/DreamUIWidgetBinding.h"
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

FText UDreamUISequence::GetDisplayName() const
{
	return FText::FromName(GetFName());
}

ETrackSupport UDreamUISequence::IsTrackSupportedImpl(TSubclassOf<UMovieSceneTrack> InTrackClass) const
{
	// Property tracks come through the default path; this mirrors the embedded sequence's opt-ins.
	return Super::IsTrackSupportedImpl(InTrackClass);
}

#endif

#undef LOCTEXT_NAMESPACE
