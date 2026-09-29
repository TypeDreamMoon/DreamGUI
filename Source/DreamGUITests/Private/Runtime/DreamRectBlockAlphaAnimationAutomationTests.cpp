// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "DreamTweenManager.h"
#include "DreamTweener.h"
#include "Engine/World.h"

#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * A RECT BLOCK'S ALPHA TWEENS FADE THE COLOUR THEY NAME, STARTING FROM THAT COLOUR.
 *
 * UDreamRectBlock has an alpha tween for every colour it draws: the body, the body's gradient, the
 * border, the border's gradient and the two shadows. A tween takes its start value from its getter on
 * its first step, and the five beyond the body's are stamped out of one macro whose getter read the
 * BODY's alpha for all of them. The end value never showed it -- every one of them still arrived. The
 * way there did: a transparent border told to fade in over an opaque body began at opaque, so it was
 * fully drawn from the first frame and the fade never happened.
 *
 * So each case puts the colour it animates at alpha zero and the body at full alpha, asks for a linear
 * fade to full alpha, and reads the colour halfway through. Halfway up from its own zero is about half;
 * started from the body's alpha it would already be full. The rig's world belongs to a game instance,
 * so the tween manager exists and the pump advances tweens by the world's frame.
 */
namespace DreamRectBlockAlphaAnimationTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** How long each fade lasts, in the rig's frames: long enough that halfway is many frames from either end. */
	constexpr int32 FadeFrames = 20;

	/** One alpha tween, and the colour it is meant to animate. */
	struct FAlphaTweenCase
	{
		const TCHAR* ColourName;
		UDreamTweener* (UDreamRectBlock::*FadeTo)(float, float, float, EDreamTweenEase);
		const FColor& (UDreamRectBlock::*GetColour)() const;
		void (UDreamRectBlock::*SetColour)(const FColor&);
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockAlphaTweenStartTest,
	"DreamGUI.RectBlock.EveryAlphaTweenStartsFromTheAlphaOfTheColourItFades",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRectBlockAlphaTweenStartTest::RunTest(const FString& Parameters)
{
	using namespace DreamRectBlockAlphaAnimationTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("The rig's world has a tween manager to run the fades"),
		UDreamTweenManager::GetDreamTweenInstance(Rig.GetWorld())))
	{
		return false;
	}
	const float FadeSeconds = FadeFrames * Rig.Context().FrameSeconds;

	// Every alpha tween the macro stamps out. BodyAlphaTo is written by hand, always read the body's own
	// alpha, and has no other colour to be confused with, so it is not among them.
	const FAlphaTweenCase Cases[] =
	{
		{ TEXT("body gradient"), &UDreamRectBlock::BodyGradientAlphaTo, &UDreamRectBlock::GetBodyGradientColor, &UDreamRectBlock::SetBodyGradientColor },
		{ TEXT("border"), &UDreamRectBlock::BorderAlphaTo, &UDreamRectBlock::GetBorderColor, &UDreamRectBlock::SetBorderColor },
		{ TEXT("border gradient"), &UDreamRectBlock::BorderGradientAlphaTo, &UDreamRectBlock::GetBorderGradientColor, &UDreamRectBlock::SetBorderGradientColor },
		{ TEXT("inner shadow"), &UDreamRectBlock::InnerShadowAlphaTo, &UDreamRectBlock::GetInnerShadowColor, &UDreamRectBlock::SetInnerShadowColor },
		{ TEXT("outer shadow"), &UDreamRectBlock::OuterShadowAlphaTo, &UDreamRectBlock::GetOuterShadowColor, &UDreamRectBlock::SetOuterShadowColor },
	};
	for (const FAlphaTweenCase& Case : Cases)
	{
		// A widget of its own per case, so one fade can never be read as another's.
		UDreamWidget* Widget = Rig.MakeWidget(FString::Printf(TEXT("Faded %s"), Case.ColourName), nullptr, FVector2D(200.0, 100.0));
		UDreamRectBlock* Rect = Widget != nullptr ? Widget->CreateNewVisual<UDreamRectBlock>() : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("A rect block to fade the %s of"), Case.ColourName), Rect))
		{
			continue;
		}
		// The body opaque and the faded colour transparent, so the two possible starts are the two ends
		// of the range and cannot be mistaken for each other.
		Rect->SetBodyColor(FColor(255, 255, 255, 255));
		FColor Transparent = (Rect->*Case.GetColour)();
		Transparent.A = 0;
		(Rect->*Case.SetColour)(Transparent);
		if (!TestEqual(FString::Printf(TEXT("Before the fade the %s is transparent"), Case.ColourName),
			static_cast<int32>((Rect->*Case.GetColour)().A), 0))
		{
			continue;
		}

		UDreamTweener* Fade = (Rect->*Case.FadeTo)(1.0f, FadeSeconds, 0.0f, EDreamTweenEase::Linear);
		if (!TestNotNull(FString::Printf(TEXT("Fading the %s's alpha makes a tween"), Case.ColourName), Fade))
		{
			continue;
		}

		Rig.PumpFrames(FadeFrames / 2);
		const int32 Halfway = (Rect->*Case.GetColour)().A;
		// Linear from 0 to 255 is 127 at the midpoint. The window is wide enough to forgive a frame
		// either way and nowhere near 255, which is where a fade started from the body's alpha sits.
		TestTrue(FString::Printf(TEXT("Halfway through, the %s's alpha is halfway up from its own zero (%d; a start taken from the body's alpha would make it 255)"),
			Case.ColourName, Halfway),
			Halfway >= 100 && Halfway <= 155);
		TestEqual(FString::Printf(TEXT("And the body's alpha is left alone while the %s fades"), Case.ColourName),
			static_cast<int32>(Rect->GetBodyColor().A), 255);

		// The rest of the fade, and two frames' grace.
		Rig.PumpFrames(FadeFrames - FadeFrames / 2 + 2);
		TestEqual(FString::Printf(TEXT("The %s arrives at full alpha"), Case.ColourName),
			static_cast<int32>((Rect->*Case.GetColour)().A), 255);
	}
	return true;
}

#endif
