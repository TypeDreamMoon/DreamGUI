// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Camera/CameraTypes.h"
#include "Controls/DreamButton.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "DreamTweenManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Extensions/DreamGameViewportClient.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Interaction/UIButton.h"
#include "Interaction/UITextInput.h"
#include "Misc/App.h"
#include "SceneView.h"
#include "UObject/StrongObjectPtr.h"
#include "WaitUntil.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverUntil.h"
#include "Driver/DreamDriverVirtualCamera.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * THE PLAY-IN-EDITOR SMOKE LAYER: a handful of gestures in a real play session, each chosen because
 * it proves one thing the headless rig cannot.
 *
 * The headless rig imitates a game -- a player controller spawned by hand, a viewport size handed to
 * the canvas, a pump that ticks what it knows to tick. Here nothing is imitated: the engine's GameMode
 * logs the player in, the local player owns a viewport, the viewport client is the one a project
 * configures, and every frame is an engine frame (FDreamDriverSequence::PerformLatent, one step per
 * frame). So these are few, and each says in its comment which part of the real thing it is about:
 *
 *  - a click through the input actor and the player controller's input stack;
 *  - characters through the game viewport client, the road the platform takes;
 *  - a default button's colour transition, run by tweens the GameInstance ticks;
 *  - a world-space button hit by the production raycaster through the local player's projection;
 *  - the same gesture giving the same events here as on the headless rig, both of its input hosts.
 *
 * Every wait is a FDreamUntil condition rather than a count of frames: an engine frame is as long as
 * the machine makes it, and a count that is right at 60 Hz is wrong at 10.
 */
namespace DreamDriverPieTestLocal
{
	/** What a condition is given before it reports that it gave up, and the step's own backstop behind it. */
	FWaitTimeout ConditionLimit()
	{
		return FWaitTimeout::InSeconds(2.0);
	}

	FWaitTimeout StepLimit()
	{
		return FWaitTimeout::InSeconds(3.0);
	}

	/** The button the pass-over comparisons make, on both rigs. */
	const FVector2D PassButtonSize(200.0, 60.0);

	/**
	 * Sideways off that button from its centre: half its width reaches its edge, a quarter more is clearly
	 * past it. Measured from the button rather than a fixed distance because the viewport is whatever the
	 * session came up with -- a real-RHI session plays in the level editor's viewport as it is laid out,
	 * which can be narrower than a centred button plus a few hundred pixels, and a pointer sent outside
	 * the viewport is not a gesture a player can make.
	 */
	const FVector2D OffTheButton(0.75 * PassButtonSize.X, 0.0);

	/** One pass over a button, as the UMG button reports it: arrive, press, release (and so click), leave. */
	const TArray<FName>& ExpectedPassLog()
	{
		static const TArray<FName> Expected = {
			FName(TEXT("Hovered")), FName(TEXT("Pressed")), FName(TEXT("Released")), FName(TEXT("Clicked")), FName(TEXT("Unhovered")) };
		return Expected;
	}

	FString DescribeLog(const TArray<FName>& InLog)
	{
		return InLog.Num() == 0
			? FString(TEXT("(nothing)"))
			: FString::JoinBy(InLog, TEXT(", "), [](const FName& InEntry) { return InEntry.ToString(); });
	}

	/** Every one of the button's five events to the listener, so an assertion can see all of them. */
	void BindButton(UDreamButton* InButton, UDreamPressInteractionListener* InListener)
	{
		if (InButton == nullptr || InListener == nullptr)
		{
			return;
		}
		InButton->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
		InButton->OnPressed.AddDynamic(InListener, &UDreamPressInteractionListener::HandlePressed);
		InButton->OnReleased.AddDynamic(InListener, &UDreamPressInteractionListener::HandleReleased);
		InButton->OnHovered.AddDynamic(InListener, &UDreamPressInteractionListener::HandleHovered);
		InButton->OnUnhovered.AddDynamic(InListener, &UDreamPressInteractionListener::HandleUnhovered);
	}

