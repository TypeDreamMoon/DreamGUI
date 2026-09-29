// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamOnDiskFixture.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "UObject/Package.h"

/*
 * What a serializer does that a constructor does not.
 *
 * Every other test in this suite builds its tree with ConstructWidget and TrySetParent, which attach
 * live. A tree that arrives from a .uasset went through none of that, and the difference is where
 * the defects were: UDreamWidgetTree had no PostLoad, so GetParent() came back null for every widget
 * in every saved asset -- the designer's duplicate command did nothing at all on a real asset, and
 * 340 green tests could not see it, because not one of them had a tree that arrived any other way.
 *
 * So these go through the disk. The first one exists to prove the fixture is not lying: a reload
 * that quietly hands the built objects back would leave every assertion below true and mean nothing.
 */

namespace DreamOnDiskTestLocal
{
	using namespace DreamOnDiskFixture;

	/** Root, with A under it and C under A. Deep enough that a one-level restore shows as a difference. */
	UDreamWidgetTree* BuildTree(UPackage* InPackage, const TCHAR* InName)
	{
		UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(InPackage, FName(InName), RF_Public | RF_Standalone);
		Tree->RootWidget = Tree->ConstructWidget<UDreamWidget>();
		Tree->RootWidget->SetDisplayName(TEXT("Root"));

		UDreamWidget* A = Tree->ConstructWidget<UDreamWidget>();
		A->SetDisplayName(TEXT("A"));
		A->SetParentBeforeRegister(Tree->RootWidget);

		UDreamWidget* C = Tree->ConstructWidget<UDreamWidget>();
		C->SetDisplayName(TEXT("C"));
		C->SetParentBeforeRegister(A);
		return Tree;
	}

	UDreamWidget* FindByDisplayName(const UDreamWidgetTree* InTree, const TCHAR* InDisplayName)
	{
		UDreamWidget* Found = nullptr;
		InTree->ForEachWidget([&Found, InDisplayName](UDreamWidget* Widget)
		{
			if (Found == nullptr && Widget->GetDisplayName() == InDisplayName)
			{
				Found = Widget;
			}
		});
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamOnDiskFixtureReallyReloadsTest,
	"DreamGUI.Editor.OnDisk.TheFixtureReallyReadsTheFileBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamOnDiskFixtureReallyReloadsTest::RunTest(const FString& Parameters)
{
	using namespace DreamOnDiskTestLocal;

	FScopedOnDiskPackage Fixture(TEXT("OnDiskSelfCheck"));
	UDreamWidgetTree* Built = BuildTree(Fixture.Package, TEXT("OnDiskSelfCheck"));

	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("the package saved: %s"), *Error), Fixture.Save(Built, Error)))return false;

	UDreamWidgetTree* Loaded = Cast<UDreamWidgetTree>(Fixture.Reload(Error));
	if (!TestNotNull(*FString::Printf(TEXT("the package reloaded: %s"), *Error), Loaded))return false;

	// The whole point. LoadPackage answers from memory when a package of that name is already loaded,
	// so a fixture that forgets to vacate the name hands back the objects the test just built and
	// every claim about loading passes without a serializer having run.
	TestNotEqual(TEXT("the reloaded tree is a DIFFERENT object from the one built"), (const UObject*)Loaded, (const UObject*)Built);
	if (!TestNotNull(TEXT("and it has a root"), (UObject*)Loaded->RootWidget.Get()))return false;
	TestNotEqual(TEXT("whose root is a different object too"),
		(const UObject*)Loaded->RootWidget.Get(), (const UObject*)Built->RootWidget.Get());
	TestEqual(TEXT("the hierarchy survived the round trip"), Loaded->CountWidgets(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamOnDiskTreeKnowsItsParentsTest,
	"DreamGUI.Editor.OnDisk.AWidgetTreeFromDiskKnowsItsParents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamOnDiskTreeKnowsItsParentsTest::RunTest(const FString& Parameters)
{
	using namespace DreamOnDiskTestLocal;

	FScopedOnDiskPackage Fixture(TEXT("OnDiskParents"));
	UDreamWidgetTree* Built = BuildTree(Fixture.Package, TEXT("OnDiskParents"));

	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("the package saved: %s"), *Error), Fixture.Save(Built, Error)))return false;
	UDreamWidgetTree* Loaded = Cast<UDreamWidgetTree>(Fixture.Reload(Error));
	if (!TestNotNull(*FString::Printf(TEXT("the package reloaded: %s"), *Error), Loaded))return false;

	// Parent is Transient. Children is what the file holds, and something has to turn one into the
	// other -- UDreamWidgetTree::PostLoad. Without it every widget in every saved asset answers null
	// here, and every command that walks upward (duplicate, reparent, "which panel am I in") refuses
	// to do anything, on real assets only.
	UDreamWidget* LoadedA = FindByDisplayName(Loaded, TEXT("A"));
	UDreamWidget* LoadedC = FindByDisplayName(Loaded, TEXT("C"));
	if (!TestNotNull(TEXT("A came back"), (UObject*)LoadedA))return false;
	if (!TestNotNull(TEXT("C came back"), (UObject*)LoadedC))return false;

	TestEqual(TEXT("A's parent is the loaded root"), LoadedA->GetParent(), Loaded->RootWidget.Get());
	TestEqual(TEXT("C's parent is the loaded A"), LoadedC->GetParent(), LoadedA);
	TestNull(TEXT("and the root has none"), (UObject*)Loaded->RootWidget->GetParent());

	// Nothing points back at the objects the test built. A back-pointer restored from the wrong tree
	// would satisfy "is not null" and be worse than null.
	TestNotEqual(TEXT("nothing links back into the pre-save tree"),
		(const UObject*)LoadedA->GetParent(), (const UObject*)Built->RootWidget.Get());
	return true;
}

#endif
