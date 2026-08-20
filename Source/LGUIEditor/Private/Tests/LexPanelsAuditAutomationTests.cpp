// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/LexScrollBoxInputHandler.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIBehaviour.h"
#include "Engine/World.h"
#include "Interaction/UIButton.h"
#include "UObject/StrongObjectPtr.h"

// Copy, Cut and Duplicate all end in a paste of the selected component, and the paste refuses classes
// the class picker hides. Whichever way the refusal is reached, it has to be reached before the menu
// entry is offered: Cut has already deleted the component by the time the paste refuses it, Duplicate
// silently does nothing, and the clipboard is one static shared by every panel, so an item no paste
// will accept leaves Paste greyed out for every widget in every prefab editor.
//
// The predicate is asked here rather than through the panel because the panel is a Slate widget no
// headless test can construct.
bool LexUIWidgetComponentClipboard_CanPasteClass(const UClass* InComponentClass);
bool LexUIWidgetComponentClipboard_CanTakeComponent(const ULexUIBehaviour* InComponent);
ULexUIBehaviour* LexUIWidgetComponentClipboard_PasteOnto(ULexWidget* InTargetWidget, ULexUIBehaviour* InSource);

namespace LexPanelsAuditTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** GC follows UPROPERTY references, not outer chains, so a widget held by nothing else goes mid-test. */
	TStrongObjectPtr<ULexWidget> MakeWidget(UWorld* World, const TCHAR* DisplayName)
	{
		TStrongObjectPtr<ULexWidget> Widget(NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional));
		Widget->SetDisplayName(DisplayName);
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexClipboardTakesOnlyWhatAPasteWouldAcceptTest,
	"LGUI.Editor.ComponentClipboard.TheClipboardTakesOnlyWhatAPasteWouldAccept",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexClipboardTakesOnlyWhatAPasteWouldAcceptTest::RunTest(const FString& Parameters)
{
	using namespace LexPanelsAuditTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<ULexWidget> Source = MakeWidget(TestWorld.World, TEXT("Source"));
	TStrongObjectPtr<ULexWidget> Target = MakeWidget(TestWorld.World, TEXT("Target"));

	// The scroll box makes one of these for itself on register, and the component list shows every
	// component on the widget, so this is a row the user can select and reach the menu from.
	ULexUIBehaviour* Handler = Source->AddComponent(ULexScrollBoxInputHandler::StaticClass());
	ULexUIBehaviour* Button = Source->AddComponent<UUIButton>();
	if (!TestTrue(TEXT("the source widget carries both a pasteable and a refused component"),
		IsValid(Handler) && IsValid(Button)))return true;

	TestFalse(TEXT("a component of a class the picker hides is not taken"),
		LexUIWidgetComponentClipboard_CanTakeComponent(Handler));
	TestTrue(TEXT("an ordinary component is"),
		LexUIWidgetComponentClipboard_CanTakeComponent(Button));
	TestFalse(TEXT("nothing is not"), LexUIWidgetComponentClipboard_CanTakeComponent(nullptr));

	// The two answers are the same answer: anything the menu offers to take must be something a paste
	// puts back down, or the component is gone and the clipboard is stuck holding it.
	for (ULexUIBehaviour* Component : TArray<ULexUIBehaviour*>{ Handler, Button })
	{
		const bool bTaken = LexUIWidgetComponentClipboard_CanTakeComponent(Component);
		const bool bPasted = IsValid(LexUIWidgetComponentClipboard_PasteOnto(Target.Get(), Component));
		TestTrue(*FString::Printf(TEXT("what the clipboard takes is what a paste accepts, for %s"), *Component->GetClass()->GetName()),
			bTaken == bPasted);
	}

	TestTrue(TEXT("the class answer and the component answer agree"),
		LexUIWidgetComponentClipboard_CanTakeComponent(Handler) == LexUIWidgetComponentClipboard_CanPasteClass(Handler->GetClass())
		&& LexUIWidgetComponentClipboard_CanTakeComponent(Button) == LexUIWidgetComponentClipboard_CanPasteClass(Button->GetClass()));
	return true;
}

#endif
