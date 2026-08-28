// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Tests/DreamNavigationTestTypes.h"
#include "Engine/World.h"

/*
 * Back. There was none: Escape was bound to nothing, the gamepad's B did nothing, and the only place
 * either key appeared was inside UITextInput, where Escape was read and thrown away. Closing a screen
 * was something a Blueprint had to wire up per screen, with no notion of the screen in front.
 */

namespace DreamBackActionTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamBackHandlingScope* MakeScope(UWorld* World, const TCHAR* Name)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(100.0f);
		Widget->OnRegister();
		UDreamBackHandlingScope* Scope = Widget->AddComponent<UDreamBackHandlingScope>();
		Scope->SetActivateWhenEnabled(false);
		return Scope;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBackClosesTopScreenTest,
	"DreamGUI.Navigation.Back.ClosesOneScreenAtATime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBackClosesTopScreenTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackActionTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUINavigationStack* Stack = TestWorld.World->GetSubsystem<UDreamUINavigationStack>();
	if (!TestNotNull(TEXT("Navigation stack subsystem exists"), Stack))
	{
		return false;
	}

	TestFalse(TEXT("With nothing open, Back has nothing to do"), Stack->HandleBack(0));

	UDreamBackHandlingScope* Page = MakeScope(TestWorld.World, TEXT("Page"));
	UDreamBackHandlingScope* Dialog = MakeScope(TestWorld.World, TEXT("Dialog"));
	Page->ActivateScope();
	Dialog->ActivateScope();

	TestTrue(TEXT("Back is taken"), Stack->HandleBack(0));
	// One screen at a time: a Back that unwound the whole stack would drop the player out of the menu
	// entirely on a single press.
	TestEqual(TEXT("...and closes only the dialog"), Stack->GetActiveScope(0), static_cast<UDreamUINavigationScope*>(Page));

	TestTrue(TEXT("Again"), Stack->HandleBack(0));
	TestNull(TEXT("...and now the page too"), Stack->GetActiveScope(0));
	TestFalse(TEXT("With nothing left it goes unclaimed"), Stack->HandleBack(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBackInterceptionTest,
	"DreamGUI.Navigation.Back.AScreenCanInterceptOrPassItOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBackInterceptionTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackActionTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUINavigationStack* Stack = TestWorld.World->GetSubsystem<UDreamUINavigationStack>();
	if (!TestNotNull(TEXT("Navigation stack subsystem exists"), Stack))
	{
		return false;
	}

	UDreamBackHandlingScope* Page = MakeScope(TestWorld.World, TEXT("Page"));
	UDreamBackHandlingScope* Dialog = MakeScope(TestWorld.World, TEXT("Dialog"));
	Page->ActivateScope();
	Dialog->ActivateScope();

	// A screen that deals with Back itself -- discarding an edit, stepping back a page, asking for
	// confirmation -- keeps it, and must not also be closed by it.
	Dialog->bHandleBack = true;
	TestTrue(TEXT("The handling screen takes Back"), Stack->HandleBack(0));
	TestEqual(TEXT("...was offered it"), Dialog->BackOfferCount, 1);
	TestEqual(TEXT("...and is still open"), Stack->GetActiveScope(0), static_cast<UDreamUINavigationScope*>(Dialog));
	TestEqual(TEXT("...and the page never saw it"), Page->BackOfferCount, 0);

	// Neither handling nor closing means transparent: the screen below gets its turn, which is how a
	// HUD panel sits on the stack without swallowing Back.
	Dialog->bHandleBack = false;
	Dialog->SetCloseOnBack(false);
	TestTrue(TEXT("Back is still taken"), Stack->HandleBack(0));
	TestEqual(TEXT("The transparent screen was offered it"), Dialog->BackOfferCount, 2);
	TestEqual(TEXT("...and the page below was too"), Page->BackOfferCount, 1);
	TestEqual(TEXT("...and it is the page that closed"), Stack->GetActiveScope(0), static_cast<UDreamUINavigationScope*>(Dialog));
	TestFalse(TEXT("The page is no longer open"), Page->IsScopeActive());
	return true;
}

#endif
