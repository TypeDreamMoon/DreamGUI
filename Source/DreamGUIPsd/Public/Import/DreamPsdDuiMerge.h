// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Import/DreamPsdDuiModel.h"

/**
 * What the last import wrote, kept beside the .dui (`<file>.psdimport.json`): which nodes it made, from which layers,
 * with which values. A reimport reads it to tell a line the importer wrote from one an author changed or added.
 */
struct DREAMGUIPSD_API FDreamPsdImportManifest
{
	struct FNode
	{
		int32 LayerId = -1;
		FString Type;
		/** Name -> value text, the node's own lines. */
		TMap<FString, FString> Properties;
		/** Class -> (name -> value text). */
		TMap<FString, TMap<FString, FString>> Components;
	};

	/** The document, as the import was given it. */
	FString SourceFile;
	/** The asset folder its textures went to. */
	FString TextureFolder;
	/** The Blueprint made from the .dui. */
	FString BlueprintPath;
	/** Node id -> what the import wrote on it. */
	TMap<FString, FNode> Nodes;
	/** Every texture it made, by object path. */
	TArray<FString> Textures;

	static FDreamPsdImportManifest FromTree(const FDreamPsdDuiNode& InRoot);
	FString ToJson() const;
	static bool FromJson(const FString& InJson, FDreamPsdImportManifest& OutManifest);
	/** `<file>.psdimport.json`. */
	static FString PathFor(const FString& InDuiFile);
};

struct DREAMGUIPSD_API FDreamPsdMergeReport
{
	/** What a reimport changed that an author may not have wanted changed, one line each. */
	TArray<FString> Conflicts;
	/** What it did, one line each. */
	TArray<FString> Changes;
	/** Why it could not merge at all. */
	TArray<FString> Errors;

	bool Succeeded() const { return Errors.Num() == 0; }
};

/**
 * Brings an existing .dui up to a new import, editing its text rather than replacing it -- through the editor's text
 * patcher, so the file keeps its formatting, comments and every line the importer did not write.
 *
 * - A node whose id ends in `_psd<n>` is layer n's. A layer that is gone takes its node with it; whatever an author
 *   nested in that node is first moved up to its parent (and listed). A layer renamed renames its node with
 *   `(was: old id)`, so graph references and bindings follow. A layer moved or restacked moves its node.
 * - A new layer's node is inserted where the document puts it, among the generated nodes; nodes an author added keep
 *   their places.
 * - On a generated node, the lines the importer owns (DreamPsdDui::IsOwnedProperty) are rewritten to the new values,
 *   and the ones it wrote last time and does not write now are removed. Every other line is the author's and is kept.
 *   An owned line the author changed since the last import is overwritten and listed; one they turned into a binding
 *   is kept and listed.
 * - A generated node the author deleted comes back, and is listed: leave a layer out with `!skip` in its name.
 */
namespace DreamPsdDuiMerge
{
	DREAMGUIPSD_API bool Merge(const FString& InOldText, const FDreamPsdImportManifest* InLastImport, const FDreamPsdDuiNode& InNewRoot,
		FString& OutText, FDreamPsdMergeReport& OutReport);
}
