// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MovieScene.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "WaitUntil.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * SEQUENCER ANIMATIONS UNDER THE HEADLESS PUMP.
 *
 * A widget animation (UDreamWidgetAnimationComponent) is played by a movie-scene player that the world's
 * UMovieSceneSequenceTickManager ticks -- through DreamGUI's animation ticker, or directly under
 * DreamUI.Animation.Ticker 0 -- and the tick manager is ticked by UWorld::Tick's Sequencer broadcast, at the top of
 * the frame. The rig's pump stands in for UWorld::Tick and makes that broadcast in the same place
 * (FDreamDriverContext::PumpOneFrame). These hold it to the engine: the same animation, played in a rig and in a
 * world the engine's own UWorld::Tick advances, is at the same value after every frame, halfway along its curve at
 * half its length and on its last key at the end; and in a play session, frame by frame, it is where its curve says
 * for the time it reports, to the same last key.
 */
namespace DreamDriverSequencerPumpTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	/** One display frame per pumped frame, so the curve's frames are the rig's frames. */
	constexpr int32 DisplayRate = 60;
	constexpr int32 TickResolution = 24000;
	constexpr int32 TicksPerFrame = TickResolution / DisplayRate;
	/** Half a second, keyed at both ends. */
	constexpr int32 LastKeyFrame = 30;
	constexpr float LengthSeconds = static_cast<float>(LastKeyFrame) / DisplayRate;
	constexpr float FromWidth = 20.0f;
	constexpr float ToWidth = 220.0f;
	/** How far the width moves in one display frame: the most two honest readings of the curve can differ by. */
	constexpr float OneFrameOfWidth = (ToWidth - FromWidth) / LastKeyFrame;
	constexpr float FrameSeconds = 1.0f / DisplayRate;

	/** The width the curve gives at InSeconds into the play. */
	float RampAt(float InSeconds)
	{
		return FromWidth + (ToWidth - FromWidth) * FMath::Clamp(InSeconds / LengthSeconds, 0.0f, 1.0f);
	}

	/**
	 * A width ramp on InBar, played from InHost: an animation component on the host, a new animation on it, the bar
	 * possessed under its display name and bound against the host (what the runtime resolves from), and a float track on
	 * AnimatableWidth, linear from FromWidth at frame 0 to ToWidth at LastKeyFrame. The animation editor's own steps, as
	 * the playback tests take them.
	 */
	UDreamWidgetAnimation* AuthorWidthRamp(UDreamWidget* InHost, UDreamWidget* InBar, UDreamWidgetAnimationComponent*& OutAnimator)
	{
		OutAnimator = IsValid(InHost) ? InHost->AddComponent<UDreamWidgetAnimationComponent>() : nullptr;
		UDreamWidgetAnimation* Animation = OutAnimator != nullptr ? OutAnimator->AddNewAnimation() : nullptr;
		UMovieScene* MovieScene = Animation != nullptr ? Animation->GetMovieScene() : nullptr;
		if (MovieScene == nullptr || !IsValid(InBar))
		{
			return nullptr;
		}
		MovieScene->SetTickResolutionDirectly(FFrameRate(TickResolution, 1));
		MovieScene->SetDisplayRate(FFrameRate(DisplayRate, 1));
		MovieScene->SetPlaybackRange(FFrameNumber(0), LastKeyFrame * TicksPerFrame);

		const FGuid BarGuid = MovieScene->AddPossessable(InBar->GetDisplayName(), UDreamWidget::StaticClass());
		Animation->BindPossessableObject(BarGuid, *InBar, InHost);

		UMovieSceneFloatTrack* Track = MovieScene->AddTrack<UMovieSceneFloatTrack>(BarGuid);
		Track->SetPropertyNameAndPath(TEXT("AnimatableWidth"), TEXT("AnimatableWidth"));
		UMovieSceneFloatSection* Section = CastChecked<UMovieSceneFloatSection>(Track->CreateNewSection());
		Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(LastKeyFrame * TicksPerFrame)));
		TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		if (Channels.Num() < 1)
		{
			return nullptr;
		}
		Channels[0]->AddLinearKey(FFrameNumber(0), FromWidth);
		Channels[0]->AddLinearKey(FFrameNumber(LastKeyFrame * TicksPerFrame), ToWidth);
		Track->AddSection(*Section);
		return Animation;
	}

	/**
	 * A world the engine's own UWorld::Tick advances. With a world context, because UWorld::Tick asks the engine about
	 * seamless travel through the world's context; LEVELTICK_TimeOnly, because the clock and the Sequencer broadcast are
	 * the two things the tick manager reads, and a world with no actors has nothing else to tick. The playback tests'
	 * oracle (DreamWidgetAnimationPlaybackAutomationTests.cpp), held here as the one the rig is compared with.
	 */
	struct FEngineTickedWorld
	{
		UWorld* World = nullptr;

		FEngineTickedWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
		}

		~FEngineTickedWorld()
		{
			if (World != nullptr)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}

		UE_NONCOPYABLE(FEngineTickedWorld);

		void TickFrame()
		{
			World->Tick(LEVELTICK_TimeOnly, FrameSeconds);
		}
	};

	/** A widget made the way the playback tests make one in a world that is not a rig's: parent, then register. */
	UDreamWidget* MakeEngineWidget(UWorld* InWorld, const TCHAR* InName, UDreamWidget* InParent, const FVector2D& InSize)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(InName);
		Widget->SetWidth(InSize.X);
		Widget->SetHeight(InSize.Y);
		if (InParent != nullptr)
		{
			Widget->TrySetParent(InParent, false);
		}
		Widget->OnRegister();
		return Widget;
	}

	/** The time the playing instance of InAnimation reports, or unset once nothing is playing. */
	TOptional<float> CurrentTimeOf(const UDreamWidgetAnimationComponent* InAnimator, UDreamWidgetAnimation* InAnimation)
	{
		if (InAnimator == nullptr || InAnimation == nullptr)
		{
			return TOptional<float>();
		}
		const FDreamUIAnimationHandle Handle = InAnimator->FindAnimationInstance(InAnimation);
		if (!Handle.IsValid() || !InAnimator->IsAnimationPlaying(Handle))
		{
			return TOptional<float>();
		}
		return InAnimator->GetAnimationCurrentTime(Handle);
	}
}