	/**
	 * The one gesture two of the tests below compare across rigs, written once so it is literally the
	 * same list of steps on both: onto the button, press, release, off again -- each followed by a wait
	 * for the event it should produce, so the list takes as many frames as the pump in question needs
	 * and no assumption about how many that is.
	 */
	void AddPassOverButton(FDreamDriverSequence& InSteps, const FDreamLocatorRef& InButton,
		const TStrongObjectPtr<UDreamPressInteractionListener>& InListener)
	{
		const TStrongObjectPtr<UDreamPressInteractionListener> Listener = InListener;
		InSteps.MoveTo(InButton)
			.Wait(FDreamUntil::Condition([Listener]() { return Listener->HoveredCount > 0; }, ConditionLimit()),
				StepLimit(), TEXT("the pointer's arrival to hover the button"))
			.Press()
			.Wait(FDreamUntil::Condition([Listener]() { return Listener->PressedCount > 0; }, ConditionLimit()),
				StepLimit(), TEXT("the press to reach the button"))
			.Release()
			.Wait(FDreamUntil::Condition([Listener]() { return Listener->ClickedCount > 0; }, ConditionLimit()),
				StepLimit(), TEXT("the release to click the button"))
			.MoveBy(OffTheButton)
			.Wait(FDreamUntil::Condition([Listener]() { return Listener->UnhoveredCount > 0; }, ConditionLimit()),
				StepLimit(), TEXT("the pointer's departure to unhover the button"));
	}

	/** The face a UDreamButton tints between its states: its selectable's transition target. */
	UDreamVisual* TintedFace(UDreamButton* InButton)
	{
		return InButton != nullptr && InButton->ButtonBehaviour != nullptr ? InButton->ButtonBehaviour->GetTransitionTarget() : nullptr;
	}

	/**
	 * Queue the in-play half of a comparison: a fresh session, the same button, the same pass, and at
	 * the end the in-play event log held against InReferenceLog, which InReferenceName produced.
	 */
	void QueuePassInPlayAndCompare(FAutomationTestBase& InTest, const TArray<FName>& InReferenceLog, const FString& InReferenceName)
	{
		FAutomationTestBase* Test = &InTest;
		const TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
		TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(InTest);
		Rig->Start();
		Rig->WhenReady([Listener](FDreamDriverPieRig& InRig)
		{
			BindButton(InRig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, PassButtonSize), Listener.Get());
		});
		FDreamDriverSequence Steps = Rig->Sequence();
		AddPassOverButton(Steps, FDreamBy::Name(TEXT("Play")), Listener);
		Steps.Then([Test, Listener, InReferenceLog, InReferenceName](FDreamDriverContext&)
		{
			const TArray<FName>& PlayLog = Listener->Log;
			Test->TestTrue(FString::Printf(
				TEXT("In play the pass produces exactly the events %s produced, in the same order (in play: %s; %s: %s)"),
				*InReferenceName, *DescribeLog(PlayLog), *InReferenceName, *DescribeLog(InReferenceLog)),
				PlayLog == InReferenceLog);
		});
		Steps.PerformLatent();
		Rig->Finish();
	}
}

