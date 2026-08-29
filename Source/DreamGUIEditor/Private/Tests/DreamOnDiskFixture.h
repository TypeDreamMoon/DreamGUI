// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"

class UPackage;

/**
 * A package that has actually been to disk and back.
 *
 * Every widget tree in this suite is built with ConstructWidget/CreateWidget, which attaches the
 * widget live: the Parent back-pointer is set as a side effect, object names are handed out by a
 * counter that keeps counting for the life of the process, and no serializer ever runs. A tree that
 * arrives from a .uasset has none of that. Four of the five defects found by opening the editor on
 * 2026-08-29 were the same shape -- true of a tree the tests build, false of a tree the editor
 * loads -- and the suite was structurally unable to see any of them:
 *
 *   - Parent is Transient, so a loaded tree has empty back-pointers until something rebuilds them.
 *     UDreamWidgetTree had no PostLoad, so GetParent() was null for every widget in every saved
 *     asset, and duplicate did nothing at all on a real asset. 340 tests stayed green.
 *   - FName collisions between two assets cannot happen in one process: the generated DreamWidget_N
 *     counter never repeats. Load two packages and both start at 0, which is what makes
 *     name-keyed cross-tree lookup dangerous in the editor and invisible here.
 *
 * So: write the package, take the in-memory copy out of the name, read the file back. The reloaded
 * asset is a different object graph that arrived through the serializer -- the only kind of tree the
 * user ever actually has.
 */
namespace DreamOnDiskFixture
{
	struct FScopedOnDiskPackage
	{
		/** "/Temp/DreamGUITests/<InAssetName>" -- a real mount point, so it has a real filename. */
		explicit FScopedOnDiskPackage(const TCHAR* InAssetName);
		/** Removes the file as well as the root reference; nothing is left behind on disk. */
		~FScopedOnDiskPackage();

		FScopedOnDiskPackage(const FScopedOnDiskPackage&) = delete;
		FScopedOnDiskPackage& operator=(const FScopedOnDiskPackage&) = delete;

		/** Write InAsset and everything it references out. OutError says why not, when it says no. */
		bool Save(UObject* InAsset, FString& OutError);

		/**
		 * Read the package back off disk and hand back the asset of InAssetName inside it.
		 *
		 * The in-memory package is renamed out of the way first, because LoadPackage hands back what
		 * is already loaded under that name -- without the rename this returns the very objects the
		 * test built, and every assertion about loading passes without a serializer having run.
		 */
		UObject* Reload(FString& OutError);

		FString PackageName;
		FString AssetName;
		FString FileName;
		UPackage* Package = nullptr;
	};
}

#endif
