// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "DreamUIAssetFactory.generated.h"

/**
 * What every DreamGUI asset is offered as in the Content Browser's Add menu: which section of the DreamGUI submenu,
 * under what label, with what tooltip.
 *
 * The submenu used to be one flat list under the engine's Basic heading, sorted by the asset type names -- "DreamUI
 * FontData DistanceField", "DreamUI RichText Custom Style Data" -- so the widget Blueprint, the asset an author makes
 * most, came last, the three fonts were split by the rich-text entries, and the tooltips were the classes' comments,
 * written for programmers or missing. Every DreamGUI factory derives from this instead and says what its entry is.
 *
 * Only the menu entry changes. The asset type names (FAssetTypeActions::GetName) stay what a tile, a filter and the
 * docs call the type; an entry is named inside a submenu that already says DreamGUI.
 */
UCLASS(Abstract)
class DREAMGUIEDITOR_API UDreamUIAssetFactory : public UFactory
{
	GENERATED_BODY()
public:
	virtual FText GetDisplayName() const override;
	virtual FText GetToolTip() const override;
	virtual TArray<FAssetCategoryPath> GetAssetMenuPathsForCategory(FName InCategory) const override;

	/**
	 * The DreamGUI submenu's sections. The menu puts its own Basic section first and the rest after it by name, which
	 * is the order these are listed in.
	 */
	static FText FontsSection();
	static FText GraphicsSection();
	static FText RichTextSection();

protected:
	/**
	 * The section of the DreamGUI submenu the entry is filed under. Empty for the submenu's Basic section: the menu's
	 * own, which it puts first and names in the editor's language.
	 */
	FText MenuSection;
	/** The entry's label. Empty for the asset type's own name. */
	FText MenuLabel;
	/** The entry's tooltip. Empty for the class's own. */
	FText MenuToolTip;
};