/*
 * A click, all the way from the player controller.
 *
 * The press and the release go in through APlayerController::InputKey, as the viewport client feeds
 * a real mouse button to it; the controller's own tick processes them, the input actor's binding on
 * its input stack hands them to the module, and the event system dispatches -- the whole road a game
 * takes, which the headless rig's default arrangement skips by feeding the module directly. That the
 * press really travelled it is asserted where only it would show: the left mouse button is down in
 * the player controller's own input while it is held, and up again after.
 *
 * Counts and order are SButton's: one press, one release, one click, press before release before
 * click.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieClickTest,
	"DreamGUI.Pie.ClickingAButtonGoesThroughThePlayerControllerAndClicksItOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPieClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPieTestLocal;
	const TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Listener](FDreamDriverPieRig& InRig)
	{
		BindButton(InRig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 60.0)), Listener.Get());
	});
	Rig->Sequence()
		.Then([this](FDreamDriverContext& InContext)
		{
			// The arrangement the claim rests on: the event system that will dispatch is the input
			// actor's own, not one the rig hung somewhere else.
			TestTrue(TEXT("The event system is the input actor's own"),
				InContext.InputActor != nullptr && InContext.EventSystem != nullptr && InContext.EventSystem->GetOwner() == InContext.InputActor);
		})
		.MoveTo(FDreamBy::Name(TEXT("Play")))
		.Press()
		.Wait(FDreamUntil::Condition([Listener]() { return Listener->PressedCount > 0; }, ConditionLimit()),
			StepLimit(), TEXT("the press to reach the button"))
		.Then([this](FDreamDriverContext& InContext)
		{
			TestTrue(TEXT("While it is held, the left mouse button is down in the player controller's own input"),
				InContext.PlayerController != nullptr && InContext.PlayerController->IsInputKeyDown(EKeys::LeftMouseButton));
		})
		.Release()
		.Wait(FDreamUntil::Condition([Listener]() { return Listener->ClickedCount > 0; }, ConditionLimit()),
			StepLimit(), TEXT("the release to click the button"))
		.Then([this, Listener](FDreamDriverContext& InContext)
		{
			TestFalse(TEXT("After the release the left mouse button is up in the player controller's input"),
				InContext.PlayerController != nullptr && InContext.PlayerController->IsInputKeyDown(EKeys::LeftMouseButton));
			TestEqual(TEXT("One press"), Listener->PressedCount, 1);
			TestEqual(TEXT("One release"), Listener->ReleasedCount, 1);
			TestEqual(TEXT("One click"), Listener->ClickedCount, 1);
			const TArray<FName> Expected = { FName(TEXT("Pressed")), FName(TEXT("Released")), FName(TEXT("Clicked")) };
			TestTrue(FString::Printf(TEXT("Press, then release, then click -- SButton's order (got %s)"), *DescribeLog(Listener->Log)),
				Listener->LogOnly(Expected) == Expected);
		})
		.PerformLatent();
	Rig->Finish();
	return true;
}

/*
 * Characters, the way the platform delivers them in a game.
 *
 * Not through UUITextInput::HandleCharacterInput, which is what every headless typing test calls: each
 * character goes in where the platform puts a typed character, FSlateApplication::ProcessKeyCharEvent,
 * and from there the engine takes it -- along Slate's keyboard focus to the play session's viewport
 * widget, FSceneViewport::OnKeyChar, the viewport client's InputChar, which in UDreamGameViewportClient
 * offers it to the console and then routes it to the field being edited
 * (UUITextInput::RouteCharacterInputToActiveInput). A project that has not set this viewport client
 * stops at the console, and a character goes nowhere; that is what the rig swapping the class in, and
 * this test typing through it, is about. The character step itself fails, naming the gate, if the
 * field does not receive a character -- including when the viewport client claims one without passing
 * it on, which in a play-in-editor viewport the engine's base client does for every character.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieTypeTest,
	"DreamGUI.Pie.TypingReachesTheFieldThroughTheGameViewportClient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPieTypeTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPieTestLocal;
	const TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Listener](FDreamDriverPieRig& InRig)
	{
		if (UDreamTextInput* Field = InRig.MakeControl<UDreamTextInput>(TEXT("Username"), nullptr, FVector2D(320.0, 40.0)))
		{
			Field->OnTextChanged.AddDynamic(Listener.Get(), &UDreamTextInteractionListener::HandleTextChanged);
		}
	});

	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Click(FDreamBy::Name(TEXT("Username")));
	Steps.Wait(FDreamUntil::Condition([Rig]() -> bool
		{
			const UDreamTextInput* Field = Cast<UDreamTextInput>(Rig->FindMade(TEXT("Username")));
			return Field != nullptr && Field->InputBehaviour != nullptr && Field->InputBehaviour->IsInputActive();
		}, ConditionLimit()),
		StepLimit(), TEXT("the click to begin an edit of the field"));
	FDreamDriverPieRig::TypeThroughViewport(Steps, TEXT("hello"));
	Steps.Then([this, Rig, Listener](FDreamDriverContext&)
	{
		UGameViewportClient* Client = Rig->GetViewportClient();
		TestTrue(TEXT("The characters went through a UDreamGameViewportClient"),
			Client != nullptr && Client->IsA<UDreamGameViewportClient>());
		UDreamTextInput* Field = Cast<UDreamTextInput>(Rig->FindMade(TEXT("Username")));
		if (!TestNotNull(TEXT("The field is still there"), Field))
		{
			return;
		}
		TestEqual(TEXT("The field holds what was typed"), Field->GetText(), FString(TEXT("hello")));
		// SEditableText raises OnTextChanged once per edit, and each character is one edit.
		TestEqual(TEXT("Each character arrived on its own"), Listener->TextChangedCount, 5);
		TestSamePtr(TEXT("The field the viewport client routed to is the one being edited"),
			UUITextInput::GetActiveTextInput(), Field->InputBehaviour.Get());
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

/*
 * A default button's colour transition, in engine frames.
 *
 * UUISelectable tints its face through a tween (AnimDuration, 0.2 seconds for a default UDreamButton)
 * and tweens exist only where a GameInstance owns a UDreamTweenManager. A play session has one, and
 * its tick helper runs in the engine's tick groups, so this is the transition exactly as a player sees
 * it. Asserted: the face rests at the normal colour; on the frame the pointer arrives it is not yet the
 * hovered colour (unless that one frame was longer than the whole transition, which a slow machine can
 * make happen -- then it is said, not failed); it reaches the hovered colour; and that took a real part
 * of the transition's length in world time. Without the tween the face either never changes or
 * changes in the arrival frame, and one of those fails.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieHoverTweenTest,
	"DreamGUI.Pie.ADefaultButtonFadesToItsHoveredColourInEngineFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPieHoverTweenTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPieTestLocal;

	/** What the button looked like at each moment that matters, carried between frames. */
	struct FHoverSeen
	{
		FColor Normal = FColor::Black;
		FColor Hovered = FColor::Black;
		float Duration = 0.0f;
		TOptional<FColor> OnArrival;
		double FrameSecondsOnArrival = 0.0;
		double WorldSecondsOnArrival = 0.0;
		double WorldSecondsOnReaching = 0.0;
	};
	const TSharedRef<FHoverSeen> Seen = MakeShared<FHoverSeen>();
	const TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Listener](FDreamDriverPieRig& InRig)
	{
		// Default in every respect but its name and size: the claim is about the transition a button
		// gets without anybody asking for one.
		BindButton(InRig.MakeControl<UDreamButton>(TEXT("Tinted"), nullptr, FVector2D(200.0, 60.0)), Listener.Get());
	});

	// The button and its face, looked up afresh each time: the rig hands out nothing that could keep
	// a play-world object alive past the session.
	const auto ButtonOf = [Rig]() -> UDreamButton*
	{
		return Cast<UDreamButton>(Rig->FindMade(TEXT("Tinted")));
	};
	const auto FaceColourOf = [ButtonOf]() -> TOptional<FColor>
	{
		const UDreamVisual* Face = TintedFace(ButtonOf());
		return Face != nullptr ? TOptional<FColor>(Face->GetColor()) : TOptional<FColor>();
	};

	Rig->Sequence()
		.Then([this, Seen, ButtonOf, FaceColourOf](FDreamDriverContext& InContext)
		{
			TestNotNull(TEXT("The play session's GameInstance has a tween manager"),
				UDreamTweenManager::GetDreamTweenInstance(InContext.World));
			UDreamButton* Button = ButtonOf();
			if (!TestTrue(TEXT("The button has a face it tints"), Button != nullptr && Button->ButtonBehaviour != nullptr && FaceColourOf().IsSet()))
			{
				return;
			}
			// Read off the button rather than written here: the default is whatever the library's style
			// says, and the claim is that the button reaches its own hovered colour, whatever that is.
			Seen->Normal = Button->ButtonBehaviour->GetNormalColor();
			Seen->Hovered = Button->ButtonBehaviour->GetHoveredColor();
			Seen->Duration = Button->ButtonBehaviour->GetAnimDuration();
			TestTrue(TEXT("A default button has a transition of some length"), Seen->Duration > 0.0f);
			TestTrue(TEXT("...between two different colours, or there is nothing to watch"), Seen->Normal != Seen->Hovered);
		})
		// At rest first. Waited for rather than asserted: whether a new button OPENS in its normal colour
		// is a different claim with a test of its own (below), and this one should not fail for it.
		.Wait(FDreamUntil::Condition([Seen, FaceColourOf]() -> bool
			{
				const TOptional<FColor> Now = FaceColourOf();
				return Now.IsSet() && Now.GetValue() == Seen->Normal;
			}, ConditionLimit()),
			StepLimit(), TEXT("the button's face to rest at its normal colour"))
		.MoveTo(FDreamBy::Name(TEXT("Tinted")))
		.Wait(FDreamUntil::Condition([Listener]() { return Listener->HoveredCount > 0; }, ConditionLimit()),
			StepLimit(), TEXT("the pointer's arrival to hover the button"))
		.Then([Seen, FaceColourOf](FDreamDriverContext& InContext)
		{
			// The frame the hover began: the transition started in this frame's event system tick.
			Seen->OnArrival = FaceColourOf();
			Seen->FrameSecondsOnArrival = FApp::GetDeltaTime();
			Seen->WorldSecondsOnArrival = InContext.World != nullptr ? InContext.World->GetTimeSeconds() : 0.0;
		})
		.Wait(FDreamUntil::Condition([Seen, FaceColourOf]() -> bool
			{
				const TOptional<FColor> Now = FaceColourOf();
				return Now.IsSet() && Now.GetValue() == Seen->Hovered;
			}, ConditionLimit()),
			StepLimit(), TEXT("the button's face to reach its hovered colour"))
		.Then([this, Seen](FDreamDriverContext& InContext)
		{
			Seen->WorldSecondsOnReaching = InContext.World != nullptr ? InContext.World->GetTimeSeconds() : 0.0;
			const double Taken = Seen->WorldSecondsOnReaching - Seen->WorldSecondsOnArrival;
			AddInfo(FString::Printf(TEXT("The face went from %s to %s in %.3f s of world time; the transition is %.3f s and the arrival frame was %.4f s."),
				Seen->OnArrival.IsSet() ? *Seen->OnArrival->ToString() : TEXT("(no face)"), *Seen->Hovered.ToString(),
				Taken, Seen->Duration, Seen->FrameSecondsOnArrival));
			if (Seen->FrameSecondsOnArrival >= Seen->Duration)
			{
				// One frame covered the whole transition; nothing in between is observable in this run.
				AddInfo(TEXT("The arrival frame was longer than the transition, so the in-between colours could not be seen; only the end colour is asserted."));
				return;
			}
			TestTrue(TEXT("On the frame the pointer arrived the face had not yet reached the hovered colour -- it fades, it does not snap"),
				Seen->OnArrival.IsSet() && Seen->OnArrival.GetValue() != Seen->Hovered);
			// Half, not all of it: the tween starts inside one frame and finishes inside another, and
			// both ends round to a frame. A snap takes none of it.
			TestTrue(FString::Printf(TEXT("Reaching the hovered colour took a real part of the %.2f s transition (%.3f s)"), Seen->Duration, Taken),
				Taken >= 0.5 * Seen->Duration);
		})
		.PerformLatent();
	Rig->Finish();
	return true;
}

