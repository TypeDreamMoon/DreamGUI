// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Text/DreamUIPaths.h"

#include "DirectoryWatcherModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IDirectoryWatcher.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

/*
 * Create Source File, the designer's way out of a text class that names no .dui yet: where the new file
 * goes, and what is left behind.
 *
 * Driven through FDreamWidgetBlueprintEditor::CreateTextSourceFileFor, the function the menu entry
 * calls, with the save dialog answered in code. The dialog is the one step of the command that needs a
 * person; everything else -- the directory it opens in, the directory made for it, the starter, the path
 * the class stores, the compile -- is the command's own and runs here exactly as it does from the menu.
 *
 * The case that matters is a project with no DUI/ of its own while a plugin has one. The command used to
 * open in the first source root, and then the first root is the PLUGIN's, so a project's first .dui went
 * into a folder belonging to a plugin. So every test here puts a plugin root in place first: without one
 * there is only one place a file could land, and the old answer would pass too.
 *
 * Real directories, because the rule is about the file system. And the clean-up is held to one rule: a
 * directory that was not there when the test began is the test's and goes whole; in one that was, only
 * the file the test caused to be written is removed, never anything that was there before. A project
 * such as DevTest has a DUI/ of its own full of an author's sources, and nothing in it is this suite's to
 * delete. That same project cannot be put in the state "no DUI/ yet" without moving those sources out of
 * the way, so the test that needs that state says so and does not run there; the host project the suite
 * runs in has no DUI/, and that is where it is proved.
 */

namespace DreamUICreateSourceFileTestLocal
{
	/** Absolute, forward slashes, no trailing slash: one spelling for every path compared here. */
	FString NormalizedPath(const FString& InPath)
	{
		FString Path = FPaths::ConvertRelativePathToFull(InPath);
		FPaths::NormalizeDirectoryName(Path);
		return Path;
	}

	/** The same file or directory, however the two spellings got here. */
	bool SamePath(const FString& InA, const FString& InB)
	{
		return NormalizedPath(InA).Equals(NormalizedPath(InB), ESearchCase::IgnoreCase);
	}

	FString ProjectSourceDirectory()
	{
		return NormalizedPath(FPaths::Combine(FPaths::ProjectDir(), DreamUIPaths::SourceDirectoryName));
	}

