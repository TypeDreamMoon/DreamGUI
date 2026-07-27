// Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once

#include "Core/LexUIBehaviour.h"
#include "LexUIPrefabBehaviourTestTypes.generated.h"

class ULexWidget;

/** Stand-in for a behaviour a designer drops on a widget, so behaviour-typed binds have a target. */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class ULexUIAutoBindTargetBehaviour : public ULexUIBehaviour
{
	GENERATED_BODY()
};

/**
 * Native companion covering one property per AutoBindAndValidate branch. Property NAMES are the
 * fixture's contract: the pass keys candidates off each widget's sanitized display name, so a
 * property binds only when a widget of the same name exists.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class ULexUIAutoBindTestBehaviour : public ULexUIBehaviour
{
	GENERATED_BODY()
public:
	/** Savable widget reference with a same-named widget in the tree: the auto-bind happy path. */
	UPROPERTY(EditAnywhere)
		TObjectPtr<ULexWidget> PlayButton;
	/** Two widgets share this display name, so the pass must report rather than pick one. */
	UPROPERTY(EditAnywhere)
		TObjectPtr<ULexWidget> Ambiguous;
	/** No widget carries this name: unbound, and silent -- an unused variable is not a problem. */
	UPROPERTY(EditAnywhere)
		TObjectPtr<ULexWidget> Absent;
	/** Only a sub-prefab widget carries this name, which the prefab writer cannot reference. */
	UPROPERTY(EditAnywhere)
		TObjectPtr<ULexWidget> InsideSubPrefab;
	/** Behaviour-typed bind: resolves to a component on the same-named widget. */
	UPROPERTY(EditAnywhere)
		TObjectPtr<ULexUIAutoBindTargetBehaviour> Scoreboard;
	/** Bindable name but an unbindable type: the pass must ignore it entirely. */
	UPROPERTY(EditAnywhere)
		TObjectPtr<UObject> Unrelated;
	/** Editable but transient: a runtime cache the prefab writer drops, so never auto-bind it. */
	UPROPERTY(EditAnywhere, Transient)
		TObjectPtr<ULexWidget> RuntimeCache;
	/** EditDefaultsOnly carries CPF_DisableEditOnInstance, which the writer also drops. */
	UPROPERTY(EditDefaultsOnly)
		TObjectPtr<ULexWidget> NotInstanceEditable;
};
