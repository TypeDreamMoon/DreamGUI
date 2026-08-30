// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/DreamUIBehaviour.h"
#include "Engine/Texture2D.h"
#include "DreamWidgetBehaviourTestTypes.generated.h"

class UDreamWidget;

/**
 * A struct with no short form, so the reflective sweep has to recurse it to leaves. One field of
 * every interesting kind: a scalar, a struct WITH a short form (recursion must stop there and print
 * it whole), and one tagged DuiHidden (the sweep must not see it).
 */
USTRUCT()
struct FDreamUISweepTestStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	float Thickness = 1.0f;

	UPROPERTY(EditAnywhere)
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere)
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(EditAnywhere, meta = (DuiHidden))
	float DerivedCache = 0.0f;
};

/**
 * The reflective sweep's subject: properties nobody listed anywhere, which is the point. Under the
 * old hand-kept tables a `+` component block compared only what the file already wrote, so the
 * FIRST edit of any of these died silently.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamUISweepTestBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	FDreamUISweepTestStyle Style;

	/** Soft on purpose: stored as a path, never loaded by the compile. */
	UPROPERTY(EditAnywhere)
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditAnywhere)
	float Plain = 0.0f;
};

/** Stand-in for a behaviour a designer drops on a widget, so behaviour-typed binds have a target. */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamUIAutoBindTargetBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()
};

/**
 * Native companion covering one property per AutoBindAndValidate branch. Property NAMES are the
 * fixture's contract: the pass keys candidates off each widget's sanitized display name, so a
 * property binds only when a widget of the same name exists.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamUIAutoBindTestBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	/** Savable widget reference with a same-named widget in the tree: the auto-bind happy path. */
	UPROPERTY(EditAnywhere)
		TObjectPtr<UDreamWidget> PlayButton;
	/** Two widgets share this display name, so the pass must report rather than pick one. */
	UPROPERTY(EditAnywhere)
		TObjectPtr<UDreamWidget> Ambiguous;
	/** No widget carries this name: unbound, and silent -- an unused variable is not a problem. */
	UPROPERTY(EditAnywhere)
		TObjectPtr<UDreamWidget> Absent;
	/** Only a sub-prefab widget carries this name, which the prefab writer cannot reference. */
	UPROPERTY(EditAnywhere)
		TObjectPtr<UDreamWidget> InsideNestedInstance;
	/** Behaviour-typed bind: resolves to a component on the same-named widget. */
	UPROPERTY(EditAnywhere)
		TObjectPtr<UDreamUIAutoBindTargetBehaviour> Scoreboard;
	/** Bindable name but an unbindable type: the pass must ignore it entirely. */
	UPROPERTY(EditAnywhere)
		TObjectPtr<UObject> Unrelated;
	/** Editable but transient: a runtime cache the prefab writer drops, so never auto-bind it. */
	UPROPERTY(EditAnywhere, Transient)
		TObjectPtr<UDreamWidget> RuntimeCache;
	/** EditDefaultsOnly carries CPF_DisableEditOnInstance, which the writer also drops. */
	UPROPERTY(EditDefaultsOnly)
		TObjectPtr<UDreamWidget> NotInstanceEditable;
};