/*
 * A new default button opens in its normal colour, on the frame it is made.
 *
 * UMG draws a freshly created SButton in its normal style from its first frame; being created is not
 * a transition. UUISelectable means the same of itself -- OnRegister applies the state immediately
 * "so the control opens in its normal colours rather than whatever the brush was authored with" -- but
 * a UDreamButton is given its colours by its style push during construction
 * (UDreamUIControl::PushSelectableState -> UUISelectable::SetNormalColor, after WireParts has already
 * set the transition target), and that setter re-applies the state through the ANIMATED road. Where
 * tweens run -- a play session, or any world a GameInstance owns -- that is a fade from the face's
 * initial colour to the normal one, over the transition's length, for every button on a screen that
 * has just opened.
 *
 * Read in the frame the button is made, before any tween has ticked; then the face must still arrive
 * at the normal colour, which holds either way.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieOpensInNormalColourTest,
	"DreamGUI.Pie.AFreshDefaultButtonOpensInItsNormalColourWithoutFadingIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPieOpensInNormalColourTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPieTestLocal;

	/** The face as it was on the frame the button was made. */
	struct FOpening
	{
		TOptional<FColor> OnCreation;
		FColor Normal = FColor::Black;
	};
	const TSharedRef<FOpening> Seen = MakeShared<FOpening>();

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Seen](FDreamDriverPieRig& InRig)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(TEXT("Fresh"), nullptr, FVector2D(200.0, 60.0));
		if (const UDreamVisual* Face = TintedFace(Button))
		{
			// Inside the latent command that made it, so after this frame's world tick and before the
			// next one: no tween has advanced since the button was built.
			Seen->OnCreation = Face->GetColor();
			Seen->Normal = Button->ButtonBehaviour->GetNormalColor();
		}
	});
	Rig->Sequence()
		.Then([this, Seen](FDreamDriverContext&)
		{
			if (!TestTrue(TEXT("A default button with a tinted face was made"), Seen->OnCreation.IsSet()))
			{
				return;
			}
			TestTrue(FString::Printf(
				TEXT("On the frame it is made the face already wears its normal colour %s (it wears %s) -- a new SButton opens in its normal style, with no transition"),
				*Seen->Normal.ToString(), *Seen->OnCreation->ToString()),
				Seen->OnCreation.GetValue() == Seen->Normal);
		})
		.Wait(FDreamUntil::Condition([Rig, Seen]() -> bool
			{
				const UDreamVisual* Face = TintedFace(Cast<UDreamButton>(Rig->FindMade(TEXT("Fresh"))));
				return Face != nullptr && Face->GetColor() == Seen->Normal;
			}, ConditionLimit()),
			StepLimit(), TEXT("the new button's face to settle at its normal colour"))
		.PerformLatent();
	Rig->Finish();
	return true;
}

