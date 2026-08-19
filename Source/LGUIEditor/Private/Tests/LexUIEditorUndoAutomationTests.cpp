// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "LexUIEditorTools.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Editor.h"
#include "Engine/World.h"

// Creating a widget opened a transaction that Modify()'d the selection object and the prefab
// helper, and nothing else. The parent's Children array -- the thing the create actually writes --
// was never snapshotted, so the entry on the undo stack described no change. Ctrl+Z popped it,
// nothing happened, and the user's next Ctrl+Z undid the edit *before* the create. That failure is
// invisible until someone reaches for undo and loses work, so it is pinned here.
namespace LexUIEditorUndoTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeRoot(UWorld* World)
	{
		ULexWidget* Root = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Root->SetDisplayName(TEXT("Root"));
		Root->SetWidth(800.0f);
		Root->SetHeight(600.0f);
		Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
		return Root;
	}

	/**
	 * The raw array length, not the number of live entries.
	 *
	 * Undo invalidates an object created inside the transaction all by itself, so counting only
	 * valid children reports success even when the parent's Children array was never restored and
	 * still holds a stale slot. That stale slot is the actual defect: it misnumbers every
	 * SiblingIndex after it and gets written out on save. Count the slots.
	 */
	int32 ChildSlotCount(const ULexWidget* Widget)
	{
		return Widget->GetChildren().Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexCreateWidgetIsUndoableTest,
	"LGUI.Editor.Undo.CreatingAWidgetIsRecorded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexCreateWidgetIsUndoableTest::RunTest(const FString& Parameters)
{
	using namespace LexUIEditorUndoTestLocal;
	if (GEditor == nullptr || GEditor->Trans == nullptr)
	{
		AddError(TEXT("no transaction buffer; this test cannot say anything"));
		return false;
	}
	FScopedTestWorld TestWorld;
	ULexWidget* Root = MakeRoot(TestWorld.World);

	ULexWidget* Created = FLexUIEditorTools::CreateWidgetAndReturn([Root]() { return Root; }, TEXT("Child"), nullptr, nullptr);
	if (!TestNotNull(TEXT("the widget was created"), (UObject*)Created))return false;
	TestEqual(TEXT("and attached"), ChildSlotCount(Root), 1);

	GEditor->UndoTransaction();
	TestEqual(TEXT("undo takes it back out of the parent"), ChildSlotCount(Root), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUndoRestoresTheParentTest,
	"LGUI.Editor.Undo.UndoDoesNotReachPastTheCreate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUndoRestoresTheParentTest::RunTest(const FString& Parameters)
{
	using namespace LexUIEditorUndoTestLocal;
	if (GEditor == nullptr || GEditor->Trans == nullptr)
	{
		AddError(TEXT("no transaction buffer; this test cannot say anything"));
		return false;
	}
	FScopedTestWorld TestWorld;
	ULexWidget* Root = MakeRoot(TestWorld.World);

	// An ordinary edit first. If the create records nothing, this is what the user loses when they
	// press Ctrl+Z once expecting the create to come off.
	GEditor->BeginTransaction(FText::FromString(TEXT("Test Resize")));
	Root->Modify();
	Root->SetWidth(1234.0f);
	GEditor->EndTransaction();
	TestEqual(TEXT("the earlier edit applied"), Root->GetWidth(), 1234.0f);

	FLexUIEditorTools::CreateWidgetAndReturn([Root]() { return Root; }, TEXT("Child"), nullptr, nullptr);
	TestEqual(TEXT("child attached"), ChildSlotCount(Root), 1);

	GEditor->UndoTransaction();
	TestEqual(TEXT("one undo removes the child"), ChildSlotCount(Root), 0);
	TestEqual(TEXT("and leaves the earlier edit alone"), Root->GetWidth(), 1234.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexCreateControlIsUndoableTest,
	"LGUI.Editor.Undo.CreatingARegisteredControlIsRecorded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexCreateControlIsUndoableTest::RunTest(const FString& Parameters)
{
	using namespace LexUIEditorUndoTestLocal;
	if (GEditor == nullptr || GEditor->Trans == nullptr)
	{
		AddError(TEXT("no transaction buffer; this test cannot say anything"));
		return false;
	}
	FScopedTestWorld TestWorld;
	ULexWidget* Root = MakeRoot(TestWorld.World);

	// A native registry control goes through CreateWidgetAndReturn with a recipe callback, which is
	// the path the palette uses for every panel.
	ULexWidget* Created = FLexUIEditorTools::CreateRegisteredControlAndReturn([Root]() { return Root; }, TEXT("VerticalBox"), nullptr);
	if (!TestNotNull(TEXT("the control was created"), (UObject*)Created))return false;
	TestNotNull(TEXT("with its layout container"), (UObject*)Created->GetLayoutContainer());
	TestEqual(TEXT("and attached"), ChildSlotCount(Root), 1);

	GEditor->UndoTransaction();
	TestEqual(TEXT("undo takes it back out"), ChildSlotCount(Root), 0);
	return true;
}

#endif
