// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamLayoutPassContext.h"
#include "Core/DreamUIManager.h"
#include "DreamScopedWorld.h"
#include "Engine/World.h"

/*
 * A LAYOUT PASS BELONGS TO ONE WORLD.
 *
 * The writer stack, the pass depth and the desired-size memo used to be static on the classes that use
 * them, so every world shared one copy: a pass running in a play session made the editor world's widgets
 * look as though they were being written by layout, and a pass left open anywhere changed how every later
 * test's layout behaved. Each world's manager holds them now, and a tree in no world with a manager keeps
 * them on its own root.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutPassContextPerWorldTest,
	"DreamGUI.Layout.EachWorldKeepsItsOwnLayoutPassState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutPassContextPerWorldTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld First;
	DreamTests::FScopedGameWorld Second;
	UDreamUIManagerWorldSubsystem* FirstManager = UDreamUIManagerWorldSubsystem::GetInstance(First.World);
	UDreamUIManagerWorldSubsystem* SecondManager = UDreamUIManagerWorldSubsystem::GetInstance(Second.World);
	if (!TestNotNull(TEXT("The first world has a UI manager"), FirstManager)
		|| !TestNotNull(TEXT("The second world has a UI manager"), SecondManager))
	{
		return false;
	}
	UDreamWidget* InFirst = NewObject<UDreamWidget>(First.World, NAME_None, RF_Transient);
	UDreamWidget* InSecond = NewObject<UDreamWidget>(Second.World, NAME_None, RF_Transient);
	TestTrue(TEXT("A widget in a world uses its world's layout state"), &InFirst->GetLayoutPassContext() == &FirstManager->GetLayoutPassContext());
	TestTrue(TEXT("...and a widget in another world uses that one's"), &InSecond->GetLayoutPassContext() == &SecondManager->GetLayoutPassContext());
	{
		FDreamLayoutPassContext::FPassScope Pass(FirstManager->GetLayoutPassContext());
		TestTrue(TEXT("While a pass runs in one world, its widgets are being written by layout"), InFirst->IsLayoutWriting());
		TestFalse(TEXT("...and the other world's are not"), InSecond->IsLayoutWriting());
	}
	TestFalse(TEXT("Once the pass is over, neither is"), InFirst->IsLayoutWriting() || InSecond->IsLayoutWriting());

	// A tree no manager holds state for keeps it on its root, where every widget of the tree reaches it and
	// no other tree does.
	UDreamWidget* DetachedRoot = NewObject<UDreamWidget>(GetTransientPackage(), NAME_None, RF_Transient);
	UDreamWidget* DetachedChild = NewObject<UDreamWidget>(DetachedRoot, NAME_None, RF_Transient);
	UDreamWidget* OtherDetached = NewObject<UDreamWidget>(GetTransientPackage(), NAME_None, RF_Transient);
	DetachedChild->SetParentBeforeRegister(DetachedRoot);
	TestTrue(TEXT("A tree in no world shares one layout state between its widgets"),
		&DetachedChild->GetLayoutPassContext() == &DetachedRoot->GetLayoutPassContext());
	TestTrue(TEXT("...that no other tree shares"), &OtherDetached->GetLayoutPassContext() != &DetachedRoot->GetLayoutPassContext());
	TestTrue(TEXT("...nor any world"), &DetachedRoot->GetLayoutPassContext() != &FirstManager->GetLayoutPassContext());
	{
		FDreamLayoutPassContext::FPassScope Pass(DetachedRoot->GetLayoutPassContext());
		TestTrue(TEXT("A pass over that tree is seen from inside it"), DetachedChild->IsLayoutWriting());
		TestFalse(TEXT("...and from nowhere else"), OtherDetached->IsLayoutWriting() || InFirst->IsLayoutWriting());
	}
	DetachedRoot->DestroyWidget();
	OtherDetached->DestroyWidget();
	return true;
}

#endif
