// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "DreamWidgetBlueprintTestTypes.generated.h"

class UDreamWidget;

/**
 * A native base a Blueprint can derive from, declaring one required binding and one plain reference.
 *
 * The pair is the point. Only the marked one is a claim the compiler is entitled to check; the
 * unmarked one is somebody's own member and must be left alone, which is exactly the distinction the
 * first version of the validation pass got wrong -- it inferred bindings from shape and flagged
 * UDreamWidget::Parent.
 */
UCLASS(NotBlueprintType, HideDropdown)
class UDreamWidgetBlueprintBindingBase : public UDreamUserWidget
{
	GENERATED_BODY()
public:
	/** Says it is a binding, so a hierarchy without a widget of this name is a compile error. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Test", meta = (BindDreamWidget))
	TObjectPtr<UDreamWidget> RequiredHeader = nullptr;

	/** Widget-typed and transient, but claims nothing. Must never be reported as a missing binding. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Test")
	TObjectPtr<UDreamWidget> UnmarkedReference = nullptr;

	/**
	 * Spelled UMG's way. This framework reads BindDreamWidget and must not read this one.
	 *
	 * Two spellings of a metadata key never fail to compile -- they fail by quietly never matching --
	 * so the only way to know which one is live is to declare both and see which raises the error.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Test", meta = (BindWidget))
	TObjectPtr<UDreamWidget> UMGSpelledBinding = nullptr;
};

/**
 * A text-backed base that declares one function a .dui can bind to.
 *
 * Native rather than a function added to the test's Blueprint graph, because what is being checked is
 * the HANDOVER -- that the bindings the builder produced reached UDreamWidgetBlueprint::PropertyBindings
 * in time for CompilePropertyBindings to resolve them. A binding whose function does not exist fails
 * that pass and would prove the handover only by the shape of its error message, which is the kind of
 * assertion that keeps passing after the thing it describes stops working.
 *
 * FText and no parameters because that is the shape a binding must have: FindDreamWidgetSetterFor
 * matches UDreamText::SetText, and the compiler requires the return type to be the property's exactly.
 */
UCLASS(NotBlueprintType, HideDropdown)
class UDreamTextUserWidgetBindingBase : public UDreamTextUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category = "Test")
	FText GetTitleText() const { return FText::FromString(TEXT("bound")); }
};

/**
 * A plain object with editable properties and no detail customization registered against it.
 *
 * FPropertyRowGenerator builds the real layout, customizations included, and DreamGUI's reach for a
 * details view a generator has none of. Anything testing property-handle SEMANTICS wants a class
 * nobody customizes, which is the whole job of this one.
 */
UCLASS(NotBlueprintType, HideDropdown)
class UDreamDetailsMultiSelectTestObject : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Test")
	bool bFlag = false;

	UPROPERTY(EditAnywhere, Category = "Test")
	float Amount = 0.0f;
};