	/** This plugin's own DUI/ directory, whether or not it exists; empty when the plugin cannot find itself. */
	FString PluginSourceDirectory()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
		if (!Plugin.IsValid() || !Plugin->IsEnabled())
		{
			return FString();
		}
		return NormalizedPath(FPaths::Combine(Plugin->GetBaseDir(), DreamUIPaths::SourceDirectoryName));
	}

	/** Every file and directory under InDirectory, recursively, normalised. Empty when it does not exist. */
	TSet<FString> ListEntries(const FString& InDirectory)
	{
		TSet<FString> Entries;
		if (!IFileManager::Get().DirectoryExists(*InDirectory))
		{
			return Entries;
		}
		TArray<FString> Found;
		IFileManager::Get().FindFilesRecursive(Found, *InDirectory, TEXT("*"), /*Files*/true, /*Directories*/true);
		for (const FString& Entry : Found)
		{
			Entries.Add(NormalizedPath(Entry));
		}
		return Entries;
	}

	/**
	 * Wait, briefly, until the source-root list names InDirectory.
	 *
	 * GetSourceRoots keeps its answer for half a second and asks again early only about the project's own
	 * root, so a plugin root a fixture has just made can be missing from it for that long. An author
	 * clicking through a dialog never meets the gap; a test that makes the folder and calls straight in
	 * does, and then the class would store an absolute path for a file that is under a root. Polled on the
	 * wall clock, which is what the memo runs on.
	 */
	bool WaitUntilListed(const FString& InDirectory)
	{
		const double Deadline = FPlatformTime::Seconds() + 2.0;
		for (;;)
		{
			for (const FDreamUISourceRoot& Root : DreamUIPaths::GetSourceRoots())
			{
				if (SamePath(Root.Directory, InDirectory))
				{
					return true;
				}
			}
			if (FPlatformTime::Seconds() >= Deadline)
			{
				return false;
			}
			FPlatformProcess::Sleep(0.05f);
		}
	}

	/**
	 * One source root for the length of a test, taken away again exactly as far as the test added to it.
	 *
	 * bInMake makes the directory when it is not there. Without it the directory is only observed, which
	 * is what "the project has no DUI/ yet" needs: there the command under test is what makes it.
	 */
	struct FScopedSourceRoot
	{
		FScopedSourceRoot(const FString& InDirectory, bool bInMake)
			: Directory(NormalizedPath(InDirectory))
		{
			bExisted = IFileManager::Get().DirectoryExists(*Directory);
			Before = ListEntries(Directory);
			if (!bExisted && bInMake)
			{
				IFileManager::Get().MakeDirectory(*Directory, /*Tree*/true);
			}
		}

		~FScopedSourceRoot()
		{
			if (!bExisted)
			{
				// Not there when the test began, so everything in it now came from the test.
				IFileManager::Get().DeleteDirectory(*Directory, /*RequireExists*/false, /*Tree*/true);
				return;
			}
			for (const FString& File : FilesToRemove)
			{
				// Only a file that was not there before: a root that already existed is somebody's source
				// tree, and nothing that was in it is ever this fixture's to delete.
				if (!Before.Contains(NormalizedPath(File)))
				{
					IFileManager::Get().Delete(*File, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
				}
			}
		}

		FScopedSourceRoot(const FScopedSourceRoot&) = delete;
		FScopedSourceRoot& operator=(const FScopedSourceRoot&) = delete;

		/** A file the test caused to be written; removed on the way out when the root was already there. */
		void RemoveOnExit(const FString& InFilePath)
		{
			if (!InFilePath.IsEmpty())
			{
				FilesToRemove.AddUnique(InFilePath);
			}
		}

		/** What is here now that was not when the fixture began, sorted. */
		TArray<FString> Added() const
		{
			TArray<FString> Result;
			for (const FString& Entry : ListEntries(Directory))
			{
				if (!Before.Contains(Entry))
				{
					Result.Add(Entry);
				}
			}
			Result.Sort();
			return Result;
		}

		/** Whether everything that was here when the fixture began still is. */
		bool KeptEverything() const
		{
			const TSet<FString> Now = ListEntries(Directory);
			for (const FString& Entry : Before)
			{
				if (!Now.Contains(Entry))
				{
					return false;
				}
			}
			return true;
		}

		FString Directory;
		bool bExisted = false;
		TSet<FString> Before;
		TArray<FString> FilesToRemove;
	};

	/**
	 * A text-capable widget Blueprint that names no .dui yet -- the state the command is offered in.
	 *
	 * The name carries a GUID for two reasons. CreateBlueprint asserts that the package holds no
	 * Blueprint of that name yet, and a Blueprint is Standalone, so it outlives the test: with a fixed
	 * name, a second run in the same editor session would take the editor down. And the command names the
	 * new file after the class, so the file cannot collide with anything already in a project's DUI/.
	 */
	struct FScopedSourcelessBlueprint
	{
		FScopedSourcelessBlueprint()
		{
			Name = FString::Printf(TEXT("BP_CreateSource_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), *Name));
			if (Package == nullptr)
			{
				return;
			}
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTextUserWidget::StaticClass(), Package, FName(*Name), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedSourcelessBlueprint()
		{
			// The class stops naming its file BEFORE the roots delete it -- declared after them in every
			// test, so destroyed first. The command asked the source watcher to watch the root the file is
			// in, and a removal the watcher sees while a loaded class still names the file is reported as a
			// warning a debounce later, in the middle of whichever test is running by then.
			if (Blueprint != nullptr && Blueprint->GeneratedClass != nullptr)
			{
				if (UDreamTextUserWidget* Defaults = Cast<UDreamTextUserWidget>(
					Blueprint->GeneratedClass->GetDefaultObject(/*bCreateIfNeeded*/false)))
				{
					Defaults->SourceFile.FilePath.Reset();
				}
			}
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		FScopedSourcelessBlueprint(const FScopedSourcelessBlueprint&) = delete;
		FScopedSourcelessBlueprint& operator=(const FScopedSourcelessBlueprint&) = delete;

		/** The file name the command suggests: the class's own, with the extension. */
		FString SuggestedFileName() const
		{
			return Name + DreamUIPaths::SourceExtension;
		}

		UDreamWidget* FindTemplate(const TCHAR* InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Blueprint != nullptr && IsValid(Blueprint->WidgetTree))
			{
				const FString Wanted(InDisplayName);
				Blueprint->WidgetTree->ForEachWidget([&Found, &Wanted](UDreamWidget* Widget)
				{
					if (Found == nullptr && Widget->GetDisplayName() == Wanted)
					{
						Found = Widget;
					}
				});
			}
			return Found;
		}

		FString Name;
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
	};

	/**
	 * The save dialog, answered in code: it records what it was offered, then saves the suggested name --
	 * into the directory it was offered, which is the author just clicking Save, or into SaveDirectory
	 * when one is given -- or cancels.
	 */
	struct FDialogScript
	{
		FString SaveDirectory;
		bool bCancel = false;

		int32 TimesAsked = 0;
		FString OfferedDirectory;
		FString OfferedName;
		/** Whether the offered directory was on disk while the dialog was open. */
		bool bOfferedDirectoryExisted = false;

		bool Answer(const FString& InDefaultDirectory, const FString& InSuggestedName, FString& OutChosenPath)
		{
			++TimesAsked;
			OfferedDirectory = InDefaultDirectory;
			OfferedName = InSuggestedName;
			bOfferedDirectoryExisted = IFileManager::Get().DirectoryExists(*InDefaultDirectory);
			if (bCancel)
			{
				return false;
			}
			OutChosenPath = FPaths::Combine(SaveDirectory.IsEmpty() ? InDefaultDirectory : SaveDirectory, InSuggestedName);
			return true;
		}
	};

	/** The command behind the menu entry, with InOutDialog standing in for the platform's dialog. */
	bool CreateSourceFile(UDreamWidgetBlueprint* InBlueprint, FDialogScript& InOutDialog, FString& OutFilePath, FText& OutError)
	{
		return FDreamWidgetBlueprintEditor::CreateTextSourceFileFor(InBlueprint,
			[&InOutDialog](const FString& InDefaultDirectory, const FString& InSuggestedName, FString& OutChosenPath)
			{
				return InOutDialog.Answer(InDefaultDirectory, InSuggestedName, OutChosenPath);
			},
			OutFilePath, OutError);
	}

	/**
	 * Declared first in a test, so it is destroyed last -- after the roots have taken their folders away --
	 * and ticks the directory watcher so it lets go of a folder that was deleted while it watched it.
	 *
	 * The command hands the root it wrote into to the source watcher, and a folder deleted under an open
	 * watch can keep its name held -- the file system may finish a delete only when the last handle on it
	 * closes -- until the watcher closes its handle, which it does when it next ticks and finds the watch
	 * broken. An engine frame ticks it. A synchronous test is inside one frame, and the next test to make
	 * the same folder -- the next of these, or the paths tests -- could find the name still held and fail
	 * for a reason that has nothing to do with it. Commandlets tick the watcher by hand in the same way;
	 * what it delivers is what the next frame would have delivered.
	 */
	struct FScopedWatcherRelease
	{
		FScopedWatcherRelease() = default;
		FScopedWatcherRelease(const FScopedWatcherRelease&) = delete;
		FScopedWatcherRelease& operator=(const FScopedWatcherRelease&) = delete;

		~FScopedWatcherRelease()
		{
			FDirectoryWatcherModule& Module =
				FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
			if (IDirectoryWatcher* Watcher = Module.Get())
			{
				// More than once: the first tick takes the removal the deletion produced and asks for the next
				// change, and only the answer to THAT, on a folder that is gone, makes the watcher close up.
				for (int32 Pass = 0; Pass < 3; ++Pass)
				{
					FPlatformProcess::Sleep(0.01f);
					Watcher->Tick(0.0f);
				}
			}
		}
	};

	FString JoinPaths(const TArray<FString>& InPaths)
	{
		return InPaths.Num() > 0 ? FString::Join(InPaths, TEXT(", ")) : FString(TEXT("(nothing)"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUICreateSourceWithoutProjectFolderTest,
	"DreamGUI.Text.CreateSourceFile.WithNoProjectFolderItMakesOneAndWritesThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The decision this exists for: a project with no DUI/ of its own gets one, and its first .dui is written
 * into it -- not into the DUI/ of whichever plugin happens to have one.
 */
bool FDreamUICreateSourceWithoutProjectFolderTest::RunTest(const FString&)
{
	using namespace DreamUICreateSourceFileTestLocal;

	const FString ProjectRoot = ProjectSourceDirectory();
	if (IFileManager::Get().DirectoryExists(*ProjectRoot))
	{
		// Said, not faked. The only way to put this project in the state under test is to move its own
		// sources out of the way, and a test that does that to an author's source tree costs more than
		// anything it could check. The host project has no DUI/, and runs this for real.
		AddInfo(FString::Printf(
			TEXT("This project already has '%s', so a project without one cannot be set up here; the case was not run."),
			*ProjectRoot));
		return true;
	}
	const FString PluginRoot = PluginSourceDirectory();
	if (!TestFalse(TEXT("this plugin can find its own directory"), PluginRoot.IsEmpty()))
	{
		return false;
	}

	FScopedWatcherRelease WatcherRelease;
	// The plugin root is what made the old answer wrong, so it has to exist for this to mean anything.
	FScopedSourceRoot Plugin(PluginRoot, /*bInMake*/true);
	// Observed only: making this directory is the behaviour under test.
	FScopedSourceRoot Project(ProjectRoot, /*bInMake*/false);
	FScopedSourcelessBlueprint Scoped;
	if (!TestTrue(TEXT("a plugin root is in place for the old answer to have picked"),
			IFileManager::Get().DirectoryExists(*PluginRoot))
		|| !TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)
		|| !TestTrue(TEXT("and is one the command is offered for"), DreamUITextAuthoring::CanAuthorFromText(Scoped.Blueprint)))
	{
		return false;
	}

	FDialogScript Dialog;
	FString FilePath;
	FText Error;
	const bool bCreated = CreateSourceFile(Scoped.Blueprint, Dialog, FilePath, Error);
	Project.RemoveOnExit(FilePath);
	Plugin.RemoveOnExit(FilePath);
	if (!Error.IsEmpty())
	{
		AddInfo(FString::Printf(TEXT("the command said: %s"), *Error.ToString()));
	}

	TestTrue(TEXT("the command wrote a file and pointed the class at it"), bCreated);
	TestEqual(TEXT("the dialog was asked once"), Dialog.TimesAsked, 1);
	TestTrue(TEXT("and opened in the project's own DUI directory, not the plugin's"),
		SamePath(Dialog.OfferedDirectory, ProjectRoot));
	TestTrue(TEXT("which existed by then, so the dialog had it to open in"), Dialog.bOfferedDirectoryExisted);
	TestEqual(TEXT("suggesting the class's own name"), Dialog.OfferedName, Scoped.SuggestedFileName());

	TestTrue(TEXT("the project's DUI directory is there now"), IFileManager::Get().DirectoryExists(*ProjectRoot));
	TestTrue(TEXT("the file is in it"), FPaths::IsUnderDirectory(FilePath, ProjectRoot));
	TestTrue(TEXT("and on disk"), FPaths::FileExists(FilePath));
	const TArray<FString> PluginAdded = Plugin.Added();
	if (!TestEqual(TEXT("nothing at all was added to the plugin's DUI directory"), PluginAdded.Num(), 0))
	{
		AddInfo(FString::Printf(TEXT("added to the plugin's: %s"), *JoinPaths(PluginAdded)));
	}

	// Stored as the project-relative spelling -- no plugin token -- which is what makes the class the
	// project's own on every machine.
	TestEqual(TEXT("the class names the file relative to the project's root"),
		DreamUITextAuthoring::GetAuthoredSourcePath(Scoped.Blueprint), Scoped.SuggestedFileName());
	TestNotNull(TEXT("and the compile that followed built the starter's hierarchy"), Scoped.FindTemplate(TEXT("Title")));

	// The folder is a source root from the moment it exists: listed first, as the project's own.
	const TArray<FDreamUISourceRoot> Roots = DreamUIPaths::GetSourceRoots();
	TestTrue(TEXT("the source roots list the new folder first, straight away"),
		Roots.Num() > 0 && SamePath(Roots[0].Directory, ProjectRoot) && Roots[0].RootToken.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUICreateSourceWithProjectFolderTest,
	"DreamGUI.Text.CreateSourceFile.WithAProjectFolderItStillWritesThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The control: a project that has DUI/ gets what it always got, its own folder, with a plugin root beside
 * it. The command adds its one file and touches nothing else that is there.
 */
bool FDreamUICreateSourceWithProjectFolderTest::RunTest(const FString&)
{
	using namespace DreamUICreateSourceFileTestLocal;

	const FString ProjectRoot = ProjectSourceDirectory();
	const FString PluginRoot = PluginSourceDirectory();
	if (!TestFalse(TEXT("this plugin can find its own directory"), PluginRoot.IsEmpty()))
	{
		return false;
	}

	FScopedWatcherRelease WatcherRelease;
	FScopedSourceRoot Plugin(PluginRoot, /*bInMake*/true);
	// Made when the project has none (the host project), used as it is when it has one (DevTest).
	FScopedSourceRoot Project(ProjectRoot, /*bInMake*/true);
	FScopedSourcelessBlueprint Scoped;
	if (!TestTrue(TEXT("the project has a DUI directory"), IFileManager::Get().DirectoryExists(*ProjectRoot))
		|| !TestTrue(TEXT("and a plugin root is in place beside it"), IFileManager::Get().DirectoryExists(*PluginRoot))
		|| !TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint))
	{
		return false;
	}

	FDialogScript Dialog;
	FString FilePath;
	FText Error;
	const bool bCreated = CreateSourceFile(Scoped.Blueprint, Dialog, FilePath, Error);
	Project.RemoveOnExit(FilePath);
	Plugin.RemoveOnExit(FilePath);
	if (!Error.IsEmpty())
	{
		AddInfo(FString::Printf(TEXT("the command said: %s"), *Error.ToString()));
	}

	TestTrue(TEXT("the command wrote a file and pointed the class at it"), bCreated);
	TestTrue(TEXT("the dialog opened in the project's own DUI directory"), SamePath(Dialog.OfferedDirectory, ProjectRoot));
	TestTrue(TEXT("the file is in it"), FPaths::IsUnderDirectory(FilePath, ProjectRoot) && FPaths::FileExists(FilePath));

	const TArray<FString> ProjectAdded = Project.Added();
	if (!TestTrue(TEXT("and it is the only thing the command added there"),
		ProjectAdded.Num() == 1 && SamePath(ProjectAdded[0], FilePath)))
	{
		AddInfo(FString::Printf(TEXT("added to the project's: %s"), *JoinPaths(ProjectAdded)));
	}
	TestTrue(TEXT("while everything that was already there still is"), Project.KeptEverything());
	const TArray<FString> PluginAdded = Plugin.Added();
	if (!TestEqual(TEXT("nothing at all was added to the plugin's DUI directory"), PluginAdded.Num(), 0))
	{
		AddInfo(FString::Printf(TEXT("added to the plugin's: %s"), *JoinPaths(PluginAdded)));
	}

	TestEqual(TEXT("the class names the file relative to the project's root"),
		DreamUITextAuthoring::GetAuthoredSourcePath(Scoped.Blueprint), Scoped.SuggestedFileName());
	TestNotNull(TEXT("and the compile that followed built the starter's hierarchy"), Scoped.FindTemplate(TEXT("Title")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUICreateSourceCancelledTest,
	"DreamGUI.Text.CreateSourceFile.ACancelledDialogLeavesTheProjectAsItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The folder made so the dialog has somewhere to open is the dialog's, not the author's: a cancel takes it
 * away again, and a project that had a DUI/ keeps it untouched. Without the first half, opening the menu
 * and changing one's mind would leave an empty source root behind -- listed first, watched, and written
 * into the workspace from then on.
 */
bool FDreamUICreateSourceCancelledTest::RunTest(const FString&)
{
	using namespace DreamUICreateSourceFileTestLocal;

	const FString ProjectRoot = ProjectSourceDirectory();
	const FString PluginRoot = PluginSourceDirectory();
	if (!TestFalse(TEXT("this plugin can find its own directory"), PluginRoot.IsEmpty()))
	{
		return false;
	}

	FScopedSourceRoot Plugin(PluginRoot, /*bInMake*/true);
	FScopedSourceRoot Project(ProjectRoot, /*bInMake*/false);
	FScopedSourcelessBlueprint Scoped;
	if (!TestTrue(TEXT("a plugin root is in place for the old answer to have picked"),
			IFileManager::Get().DirectoryExists(*PluginRoot))
		|| !TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint))
	{
		return false;
	}

	FDialogScript Dialog;
	Dialog.bCancel = true;
	FString FilePath;
	FText Error;
	const bool bCreated = CreateSourceFile(Scoped.Blueprint, Dialog, FilePath, Error);
	Project.RemoveOnExit(FilePath);
	Plugin.RemoveOnExit(FilePath);

	TestFalse(TEXT("a cancelled dialog writes nothing"), bCreated);
	TestTrue(TEXT("and is not reported as an error"), Error.IsEmpty());
	TestTrue(TEXT("no file is named"), FilePath.IsEmpty());
	TestEqual(TEXT("the dialog was asked once"), Dialog.TimesAsked, 1);
	TestTrue(TEXT("in the project's own DUI directory"), SamePath(Dialog.OfferedDirectory, ProjectRoot));
	TestTrue(TEXT("which was on disk while the dialog was open"), Dialog.bOfferedDirectoryExisted);

	if (Project.bExisted)
	{
		TestTrue(TEXT("the project's DUI directory is still there"), IFileManager::Get().DirectoryExists(*ProjectRoot));
		TestTrue(TEXT("with everything that was in it"), Project.KeptEverything());
		TestEqual(TEXT("and nothing added"), Project.Added().Num(), 0);
	}
	else
	{
		TestFalse(TEXT("the DUI directory made for the dialog is gone again"), IFileManager::Get().DirectoryExists(*ProjectRoot));
	}
	TestEqual(TEXT("nothing was added to the plugin's DUI directory"), Plugin.Added().Num(), 0);
	TestTrue(TEXT("and the class still names no file"), DreamUITextAuthoring::GetAuthoredSourcePath(Scoped.Blueprint).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUICreateSourceOtherRootTest,
	"DreamGUI.Text.CreateSourceFile.AnotherRootTheAuthorPicksIsHonoured",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The default is only where the dialog opens. An author who goes to a plugin's DUI/ and saves there gets
 * the file there, stored under the plugin's name, exactly as before -- and the folder made in the project
 * for the dialog, now empty, goes again, so the project ends as it would have without it.
 */
bool FDreamUICreateSourceOtherRootTest::RunTest(const FString&)
{
	using namespace DreamUICreateSourceFileTestLocal;

	const FString ProjectRoot = ProjectSourceDirectory();
	const FString PluginRoot = PluginSourceDirectory();
	if (!TestFalse(TEXT("this plugin can find its own directory"), PluginRoot.IsEmpty()))
	{
		return false;
	}

	FScopedWatcherRelease WatcherRelease;
	FScopedSourceRoot Plugin(PluginRoot, /*bInMake*/true);
	FScopedSourceRoot Project(ProjectRoot, /*bInMake*/false);
	FScopedSourcelessBlueprint Scoped;
	if (!TestTrue(TEXT("the plugin's root is among the source roots"), WaitUntilListed(PluginRoot))
		|| !TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint))
	{
		return false;
	}

	FDialogScript Dialog;
	Dialog.SaveDirectory = PluginRoot;
	FString FilePath;
	FText Error;
	const bool bCreated = CreateSourceFile(Scoped.Blueprint, Dialog, FilePath, Error);
	Project.RemoveOnExit(FilePath);
	Plugin.RemoveOnExit(FilePath);
	if (!Error.IsEmpty())
	{
		AddInfo(FString::Printf(TEXT("the command said: %s"), *Error.ToString()));
	}

	TestTrue(TEXT("the command wrote a file and pointed the class at it"), bCreated);
	TestTrue(TEXT("the dialog still opened in the project's own DUI directory"), SamePath(Dialog.OfferedDirectory, ProjectRoot));
	TestTrue(TEXT("but the file is where the author saved it"),
		FPaths::IsUnderDirectory(FilePath, PluginRoot) && FPaths::FileExists(FilePath));
	TestEqual(TEXT("and the class names it under the plugin's name"),
		DreamUITextAuthoring::GetAuthoredSourcePath(Scoped.Blueprint),
		FString(TEXT("Plugin.DreamGUI:")) + Scoped.SuggestedFileName());
	TestNotNull(TEXT("and the compile that followed built the starter from there"), Scoped.FindTemplate(TEXT("Title")));

	if (Project.bExisted)
	{
		TestTrue(TEXT("the project's DUI directory kept everything that was in it"), Project.KeptEverything());
		TestEqual(TEXT("and gained nothing"), Project.Added().Num(), 0);
	}
	else
	{
		TestFalse(TEXT("the DUI directory made in the project for the dialog is gone again"),
			IFileManager::Get().DirectoryExists(*ProjectRoot));
	}
	return true;
}

#endif
