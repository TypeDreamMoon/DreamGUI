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
	 * Create "BP_<PrefabName>" (ULexUIBehaviour subclass) next to the prefab asset. Reuses an
	 * existing orphaned asset of that name instead of minting a numbered duplicate. The prefab
	 * editor assigns and attaches the resulting class transactionally. Returns null on failure.
	 */
	UBlueprint* CreateBehaviourBlueprint(ULexUIPrefab* InPrefab, ULexWidget* InRootWidget);

	/** Variable-name suggestion from a promote target: the widget's display name (or its owner widget's), sanitized to an identifier. */
	FString MakeVariableNameForTarget(UObject* InTarget);
	/**
	 * UMG "Is Variable" counterpart: add (or reuse, when type-compatible) an Instance-Editable
	 * member variable on the behaviour blueprint typed to InTarget's class, compile, then bind
	 * the behaviour instance's property to InTarget (a widget / visual / behaviour inside this
	 * prefab). The reference is serialized with the prefab (GUID-remapped), so it survives
	 * renames and needs no runtime lookup. Instance-Editable is required: LexUI's writer skips
	 * CPF_DisableEditOnInstance, so a default blueprint variable would come back null.
	 * @return true on success; OutMessage carries the failure reason or a rebound notice.
	 */
	bool PromoteToVariable(UBlueprint* InBlueprint, ULexWidget* InRootWidget, UObject* InTarget, const FString& InVariableName, FText& OutMessage);

	/** One FLexUIEventDelegate property found on a behaviour -- an event that can get a handler. */
	struct FDiscoveredEvent
	{
		ULexUIBehaviour* Component = nullptr;
		class FStructProperty* EventProperty = nullptr;
		FString DisplayName;//e.g. "OnClick"
	};
	/** Every FLexUIEventDelegate UPROPERTY across InWidget's behaviours (UIButton.OnClick, UIToggle.OnValueChanged, ...). */
	void DiscoverEvents(ULexWidget* InWidget, TArray<FDiscoveredEvent>& OutEvents);
	/**
	 * UMG "Event +" counterpart: generate a handler function on the behaviour blueprint whose
	 * signature matches the event's native parameter, compile, then wire InEvent's
	 * FLexUIEventDelegate to call it on the companion instance. When the parameter type can't
	 * be mapped to a pin the handler is parameterless (the binding still fires, without the value).
	 * @return the generated function name (NAME_None on failure); OutMessage carries the reason.
	 */
	FName AddEventHandler(UBlueprint* InBlueprint, ULexWidget* InRootWidget, const FDiscoveredEvent& InEvent, FText& OutMessage);

	/**
	 * Editor-time BindWidget: for every Instance-Editable, hard-object, blueprint-declared null
	 * property on the prefab's OWN companion behaviour (resolved by BP_<PrefabName>, so reusable
	 * behaviours a designer attached to the root are left alone), find the descendant widget whose
	 * (sanitized) display name equals the property name and bind it (widget property -> the widget,
	 * visual property -> its Visual, behaviour property -> a matching behaviour). Weak/soft/lazy
	 * refs are ignored (they don't serialize as hard references). Ambiguous names are reported not
	 * guessed; existing bindings are checked for dangling targets and for not-Instance-Editable
	 * properties (which LexUI's writer would drop).
	 * @param OutBoundDetails  "Variable -> Widget" per auto-bound property.
	 * @param OutProblems      Dangling / ambiguous / not-savable descriptions.
	 */
	void AutoBindAndValidate(ULexWidget* InRootWidget, ULexUIPrefab* InPrefab, TArray<FString>& OutBoundDetails,
		TArray<FString>& OutProblems, bool bPerformAutoBind = true);
}
