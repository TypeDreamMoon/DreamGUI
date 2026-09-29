// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPackage;

/**
 * Whether a package comes back clean: loaded, every Blueprint in it compiled, and nothing wrong logged on the
 * way.
 *
 * What goes wrong when a type moves between modules shows up here and nowhere earlier. A class the loader
 * cannot find, a struct it cannot read, a Blueprint whose parent is gone: each is logged while the package
 * loads or its Blueprints compile, and is otherwise noticed only when somebody opens the asset.
 */
namespace DreamPackageLoadCheck
{
	struct FResult
	{
		UPackage* Package = nullptr;
		/**
		 * What makes the package unusable; empty when it came back clean. Every error logged meanwhile, from
		 * any thread and in any category; every warning outside the Blueprint compiler's own log (a missing
		 * class, an import that failed, a property that did not read); every error the compiler reports for
		 * the package's Blueprints.
		 */
		TArray<FString> Problems;
		/** The compiler's warnings for the package's own Blueprints. */
		TArray<FString> CompilerWarnings;
		/**
		 * Compiler warnings logged for other Blueprints the load compiled on the way. Worth reading and never
		 * a failure: whether a dependency is compiled at all depends on what the process has already loaded,
		 * and a Blueprint is judged when its own package is the one being checked.
		 */
		TArray<FString> OtherNotes;
	};

	/** Load InPackageName and compile every Blueprint in it. */
	FResult LoadAndCompile(const FString& InPackageName);
}
