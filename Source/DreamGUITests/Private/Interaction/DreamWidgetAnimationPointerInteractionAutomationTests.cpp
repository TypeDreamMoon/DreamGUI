// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "MovieScene.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * A BUTTON CLICKED WHILE A WIDGET ANIMATION IS STILL CHANGING IT.
 *
 * A UMG widget animation writes its tracks into the widget every frame it plays, and the widget is hit-tested where the
 * frame drew it: a button whose width an animation is still growing is pressed on the part it has grown to by then, and
 * not on the part it has yet to grow into. DreamGUI's widget animations are movie-scene players ticked by the world's
 * Sequencer tick, which the rig's pump makes (DreamGUI.Driver.Pump.AWidgetAnimationAdvancesFrameForFrameInTheRigAsTheEnginesWorldTickAdvancesIt);
 * here one grows a button's width from 40 to 400 over a second, and a click aimed near the edge the button has reached is
 * made while it is still growing.
 */
namespace DreamWidgetAnimationPointerTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	constexpr int32 DisplayRate = 60;
	constexpr int32 TickResolution = 24000;
	constexpr int32 TicksPerFrame = TickResolution / DisplayRate;
	/** A second, keyed at both ends. */
	constexpr int32 LastKeyFrame = 60;
	constexpr float FromWidth = 40.0f;
	constexpr float ToWidth = 400.0f;

	/**
	 * A width ramp on InTarget played from InHost: an animation component on the host, a new animation on it, the target
	 * possessed under its display name and bound against the host, and a float track on AnimatableWidth, linear from
	 * FromWidth at frame 0 to ToWidth at LastKeyFrame -- the animation editor's own steps.
	 */
	UDreamWidgetAnimation* AuthorWidthRamp(UDreamWidget* InHost, UDreamWidget* InTarget, UDreamWidgetAnimationComponent*& OutAnimator)
	{
		OutAnimator = IsValid(InHost) ? InHost->AddComponent<UDreamWidgetAnimationComponent>() : nullptr;
		UDreamWidgetAnimation* Animation = OutAnimator != nullptr ? OutAnimator->AddNewAnimation() : nullptr;
		UMovieScene* MovieScene = Animation != nullptr ? Animation->GetMovieScene() : nullptr;
		if (MovieScene == nullptr || !IsValid(InTarget))
		{
			return nullptr;
		}
		MovieScene->SetTickResolutionDirectly(FFrameRate(TickResolution, 1));
		MovieScene->SetDisplayRate(FFrameRate(DisplayRate, 1));
		MovieScene->SetPlaybackRange(FFrameNumber(0), LastKeyFrame * TicksPerFrame);

		const FGuid TargetGuid = MovieScene->AddPossessable(InTarget->GetDisplayName(), UDreamWidget::StaticClass());
		Animation->BindPossessableObject(TargetGuid, *InTarget, InHost);

		UMovieSceneFloatTrack* Track = MovieScene->AddTrack<UMovieSceneFloatTrack>(TargetGuid);
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedButtonClickTest,
	"DreamGUI.Button.WhileAWidgetAnimationWidensItAClickNearTheEdgeItHasGrownToClicksItAndTheSamePixelBeforeDidNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedButtonClickTest, "DreamGUI.Button.WhileAWidgetAnimationWidensItAClickNearTheEdgeItHasGrownToClicksItAndTheSamePixelBeforeDidNot", "[Pointer][Animated]")

/*
 * Before the animation, a click well to the right of the 40-unit button lands beside it and clicks nothing. Played, the
 * width grows; once it is past halfway the click is aimed a few pixels inside the right edge the button has by the time the
 * step runs -- the same pixel as before, give or take, now on the button -- and it is clicked once while the animation is
 * still playing and the button not yet at its full width.
 */
bool FDreamAnimatedButtonClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPointerTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamWidget* Host = Rig.IsUsable() ? Rig.MakeWidget(TEXT("AnimatedHost"), nullptr, FVector2D(600.0, 300.0)) : nullptr;
	UDreamButton* Button = Host != nullptr ? Rig.MakeControl<UDreamButton>(TEXT("Grower"), Host, FVector2D(FromWidth, 60.0)) : nullptr;
	if (!TestTrue(TEXT("The rig, a host and a narrow button on it came up"), Button != nullptr))
	{
		return false;
	}
	Button->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
	UDreamWidgetAnimationComponent* Animator = nullptr;
	UDreamWidgetAnimation* Animation = AuthorWidthRamp(Host, Button, Animator);
	if (!TestNotNull(TEXT("A width ramp for the button was authored on the host"), Animation))
	{
		return false;
	}
	Rig.PumpFrames(2);
	FDreamElementRef Grower = Rig.Driver()->Find(FDreamBy::Widget(Button));
	const TOptional<FBox2D> Narrow = Grower->GetPixelRect();
	if (!TestTrue(TEXT("The narrow button is on the viewport"), Narrow.IsSet()))
	{
		return false;
	}
	// Beside the narrow button, where three quarters of the full width will reach.
	const FVector2D Beside(Narrow->GetCenter().X + 0.375 * ToWidth - 10.0, Narrow->GetCenter().Y);
	TestTrue(TEXT("A click beside the narrow button completes"), Rig.Driver()->Sequence().MoveToPixel(Beside).Press().Release().Perform());
	TestEqual(TEXT("...and clicks nothing of it"), Listener->ClickedCount, 0);
	// Away again and past the double-click time, so the next press is a press of its own.
	TestTrue(TEXT("Moving off and waiting completes"), Rig.Driver()->Sequence().MoveToPixel(FVector2D(10.0, 10.0)).WaitSeconds(1.0f).Perform());

	const FDreamUIAnimationHandle Handle = Animator->PlayAnimation(Animation);
	if (!TestTrue(TEXT("The animation is playing"), Handle.IsValid() && Animator->IsAnimationPlaying(Handle)))
	{
		return false;
	}
	const FWaitTimeout Limit = FWaitTimeout::InSeconds(2.0);
	const float ThreeQuarters = FromWidth + 0.75f * (ToWidth - FromWidth);
	float WidthAtThePress = 0.0f;
	bool bPlayingAtTheRelease = false;
	TestTrue(TEXT("Waiting for the button to grow past three quarters and clicking near its edge completes"),
		Rig.Driver()->Sequence()
			.Wait(FDreamUntil::Condition([Button, ThreeQuarters]() { return Button->GetWidth() >= ThreeQuarters; }, Limit), Limit,
				TEXT("the animation to widen the button past three quarters"))
			.MoveToResolvedPixel([Grower](FDreamDriverContext&) -> TOptional<FVector2D>
			{
				const TOptional<FBox2D> Now = Grower->GetPixelRect();
				return Now.IsSet() ? TOptional<FVector2D>(FVector2D(Now->Max.X - 8.0, Now->GetCenter().Y)) : TOptional<FVector2D>();
			}, TEXT("a few pixels inside the right edge the button has grown to"))
			.Then([Button, &WidthAtThePress](FDreamDriverContext&) { WidthAtThePress = Button->GetWidth(); })
			.Press()
			.Release()
			.Then([Animator, Handle, &bPlayingAtTheRelease](FDreamDriverContext&) { bPlayingAtTheRelease = Animator->IsAnimationPlaying(Handle); })
			.Perform());
	TestEqual(TEXT("The click near the edge the button had grown to clicked it, once"), Listener->ClickedCount, 1);
	TestTrue(FString::Printf(TEXT("...while it was still growing (%.1f units wide at the press)"), WidthAtThePress),
		WidthAtThePress < ToWidth - 1.0f);
	TestTrue(TEXT("...the animation still playing as it was let go"), bPlayingAtTheRelease);
	Animator->StopAllAnimations();
	return true;
}

#endif
