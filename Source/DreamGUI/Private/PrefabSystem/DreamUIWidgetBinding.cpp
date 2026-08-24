// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/DreamUIWidgetBinding.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "MovieScene.h"
#include "MovieSceneBindingReferences.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "DreamUIWidgetBinding"

FString UDreamUIWidgetBinding::BuildWidgetPathFromRoot(const UDreamWidget* Root, const UDreamWidget* Widget)
{
	TArray<FString> Segments;
	const UDreamWidget* Walker = Widget;
	while (Walker != nullptr && Walker != Root)
	{
		Segments.Insert(Walker->GetDisplayName(), 0);
		Walker = Walker->GetParent();
	}
	// Widget was not under Root at all: an empty path (the root itself) is the honest fallback.
	return Walker == Root ? FString::Join(Segments, TEXT("/")) : FString();
}

UDreamWidget* UDreamUIWidgetBinding::ResolveWidgetPath(UDreamWidget* Root, const FString& InPath)
{
	if (Root == nullptr || InPath.IsEmpty())
	{
		return Root;
	}
	TArray<FString> Segments;
	InPath.ParseIntoArray(Segments, TEXT("/"));
	UDreamWidget* Walker = Root;
	for (const FString& Segment : Segments)
	{
		UDreamWidget* Next = nullptr;
		for (UDreamWidget* Child : Walker->GetChildren())
		{
			// First display-name match wins; keep sibling names unique for stable bindings.
			if (IsValid(Child) && Child->GetDisplayName() == Segment)
			{
				Next = Child;
				break;
			}
		}
		if (Next == nullptr)
		{
			return nullptr;
		}
		Walker = Next;
	}
	return Walker;
}

UDreamWidgetPresenterComponentBase* UDreamUIWidgetBinding::ResolvePresenter(UObject* Context) const
{
	UWorld* ContextWorld = Context != nullptr ? Context->GetWorld() : nullptr;
	if (ContextWorld == nullptr)
	{
		return nullptr;
	}
	FSoftObjectPath ActorPath = PresenterActor.ToSoftObjectPath();
	if (ActorPath.IsNull())
	{
		return nullptr;
	}
	// The stored path names the editor world's actor; a PIE (or otherwise duplicated) world holds a
	// renamed copy, so remap before resolving or the editor-world original answers instead.
	if (ContextWorld->GetOutermost()->GetPIEInstanceID() != INDEX_NONE)
	{
		ActorPath.FixupForPIE(ContextWorld->GetOutermost()->GetPIEInstanceID());
	}
	AActor* Actor = Cast<AActor>(ActorPath.ResolveObject());
	if (Actor == nullptr || Actor->GetWorld() != ContextWorld)
	{
		return nullptr;
	}
	return Actor->FindComponentByClass<UDreamWidgetPresenterComponentBase>();
}

FMovieSceneBindingResolveResult UDreamUIWidgetBinding::ResolveBinding(const FMovieSceneBindingResolveParams& ResolveParams, int32 BindingIndex, TSharedRef<const UE::MovieScene::FSharedPlaybackState> SharedPlaybackState) const
{
	FMovieSceneBindingResolveResult Result;
	if (const UDreamWidgetPresenterComponentBase* Presenter = ResolvePresenter(ResolveParams.Context))
	{
		if (UDreamWidget* Widget = ResolveWidgetPath(Presenter->GetLoadedWidget(), WidgetPath))
		{
			Result.Objects.Add(Widget);
		}
	}
	return Result;
}

bool UDreamUIWidgetBinding::SupportsBindingCreationFromObject(const UObject* SourceObject) const
{
	if (SourceObject == nullptr)
	{
		return false;
	}
	if (SourceObject->IsA<UDreamWidget>() || SourceObject->IsA<UDreamWidgetPresenterComponentBase>())
	{
		return true;
	}
	const AActor* Actor = Cast<AActor>(SourceObject);
	return Actor != nullptr && Actor->FindComponentByClass<UDreamWidgetPresenterComponentBase>() != nullptr;
}

void UDreamUIWidgetBinding::InitializeFromObject(UObject* SourceObject)
{
	if (const UDreamWidget* Widget = Cast<UDreamWidget>(SourceObject))
	{
		// Walk up to the loaded root, then find whichever presenter loaded it.
		const UDreamWidget* Root = Widget;
		while (Root->GetParent() != nullptr)
		{
			Root = Root->GetParent();
		}
		for (TObjectIterator<UDreamWidgetPresenterComponentBase> It; It; ++It)
		{
			if (It->GetLoadedWidget() == Root)
			{
				PresenterActor = It->GetOwner();
				break;
			}
		}
		WidgetPath = BuildWidgetPathFromRoot(Root, Widget);
	}
	else if (const UDreamWidgetPresenterComponentBase* Presenter = Cast<UDreamWidgetPresenterComponentBase>(SourceObject))
	{
		PresenterActor = Presenter->GetOwner();
		WidgetPath.Reset();
	}
	else if (const AActor* Actor = Cast<AActor>(SourceObject))
	{
		PresenterActor = const_cast<AActor*>(Actor);
		WidgetPath.Reset();
	}
}

UMovieSceneCustomBinding* UDreamUIWidgetBinding::CreateNewCustomBinding(UObject* SourceObject, UMovieScene& OwnerMovieScene)
{
	const FName BindingName = MakeUniqueObjectName(&OwnerMovieScene, UDreamUIWidgetBinding::StaticClass(),
		SourceObject != nullptr ? FName(*(SourceObject->GetFName().ToString() + TEXT("_WidgetBinding"))) : FName(TEXT("DreamUIWidgetBinding")));
	UDreamUIWidgetBinding* NewBinding = NewObject<UDreamUIWidgetBinding>(&OwnerMovieScene, UDreamUIWidgetBinding::StaticClass(), BindingName, RF_Transactional);
	NewBinding->InitializeFromObject(SourceObject);
	return NewBinding;
}

UClass* UDreamUIWidgetBinding::GetBoundObjectClass() const
{
	return UDreamWidget::StaticClass();
}

FString UDreamUIWidgetBinding::GetDesiredBindingName() const
{
	if (!WidgetPath.IsEmpty())
	{
		FString Last;
		WidgetPath.Split(TEXT("/"), nullptr, &Last, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		return Last.IsEmpty() ? WidgetPath : Last;
	}
	const FSoftObjectPath ActorPath = PresenterActor.ToSoftObjectPath();
	return ActorPath.IsNull() ? FString() : ActorPath.GetSubPathString();
}

#if WITH_EDITOR

FText UDreamUIWidgetBinding::GetBindingTypePrettyName() const
{
	return LOCTEXT("BindingTypePrettyName", "DreamUI Widget");
}

bool UDreamUIWidgetBinding::SupportsConversionFromBinding(const FMovieSceneBindingReference& BindingReference, const UObject* SourceObject) const
{
	return SupportsBindingCreationFromObject(SourceObject);
}

UMovieSceneCustomBinding* UDreamUIWidgetBinding::CreateCustomBindingFromBinding(const FMovieSceneBindingReference& BindingReference, UObject* SourceObject, UMovieScene& OwnerMovieScene)
{
	return CreateNewCustomBinding(SourceObject, OwnerMovieScene);
}

#endif

#undef LOCTEXT_NAMESPACE
