// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Text/DreamUIPaths.h"

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/*
 * Where a .dui lives.
 *
 * Real directories, because every interesting case in this rule is a question about the file system:
 * whether a root exists, which root holds the file, whether an absolute path is under one. A test
 * that stubbed that out would assert the string manipulation and none of the behaviour, and the
 * string manipulation is not the part that breaks.
 *
 * Each fixture removes every directory it made -- the root itself when it had to create it, and the
 * folders on the way to its file when the root was already there: an empty DUI/ folder left behind
 * changes the answer GetSourceRoots gives every later test in the process, a suite whose result
 * depends on the order it ran in is worse than a red one, and an empty PathsTest/ left in a project's
 * real DUI/ is litter in the author's own source tree.
 */

namespace DreamUIPathsTestLocal
{
	/** A .dui inside a source root, with the root and the folders leading to the file created if they were not there. */
	struct FScopedRootFile
	{
		explicit FScopedRootFile(const FString& InRootDirectory, const TCHAR* InRelativePath)
			: RootDirectory(InRootDirectory)
		{
			bCreatedRoot = !IFileManager::Get().DirectoryExists(*RootDirectory);
			IFileManager::Get().MakeDirectory(*RootDirectory, /*Tree=*/true);
			FilePath = FPaths::ConvertRelativePathToFull(RootDirectory / InRelativePath);
			FPaths::NormalizeFilename(FilePath);
			// The highest folder between the root and the file that is not there yet, so the destructor
			// takes away exactly what the write below adds: SaveStringToFile makes the folders on the way
			// by itself, and deleting only the file used to leave an empty PathsTest/ in a DUI/ that
			// already existed. A root this fixture made goes as a whole, so there is nothing to note.
			if (!bCreatedRoot)
			{
				FString RootForComparison = FPaths::ConvertRelativePathToFull(RootDirectory);
				FPaths::NormalizeDirectoryName(RootForComparison);
				for (FString Folder = FPaths::GetPath(FilePath);
					Folder.Len() > RootForComparison.Len() && !IFileManager::Get().DirectoryExists(*Folder);
					Folder = FPaths::GetPath(Folder))
				{
					CreatedFolder = Folder;
				}
			}
			bWritten = FFileHelper::SaveStringToFile(TEXT("Widget Root { }\n"), *FilePath);
		}

		~FScopedRootFile()
		{
			IFileManager::Get().Delete(*FilePath, /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true);
			if (bCreatedRoot)
			{
				IFileManager::Get().DeleteDirectory(*RootDirectory, /*RequireExists=*/false, /*Tree=*/true);
			}
			else if (!CreatedFolder.IsEmpty())
			{
				IFileManager::Get().DeleteDirectory(*CreatedFolder, /*RequireExists=*/false, /*Tree=*/true);
			}
		}

		FScopedRootFile(const FScopedRootFile&) = delete;
		FScopedRootFile& operator=(const FScopedRootFile&) = delete;

		FString RootDirectory;
		FString FilePath;
		/** Empty unless the root was already there and the write had folders to make under it. */
		FString CreatedFolder;
		bool bWritten = false;
		bool bCreatedRoot = false;
	};

