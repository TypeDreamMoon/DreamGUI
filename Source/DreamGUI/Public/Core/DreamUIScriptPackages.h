// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class ECoreRedirectFlags : uint32;

/**
 * The script packages of DreamGUI's runtime modules -- "/Script/DreamGUI", and whichever others the
 * plugin's runtime modules add -- in the order the modules registered them.
 *
 * Everything that asks "is this one of DreamGUI's types" or turns a short type name into a type goes
 * through this list instead of spelling "/Script/DreamGUI": the .dui lookups, the reference-docs
 * generator, and the tests that enumerate the plugin's own types. A type that moves into another of the
 * plugin's modules then keeps resolving by its short name and keeps being counted, with nothing to
 * update but that module's registration.
 *
 * Each runtime module registers its own package from StartupModule and unregisters it from
 * ShutdownModule. Game-thread only, like module startup.
 */
namespace DreamUI
{
	/** Adds InPackageName ("/Script/DreamGUI") to the list. Idempotent. */
	DREAMGUI_API void RegisterRuntimeScriptPackage(FName InPackageName);

	/** Removes it again. */
	DREAMGUI_API void UnregisterRuntimeScriptPackage(FName InPackageName);

	/** The registered packages, in registration order: the core module's first. */
	DREAMGUI_API TArray<FName> GetRuntimeScriptPackages();

	/** Whether InPackageName is one of them. */
	DREAMGUI_API bool IsRuntimeScriptPackage(FName InPackageName);

	/** Whether InObjectPath ("/Script/DreamGUI.DreamWidget", "/Script/DreamGUI.Class.Function") lies inside one of them. */
	DREAMGUI_API bool IsInRuntimeScriptPackage(FStringView InObjectPath);

	/**
	 * InObjectPath with the CoreRedirects for InType applied, the way the engine applies them to a
	 * reference it loads: "/Script/DreamGUI.OldName" becomes wherever that type lives now. Unchanged when
	 * no redirect matches.
	 *
	 * For a type path written as text -- in a .dui, or sent by a tool -- which the lookups that take a
	 * path (TryFindTypeSlowSafe above all) do not redirect by themselves: a type that moves into another
	 * module, or is renamed with a redirect, must keep answering to what an older file wrote.
	 */
	DREAMGUI_API FString ApplyTypeRedirects(ECoreRedirectFlags InType, const FString& InObjectPath);
}
