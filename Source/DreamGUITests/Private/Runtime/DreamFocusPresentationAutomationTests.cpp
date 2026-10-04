// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIInputServices.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputTypes.h"
#include "Event/DreamUIInputUser.h"
#include "GenericPlatform/InputDeviceRegistry.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"

/*
 * WHERE THE PLAYER'S FOCUS COMES FROM, AND WHAT IT LOOKS LIKE.
 *
 * The mouse and the keys share the navigation cursor (pointer 0), and the mouse used to win every argument about it: a
 * hover rewrote the highlight whether or not the mouse had moved, and a pointer nothing had placed sat at the top-left
 * pixel, hovering whatever was there. Round 5 gives the hover the highlight only after real motion, starts every pointer
 * off the viewport, records what moved the focus (keys, a pointer or code) and draws it only when keys or a pad put it
 * there, reads the pad's model from the pad that was used, makes no player for an index nobody is, and keeps Slate's
 * own navigation out of UMG through the game viewport client's navigation override. Headless, on the driver's rig.
 */
namespace DreamFocusPresentationTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D ButtonSize(200.0, 60.0);

	UDreamWidget* FaceOf(const UDreamButton* InButton)
	{
		return InButton != nullptr ? InButton->FaceNode.Get() : nullptr;
	}

	/** Whether InWidget is InControl or one of its parts: a hover lands on whatever part of a button is under the pointer. */
	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	UDreamWidget* HighlightOf(const FDreamDriverRig& InRig)
	{
		const UDreamEventSystem* Events = InRig.EventSystem();
		return Events != nullptr ? Events->GetHighlightedComponentForNavigation(0) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPointerHoverAtRestTest,
	"DreamGUI.Input.Pointer.HoverWithoutMouseMotionLeavesTheNavigationHighlightWhereItIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Every Enter a pointer was dispatched wrote the navigation highlight, so a button that appeared under a mouse at rest --
 * a screen opening, a list scrolling under it -- took the highlight off where the keys had left it. The hover moves the
 * highlight only when the pointer has really moved since its last trace now. The mouse rests where a hidden button is;
 * the focus goes to the other button; the hidden one is shown under the resting mouse: it is hovered, and the highlight
 * stays. Then the mouse moves, and the hover takes the highlight as it always did.
 */
bool FDreamPointerHoverAtRestTest::RunTest(const FString& Parameters)
{
	using namespace DreamFocusPresentationTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Focused = Rig.MakeControl<UDreamButton>(TEXT("Focused"), nullptr, ButtonSize, FVector2D(-300.0, 0.0));
	UDreamButton* Appearing = Rig.MakeControl<UDreamButton>(TEXT("Appearing"), nullptr, ButtonSize, FVector2D(300.0, 0.0));
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("Two buttons and the world's input"), FaceOf(Focused) != nullptr && FaceOf(Appearing) != nullptr && Services != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const TOptional<FVector2D> OnFocused = FDreamDriverProjection::WidgetCentrePixel(FaceOf(Focused));
	const TOptional<FVector2D> OnAppearing = FDreamDriverProjection::WidgetCentrePixel(FaceOf(Appearing));
	if (!TestTrue(TEXT("Both are somewhere the mouse can go"), OnFocused.IsSet() && OnAppearing.IsSet()))
	{
		return false;
	}
	Appearing->SetVisibility(EDreamWidgetVisibility::Hidden);
	Rig.PumpFrames(1);
	FDreamDriverRef Driver = Rig.Driver();
	TestTrue(TEXT("The mouse comes to rest where the hidden button is"), Driver->Sequence().MoveToPixel(OnAppearing.GetValue()).WaitFrames(1).Perform());
	TestFalse(TEXT("A hidden button is not hovered"), Services->IsHovered(FaceOf(Appearing), 0));
	if (!TestTrue(TEXT("The other button takes the focus"), Services->FocusForNavigation(FaceOf(Focused), 0)))
	{
		return false;
	}
	TestTrue(TEXT("...and the highlight with it"), IsPartOf(HighlightOf(Rig), FaceOf(Focused)));

	Appearing->SetVisibility(EDreamWidgetVisibility::Visible);
	Rig.PumpFrames(2);
	TestTrue(TEXT("The button shown under the resting mouse is hovered"), Services->IsHovered(FaceOf(Appearing), 0));
	TestTrue(TEXT("...and the highlight stays where the focus put it"), IsPartOf(HighlightOf(Rig), FaceOf(Focused)));

	TestTrue(TEXT("The mouse moves off and back"), Driver->Sequence()
		.MoveToPixel(OnFocused.GetValue()).WaitFrames(1)
		.MoveToPixel(OnAppearing.GetValue()).WaitFrames(1)
		.Perform());
	TestTrue(TEXT("A mouse that moved onto the button takes the highlight there"), IsPartOf(HighlightOf(Rig), FaceOf(Appearing)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPointerStartsOffViewportTest,
	"DreamGUI.Input.Pointer.ANewPointerStartsOffTheViewportAndHoversNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A pointer was born at (0,0), the top-left pixel -- the navigation cursor's, made by a focus or a key before any mouse
 * moved it, or a script's -- and was traced there every frame, hovering whatever was drawn in that corner. It is born off
 * the viewport now, and hovers nothing until something places it. A widget over the top-left pixel; a pointer placed at
 * (0,0) is over it (the premise); a new pointer nothing places is not.
 */
bool FDreamPointerStartsOffViewportTest::RunTest(const FString& Parameters)
{
	using namespace DreamFocusPresentationTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// Half a pixel past the viewport's top-left corner and no further: the top-left pixel is inside it, (-1,-1) is not.
	const double HalfWidth = 200.0;
	const double HalfHeight = 150.0;
	const FVector2D CornerCentre(-ViewportSize.X * 0.5 - 0.5 + HalfWidth, ViewportSize.Y * 0.5 + 0.5 - HalfHeight);
	UDreamWidget* Corner = Rig.MakeWidget(TEXT("Corner"), nullptr, FVector2D(HalfWidth * 2.0, HalfHeight * 2.0), CornerCentre);
	UDreamUIInputUser* User = Rig.EventSystem() != nullptr ? Rig.EventSystem()->GetInputUser() : nullptr;
	if (!TestTrue(TEXT("A widget over the top-left corner, and a player"), IsValid(Corner) && User != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);

	const int32 PlacedID = DreamUIPointerIds::ScriptBase + 1;
	const UDreamPointerEventData* Placed = User->GetPointerEventData(PlacedID, true);
	User->MovePointer(PlacedID, FVector::ZeroVector);
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("A pointer placed on the top-left pixel is over the corner widget"), Placed != nullptr && IsPartOf(Placed->EnterWidget, Corner)))
	{
		User->RetirePointer(PlacedID);
		return false;
	}
	User->RetirePointer(PlacedID);
	Rig.PumpFrames(1);

	const int32 FreshID = DreamUIPointerIds::ScriptBase + 2;
	const UDreamPointerEventData* Fresh = User->GetPointerEventData(FreshID, true);
	if (!TestNotNull(TEXT("A new pointer"), Fresh))
	{
		return false;
	}
	TestTrue(TEXT("A new pointer starts off the viewport"), Fresh->PointerPosition.Equals(DreamUIPointerPosition::OffViewport()));
	Rig.PumpFrames(2);
	TestFalse(TEXT("...and, traced there, is over nothing"), IsValid(Fresh->EnterWidget));
	TestEqual(TEXT("...hovering nothing at all"), Fresh->HoverComponentArray.Num(), 0);
	User->RetirePointer(FreshID);
	Rig.PumpFrames(1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFocusVisibleTest,
	"DreamGUI.Input.Focus.FocusIsDrawnAfterAKeyStepAndNotAfterAClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A clicked button was drawn focused, as one the keys had reached was: the focus look said nothing about how it got
 * there. Each player now records what last moved the focus -- Tab, a direction, a pointer, code -- and, as CSS's
 * :focus-visible, the focus is drawn when keys or a pad put it there, not after a click, and after code when the player
 * was last on the keys. The listeners hear each change.
 */
bool FDreamFocusVisibleTest::RunTest(const FString& Parameters)
{
	using namespace DreamFocusPresentationTestLocal;
	UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
	TGuardValue<bool> VisibleGuard(Settings->bFocusVisibleOnlyFromKeys, true);
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Left = Rig.MakeControl<UDreamButton>(TEXT("Left"), nullptr, ButtonSize, FVector2D(-250.0, 0.0));
	UDreamButton* Right = Rig.MakeControl<UDreamButton>(TEXT("Right"), nullptr, ButtonSize, FVector2D(250.0, 0.0));
	UDreamUIInputUser* User = Rig.EventSystem() != nullptr ? Rig.EventSystem()->GetInputUser() : nullptr;
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("Two buttons, a player and the world's input"), FaceOf(Left) != nullptr && FaceOf(Right) != nullptr && User != nullptr && Services != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	int32 Changes = 0;
	bool bLatestAnnounced = false;
	const FDelegateHandle Listening = User->GetFocusVisibleChangedEvent().AddLambda([&Changes, &bLatestAnnounced](bool bInVisible)
	{
		++Changes;
		bLatestAnnounced = bInVisible;
	});
	ON_SCOPE_EXIT{ User->GetFocusVisibleChangedEvent().Remove(Listening); };
	FDreamDriverRef Driver = Rig.Driver();

	TestTrue(TEXT("Tab completes"), Driver->Sequence().Tab().Perform());
	TestTrue(TEXT("Tab put the focus on a button"), IsPartOf(Services->GetFocusedWidget(0), Left) || IsPartOf(Services->GetFocusedWidget(0), Right));
	TestEqual(TEXT("...recorded as Tab's"), Services->GetFocusCause(0), EDreamUIFocusCause::Tab);
	TestTrue(TEXT("...and drawn"), Services->IsFocusVisible(0));
	TestTrue(TEXT("...as the player says too"), User->IsFocusVisible());
	TestTrue(TEXT("The listeners heard it is drawn"), Changes > 0 && bLatestAnnounced);

	const int32 ChangesBeforeClick = Changes;
	TestTrue(TEXT("A click on the right button completes"), Driver->Find(FDreamBy::Widget(FaceOf(Right)))->Click());
	TestEqual(TEXT("The click focused the right button"), Services->GetFocusedWidget(0), FaceOf(Right));
	TestEqual(TEXT("...recorded as the pointer's"), Services->GetFocusCause(0), EDreamUIFocusCause::Pointer);
	TestFalse(TEXT("...and not drawn"), Services->IsFocusVisible(0));
	TestTrue(TEXT("The listeners heard it is no longer drawn"), Changes > ChangesBeforeClick && !bLatestAnnounced);

	TestTrue(TEXT("The left arrow completes"), Driver->Sequence().Key(EKeys::Left).Perform());
	TestEqual(TEXT("The arrow moved the focus to the left button"), Services->GetFocusedWidget(0), FaceOf(Left));
	TestEqual(TEXT("...recorded as a step's"), Services->GetFocusCause(0), EDreamUIFocusCause::Navigation);
	TestTrue(TEXT("...and drawn again"), Services->IsFocusVisible(0) && bLatestAnnounced);

	TestTrue(TEXT("Code moves the focus"), Services->FocusForNavigation(FaceOf(Right), 0));
	TestEqual(TEXT("...recorded as code's"), Services->GetFocusCause(0), EDreamUIFocusCause::Script);
	TestTrue(TEXT("...and drawn, the player's last input being a key"), Services->IsFocusVisible(0));
	const FVector2D OnNothing(40.0, 40.0);
	TestTrue(TEXT("A click on nothing completes"), Driver->Sequence().MoveToPixel(OnNothing).Press().Release().Perform());
	TestEqual(TEXT("The focus is where code put it"), Services->GetFocusedWidget(0), FaceOf(Right));
	TestFalse(TEXT("...and not drawn once the player is on the mouse"), Services->IsFocusVisible(0));
	TestFalse(TEXT("...as the listeners heard"), bLatestAnnounced);

	Settings->bFocusVisibleOnlyFromKeys = false;
	TestTrue(TEXT("With the setting off every focus is drawn"), Services->IsFocusVisible(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPadModelFromLatestDeviceTest,
	"DreamGUI.Input.Device.ThePadModelIsReadFromTheDeviceThatSentTheLatestInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The pad's model was read from the player's lowest-numbered device -- on a desktop, the keyboard -- and only when the
 * device class turned to Gamepad, so a second pad of another make picked up mid-session kept the first one's glyphs. It
 * is read now from the pad that sent the input, whenever that is another pad, and the change event goes out. Two pads
 * the device registry is told about for the test, an Xbox and a PlayStation one.
 */
bool FDreamPadModelFromLatestDeviceTest::RunTest(const FString& Parameters)
{
	using namespace DreamFocusPresentationTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamUIInputUser* User = Rig.IsUsable() && Rig.EventSystem() != nullptr ? Rig.EventSystem()->GetInputUser() : nullptr;
	if (!TestNotNull(TEXT("A player"), User))
	{
		return false;
	}
	const FInputDeviceId XboxPad = FInputDeviceId::CreateFromInternalId(4101);
	const FInputDeviceId SonyPad = FInputDeviceId::CreateFromInternalId(4102);
	FInputDeviceDescriptor XboxDescriptor;
	XboxDescriptor.HardwareDeviceHandle = XboxPad;
	XboxDescriptor.InputDeviceName = TEXT("XInputInterface");
	XboxDescriptor.HardwareDeviceIdentifier = TEXT("XInputController");
	FInputDeviceDescriptor SonyDescriptor;
	SonyDescriptor.HardwareDeviceHandle = SonyPad;
	SonyDescriptor.InputDeviceName = TEXT("PS5Controller");
	SonyDescriptor.HardwareDeviceIdentifier = TEXT("DualSense");
	FInputDeviceRegistry::SetSimulatedDescriptor(XboxPad, XboxDescriptor);
	FInputDeviceRegistry::SetSimulatedDescriptor(SonyPad, SonyDescriptor);
	ON_SCOPE_EXIT
	{
		FInputDeviceRegistry::ClearSimulatedDescriptor(XboxPad);
		FInputDeviceRegistry::ClearSimulatedDescriptor(SonyPad);
	};
	int32 ModelChanges = 0;
	EDreamUIGamepadModel LatestModel = EDreamUIGamepadModel::Generic;
	const FDelegateHandle Listening = User->GetGamepadModelChangedEvent().AddLambda([&ModelChanges, &LatestModel](EDreamUIGamepadModel InModel)
	{
		++ModelChanges;
		LatestModel = InModel;
	});
	ON_SCOPE_EXIT{ User->GetGamepadModelChangedEvent().Remove(Listening); };
	User->ReportInputDevice(EDreamUIInputDevice::MouseAndKeyboard);
	const int32 ChangesBefore = ModelChanges;

	User->ReportInputDevice(EDreamUIInputDevice::Gamepad, XboxPad);
	TestEqual(TEXT("Picking up the Xbox pad reads its model"), User->GetCurrentGamepadModel(), EDreamUIGamepadModel::Xbox);
	TestTrue(TEXT("...and says so"), ModelChanges > ChangesBefore && LatestModel == EDreamUIGamepadModel::Xbox);

	const int32 ChangesAfterXbox = ModelChanges;
	User->ReportInputDevice(EDreamUIInputDevice::Gamepad, SonyPad);
	TestEqual(TEXT("A press on the PlayStation pad, the device class still Gamepad, reads that pad's model"),
		User->GetCurrentGamepadModel(), EDreamUIGamepadModel::PlayStation);
	TestEqual(TEXT("...and says so, once"), ModelChanges, ChangesAfterXbox + 1);
	TestEqual(TEXT("...to the glyphs' listeners"), LatestModel, EDreamUIGamepadModel::PlayStation);

	User->ReportInputDevice(EDreamUIInputDevice::Gamepad, SonyPad);
	TestEqual(TEXT("Another press on the same pad says nothing"), ModelChanges, ChangesAfterXbox + 1);
	User->ReportInputDevice(EDreamUIInputDevice::Gamepad);
	TestEqual(TEXT("...nor does a press whose source does not name its pad, after one that did"), User->GetCurrentGamepadModel(), EDreamUIGamepadModel::PlayStation);

	User->SetGamepadModelOverride(true, EDreamUIGamepadModel::Switch);
	User->ReportInputDevice(EDreamUIInputDevice::Gamepad, XboxPad);
	TestEqual(TEXT("An override outranks the pad in the player's hands"), User->GetCurrentGamepadModel(), EDreamUIGamepadModel::Switch);
	User->SetGamepadModelOverride(false, EDreamUIGamepadModel::Generic);
	TestEqual(TEXT("Dropping it reads the pad used last, the one used under the override"), User->GetCurrentGamepadModel(), EDreamUIGamepadModel::Xbox);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNoPhantomPlayersTest,
	"DreamGUI.Input.Players.NoInputUserIsMadeForAnIndexWithNoPlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * FocusForNavigation made the player it was asked about whenever there was none: a scope set to player 3 in a one-player
 * game, or a Blueprint asking for player 3's event system, left a phantom player behind, counted among the players and
 * holding a focus nobody could see or move. A player is made now only for an index that is somebody's -- a local player,
 * an event system placed for it, a player made on purpose, or the first player a world always has.
 */
bool FDreamNoPhantomPlayersTest::RunTest(const FString& Parameters)
{
	using namespace DreamFocusPresentationTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, ButtonSize);
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("A button and the world's input"), FaceOf(Button) != nullptr && Input != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const int32 Nobody = 3;
	TestTrue(TEXT("The first player is somebody"), Input->HasPlayerAt(0));
	TestFalse(TEXT("Player 3 is nobody here"), Input->HasPlayerAt(Nobody));

	TestFalse(TEXT("Focus for player 3 is refused"), Input->FocusForNavigation(FaceOf(Button), Nobody));
	TestNull(TEXT("...and made no player 3"), Input->GetUser(Nobody));
	TestNull(TEXT("A Blueprint asking for player 3's event system is told there is none"),
		UDreamEventSystem::GetDreamEventSystemInstance(Rig.GetWorld(), Nobody));
	TestNull(TEXT("...and that made no player 3 either"), Input->GetUser(Nobody));
	TArray<int32> Players;
	Input->GetUserIndices(Players);
	TestFalse(TEXT("Player 3 is not among the players"), Players.Contains(Nobody));

	// A player made on purpose -- a test's second player, a project's script user -- is somebody.
	if (!TestNotNull(TEXT("Player 3 made on purpose"), Input->GetOrCreateUser(Nobody)))
	{
		return false;
	}
	ON_SCOPE_EXIT{ Input->RemoveUser(Nobody); };
	TestTrue(TEXT("...is somebody"), Input->HasPlayerAt(Nobody));
	TestTrue(TEXT("...and takes the focus"), Input->FocusForNavigation(FaceOf(Button), Nobody));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateGuardChainTest,
	"DreamGUI.Input.SlateGuard.TheGuardIsChainedOntoTheNavigationOverrideAndGivesItBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * UGameViewportClient::OnNavigationOverride holds one binding, and the input subsystem chains its Slate guard onto it: a
 * project's own binding is kept and asked whenever the guard lets a navigation through, and given back when the world
 * goes -- but a binding the project made since is left alone. Checked on a viewport client made for the test, whose
 * navigation the guard always lets through (it has no viewport for Slate's focus to be on).
 */
bool FDreamSlateGuardChainTest::RunTest(const FString& Parameters)
{
	using namespace DreamFocusPresentationTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamUIInputSubsystem* Input = Rig.IsUsable() ? UDreamUIInputSubsystem::Get(Rig.GetWorld()) : nullptr;
	if (!TestNotNull(TEXT("The world's input"), Input) || !TestNotNull(TEXT("The engine"), GEngine))
	{
		return false;
	}
	// Inside the engine, as every viewport client is: the class is declared Within UEngine, and any other outer is refused
	// with an ensure.
	TStrongObjectPtr<UGameViewportClient> Client(NewObject<UGameViewportClient>(GEngine, NAME_None, RF_Transient));
	int32 ProjectCalls = 0;
	Client->OnNavigationOverride().BindLambda([&ProjectCalls](const uint32, TSharedPtr<SWidget>)
	{
		++ProjectCalls;
		return false;
	});
	ON_SCOPE_EXIT
	{
		Input->UnbindSlateNavigationGuard();
		Client->OnNavigationOverride().Unbind();
	};

	Input->BindSlateNavigationGuard(Client.Get());
	TestTrue(TEXT("The guard is what the delegate calls now"), Client->OnNavigationOverride().IsBoundToObject(Input));
	TestFalse(TEXT("A navigation the guard lets through..."), Client->OnNavigationOverride().Execute(0, nullptr));
	TestEqual(TEXT("...is asked of the project's own binding"), ProjectCalls, 1);

	Input->UnbindSlateNavigationGuard();
	TestFalse(TEXT("Unbound, the guard is gone"), Client->OnNavigationOverride().IsBoundToObject(Input));
	TestTrue(TEXT("...and the project's binding is back"), Client->OnNavigationOverride().IsBound());
	Client->OnNavigationOverride().Execute(0, nullptr);
	TestEqual(TEXT("...answering on its own"), ProjectCalls, 2);

	// A binding the project makes after the guard was chained is the project's, and stays when the guard goes.
	Input->BindSlateNavigationGuard(Client.Get());
	int32 LaterCalls = 0;
	Client->OnNavigationOverride().BindLambda([&LaterCalls](const uint32, TSharedPtr<SWidget>)
	{
		++LaterCalls;
		return true;
	});
	Input->UnbindSlateNavigationGuard();
	TestTrue(TEXT("A binding made after the guard stays when the guard goes"), Client->OnNavigationOverride().Execute(0, nullptr));
	TestEqual(TEXT("...and is the one asked"), LaterCalls, 1);
	TestEqual(TEXT("...not the one the guard had kept"), ProjectCalls, 2);
	return true;
}

#endif
