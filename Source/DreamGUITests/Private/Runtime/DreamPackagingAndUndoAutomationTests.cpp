// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
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
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "DreamCrosscuttingTestTypes.h"
#include "UObject/Package.h"
#include "Utils/DreamUIUtils.h"

/*
 * The parts of DreamGUI that only a package, a cook or an undo ever exercises.
 *
 * Everything asserted here shares one shape: the editor never sees it. The suite runs in an editor
 * process with every asset on disk, all 691 tests carrying EditorContext, so a config file that is
 * read too late to matter, a redirect that names a class nobody kept, a descriptor that does not
 * list the platforms its modules compile for, and a check() that only exists in a Development
 * package are all invisible from inside. They are also all decidable without building anything --
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
	 * Load-bearing for everything below: an ini comment may quote the very text being searched for,
	 * and the file this test reads opens with a comment explaining why it must not declare
	 * [CoreRedirects]. A whole-file Contains() cannot tell the rule from the text that states it.
	 */
	bool IsComment(const FString& TrimmedLine)
	{
		return TrimmedLine.IsEmpty() || TrimmedLine.StartsWith(TEXT(";"));
	}

	/**
	 * One ini line's `(OldName="A",NewName="B")` pair under the given `+Something=` prefix, or false
	 * when the line is not one of those. Class and struct entries have the identical shape, and the
	 * prefix is the only thing that tells them apart.
	 */
	bool ParseRedirect(const FString& Line, const TCHAR* EntryPrefix, FString& OutOld, FString& OutNew)
	{
		const FString Trimmed = Line.TrimStartAndEnd();
		if (IsComment(Trimmed) || !Trimmed.StartsWith(EntryPrefix))
		{
			return false;
		}
		const int32 OldStart = Trimmed.Find(TEXT("OldName=\""));
		const int32 NewStart = Trimmed.Find(TEXT("NewName=\""));
		if (OldStart == INDEX_NONE || NewStart == INDEX_NONE)
		{
			return false;
		}
		const int32 OldValue = OldStart + 9;
		const int32 NewValue = NewStart + 9;
		const int32 OldEnd = Trimmed.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, OldValue);
		const int32 NewEnd = Trimmed.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NewValue);
		if (OldEnd == INDEX_NONE || NewEnd == INDEX_NONE)
		{
			return false;
		}
		OutOld = Trimmed.Mid(OldValue, OldEnd - OldValue);
		OutNew = Trimmed.Mid(NewValue, NewEnd - NewValue);
		return !OutOld.IsEmpty() && !OutNew.IsEmpty();
	}

	/** One ini line's `+ClassRedirects=(OldName="A",NewName="B")` pair, or false when it is not one. */
	bool ParseClassRedirect(const FString& Line, FString& OutOld, FString& OutNew)
	{
		return ParseRedirect(Line, TEXT("+ClassRedirects="), OutOld, OutNew);
	}

	/** Whether a redirect target names a type in one of this plugin's own script modules. */
	bool TargetsThisPlugin(const FString& NewName)
	{
		return NewName.StartsWith(TEXT("/Script/DreamGUI."))
			|| NewName.StartsWith(TEXT("/Script/DreamGUIEditor."));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPluginConfigCarriesNoRedirectsTest,
	"DreamGUI.Packaging.ThePluginsOwnConfigDoesNotPretendToCarryCoreRedirects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPluginConfigCarriesNoRedirectsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPackagingTestLocal;

	// Two different files, two different reasons, both verified against the engine rather than
	// assumed -- the premise is the whole test, so it is written down here:
	//
	//   Config/DefaultDreamGUI.ini IS read. FPluginManager::ConfigureEnabledPluginForCurrentTarget
	//   ends with `ConfigContext.Load(*Plugin.Name)`, which loads it into the "DreamGUI" branch, and
	//   that is how UCLASS(config = DreamGUI) defaults ship. But that happens while plugins mount
	//   inside AppInit, and InitUObject has already read every [CoreRedirects] section out of
	//   GConfig by then -- LoadCoreModules runs before AppInit. A redirect here is not
	//   inert-but-harmless; it reads as a safety net that is not there, and the entries that used to
	//   live in this file covered fourteen control renames and two function renames the project
	//   config does not.
	//
	//   Config/DefaultEngine.ini is NOT read at all. FConfigCacheIni::AddPluginsToBranches takes each
	//   .ini in a plugin's Config dir and looks up a BRANCH by the file's base name, so a plugin
	//   layers into the engine config with Config/Engine.ini -- which is what every engine plugin
	//   that does it uses, and not one of them ships a Config/DefaultEngine.ini. "DefaultEngine" is
	//   not a branch, so FindBranch misses and the file is skipped with a Verbose log. That is what
	//   makes it usable as the copy-me template README points at.
	const FString Dir = PluginDir();
	if (!TestFalse(TEXT("the plugin manager knows where DreamGUI lives"), Dir.IsEmpty()))
	{
		return false;
	}

	TArray<FString> PluginIniLines;
	const FString PluginIniPath = FPaths::Combine(Dir, TEXT("Config"), TEXT("DefaultDreamGUI.ini"));
	if (!TestTrue(TEXT("the plugin's own config file is readable"), FFileHelper::LoadFileToStringArray(PluginIniLines, *PluginIniPath)))
	{
		return false;
	}

	bool bDeclaresCoreRedirects = false;
	bool bCarriesRedirectEntry = false;
	for (const FString& Line : PluginIniLines)
	{
		const FString Trimmed = Line.TrimStartAndEnd();
		// Comments are skipped rather than searched: this file's own header says the words
		// "[CoreRedirects]" in the course of forbidding them, and a Contains() over the whole file
		// read that sentence as the thing it forbids.
		if (IsComment(Trimmed))
		{
			continue;
		}
		if (Trimmed.Equals(TEXT("[CoreRedirects]"), ESearchCase::IgnoreCase))
		{
			bDeclaresCoreRedirects = true;
		}
		if (Trimmed.StartsWith(TEXT("+ClassRedirects="))
			|| Trimmed.StartsWith(TEXT("+FunctionRedirects="))
			|| Trimmed.StartsWith(TEXT("+StructRedirects="))
			|| Trimmed.StartsWith(TEXT("+EnumRedirects="))
			|| Trimmed.StartsWith(TEXT("+PackageRedirects=")))
		{
			bCarriesRedirectEntry = true;
		}
	}
	TestFalse(TEXT("and it declares no [CoreRedirects] section, which would never be read"), bDeclaresCoreRedirects);
	TestFalse(TEXT("nor any redirect entry under any other section"), bCarriesRedirectEntry);

	// The template the README tells the reader to copy is where they have to live instead, and the
	// entries moved out of the file above have to be in it -- on a real entry line, not in a comment
	// that mentions one.
	TArray<FString> TemplateIniLines;
	const FString TemplateIniPath = FPaths::Combine(Dir, TEXT("Config"), TEXT("DefaultEngine.ini"));
	if (!TestTrue(TEXT("the redirect template is readable"), FFileHelper::LoadFileToStringArray(TemplateIniLines, *TemplateIniPath)))
	{
		return false;
	}

	bool bHasControlRename = false;
	bool bHasUpdateToTickRename = false;
	for (const FString& Line : TemplateIniLines)
	{
		const FString Trimmed = Line.TrimStartAndEnd();
		if (IsComment(Trimmed))
		{
			continue;
		}
		if (Trimmed.StartsWith(TEXT("+ClassRedirects=")) && Trimmed.Contains(TEXT("/Script/DreamGUI.UIButtonComponent")))
		{
			bHasControlRename = true;
		}
		if (Trimmed.StartsWith(TEXT("+FunctionRedirects=")) && Trimmed.Contains(TEXT("DreamUIBehaviour.ReceiveUpdate")))
		{
			bHasUpdateToTickRename = true;
		}
	}
	TestTrue(TEXT("the control renames survived the move"), bHasControlRename);
	TestTrue(TEXT("and so did the Update-to-Tick rename on the behaviour base"), bHasUpdateToTickRename);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClassRedirectsNeverStealALiveClassTest,
	"DreamGUI.Packaging.NoClassRedirectTakesANameThatStillExistsOrHopsIntoAnotherRedirect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClassRedirectsNeverStealALiveClassTest::RunTest(const FString& Parameters)
{
	using namespace DreamPackagingTestLocal;

	// Two invariants, and the same single entry broke both of them for a month.
	//
	//   A redirect whose OldName is a class that still exists does not rescue an old asset, it
	//   hijacks a current one: the loader rewrites every reference to the live class and then fails
	//   to find whatever it was pointed at. The entry pointed the presenter component class, which
	//   was live at the time, at a prefab-era name that had never existed. Both of those classes
	//   have since been deleted and the entries naming them are gone with them -- the shape is what
	//   the checks below are for, not the particular pair.
	//
	//   CoreRedirects are applied ONCE. A redirect whose NewName is another redirect's OldName does
	//   not chain -- the loader takes one hop and stops -- so a pair like that is at best a no-op and
	//   at worst, as above, a cycle: the engine config next door carried the exact reverse of that
	//   entry for as long as both halves existed.
	const FString Dir = PluginDir();
	if (!TestFalse(TEXT("the plugin manager knows where DreamGUI lives"), Dir.IsEmpty()))
	{
		return false;
	}

	TArray<FString> Lines;
	const FString TemplateIniPath = FPaths::Combine(Dir, TEXT("Config"), TEXT("DefaultEngine.ini"));
	if (!TestTrue(TEXT("the redirect template is readable"), FFileHelper::LoadFileToStringArray(Lines, *TemplateIniPath)))
	{
		return false;
	}

	TMap<FString, FString> Redirects;
	for (const FString& Line : Lines)
	{
		FString OldName;
		FString NewName;
		if (ParseClassRedirect(Line, OldName, NewName))
		{
			Redirects.Add(OldName, NewName);
		}
	}
	TestTrue(TEXT("the template still has redirects to check"), Redirects.Num() > 0);

	for (const TPair<FString, FString>& Redirect : Redirects)
	{
		// Only a name in a module that is loaded can be looked up, which is the whole population that
		// matters: a redirect away from a class the running process does not have cannot hijack it.
		const UClass* LiveOldClass = FindObject<UClass>(nullptr, *Redirect.Key);
		TestNull(*FString::Printf(TEXT("'%s' is redirected away, so no such class may still exist"), *Redirect.Key),
			LiveOldClass);

		const FString* SecondHop = Redirects.Find(Redirect.Value);
		TestNull(*FString::Printf(TEXT("'%s' redirects to '%s', which must not itself be redirected"),
			*Redirect.Key, *Redirect.Value), SecondHop);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClassRedirectTargetsExistTest,
	"DreamGUI.Packaging.EveryClassRedirectTargetExists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClassRedirectTargetsExistTest::RunTest(const FString& Parameters)
{
	using namespace DreamPackagingTestLocal;

	// The other half of the invariant above, and the half nothing watched: that test asks whether a
	// redirect steals a name that is still live, this one asks whether it hands out a name that is
	// not. Both are the same failure to the person hitting it -- "because its class does not exist"
	// on an asset that names neither type -- because the loader rewrites the reference first and
	// only then looks it up, so a dead target turns a recoverable load into an unrecoverable one and
	// hides which name was actually written down.
	//
	// Deleting a class is where this goes wrong: the class goes, its own entries stay, and nothing
	// in a build or a cook reads this file. Nine entries naming prefab types were left pointing into
	// nothing for exactly that reason.
	//
	// Only this plugin's two script modules are checked. A target in another module is a claim about
	// that module's contents, and whether it is loaded in this process is not something this file
	// decides -- DreamGUIK2Nodes is uncooked-only, and a false red there would say nothing true.
	const FString Dir = PluginDir();
	if (!TestFalse(TEXT("the plugin manager knows where DreamGUI lives"), Dir.IsEmpty()))
	{
		return false;
	}

	TArray<FString> Lines;
	const FString TemplateIniPath = FPaths::Combine(Dir, TEXT("Config"), TEXT("DefaultEngine.ini"));
	if (!TestTrue(TEXT("the redirect template is readable"), FFileHelper::LoadFileToStringArray(Lines, *TemplateIniPath)))
	{
		return false;
	}

	int32 NumChecked = 0;
	for (const FString& Line : Lines)
	{
		FString OldName;
		FString NewName;
		if (ParseRedirect(Line, TEXT("+ClassRedirects="), OldName, NewName) && TargetsThisPlugin(NewName))
		{
			++NumChecked;
			TestNotNull(*FString::Printf(TEXT("'%s' redirects to '%s', which has to be a class that exists"),
				*OldName, *NewName), FindObject<UClass>(nullptr, *NewName));
		}
		else if (ParseRedirect(Line, TEXT("+StructRedirects="), OldName, NewName) && TargetsThisPlugin(NewName))
		{
			++NumChecked;
			TestNotNull(*FString::Printf(TEXT("'%s' redirects to '%s', which has to be a struct that exists"),
				*OldName, *NewName), FindObject<UScriptStruct>(nullptr, *NewName));
		}
	}
	// A parser that quietly stopped matching would otherwise pass this test by checking nothing.
	TestTrue(TEXT("and the file still has targets in this plugin to check"), NumChecked > 0);
	return true;
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
	// from the packaged plugin. README sends the reader to Config/DefaultEngine.ini for redirects,
	// and the MIT notice has to travel with any copy -- neither was listed.
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
		TEXT("/Config/DefaultDreamGUI.ini"),
		TEXT("/Config/DefaultEngine.ini"),
		TEXT("/Config/Game.ini"),
		TEXT("/README.md"),
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

	// The runtime module is the one that carries the classes the content names, so its allow list is
	// the list the descriptor has to cover. A platform a module compiles for but the descriptor omits
	// would have its content excluded from the cook while its code shipped.
	for (const FModuleDescriptor& Module : Descriptor.Modules)
	{
		if (Module.Name != FName(TEXT("DreamGUI")))
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
