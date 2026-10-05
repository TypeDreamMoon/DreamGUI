// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIScriptPackages.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/CoreDelegates.h"
#include "Core/DreamUIImageBrush.h"
#include "Core/DreamUISpriteData.h"
#include "Core/DreamUIStaticSpriteAtlasData.h"
#include "Core/DreamWidgetTree.h"
#include "Extensions/DreamCanvasRenderTargetPreviewer.h"
#include "Extensions/DreamPostProcessRenderElement.h"
#include "Extensions/DreamPostProcessRenderElement_Text.h"
#include "Extensions/DreamStaticMesh.h"
#include "CoreGlobals.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "K2Node.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ModuleDescriptor.h"
#include "ShaderCore.h"
#include "DreamCrosscuttingTestTypes.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Utils/DreamUIUtils.h"

/*
 * The parts of DreamGUI that only a package, a cook or an undo ever exercises.
 *
 * Everything asserted here shares one shape: the editor never sees it. The suite runs in an editor
 * process with every asset on disk, all of its tests carrying EditorContext, so a file the packaged
 * plugin leaves behind, a descriptor that does not list the platforms its modules compile for, and a
 * check() that only exists in a Development package are all invisible from inside. They are also all decidable without building anything --
 * the descriptor is parsed, the class table is in memory, and the ini files are text -- which is
 * what this file is for.
 *
 * The one thing NOT asserted here, and the reason is worth recording: PreEditChange(nullptr) does
 * not currently crash against the unfixed code either. UObject::PreEditUndo() passes NULL and the
 * plugin dereferenced it in ten places, but FField::GetFName() has a null-this compatibility guard
 * that answers NAME_None instead of faulting, and that guard is deprecated rather than gone. So the
 * undo test below pins the CONTRACT -- an undo takes no per-property branch and disturbs nothing --
 * which is what has to keep holding after the guard is removed, and it cannot go red today.
 */

