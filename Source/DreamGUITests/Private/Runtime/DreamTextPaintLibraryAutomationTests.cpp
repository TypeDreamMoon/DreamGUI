// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Text/DreamTextPaintLibrary.h"
#include "DreamTweenManager.h"
#include "DreamTweener.h"
#include "Engine/World.h"
#include "DreamScopedGameInstanceWorld.h"

/*
 * The paint tweens: each drives one of a text's paint animation setters through DreamTween, as UDreamVisual::ColorTo
 * drives the colour. Run in a world a game instance owns -- the tween manager is one of its subsystems -- with each tween
 * on the manager's manual clock, so a step is exactly the time handed to it.
 */
namespace DreamTextPaintLibraryTestLocal
{
	/** A registered widget drawing a text, in InWorld. */
	UDreamText* MakeText(UWorld* InWorld, UDreamWidget*& OutWidget)
	{
		OutWidget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Public | RF_Transactional);
		OutWidget->SetDisplayName(TEXT("Painted"));
		OutWidget->OnRegister();
		return OutWidget->CreateNewVisual<UDreamText>();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintLibraryTweenTest,
	"DreamGUI.Text.Paint.PaintPhaseToAndPaintAngleToTweenATextsPaintAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintLibraryTweenTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintLibraryTestLocal;
	DreamTests::FScopedGameInstanceWorld TestWorld;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(TestWorld.World);
	UDreamWidget* Widget = nullptr;
	UDreamText* Text = MakeText(TestWorld.World, Widget);
	if (!TestNotNull(TEXT("the world has a tween manager"), Manager) || !TestNotNull(TEXT("a text"), Text))
	{
		return false;
	}

	UDreamTweener* Face = UDreamTextPaintLibrary::PaintPhaseTo(Text, EDreamTextPaintLayer::Face, 1.0f, 1.0f, 0.0f, EDreamTweenEase::Linear);
	UDreamTweener* Outline = UDreamTextPaintLibrary::PaintPhaseTo(Text, EDreamTextPaintLayer::Outline, -1.0f, 1.0f, 0.0f, EDreamTweenEase::Linear);
	UDreamTweener* Overlay = UDreamTextPaintLibrary::PaintPhaseTo(Text, EDreamTextPaintLayer::Overlay, 2.0f, 1.0f, 0.5f, EDreamTweenEase::Linear);
	UDreamTweener* Angle = UDreamTextPaintLibrary::PaintAngleTo(Text, 90.0f, 1.0f, 0.0f, EDreamTweenEase::Linear);
	if (!TestNotNull(TEXT("a face phase tween"), Face) || !TestNotNull(TEXT("an outline one"), Outline)
		|| !TestNotNull(TEXT("an overlay one"), Overlay) || !TestNotNull(TEXT("an angle one"), Angle))
	{
		Widget->DestroyWidget();
		return false;
	}
	for (UDreamTweener* Tween : { Face, Outline, Overlay, Angle })
	{
		Tween->SetTickType(EDreamTweenTickType::Manual);
	}
	Manager->ManualTick(0.5f);
	TestEqual(TEXT("the face phase is halfway"), Text->GetFacePaintPhase(), 0.5f, 0.01f);
	TestEqual(TEXT("the outline phase halfway the other way"), Text->GetOutlinePaintPhase(), -0.5f, 0.01f);
	TestEqual(TEXT("the overlay phase waits out its delay"), Text->GetOverlayPaintPhase(), 0.0f, 0.01f);
	TestEqual(TEXT("the angle offset is halfway"), Text->GetPaintAngleOffset(), 45.0f, 0.1f);
	Manager->ManualTick(0.5f);
	TestEqual(TEXT("the face phase arrives"), Text->GetFacePaintPhase(), 1.0f, 0.01f);
	TestEqual(TEXT("and the angle offset"), Text->GetPaintAngleOffset(), 90.0f, 0.1f);
	TestEqual(TEXT("the delayed overlay is under way"), Text->GetOverlayPaintPhase(), 1.0f, 0.01f);

	TestNull(TEXT("no text, no tween"), UDreamTextPaintLibrary::PaintPhaseTo(nullptr, EDreamTextPaintLayer::Face, 1.0f));
	TestNull(TEXT("for the angle either"), UDreamTextPaintLibrary::PaintAngleTo(nullptr, 1.0f));
	Widget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintLibraryShimmerTest,
	"DreamGUI.Text.Paint.PlayShimmerSweepsTheOverlayPhaseFromMinusOneToOneAndStartsAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextPaintLibraryShimmerTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintLibraryTestLocal;
	DreamTests::FScopedGameInstanceWorld TestWorld;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(TestWorld.World);
	UDreamWidget* Widget = nullptr;
	UDreamText* Text = MakeText(TestWorld.World, Widget);
	if (!TestNotNull(TEXT("the world has a tween manager"), Manager) || !TestNotNull(TEXT("a text"), Text))
	{
		return false;
	}
	Text->SetOverlayPaintPhase(0.3f);
	UDreamTweener* Shimmer = UDreamTextPaintLibrary::PlayShimmer(Text, 1.0f, 0.25f, 2);
	if (!TestNotNull(TEXT("a shimmer"), Shimmer))
	{
		Widget->DestroyWidget();
		return false;
	}
	Shimmer->SetTickType(EDreamTweenTickType::Manual);
	TestEqual(TEXT("it starts off the text, at -1"), Text->GetOverlayPaintPhase(), -1.0f);
	Manager->ManualTick(0.25f);
	TestEqual(TEXT("and stays there through its delay"), Text->GetOverlayPaintPhase(), -1.0f, 0.01f);
	Manager->ManualTick(0.5f);
	TestEqual(TEXT("at an even speed: halfway through a pass is the middle"), Text->GetOverlayPaintPhase(), 0.0f, 0.01f);
	Manager->ManualTick(0.25f);
	TestEqual(TEXT("three quarters of the way, half the text past"), Text->GetOverlayPaintPhase(), 0.5f, 0.01f);
	Manager->ManualTick(0.25f);
	TestEqual(TEXT("a pass ends at 1, off the far side"), Text->GetOverlayPaintPhase(), 1.0f, 0.01f);
	Manager->ManualTick(0.25f);
	TestEqual(TEXT("and the next starts again from -1: a quarter in, it is at -0.5"), Text->GetOverlayPaintPhase(), -0.5f, 0.01f);
	TestTrue(TEXT("the shimmer runs on"), Manager->IsTweening(Shimmer));
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Manager->ManualTick(0.25f);
	}
	TestEqual(TEXT("after its two passes it rests at the end"), Text->GetOverlayPaintPhase(), 1.0f, 0.01f);
	TestFalse(TEXT("and is over"), Manager->IsTweening(Shimmer));
	TestNull(TEXT("no text, no shimmer"), UDreamTextPaintLibrary::PlayShimmer(nullptr));
	Widget->DestroyWidget();
	return true;
}

#endif
