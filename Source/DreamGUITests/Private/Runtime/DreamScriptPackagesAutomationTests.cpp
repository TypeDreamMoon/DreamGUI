// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Core/DreamUIScriptPackages.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

/*
 * Where the plugin's own types are looked for, now that nothing spells "/Script/DreamGUI" to find or
 * count them: the list of runtime script packages, and the widget registry that forgets a module's tags
 * when the module goes.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRuntimePackageListTest,
	"DreamGUI.Packaging.TheRuntimePackageListStartsWithTheCoreAndHoldsEachPackageOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRuntimePackageListTest::RunTest(const FString& Parameters)
{
	const TArray<FName> Packages = DreamUI::GetRuntimeScriptPackages();
	if (!TestTrue(TEXT("the list is not empty"), Packages.Num() > 0))
	{
		return false;
	}
	TestEqual(TEXT("the core module's package comes first"), Packages[0], FName(TEXT("/Script/DreamGUI")));
	TestTrue(TEXT("and it is a runtime package"), DreamUI::IsRuntimeScriptPackage(TEXT("/Script/DreamGUI")));
	TestFalse(TEXT("the editor module's package is not"), DreamUI::IsRuntimeScriptPackage(TEXT("/Script/DreamGUIEditor")));

	TestTrue(TEXT("a type of the core lies inside it"), DreamUI::IsInRuntimeScriptPackage(TEXT("/Script/DreamGUI.DreamWidget")));
	TestTrue(TEXT("and so does a function of one"), DreamUI::IsInRuntimeScriptPackage(TEXT("/Script/DreamGUI.DreamWidget.SetWidth")));
	// The package is the whole part before the dot, so a package whose name merely starts the same way is another one.
	TestFalse(TEXT("a type of a package whose name starts the same way does not"),
		DreamUI::IsInRuntimeScriptPackage(TEXT("/Script/DreamGUIEditor.DreamWidgetBlueprint")));
	TestFalse(TEXT("and a package path alone names nothing inside a package"), DreamUI::IsInRuntimeScriptPackage(TEXT("/Script/DreamGUI")));

	const FName Added(TEXT("/Script/DreamGUITestsOnlyPackage"));
	{
		DreamUI::RegisterRuntimeScriptPackage(Added);
		ON_SCOPE_EXIT
		{
			DreamUI::UnregisterRuntimeScriptPackage(Added);
		};
		// A module registers from StartupModule, which a reload runs again.
		DreamUI::RegisterRuntimeScriptPackage(Added);
		const TArray<FName> WithAdded = DreamUI::GetRuntimeScriptPackages();
		TestEqual(TEXT("a package is listed once, however often it registers"), WithAdded.Num(), Packages.Num() + 1);
		TestEqual(TEXT("after the packages registered before it"), WithAdded.Last(), Added);
		TestTrue(TEXT("and a type path inside it counts as the plugin's"),
			DreamUI::IsInRuntimeScriptPackage(TEXT("/Script/DreamGUITestsOnlyPackage.Anything")));
	}
	TestTrue(TEXT("unregistering takes it out again and leaves the rest as they were"), DreamUI::GetRuntimeScriptPackages() == Packages);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetRegistryModuleTest,
	"DreamGUI.Packaging.AModuleThatGoesTakesItsTagsOutOfTheWidgetRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetRegistryModuleTest::RunTest(const FString& Parameters)
{
	const FName Module(TEXT("DreamGUITestsOnlyModule"));
	const FName Scope(TEXT("DreamGUITestsOnly"));
	ON_SCOPE_EXIT
	{
		FDreamUIWidgetRegistry::UnregisterModule(Module);
	};
	FDreamUIWidgetRegistry::Register({ Scope, TEXT("Probe"), []() -> UClass* { return UDreamButton::StaticClass(); },
		FDreamUIWidgetRegistry::EKind::ScopedWidget, Module });
	FDreamUIWidgetRegistry::Register({ Scope, TEXT("OtherProbe"), []() -> UClass* { return UDreamButton::StaticClass(); },
		FDreamUIWidgetRegistry::EKind::ScopedWidget, Module });

	TestEqual(TEXT("a registered tag resolves"), FDreamUIWidgetRegistry::Resolve(Scope, TEXT("Probe")), UDreamButton::StaticClass());
	FDreamUIWidgetRegistry::UnregisterModule(Module);
	TestNull(TEXT("once its module unregisters, it does not"), FDreamUIWidgetRegistry::Resolve(Scope, TEXT("Probe")));
	TestEqual(TEXT("nor does anything else that module declared"), FDreamUIWidgetRegistry::NamesInScope(Scope).Num(), 0);
	TestEqual(TEXT("and the other modules' tags are untouched"),
		FDreamUIWidgetRegistry::Resolve(TEXT("Native"), TEXT("Button")), UDreamButton::StaticClass());

	// Every tag a class of the core declares is recorded as the core's, so the core's ShutdownModule takes
	// exactly those -- and a class that moves to another module takes its tag's module with it.
	TArray<FDreamUIWidgetRegistry::FEntry> Entries;
	FDreamUIWidgetRegistry::GetAllEntries(Entries);
	int32 NumCore = 0;
	for (const FDreamUIWidgetRegistry::FEntry& Entry : Entries)
	{
		const UClass* Class = Entry.ClassGetter != nullptr ? Entry.ClassGetter() : nullptr;
		if (Class != nullptr && Class->GetOutermost()->GetFName() == FName(TEXT("/Script/DreamGUI")))
		{
			++NumCore;
			TestEqual(*FString::Printf(TEXT("'%s.%s' is recorded as the core module's"), *Entry.Scope.ToString(), *Entry.Name.ToString()),
				Entry.Module, FName(TEXT("DreamGUI")));
		}
	}
	TestTrue(TEXT("and the core declares tags to check"), NumCore > 20);
	return true;
}

#endif
