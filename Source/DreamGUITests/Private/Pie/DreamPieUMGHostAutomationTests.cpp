// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Blueprint/UserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "UMG/DreamUMGWidget.h"
#include "UMG/DreamUMGWidgetInteraction.h"
#include "WaitUntil.h"

#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Pie/DreamPieTestTypes.h"

/*
 * A UMG BUTTON HOSTED IN THE UI, PRESSED FOR REAL.
 *
 * UDreamUMGWidget draws a UMG widget into a texture through a virtual Slate window, and UDreamUMGWidgetInteraction --
 * UWidgetInteractionComponent's counterpart -- hands every DreamGUI pointer over that surface to Slate as a virtual user's
 * pointer, along the widget path the window's hit grid finds under it. The headless tests can only watch what the bridge
 * decides and record what it would send (Interaction/DreamUMGHostInteractionTestTypes.h): the hit grid is filled by drawing
 * the window, and nothing is drawn without a renderer. Here the session draws it, so what is asserted is the UMG button's
 * own events, as SButton raises them: hovered as the pointer arrives, pressed on the press and clicked on the release
 * (SButton.cpp, OnMouseButtonDown and OnMouseButtonUp), for the mouse and for a finger, which Slate hands on as a mouse
 * press when the touch is not taken as one (FSlateApplication's touch fallback).
 *
 * NonNullRHI: a null renderer draws nothing, so the hit grid stays empty and no press reaches the button.
 */
namespace DreamPieUMGHostTestLocal
{
	FWaitTimeout ConditionLimit()
	{
		return FWaitTimeout::InSeconds(3.0);
	}

	FWaitTimeout StepLimit()
	{
		return FWaitTimeout::InSeconds(4.0);
	}

	/** What a test keeps of the play world between steps: weakly. */
	struct FHosted
	{
		TWeakObjectPtr<UDreamWidget> Surface;
		TWeakObjectPtr<UDreamUMGWidget> Visual;
		TWeakObjectPtr<UDreamPieUMGButtonPage> Page;
	};

	/**
	 * A 300 by 120 surface on the session's screen whose visual is a UMG widget showing a page that is one UButton, the
	 * bridge on the surface, and the button's four events to InCounter. Drawn whatever the editor thinks of the viewport's
	 * visibility (TickWhenOffscreen), since a session played off screen is still a session.
	 */
	void BuildHostedButton(FDreamDriverPieRig& InRig, UDreamPieUMGButtonCounter* InCounter, FHosted& OutHosted)
	{
		UDreamWidget* Surface = InRig.MakeWidget(TEXT("Surface"), nullptr, FVector2D(300.0, 120.0));
		if (Surface == nullptr)
		{
			return;
		}
		Surface->RemoveVisual();
		UDreamUMGWidget* Visual = Surface->CreateNewVisual<UDreamUMGWidget>();
		UDreamPieUMGButtonPage* Page = CreateWidget<UDreamPieUMGButtonPage>(InRig.GetPlayerController(), UDreamPieUMGButtonPage::StaticClass());
		if (Visual == nullptr || Page == nullptr)
		{
			return;
		}
		Visual->SetTickWhenOffscreen(true);
		// Taking the page's Slate widget is what builds its button.
		Visual->SetWidget(Page);
		InCounter->Listen(Page->HostedButton);
		Surface->AddComponent<UDreamUMGWidgetInteraction>();
		OutHosted.Surface = Surface;
		OutHosted.Visual = Visual;
		OutHosted.Page = Page;
	}

