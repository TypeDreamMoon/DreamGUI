// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Animation/DreamUISequence.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "MovieScene.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace DreamStandaloneAnimationNameTestLocal
{
	struct FScopedTickWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);

		FScopedTickWorld()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		}

		~FScopedTickWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		void TickFrames(int32 Count)
		{
			for (int32 Index = 0; Index < Count; ++Index) World->Tick(LEVELTICK_TimeOnly, 1.0f / 30.0f);
		}
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamStandaloneAnimationNameTest,
	"DreamGUI.Animation.Playback.AStandaloneAssetPlaysByNameOnEveryOwningComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamStandaloneAnimationNameTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Asset on first component"));
	OutTestCommands.Add(TEXT("First"));
	OutBeautifiedNames.Add(TEXT("Asset on second component"));
	OutTestCommands.Add(TEXT("Second"));
}

bool FDreamStandaloneAnimationNameTest::RunTest(const FString& Parameters)
{
	using namespace DreamStandaloneAnimationNameTestLocal;
	FScopedTickWorld Scope;
	TStrongObjectPtr<UDreamUISequence> Asset(NewObject<UDreamUISequence>(GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UDreamUISequence::StaticClass(), TEXT("StandaloneIntro"))));
	UMovieScene* MovieScene = Asset->GetMovieScene();
	MovieScene->SetTickResolutionDirectly(FFrameRate(24000, 1));
	MovieScene->SetDisplayRate(FFrameRate(30, 1));
	MovieScene->SetPlaybackRange(FFrameNumber(0), 24000);
	const FGuid RootBinding = Asset->EnsureRootBinding();
	UMovieSceneFloatTrack* Track = MovieScene->AddTrack<UMovieSceneFloatTrack>(RootBinding);
	Track->SetPropertyNameAndPath(TEXT("AnimatableWidth"), TEXT("AnimatableWidth"));
	UMovieSceneFloatSection* Section = CastChecked<UMovieSceneFloatSection>(Track->CreateNewSection());
	Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(24000)));
	TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	Channels[0]->AddLinearKey(FFrameNumber(0), 20.0f);
	Channels[0]->AddLinearKey(FFrameNumber(12000), 220.0f);
	Track->AddSection(*Section);

	TStrongObjectPtr<UDreamWidgetTree> Archetype(NewObject<UDreamWidgetTree>(GetTransientPackage()));
	UDreamWidget* Root = Archetype->ConstructWidget<UDreamWidget>();
	Root->SetDisplayName(TEXT("Root"));
	Root->SetWidth(20.0f);
	Archetype->RootWidget = Root;
	UDreamWidgetAnimationComponent* First = Root->AddComponent<UDreamWidgetAnimationComponent>();
	UDreamWidgetAnimationComponent* Second = Root->AddComponent<UDreamWidgetAnimationComponent>();
	if (!TestNotNull(TEXT("the first animation component exists"), First)
		|| !TestNotNull(TEXT("the second animation component exists"), Second)) return false;
	UDreamWidgetAnimationComponent* AuthoredOwner = Parameters == TEXT("First") ? First : Second;
	const FArrayProperty* AssetsProperty = FindFProperty<FArrayProperty>(UDreamWidgetAnimationComponent::StaticClass(), TEXT("SequenceAssets"));
	if (!TestNotNull(TEXT("the component exposes its editable standalone asset list"), AssetsProperty)) return false;
	const FObjectPropertyBase* ElementProperty = CastField<FObjectPropertyBase>(AssetsProperty->Inner);
	if (!TestNotNull(TEXT("the asset list contains object references"), ElementProperty)) return false;
	FScriptArrayHelper Assets(AssetsProperty, AssetsProperty->ContainerPtrToValuePtr<void>(AuthoredOwner));
	ElementProperty->SetObjectPropertyValue(Assets.GetRawPtr(Assets.AddValue()), Asset.Get());

	TStrongObjectPtr<UDreamUserWidget> Instance(NewObject<UDreamUserWidget>(Scope.World));
	Instance->InitializeFromArchetype(Archetype.Get());
	RegisterDreamWidgetHierarchy(Instance.Get());
	ON_SCOPE_EXIT
	{
		if (IsValid(Instance.Get()))
		{
			Instance->StopAllAnimations();
			Instance->DestroyWidget();
		}
	};
	TArray<UDreamWidgetAnimationComponent*> Animators;
	Instance->CollectAnimationComponents(Animators);
	if (!TestEqual(TEXT("both authored animation components are instanced"), Animators.Num(), 2)) return false;
	const int32 OwnerIndex = Parameters == TEXT("First") ? 0 : 1;
	UDreamWidgetAnimationComponent* Owner = Animators[OwnerIndex];
	UDreamWidget* LiveRoot = Instance->GetContentRoot();
	if (!TestNotNull(TEXT("the real instance has an animation target"), LiveRoot)) return false;
	TestEqual(TEXT("the public name lookup finds the asset"), Instance->GetAnimationByName(Asset->GetName()), static_cast<UMovieSceneSequence*>(Asset.Get()));
	TestEqual(TEXT("the public owner lookup finds its actual component"), Instance->FindAnimationComponentFor(Asset.Get()), Owner);

	const FDreamUIAnimationHandle Direct = Instance->PlayAnimation(Asset.Get());
	if (!TestTrue(TEXT("playing the same asset directly starts a real instance"), Direct.IsValid())) return false;
	TestTrue(TEXT("direct playback belongs to the asset's component"), Owner->IsAnimationPlaying(Direct));
	Scope.TickFrames(8);
	TestTrue(TEXT("direct playback writes the target width after real world ticks"), LiveRoot->GetWidth() > 40.0f && LiveRoot->GetWidth() < 220.0f);
	Instance->StopAnimation(Direct);
	LiveRoot->SetWidth(20.0f);

	const FDreamUIAnimationHandle ByName = Instance->PlayAnimationByName(Asset->GetName());
	TestTrue(TEXT("playing by asset name also starts a real instance"), ByName.IsValid());
	TestTrue(TEXT("name-based playback uses the component that owns the asset"), Owner->IsAnimationPlaying(ByName));
	Scope.TickFrames(8);
	TestTrue(TEXT("name-based playback writes the same live target after world ticks"), LiveRoot->GetWidth() > 40.0f && LiveRoot->GetWidth() < 220.0f);
	Scope.TickFrames(30);
	TestFalse(TEXT("the name-based run finishes normally"), Owner->IsAnimationPlaying(ByName));
	TestEqual(TEXT("the completed name-based run retained its last key"), LiveRoot->GetWidth(), 220.0f, 0.01f);
	return true;
}

#endif