	FString ProjectRootDirectory()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), DreamUIPaths::SourceDirectoryName));
	}

	/** This plugin's own DUI/ directory, whether or not it exists yet; empty when the plugin cannot find itself. */
	FString PluginRootDirectory()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
		if (!Plugin.IsValid() || !Plugin->IsEnabled())
		{
			return FString();
		}
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(Plugin->GetBaseDir(), DreamUIPaths::SourceDirectoryName));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPathsProjectRootTest,
	"DreamGUI.Text.PathsResolveAgainstTheProjectRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPathsProjectRootTest::RunTest(const FString&)
{
	using namespace DreamUIPathsTestLocal;

	TestEqual(TEXT("empty in, empty out"), DreamUIPaths::Resolve(FString()), FString());
	TestEqual(TEXT("and whitespace counts as empty"), DreamUIPaths::Resolve(TEXT("   ")), FString());

	// The roots are read once BEFORE the project's own folder is made, with a plugin root in place so
	// the answer is not empty (an empty one is never memoised). In a project that has no DUI/ yet that
	// is the moment this test is really about: GetSourceRoots keeps a short memo, and a folder made
	// right after the memo was taken still has to be found at once -- the editor's own Open Workspace
	// makes the folder and asks about it in the same call. Without this step the case arose only when
	// the test before happened to leave a fresh memo behind, which is how it was found.
	const FString PluginRoot = PluginRootDirectory();
	if (!TestFalse(TEXT("this plugin can find its own directory"), PluginRoot.IsEmpty()))
	{
		return false;
	}
	FScopedRootFile Primer(PluginRoot, TEXT("PathsTest/Primer.dui"));
	if (!TestTrue(TEXT("the primer wrote its file"), Primer.bWritten))
	{
		return false;
	}
	DreamUIPaths::GetSourceRoots();

	FScopedRootFile File(ProjectRootDirectory(), TEXT("PathsTest/Panel.dui"));
	if (!TestTrue(TEXT("the fixture wrote its file"), File.bWritten))
	{
		return false;
	}

	TestEqual(TEXT("a relative path resolves against the project's DUI directory"),
		DreamUIPaths::Resolve(TEXT("PathsTest/Panel.dui")), File.FilePath);
	TestEqual(TEXT("an absolute one is used as written"),
		DreamUIPaths::Resolve(File.FilePath), File.FilePath);

	// The half a file picker needs: it hands back the absolute path, and what gets stored has to be
	// the relative one or the asset only opens on this machine.
	TestEqual(TEXT("and comes back out as the relative spelling"),
		DreamUIPaths::MakePortablePath(File.FilePath), FString(TEXT("PathsTest/Panel.dui")));

	const FString Outside = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NotAnyRoot.dui")));
	TestEqual(TEXT("a file under no root keeps its absolute path rather than being discarded"),
		DreamUIPaths::MakePortablePath(Outside), [&Outside] {
			FString Path = Outside;
			FPaths::NormalizeFilename(Path);
			return Path;
		}());

	TArray<FString> Found;
	DreamUIPaths::FindSourceFiles(Found);
	TestTrue(TEXT("and discovery finds it"), Found.Contains(File.FilePath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIPathsPluginRootTest,
	"DreamGUI.Text.PathsResolveAgainstAPluginRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIPathsPluginRootTest::RunTest(const FString&)
{
	using namespace DreamUIPathsTestLocal;

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
	if (!TestTrue(TEXT("this plugin can find itself"), Plugin.IsValid() && Plugin->IsEnabled()))
	{
		return false;
	}
	const FString PluginRoot = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(Plugin->GetBaseDir(), DreamUIPaths::SourceDirectoryName));

	FScopedRootFile File(PluginRoot, TEXT("PathsTest/Shipped.dui"));
	if (!TestTrue(TEXT("the fixture wrote its file"), File.bWritten))
	{
		return false;
	}

	TestEqual(TEXT("the qualified spelling names the plugin's root"),
		DreamUIPaths::Resolve(TEXT("Plugin.DreamGUI:PathsTest/Shipped.dui")), File.FilePath);
	TestEqual(TEXT("and is what a path under that root is stored as"),
		DreamUIPaths::MakePortablePath(File.FilePath),
		FString(TEXT("Plugin.DreamGUI:PathsTest/Shipped.dui")));

	// A bare path still finds it, because the search walks every root. That convenience is exactly
	// what makes the qualified spelling necessary: two roots holding PathsTest/Shipped.dui would
	// resolve to whichever came first, silently.
	TestEqual(TEXT("a bare path finds it too, by search"),
		DreamUIPaths::Resolve(TEXT("PathsTest/Shipped.dui")), File.FilePath);

	// A token that names nothing must not resolve to a plugin's directory by accident, and must not
	// come back empty either -- the caller is about to print it.
	const FString Unresolvable = DreamUIPaths::Resolve(TEXT("Plugin.NoSuchPlugin:Whatever.dui"));
	TestFalse(TEXT("an unknown plugin token does not resolve into a real plugin"),
		Unresolvable.StartsWith(PluginRoot));
	TestTrue(TEXT("but still names a path a diagnostic can print"), Unresolvable.EndsWith(TEXT("Whatever.dui")));

	// A drive letter is a colon too. Reading "D:/Work/X.dui" as a root token named "D" would turn
	// every absolute path on Windows into an unresolvable one.
	const FString AbsoluteWithDrive = FPaths::ConvertRelativePathToFull(File.FilePath);
	TestEqual(TEXT("and a drive letter is not read as a root token"),
		DreamUIPaths::Resolve(AbsoluteWithDrive), File.FilePath);
	return true;
}

#endif
