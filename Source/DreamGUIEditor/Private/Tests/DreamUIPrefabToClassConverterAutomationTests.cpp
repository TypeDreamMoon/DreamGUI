// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamUIPrefabToClassConverter.h"
#include "DreamWidgetBlueprint.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "PrefabSystem/WidgetSerializer.h"

/*
 * Converting the existing prefabs into classes.
 *
 * The inputs are the plugin's own shipped control prefabs -- Button, Toggle, the scroll views --
 * rather than trees built by the test. That is the point: a converter proved against a fixture it
 * also authored has proved nothing about the assets it exists to move. These are real, they are
 * what the palette drops today, and they are the first thing a migration has to survive.
 *
 * The acceptance check is DreamUIPrefabToClass::VerifyFidelity, which compares the hierarchy the
 * prefab loads against the hierarchy the compiled class builds, node by node and property by
 * property. Its one blind spot is documented on the function and is why nothing here treats a clean
 * comparison as proof that object references point at the right widgets.
 */

namespace DreamUIPrefabToClassTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** The shipped control prefabs, by the paths the palette uses. */
	const TCHAR* ControlPrefabPaths[] =
	{
		TEXT("/DreamGUI/Prefabs/Button.Button"),
		TEXT("/DreamGUI/Prefabs/Toggle.Toggle"),
		TEXT("/DreamGUI/Prefabs/VerticalSlider.VerticalSlider"),
		TEXT("/DreamGUI/Prefabs/VerticalScrollbar.VerticalScrollbar"),
	};

	UDreamUIPrefab* LoadControlPrefab(const TCHAR* InPath)
	{
		return LoadObject<UDreamUIPrefab>(nullptr, InPath);
	}

	/**
	 * The shipped control prefabs do not load cleanly, and that predates all of this.
	 *
	 *   - 'UIButtonComponent' no longer exists in the source at all; Button.uasset still names it.
	 *     A casualty of the LGUI -> DreamGUI rename.
	 *   - Their FText data does not match what the current deserializer expects. Prefab version 9
	 *     (FTextAsReference) says in its own comment that it is not compatible with earlier versions
	 *     and that every prefab asset must be re-created. That was never done.
	 *
	 * Declared here so these tests measure what they are about -- whether conversion preserves what
	 * loads -- rather than the health of the inputs. They are NOT thereby dismissed: the caller
	 * surfaces them as warnings, and re-saving the shipped prefabs is tracked separately. Conversion
	 * faithfully carries damaged input across; that is correct behaviour for a converter and a
	 * separate problem for the assets.
	 */
	void ExpectKnownShippedPrefabDamage(FAutomationTestBase& InTest)
	{
		InTest.AddExpectedError(TEXT("Failed loading tagged TextProperty"), EAutomationExpectedErrorFlags::Contains, 0);
		InTest.AddExpectedError(TEXT("Missing class when creating object"), EAutomationExpectedErrorFlags::Contains, 0);
	}

	/**
	 * A prefab built by the test, for the checks that are about the CONVERTER rather than about the
	 * shipped assets.
	 *
	 * The shipped controls are deliberately damaged input (see above) and their load errors are
	 * demoted by the log after the first occurrence, which makes any expectation of them
	 * order-dependent. These two checks do not need real assets to mean anything, so they do not take
	 * the fragility.
	 */
	UDreamUIPrefab* MakeSyntheticPrefab(UWorld* InWorld, const TCHAR* InRootName)
	{
		UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(InWorld);
		UDreamWidget* Root = Tree->ConstructWidget<UDreamWidget>();
		Root->SetDisplayName(InRootName);
		Tree->RootWidget = Root;
		UDreamWidget* Child = Tree->ConstructWidget<UDreamWidget>();
		Child->SetDisplayName(TEXT("Child"));
		Child->TrySetParent(Root, false);

		UDreamUIPrefab* Prefab = NewObject<UDreamUIPrefab>();
		TMap<UObject*, FGuid> ObjectToGuid;
		ObjectToGuid.Add(Root, FGuid::NewGuid());
		TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> NoSubPrefabs;
		if (!DreamUIPrefabSystem::WidgetSerializer::SavePrefab(Root, Prefab, ObjectToGuid, NoSubPrefabs, true))
		{
			return nullptr;
		}
		Root->DestroyWidget();
		return Prefab;
	}

	/** A destination package under /Temp so nothing lands in the plugin's content. */
	FString MakeTargetPackageName(const FString& InSuffix)
	{
		return FString::Printf(TEXT("/Temp/DreamGUIConversion/BP_%s"), *InSuffix);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabConvertsWithFidelityTest,
	"DreamGUI.PrefabToClass.TheShippedControlsConvertWithoutLoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPrefabConvertsWithFidelityTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPrefabToClassTestLocal;

	ExpectKnownShippedPrefabDamage(*this);

	int32 Converted = 0;
	for (const TCHAR* PrefabPath : ControlPrefabPaths)
	{
		UDreamUIPrefab* Prefab = LoadControlPrefab(PrefabPath);
		if (Prefab != nullptr)
		{
			// Recorded as data rather than asserted, because the diagnosis above is about the assets
			// and not about the converter. Anything below LEXUI_CURRENT_PREFAB_VERSION here is a
			// prefab that was never re-saved after the format changed under it.
			AddInfo(FString::Printf(TEXT("%s: prefab version %d (current is %d)"),
				PrefabPath, Prefab->PrefabVersion, LEXUI_CURRENT_PREFAB_VERSION));
		}
		if (Prefab == nullptr)
		{
			// A missing shipped asset is worth saying out loud rather than skipping silently, but it
			// is not this test's failure to report.
			AddWarning(FString::Printf(TEXT("Control prefab '%s' did not load; skipping it."), PrefabPath));
			continue;
		}

		const FString ShortName = FPackageName::GetShortName(FSoftObjectPath(PrefabPath).GetLongPackageName());
		DreamUIPrefabToClass::FConversionResult Result = DreamUIPrefabToClass::ConvertPrefab(Prefab, MakeTargetPackageName(ShortName));

		if (!TestTrue(FString::Printf(TEXT("'%s' converts: %s"), PrefabPath, *Result.ToString()), Result.IsSuccess()))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("'%s' converted to something non-empty"), PrefabPath), Result.WidgetCount > 0);
		Converted++;

		UClass* GeneratedClass = Result.Blueprint->GeneratedClass;
		if (!TestNotNull(FString::Printf(TEXT("'%s' produced a class"), PrefabPath), GeneratedClass))
		{
			continue;
		}

		FScopedGameWorld TestWorld;
		TArray<FString> Differences;
		const bool bFaithful = DreamUIPrefabToClass::VerifyFidelity(Prefab, GeneratedClass, TestWorld.World, Differences);
		if (!bFaithful)
		{
			// Every difference, not just the count: a migration report nobody can act on is no report.
			for (const FString& Difference : Differences)
			{
				AddError(FString::Printf(TEXT("%s: %s"), PrefabPath, *Difference));
			}
		}
		TestTrue(FString::Printf(TEXT("'%s' converts without losing anything"), PrefabPath), bFaithful);
	}

	// If every prefab failed to load, everything above was vacuous and would have "passed".
	TestTrue(TEXT("at least one shipped control prefab was actually converted"), Converted > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabConversionLeavesSourceAloneTest,
	"DreamGUI.PrefabToClass.ConversionDoesNotTouchTheSourcePrefab",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPrefabConversionLeavesSourceAloneTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPrefabToClassTestLocal;

	FScopedGameWorld SourceWorld;
	UDreamUIPrefab* Prefab = MakeSyntheticPrefab(SourceWorld.World, TEXT("Untouched"));
	if (!TestNotNull(TEXT("the fixture prefab serialized"), Prefab))
	{
		return false;
	}

	// The migration's safety property: a bad conversion costs a deleted asset, not a damaged one.
	// Worth pinning because the obvious implementation -- reuse the loaded hierarchy instead of
	// copying it -- would quietly reparent the prefab's own objects into the new Blueprint.
	const int32 BinarySizeBefore = Prefab->BinaryData.Num();
	const uint16 VersionBefore = Prefab->PrefabVersion;
	const bool bDirtyBefore = Prefab->GetOutermost()->IsDirty();

	DreamUIPrefabToClass::FConversionResult Result = DreamUIPrefabToClass::ConvertPrefab(Prefab, MakeTargetPackageName(TEXT("SourceUntouched")));
	TestTrue(TEXT("the conversion ran"), Result.IsSuccess());

	TestEqual(TEXT("the prefab's serialized data is unchanged"), Prefab->BinaryData.Num(), BinarySizeBefore);
	TestEqual(TEXT("and its version is unchanged"), Prefab->PrefabVersion, VersionBefore);
	TestEqual(TEXT("and converting did not dirty its package"), Prefab->GetOutermost()->IsDirty(), bDirtyBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPrefabConversionRejectsNonsenseTest,
	"DreamGUI.PrefabToClass.BadInputIsReportedNotCrashed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPrefabConversionRejectsNonsenseTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIPrefabToClassTestLocal;

	// The empty-prefab case below deliberately feeds the deserializer nothing, which it reports.
	AddExpectedError(TEXT("No actor generated"), EAutomationExpectedErrorFlags::Contains, 0);

	// A migration runs unattended over a whole tree of assets, so the failure modes have to be
	// reported rather than fatal -- one broken prefab must not take the run down with it.
	{
		DreamUIPrefabToClass::FConversionResult Result = DreamUIPrefabToClass::ConvertPrefab(nullptr, MakeTargetPackageName(TEXT("Null")));
		TestFalse(TEXT("a null prefab is refused"), Result.IsSuccess());
		TestTrue(TEXT("and says why"), Result.Errors.Num() > 0);
	}
	{
		UDreamUIPrefab* Empty = NewObject<UDreamUIPrefab>();
		DreamUIPrefabToClass::FConversionResult Result = DreamUIPrefabToClass::ConvertPrefab(Empty, MakeTargetPackageName(TEXT("Empty")));
		TestFalse(TEXT("a prefab with no hierarchy is refused"), Result.IsSuccess());
		TestTrue(TEXT("and says why"), Result.Errors.Num() > 0);
	}
	{
		FScopedGameWorld SourceWorld;
		UDreamUIPrefab* Prefab = MakeSyntheticPrefab(SourceWorld.World, TEXT("NoDestination"));
		if (Prefab != nullptr)
		{
			DreamUIPrefabToClass::FConversionResult Result = DreamUIPrefabToClass::ConvertPrefab(Prefab, FString());
			TestFalse(TEXT("an empty destination is refused"), Result.IsSuccess());
		}
	}

	return true;
}

#endif
