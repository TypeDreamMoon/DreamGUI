// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "DreamWidgetBlueprint.generated.h"

class UDreamWidgetTree;
class UDreamWidget;

/**
 * The authoring asset for a DreamUI hierarchy that is a class -- UMG's UWidgetBlueprint, for DreamUI.
 *
 * It holds the hierarchy a designer edits; compiling duplicates that onto the generated class as the
 * archetype instances are built from. The two copies are deliberate and match UMG: editing the
 * archetype in place would mutate every live instance's template mid-session.
 *
 * Editor-module only, like UWidgetBlueprint -- a cooked build has the generated class and needs
 * nothing else. The designer surface for it is out of scope here: a stock Blueprint editor opens
 * this asset and can already author graphs and variables against it.
 */
UCLASS(BlueprintType, DisplayName = "DreamUI Widget Blueprint")
class DREAMGUIEDITOR_API UDreamWidgetBlueprint : public UBlueprint
{
	GENERATED_BODY()

public:
	UDreamWidgetBlueprint();

	/** The hierarchy being authored. Never handed to instances directly; the compiler duplicates it. */
	UPROPERTY()
	TObjectPtr<UDreamWidgetTree> WidgetTree = nullptr;

	/** Create the tree (and its root) if this asset has none yet, so a fresh asset is editable. */
	UDreamWidgetTree* GetOrCreateWidgetTree();

	/** Every widget in the authored hierarchy, root first. Empty when nothing has been authored. */
	void GetAllSourceWidgets(TArray<UDreamWidget*>& OutWidgets) const;

	virtual UClass* GetBlueprintClass() const override;
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
	virtual bool AlwaysCompileOnLoad() const override { return true; }
#if WITH_EDITOR
	virtual void GetReparentingRules(TSet<const UClass*>& AllowedChildrenOfClasses, TSet<const UClass*>& DisallowedChildrenOfClasses) const override;
#endif
};
