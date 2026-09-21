// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIPaths.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

namespace DreamUIPaths
{
	namespace Local
	{
		/** "Plugin." -- the prefix that turns a root token into a plugin name. */
		const TCHAR* const PluginTokenPrefix = TEXT("Plugin.");

		/** Absolute, forward slashes, one trailing slash. Directories are compared as strings here. */
		FString NormalizeDirectory(const FString& InPath)
		{
			FString Directory = FPaths::ConvertRelativePathToFull(InPath);
			FPaths::NormalizeDirectoryName(Directory);
			return Directory + TEXT("/");
		}

		FString NormalizeFile(const FString& InPath)
		{
			FString File = FPaths::ConvertRelativePathToFull(InPath);
			FPaths::NormalizeFilename(File);
			return File;
		}

		/** The root a token names, or nullptr. An empty token is the project's own. */
		const FDreamUISourceRoot* FindRootByToken(const TArray<FDreamUISourceRoot>& InRoots, const FString& InToken)
		{
			for (const FDreamUISourceRoot& Root : InRoots)
			{
				if (Root.RootToken.Equals(InToken, ESearchCase::IgnoreCase))
				{
					return &Root;
				}
			}
			return nullptr;
		}

		/** Splits "Plugin.X:Rest" into its two halves. False when there is no token to split off. */
		bool SplitRootToken(const FString& InPath, FString& OutToken, FString& OutRelative)
		{
			int32 ColonIndex = INDEX_NONE;
			if (!InPath.FindChar(TEXT(':'), ColonIndex))
			{
				return false;
			}
			// A Windows drive letter is a colon too, and "D:/Work" must not be read as a root token
			// named "D". Requiring the prefix is what separates them, and it is why the project's own
			// root has no token at all: there is no spelling of it that could collide.
			if (!InPath.StartsWith(PluginTokenPrefix, ESearchCase::IgnoreCase))
			{
				return false;
			}
			OutToken = InPath.Left(ColonIndex);
			OutRelative = InPath.RightChop(ColonIndex + 1);
			OutRelative.RemoveFromStart(TEXT("/"));
			return true;
		}
	}

	TArray<FDreamUISourceRoot> GetSourceRoots()
	{
		// A HALF-SECOND MEMO, which is the smallest thing that answers the complaint without
		// reopening the decision the header explains.
		//
		// The header refuses a cache because DreamFX's had to be invalidated by hand at every
		// mutation site and answered wrongly for a DUI folder created while the editor was running.
		// Both of those are properties of a cache that lives until somebody clears it. This one
		// expires on its own, so the folder created a moment ago is found a moment later and nobody
		// has to remember anything -- while a single write-back flush, which resolves one `use` per
		// import and enumerates every enabled plugin with a DirectoryExists each time, pays for the
		// walk once instead of once per line.
		//
		// Game thread only, like everything that asks IPluginManager anything.
		static TArray<FDreamUISourceRoot> Cached;
		static double CachedAt = 0.0;
		constexpr double MemoSeconds = 0.5;
		const double Now = FPlatformTime::Seconds();
		if (!Cached.IsEmpty() && Now - CachedAt < MemoSeconds)
		{
			return Cached;
		}

		TArray<FDreamUISourceRoot> Roots;

		const FString ProjectRoot = Local::NormalizeDirectory(
			FPaths::Combine(FPaths::ProjectDir(), SourceDirectoryName));
		if (IFileManager::Get().DirectoryExists(*ProjectRoot))
		{
			FDreamUISourceRoot& Root = Roots.AddDefaulted_GetRef();
			Root.Directory = ProjectRoot;
		}

		for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
		{
			const FString PluginRoot = Local::NormalizeDirectory(
				FPaths::Combine(Plugin->GetBaseDir(), SourceDirectoryName));
			if (!IFileManager::Get().DirectoryExists(*PluginRoot))
			{
				continue;
			}
			FDreamUISourceRoot& Root = Roots.AddDefaulted_GetRef();
			Root.Directory = PluginRoot;
			Root.RootToken = FString::Printf(TEXT("%s%s"), Local::PluginTokenPrefix, *Plugin->GetName());
		}

		// An EMPTY answer is not memoised, deliberately: "no DUI folder yet" is the state an author
		// leaves by creating one, and a project in that state asks this rarely enough that walking
		// every time costs nothing. Memoising it would put the half-second delay on exactly the
		// moment somebody is waiting to see their first file appear.
		if (!Roots.IsEmpty())
		{
			Cached = Roots;
			CachedAt = Now;
		}
		return Roots;
	}

