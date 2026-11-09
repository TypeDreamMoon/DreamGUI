// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Import/DreamPsdMapper.h"
#include "Import/DreamPsdDuiMerge.h"

class UDreamWidgetBlueprint;
class UTexture2D;

struct DREAMGUIPSD_API FDreamPsdImportRequest
{
	/** The document. */
	FString SourceFile;
	/**
	 * The folder the Blueprint goes in: /Game/UI. Its textures go in a folder of the document's name below it
	 * (/Game/UI/Hud), and its .dui to the project's DUI directory at the same path (DUI/UI/WBP_Hud.dui).
	 */
	FString AssetFolder = TEXT("/Game/UI");
	/** The document's name in asset names; empty takes the file's. */
	FString Name;
	/** Where the .dui goes; empty takes the default above. */
	FString DuiFile;
	/** The Blueprint's package name; empty takes AssetFolder / BlueprintPrefix + Name. */
	FString BlueprintPackage;
	/** Make (or recompile) the widget Blueprint. Off: the .dui and the textures only. */
	bool bCreateBlueprint = true;
	/** Save the packages it writes. */
	bool bSave = true;
	/** Leave the .dui and the manifest unwritten and the assets unmade: the report says what an import would do. */
	bool bDryRun = false;
	/** Options in place of the project's settings (tests); unset reads UDreamPsdImportSettings. */
	TOptional<FDreamPsdMapOptions> MapOptions;
};

struct DREAMGUIPSD_API FDreamPsdImportReport
{
	bool bSucceeded = false;
	/** This import merged into an existing .dui. */
	bool bReimport = false;
	FString DuiFile;
	FString BlueprintPath;
	FString DuiText;
	FDreamPsdMessages Messages;
	FDreamPsdMapResult Map;
	FDreamPsdMergeReport Merge;
	/** Textures made or updated, by object path. */
	TArray<FString> Textures;
	/** Textures the last import made that this one did not: left in place, for the author to delete. */
	TArray<FString> UnusedTextures;
	/** The Blueprint's compile, when it said something. */
	TArray<FString> CompileMessages;

	FString ToString() const;
};

/**
 * Imports a layered document: reads it, maps it, makes or updates its textures, writes its .dui -- fresh, or merged
 * into the one a previous import wrote (DreamPsdDuiMerge) -- with a manifest beside it, then makes the widget Blueprint
 * whose source the .dui is, or recompiles it.
 */
namespace DreamPsdImporter
{
	DREAMGUIPSD_API bool Import(const FDreamPsdImportRequest& InRequest, FDreamPsdImportReport& OutReport);

	/** Import again, from what the last import of the Blueprint's .dui wrote in its manifest. */
	DREAMGUIPSD_API bool Reimport(UDreamWidgetBlueprint* InBlueprint, FDreamPsdImportReport& OutReport);

	/** Whether a Blueprint's .dui has a manifest beside it -- it was imported, and can be reimported. */
	DREAMGUIPSD_API bool CanReimport(const UDreamWidgetBlueprint* InBlueprint);

	/**
	 * Make or update the textures of a mapping in their packages, saving them when bInSave: what Import does with them,
	 * and what a test that renders an import without writing anything to disk calls with bInSave off. Each is compiled
	 * before this returns.
	 */
	DREAMGUIPSD_API TArray<UTexture2D*> MakeTextures(const TArray<FDreamPsdTextureRequest>& InRequests, bool bInSave, FDreamPsdMessages& OutMessages);

	/** The .dui file an import of InName into InAssetFolder writes by default. */
	DREAMGUIPSD_API FString DefaultDuiFile(const FString& InBlueprintPackage);
}
