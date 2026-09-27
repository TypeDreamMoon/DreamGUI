// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamOldAssetFixtures.h"
#include "DreamPackageLoadCheck.h"
#include "EngineUtils.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

#include "Driver/DreamDriverPieRig.h"

/*
 * Whether what was saved before anything moved still loads -- the promise every class move has to keep.
 *
 * The old-asset fixtures (see DreamOldAssetFixtures.h) load and compile without a warning, describe exactly
 * as the snapshot taken when they were saved says they do, and play.
 */
namespace DreamOldAssetTestLocal
{
	using namespace DreamOldAssetFixtures;

	/** One object of a description: its class line and its property lines. */
	struct FDescribedObject
	{
		FString Class;
		TArray<FString> Properties;
	};

	/** A description by object: "object <path> <class>" opens one, and the lines under it are its properties. */
	TMap<FString, FDescribedObject> ByObject(const FString& InDescription)
	{
		TMap<FString, FDescribedObject> Objects;
		TArray<FString> Lines;
		InDescription.ParseIntoArray(Lines, TEXT("\n"), /*bCullEmpty*/true);
		FDescribedObject* Current = &Objects.Add(TEXT("(the asset)"));
		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(TEXT("object ")) || Line.StartsWith(TEXT("blueprint ")) || Line.StartsWith(TEXT("world ")))
			{
				TArray<FString> Parts;
				Line.ParseIntoArray(Parts, TEXT(" "));
				Current = &Objects.Add(Parts.Num() > 1 ? Parts[1] : Line);
				Current->Class = Parts.Num() > 2 ? Parts[2] : FString();
				continue;
			}
			Current->Properties.Add(Line);
		}
		return Objects;
	}

	/** What differs between two descriptions, object by object, as sentences. */
	TArray<FString> Differences(const FString& InExpected, const FString& InActual)
	{
		TArray<FString> Out;
		const TMap<FString, FDescribedObject> Expected = ByObject(InExpected);
		const TMap<FString, FDescribedObject> Actual = ByObject(InActual);
		for (const TPair<FString, FDescribedObject>& Pair : Expected)
		{
			const FDescribedObject* Found = Actual.Find(Pair.Key);
			if (Found == nullptr)
			{
				Out.Add(FString::Printf(TEXT("%s (%s) is gone"), *Pair.Key, *Pair.Value.Class));
				continue;
			}
			if (Found->Class != Pair.Value.Class)
			{
				Out.Add(FString::Printf(TEXT("%s was a %s and is a %s"), *Pair.Key, *Pair.Value.Class, *Found->Class));
			}
			for (const FString& Property : Pair.Value.Properties)
			{
				if (!Found->Properties.Contains(Property))
				{
					Out.Add(FString::Printf(TEXT("%s no longer has %s"), *Pair.Key, *Property.TrimStart()));
				}
			}
			for (const FString& Property : Found->Properties)
			{
				if (!Pair.Value.Properties.Contains(Property))
				{
					Out.Add(FString::Printf(TEXT("%s now has %s"), *Pair.Key, *Property.TrimStart()));
				}
			}
		}
		for (const TPair<FString, FDescribedObject>& Pair : Actual)
		{
			if (!Expected.Contains(Pair.Key))
			{
				Out.Add(FString::Printf(TEXT("%s (%s) is new"), *Pair.Key, *Pair.Value.Class));
			}
		}
		return Out;
	}

	/** Whether every fixture is in this project, saying which is not. The test host's template carries them. */
	bool FixturesArePresent(FAutomationTestBase& InTest)
	{
		bool bAll = true;
		for (const FString& PackageName : PackageNames())
		{
			bAll &= InTest.TestTrue(*FString::Printf(TEXT("%s is in this project (Tools/TestHost/Template/Content carries it)"), *PackageName),
				FPackageName::DoesPackageExist(PackageName));
		}
		return bAll;
	}

	int32 CountDescendants(const UDreamWidget* InRoot)
	{
		int32 Count = 0;
		if (IsValid(InRoot))
		{
			for (const UDreamWidget* Child : InRoot->GetChildren())
			{
				Count += IsValid(Child) ? 1 + CountDescendants(Child) : 0;
			}
		}
		return Count;
	}

	UDreamWidget* FindDescendant(UDreamWidget* InRoot, const FString& InDisplayName)
	{
		if (!IsValid(InRoot))
		{
			return nullptr;
		}
		for (UDreamWidget* Child : InRoot->GetChildren())
		{
			if (IsValid(Child) && Child->GetDisplayName() == InDisplayName)
			{
				return Child;
			}
			if (UDreamWidget* Found = FindDescendant(Child, InDisplayName))
			{
				return Found;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamOldAssetsLoadCleanTest,
	"DreamGUI.Compatibility.TheOldAssetFixturesLoadCleanAndCompileWithoutAWarning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamOldAssetsLoadCleanTest::RunTest(const FString& Parameters)
{
	using namespace DreamOldAssetTestLocal;

	if (!FixturesArePresent(*this))
	{
		return false;
	}
	for (const FString& PackageName : PackageNames())
	{
		const DreamPackageLoadCheck::FResult Loaded = DreamPackageLoadCheck::LoadAndCompile(PackageName);
		TestNotNull(*FString::Printf(TEXT("%s loads"), *PackageName), Loaded.Package);
		for (const FString& Problem : Loaded.Problems)
		{
			AddError(FString::Printf(TEXT("%s: %s"), *PackageName, *Problem));
		}
		// The fixtures' own Blueprints were saved compiling without a warning; a warning now is something a
		// move changed. Other Blueprints compiled on the way -- the plugin's own presets -- are not the fixtures'.
		for (const FString& Warning : Loaded.CompilerWarnings)
		{
			AddError(FString::Printf(TEXT("%s: %s"), *PackageName, *Warning));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamOldAssetsMatchSnapshotTest,
	"DreamGUI.Compatibility.TheOldAssetFixturesHoldWhatTheSnapshotTakenWhenTheyWereSavedRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Every object, every non-default property, compared with the snapshot the saving code took of the same
 * files -- after carrying the snapshot's script paths through the redirects, because a class that moved is
 * expected to be found under its new path and nowhere else. Anything else that differs is data a move lost
 * or changed: a struct that stopped loading comes back as its defaults, an enum as its first value, and
 * either shows up here as a property the snapshot has and the load does not.
 */
bool FDreamOldAssetsMatchSnapshotTest::RunTest(const FString& Parameters)
{
	using namespace DreamOldAssetTestLocal;

	if (!FixturesArePresent(*this))
	{
		return false;
	}
	FString Snapshot;
	if (!TestTrue(TEXT("the snapshot is next to the fixtures"), FFileHelper::LoadFileToString(Snapshot, *SnapshotFilename())))
	{
		return false;
	}
	const TMap<FString, FString> Recorded = ParseSnapshot(Snapshot);
	for (const FString& PackageName : PackageNames())
	{
		const FString* Block = Recorded.Find(PackageName);
		if (!TestNotNull(*FString::Printf(TEXT("the snapshot records %s"), *PackageName), Block))
		{
			continue;
		}
		const FString Expected = ApplyRedirects(*Block);
		const FString Actual = Describe(DreamPackageLoadCheck::LoadAndCompile(PackageName).Package);
		if (Actual == Expected)
		{
			continue;
		}
		const TArray<FString> Found = Differences(Expected, Actual);
		AddError(FString::Printf(TEXT("%s does not hold what the snapshot records (%d differences)"), *PackageName, Found.Num()));
		for (int32 Index = 0; Index < FMath::Min(Found.Num(), 40); ++Index)
		{
			AddInfo(FString::Printf(TEXT("  %s"), *Found[Index]));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamOldAssetLevelPlaysTest,
	"DreamGUI.Pie.TheOldAssetLevelPlaysWithItsWorldWidgetShowingTheSavedHierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The fixture level in a play session of its own: the world widget comes up showing the nested fixture --
 * the nested user widgets, the button with its slot filled, and the title its property binding drives --
 * and the event system and the raycaster the level placed are there to route input.
 */
bool FDreamOldAssetLevelPlaysTest::RunTest(const FString& Parameters)
{
	using namespace DreamOldAssetTestLocal;

	if (!FixturesArePresent(*this))
	{
		return false;
	}
	// Loaded and compiled first, so the session plays classes compiled from what is on disk.
	for (const FString& PackageName : PackageNames())
	{
		for (const FString& Problem : DreamPackageLoadCheck::LoadAndCompile(PackageName).Problems)
		{
			AddError(FString::Printf(TEXT("%s: %s"), *PackageName, *Problem));
		}
	}
	const FString Level = PackageNames().Last();
	FDreamPieRigOptions Options;
	Options.MapOverride = Level + TEXT(".") + FPackageName::GetShortName(Level);
	// The level places the plugin's event system actor, and a world takes one per player.
	Options.bLevelBringsItsOwnEventSystem = true;
	// The world widget faces the origin from 3 m away, where the player starts.
	Options.PlayerStartLocation = FVector::ZeroVector;
	Options.PlayerStartRotation = FRotator::ZeroRotator;

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this, Options);
	Rig->Start();
	Rig->WhenReady([this](FDreamDriverPieRig& InRig)
	{
		UWorld* World = InRig.GetWorld();
		if (!TestNotNull(TEXT("the session is up"), World))
		{
			return;
		}
		ADreamWorldWidgetActor* WorldWidget = nullptr;
		for (TActorIterator<ADreamWorldWidgetActor> It(World); It && WorldWidget == nullptr; ++It)
		{
			WorldWidget = *It;
		}
		if (!TestNotNull(TEXT("the level's world widget actor is in the session"), WorldWidget)
			|| !TestNotNull(TEXT("with its component"), WorldWidget->GetWidgetComponent()))
		{
			return;
		}
		UDreamWidget* Loaded = WorldWidget->GetWidgetComponent()->GetLoadedWidget();
		if (!TestNotNull(TEXT("the world widget built its hierarchy"), Loaded))
		{
			return;
		}
		TestEqual(TEXT("from the nested fixture's class"), Loaded->GetClass()->GetName(), FString(TEXT("WBP_FixtureNested_C")));
		AddInfo(FString::Printf(TEXT("The level's world widget came up in play as a %s with %d widgets under it."),
			*Loaded->GetClass()->GetName(), CountDescendants(Loaded)));
		for (const TCHAR* Name : { TEXT("Bindings"), TEXT("Animated"), TEXT("Confirm"), TEXT("Caption") })
		{
			TestNotNull(*FString::Printf(TEXT("%s is in the hierarchy"), Name), FindDescendant(Loaded, Name));
		}
		UDreamWidget* Confirm = FindDescendant(Loaded, TEXT("Confirm"));
		TestTrue(TEXT("Confirm is the button it was saved as"), Confirm != nullptr && Confirm->IsA<UDreamButton>());
		UDreamWidget* Title = FindDescendant(Loaded, TEXT("Title"));
		const UDreamText* TitleText = Title != nullptr ? Cast<UDreamText>(Title->GetVisual()) : nullptr;
		if (TestNotNull(TEXT("the nested bindings fixture's title is in the hierarchy"), TitleText))
		{
			TestEqual(TEXT("and shows what its property binding returns"), TitleText->GetText().ToString(), FString(TEXT("Bound before the split")));
		}

		// By the names the level gave them, which a play session's copy of the level keeps.
		int32 EventSystems = 0;
		int32 Raycasters = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			EventSystems += It->GetFName() == FName(TEXT("FixtureEventSystem")) ? 1 : 0;
			Raycasters += It->GetFName() == FName(TEXT("FixturePointer")) && It->FindComponentByClass<UDreamWorldSpaceRaycaster>() != nullptr ? 1 : 0;
		}
		TestEqual(TEXT("the level's event system actor is in the session"), EventSystems, 1);
		TestEqual(TEXT("and so is its actor with a raycaster of its own"), Raycasters, 1);
	});
	Rig->Finish();
	return true;
}

#endif
