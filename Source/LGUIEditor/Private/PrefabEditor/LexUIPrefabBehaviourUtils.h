// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ULexWidget;
class ULexUIBehaviour;
class ULexUIPrefab;
class UBlueprint;

/**
 * The prefab's "companion behaviour blueprint", UMG-WidgetBlueprint style, ported from the
 * LGUI3 fork's design onto LexUI's model: LexUI attaches ULexUIBehaviour scripts to a widget
 * (ULexWidget::AddComponent), so the companion is a Blueprint ULexUIBehaviour subclass on the
 * prefab ROOT widget that carries the prefab's logic (Awake/Start/Tick lifecycle).
 *
 * This is the foundation of the code-behind trinity (companion / Promote-to-Variable /
 * Event +). It is kept backend-neutral in spirit for a future AngelScript host.
 *
 * NOTE (same serialization landmine as LGUI3): LexUI's prefab writer skips
 * CPF_DisableEditOnInstance properties, and blueprint variables get that flag by default --
 * so promoted variables must be made Instance Editable, handled in the Promote step later.
 */
namespace LexUIPrefabBehaviourUtils
{
	/** BP asset name convention identifying a prefab's companion behaviour ("BP_<PrefabName>"). */
	FString GetCompanionBlueprintName(ULexUIPrefab* InPrefab);

	/** The companion behaviour instance on the root widget (blueprint ULexUIBehaviour named per convention), or null. */
	ULexUIBehaviour* FindBehaviourComponent(ULexWidget* InRootWidget, ULexUIPrefab* InPrefab);
	/** The blueprint asset behind FindBehaviourComponent, or null. */
	UBlueprint* FindBehaviourBlueprint(ULexWidget* InRootWidget, ULexUIPrefab* InPrefab);
	/**
	 * Create "BP_<PrefabName>" (ULexUIBehaviour subclass) next to the prefab asset and attach
	 * an instance to the root widget. Re-attaches an existing orphaned asset of that name
	 * instead of minting a numbered duplicate. Returns null on failure.
	 */
	UBlueprint* CreateBehaviourBlueprint(ULexUIPrefab* InPrefab, ULexWidget* InRootWidget);
}