	/** Whether the hosted page has been drawn into its texture at least once -- the draw that fills its window's hit grid. */
	bool HasBeenDrawn(const FHosted& InHosted)
	{
		return InHosted.Visual.IsValid() && InHosted.Visual->GetRenderTarget() != nullptr && InHosted.Page.IsValid() && InHosted.Page->HostedButton != nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieUMGMouseTest,
	"DreamGUI.Pie.RHI.AUMGButtonHostedInTheUIIsHoveredPressedAndClickedByTheMouseThroughTheBridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieUMGMouseTest, "DreamGUI.Pie.RHI.AUMGButtonHostedInTheUIIsHoveredPressedAndClickedByTheMouseThroughTheBridge", "[Pointer][Animated]")

/*
 * The mouse onto the surface, pressed and let go there: the UMG button heard the hover, the press, the release and the
 * click, once each -- the click a UMG graph bound to OnClicked would run on.
 */
bool FDreamPieUMGMouseTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieUMGHostTestLocal;
	const TStrongObjectPtr<UDreamPieUMGButtonCounter> Counter(NewObject<UDreamPieUMGButtonCounter>());
	const TSharedRef<FHosted> Hosted = MakeShared<FHosted>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([this, Counter, Hosted](FDreamDriverPieRig& InRig)
	{
		BuildHostedButton(InRig, Counter.Get(), *Hosted);
		TestTrue(TEXT("A surface hosting a UMG button, with the bridge on it"), Hosted->Surface.IsValid() && Hosted->Page.IsValid());
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Wait(FDreamUntil::Condition([Hosted]() { return HasBeenDrawn(*Hosted); }, ConditionLimit()), StepLimit(), TEXT("the hosted page to be drawn"))
		.WaitFrames(1)
		.MoveTo(Rig->Made(TEXT("Surface")))
		.Wait(FDreamUntil::Condition([Counter]() { return Counter->HoveredCount > 0; }, ConditionLimit()), StepLimit(),
			TEXT("the UMG button to hear the pointer arrive"))
		.Press()
		.Wait(FDreamUntil::Condition([Counter]() { return Counter->PressedCount > 0; }, ConditionLimit()), StepLimit(),
			TEXT("the UMG button to be pressed"))
		.Release()
		.Wait(FDreamUntil::Condition([Counter]() { return Counter->ClickedCount > 0; }, ConditionLimit()), StepLimit(),
			TEXT("the UMG button to be clicked"))
		.Then([this, Counter](FDreamDriverContext&)
		{
			TestEqual(TEXT("The UMG button was hovered once"), Counter->HoveredCount, 1);
			TestEqual(TEXT("...pressed once"), Counter->PressedCount, 1);
			TestEqual(TEXT("...released once"), Counter->ReleasedCount, 1);
			TestEqual(TEXT("...and clicked once"), Counter->ClickedCount, 1);
		})
		.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieUMGFingerTest,
	"DreamGUI.Pie.RHI.AFingerOnAUMGButtonHostedInTheUIPressesItAndLiftingItClicksIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieUMGFingerTest, "DreamGUI.Pie.RHI.AFingerOnAUMGButtonHostedInTheUIPressesItAndLiftingItClicksIt", "[Touch][Animated]")

/*
 * A finger through the player controller onto the surface and lifted there: the bridge sends it as a touch of its own,
 * Slate hands the touch on to SButton as a press, and the lift is the release and the click -- once each.
 */
bool FDreamPieUMGFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieUMGHostTestLocal;
	const TStrongObjectPtr<UDreamPieUMGButtonCounter> Counter(NewObject<UDreamPieUMGButtonCounter>());
	const TSharedRef<FHosted> Hosted = MakeShared<FHosted>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([this, Counter, Hosted](FDreamDriverPieRig& InRig)
	{
		BuildHostedButton(InRig, Counter.Get(), *Hosted);
		TestTrue(TEXT("A surface hosting a UMG button, with the bridge on it"), Hosted->Surface.IsValid() && Hosted->Page.IsValid());
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Wait(FDreamUntil::Condition([Hosted]() { return HasBeenDrawn(*Hosted); }, ConditionLimit()), StepLimit(), TEXT("the hosted page to be drawn"))
		.WaitFrames(1)
		.TouchDownAtResolvedPixel(0, [Hosted](FDreamDriverContext& InContext) -> TOptional<FVector2D>
		{
			return Hosted->Surface.IsValid() ? FDreamDriverProjection::WidgetCentrePixel(Hosted->Surface.Get(), InContext.Camera.Get()) : TOptional<FVector2D>();
		}, TEXT("the middle of the surface"))
		.Wait(FDreamUntil::Condition([Counter]() { return Counter->PressedCount > 0; }, ConditionLimit()), StepLimit(),
			TEXT("the finger to press the UMG button"))
		.TouchUp(0)
		.Wait(FDreamUntil::Condition([Counter]() { return Counter->ClickedCount > 0; }, ConditionLimit()), StepLimit(),
			TEXT("lifting the finger to click the UMG button"))
		.Then([this, Counter](FDreamDriverContext&)
		{
			TestEqual(TEXT("The finger pressed the UMG button once"), Counter->PressedCount, 1);
			TestEqual(TEXT("...lifting it released it once"), Counter->ReleasedCount, 1);
			TestEqual(TEXT("...and clicked it once"), Counter->ClickedCount, 1);
		})
		.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
