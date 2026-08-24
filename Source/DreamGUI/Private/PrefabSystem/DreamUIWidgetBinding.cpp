// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/DreamUIWidgetBinding.h"
#include "PrefabSystem/PrefabAnimation/DreamUISequence.h"
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
	// A widget as the context re-roots the whole binding: this is how a child binding resolves
	// against its parent's object (parent contexts are significant on UDreamUISequence), and how a
	// component-played sequence asset resolves against the widget hosting the player -- both
	// without any PresenterActor being set on the asset.
	UDreamWidget* Root = nullptr;
	if (UDreamWidget* ContextWidget = Cast<UDreamWidget>(ResolveParams.Context))
	{
		Root = ContextWidget;
	}
	else if (const UDreamWidgetPresenterComponentBase* Presenter = ResolvePresenter(ResolveParams.Context))
	{
		Root = Presenter->GetLoadedWidget();
	}
	if (Root == nullptr)
	{
		// The standalone asset editor: no presenter, no widget context, but the asset put up a
		// preview tree of its own prefab.
		if (const UDreamUISequence* OwningSequence = Cast<UDreamUISequence>(ResolveParams.Sequence.Get()))
		{
			Root = OwningSequence->GetPreviewRoot();
		}
	}
	if (UDreamWidget* Widget = ResolveWidgetPath(Root, WidgetPath))
	{
		if (SubObjectPathRelativeToWidget.IsEmpty())
		{
			Result.Objects.Add(Widget);
		}
		else if (UObject* SubObject = StaticFindObject(UObject::StaticClass(), Widget, *SubObjectPathRelativeToWidget))
		{
			Result.Objects.Add(SubObject);
		}
	}
	return Result;
}

bool UDreamUIWidgetBinding::SupportsBindingCreationFromObject(const UObject* SourceObject) const
{
	// Widgets only. The sequencer consults every custom binding type whenever any object is
	// possessed, and answering yes for a presenter-carrying actor would hijack a plain actor
	// possession into a widget binding. Actors and presenters stay supported for explicit
	// conversion (SupportsConversionFromBinding below).
	return SourceObject != nullptr && SourceObject->IsA<UDreamWidget>();
}

bool UDreamUIWidgetBinding::SupportsSourceObjectForConversion(const UObject* SourceObject) const
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
	return SubObjectPathRelativeToWidget.IsEmpty() ? UDreamWidget::StaticClass() : UObject::StaticClass();
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
	return SupportsSourceObjectForConversion(SourceObject);
}

UMovieSceneCustomBinding* UDreamUIWidgetBinding::CreateCustomBindingFromBinding(const FMovieSceneBindingReference& BindingReference, UObject* SourceObject, UMovieScene& OwnerMovieScene)
{
	return CreateNewCustomBinding(SourceObject, OwnerMovieScene);
}

#endif

#undef LOCTEXT_NAMESPACE