namespace DreamPackagingTestLocal
{
	/** The plugin's own directory, or empty when the plugin manager does not know us. */
	FString PluginDir()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
		return Plugin.IsValid() ? Plugin->GetBaseDir() : FString();
	}

	/**
	 * Whether a line is a comment.
	 *
	 * Load-bearing for everything below: an ini comment may quote the very text being searched for --
	 * FilterPlugin.ini explains itself in comments that name folders and files. A whole-file Contains()
	 * cannot tell a rule from the text that describes it.
	 */
	bool IsComment(const FString& TrimmedLine)
	{
		return TrimmedLine.IsEmpty() || TrimmedLine.StartsWith(TEXT(";"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPackagedPluginCarriesItsDocumentedFilesTest,
	"DreamGUI.Packaging.ThePackagedPluginCarriesTheFilesItsReadmeSendsPeopleTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPackagedPluginCarriesItsDocumentedFilesTest::RunTest(const FString& Parameters)
{
	using namespace DreamPackagingTestLocal;

	// BuildPlugin's default filter takes /Source, /Content, /Resources, /Shaders and
	// /Binaries/ThirdParty and nothing else, so a file not named in FilterPlugin.ini is simply absent
	// from the packaged plugin. Game.ini is layered into the engine's Game branch, and the MIT notice has
	// to travel with any copy.
	const FString Dir = PluginDir();
	if (!TestFalse(TEXT("the plugin manager knows where DreamGUI lives"), Dir.IsEmpty()))
	{
		return false;
	}

	TArray<FString> FilterLines;
	const FString FilterPath = FPaths::Combine(Dir, TEXT("Config"), TEXT("FilterPlugin.ini"));
	if (!TestTrue(TEXT("FilterPlugin.ini is readable"), FFileHelper::LoadFileToStringArray(FilterLines, *FilterPath)))
	{
		return false;
	}

	// Rules only, never the comments around them: this file's header explains the default filter by
	// naming the folders it covers, and a rule quoted in a comment packages nothing.
	TSet<FString> Rules;
	for (const FString& Line : FilterLines)
	{
		const FString Trimmed = Line.TrimStartAndEnd();
		if (!IsComment(Trimmed) && !Trimmed.StartsWith(TEXT("[")))
		{
			Rules.Add(Trimmed);
		}
	}

	const TCHAR* const MustBePackaged[] =
	{
		TEXT("/Config/Game.ini"),
		TEXT("/README.md"),
		TEXT("/Docs/Migration.md"),
		TEXT("/LICENSE"),
	};
	for (const TCHAR* Entry : MustBePackaged)
	{
		TestTrue(*FString::Printf(TEXT("FilterPlugin.ini packages '%s'"), Entry), Rules.Contains(FString(Entry)));
		// And the rule has to name something that is there: a filter line for a file that does not
		// exist packages nothing and says nothing.
		const FString OnDisk = FPaths::Combine(Dir, FString(Entry).RightChop(1));
		TestTrue(*FString::Printf(TEXT("and '%s' exists to be packaged"), Entry),
			IFileManager::Get().FileExists(*OnDisk));
	}

	// The copy-me template the redirects once lived in stays retired. The engine never read it -- a plugin
	// layers into a config branch only through a file named for the branch, and "DefaultEngine" names
	// none -- and a stale copy of it in a project would carry redirects 1.0.0 no longer ships.
	TestFalse(TEXT("the plugin ships no Config/DefaultEngine.ini any more"),
		IFileManager::Get().FileExists(*FPaths::Combine(Dir, TEXT("Config"), TEXT("DefaultEngine.ini"))));
	TestFalse(TEXT("and FilterPlugin.ini does not ask for one"), Rules.Contains(TEXT("/Config/DefaultEngine.ini")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPluginShipsNoCoreRedirectsTest,
	"DreamGUI.Packaging.ThePluginShipsNoCoreRedirects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPluginShipsNoCoreRedirectsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPackagingTestLocal;

	// 1.0.0 carries no CoreRedirects: assets saved against LGUI, LexUI or a build before 1.0.0 are moved by
	// opening them once with 2.1.0, which carries them, and saving (Docs/Migration.md). A [CoreRedirects]
	// section back in any of the plugin's ini files would be applied by the engine to every project that
	// mounts the plugin, so it comes back only on purpose, and with this test changed to say why.
	const FString Dir = PluginDir();
	if (!TestFalse(TEXT("the plugin manager knows where DreamGUI lives"), Dir.IsEmpty()))
	{
		return false;
	}
	TArray<FString> IniFiles;
	IFileManager::Get().FindFiles(IniFiles, *FPaths::Combine(Dir, TEXT("Config"), TEXT("*.ini")), true, false);
	TestTrue(TEXT("the plugin has config files to look at"), IniFiles.Num() > 0);
	for (const FString& IniFile : IniFiles)
	{
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Dir, TEXT("Config"), IniFile));
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (!IsComment(Trimmed))
			{
				TestFalse(*FString::Printf(TEXT("Config/%s has no [CoreRedirects] section"), *IniFile),
					Trimmed.Equals(TEXT("[CoreRedirects]"), ESearchCase::IgnoreCase));
			}
		}
	}
	TestFalse(TEXT("and Config/DefaultDreamGUI.ini, where they lived, is gone"),
		IFileManager::Get().FileExists(*FPaths::Combine(Dir, TEXT("Config"), TEXT("DefaultDreamGUI.ini"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDefaultAssetsAreAlwaysCookedTest,
	"DreamGUI.Packaging.ThePluginsOwnDefaultAssetsAreCookedWithoutTheProjectAskingForThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDefaultAssetsAreAlwaysCookedTest::RunTest(const FString& Parameters)
{
	using namespace DreamPackagingTestLocal;

	// Everything UDreamGUISettings reaches for is a TSoftObjectPtr on a NATIVE CDO, which lives in
	// /Script/DreamGUI and never enters the asset registry -- so no package dependency exists for the
	// cooker to follow. Those assets survive an ordinary cook only because UE cooks all mounted
	// content by default; an explicit-package-list cook (-map=, DLC/chunk, MapsToCook,
	// -SkipSoftReferences) drops every one of them and ships a blank UI.
	//
	// The fix is a file, and the file has to be named for a config BRANCH: AddPluginsToBranches looks
	// each plugin ini up by its base name, so Config/Game.ini reaches the Game branch where
	// UProjectPackagingSettings (UCLASS(config=Game)) lives, and a Config/DefaultGame.ini would find
	// no branch called "DefaultGame" and be skipped in silence. That distinction is the whole reason
	// this test asserts the merged config rather than the file: the file being right on disk proves
	// nothing about whether the engine read it.
	const FString Dir = PluginDir();
	if (!TestFalse(TEXT("the plugin manager knows where DreamGUI lives"), Dir.IsEmpty()))
	{
		return false;
	}
	TestTrue(TEXT("the plugin ships a Config/Game.ini, the name that reaches the Game branch"),
		IFileManager::Get().FileExists(*FPaths::Combine(Dir, TEXT("Config"), TEXT("Game.ini"))));
	TestFalse(TEXT("and not a Config/DefaultGame.ini, which no branch is named for"),
		IFileManager::Get().FileExists(*FPaths::Combine(Dir, TEXT("Config"), TEXT("DefaultGame.ini"))));

	// The end of the mechanism: the entry is in the running process's merged Game config, which it
	// can only be if the plugin's layer was applied.
	TArray<FString> CookDirectories;
	GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("DirectoriesToAlwaysCook"),
		CookDirectories, GGameIni);
	const bool bCooksPluginContent = CookDirectories.ContainsByPredicate([](const FString& Entry)
	{
		return Entry.Contains(TEXT("/DreamGUI"));
	});
	TestTrue(TEXT("/DreamGUI is in DirectoriesToAlwaysCook, so an explicit-package-list cook still gets the defaults"),
		bCooksPluginContent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDescriptorPlatformsMatchItsModulesTest,
	"DreamGUI.Packaging.TheDescriptorNamesTheSamePlatformsItsModulesCompileFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDescriptorPlatformsMatchItsModulesTest::RunTest(const FString& Parameters)
{
	// Per-module PlatformAllowList decides what COMPILES. Descriptor-level SupportedTargetPlatforms
	// decides what the cooker treats as never-cook for a platform
	// (CookOnTheFlyServer::DiscoverPlatformSpecificNeverCookPackages reads only the descriptor one).
	// With the descriptor list empty, a target outside the allow list built none of the modules and
	// cooked all of /DreamGUI anyway -- every uasset in the package naming a class that is not there.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
	if (!TestTrue(TEXT("the plugin manager knows about DreamGUI"), Plugin.IsValid()))
	{
		return false;
	}

	const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
	TestTrue(TEXT("the descriptor names the platforms it supports"), Descriptor.SupportedTargetPlatforms.Num() > 0);
	TestTrue(TEXT("and the content it ships is content, which is what makes that matter"), Descriptor.bCanContainContent);

	// The runtime modules carry the classes the content names, so their allow lists are the lists the
	// descriptor has to cover -- every module whose script package is one of DreamGUI's runtime
	// packages. A platform a module compiles for but the descriptor omits would have its content
	// excluded from the cook while its code shipped.
	for (const FModuleDescriptor& Module : Descriptor.Modules)
	{
		if (!DreamUI::IsRuntimeScriptPackage(FName(*(TEXT("/Script/") + Module.Name.ToString()))))
		{
			continue;
		}
		for (const FString& Platform : Module.PlatformAllowList)
		{
			TestTrue(*FString::Printf(TEXT("the descriptor supports '%s', which the runtime module compiles for"), *Platform),
				Descriptor.SupportedTargetPlatforms.Contains(Platform));
		}
	}

	// The description is what a user reads in the plugin browser, and it named a workflow that was
	// deleted with the prefab asset model.
	TestFalse(TEXT("and the description no longer advertises the prefab workflow"),
		Descriptor.Description.Contains(TEXT("Prefab")));

	// The platform list is a claim, and README is where the claim is qualified -- which platforms
	// merely build and which have actually been run. A platform added to the descriptor and not to
	// that table is a claim nobody wrote down the status of.
	FString Readme;
	const FString ReadmePath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("README.md"));
	if (TestTrue(TEXT("README is readable"), FFileHelper::LoadFileToString(Readme, *ReadmePath)))
	{
		TestTrue(TEXT("README has a Platforms section qualifying the claim"), Readme.Contains(TEXT("## Platforms")));
		for (const FString& Platform : Descriptor.SupportedTargetPlatforms)
		{
			TestTrue(*FString::Printf(TEXT("README says where '%s' stands"), *Platform),
				Readme.Contains(Platform, ESearchCase::IgnoreCase));
		}
	}
	return true;
}

namespace DreamPackagingTestLocal
{
	/**
	 * Whether a module of this host type loads in a game run on uncooked content: the editor binary with
	 * -game (or Standalone Game), which is WITH_EDITOR and has developer tools and uncooked data, but is
	 * no commandlet, no dedicated server or client, and has GIsEditor off. The answers are
	 * FModuleDescriptor::IsLoadedInCurrentConfiguration's for that process, written out rather than
	 * asked for, because asking would mean switching GIsEditor off under a running editor.
	 */
	bool LoadsInAGameOnUncookedContent(EHostType::Type InType)
	{
		switch (InType)
		{
		case EHostType::Runtime:
		case EHostType::RuntimeNoCommandlet:
		case EHostType::RuntimeAndProgram:
		case EHostType::UncookedOnly:
		case EHostType::Developer:
		case EHostType::DeveloperTool:
		case EHostType::ServerOnly:
		case EHostType::ClientOnly:
		case EHostType::ClientOnlyNoCommandlet:
			return true;
		case EHostType::CookedOnly:
		case EHostType::Editor:
		case EHostType::EditorNoCommandlet:
		case EHostType::EditorAndProgram:
		case EHostType::Program:
		default:
			return false;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBlueprintTypesLoadInAnUncookedGameTest,
	"DreamGUI.Packaging.EveryBlueprintTypeAndNodeThePluginDefinesLoadsInAGameRunOnUncookedContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBlueprintTypesLoadInAnUncookedGameTest::RunTest(const FString& Parameters)
{
	using namespace DreamPackagingTestLocal;

	// An editor build keeps none of the Blueprint bytecode it loads ([StructSerialization]
	// SkipByteCodeSerialization, BaseEditor.ini): a Blueprint class's functions come back empty and the
	// Blueprint rebuilds them as it loads. A game run on uncooked content -- -game with the editor
	// binary, Standalone Game -- is such a build, with GIsEditor off. So a Blueprint type, or a node its
	// graphs hold, whose class lives in a module that game does not load, cannot be loaded there, nothing
	// rebuilds the class, and every function of every such asset does nothing, with no line in the log
	// to say so. UDreamWidgetBlueprint was one, in the Editor module DreamGUIEditor: a stress level whose
	// widgets ran On Construct -> Delay -> Play Animation animated in PIE and stood still in -game.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
	if (!TestTrue(TEXT("the plugin manager knows about DreamGUI"), Plugin.IsValid()))
	{
		return false;
	}
	const TArray<FModuleDescriptor>& Modules = Plugin->GetDescriptor().Modules;

	int32 BlueprintTypes = 0;
	int32 Nodes = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		const UClass* Class = *It;
		const bool bIsBlueprintType = Class->IsChildOf(UBlueprint::StaticClass());
		const bool bIsNode = Class->IsChildOf(UK2Node::StaticClass());
		if ((!bIsBlueprintType && !bIsNode) || !Class->HasAnyClassFlags(CLASS_Native))
		{
			continue;
		}
		// A native class's package is /Script/<its module>.
		const FName ModuleName(*FPackageName::GetShortName(Class->GetOutermost()->GetName()));
		const FModuleDescriptor* Module = Modules.FindByPredicate([ModuleName](const FModuleDescriptor& InModule)
		{
			return InModule.Name == ModuleName;
		});
		if (Module == nullptr)
		{
			// Not one of this plugin's.
			continue;
		}
		if (bIsBlueprintType)
		{
			++BlueprintTypes;
		}
		else
		{
			++Nodes;
		}
		TestTrue(*FString::Printf(TEXT("%s is in %s, a module a game on uncooked content loads (it is %s)"),
				*Class->GetName(), *ModuleName.ToString(), EHostType::ToString(Module->Type)),
			LoadsInAGameOnUncookedContent(Module->Type));
	}
	// Otherwise the loop above could pass by looking at nothing.
	TestTrue(TEXT("the plugin's Blueprint types were found -- UDreamWidgetBlueprint at least"), BlueprintTypes > 0);
	TestTrue(TEXT("and so were its Blueprint nodes"), Nodes > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamShaderDirectoryMappedOnceTest,
	"DreamGUI.Packaging.ThePluginsShaderDirectoryIsMappedExactlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamShaderDirectoryMappedOnceTest::RunTest(const FString& Parameters)
{
	// StartupModule maps /Plugin/DreamGUI to the plugin's Shaders folder. The mapping is
	// process-wide and permanent -- the engine offers no per-directory unregister, so ShutdownModule
	// cannot undo it -- and AddShaderSourceDirectoryMapping check()s that the virtual directory is
	// not mapped yet. The module reaching StartupModule a second time is not exotic: disabling and
	// re-enabling the plugin in a running editor does it.
	const TMap<FString, FString>& Mappings = AllShaderSourceDirectoryMappings();
	const FString* RealDir = Mappings.Find(TEXT("/Plugin/DreamGUI"));
	if (!TestNotNull(TEXT("the plugin's shader directory is mapped"), RealDir))
	{
		return false;
	}
	TestTrue(TEXT("and it points at a directory that exists"), IFileManager::Get().DirectoryExists(**RealDir));
	TestTrue(TEXT("and that directory is the plugin's own Shaders folder"), RealDir->EndsWith(TEXT("Shaders")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamManagerSubsystemLeavesNoGlobalSubscriptionBehindTest,
	"DreamGUI.Lifecycle.AWorldSubsystemTakesItsGlobalSubscriptionsWithItWhenTheWorldGoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamManagerSubsystemLeavesNoGlobalSubscriptionBehindTest::RunTest(const FString& Parameters)
{
	// UDreamUIManagerWorldSubsystem::Initialize subscribes to two process-wide multicasts and
	// Deinitialize used to unsubscribe from neither. AddUObject is a weak binding, so nothing crashed
	// -- the cost is that every PIE start/stop added one more entry to two global arrays for the
	// length of an editor session, and that the subsystem kept receiving OnEndOfFrame between its own
	// Deinitialize and its collection.
	//
	// Observable at exactly one moment, which is why this is written the way it is:
	// UWorld::DestroyWorld -> CleanupWorld(true, true) runs SubsystemCollection.Deinitialize()
	// SYNCHRONOUSLY, so the assertion below happens after Deinitialize and before the subsystem is
	// collected. After a GC the leaked entry's weak pointer resolves to null and IsBoundToObject
	// stops matching it, which would make a leaking build look clean.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("a world can be created"), World))
	{
		return false;
	}

	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
	if (!TestNotNull(TEXT("and it has a DreamUI manager subsystem"), Manager))
	{
		World->DestroyWorld(false);
		return false;
	}

	// The other half of the pair: a test that only checked the unsubscribe would still pass if the
	// subscribe silently stopped happening.
	TestTrue(TEXT("an initialized manager is on OnEndFrame"), FCoreDelegates::OnEndFrame.IsBoundToObject(Manager));
	TestTrue(TEXT("and on OnEnginePreExit"), FCoreDelegates::OnEnginePreExit.IsBoundToObject(Manager));

	World->DestroyWorld(false);

	TestFalse(TEXT("and it is off OnEndFrame once its world is gone"), FCoreDelegates::OnEndFrame.IsBoundToObject(Manager));
	TestFalse(TEXT("and off OnEnginePreExit too"), FCoreDelegates::OnEnginePreExit.IsBoundToObject(Manager));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUndoAsksAboutNoPropertyTest,
	"DreamGUI.Lifecycle.AnUndoAsksEveryDreamObjectAboutNoPropertyAtAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUndoAsksAboutNoPropertyTest::RunTest(const FString& Parameters)
{
	// UObject::PreEditUndo() is `PreEditChange(NULL)`, and the transaction buffer calls it on every
	// object in a transaction it replays -- so this is the shape of every Ctrl+Z that touched a
	// DreamGUI object. Ten overrides read the argument's name without checking it first.
	//
	// This cannot go red against the unfixed code: FField::GetFName() has a null-this compatibility
	// guard that answers NAME_None, so an unguarded dereference survives -- today. What is pinned is
	// the contract that has to keep holding once that deprecated guard is gone: an undo names no
	// property, so no per-property branch may run and nothing may be disturbed.
	// Every visual in the plugin that overrides PreEditChange, each on its own widget because a widget
	// holds exactly one visual. The widget itself overrides it too, and is asked alongside them.
	UClass* const VisualClasses[] =
	{
		UDreamImage::StaticClass(),
		UDreamText::StaticClass(),
		UDreamStaticMesh::StaticClass(),
		UDreamPostProcessRenderElement::StaticClass(),
		UDreamPostProcessRenderElement_Text::StaticClass(),
		UDreamCanvasRenderTargetPreviewer::StaticClass(),
	};

	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	for (UClass* VisualClass : VisualClasses)
	{
		UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), FName(*FString::Printf(TEXT("UndoTarget_%s"), *VisualClass->GetName())));
		if (!TestNotNull(TEXT("the authoring tree builds a widget"), Widget))
		{
			continue;
		}
		Widget->SetWidth(120.0f);
		Widget->SetHeight(60.0f);
		UDreamVisual* Visual = Widget->CreateNewVisual(VisualClass);

		// Twice, because an undo and a redo both come through here and the first must not leave the
		// object in a state the second cannot survive.
		Widget->PreEditUndo();
		Widget->PreEditUndo();
		TestTrue(TEXT("the widget survives being asked about no property"), IsValid(Widget));

		if (TestNotNull(*FString::Printf(TEXT("a %s visual exists on it"), *VisualClass->GetName()), Visual))
		{
			UObject* BrushResourceBefore = nullptr;
			UDreamImage* Image = Cast<UDreamImage>(Visual);
			if (Image != nullptr)
			{
				BrushResourceBefore = Image->GetBrush().GetResourceObject();
			}

			Visual->PreEditUndo();
			Visual->PreEditUndo();
			TestTrue(*FString::Printf(TEXT("and %s survives it too"), *VisualClass->GetName()), IsValid(Visual));

			if (Image != nullptr && BrushResourceBefore != nullptr)
			{
				// The one branch with something observable behind it: the image's PreEditChange
				// unregisters the brush from its sprite when the brush property is the one changing,
				// and an undo names no property at all.
				TestSamePtr(TEXT("and an undo that named nothing did not unregister the brush"),
					Image->GetBrush().GetResourceObject(), BrushResourceBefore);
			}
		}
		Widget->DestroyWidget();
	}

	// And the asset-side overrides, which an undo in their own editor reaches the same way.
	UDreamUISpriteData* SpriteData = NewObject<UDreamUISpriteData>(GetTransientPackage());
	UDreamUIStaticSpriteAtlasData* AtlasData = NewObject<UDreamUIStaticSpriteAtlasData>(GetTransientPackage());
	for (UObject* Asset : { static_cast<UObject*>(SpriteData), static_cast<UObject*>(AtlasData) })
	{
		if (Asset == nullptr)
		{
			continue;
		}
		Asset->PreEditUndo();
		Asset->PreEditUndo();
		TestTrue(*FString::Printf(TEXT("'%s' survives being asked about no property"), *Asset->GetClass()->GetName()),
			IsValid(Asset));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUMGWidgetWithoutWorldTest,
	"DreamGUI.Extensions.AUMGWidgetWithNoWorldStillAnswersWhatTimeItIsAndWhetherToDraw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUMGWidgetWithoutWorldTest::RunTest(const FString& Parameters)
{
	// Two sibling extensions were hardened against a missing world in September and this one was not,
	// so it kept `GetWorld()->TimeSince(GetWorld()->LastRenderTime)` and
	// `GetWorld()->GetTimeSeconds()` as bare dereferences, plus a bare `GetWidget()->` in
	// IsWidgetVisible. All three states are ordinary: a component being torn down with its world, and
	// a component in a Blueprint's authoring tree, which has no world by construction.
	UDreamUMGWidgetTimingProbe* Orphan = NewObject<UDreamUMGWidgetTimingProbe>(GetTransientPackage());
	if (!TestNotNull(TEXT("a UMG widget visual can be built with no widget and no world"), Orphan))
	{
		return false;
	}
	TestNull(TEXT("and it really has no world"), Orphan->GetWorld());
	TestNull(TEXT("and no owning widget either"), Orphan->GetWidget());

	TestFalse(TEXT("so it is not visible, rather than crashing on the question"), Orphan->IsWidgetVisible());
	TestFalse(TEXT("and it has nothing to draw"), Orphan->CallShouldDrawWidget());

	Orphan->UseRealTime();
	TestTrue(TEXT("the real-time clock never needed a world"), Orphan->CallGetCurrentTime() > 0.0);

	Orphan->UseGameTime();
	TestTrue(TEXT("and the game-time branch falls back to it rather than dereferencing nothing"),
		Orphan->CallGetCurrentTime() > 0.0);

	// On a worldless authoring tree the visual DOES have a widget, and the widget has no canvas --
	// the other half of the same pair, and the half a test can build the ordinary way.
	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Hosted"));
	if (!TestNotNull(TEXT("the authoring tree has a widget"), Widget))
	{
		return false;
	}
	Tree->RootWidget = Widget;
	Widget->CreateNewVisual(UDreamUMGWidgetTimingProbe::StaticClass());
	UDreamUMGWidgetTimingProbe* Hosted = Cast<UDreamUMGWidgetTimingProbe>(Widget->GetVisual());
	if (TestNotNull(TEXT("and the probe is its visual"), Hosted))
	{
		TestNotNull(TEXT("which does have an owning widget"), Hosted->GetWidget());
		TestNull(TEXT("but no render canvas, being an authoring tree"), Hosted->GetWidget()->GetRenderCanvas());
		TestFalse(TEXT("so there is still nothing to draw"), Hosted->CallShouldDrawWidget());
		Hosted->UseGameTime();
		TestTrue(TEXT("and the clock still answers"), Hosted->CallGetCurrentTime() > 0.0);
	}
	Widget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAtlasRepackOnANonSpriteBrushTest,
	"DreamGUI.Visuals.AnAtlasRepackReachingAnImageWhoseBrushIsNoLongerASpriteIsIgnored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAtlasRepackOnANonSpriteBrushTest::RunTest(const FString& Parameters)
{
	// ApplyAtlasTextureChange is broadcast by the sprite data to everything on its registration list.
	// What the brush holds is decided elsewhere, so "registered" does not imply "still a sprite" --
	// and the old code asserted that it did and then read the answer through a C-style cast, which
	// reinterprets whatever object is really there. check() is compiled out in Shipping, so the same
	// state was a hard crash in a Development or Test package and a silent bad read for players.
	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Repack"));
	if (!TestNotNull(TEXT("the authoring tree has a widget"), Widget))
	{
		return false;
	}
	Tree->RootWidget = Widget;
	Widget->CreateNewVisual(UDreamImage::StaticClass());
	UDreamImage* Image = Cast<UDreamImage>(Widget->GetVisual());
	if (!TestNotNull(TEXT("with an image on it"), Image))
	{
		Widget->DestroyWidget();
		return false;
	}

	// A plain texture is a legal brush resource and is not a sprite, which is the whole state under
	// test.
	UTexture2D* PlainTexture = FDreamUIUtils::GetDefaultWhiteTexture();
	if (!TestNotNull(TEXT("a plain texture is available to put in the brush"), PlainTexture))
	{
		Widget->DestroyWidget();
		return false;
	}
	FDreamUIImageBrush PlainTextureBrush = Image->GetBrush();
	PlainTextureBrush.SetResourceObject(PlainTexture);
	Image->SetBrush(PlainTextureBrush);
	TestNull(TEXT("the brush no longer holds a sprite"),
		Cast<UDreamUISpriteData_BaseObject>(Image->GetBrush().GetResourceObject()));

	Image->ApplyAtlasTextureChange_Implementation();

	TestTrue(TEXT("the image survives a repack that no longer concerns it"), IsValid(Image));
	TestSamePtr(TEXT("and its brush is left exactly as it was"),
		Image->GetBrush().GetResourceObject(), static_cast<UObject*>(PlainTexture));

	Widget->DestroyWidget();
	return true;
}

#endif
