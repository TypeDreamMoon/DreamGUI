// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "LexUIEditorTools.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexPanelLayouts.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "Editor.h"
#include "Engine/World.h"

// Two defects meet here, and both were silent.
//
// The copy clipboard was a TMap keyed by display name, so copying two widgets that share a name kept
// one and dropped the other with no message. Names collide by construction -- sub-prefab children
// are skipped by EnsureUniqueWidgetDisplayNames on purpose -- so this was the ordinary case, not a
// corner one. What is pinned below is the count, which is what the key was quietly deciding.
//
// And a prefab dragged from the Palette was flattened in with no nesting guard at all, so dropping
// the open prefab onto its own child inlined a copy of itself; Apply baked it, and the next repeat
// doubled the asset again. The guard is plain C++ and is pinned directly; the Palette routing that
// calls it is Slate-bound and is not reachable from here.
namespace LexPaletteTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** CopyWidgets roots its prefabs so GC cannot take them; leaving them rooted would leak. */
	struct FScopedClipboard
	{
		~FScopedClipboard()
		{
			for (auto& CopiedItem : FLexUIEditorTools::CopiedWidgetPrefabList)
			{
				if (CopiedItem.Prefab.IsValid())
				{
					CopiedItem.Prefab->RemoveFromRoot();
					CopiedItem.Prefab->ConditionalBeginDestroy();
				}
			}
			FLexUIEditorTools::CopiedWidgetPrefabList.Reset();
		}
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
	 * Name the child after attaching it. Going through the create tools would uniquify the name, and
	 * a name collision is the whole precondition of these tests.
	 */
	ULexWidget* MakeChildNamed(UWorld* World, ULexWidget* Parent, const TCHAR* Name)
	{
		ULexWidget* Child = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Child->SetWidth(100.0f);
		Child->SetHeight(50.0f);
		Child->TrySetParent(Parent, false);
		Child->SetDisplayName(Name);
		return Child;
	}

	/** Paste refuses to run unless some helper object claims the target widget. */
	ULexUIPrefabHelperObject* MakeHelperFor(UWorld* World, ULexWidget* Root)
	{
		ULexUIPrefabHelperObject* Helper = NewObject<ULexUIPrefabHelperObject>(World);
		Helper->LoadedRootWidget = Root;
		return Helper;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexClipboardKeepsEveryCopiedWidgetTest,
	"LGUI.Editor.Palette.CopyingTwoSameNamedWidgetsKeepsBoth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexClipboardKeepsEveryCopiedWidgetTest::RunTest(const FString& Parameters)
{
	using namespace LexPaletteTestLocal;
	FScopedTestWorld TestWorld;
	FScopedClipboard Clipboard;

	ULexWidget* Root = MakeRoot(TestWorld.World);
	ULexWidget* First = MakeChildNamed(TestWorld.World, Root, TEXT("Item"));
	ULexWidget* Second = MakeChildNamed(TestWorld.World, Root, TEXT("Item"));
	if (!TestEqual(TEXT("the two widgets share a display name"), First->GetDisplayName(), Second->GetDisplayName()))
	{
		// Without the collision the next assertion would pass for the wrong reason.
		return false;
	}

	FLexUIEditorTools::CopyWidgets([First, Second]() { return TArray<ULexWidget*>{First, Second}; });

	// Keyed by name this was 1, and the second widget was already gone before paste ever ran.
	TestEqual(TEXT("both copies are on the clipboard"), FLexUIEditorTools::CopiedWidgetPrefabList.Num(), 2);
	TestTrue(TEXT("and all of them are usable"), FLexUIEditorTools::HaveValidCopiedWidgets());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPasteYieldsOneWidgetPerCopyTest,
	"LGUI.Editor.Palette.PastingTwoSameNamedWidgetsYieldsTwo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPasteYieldsOneWidgetPerCopyTest::RunTest(const FString& Parameters)
{
	using namespace LexPaletteTestLocal;
	FScopedTestWorld TestWorld;
	FScopedClipboard Clipboard;

	ULexWidget* Root = MakeRoot(TestWorld.World);
	ULexUIPrefabHelperObject* Helper = MakeHelperFor(TestWorld.World, Root);
	if (!TestNotNull(TEXT("a helper claims the paste target"), (UObject*)Helper))return false;
	ULexWidget* First = MakeChildNamed(TestWorld.World, Root, TEXT("Item"));
	ULexWidget* Second = MakeChildNamed(TestWorld.World, Root, TEXT("Item"));

	FLexUIEditorTools::CopyWidgets([First, Second]() { return TArray<ULexWidget*>{First, Second}; });
	const TArray<ULexWidget*> BeforePaste = Root->GetChildren();
	FLexUIEditorTools::PasteWidgets([Root]() { return TArray<ULexWidget*>{Root}; });

	TestEqual(TEXT("two copies paste as two widgets"), Root->GetChildren().Num() - BeforePaste.Num(), 2);

	// The name is only a naming hint for paste; it must not be what decides how many there are.
	TSet<FString> PastedNames;
	for (ULexWidget* Child : Root->GetChildren())
	{
		if (!BeforePaste.Contains(Child))
		{
			PastedNames.Add(Child->GetDisplayName());
		}
	}
	TestEqual(TEXT("and the two pastes are named apart"), PastedNames.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPrefabCannotNestInsideItselfTest,
	"LGUI.Editor.Palette.APrefabCannotBeNestedInsideItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPrefabCannotNestInsideItselfTest::RunTest(const FString& Parameters)
{
	using namespace LexPaletteTestLocal;
	FScopedTestWorld TestWorld;

	ULexWidget* Root = MakeRoot(TestWorld.World);
	ULexUIPrefabHelperObject* Helper = MakeHelperFor(TestWorld.World, Root);
	ULexUIPrefab* Prefab = NewObject<ULexUIPrefab>();
	Prefab->PrefabVersion = LEXUI_CURRENT_PREFAB_VERSION;

	FText Error;
	// A widget whose prefab is not recorded has nothing to be cyclic with, so the guard has to stay
	// out of the way -- otherwise "it refuses everything" would look like a passing test.
	TestTrue(TEXT("a widget with no source prefab accepts one"),
		FLexUIEditorTools::CanNestPrefabUnderWidget(Prefab, Root, Error));

	Helper->PrefabAsset = Prefab;
	TestFalse(TEXT("the prefab being edited cannot become its own child"),
		FLexUIEditorTools::CanNestPrefabUnderWidget(Prefab, Root, Error));
	TestFalse(TEXT("and the refusal says why"), Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexOldPrefabVersionIsRefusedTest,
	"LGUI.Editor.Palette.APrefabTooOldToDeserializeIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexOldPrefabVersionIsRefusedTest::RunTest(const FString& Parameters)
{
	using namespace LexPaletteTestLocal;
	FScopedTestWorld TestWorld;

	ULexWidget* Root = MakeRoot(TestWorld.World);
	ULexUIPrefab* Current = NewObject<ULexUIPrefab>();
	Current->PrefabVersion = LEXUI_CURRENT_PREFAB_VERSION;
	ULexUIPrefab* TooOld = NewObject<ULexUIPrefab>();
	TooOld->PrefabVersion = (uint16)ELexUIPrefabVersion::OldVersion;

	FText Error;
	TestTrue(TEXT("a current prefab passes"), FLexUIEditorTools::CanNestPrefabUnderWidget(Current, Root, Error));
	// The Content Browser drop has rejected these for as long as it has existed; the Palette drag
	// went straight past the check and flattened an undeserializable prefab into the tree.
	TestFalse(TEXT("a prefab from before the FArchive format is refused"),
		FLexUIEditorTools::CanNestPrefabUnderWidget(TooOld, Root, Error));
	TestFalse(TEXT("and the refusal says why"), Error.IsEmpty());

	TestFalse(TEXT("a prefab that failed to load is refused too"),
		FLexUIEditorTools::CanNestPrefabUnderWidget(nullptr, Root, Error));
	return true;
}

#endif