	void FindSourceFiles(TArray<FString>& OutFiles)
	{
		for (const FDreamUISourceRoot& Root : GetSourceRoots())
		{
			TArray<FString> Found;
			IFileManager::Get().FindFilesRecursive(Found, *Root.Directory,
				*FString::Printf(TEXT("*%s"), SourceExtension), /*Files=*/true, /*Directories=*/false,
				/*bClearFileNames=*/false);
			for (const FString& File : Found)
			{
				OutFiles.AddUnique(Local::NormalizeFile(File));
			}
		}
		OutFiles.Sort();
	}

	FString Resolve(const FString& InPath, bool* OutRootTokenResolved)
	{
		// True for everything that is not the one case below, including a path with no token at all:
		// "did the spelling's own root answer" is trivially yes when it named none.
		if (OutRootTokenResolved != nullptr)
		{
			*OutRootTokenResolved = true;
		}
		// Trimmed first because this string is typed by hand as often as it is picked, and a trailing
		// space turns "the file is right there" into DUI6001 with a message that looks identical to a
		// path that is genuinely wrong.
		const FString Trimmed = InPath.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			// Empty is the ordinary state of every widget blueprint that is not text-backed, not a
			// mistake: the caller reads it as "this class has no .dui" and leaves the hierarchy alone.
			return FString();
		}

		FString Token;
		FString Relative;
		if (Local::SplitRootToken(Trimmed, Token, Relative))
		{
			const TArray<FDreamUISourceRoot> Roots = GetSourceRoots();
			if (const FDreamUISourceRoot* Root = Local::FindRootByToken(Roots, Token))
			{
				return Local::NormalizeFile(Root->Directory + Relative);
			}
			// The plugin is disabled, has no DUI directory, or is not installed. Where the plugin WOULD
			// have put the file is unknowable without the plugin, so the project's own root stands in
			// and the caller gets an absolute path that does not exist -- which is the contract every
			// caller here relies on (this string is opened as well as printed).
			//
			// Stated plainly because the comment that used to sit here claimed the token survived
			// into the message, and it does not: a reader whose `Plugin.DreamUIX:` was a typo is told
			// about a path under the PROJECT, with the plugin name nowhere in it. Keeping the token
			// would mean returning a string that is not a filename, and every caller would then have
			// to learn that a resolved path may not be openable. If this message ever needs to name
			// the plugin, the fix is an out-parameter saying which root answered -- not a path that
			// is sometimes a spelling -- which is what OutRootTokenResolved is, and it is now here.
			if (OutRootTokenResolved != nullptr)
			{
				*OutRootTokenResolved = false;
			}
			return Local::NormalizeFile(FPaths::Combine(FPaths::ProjectDir(), SourceDirectoryName, Relative));
		}

		if (!FPaths::IsRelative(Trimmed))
		{
			// Absolute and normalised, because this string is about to be both opened AND printed: it
			// is what a diagnostic names when the file will not read, and a message log line only
			// becomes something a reader can act on -- or click -- when the path in it is the one on
			// disk.
			return Local::NormalizeFile(Trimmed);
		}

		const FString ProjectCandidate =
			Local::NormalizeFile(FPaths::Combine(FPaths::ProjectDir(), SourceDirectoryName, Trimmed));
		for (const FDreamUISourceRoot& Root : GetSourceRoots())
		{
			const FString Candidate = Local::NormalizeFile(Root.Directory + Trimmed);
			if (FPaths::FileExists(Candidate))
			{
				return Candidate;
			}
		}
		return ProjectCandidate;
	}

	FString MakePortablePath(const FString& InAbsolutePath)
	{
		const FString Trimmed = InAbsolutePath.TrimStartAndEnd();
		if (Trimmed.IsEmpty() || FPaths::IsRelative(Trimmed))
		{
			// Already relative means already portable, whatever it resolves to. Rewriting it here
			// would turn a path the author typed into one they did not.
			return Trimmed;
		}

		const FString Full = Local::NormalizeFile(Trimmed);
		// Longest match wins: a plugin inside the project directory sits under the project's own root
		// as a string, and claiming it for the project would produce a path with "Plugins/..." in it
		// that breaks the moment the plugin is installed anywhere else.
		const FDreamUISourceRoot* Best = nullptr;
		int32 BestLength = -1;
		const TArray<FDreamUISourceRoot> Roots = GetSourceRoots();
		for (const FDreamUISourceRoot& Root : Roots)
		{
			if (Full.StartsWith(Root.Directory, ESearchCase::IgnoreCase) && Root.Directory.Len() > BestLength)
			{
				BestLength = Root.Directory.Len();
				Best = &Root;
			}
		}
		if (Best == nullptr)
		{
			return Full;
		}

		const FString Relative = Full.RightChop(Best->Directory.Len());
		return Best->RootToken.IsEmpty()
			? Relative
			: FString::Printf(TEXT("%s:%s"), *Best->RootToken, *Relative);
	}
}