/**
 * The pump plays a widget animation the way the engine's world tick does. The same ramp, played on the same frame in a
 * rig and in a world UWorld::Tick advances, is compared after every frame of the play and after it: equal throughout,
 * halfway along the curve at half its length, on the last key once it is over, and finished in both. Run both ways
 * DreamGUI ticks its players -- through its animation ticker (DreamUI.Animation.Ticker 1, the default) and registered
 * with the world's tick manager directly (0) -- because the pump makes the world's broadcast, and both are under it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverSequencerPumpTest,
	"DreamGUI.Driver.Pump.AWidgetAnimationAdvancesFrameForFrameInTheRigAsTheEnginesWorldTickAdvancesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverSequencerPumpTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverSequencerPumpTestLocal;
	for (const int32 TickerSetting : { 1, 0 })
	{
		const FString Road = TickerSetting != 0 ? TEXT("through DreamGUI's animation ticker") : TEXT("registered with the world's tick manager");
		const auto Say = [&Road](const TCHAR* InWhat) { return FString::Printf(TEXT("[%s] %s"), *Road, InWhat); };
		const DreamTests::Lifecycle::FScopedConsoleVariable Ticker(TEXT("DreamUI.Animation.Ticker"), TickerSetting);

		FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
		Rig.BindTest(this);
		if (!TestTrue(Say(TEXT("The rig came up")), Rig.IsUsable()))
		{
			continue;
		}
		UDreamWidget* RigHost = Rig.MakeWidget(TEXT("AnimatedHost"), nullptr, FVector2D(400.0, 300.0));
		UDreamWidget* RigBar = Rig.MakeWidget(TEXT("AnimatedBar"), RigHost, FVector2D(60.0, 40.0));
		UDreamWidgetAnimationComponent* RigAnimator = nullptr;
		UDreamWidgetAnimation* RigAnimation = AuthorWidthRamp(RigHost, RigBar, RigAnimator);

		FEngineTickedWorld Engine;
		UDreamWidget* EngineHost = MakeEngineWidget(Engine.World, TEXT("AnimatedHost"), nullptr, FVector2D(400.0, 300.0));
		UDreamWidget* EngineBar = MakeEngineWidget(Engine.World, TEXT("AnimatedBar"), EngineHost, FVector2D(60.0, 40.0));
		UDreamWidgetAnimationComponent* EngineAnimator = nullptr;
		UDreamWidgetAnimation* EngineAnimation = AuthorWidthRamp(EngineHost, EngineBar, EngineAnimator);
		if (!TestTrue(Say(TEXT("The same width ramp is authored in the rig and in the engine-ticked world")),
			RigAnimation != nullptr && EngineAnimation != nullptr))
		{
			if (IsValid(EngineHost))
			{
				EngineHost->DestroyWidget();
			}
			continue;
		}
		Rig.PumpFrames(1);

		const FDreamUIAnimationHandle RigHandle = RigAnimator->PlayAnimation(RigAnimation);
		const FDreamUIAnimationHandle EngineHandle = EngineAnimator->PlayAnimation(EngineAnimation);
		TestTrue(Say(TEXT("The rig's play is live")), RigHandle.IsValid() && RigAnimator->IsAnimationPlaying(RigHandle));
		TestTrue(Say(TEXT("...and so is the engine world's")), EngineHandle.IsValid() && EngineAnimator->IsAnimationPlaying(EngineHandle));

		// Frame by frame, the rig's frame and the engine's: the first frame they disagree on is the one reported.
		constexpr int32 HalfwayFrame = LastKeyFrame / 2;
		constexpr int32 FramesToRun = LastKeyFrame + 10;
		int32 FirstDisagreement = INDEX_NONE;
		float RigAtDisagreement = 0.0f;
		float EngineAtDisagreement = 0.0f;
		float RigAtHalfway = 0.0f;
		float EngineAtHalfway = 0.0f;
		float LargestOffCurve = 0.0f;
		for (int32 Frame = 1; Frame <= FramesToRun; ++Frame)
		{
			Rig.PumpFrames(1);
			Engine.TickFrame();
			const float RigWidth = RigBar->GetWidth();
			const float EngineWidth = EngineBar->GetWidth();
			if (FirstDisagreement == INDEX_NONE && !FMath::IsNearlyEqual(RigWidth, EngineWidth, 0.01f))
			{
				FirstDisagreement = Frame;
				RigAtDisagreement = RigWidth;
				EngineAtDisagreement = EngineWidth;
			}
			if (Frame == HalfwayFrame)
			{
				RigAtHalfway = RigWidth;
				EngineAtHalfway = EngineWidth;
			}
			// And where the curve says for the time the rig's own player reports.
			if (const TOptional<float> Now = CurrentTimeOf(RigAnimator, RigAnimation); Now.IsSet())
			{
				LargestOffCurve = FMath::Max(LargestOffCurve, FMath::Abs(RigWidth - RampAt(Now.GetValue())));
			}
		}

		TestEqual(*Say(*FString::Printf(TEXT("The rig and the engine's world agree on every frame (first disagreement at frame %d: rig %.2f, engine %.2f)"),
			FirstDisagreement, RigAtDisagreement, EngineAtDisagreement)), FirstDisagreement, static_cast<int32>(INDEX_NONE));
		TestTrue(Say(*FString::Printf(TEXT("Halfway through its length the width is halfway along the curve, within a frame of it (got %.2f)"), RigAtHalfway)),
			FMath::IsNearlyEqual(RigAtHalfway, RampAt(LengthSeconds * 0.5f), OneFrameOfWidth + 0.01f));
		TestTrue(Say(*FString::Printf(TEXT("...and not already at either end (got %.2f)"), RigAtHalfway)),
			RigAtHalfway > FromWidth + 0.5f && RigAtHalfway < ToWidth - 0.5f);
		TestEqual(Say(TEXT("...where the engine's world has it too")), RigAtHalfway, EngineAtHalfway, 0.01f);
		TestTrue(Say(*FString::Printf(TEXT("While it plays the width is where the curve says for the time the player reports (off by at most %.3f)"), LargestOffCurve)),
			LargestOffCurve <= 0.05f);
		TestEqual(Say(TEXT("At the end the rig's bar holds the last key's width")), RigBar->GetWidth(), ToWidth, 0.01f);
		TestEqual(Say(TEXT("...as the engine world's does")), EngineBar->GetWidth(), ToWidth, 0.01f);
		TestFalse(Say(TEXT("The rig's play has finished")), RigAnimator->IsAnimationPlaying(RigHandle));
		TestFalse(Say(TEXT("...and so has the engine world's")), EngineAnimator->IsAnimationPlaying(EngineHandle));

		EngineAnimator->StopAllAnimations();
		EngineHost->DestroyWidget();
		RigAnimator->StopAllAnimations();
	}
	return true;
}

/**
 * The same ramp in a play session, where the engine's frames are the pump: frame after frame the width is where the
 * curve says for the time the player reports, it passes the middle of the curve on the way, and it ends on the last
 * key -- the reading the rig's test above gives, from engine frames whose lengths are whatever they were.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieSequencerTest,
	"DreamGUI.Pie.AWidgetAnimationPlaysAlongItsCurveInEngineFramesToItsLastKeyAsTheRigsPumpPlaysIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPieSequencerTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverSequencerPumpTestLocal;

	/** What the frames saw, carried between them. Play-world objects only weakly, as the PIE rig asks. */
	struct FSeen
	{
		TWeakObjectPtr<UDreamWidgetAnimationComponent> Animator;
		TWeakObjectPtr<UDreamWidgetAnimation> Animation;
		bool bStarted = false;
		int32 FramesPlaying = 0;
		float LargestOffCurve = 0.0f;
		bool bSawTheMiddle = false;
	};
	const TSharedRef<FSeen> Seen = MakeShared<FSeen>();

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Seen](FDreamDriverPieRig& InRig)
	{
		UDreamWidget* Host = InRig.MakeWidget(TEXT("AnimatedHost"), nullptr, FVector2D(400.0, 300.0));
		UDreamWidget* Bar = InRig.MakeWidget(TEXT("AnimatedBar"), Host, FVector2D(60.0, 40.0));
		UDreamWidgetAnimationComponent* Animator = nullptr;
		UDreamWidgetAnimation* Animation = AuthorWidthRamp(Host, Bar, Animator);
		if (Animation != nullptr && Animator != nullptr)
		{
			Seen->Animator = Animator;
			Seen->Animation = Animation;
			Seen->bStarted = Animator->PlayAnimation(Animation).IsValid();
		}
	});

	const auto BarWidth = [Rig]() -> TOptional<float>
	{
		const UDreamWidget* Bar = Rig->FindMade(TEXT("AnimatedBar"));
		return Bar != nullptr ? TOptional<float>(Bar->GetWidth()) : TOptional<float>();
	};

	Rig->Sequence()
		.Then([this, Seen](FDreamDriverContext&)
		{
			TestTrue(TEXT("The ramp is authored on a widget in the play session and its play is live"), Seen->bStarted);
		})
		// Every engine frame of the play, read once: the width against the curve at the reported time.
		.Wait(FDreamUntil::Condition([Seen, BarWidth]() -> bool
			{
				const TOptional<float> Now = CurrentTimeOf(Seen->Animator.Get(), Seen->Animation.Get());
				const TOptional<float> Width = BarWidth();
				if (!Now.IsSet() || !Width.IsSet())
				{
					// Over -- or never started, which the step before says.
					return true;
				}
				++Seen->FramesPlaying;
				Seen->LargestOffCurve = FMath::Max(Seen->LargestOffCurve, FMath::Abs(Width.GetValue() - RampAt(Now.GetValue())));
				if (Now.GetValue() > LengthSeconds * 0.25f && Now.GetValue() < LengthSeconds * 0.75f)
				{
					Seen->bSawTheMiddle = true;
				}
				return false;
			}, FWaitTimeout::InSeconds(3.0)),
			FWaitTimeout::InSeconds(4.0), TEXT("the play to reach its end"))
		.Then([this, Seen, BarWidth](FDreamDriverContext&)
		{
			TestTrue(*FString::Printf(TEXT("It played over several engine frames rather than in one (%d)"), Seen->FramesPlaying), Seen->FramesPlaying > 2);
			TestTrue(*FString::Printf(TEXT("On every one the width was where the curve says for the reported time (off by at most %.3f)"), Seen->LargestOffCurve),
				Seen->LargestOffCurve <= 0.05f);
			TestTrue(TEXT("It went through the middle of its curve on the way"), Seen->bSawTheMiddle);
			const TOptional<float> Width = BarWidth();
			TestTrue(*FString::Printf(TEXT("At the end the bar holds the last key's width (got %.2f)"), Width.Get(-1.0f)),
				Width.IsSet() && FMath::IsNearlyEqual(Width.GetValue(), ToWidth, 0.01f));
		})
		.PerformLatent();
	Rig->Finish();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