/*
 * A world-space button, hit through the local player's real projection.
 *
 * Headless, a world-space ray has to come from the driver's own camera, because there is no player to
 * deproject through. Here there is one: the button sits on a world-space panel in front of the player,
 * the raycaster is the production UDreamWorldSpaceRaycaster, and it turns the pointer's pixel into a ray
 * with ULocalPlayer::GetProjectionData -- the road a game's world-space UI takes, which nothing headless
 * reaches. The pixel the driver aims at comes from the context's camera, mirrored from the same player;
 * so a click that lands proves the two agree, and the ray check says by how much.
 *
 * The button is placed off the panel's centre on purpose. The centre of the view is where every
 * mistake that keeps the view axis -- a flipped Y, a mirrored X, a wrong field of view -- still lands.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieWorldSpaceClickTest,
	"DreamGUI.Pie.AWorldSpaceButtonIsClickedThroughTheLocalPlayersRealProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPieWorldSpaceClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPieTestLocal;
	const TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([this, Listener](FDreamDriverPieRig& InRig)
	{
		/** How far in front of the player's eye the panel stands, in world units. */
		constexpr double PanelDistance = 400.0;
		if (!TestTrue(TEXT("The rig mirrors the player's view into the context's camera"), InRig.Context().Camera.IsValid()))
		{
			return;
		}
		// Square to the view: the panel's face is its local X=0 plane, so giving it the eye's rotation
		// points its normal down the line of sight.
		const FMinimalViewInfo& Eye = InRig.Context().Camera->View;
		const FTransform PanelTransform(Eye.Rotation, Eye.Location + Eye.Rotation.Vector() * PanelDistance);
		UDreamWidget* Panel = InRig.MakeWorldPanel(TEXT("WorldPanel"), PanelTransform, FVector2D(400.0, 300.0));
		if (!TestNotNull(TEXT("A world-space panel stands in front of the player"), Panel))
		{
			return;
		}
		// Up and to the right of the panel's centre (anchored Y is upward).
		BindButton(InRig.MakeControl<UDreamButton>(TEXT("WorldPlay"), Panel, FVector2D(160.0, 60.0), FVector2D(90.0, 60.0)), Listener.Get());
		TestNotNull(TEXT("The production world-space raycaster is placed for the player"), InRig.AddWorldPointer());
	});

	Rig->Sequence()
		.MoveTo(Rig->Made(TEXT("WorldPlay")))
		.Wait(FDreamUntil::Condition([Listener]() { return Listener->HoveredCount > 0; }, ConditionLimit()),
			StepLimit(), TEXT("the pointer's arrival to hover the world-space button"))
		.Then([this](FDreamDriverContext& InContext)
		{
			/*
			 * The ray the production raycaster casts through the pointer's pixel, rebuilt here from the
			 * local player exactly as UDreamWorldSpaceRaycaster::GenerateRay builds it, next to the ray
			 * the driver's camera gives for the same pixel. Checked separately, direction and origin, so
			 * a disagreement says which half of the camera drifted from the player.
			 */
			ULocalPlayer* Player = InContext.LocalPlayer;
			FViewport* Viewport = Player != nullptr && Player->ViewportClient != nullptr ? Player->ViewportClient->Viewport : nullptr;
			FSceneViewProjectionData Projection;
			if (!TestTrue(TEXT("The local player has projection data for its viewport"),
				Player != nullptr && Viewport != nullptr && Player->GetProjectionData(Viewport, Projection)))
			{
				return;
			}
			if (!TestTrue(TEXT("The context has a camera and an input module to ask"), InContext.Camera.IsValid() && InContext.InputModule != nullptr))
			{
				return;
			}
			const FVector2D Pixel = InContext.InputModule->GetVirtualCursor();
			const FMatrix InverseViewProjection = (Projection.ViewRotationMatrix * Projection.ProjectionMatrix).InverseFast();
			FVector PlayerOrigin = FVector::ZeroVector;
			FVector PlayerDirection = FVector::ForwardVector;
			FSceneView::DeprojectScreenToWorld(Pixel, Projection.GetConstrainedViewRect(), InverseViewProjection, PlayerOrigin, PlayerDirection);
			PlayerOrigin += Projection.ViewOrigin;

			FVector CameraOrigin = FVector::ZeroVector;
			FVector CameraDirection = FVector::ForwardVector;
			if (!TestTrue(TEXT("The driver's camera can deproject the pointer's pixel"), InContext.Camera->Deproject(Pixel, CameraOrigin, CameraDirection)))
			{
				return;
			}
			const double Cosine = FMath::Clamp(FVector::DotProduct(PlayerDirection.GetSafeNormal(), CameraDirection.GetSafeNormal()), -1.0, 1.0);
			const double DegreesApart = FMath::RadiansToDegrees(FMath::Acos(Cosine));
			const double UnitsApart = FVector::Dist(PlayerOrigin, CameraOrigin);
			TestTrue(FString::Printf(TEXT("Through pixel %s the driver's camera and the local player cast rays in the same direction (%.4f degrees apart)"),
				*Pixel.ToString(), DegreesApart), DegreesApart < 0.05);
			TestTrue(FString::Printf(TEXT("...from the same origin (%.3f units apart)"), UnitsApart), UnitsApart < 1.0);
		})
		.Press()
		.Wait(FDreamUntil::Condition([Listener]() { return Listener->PressedCount > 0; }, ConditionLimit()),
			StepLimit(), TEXT("the press to reach the world-space button"))
		.Then([this](FDreamDriverContext& InContext)
		{
			// Which raycaster found the press: the production class itself, not a test subclass -- the
			// point of this test is the ray a game casts.
			const UDreamPointerEventData* EventData = InContext.GetPointerEventData(0);
			const UDreamBaseRaycaster* PressRaycaster = EventData != nullptr ? EventData->PressRaycaster.Get() : nullptr;
			TestTrue(FString::Printf(TEXT("The press was found by the production UDreamWorldSpaceRaycaster (it was found by %s)"),
				*GetNameSafe(PressRaycaster != nullptr ? PressRaycaster->GetClass() : nullptr)),
				PressRaycaster != nullptr && PressRaycaster->GetClass() == UDreamWorldSpaceRaycaster::StaticClass());
		})
		.Release()
		.Wait(FDreamUntil::Condition([Listener]() { return Listener->ClickedCount > 0; }, ConditionLimit()),
			StepLimit(), TEXT("the release to click the world-space button"))
		.Then([this, Listener](FDreamDriverContext&)
		{
			TestEqual(TEXT("The world-space button was clicked once"), Listener->ClickedCount, 1);
			TestEqual(TEXT("...pressed once"), Listener->PressedCount, 1);
			TestEqual(TEXT("...and released once"), Listener->ReleasedCount, 1);
		})
		.PerformLatent();
	Rig->Finish();
	return true;
}

