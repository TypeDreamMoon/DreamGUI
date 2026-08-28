// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

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
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Test", meta = (BindWidget))
	TObjectPtr<UDreamWidget> RequiredHeader = nullptr;

	/** Widget-typed and transient, but claims nothing. Must never be reported as a missing binding. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Test")
	TObjectPtr<UDreamWidget> UnmarkedReference = nullptr;
};
