// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Animation/DreamWidgetAnimationObjectReference.h"
#include "Animation/DreamWidgetAnimationPlayer.h"
#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Evaluation/MovieSceneEvaluationField.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "MovieSceneTrackEvaluationField.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneVectorSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneVectorTrack.h"

/*
 * Playback, end to end: a widget tree in a world that ticks, an animation bound to one of its
 * widgets, PlayAnimation, world ticks, and then the widget's property is read back.
 *
 * Every other animation test in this plugin stops short of the tick. The binding tests resolve
 * against an editor world that never advances, the sequence-player tests are the frame-by-frame
 * image players, and the editor's own scrubbing goes through FSequencer rather than the runtime
 * player. That gap is how a whole class of track could fail to write anything at runtime while
 * every existing test stayed green (2026-09-03: the FVector tracks -- every translate and scale
 * a widget has -- while float and rotator tracks on the same widget worked). These tests are
 * the oracle for that: one float track, one vector track, the same play, the same ticks.
 *
 * The tick is LEVELTICK_TimeOnly: it advances the world clock and broadcasts the sequence tick
 * -- the two things the movie-scene tick manager needs -- without ticking actors a test world
 * does not have. Keys are linear so the expected value at a frame is arithmetic.
 */
namespace DreamWidgetAnimationPlaybackTestLocal
{
	constexpr int32 FramesPerSecond = 30;
	constexpr int32 TicksPerFrame = 24000 / FramesPerSecond;
	constexpr int32 AnimationFrames = 20;
	constexpr int32 LastKeyFrame = 15;
	constexpr float FrameSeconds = 1.0f / FramesPerSecond;

