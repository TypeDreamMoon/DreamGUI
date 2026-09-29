// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "DreamTweenManager.h"
#include "DreamTweener.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Interaction/UIButton.h"
#include "Interaction/UISelectable.h"
#include "WaitUntil.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverUntil.h"
#include "DreamScopedGameInstanceWorld.h"

/*
 * THE RIG'S WORLD IS A GAME'S WORLD, TWEENS INCLUDED.
 *
 * The tween manager is a game instance subsystem. A world made with UWorld::CreateWorld belongs to no
 * game instance, so in it UDreamTweenManager::To answers null and every animated thing a control does
 * -- the Selectable colour fade, a toggle's check, an animated scroll, a popup opening -- either snaps
 * or never happens. That is the designer's preview world, and it was also, until now, the rig's.
 *
 * These pin the other half: the rig's world is owned by a game instance built the way a standalone
 * game builds one, the tween manager exists in it, the pump advances tweens at the rate the world
 * clock says, a default-configured control really does animate, and taking the rig down leaves the
 * engine's world list as it found it.
 */
namespace DreamDriverGameInstanceTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** A viewport pixel well clear of InRect: the far quadrant from its centre. */
	FVector2D PixelAwayFrom(const FBox2D& InRect)
	{
		const FVector2D Centre = InRect.GetCenter();
		return FVector2D(
			Centre.X < ViewportSize.X * 0.5 ? ViewportSize.X - 40.0 : 40.0,
			Centre.Y < ViewportSize.Y * 0.5 ? ViewportSize.Y - 40.0 : 40.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverGameInstanceWorldTest,
	"DreamGUI.Driver.GameInstance.TheRigsWorldBelongsToAGameInstanceThatHasATweenManager",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverGameInstanceWorldTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverGameInstanceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UWorld* World = Rig.GetWorld();
	UGameInstance* GameInstance = Rig.GetGameInstance();
	if (!TestNotNull(TEXT("A default rig has a game instance"), GameInstance))
	{
		return false;
	}

	// Both directions, because both are asked: a widget finds the manager through
	// World->GetGameInstance(), and the manager finds its world through GameInstance->GetWorld().
	TestSamePtr(TEXT("The rig's world says the game instance owns it"), World->GetGameInstance(), GameInstance);
	TestSamePtr(TEXT("And the game instance says the rig's world is its world"), GameInstance->GetWorld(), World);
	TestSamePtr(TEXT("The context carries the same game instance the rig reports"), Rig.Context().GameInstance, GameInstance);
	TestTrue(TEXT("The options say so too"), Rig.GetOptions().bWithGameInstance);

	UDreamTweenManager* TweenManager = UDreamTweenManager::GetDreamTweenInstance(World);
	TestNotNull(TEXT("The tween manager exists, found the way every widget tween finds it"), TweenManager);
	TestSamePtr(TEXT("And it is the game instance's own subsystem"), TweenManager, GameInstance->GetSubsystem<UDreamTweenManager>());

	TestTrue(TEXT("The engine knows the world through a world context"),
		DreamTests::FScopedGameInstanceWorld::HasWorldContextFor(World));
	TestTrue(TEXT("And that context is owned by the rig's game instance"),
		DreamTests::FScopedGameInstanceWorld::HasWorldContextOwnedBy(GameInstance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverGameInstanceBareWorldTest,
	"DreamGUI.Driver.GameInstance.ABareWorldRigHasNoGameInstanceAndNoTweensLikeTheDesignerPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverGameInstanceBareWorldTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverGameInstanceTestLocal;
	FDreamRigOptions Options;
	Options.ViewportSize = ViewportSize;
	// The world the designer previews in: made with UWorld::CreateWorld, owned by nobody. Kept as an
	// option precisely so "what does this control do without tweens" stays askable.
	Options.bWithGameInstance = false;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UWorld* World = Rig.GetWorld();

	TestNull(TEXT("The rig reports no game instance"), Rig.GetGameInstance());
	TestNull(TEXT("The world belongs to none"), World->GetGameInstance());
	TestNull(TEXT("So there is no tween manager to find"), UDreamTweenManager::GetDreamTweenInstance(World));

	// And the consequence every animated control lives with in such a world: asking for a tween gets
	// nothing back, and nothing is ever written.
	TSharedRef<float> Value = MakeShared<float>(0.0f);
	UDreamTweener* Tweener = UDreamTweenManager::To(World,
		FDreamTweenFloatGetterFunction::CreateLambda([Value]() { return *Value; }),
		FDreamTweenFloatSetterFunction::CreateLambda([Value](float InValue) { *Value = InValue; }),
		1.0f, 0.5f);
	TestNull(TEXT("A tween asked for in a world with no game instance is not made"), Tweener);
	Rig.PumpFrames(30);
	TestEqual(TEXT("And the value it would have driven never moves"), *Value, 0.0f);
	TestFalse(TEXT("The world has no world context either: nothing registered it with the engine"),
		DreamTests::FScopedGameInstanceWorld::HasWorldContextFor(World));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverGameInstanceTweenTimingTest,
	"DreamGUI.Driver.GameInstance.AHalfSecondTweenIsHalfwayAtFifteenFramesAndFinishedAtThirty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverGameInstanceTweenTimingTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverGameInstanceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UWorld* World = Rig.GetWorld();
	// The arithmetic below is in frames of this length: 30 of them are half a second.
	if (!TestNearlyEqual(TEXT("The pump's frame is a sixtieth of a second"), Rig.Context().FrameSeconds, 1.0f / 60.0f, 1.0e-6f))
	{
		return false;
	}

	// Shared, so the setter can never write into a dead stack frame whatever order things die in.
	TSharedRef<float> Value = MakeShared<float>(0.0f);
	UDreamTweener* Tweener = UDreamTweenManager::To(World,
		FDreamTweenFloatGetterFunction::CreateLambda([Value]() { return *Value; }),
		FDreamTweenFloatSetterFunction::CreateLambda([Value](float InValue) { *Value = InValue; }),
		1.0f, 0.5f);
	if (!TestNotNull(TEXT("With a game instance, asking for a tween makes one"), Tweener))
	{
		return false;
	}
	// Linear, so "halfway in time" is "halfway in value" and the assertion needs no easing table.
	Tweener->SetEase(EDreamTweenEase::Linear);
	TestEqual(TEXT("It is a DuringPhysics tween, the tick type every widget tween gets"),
		Tweener->GetTickType(), EDreamTweenTickType::DuringPhysics);
	TWeakObjectPtr<UDreamTweener> WeakTweener(Tweener);

	TestEqual(TEXT("Nothing moves before a frame is pumped"), *Value, 0.0f);
	Rig.PumpFrames(15);
	// 15 frames of 1/60 is a quarter of a second: half of the tween. A pump that did not step tweens
	// leaves it at 0; one that stepped them by anything but the world's delta lands elsewhere.
	TestNearlyEqual(TEXT("A quarter of a second in, a half-second linear tween is halfway"), *Value, 0.5f, 0.02f);
	TestTrue(TEXT("And it is still running"), UDreamTweenManager::IsTweening(World, WeakTweener.Get()));

	Rig.PumpFrames(15);
	TestEqual(TEXT("Half a second in, it has written its end value exactly"), *Value, 1.0f);
	TestFalse(TEXT("And it has finished and left the manager"), UDreamTweenManager::IsTweening(World, WeakTweener.Get()));

	Rig.PumpFrames(5);
	TestEqual(TEXT("A finished tween writes nothing more"), *Value, 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverGameInstanceButtonFadeTest,
	"DreamGUI.Driver.GameInstance.ADefaultButtonFadesToItsHoveredColourAndBackOverItsTransitionTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverGameInstanceButtonFadeTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverGameInstanceTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// Nothing configured: the button as MakeControl builds it, with the style's own colours and the
	// style's own transition time pushed onto its UIButton (UDreamButton::ApplyStyle).
	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 80.0));
	if (!TestNotNull(TEXT("A button can be made on the rig"), Button)
		|| !TestNotNull(TEXT("It carries its UIButton"), Button->ButtonBehaviour.Get()))
	{
		return false;
	}
	Rig.PumpFrames(1);
	UUIButton* Selectable = Button->ButtonBehaviour;
	UDreamVisual* Face = Selectable->GetTransitionTarget();
	if (!TestNotNull(TEXT("The transition tints the face the button stands on"), Face))
	{
		return false;
	}

	const FColor Normal = Selectable->GetNormalColor();
	const FColor Hovered = Selectable->GetHoveredColor();
	const float Duration = Selectable->GetAnimDuration();
	const float FrameSeconds = Rig.Context().FrameSeconds;
	// The preconditions the rest reads from, stated rather than assumed: a style that made the two
	// colours equal, or the transition instant, would make every line below vacuous.
	if (!TestTrue(TEXT("The hovered colour differs from the normal one"), Hovered != Normal)
		|| !TestTrue(FString::Printf(TEXT("The default transition takes several frames (%.3f seconds)"), Duration), Duration > 3.0f * FrameSeconds))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	// The style push itself is a transition: ApplyStyle sets the normal colour while the selectable is
	// in its normal state, and UUISelectable::SetNormalColor animates to it. So the face is still on
	// its way to Normal a frame after it was made, and the hover below has to start from rest.
	const FWaitTimeout SettleTimeout = FWaitTimeout::InSeconds(Duration + 2.0 * FrameSeconds);
	TestTrue(TEXT("Within its transition time the new button's face settles on the normal colour"),
		Driver->Wait(FDreamUntil::Condition([Face, Normal]() { return Face->GetColor() == Normal; }, SettleTimeout),
			SettleTimeout, TEXT("the face settling on the normal colour")));

	FDreamElementRef Play = Driver->Find(FDreamBy::Name(TEXT("Play")));
	const TOptional<FBox2D> Rect = Play->GetPixelRect();
	if (!TestTrue(TEXT("The button has a place on the viewport"), Rect.IsSet()))
	{
		return false;
	}

	TestTrue(TEXT("Hovering the button completes"), Play->Hover());
	// One frame in: the hover arrived, the fade started and took its first step, and that is all. In
	// a world with no tween manager the colour would never have left Normal; with the transition
	// snapping it would already be Hovered. Neither is a fade.
	TestTrue(TEXT("One frame into the hover the face is on its way, not yet there"), Face->GetColor() != Hovered);

	// A frame's grace on top of the transition time: the fade is timed from the frame the hover was
	// seen, and the wait from the frame after.
	const FWaitTimeout FadeTimeout = FWaitTimeout::InSeconds(Duration + 2.0 * FrameSeconds);
	TestTrue(TEXT("Within its transition time the face reaches the hovered colour"),
		Driver->Wait(FDreamUntil::Condition([Face, Hovered]() { return Face->GetColor() == Hovered; }, FadeTimeout),
			FadeTimeout, TEXT("the face reaching the hovered colour")));

	TestTrue(TEXT("Moving off the button completes"), Driver->Sequence().MoveToPixel(PixelAwayFrom(Rect.GetValue())).Perform());
	TestFalse(TEXT("Off the button, it is no longer hovered"), Play->IsHovered());
	TestTrue(TEXT("Within its transition time the face is back at the normal colour"),
		Driver->Wait(FDreamUntil::Condition([Face, Normal]() { return Face->GetColor() == Normal; }, FadeTimeout),
			FadeTimeout, TEXT("the face returning to the normal colour")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverGameInstanceTeardownTest,
	"DreamGUI.Driver.GameInstance.TearingDownARigTakesItsWorldContextOffTheEnginesList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverGameInstanceTeardownTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverGameInstanceTestLocal;
	if (!TestNotNull(TEXT("There is an engine to register worlds with"), GEngine))
	{
		return false;
	}
	const int32 ContextsBefore = GEngine->GetWorldContexts().Num();

	// Remembered as addresses only, and never dereferenced once the rig is gone: after it the world
	// and the game instance are garbage, and the question is only whether the engine still names them.
	const UWorld* RigWorld = nullptr;
	const UGameInstance* RigGameInstance = nullptr;
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
		Rig.BindTest(this);
		if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		RigWorld = Rig.GetWorld();
		RigGameInstance = Rig.GetGameInstance();
		TestEqual(TEXT("While the rig lives, the engine has exactly one more world context"),
			GEngine->GetWorldContexts().Num(), ContextsBefore + 1);
		TestTrue(TEXT("And it is the rig's"), DreamTests::FScopedGameInstanceWorld::HasWorldContextFor(RigWorld));
	}

	TestFalse(TEXT("Once the rig is gone no world context names its world"),
		DreamTests::FScopedGameInstanceWorld::HasWorldContextFor(RigWorld));
	TestFalse(TEXT("And none is owned by its game instance"),
		DreamTests::FScopedGameInstanceWorld::HasWorldContextOwnedBy(RigGameInstance));
	TestEqual(TEXT("The engine's list is back to the length it had"), GEngine->GetWorldContexts().Num(), ContextsBefore);
	return true;
}

#endif
