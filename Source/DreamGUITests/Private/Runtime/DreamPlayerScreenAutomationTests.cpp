// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamPlayerScreenTestTypes.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamKeyEventData.h"
#include "Event/Interface/DreamKeyInterface.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "DreamScopedWorld.h"

/*
 * One screen per local player, and the input surface that goes with it.
 *
 * DreamGUI had exactly one screen root per world and forbade a second, which is the same statement as
 * "no split screen": a second local player's UI IS a second overlay canvas. These pin the three parts
 * that make it work -- a root per player, a stack per player that does not cover anyone else's, and a
 * widget that knows which player it belongs to so nothing has to be told.
 */

namespace DreamPlayerScreenTestLocal
{
	using DreamTests::FScopedGameWorld;

	UDreamScreenUISubsystem* GetScreen(UWorld* InWorld)
	{
		return IsValid(InWorld) ? InWorld->GetSubsystem<UDreamScreenUISubsystem>() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayerScreenPerPlayerRootTest,
	"DreamGUI.Screen.EachLocalPlayerGetsAScreenOfItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayerScreenPerPlayerRootTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayerScreenTestLocal;
	FScopedGameWorld TestWorld;
	UDreamScreenUISubsystem* Screen = GetScreen(TestWorld.World);
	if (!TestNotNull(TEXT("the screen subsystem exists in a game world"), Screen))
	{
		return false;
	}

	UDreamWidget* FirstRoot = Screen->GetOrCreateScreenRootForUserIndex(0);
	UDreamWidget* SecondRoot = Screen->GetOrCreateScreenRootForUserIndex(1);
	if (!TestNotNull(TEXT("the first player has a screen"), FirstRoot)
		|| !TestNotNull(TEXT("and so does the second"), SecondRoot))
	{
		return false;
	}
	TestNotEqual(TEXT("which is not the same screen"), (const UDreamWidget*)FirstRoot, (const UDreamWidget*)SecondRoot);
	TestEqual(TEXT("asking again gives the same one back"),
		(const UDreamWidget*)Screen->GetOrCreateScreenRootForUserIndex(1), (const UDreamWidget*)SecondRoot);

	const TArray<int32> Indices = Screen->GetScreenPlayerIndices();
	TestEqual(TEXT("both players are on the list"), Indices.Num(), 2);
	TestTrue(TEXT("...the first"), Indices.Contains(0));
	TestTrue(TEXT("...and the second"), Indices.Contains(1));

	// A widget already sitting on a player's screen belongs to that player, whatever it is: the
	// structural answer comes first, so an overlay service placing a tooltip beside a plain widget
	// lands on the right screen with nobody passing a player around.
	UDreamWidget* SecondPlayerChild = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	SecondPlayerChild->SetDisplayName(TEXT("OnSecondScreen"));
	// Parent, THEN register -- the order the name states and the one every production path follows.
	// SetParentBeforeRegister asserts !bIsRegistered, so registering first takes the editor down.
	SecondPlayerChild->SetParentBeforeRegister(SecondRoot);
	RegisterDreamWidgetHierarchy(SecondPlayerChild);
	TestEqual(TEXT("an overlay for a widget on the second screen goes to the second screen"),
		(const UDreamWidget*)Screen->GetOrCreateScreenRootForWidget(SecondPlayerChild), (const UDreamWidget*)SecondRoot);

	Screen->RemoveAllUI();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayerScreenOwningPlayerResolutionTest,
	"DreamGUI.UserWidget.AWidgetInheritsItsHostsOwningPlayerUnlessItWasToldOtherwise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayerScreenOwningPlayerResolutionTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayerScreenTestLocal;
	FScopedGameWorld TestWorld;

	APlayerController* FirstPlayer = TestWorld.World->SpawnActor<APlayerController>();
	APlayerController* SecondPlayer = TestWorld.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("a controller to own things"), FirstPlayer)
		|| !TestNotNull(TEXT("and a second one"), SecondPlayer))
	{
		return false;
	}

	UDreamUserWidget* Host = NewObject<UDreamUserWidget>(TestWorld.World, UDreamUserWidget::StaticClass());
	UDreamUserWidget* Nested = NewObject<UDreamUserWidget>(TestWorld.World, UDreamUserWidget::StaticClass());
	// Build the hierarchy, then bring it to life, which is what every production path does and what
	// SetParentBeforeRegister's own check(!bIsRegistered) demands.
	Nested->SetParentBeforeRegister(Host);
	RegisterDreamWidgetHierarchy(Host);

	Host->SetOwningPlayer(FirstPlayer);
	TestEqual(TEXT("the host answers with the controller it was given"),
		(const APlayerController*)Host->GetOwningPlayer(), (const APlayerController*)FirstPlayer);
	// The inheritance that spares every nested widget, list cell and pushed page a SetOwningPlayer call.
	TestEqual(TEXT("a nested widget inherits it without being told"),
		(const APlayerController*)Nested->GetOwningPlayer(), (const APlayerController*)FirstPlayer);

	Nested->SetOwningPlayer(SecondPlayer);
	TestEqual(TEXT("and an explicit owner beats the inherited one"),
		(const APlayerController*)Nested->GetOwningPlayer(), (const APlayerController*)SecondPlayer);
	TestEqual(TEXT("without disturbing the host's"),
		(const APlayerController*)Host->GetOwningPlayer(), (const APlayerController*)FirstPlayer);

	Host->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayerScreenCustomPlacementTest,
	"DreamGUI.Screen.AHandPlacedPageIsNotDraggedBackToFullScreenByTheStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayerScreenCustomPlacementTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayerScreenTestLocal;
	FScopedGameWorld TestWorld;
	UDreamScreenUISubsystem* Screen = GetScreen(TestWorld.World);
	if (!TestNotNull(TEXT("the screen subsystem exists in a game world"), Screen))
	{
		return false;
	}

	UDreamWidget* Page = Screen->PushWidgetOfClass(TEXT("Window"), UDreamUserWidget::StaticClass(),
		EDreamUIScreenPageCachePolicy::KeepAlive, /*bHidePrevious*/false);
	UDreamUserWidget* Window = Cast<UDreamUserWidget>(Page);
	if (!TestNotNull(TEXT("the page was pushed"), Window))
	{
		return false;
	}

	// ConfigurePage forces anchors 0..1 and a zero inset on every refresh, which is right for a screen
	// and is what made a floating window impossible: the next push moved it back.
	Window->SetAnchorsInViewport(FVector2D(0.5, 0.5), FVector2D(0.5, 0.5));
	Window->SetDesiredSizeInViewport(FVector2D(400.0, 300.0));
	Window->SetPositionInViewport(FVector2D(120.0, -60.0));
	TestTrue(TEXT("the page is marked hand-placed"), Screen->GetPageHasCustomPlacement(Window));

	// Anything that refreshes the stack is the moment the old code undid the placement.
	UDreamWidget* Other = Screen->PushWidgetOfClass(TEXT("Other"), UDreamUserWidget::StaticClass(),
		EDreamUIScreenPageCachePolicy::KeepAlive, /*bHidePrevious*/false);
	TestNotNull(TEXT("a second page was pushed over it"), Other);
	Screen->PopUI();

	TestTrue(TEXT("the hand-placed size survived the refreshes"),
		Window->GetSizeDelta().Equals(FVector2D(400.0, 300.0)));
	TestTrue(TEXT("...and so did the position"),
		Window->GetAnchoredPosition().Equals(FVector2D(120.0, -60.0)));

	// And handing it back is an instruction, not a preference: full-bleed again immediately.
	Window->ClearPlacementInViewport();
	TestFalse(TEXT("the mark is gone"), Screen->GetPageHasCustomPlacement(Window));
	TestTrue(TEXT("and the page went back to full-bleed"), Window->GetSizeDelta().IsNearlyZero());

	Screen->RemoveAllUI();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayerScreenKeyBubbleTest,
	"DreamGUI.Key.AKeyBubblesUpFromTheFocusedWidgetAndStopsAtWhoeverKeepsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayerScreenKeyBubbleTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayerScreenTestLocal;
	FScopedGameWorld TestWorld;

	UDreamWidget* Parent = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	UDreamWidget* Child = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	Parent->SetDisplayName(TEXT("Parent"));
	Child->SetDisplayName(TEXT("Child"));
	// Attach first, register the whole thing afterwards. Registering a widget and THEN calling
	// SetParentBeforeRegister trips its check(!bIsRegistered), which is a fatal assertion rather than
	// a failed test: it takes the editor down and the rest of the suite with it.
	Child->SetParentBeforeRegister(Parent);
	RegisterDreamWidgetHierarchy(Parent);

	UDreamKeyRecordingBehaviour* ParentHandler =
		Cast<UDreamKeyRecordingBehaviour>(Parent->AddComponent(UDreamKeyRecordingBehaviour::StaticClass()));
	UDreamKeyRecordingBehaviour* ChildHandler =
		Cast<UDreamKeyRecordingBehaviour>(Child->AddComponent(UDreamKeyRecordingBehaviour::StaticClass()));
	if (!TestNotNull(TEXT("the parent has a key handler"), ParentHandler)
		|| !TestNotNull(TEXT("and so does the child"), ChildHandler))
	{
		return false;
	}

	// Nobody keeps it: the key reaches the focused widget and everything above it, which is what makes
	// "my screen has a shortcut" writeable on the screen rather than on every row in it.
	UDreamKeyEventData* Passing = NewObject<UDreamKeyEventData>(TestWorld.World);
	Passing->KeyEventType = EDreamUIKeyEventType::KeyDown;
	Passing->Key = EKeys::X;
	Passing->bIsPressed = true;
	TestFalse(TEXT("an unclaimed key is reported unhandled"), DreamUIKeyDispatch::DispatchBubbling(Child, Passing));
	TestEqual(TEXT("the focused widget saw it"), ChildHandler->KeyDownCount, 1);
	TestEqual(TEXT("and so did the one above it"), ParentHandler->KeyDownCount, 1);
	TestTrue(TEXT("with the key it was sent"), ChildHandler->LastKey == EKeys::X);
	TestEqual(TEXT("and the focused widget is recorded on the event"),
		(const UDreamWidget*)Passing->FocusedWidget, (const UDreamWidget*)Child);

	// The child keeps it: the walk stops, and the parent never hears about it. A key somebody took
	// must not also fire a bound action or move the navigation cursor, which is what the return says.
	ChildHandler->bKeepTheKey = true;
	UDreamKeyEventData* Claimed = NewObject<UDreamKeyEventData>(TestWorld.World);
	Claimed->KeyEventType = EDreamUIKeyEventType::KeyDown;
	Claimed->Key = EKeys::Escape;
	Claimed->bIsPressed = true;
	TestTrue(TEXT("a claimed key is reported handled"), DreamUIKeyDispatch::DispatchBubbling(Child, Claimed));
	TestEqual(TEXT("the focused widget saw the second one"), ChildHandler->KeyDownCount, 2);
	TestEqual(TEXT("the one above it did not"), ParentHandler->KeyDownCount, 1);

	// And each channel is its own: a key-down handler is not a character handler.
	UDreamKeyEventData* Character = NewObject<UDreamKeyEventData>(TestWorld.World);
	Character->KeyEventType = EDreamUIKeyEventType::KeyChar;
	Character->Character = TEXT("a");
	DreamUIKeyDispatch::DispatchBubbling(Child, Character);
	TestEqual(TEXT("the character went to the character channel"), ChildHandler->KeyCharCount, 1);
	TestEqual(TEXT("and not to the key-down one"), ChildHandler->KeyDownCount, 2);

	Parent->DestroyWidget();
	return true;
}

#endif
