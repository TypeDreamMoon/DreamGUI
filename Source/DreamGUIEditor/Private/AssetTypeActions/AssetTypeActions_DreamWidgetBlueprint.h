// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "Toolkits/IToolkitHost.h"

/**
 * Opening a UDreamWidgetBlueprint gets the designer, not the stock Blueprint window.
 *
 * Registered rather than inherited: without this the asset falls through to FAssetTypeActions_Blueprint
 * (the nearest registered base) and opens with a graph and no design surface -- which is what happened
 * for as long as the toolkit was still an FAssetEditorToolkit and could not host the graph itself.
 */
class FAssetTypeActions_DreamWidgetBlueprint : public FAssetTypeActions_Base
{
public:
	explicit FAssetTypeActions_DreamWidgetBlueprint(EAssetTypeCategories::Type InAssetCategory);

	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor(44, 89, 180); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return AssetCategory; }
	virtual bool CanFilter() override { return true; }
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
	/**
	 * The Content Browser's right-click menu for this type.
	 *
	 * There was none, so a DreamUI Widget Blueprint offered only the generic asset verbs -- and the
	 * two operations an author reaches for constantly, "make another one like this" and "make one
	 * that derives from this", both had to be done by hand.
	 */
	virtual void GetActions(const TArray<UObject*>& InObjects, struct FToolMenuSection& Section) override;

	/**
	 * Copy InSource's hierarchy into a new asset beside it: UMG's "new from template", named plainly.
	 *
	 * A template here is an ASSET whose widget tree is copied, which is what an author means by "start
	 * from this one". The other reading -- start from a CLASS -- is already what the new-asset
	 * parent-class picker does, and the two are different operations: a copy diverges from its source
	 * for ever, a subclass keeps following it.
	 *
	 * Returns the new Blueprint, or null with the reason logged.
	 */
	static class UDreamWidgetBlueprint* CreateFromTemplate(class UDreamWidgetBlueprint* InSource);

private:
	EAssetTypeCategories::Type AssetCategory;
};
