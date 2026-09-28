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
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "DreamCrosscuttingTestTypes.h"
#include "UObject/CoreRedirects.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "Utils/DreamUIUtils.h"

/*
 * The parts of DreamGUI that only a package, a cook or an undo ever exercises.
 *
 * Everything asserted here shares one shape: the editor never sees it. The suite runs in an editor
 * process with every asset on disk, all of its tests carrying EditorContext, so a redirect that never
 * reaches the engine, a redirect that names a type nobody kept, a descriptor that does not list the
 * platforms its modules compile for, and a check() that only exists in a Development package are all
 * invisible from inside. They are also all decidable without building anything --
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

	/** The file every CoreRedirect the plugin ships lives in. */
	FString RedirectIniPath(const FString& InPluginDir)
	{
		return FPaths::Combine(InPluginDir, TEXT("Config"), TEXT("DefaultDreamGUI.ini"));
	}

	/**
	 * Whether a line is a comment.
	 *
	 * Load-bearing for everything below: an ini comment may quote the very text being searched for --
	 * the redirect file explains itself in comments that name sections and entries. A whole-file
	 * Contains() cannot tell a rule from the text that describes it.
	 */
	bool IsComment(const FString& TrimmedLine)
	{
		return TrimmedLine.IsEmpty() || TrimmedLine.StartsWith(TEXT(";"));
	}

	/**
	 * One ini line's `(OldName="A",NewName="B")` pair under the given `+Something=` prefix, or false
	 * when the line is not one of those. Every kind of entry has the identical shape, and the prefix is
	 * the only thing that tells them apart.
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

	/** One redirect of the file: its kind ("Class", "Struct", "Enum", "Function", "Object", "Package") and both names. */
	struct FRedirectEntry
	{
		FString Kind;
		FString OldName;
		FString NewName;
	};

	/** Every redirect entry of the plugin's redirect file, in file order; false when the file cannot be read. */
	bool ReadRedirects(const FString& InPluginDir, TArray<FRedirectEntry>& OutEntries)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *RedirectIniPath(InPluginDir)))
		{
			return false;
		}
		static const TCHAR* const Kinds[] = { TEXT("Class"), TEXT("Struct"), TEXT("Enum"), TEXT("Function"), TEXT("Object"), TEXT("Package") };
		for (const FString& Line : Lines)
		{
			for (const TCHAR* Kind : Kinds)
			{
				FString OldName;
				FString NewName;
				if (ParseRedirect(Line, *FString::Printf(TEXT("+%sRedirects="), Kind), OldName, NewName))
				{
					OutEntries.Add({ FString(Kind), MoveTemp(OldName), MoveTemp(NewName) });
					break;
				}
			}
		}
		return true;
	}

	/**
	 * Whether a redirect target names a type in one of this plugin's own modules: every runtime module
	 * that registered its script package, and the editor module. A target in any other module is a claim
	 * about that module's contents, and whether it is loaded in this process is not something this file
	 * decides -- DreamGUIK2Nodes is uncooked-only, and a false red there would say nothing true.
	 */
	bool TargetsThisPlugin(const FString& NewName)
	{
		return DreamUI::IsInRuntimeScriptPackage(NewName)
			|| NewName.StartsWith(TEXT("/Script/DreamGUIEditor."));
	}

	/** The live object of a type redirect's kind at InPath, or null. FindObject applies no redirect, which is the point. */
	const UObject* FindTypeOfKind(const FString& InKind, const FString& InPath)
	{
		if (InKind == TEXT("Class"))
		{
			return FindObject<UClass>(nullptr, *InPath);
		}
		if (InKind == TEXT("Struct"))
		{
			return FindObject<UScriptStruct>(nullptr, *InPath);
		}
		if (InKind == TEXT("Enum"))
		{
			return FindObject<UEnum>(nullptr, *InPath);
		}
		if (InKind == TEXT("Object"))
		{
			return FindObject<UObject>(nullptr, *InPath);
		}
		return nullptr;
	}

	/**
	 * The function a FunctionRedirects name names, or null.
	 *
	 * Written two ways in the file: "/Script/DreamGUI.Class.Function", and a short "Class.Function" the
	 * engine matches in any package -- which, in this plugin's file, means one of this plugin's runtime
	 * packages. A function's own path separates it from its class with ':', not the '.' a redirect
	 * writes, so it is found through the class.
	 */
	const UFunction* FindRedirectedFunction(const FString& InName)
	{
		// A global delegate's signature is a function of the package itself, and its name says so directly:
		// "/Script/DreamGUIExtensions.DreamLyricsLineChangedEvent__DelegateSignature".
		if (InName.StartsWith(TEXT("/")))
		{
			if (const UFunction* OfThePackage = FindObject<UFunction>(nullptr, *InName))
			{
				return OfThePackage;
			}
		}
		FString Owner;
		FString Function;
		if (!InName.Split(TEXT("."), &Owner, &Function, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			return nullptr;
		}
		const UClass* Class = nullptr;
		if (Owner.StartsWith(TEXT("/")))
		{
			Class = FindObject<UClass>(nullptr, *Owner);
		}
		else
		{
			for (const FName Package : DreamUI::GetRuntimeScriptPackages())
			{
				Class = FindObject<UClass>(nullptr, *FString::Printf(TEXT("%s.%s"), *Package.ToString(), *Owner));
				if (Class != nullptr)
				{
					break;
				}
			}
		}
		return Class != nullptr ? Class->FindFunctionByName(FName(*Function)) : nullptr;
	}

	/** The CoreRedirects type flag of a type redirect's kind, or Type_None for the kinds that are not a type. */
	ECoreRedirectFlags TypeFlagOfKind(const FString& InKind)
	{
		if (InKind == TEXT("Class"))
		{
			return ECoreRedirectFlags::Type_Class;
		}
		if (InKind == TEXT("Struct"))
		{
			return ECoreRedirectFlags::Type_Struct;
		}
		if (InKind == TEXT("Enum"))
		{
			return ECoreRedirectFlags::Type_Enum;
		}
		return ECoreRedirectFlags::None;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPluginConfigCarriesItsRedirectsTest,
	"DreamGUI.Packaging.ThePluginsOwnConfigCarriesItsCoreRedirectsAndTheEngineAppliesThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPluginConfigCarriesItsRedirectsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPackagingTestLocal;

	// The premise is the whole test, so it is written down here, as verified against the engine:
	//
	//   Config/DefaultDreamGUI.ini is mounted as the "DreamGUI" config branch while plugins mount during
	//   AppInit, and InitUObject -- bound to FCoreDelegates::OnInit, which AppInit broadcasts at its very
	//   end -- reads the [CoreRedirects] section of every config branch, this one included, before the
	//   first package loads. Started with -LogCmds="LogCoreRedirects Verbose", the editor logs
	//   "AddRedirect(.../DreamGUI.ini) adding N redirects" among the engine's own branches.
	//
	// It used to be believed that the file was mounted too late to count, so every redirect lived in a
	// template each project had to copy into its own config, and a test here asserted the opposite of
	// this one. A project that copied that block should delete its copy.
	const FString Dir = PluginDir();
	if (!TestFalse(TEXT("the plugin manager knows where DreamGUI lives"), Dir.IsEmpty()))
	{
		return false;
	}

	TArray<FRedirectEntry> Entries;
	if (!TestTrue(TEXT("the plugin's redirect file is readable"), ReadRedirects(Dir, Entries)))
	{
		return false;
	}
	TMap<FString, int32> CountByKind;
	for (const FRedirectEntry& Entry : Entries)
	{
		++CountByKind.FindOrAdd(Entry.Kind);
	}
	for (const TCHAR* Kind : { TEXT("Class"), TEXT("Struct"), TEXT("Enum"), TEXT("Function"), TEXT("Package") })
	{
		TestTrue(*FString::Printf(TEXT("the file carries %s redirects"), Kind), CountByKind.FindRef(Kind) > 0);
	}

	// The DreamGUI branch is one of the files InitUObject reads [CoreRedirects] from: GConfig's filenames,
	// each read by that very name. (GetConfigFilename spells a plugin branch's path differently.) The
	// section itself is not looked for in memory -- ReadRedirectsFromIni removes each [CoreRedirects]
	// section from its branch once it has read it, so by the time a test runs there is nothing to find.
	bool bBranchIsRead = false;
	for (const FString& Filename : GConfig->GetFilenames())
	{
		bBranchIsRead |= FPaths::GetCleanFilename(Filename).Equals(TEXT("DreamGUI.ini"), ESearchCase::IgnoreCase);
	}
	TestTrue(TEXT("the DreamGUI config branch is among the files InitUObject reads redirects from"), bBranchIsRead);

	// What shows the file was read is that its entries are in force, with nothing else there to answer
	// for it: the project's own config carries no copy of them (a project that copied the old template's
	// block should delete it, as the README says), and the engine answers every type redirect the way the
	// file says.
	TArray<FString> ProjectIniFiles;
	IFileManager::Get().FindFiles(ProjectIniFiles, *(FPaths::ProjectConfigDir() / TEXT("*.ini")), true, false);
	for (const FString& IniFile : ProjectIniFiles)
	{
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectConfigDir() / IniFile));
		int32 NumCopied = 0;
		for (const FString& Line : Lines)
		{
			NumCopied += Line.Contains(TEXT("Redirects=")) && Line.Contains(TEXT("/Script/DreamGUI")) ? 1 : 0;
		}
		TestEqual(*FString::Printf(TEXT("the project's %s carries no copy of DreamGUI's redirects"), *IniFile), NumCopied, 0);
	}

	int32 NumChecked = 0;
	for (const FRedirectEntry& Entry : Entries)
	{
		const ECoreRedirectFlags Flag = TypeFlagOfKind(Entry.Kind);
		if (Flag == ECoreRedirectFlags::None)
		{
			continue;
		}
		++NumChecked;
		const FCoreRedirectObjectName Redirected = FCoreRedirects::GetRedirectedName(Flag, FCoreRedirectObjectName(Entry.OldName));
		TestEqual(*FString::Printf(TEXT("the engine redirects %s '%s'"), *Entry.Kind, *Entry.OldName),
			Redirected.ToString(), FCoreRedirectObjectName(Entry.NewName).ToString());
	}
	TestTrue(TEXT("and there were type redirects to check"), NumChecked > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRedirectsNeverStealALiveTypeTest,
	"DreamGUI.Packaging.NoRedirectTakesANameThatStillExistsOrHopsIntoAnotherRedirect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRedirectsNeverStealALiveTypeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPackagingTestLocal;

	// Two invariants, and the same single entry broke both of them for a month.
	//
	//   A redirect whose OldName is a type that still exists does not rescue an old asset, it hijacks a
	//   current one: the loader rewrites every reference to the live type and then fails to find whatever
	//   it was pointed at. The entry pointed the presenter component class, which was live at the time, at
	//   a prefab-era name that had never existed. Both of those classes have since been deleted and the
	//   entries naming them are gone with them -- the shape is what the checks below are for, not the
	//   particular pair.
	//
	//   CoreRedirects are applied ONCE. A redirect whose NewName is another redirect's OldName does not
	//   chain -- the loader takes one hop and stops -- so a pair like that is at best a no-op and at worst,
	//   as above, a cycle.
	//
	// Classes, structs and enums alike: an enum redirect that takes a live enum's name rewrites every
	// property of that enum on load.
	const FString Dir = PluginDir();
	if (!TestFalse(TEXT("the plugin manager knows where DreamGUI lives"), Dir.IsEmpty()))
	{
		return false;
	}

	TArray<FRedirectEntry> Entries;
	if (!TestTrue(TEXT("the plugin's redirect file is readable"), ReadRedirects(Dir, Entries)))
	{
		return false;
	}

	TMap<FString, TMap<FString, FString>> ByKind;
	for (const FRedirectEntry& Entry : Entries)
	{
		if (TypeFlagOfKind(Entry.Kind) != ECoreRedirectFlags::None)
		{
			ByKind.FindOrAdd(Entry.Kind).Add(Entry.OldName, Entry.NewName);
		}
	}
	TestTrue(TEXT("the file still has type redirects to check"), ByKind.Num() > 0);

	for (const TPair<FString, TMap<FString, FString>>& Kind : ByKind)
	{
		for (const TPair<FString, FString>& Redirect : Kind.Value)
		{
			// Only a name in a module that is loaded can be looked up, which is the whole population that
			// matters: a redirect away from a type the running process does not have cannot hijack it.
			TestNull(*FString::Printf(TEXT("%s '%s' is redirected away, so no such %s may still exist"), *Kind.Key, *Redirect.Key, *Kind.Key.ToLower()),
				FindTypeOfKind(Kind.Key, Redirect.Key));

			const FString* SecondHop = Kind.Value.Find(Redirect.Value);
			TestNull(*FString::Printf(TEXT("%s '%s' redirects to '%s', which must not itself be redirected"),
				*Kind.Key, *Redirect.Key, *Redirect.Value), SecondHop);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRedirectTargetsExistTest,
	"DreamGUI.Packaging.EveryRedirectTargetExists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRedirectTargetsExistTest::RunTest(const FString& Parameters)
{
	using namespace DreamPackagingTestLocal;

	// The other half of the invariant above, and the half nothing watched: that test asks whether a
	// redirect steals a name that is still live, this one asks whether it hands out a name that is not.
	// Both are the same failure to the person hitting it -- "because its class does not exist" on an
	// asset that names neither type -- because the loader rewrites the reference first and only then
	// looks it up, so a dead target turns a recoverable load into an unrecoverable one and hides which
	// name was actually written down.
	//
	// Deleting a type is where this goes wrong: the type goes, its own entries stay, and nothing in a
	// build or a cook reads this file. Nine entries naming prefab types were left pointing into nothing
	// for exactly that reason, and an enum entry into a deleted enum outlived it the same way.
	//
	// Every kind whose target is a named thing: classes, structs, enums, objects (a global delegate's
	// signature, when one moves) and functions. Package redirects name packages and are left alone.
	const FString Dir = PluginDir();
	if (!TestFalse(TEXT("the plugin manager knows where DreamGUI lives"), Dir.IsEmpty()))
	{
		return false;
	}

	TArray<FRedirectEntry> Entries;
	if (!TestTrue(TEXT("the plugin's redirect file is readable"), ReadRedirects(Dir, Entries)))
	{
		return false;
	}

	int32 NumChecked = 0;
	for (const FRedirectEntry& Entry : Entries)
	{
		if (Entry.Kind == TEXT("Function"))
		{
			if (Entry.NewName.StartsWith(TEXT("/")) && !TargetsThisPlugin(Entry.NewName))
			{
				continue;
			}
			++NumChecked;
			TestNotNull(*FString::Printf(TEXT("'%s' redirects to '%s', which has to be a function that exists"),
				*Entry.OldName, *Entry.NewName), FindRedirectedFunction(Entry.NewName));
			continue;
		}
		if (Entry.Kind == TEXT("Package") || !TargetsThisPlugin(Entry.NewName))
		{
			continue;
		}
		++NumChecked;
		TestNotNull(*FString::Printf(TEXT("'%s' redirects to '%s', which has to be a %s that exists"),
			*Entry.OldName, *Entry.NewName, *Entry.Kind.ToLower()), FindTypeOfKind(Entry.Kind, Entry.NewName));
	}
	// A parser that quietly stopped matching would otherwise pass this test by checking nothing.
	TestTrue(TEXT("and the file still has targets in this plugin to check"), NumChecked > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSplitOffTypesAnswerToTheirOldPathTest,
	"DreamGUI.Packaging.EveryTypeInASplitOffModuleAnswersToItsOldCorePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSplitOffTypesAnswerToTheirOldPathTest::RunTest(const FString& Parameters)
{
	// A runtime module split off the core takes with it types that assets, configs and .dui files have been
	// naming as /Script/DreamGUI.<Name> -- and every one of them needs a redirect from that path, or whatever
	// named it stops loading, "because its class does not exist", on the day of the split. The tests above
	// check the entries the file has; this one checks the entries it ought to have, by walking the types each
	// split-off module actually holds and asking the engine where the old path leads.
	//
	// A type born in one of those modules, which never lived in the core, has no old path to answer to; it is
	// listed here instead, by its full path.
	static const TSet<FString> BornOutsideTheCore;
	const FName Core(TEXT("/Script/DreamGUI"));
	int32 NumModules = 0;
	int32 NumChecked = 0;
	for (const FName PackageName : DreamUI::GetRuntimeScriptPackages())
	{
		if (PackageName == Core)
		{
			continue;
		}
		++NumModules;
		UPackage* Package = FindObject<UPackage>(nullptr, *PackageName.ToString());
		if (!TestNotNull(*FString::Printf(TEXT("%s is loaded"), *PackageName.ToString()), Package))
		{
			continue;
		}
		// The package's own objects only: a delegate declared inside a class moves with its class and has no
		// entry of its own, and a default object is not a type.
		ForEachObjectWithPackage(Package, [this, &NumChecked](UObject* Object)
		{
			ECoreRedirectFlags Flag = ECoreRedirectFlags::None;
			if (Object->HasAnyFlags(RF_ClassDefaultObject))
			{
				return true;
			}
			if (Object->IsA<UClass>())
			{
				Flag = ECoreRedirectFlags::Type_Class;
			}
			else if (Object->IsA<UScriptStruct>())
			{
				Flag = ECoreRedirectFlags::Type_Struct;
			}
			else if (Object->IsA<UEnum>())
			{
				Flag = ECoreRedirectFlags::Type_Enum;
			}
			else if (Object->IsA<UFunction>())
			{
				Flag = ECoreRedirectFlags::Type_Function;
			}
			if (Flag == ECoreRedirectFlags::None || BornOutsideTheCore.Contains(Object->GetPathName()))
			{
				return true;
			}
			++NumChecked;
			const FString OldPath = FString::Printf(TEXT("/Script/DreamGUI.%s"), *Object->GetName());
			TestEqual(*FString::Printf(TEXT("'%s' leads to where the type lives now"), *OldPath),
				FCoreRedirects::GetRedirectedName(Flag, FCoreRedirectObjectName(OldPath)).ToString(), Object->GetPathName());
			// A global delegate's signature is looked up as an object as well as a function: a delegate property
			// names it as the former, a Blueprint pin as the latter.
			if (Flag == ECoreRedirectFlags::Type_Function)
			{
				TestEqual(*FString::Printf(TEXT("'%s' leads there as an object too"), *OldPath),
					FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Object, FCoreRedirectObjectName(OldPath)).ToString(),
					Object->GetPathName());
			}
			return true;
		}, /*bIncludeNestedObjects*/ false);
	}
	AddInfo(FString::Printf(TEXT("%d split-off modules, %d types checked"), NumModules, NumChecked));
	if (NumModules > 0)
	{
		TestTrue(TEXT("the split-off modules hold types to check"), NumChecked > 0);
	}
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
	// from the packaged plugin. The redirects travel in DefaultDreamGUI.ini, Game.ini is layered into the
	// engine's Game branch, and the MIT notice has to travel with any copy.
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

	// The copy-me template the redirects used to live in is retired. The engine never read it -- a plugin
	// layers into a config branch only through a file named for the branch, and "DefaultEngine" names
	// none -- and a stale copy of it in a project conflicts with the file that is read.
	TestFalse(TEXT("the plugin ships no Config/DefaultEngine.ini any more"),
		IFileManager::Get().FileExists(*FPaths::Combine(Dir, TEXT("Config"), TEXT("DefaultEngine.ini"))));
	TestFalse(TEXT("and FilterPlugin.ini does not ask for one"), Rules.Contains(TEXT("/Config/DefaultEngine.ini")));
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
