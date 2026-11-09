// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * What the importer generates, as .dui nodes: the tree a fresh import prints and a reimport brings an existing file
 * up to. Values are .dui text already (`(12, -40)`, `#FFFFFFCC`, `"Title"`), so printing is joining lines.
 *
 * A generated node's id ends in `_psd<layer id>`: that suffix is how a reimport finds the node a layer made, whatever
 * the layer is called now and wherever the node has moved. Everything else in a file -- nodes without the suffix,
 * lines the importer never writes, `+` blocks of other classes -- is the author's, and a reimport keeps it.
 */
struct DREAMGUIPSD_API FDreamPsdDuiProperty
{
	FString Name;
	FString Value;
	/** Written by the first import only; a reimport leaves the line as the author has it (the root's placement). */
	bool bSeedOnly = false;
};

struct DREAMGUIPSD_API FDreamPsdDuiComponent
{
	/** The class as a `+` line names it: `DreamLayerGroup`, `UIButton`. */
	FString ClassName;
	TArray<FDreamPsdDuiProperty> Properties;
};

struct DREAMGUIPSD_API FDreamPsdDuiNode
{
	/** `Widget`, `Image`, `RectBlock`, `Text`. */
	FString Type;
	FString Id;
	/** The layer it was made from; -1 for the root. */
	int32 LayerId = -1;
	FString LayerName;
	TArray<FDreamPsdDuiProperty> Properties;
	TArray<FDreamPsdDuiComponent> Components;
	TArray<FDreamPsdDuiNode> Children;

	/** Set a property, replacing a line of that name. */
	void Set(const FString& InName, const FString& InValue, bool bInSeedOnly = false);
	const FDreamPsdDuiProperty* Find(const FString& InName) const;
	FDreamPsdDuiComponent& AddComponent(const FString& InClassName);
	const FDreamPsdDuiComponent* FindComponent(const FString& InClassName) const;

	/** Every node of the tree, parents first, with its parent's id (empty for the root). */
	void ForEach(TFunctionRef<void(const FDreamPsdDuiNode& Node, const FString& ParentId)> InVisit, const FString& InParentId = FString()) const;
};

namespace DreamPsdDui
{
	/** Values as .dui writes them. Numbers drop trailing zeros; at most three decimals. */
	DREAMGUIPSD_API FString Number(double InValue);
	DREAMGUIPSD_API FString Vec2(double InX, double InY);
	DREAMGUIPSD_API FString Vec4(double InX, double InY, double InZ, double InW);
	/** FMargin's order: left, top, right, bottom. */
	DREAMGUIPSD_API FString Margin(double InLeft, double InTop, double InRight, double InBottom);
	/** `#RRGGBBAA`, sRGB. */
	DREAMGUIPSD_API FString Color(const FColor& InColor);
	DREAMGUIPSD_API FString Bool(bool bInValue);
	/** A quoted string, escaped. */
	DREAMGUIPSD_API FString String(const FString& InText);
	/** An object path, quoted: `"/Game/UI/T_X.T_X"`. */
	DREAMGUIPSD_API FString Asset(const FString& InObjectPath);

	/** The suffix of a generated id: `Title_psd12` is layer 12. */
	DREAMGUIPSD_API bool ParseGeneratedId(const FString& InId, int32& OutLayerId);
	DREAMGUIPSD_API FString MakeGeneratedId(const FString& InLayerName, int32 InLayerId);
	/** A name made fit for an asset: ASCII letters, digits and '_' only; InFallback when nothing is left. */
	DREAMGUIPSD_API FString AssetSafe(const FString& InName, const FString& InFallback);

	/**
	 * Whether the importer writes InName on nodes of InType: a reimport rewrites (or removes) exactly these, and
	 * leaves every other line of a generated node to its author.
	 */
	DREAMGUIPSD_API bool IsOwnedProperty(const FString& InType, const FString& InName);
	/** Whether a `+` block of this class is the importer's to add, rewrite and remove. */
	DREAMGUIPSD_API bool IsOwnedComponent(const FString& InClassName);
	/** The names the importer may write in such a block. */
	DREAMGUIPSD_API bool IsOwnedComponentProperty(const FString& InClassName, const FString& InName);

	/**
	 * The whole file a fresh import writes: a comment saying where it came from and what the suffix means, the `class`
	 * line, and the tree.
	 */
	DREAMGUIPSD_API FString Print(const FDreamPsdDuiNode& InRoot, const FString& InClassPath, const FString& InSourceName);

	/** One node and everything under it, at an indent of InDepth levels (four spaces each). */
	DREAMGUIPSD_API FString PrintNode(const FDreamPsdDuiNode& InNode, int32 InDepth);
}
