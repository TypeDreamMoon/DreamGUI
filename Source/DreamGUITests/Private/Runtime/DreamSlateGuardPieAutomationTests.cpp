// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIInputServices.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Event/DreamUIInputModeLibrary.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputTypes.h"
#include "Event/DreamUIInputUser.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SViewport.h"

#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"

/*
 * THE SLATE GUARD, IN A REAL PLAY SESSION.
 *
 * Outside the level editor's own viewport -- a packaged game, Standalone, -game, a play session in a window of its own --
 * a Tab no binding handled became Slate's own Next, and from the bare game viewport Slate's Next descends into the
 * viewport's children, the UMG layers: the first focusable widget there took the keyboard focus, and DreamGUI heard no key
 * until the viewport had it back. While DreamGUI has the keys, its guard answers Slate's navigation now (the input
 * subsystem's link on the viewport client's OnNavigationOverride, and UDreamGameViewportClient::HandleNavigation), and the
 * focus stays on the viewport. The session plays in a window of its own: in the level editor's viewport SLevelViewport
 * takes every key, and Slate never navigates. On a real RHI, which lays the window out.
 *
 * The same session checks the cursor DreamGUI shows in its UI-only input mode: a pad hides it, the mouse brings it back.
 */
namespace DreamSlateGuardPieTestLocal
{
	/** What the steps share. Weak or Slate's own: nothing here may hold the play world up. */
	struct FState
	{
		TWeakObjectPtr<UDreamButton> First;
		TWeakObjectPtr<UDreamButton> Second;
		TWeakObjectPtr<UGameViewportClient> Client;
		TSharedPtr<SButton> SlateButton;
	};

	UDreamWidget* FaceOf(const TWeakObjectPtr<UDreamButton>& InButton)
	{
		const UDreamButton* Button = InButton.Get();
		return Button != nullptr ? Button->FaceNode.Get() : nullptr;
	}

	SWidget* ViewportWidgetOf(const FState& InState)
	{
		const UGameViewportClient* Client = InState.Client.Get();
		const TSharedPtr<SViewport> Viewport = Client != nullptr ? Client->GetGameViewportWidget() : nullptr;
		return Viewport.Get();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateGuardPieTest,
	"DreamGUI.Pie.RHI.TabOnTheBareViewportLeavesSlatesFocusThereWhileDreamGUIHasTheKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamSlateGuardPieTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateGuardPieTestLocal;
	FDreamPieRigOptions Options;
	Options.Destination = EDreamPieViewportDestination::NewWindow;
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this, Options);
	const TSharedRef<FState> State = MakeShared<FState>();
	Rig->Start();
	Rig->WhenReady([State](FDreamDriverPieRig& InRig)
	{
		State->First = InRig.MakeControl<UDreamButton>(TEXT("First"), nullptr, FVector2D(200.0, 60.0), FVector2D(-150.0, 0.0));
		State->Second = InRig.MakeControl<UDreamButton>(TEXT("Second"), nullptr, FVector2D(200.0, 60.0), FVector2D(150.0, 0.0));
		// A focusable widget of Slate's own on the viewport, as a UMG button would be: what Slate's Tab moved the focus into.
		if (UGameViewportClient* Client = InRig.GetViewportClient())
		{
			State->SlateButton = SNew(SButton);
			Client->AddViewportWidgetContent(State->SlateButton.ToSharedRef(), 10);
			State->Client = Client;
		}
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Then([this, State](FDreamDriverContext& InContext)
	{
		// DreamGUI has the keys -- its first button focused -- and Slate's keyboard focus is on the bare viewport.
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InContext.World);
		TestTrue(TEXT("DreamGUI's first button takes the focus"), Services != nullptr && Services->FocusForNavigation(FaceOf(State->First), 0));
		TestTrue(TEXT("A focusable Slate widget is on the viewport"), State->SlateButton.IsValid() && State->Client.IsValid());
		const UGameViewportClient* Client = State->Client.Get();
		const TSharedPtr<SViewport> Viewport = Client != nullptr ? Client->GetGameViewportWidget() : nullptr;
		if (TestTrue(TEXT("The play session has a viewport widget"), Viewport.IsValid()))
		{
			FSlateApplication::Get().SetUserFocus(0, Viewport, EFocusCause::SetDirectly);
		}
	});
	Steps.WaitFrames(1);
	Steps.Then([this, State](FDreamDriverContext&)
	{
		TestTrue(TEXT("Slate's keyboard focus is on the bare viewport before Tab"),
			ViewportWidgetOf(*State) != nullptr && FSlateApplication::Get().GetUserFocusedWidget(0).Get() == ViewportWidgetOf(*State));
	});
	FDreamDriverPieRig::KeyThroughSlate(Steps, EKeys::Tab);
	Steps.WaitFrames(2);
	Steps.Then([this, State](FDreamDriverContext& InContext)
	{
		const SWidget* Focused = FSlateApplication::Get().GetUserFocusedWidget(0).Get();
		TestTrue(TEXT("After Tab, Slate's keyboard focus is still on the bare viewport"), ViewportWidgetOf(*State) != nullptr && Focused == ViewportWidgetOf(*State));
		TestFalse(TEXT("...not on the focusable Slate widget its own Tab would have moved it to"),
			State->SlateButton.IsValid() && Focused == State->SlateButton.Get());
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InContext.World);
		TestTrue(TEXT("DreamGUI's own Tab moved its focus on to the second button"),
			Services != nullptr && Services->GetFocusedWidget(0) == FaceOf(State->Second));
	});
	Steps.Then([this](FDreamDriverContext& InContext)
	{
		// DreamGUI's UI-only mode: DreamGUI shows the cursor, a pad hides it and the mouse brings it back.
		UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
		const bool bHideBefore = Settings->bHideCursorOnGamepad;
		Settings->bHideCursorOnGamepad = true;
		APlayerController* Controller = InContext.PlayerController;
		UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(InContext.World);
		UDreamUIInputUser* User = Input != nullptr ? Input->GetUser(0) : nullptr;
		if (TestTrue(TEXT("A player and its controller"), Controller != nullptr && User != nullptr))
		{
			User->ReportInputDevice(EDreamUIInputDevice::MouseAndKeyboard);
			UDreamUIInputModeLibrary::SetInputModeUIOnly(InContext.World, nullptr, 0);
			TestTrue(TEXT("DreamGUI's UI-only mode shows the cursor for the mouse"), Controller->bShowMouseCursor != 0);
			User->ReportInputDevice(EDreamUIInputDevice::Gamepad);
			TestFalse(TEXT("A pad hides it"), Controller->bShowMouseCursor != 0);
			User->ReportInputDevice(EDreamUIInputDevice::MouseAndKeyboard);
			TestTrue(TEXT("The mouse brings it back"), Controller->bShowMouseCursor != 0);
			UDreamUIInputModeLibrary::SetInputModeGameAndUI(InContext.World, nullptr, 0);
			User->ReportInputDevice(EDreamUIInputDevice::Gamepad);
			TestTrue(TEXT("Outside DreamGUI's UI-only mode the cursor is the game's: a pad leaves it shown"), Controller->bShowMouseCursor != 0);
			User->ReportInputDevice(EDreamUIInputDevice::MouseAndKeyboard);
		}
		Settings->bHideCursorOnGamepad = bHideBefore;
	});
	Steps.Then([State](FDreamDriverContext&)
	{
		if (UGameViewportClient* Client = State->Client.Get(); Client != nullptr && State->SlateButton.IsValid())
		{
			Client->RemoveViewportWidgetContent(State->SlateButton.ToSharedRef());
		}
		State->SlateButton.Reset();
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
