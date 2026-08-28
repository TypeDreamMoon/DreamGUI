// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "PrefabSystem/DreamUIPrefab.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamText.h"
#include "Engine/World.h"

/*
 * Whether the shipped control prefabs still load what they were authored with.
 *
 * They are version 8 while the serializer is at 9, and version 9 exists precisely because
 * FArchive << FText changed between UE 5.7 and 5.8 -- its own comment says it is incompatible with
 * earlier versions and that every prefab asset must be re-created. The reader does keep a
 * version-gated path for 8, but that path is the very one the engine change broke, which is what
 * "Failed loading tagged TextProperty ... Read 91B, expected 58B" is reporting.
 *
 * This exists to answer one question before anything is re-saved: does the text survive the load?
 *
 *   - If it does, the errors are noise and re-saving these assets as version 9 is safe and correct.
 *   - If it does not, re-saving would write the loss back to disk permanently -- the text would be
 *     gone from the source of truth, not merely misread at runtime -- and the assets have to be
 *     re-authored instead.
 *
 * It reports rather than asserts, because it is asked before the answer is known. Once the assets
 * are fixed the reporting turns into an assertion and this becomes a regression test.
 */

namespace DreamShippedPrefabHealthLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** All thirteen, because a partial survey of damaged assets is a partial repair list. */
	const TCHAR* ControlPrefabPaths[] =
	{
		TEXT("/DreamGUI/Prefabs/Button.Button"),
		TEXT("/DreamGUI/Prefabs/Toggle.Toggle"),
		TEXT("/DreamGUI/Prefabs/ToggleGroup.ToggleGroup"),
		TEXT("/DreamGUI/Prefabs/TextInput.TextInput"),
		TEXT("/DreamGUI/Prefabs/TextInput_Multiline.TextInput_Multiline"),
		TEXT("/DreamGUI/Prefabs/Dropdown.Dropdown"),
		TEXT("/DreamGUI/Prefabs/HorizontalSlider.HorizontalSlider"),
		TEXT("/DreamGUI/Prefabs/VerticalSlider.VerticalSlider"),
		TEXT("/DreamGUI/Prefabs/HorizontalScrollbar.HorizontalScrollbar"),
		TEXT("/DreamGUI/Prefabs/VerticalScrollbar.VerticalScrollbar"),
		TEXT("/DreamGUI/Prefabs/HorizontalScrollView.HorizontalScrollView"),
		TEXT("/DreamGUI/Prefabs/VerticalScrollView.VerticalScrollView"),
		TEXT("/DreamGUI/Prefabs/NavigationSelectionInputHandler.NavigationSelectionInputHandler"),
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamShippedPrefabTextSurvivesLoadTest,
	"DreamGUI.ShippedPrefabs.DoesTheirTextSurviveLoading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShippedPrefabTextSurvivesLoadTest::RunTest(const FString& Parameters)
{
	using namespace DreamShippedPrefabHealthLocal;

	AddExpectedError(TEXT("Failed loading tagged TextProperty"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Missing class when creating object"), EAutomationExpectedErrorFlags::Contains, 0);

	for (const TCHAR* PrefabPath : ControlPrefabPaths)
	{
		UDreamUIPrefab* Prefab = LoadObject<UDreamUIPrefab>(nullptr, PrefabPath);
		if (Prefab == nullptr)
		{
			AddInfo(FString::Printf(TEXT("%s: did not load"), PrefabPath));
			continue;
		}

		FScopedGameWorld TestWorld;
		UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(TestWorld.World);
		UDreamWidget* Root = Prefab->LoadPrefabInEditor(TestWorld.World, Tree, nullptr);
		if (!IsValid(Root))
		{
			AddInfo(FString::Printf(TEXT("%s (v%d): produced no hierarchy"), PrefabPath, Prefab->PrefabVersion));
			continue;
		}
		Tree->RootWidget = Root;

		int32 TextCount = 0;
		int32 EmptyCount = 0;
		TStringBuilder<256> Sample;
		Tree->ForEachWidget([&](UDreamWidget* Widget)
		{
			if (UDreamText* Text = Cast<UDreamText>(Widget->GetVisual()))
			{
				TextCount++;
				const FString Value = Text->GetText().ToString();
				if (Value.IsEmpty())
				{
					EmptyCount++;
				}
				if (Sample.Len() < 120)
				{
					Sample.Appendf(TEXT("[%s=\"%s\"]"), *Widget->GetDisplayName(), *Value);
				}
			}
		});

		AddInfo(FString::Printf(TEXT("%s (v%d): %d text visuals, %d empty %s"),
			PrefabPath, Prefab->PrefabVersion, TextCount, EmptyCount, Sample.ToString()));

		Root->DestroyWidget();
	}

	return true;
}

#endif
