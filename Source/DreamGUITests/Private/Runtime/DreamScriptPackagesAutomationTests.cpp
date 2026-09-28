// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"

#include "Controls/DreamButton.h"
#include "Core/DreamUIScriptPackages.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"
#include "Misc/ScopeExit.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "UObject/CoreRedirects.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * Where the plugin's own types are looked for, now that nothing spells "/Script/DreamGUI" to find or
 * count them: the list of runtime script packages, the widget registry that forgets a module's tags when
 * the module goes, and the .dui lookups, which follow a type to wherever the redirects say it lives now.
 *
 * The redirect cases add a redirect of their own, from a module that does not exist to a class that does
 * -- the shape a class takes once it has moved to another module -- and take it away again afterwards.
 */
namespace DreamScriptPackagesTestLocal
{
	const TCHAR* const RedirectSource = TEXT("DreamGUITests");

	/** CoreRedirects in force for the length of a scope, whatever the test does in between. */
	struct FScopedRedirects
	{
		explicit FScopedRedirects(TArray<FCoreRedirect> InRedirects)
			: Redirects(MoveTemp(InRedirects))
		{
			FCoreRedirects::AddRedirectList(Redirects, RedirectSource);
		}
		~FScopedRedirects()
		{
			FCoreRedirects::RemoveRedirectList(Redirects, RedirectSource);
		}
		FScopedRedirects(const FScopedRedirects&) = delete;
		FScopedRedirects& operator=(const FScopedRedirects&) = delete;

		TArray<FCoreRedirect> Redirects;
	};

	/** The tree a .dui text builds, and what the build had to say about it. */
	struct FBuilt
	{
		FDreamUIDiagnosticBag Diagnostics;
		TStrongObjectPtr<UDreamWidgetTree> Tree;

		UDreamWidget* Find(const TCHAR* InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Tree.IsValid())
			{
				Tree->ForEachWidget([InDisplayName, &Found](UDreamWidget* InWidget)
				{
					if (Found == nullptr && IsValid(InWidget) && InWidget->GetDisplayName() == InDisplayName)
					{
						Found = InWidget;
					}
				});
			}
			return Found;
		}
	};

	FBuilt Build(const FString& InText)
	{
		FBuilt Built;
		Built.Diagnostics.SourceName = TEXT("ScriptPackages.dui");
		FDreamUIAst Ast;
		if (FDreamUISourceFile::Parse(InText, Built.Diagnostics.SourceName, Ast, Built.Diagnostics))
		{
			TArray<FDreamWidgetPropertyBinding> Bindings;
			Built.Tree.Reset(FDreamUITextBuilder::Build(Ast, GetTransientPackage(), Built.Diagnostics, Bindings));
		}
		return Built;
	}
}

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

	// Every scoped tag is recorded as the module its class lives in, so that module's ShutdownModule takes
	// exactly its own -- and a class that moves to another module takes its tag's module with it: the
	// Native.* controls' tags are DreamGUIControls' now. (GetAllEntries lists the scoped tags; the bare visual
	// tags have no scope to key by and are enumerated elsewhere.)
	TArray<FDreamUIWidgetRegistry::FEntry> Entries;
	FDreamUIWidgetRegistry::GetAllEntries(Entries);
	TMap<FName, int32> NumByModule;
	for (const FDreamUIWidgetRegistry::FEntry& Entry : Entries)
	{
		const UClass* Class = Entry.ClassGetter != nullptr ? Entry.ClassGetter() : nullptr;
		const FName Package = Class != nullptr ? Class->GetOutermost()->GetFName() : NAME_None;
		if (!DreamUI::IsRuntimeScriptPackage(Package))
		{
			continue;
		}
		const FName OwningModule(*FPackageName::GetShortName(Package));
		++NumByModule.FindOrAdd(OwningModule);
		TestEqual(*FString::Printf(TEXT("'%s.%s' is recorded as %s's"), *Entry.Scope.ToString(), *Entry.Name.ToString(), *OwningModule.ToString()),
			Entry.Module, OwningModule);
	}
	int32 NumChecked = 0;
	for (const TPair<FName, int32>& Pair : NumByModule)
	{
		AddInfo(FString::Printf(TEXT("%s declares %d tags"), *Pair.Key.ToString(), Pair.Value));
		NumChecked += Pair.Value;
	}
	TestTrue(TEXT("and the plugin's modules declare tags to check"), NumChecked > 20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDuiTypePathRedirectTest,
	"DreamGUI.Text.ATypePathAnOlderFileWroteResolvesToWhereTheRedirectsSayTheTypeLivesNow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDuiTypePathRedirectTest::RunTest(const FString& Parameters)
{
	using namespace DreamScriptPackagesTestLocal;

	// A behaviour by the path an older file wrote, through a redirect the plugin ships.
	TestEqual(TEXT("an old behaviour path the plugin's redirects rename resolves to the renamed class"),
		FDreamUITextBuilder::ResolveComponentClass(TEXT("/Script/DreamGUI.UIButtonComponent")), UUIButton::StaticClass());
	TestEqual(TEXT("and the short name resolves through the package list"),
		FDreamUITextBuilder::ResolveComponentClass(TEXT("UIButton")), UUIButton::StaticClass());

	// And a class that has moved to another module, by the path it had before the move. The redirects lead
	// to wherever the two classes live today, read off the classes, so the next move does not strand them.
	const FScopedRedirects Moved({
		FCoreRedirect(ECoreRedirectFlags::Type_Class, TEXT("/Script/DreamGUITestsOnlyModule.MovedButtonBehaviour"), UUIButton::StaticClass()->GetPathName()),
		FCoreRedirect(ECoreRedirectFlags::Type_Class, TEXT("/Script/DreamGUITestsOnlyModule.MovedButton"), UDreamButton::StaticClass()->GetPathName()),
	});
	TestEqual(TEXT("a behaviour named by the path it had before it moved resolves"),
		FDreamUITextBuilder::ResolveComponentClass(TEXT("/Script/DreamGUITestsOnlyModule.MovedButtonBehaviour")), UUIButton::StaticClass());

	const FBuilt Built = Build(FString::Join(TArray<FString>{
		TEXT("Widget Root {"),
		TEXT("    /Script/DreamGUITestsOnlyModule.MovedButton Confirm {"),
		TEXT("    }"),
		TEXT("}"),
	}, TEXT("\n")));
	TestEqual(TEXT("a node naming a control by the path it had before it moved builds without errors"), Built.Diagnostics.NumErrors(), 0);
	UDreamWidget* Confirm = Built.Find(TEXT("Confirm"));
	if (TestNotNull(TEXT("the node is in the tree"), Confirm))
	{
		TestEqual(TEXT("as the class the redirect names"), Confirm->GetClass(), UDreamButton::StaticClass());
	}
	return true;
}

#endif