/*
 * The same pass over a button, on the headless rig and in a play session, compared event for event.
 *
 * The headless rig is what the rest of this module's interaction tests stand on, in its default
 * arrangement: input straight into the driver's module, a player controller spawned by hand, a
 * viewport size handed to the canvas. If what it reports for arrive, press, release and leave differs
 * from what a real session reports for the same gesture -- an extra event, a missing one, a different
 * order -- then every one of those tests is asserting against a world a player never sees. The
 * headless half runs synchronously in the test body; the play half is queued after it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieMatchesHeadlessTest,
	"DreamGUI.Pie.APassOverAButtonInPlayMatchesTheHeadlessRigEventForEvent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPieMatchesHeadlessTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPieTestLocal;
	TArray<FName> HeadlessLog;
	{
		const TStrongObjectPtr<UDreamPressInteractionListener> HeadlessListener(NewObject<UDreamPressInteractionListener>());
		FDreamDriverRig HeadlessRig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
		HeadlessRig.BindTest(this);
		if (!TestTrue(TEXT("The rig came up"), HeadlessRig.IsUsable()))
		{
			return false;
		}
		UDreamButton* Button = HeadlessRig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, PassButtonSize);
		if (!TestNotNull(TEXT("A button can be made on the headless rig"), Button))
		{
			return false;
		}
		BindButton(Button, HeadlessListener.Get());
		HeadlessRig.PumpFrames(1);
		FDreamDriverSequence Steps = HeadlessRig.Driver()->Sequence();
		AddPassOverButton(Steps, FDreamBy::Name(TEXT("Play")), HeadlessListener);
		TestTrue(TEXT("The pass completes on the headless rig"), Steps.Perform());
		HeadlessLog = HeadlessListener->Log;
	}
	// Held to the expected order too, so two rigs that agreed on the wrong thing would not pass.
	TestTrue(FString::Printf(TEXT("On the headless rig the pass produces hover, press, release, click, unhover (got %s)"), *DescribeLog(HeadlessLog)),
		HeadlessLog == ExpectedPassLog());

	QueuePassInPlayAndCompare(*this, HeadlessLog, TEXT("the headless rig"));
	return true;
}

/*
 * The same comparison against the headless rig in its input-actor arrangement: a real
 * ADreamStandaloneInputEventSystemActor whose buttons go through a hand-made player controller's input
 * stack. That arrangement is the headless rig's imitation of exactly what a play session does for
 * real, so this is the direct check of the imitation; a difference here and not in the default
 * arrangement's comparison points at the hand-made player controller, the local player or the pump's
 * input tick.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieMatchesActorRigTest,
	"DreamGUI.Pie.APassOverAButtonInPlayMatchesTheActorHostedHeadlessRigEventForEvent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPieMatchesActorRigTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPieTestLocal;
	TArray<FName> HeadlessLog;
	{
		const TStrongObjectPtr<UDreamPressInteractionListener> HeadlessListener(NewObject<UDreamPressInteractionListener>());
		FDreamRigOptions Options;
		Options.InputHost = EDreamRigInputHost::StandaloneActor;
		FDreamDriverRig HeadlessRig = FDreamDriverRig::Headless(Options);
		HeadlessRig.BindTest(this);
		if (!TestTrue(TEXT("The rig came up"), HeadlessRig.IsUsable()))
		{
			AddError(FString::Printf(TEXT("The headless rig with a standalone input actor did not come up: %s"), *HeadlessRig.GetBuildFailure()));
			return false;
		}
		UDreamButton* Button = HeadlessRig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, PassButtonSize);
		if (!TestNotNull(TEXT("A button can be made on the actor-hosted headless rig"), Button))
		{
			return false;
		}
		BindButton(Button, HeadlessListener.Get());
		HeadlessRig.PumpFrames(1);
		FDreamDriverSequence Steps = HeadlessRig.Driver()->Sequence();
		AddPassOverButton(Steps, FDreamBy::Name(TEXT("Play")), HeadlessListener);
		TestTrue(TEXT("The pass completes on the actor-hosted headless rig"), Steps.Perform());
		HeadlessLog = HeadlessListener->Log;
	}
	TestTrue(FString::Printf(TEXT("On the actor-hosted headless rig the pass produces hover, press, release, click, unhover (got %s)"), *DescribeLog(HeadlessLog)),
		HeadlessLog == ExpectedPassLog());

	QueuePassInPlayAndCompare(*this, HeadlessLog, TEXT("the actor-hosted headless rig"));
	return true;
}

#endif
