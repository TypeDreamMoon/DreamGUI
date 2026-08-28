// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/DreamUserWidget.h"
#include "DreamUserWidgetTestTypes.generated.h"

class UDreamWidget;

/**
 * Native stand-in for what the compiler will generate: a user widget class whose properties are named
 * after the widgets in its template.
 *
 * Property NAMES are the fixture's contract. Binding matches a widget's sanitized display name against
 * the class's object properties, so each property below exists to pin one branch of that match, and
 * renaming one silently changes what is being tested.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamUserWidgetBindFixture : public UDreamUserWidget
{
	GENERATED_BODY()
public:
	/** A widget of this display name exists in the template: the happy path. */
	UPROPERTY()
	TObjectPtr<UDreamWidget> Header = nullptr;

	/** Also present, one level deeper, so binding is shown to reach past the first level. */
	UPROPERTY()
	TObjectPtr<UDreamWidget> Caption = nullptr;

	/** No widget carries this name. Must stay null rather than pick something near it. */
	UPROPERTY()
	TObjectPtr<UDreamWidget> Absent = nullptr;

	/**
	 * A widget IS named Mismatched, but this property cannot hold one. Must stay null and complain,
	 * not cast blindly -- the compiler declares these, so a type mismatch means the two sides disagree
	 * about what a name means, which is worth a message rather than a silent skip.
	 */
	UPROPERTY()
	TObjectPtr<UDreamUserWidget> Mismatched = nullptr;
};

/** A subclass that adds no template of its own, to pin that it instances its parent's. */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamUserWidgetBindFixtureSubclass : public UDreamUserWidgetBindFixture
{
	GENERATED_BODY()
};