	/**
	 * A game world WITH a world context. UWorld::Tick asks the engine about seamless travel
	 * through the world's context, and a world made by CreateWorld alone has none, so the first
	 * tick asserts; the other fixtures in this plugin never tick and never notice.
	 */
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
		}
		~FScopedGameWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	UDreamWidget* MakeWidget(UWorld* World, const TCHAR* DisplayName, UDreamWidget* Parent)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(DisplayName);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(40.0f);
		if (Parent != nullptr)
		{
			Widget->TrySetParent(Parent, false);
		}
		Widget->OnRegister();
		return Widget;
	}

	/** A root with one child, an animation component on the root, one animation bound to the child. */
	struct FScopedTree
	{
		UDreamWidget* Root = nullptr;
		UDreamWidget* Button = nullptr;
		UDreamWidgetAnimationComponent* Animator = nullptr;
		UDreamWidgetAnimation* Animation = nullptr;
		FGuid ButtonGuid;

		explicit FScopedTree(UWorld* World)
		{
			Root = MakeWidget(World, TEXT("Root"), nullptr);
			Root->SetWidth(400.0f);
			Root->SetHeight(300.0f);
			Button = MakeWidget(World, TEXT("ButtonA"), Root);

			Animator = Root->AddComponent<UDreamWidgetAnimationComponent>();
			Animation = Animator->AddNewAnimation();

			UMovieScene* MovieScene = Animation->GetMovieScene();
			MovieScene->SetTickResolutionDirectly(FFrameRate(24000, 1));
			MovieScene->SetDisplayRate(FFrameRate(FramesPerSecond, 1));
			MovieScene->SetPlaybackRange(FFrameNumber(0), AnimationFrames * TicksPerFrame);

			ButtonGuid = MovieScene->AddPossessable(TEXT("ButtonA"), UDreamWidget::StaticClass());
			// Bound against the ROOT, which is what the runtime resolves from: the component's widget.
			Animation->BindPossessableObject(ButtonGuid, *Button, Root);
		}

		~FScopedTree()
		{
			// The test is the owner. Instances stopped first so no player outlives its widget.
			if (IsValid(Animator))
			{
				Animator->StopAllAnimations();
			}
			if (IsValid(Root))
			{
				Root->DestroyWidget();
			}
		}

		FScopedTree(const FScopedTree&) = delete;
		FScopedTree& operator=(const FScopedTree&) = delete;

		/** A float track on the child, linear from `From` at frame 0 to `To` at the last key frame. */
		void AddFloatTrack(FName PropertyName, float From, float To)
		{
			UMovieScene* MovieScene = Animation->GetMovieScene();
			UMovieSceneFloatTrack* Track = MovieScene->AddTrack<UMovieSceneFloatTrack>(ButtonGuid);
			Track->SetPropertyNameAndPath(PropertyName, PropertyName.ToString());
			UMovieSceneFloatSection* Section = CastChecked<UMovieSceneFloatSection>(Track->CreateNewSection());
			Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(AnimationFrames * TicksPerFrame)));
			TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
			Channels[0]->AddLinearKey(FFrameNumber(0), From);
			Channels[0]->AddLinearKey(FFrameNumber(LastKeyFrame * TicksPerFrame), To);
			Track->AddSection(*Section);
		}

		/** A three-channel vector track on the child, linear from `From` at frame 0 to `To` at the last key frame. */
		void AddVectorTrack(FName PropertyName, const FVector& From, const FVector& To)
		{
			UMovieScene* MovieScene = Animation->GetMovieScene();
			UMovieSceneDoubleVectorTrack* Track = MovieScene->AddTrack<UMovieSceneDoubleVectorTrack>(ButtonGuid);
			Track->SetPropertyNameAndPath(PropertyName, PropertyName.ToString());
			Track->SetNumChannelsUsed(3);
			UMovieSceneDoubleVectorSection* Section = CastChecked<UMovieSceneDoubleVectorSection>(Track->CreateNewSection());
			Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(AnimationFrames * TicksPerFrame)));
			TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
			const double FromValues[3] = { From.X, From.Y, From.Z };
			const double ToValues[3] = { To.X, To.Y, To.Z };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				Channels[Index]->AddLinearKey(FFrameNumber(0), FromValues[Index]);
				Channels[Index]->AddLinearKey(FFrameNumber(LastKeyFrame * TicksPerFrame), ToValues[Index]);
			}
			Track->AddSection(*Section);
		}
	};

	/**
	 * The gallery's button, exactly as the designer authored it: a UDreamButton (a control, so a
	 * nested user widget) as the bound object, an open-ended section, auto-tangent keys, and only
	 * the channel that moves carrying two keys. Everything the plain fixture simplifies away.
	 */
	struct FScopedControlTree
	{
		UDreamWidget* Root = nullptr;
		UDreamButton* Button = nullptr;
		UDreamWidgetAnimationComponent* Animator = nullptr;
		UDreamWidgetAnimation* Animation = nullptr;
		FGuid ButtonGuid;

		explicit FScopedControlTree(UWorld* World)
		{
			Root = MakeWidget(World, TEXT("Root"), nullptr);
			Root->SetWidth(400.0f);
			Root->SetHeight(300.0f);

			Button = NewObject<UDreamButton>(World, NAME_None, RF_Public | RF_Transactional);
			Button->SetDisplayName(TEXT("ButtonA"));
			Button->Initialize();
			Button->SetWidth(100.0f);
			Button->SetHeight(40.0f);
			Button->TrySetParent(Root, false);
			Button->OnRegister();

			Animator = Root->AddComponent<UDreamWidgetAnimationComponent>();
			Animation = Animator->AddNewAnimation();

			UMovieScene* MovieScene = Animation->GetMovieScene();
			MovieScene->SetTickResolutionDirectly(FFrameRate(24000, 1));
			MovieScene->SetDisplayRate(FFrameRate(FramesPerSecond, 1));
			MovieScene->SetPlaybackRange(FFrameNumber(0), AnimationFrames * TicksPerFrame);

			ButtonGuid = MovieScene->AddPossessable(TEXT("ButtonA"), UDreamButton::StaticClass());
			Animation->BindPossessableObject(ButtonGuid, *Button, Root);

			UMovieSceneDoubleVectorTrack* Track = MovieScene->AddTrack<UMovieSceneDoubleVectorTrack>(ButtonGuid);
			Track->SetPropertyNameAndPath(TEXT("RenderTranslation"), TEXT("RenderTranslation"));
			Track->SetNumChannelsUsed(3);
			UMovieSceneDoubleVectorSection* Section = CastChecked<UMovieSceneDoubleVectorSection>(Track->CreateNewSection());
			Section->SetRange(TRange<FFrameNumber>::All());
			TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
			Channels[0]->AddCubicKey(FFrameNumber(0), 0.0);
			Channels[1]->AddCubicKey(FFrameNumber(0), -100.0);
			Channels[1]->AddCubicKey(FFrameNumber(LastKeyFrame * TicksPerFrame), 0.0);
			Channels[2]->AddCubicKey(FFrameNumber(0), 0.0);
			Track->AddSection(*Section);
		}

		~FScopedControlTree()
		{
			if (IsValid(Animator))
			{
				Animator->StopAllAnimations();
			}
			if (IsValid(Root))
			{
				Root->DestroyWidget();
			}
		}

		FScopedControlTree(const FScopedControlTree&) = delete;
		FScopedControlTree& operator=(const FScopedControlTree&) = delete;
	};

	void TickFrames(UWorld* World, int32 Frames)
	{
		for (int32 Index = 0; Index < Frames; ++Index)
		{
			World->Tick(LEVELTICK_TimeOnly, FrameSeconds);
		}
	}

	/** The linear ramp's value at a frame, for an expectation that reads like the key data. */
	float RampAt(float From, float To, int32 Frame)
	{
		return From + (To - From) * FMath::Clamp(static_cast<float>(Frame) / LastKeyFrame, 0.0f, 1.0f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationFloatTrackPlaybackTest,
	"DreamGUI.Animation.Playback.FloatTrackWritesThroughTicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationFloatTrackPlaybackTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
	Tree.Button->SetWidth(60.0f);

	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
	if (!TestTrue(TEXT("PlayAnimation hands back a live handle"), Handle.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("The instance reports playing"), Tree.Animator->IsAnimationPlaying(Handle));
	TestTrue(TEXT("The instance is findable by its animation"), Tree.Animator->FindAnimationInstance(Tree.Animation).Player == Handle.Player);

	TickFrames(Scope.World, 8);
	const float MidValue = Tree.Button->GetWidth();
	TestTrue(FString::Printf(TEXT("Eight frames in, the width is on the ramp (got %.2f)"), MidValue),
		MidValue > RampAt(20.0f, 220.0f, 4) && MidValue < RampAt(20.0f, 220.0f, 12));

	TickFrames(Scope.World, AnimationFrames + 5);
	TestEqual(TEXT("At the end the last key's value holds"), Tree.Button->GetWidth(), 220.0f, 0.01f);
	TestFalse(TEXT("A finished instance no longer reports playing"), Tree.Animator->IsAnimationPlaying(Handle));
	TestFalse(TEXT("A finished instance is released"), Tree.Animator->FindAnimationInstance(Tree.Animation).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationVectorTrackPlaybackTest,
	"DreamGUI.Animation.Playback.VectorTrackWritesThroughTicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationVectorTrackPlaybackTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	// The slide-in every gallery button animation is: off to the side, then home.
	Tree.AddVectorTrack(TEXT("RenderTranslation"), FVector(0.0, -100.0, 0.0), FVector::ZeroVector);
	// Parked somewhere neither key names, so "never written" and "written the rest value" differ.
	Tree.Button->SetRenderTranslation(FVector(0.0, 50.0, 0.0));

	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
	if (!TestTrue(TEXT("PlayAnimation hands back a live handle"), Handle.IsValid()))
	{
		return false;
	}

	TickFrames(Scope.World, 8);
	const double MidY = Tree.Button->GetRenderTranslation().Y;
	TestTrue(FString::Printf(TEXT("Eight frames in, Y is on the ramp (got %.2f)"), MidY),
		MidY > RampAt(-100.0f, 0.0f, 4) && MidY < RampAt(-100.0f, 0.0f, 12));

	TickFrames(Scope.World, AnimationFrames + 5);
	TestEqual(TEXT("At the end Y holds the last key"), Tree.Button->GetRenderTranslation().Y, 0.0, 0.01);
	TestFalse(TEXT("A finished instance no longer reports playing"), Tree.Animator->IsAnimationPlaying(Handle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationTimeRangeTest,
	"DreamGUI.Animation.Playback.TimeRangeEndsEarly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationTimeRangeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);

	// Stop a quarter second in: frame 7.5 of a 15-frame ramp, so well short of the last key.
	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimationTimeRange(Tree.Animation, 0.0f, 0.25f);
	if (!TestTrue(TEXT("PlayAnimationTimeRange hands back a live handle"), Handle.IsValid()))
	{
		return false;
	}
	TickFrames(Scope.World, AnimationFrames + 10);

	const float EndValue = Tree.Button->GetWidth();
	TestTrue(FString::Printf(TEXT("Playback stopped near the range end, not the animation's (got %.2f)"), EndValue),
		EndValue > RampAt(20.0f, 220.0f, 5) && EndValue < RampAt(20.0f, 220.0f, 11));
	TestFalse(TEXT("The instance finished at the range end"), Tree.Animator->IsAnimationPlaying(Handle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationReverseRelativeTest,
	"DreamGUI.Animation.Playback.PlayAnimationReverseTurnsTheRunningInstanceAround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationReverseRelativeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);

	const FDreamUIAnimationHandle Forward = Tree.Animator->PlayAnimationForward(Tree.Animation);
	if (!TestTrue(TEXT("PlayAnimationForward starts an instance"), Forward.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("It runs forward"), Tree.Animator->IsAnimationPlayingForward(Forward));
	TickFrames(Scope.World, 8);
	const float TurnValue = Tree.Button->GetWidth();

	const FDreamUIAnimationHandle Reverse = Tree.Animator->PlayAnimationReverse(Tree.Animation);
	TestTrue(TEXT("PlayAnimationReverse turns the SAME instance around rather than starting another"), Reverse.Player == Forward.Player);
	TestFalse(TEXT("It now runs backwards"), Tree.Animator->IsAnimationPlayingForward(Reverse));
	TickFrames(Scope.World, 4);
	const float BackValue = Tree.Button->GetWidth();
	TestTrue(FString::Printf(TEXT("Four frames later the value has come back down (%.2f -> %.2f)"), TurnValue, BackValue), BackValue < TurnValue);

	TickFrames(Scope.World, AnimationFrames);
	TestEqual(TEXT("Run back to the start, the first key's value holds"), Tree.Button->GetWidth(), 20.0f, 0.01f);
	TestFalse(TEXT("And the instance is done"), Tree.Animator->IsAnimationPlaying(Reverse));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationFinishedEventsTest,
	"DreamGUI.Animation.Playback.FinishedFiresOnNaturalEndAndOnStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationFinishedEventsTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);

	int32 StartedCount = 0;
	int32 FinishedCount = 0;
	UDreamWidgetAnimationPlayer* LastFinished = nullptr;
	Tree.Animator->OnInstanceStarted.AddLambda([&StartedCount](const FDreamUIAnimationHandle&) { ++StartedCount; });
	Tree.Animator->OnInstanceFinished.AddLambda([&FinishedCount, &LastFinished](const FDreamUIAnimationHandle& InHandle)
	{
		++FinishedCount;
		LastFinished = InHandle.Player;
	});

	const FDreamUIAnimationHandle First = Tree.Animator->PlayAnimation(Tree.Animation);
	TestEqual(TEXT("Started fires as the instance starts"), StartedCount, 1);
	TickFrames(Scope.World, AnimationFrames + 5);
	TestEqual(TEXT("Finished fires once at the natural end"), FinishedCount, 1);
	TestTrue(TEXT("For the instance that ended"), LastFinished == First.Player);

	const FDreamUIAnimationHandle Second = Tree.Animator->PlayAnimation(Tree.Animation);
	TickFrames(Scope.World, 3);
	Tree.Animator->StopAnimation(Second);
	TestEqual(TEXT("Stop counts as finishing, the way UMG's does"), FinishedCount, 2);
	TestTrue(TEXT("For the stopped instance"), LastFinished == Second.Player);
	TestFalse(TEXT("A stopped instance is not playing"), Tree.Animator->IsAnimationPlaying(Second));
	TestFalse(TEXT("Nothing is playing any more"), Tree.Animator->IsAnyAnimationPlaying());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationPauseSeekTest,
	"DreamGUI.Animation.Playback.PauseReportsTimeAndSeekMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationPauseSeekTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);

	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
	TickFrames(Scope.World, 6);
	const float PausedAt = Tree.Animator->PauseAnimation(Handle);
	TestTrue(TEXT("Pause reports where it paused"), PausedAt > 3.0f * FrameSeconds && PausedAt < 9.0f * FrameSeconds);
	TestTrue(TEXT("Paused reads as paused"), Tree.Animator->IsAnimationPaused(Handle));
	TestFalse(TEXT("And not as playing"), Tree.Animator->IsAnimationPlaying(Handle));
	TestTrue(TEXT("A paused instance is still a live one"), Tree.Animator->FindAnimationInstance(Tree.Animation).IsValid());

	const float PausedValue = Tree.Button->GetWidth();
	TickFrames(Scope.World, 5);
	TestEqual(TEXT("Ticks do not move a paused instance"), Tree.Button->GetWidth(), PausedValue, 0.01f);

	Tree.Animator->SetAnimationCurrentTime(Handle, 12.0f * FrameSeconds);
	Tree.Animator->FlushAnimations();
	TestEqual(TEXT("Seek reports the new time"), Tree.Animator->GetAnimationCurrentTime(Handle), 12.0f * FrameSeconds, 0.5f * FrameSeconds);

	Tree.Animator->ResumeAnimation(Handle);
	TestTrue(TEXT("Resume plays again"), Tree.Animator->IsAnimationPlaying(Handle));
	TickFrames(Scope.World, AnimationFrames);
	TestEqual(TEXT("And it runs to the end from there"), Tree.Button->GetWidth(), 220.0f, 0.01f);
	TestFalse(TEXT("Done"), Tree.Animator->IsAnimationPlaying(Handle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationControlVectorTrackTest,
	"DreamGUI.Animation.Playback.VectorTrackWritesOnAControlAsAuthored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationControlVectorTrackTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedControlTree Tree(Scope.World);
	Tree.Button->SetRenderTranslation(FVector(0.0, 50.0, 0.0));

	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
	if (!TestTrue(TEXT("PlayAnimation hands back a live handle"), Handle.IsValid()))
	{
		return false;
	}

	TickFrames(Scope.World, 8);
	const double MidY = Tree.Button->GetRenderTranslation().Y;
	TestTrue(FString::Printf(TEXT("Eight frames in, the control's Y is on its way home (got %.2f)"), MidY), MidY > -99.0 && MidY < -1.0);

	TickFrames(Scope.World, AnimationFrames + 5);
	TestEqual(TEXT("At the end Y holds the last key"), Tree.Button->GetRenderTranslation().Y, 0.0, 0.01);
	return true;
}

/*
 * The mechanism behind the gallery's dead slide-in, in the plugin's own terms: the same animation
 * reached by template instancing -- NewObject with an existing animation as the template, which
 * is exactly how a class hands its animations to each instance. Before the fix the copy's vector
 * section arrived with its keys and an EMPTY channel proxy, and the compiler, which counts a
 * property section's channels through that proxy before emitting an entity for it, emitted none.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationTemplateInstancingTest,
	"DreamGUI.Animation.Playback.VectorTrackSurvivesTemplateInstancing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationTemplateInstancingTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddVectorTrack(TEXT("RenderTranslation"), FVector(0.0, -100.0, 0.0), FVector::ZeroVector);

	// The instance's copy, made the way UDreamWidgetGeneratedClass makes one.
	UDreamWidgetAnimation* Copy = NewObject<UDreamWidgetAnimation>(Tree.Animator, NAME_None, RF_Transactional, Tree.Animation);
	if (!TestNotNull(TEXT("Template instancing produced a copy"), Copy))
	{
		return false;
	}
	TestTrue(TEXT("The copy's movie scene is its own"), Copy->GetMovieScene() != Tree.Animation->GetMovieScene());

	int32 VectorSections = 0;
	for (UMovieSceneSection* Section : Copy->GetMovieScene()->GetAllSections())
	{
		if (UMovieSceneDoubleVectorSection* VectorSection = Cast<UMovieSceneDoubleVectorSection>(Section))
		{
			++VectorSections;
			TestEqual(TEXT("The copy's vector section has a channel proxy with its three channels"), VectorSection->GetChannelProxy().NumChannels(), 3);
		}
	}
	TestEqual(TEXT("One vector section came across"), VectorSections, 1);

	Tree.Button->SetRenderTranslation(FVector(0.0, 50.0, 0.0));
	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Copy);
	if (!TestTrue(TEXT("The copy plays"), Handle.IsValid()))
	{
		return false;
	}
	TickFrames(Scope.World, 8);
	const double MidY = Tree.Button->GetRenderTranslation().Y;
	TestTrue(FString::Printf(TEXT("Eight frames in, the copy has moved the button (Y = %.2f)"), MidY), MidY > -99.0 && MidY < -1.0);
	TickFrames(Scope.World, AnimationFrames + 5);
	TestEqual(TEXT("At the end Y holds the last key"), Tree.Button->GetRenderTranslation().Y, 0.0, 0.01);
	TestFalse(TEXT("A finished handle reads as invalid"), Handle.IsValid());
	return true;
}

/*
 * Against the project's own gallery: the class the designer built, instanced the way the game
 * instances it, its authored animation played through the widget API -- the report that started
 * the 2026-09-03 review, verbatim. Skips itself when the asset is not there, so the plugin's
 * suite stays self-contained elsewhere.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationGalleryAssetTest,
	"DreamGUI.Animation.Playback.ProjectGallery.ButtonSlidesIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationGalleryAssetTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	UClass* GalleryClass = LoadClass<UDreamUserWidget>(nullptr, TEXT("/Game/UI/WBP_ControlsGallery.WBP_ControlsGallery_C"));
	if (GalleryClass == nullptr)
	{
		AddInfo(TEXT("No /Game/UI/WBP_ControlsGallery in this project; nothing to check."));
		return true;
	}

	FScopedGameWorld Scope;
	UDreamUserWidget* Instance = CreateDreamWidget(Scope.World, GalleryClass);
	if (!TestNotNull(TEXT("The gallery instantiates"), Instance))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		Instance->StopAllAnimations();
		Instance->DestroyWidget();
	};

	UDreamWidget* Root = Instance->GetContentRoot();
	UDreamWidgetAnimationComponent* Animator = IsValid(Root) ? Root->GetComponent<UDreamWidgetAnimationComponent>() : nullptr;
	if (!TestNotNull(TEXT("The content root carries the animation component"), Animator) || Animator->GetSequenceArray().Num() == 0)
	{
		return false;
	}
	UDreamWidgetAnimation* Animation = Animator->GetSequenceArray()[0];
	UDreamWidget* ButtonA = FDreamWidgetAnimationObjectReference::GetWidgetFromContextWidgetByRelativePath(Root, TEXT("Gallery/GalleryBody/Content/Body/ClickColumn/ButtonA"));
	if (!TestNotNull(TEXT("ButtonA is where the binding path says"), ButtonA))
	{
		return false;
	}

	const FDreamUIAnimationHandle Handle = Instance->PlayAnimation(Animation);
	if (!TestTrue(TEXT("PlayAnimation through the user widget hands back a live handle"), Handle.IsValid()))
	{
		return false;
	}
	TickFrames(Scope.World, 8);
	const FVector Mid = ButtonA->GetRenderTranslation();
	TestTrue(FString::Printf(TEXT("Eight frames in, ButtonA has left its rest pose (Y = %.2f)"), Mid.Y), Mid.Y < -1.0);
	TickFrames(Scope.World, AnimationFrames + 5);
	TestFalse(TEXT("The instance finished"), Instance->IsAnimationPlaying(Handle));
	return true;
}

#endif
