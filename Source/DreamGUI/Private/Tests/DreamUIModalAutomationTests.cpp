// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUserWidget.h"
#include "Engine/World.h"
#include "Interaction/DreamUIModal.h"
#include "Tests/DreamDialogTestTypes.h"

/*
 * Modals stack.
 *
 * The service was written around exactly one modal at a time: a second ShowModal while one was up
 * went into a queue and did not open until the first had closed. The interface never said so -- the
 * close verb is called CloseTopModal, which promises a stack -- and the queue is wrong for the case
 * that produces nested modals in the first place: a settings dialog raising "discard your changes?".
 * Queued, that confirmation appears AFTER the dialog that asked for it has already gone, with its
 * answer delivered to a caller that no longer has anything to do with it.
 *
 * What is pinned here: the second modal opens immediately and on top, the first is still alive
 * underneath and still waiting for its own result, closing delivers to the right caller in the right
 * order, and nothing is delivered twice.
 */

namespace DreamUIModalTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	FDreamUIModalResultDynamicDelegate ResultTo(UDreamDialogResultProbe* InProbe)
	{
		FDreamUIModalResultDynamicDelegate Delegate;
		Delegate.BindUFunction(InProbe, TEXT("Record"));
		return Delegate;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIModalNestingTest,
	"DreamGUI.Modal.ASecondModalOpensOnTopOfTheFirstRatherThanBehindIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIModalNestingTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIModalTestLocal;
	FScopedGameWorld TestWorld;

	UDreamUIModalSubsystem* Modals = TestWorld.World->GetSubsystem<UDreamUIModalSubsystem>();
	if (!TestNotNull(TEXT("the modal subsystem exists in a game world"), Modals))
	{
		return false;
	}

	UDreamDialogResultProbe* OuterProbe = NewObject<UDreamDialogResultProbe>(GetTransientPackage());
	UDreamDialogResultProbe* InnerProbe = NewObject<UDreamDialogResultProbe>(GetTransientPackage());

	Modals->ShowModal(UDreamUserWidget::StaticClass(), ResultTo(OuterProbe));
	UDreamUserWidget* OuterDialog = Modals->GetActiveModalWidget();
	if (!TestNotNull(TEXT("the first modal opened"), OuterDialog))
	{
		return false;
	}
	TestEqual(TEXT("one modal is up"), Modals->GetModalDepth(), 1);

	// The whole defect in one call: this used to leave the depth at one and put the dialog in a queue.
	Modals->ShowModal(UDreamUserWidget::StaticClass(), ResultTo(InnerProbe));
	TestEqual(TEXT("the second modal is up as well, not queued behind the first"), Modals->GetModalDepth(), 2);

	UDreamUserWidget* InnerDialog = Modals->GetActiveModalWidget();
	if (!TestNotNull(TEXT("the second modal opened"), InnerDialog))
	{
		return false;
	}
	TestTrue(TEXT("the top dialog is the second one, not still the first"), InnerDialog != OuterDialog);
	TestTrue(TEXT("...and the first is still alive underneath rather than torn down"), IsValid(OuterDialog));
	TestEqual(TEXT("neither caller has been answered yet"), OuterProbe->CallCount + InnerProbe->CallCount, 0);

	// Closing the top answers the caller that raised the top, and nobody else.
	Modals->CloseTopModal(TEXT("Confirm"));
	TestEqual(TEXT("the inner caller got its result"), InnerProbe->CallCount, 1);
	TestEqual(TEXT("...the one it asked for"), InnerProbe->LastResult, FName(TEXT("Confirm")));
	TestEqual(TEXT("the outer caller is still waiting"), OuterProbe->CallCount, 0);
	TestEqual(TEXT("one modal is left"), Modals->GetModalDepth(), 1);
	TestEqual(TEXT("...and it is the first one, which now has focus again"),
		Modals->GetActiveModalWidget(), OuterDialog);

	Modals->CloseTopModal(TEXT("Cancel"));
	TestEqual(TEXT("the outer caller got its own result"), OuterProbe->CallCount, 1);
	TestEqual(TEXT("...with its own name"), OuterProbe->LastResult, FName(TEXT("Cancel")));
	TestEqual(TEXT("the inner caller was not answered a second time"), InnerProbe->CallCount, 1);
	TestFalse(TEXT("nothing is modal any more"), Modals->IsModalActive());
	TestEqual(TEXT("the stack is empty"), Modals->GetModalDepth(), 0);

	// Closing with nothing up is ordinary input -- a Back press arriving one frame late -- and must
	// answer nobody rather than reach into an empty stack.
	Modals->CloseTopModal(TEXT("Back"));
	TestEqual(TEXT("closing an empty stack answers nobody"), OuterProbe->CallCount + InnerProbe->CallCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIModalCloseAllTest,
	"DreamGUI.Modal.ClosingEverythingAnswersEveryWaitingCallerExactlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIModalCloseAllTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIModalTestLocal;
	FScopedGameWorld TestWorld;

	UDreamUIModalSubsystem* Modals = TestWorld.World->GetSubsystem<UDreamUIModalSubsystem>();
	if (!TestNotNull(TEXT("the modal subsystem exists in a game world"), Modals))
	{
		return false;
	}

	UDreamDialogResultProbe* Probes[3] = {
		NewObject<UDreamDialogResultProbe>(GetTransientPackage()),
		NewObject<UDreamDialogResultProbe>(GetTransientPackage()),
		NewObject<UDreamDialogResultProbe>(GetTransientPackage()),
	};
	for (UDreamDialogResultProbe* Probe : Probes)
	{
		Modals->ShowModal(UDreamUserWidget::StaticClass(), ResultTo(Probe));
	}
	TestEqual(TEXT("three modals stacked up"), Modals->GetModalDepth(), 3);

	// "Back to the main menu" from three dialogs deep: every awaited result has to arrive, because
	// each one is a caller holding state until it does.
	Modals->CloseAllModals(TEXT("Dismissed"));
	TestEqual(TEXT("the stack is empty"), Modals->GetModalDepth(), 0);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestEqual(FString::Printf(TEXT("modal %d was answered exactly once"), Index), Probes[Index]->CallCount, 1);
		TestEqual(FString::Printf(TEXT("modal %d was answered with the result asked for"), Index),
			Probes[Index]->LastResult, FName(TEXT("Dismissed")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIModalPerUserTest,
	"DreamGUI.Modal.EachPlayerHasTheirOwnStackOnASplitScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIModalPerUserTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIModalTestLocal;
	FScopedGameWorld TestWorld;

	/*
	 * A modal is a per-PLAYER thing and there was one stack for the whole world. On a split screen
	 * that meant player one's "are you sure?" scrimmed player two's half of the display, took player
	 * two's gamepad focus with its navigation scope, and was closed by player two's Back.
	 */
	UDreamUIModalSubsystem* Modals = TestWorld.World->GetSubsystem<UDreamUIModalSubsystem>();
	if (!TestNotNull(TEXT("the modal subsystem exists in a game world"), Modals))
	{
		return false;
	}

	UDreamDialogResultProbe* PlayerOne = NewObject<UDreamDialogResultProbe>(GetTransientPackage());
	UDreamDialogResultProbe* PlayerTwo = NewObject<UDreamDialogResultProbe>(GetTransientPackage());

	Modals->ShowModal(UDreamUserWidget::StaticClass(), ResultTo(PlayerOne), 0);
	TestEqual(TEXT("player one has a modal"), Modals->GetModalDepth(0), 1);
	TestEqual(TEXT("player two does not"), Modals->GetModalDepth(1), 0);
	TestFalse(TEXT("...and is not considered to be in one"), Modals->IsModalActive(1));
	TestTrue(TEXT("...though the world does have one somewhere"), Modals->IsAnyModalActive());

	Modals->ShowModal(UDreamUserWidget::StaticClass(), ResultTo(PlayerTwo), 1);
	TestEqual(TEXT("each player is one deep, not one player two deep"), Modals->GetModalDepth(0), 1);
	TestEqual(TEXT("...on both stacks"), Modals->GetModalDepth(1), 1);
	TestTrue(TEXT("the two dialogs are different objects"),
		Modals->GetActiveModalWidget(0) != Modals->GetActiveModalWidget(1));

	// Player two closing theirs answers THEM, and leaves player one's dialog standing.
	Modals->CloseTopModal(TEXT("Cancel"), 1);
	TestEqual(TEXT("player two got their result"), PlayerTwo->CallCount, 1);
	TestEqual(TEXT("...the one they asked for"), PlayerTwo->LastResult, FName(TEXT("Cancel")));
	TestEqual(TEXT("player one was not answered"), PlayerOne->CallCount, 0);
	TestEqual(TEXT("...and their dialog is still up"), Modals->GetModalDepth(0), 1);
	TestFalse(TEXT("player two is out of their modal"), Modals->IsModalActive(1));

	// And closing everything for one player leaves the other alone.
	Modals->ShowModal(UDreamUserWidget::StaticClass(), ResultTo(PlayerTwo), 1);
	Modals->CloseAllModals(TEXT("Dismissed"), 0);
	TestEqual(TEXT("player one's stack is empty"), Modals->GetModalDepth(0), 0);
	TestEqual(TEXT("...and they were answered once"), PlayerOne->CallCount, 1);
	TestEqual(TEXT("player two's modal is untouched"), Modals->GetModalDepth(1), 1);

	Modals->CloseAllModals(TEXT("Dismissed"), 1);
	TestFalse(TEXT("nothing is modal anywhere now"), Modals->IsAnyModalActive());
	return true;
}

#endif
