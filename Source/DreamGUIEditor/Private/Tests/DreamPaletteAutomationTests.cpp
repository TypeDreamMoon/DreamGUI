// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "DreamUIEditorTools.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
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
namespace DreamPaletteTestLocal
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
			for (auto& CopiedItem : FDreamUIEditorTools::CopiedWidgetPrefabList)
			{
				if (CopiedItem.Prefab.IsValid())
				{
					CopiedItem.Prefab->RemoveFromRoot();
					CopiedItem.Prefab->ConditionalBeginDestroy();
				}
			}
			FDreamUIEditorTools::CopiedWidgetPrefabList.Reset();
		}
	};

	UDreamWidget* MakeRoot(UWorld* World)
	{
		UDreamWidget* Root = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Root->SetDisplayName(TEXT("Root"));
		Root->SetWidth(800.0f);
		Root->SetHeight(600.0f);
		Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
		return Root;
	}

	/**
	 * Name the child after attaching it. Going through the create tools would uniquify the name, and
	 * a name collision is the whole precondition of these tests.
	 */
	UDreamWidget* MakeChildNamed(UWorld* World, UDreamWidget* Parent, const TCHAR* Name)
	{
		UDreamWidget* Child = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Child->SetWidth(100.0f);
		Child->SetHeight(50.0f);
		Child->TrySetParent(Parent, false);
		Child->SetDisplayName(Name);
		return Child;
	}

	/** Paste refuses to run unless some helper object claims the target widget. */
	UDreamUIPrefabHelperObject* MakeHelperFor(UWorld* World, UDreamWidget* Root)
	{
		UDreamUIPrefabHelperObject* Helper = NewObject<UDreamUIPrefabHelperObject>(World);
		Helper->LoadedRootWidget = Root;
		return Helper;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipboardKeepsEveryCopiedWidgetTest,
	"DreamGUI.Editor.Palette.CopyingTwoSameNamedWidgetsKeepsBoth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipboardKeepsEveryCopiedWidgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamPaletteTestLocal;
	FScopedTestWorld TestWorld;
	FScopedClipboard Clipboard;

	UDreamWidget* Root = MakeRoot(TestWorld.World);
	UDreamWidget* First = MakeChildNamed(TestWorld.World, Root, TEXT("Item"));
	UDreamWidget* Second = MakeChildNamed(TestWorld.World, Root, TEXT("Item"));
	if (!TestEqual(TEXT("the two widgets share a display name"), First->GetDisplayName(), Second->GetDisplayName()))
	{
		// Without the collision the next assertion would pass for the wrong reason.
		return false;
	}

	FDreamUIEditorTools::CopyWidgets([First, Second]() { return TArray<UDreamWidget*>{First, Second}; });

	// Keyed by name this was 1, and the second widget was already gone before paste ever ran.
	TestEqual(TEXT("both copies are on the clipboard"), FDreamUIEditorTools::CopiedWidgetPrefabList.Num(), 2);
	TestTrue(TEXT("and all of them are usable"), FDreamUIEditorTools::HaveValidCopiedWidgets());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPasteYieldsOneWidgetPerCopyTest,
	"DreamGUI.Editor.Palette.PastingTwoSameNamedWidgetsYieldsTwo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPasteYieldsOneWidgetPerCopyTest::RunTest(const FString& Parameters)
{
	using namespace DreamPaletteTestLocal;
	FScopedTestWorld TestWorld;
	FScopedClipboard Clipboard;

	UDreamWidget* Root = MakeRoot(TestWorld.World);
	UDreamUIPrefabHelperObject* Helper = MakeHelperFor(TestWorld.World, Root);
	if (!TestNotNull(TEXT("a helper claims the paste target"), (UObject*)Helper))return false;
	UDreamWidget* First = MakeChildNamed(TestWorld.World, Root, TEXT("Item"));
	UDreamWidget* Second = MakeChildNamed(TestWorld.World, Root, TEXT("Item"));

	FDreamUIEditorTools::CopyWidgets([First, Second]() { return TArray<UDreamWidget*>{First, Second}; });
	const TArray<UDreamWidget*> BeforePaste = Root->GetChildren();
	FDreamUIEditorTools::PasteWidgets([Root]() { return TArray<UDreamWidget*>{Root}; });

	TestEqual(TEXT("two copies paste as two widgets"), Root->GetChildren().Num() - BeforePaste.Num(), 2);

	// The name is only a naming hint for paste; it must not be what decides how many there are.
	TSet<FString> PastedNames;
	for (UDreamWidget* Child : Root->GetChildren())
	{
		if (!BeforePaste.Contains(Child))
		{
			PastedNames.Add(Child->GetDisplayName());
		}
	}
	TestEqual(TEXT("and the two pastes are named apart"), PastedNames.Num(), 2);
	return true;
}

#endif
