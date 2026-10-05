// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "K2Node_CreateDreamWidget.generated.h"

class FKismetCompilerContext;
class UEdGraph;
class UEdGraphPin;

/**
 * "Create Dream Widget": UMG's Create Widget node for a DreamUI widget class.
 *
 * A node rather than a function for the reason UMG's is one: the class picked on it decides its pins.
 * UK2Node_ConstructObjectFromClass grows an input for every Expose on Spawn property of that class --
 * every `props` entry of a .dui file is one -- types the result as that class, and rebuilds both when
 * the class changes. What is left here is what makes it a DreamGUI widget: the class pin's base, the
 * Owning Player pin, and the expansion.
 *
 * The expansion is UDreamUIBPLibrary::BeginDeferredCreateDreamWidget, the assignments, then
 * FinishDeferredCreateDreamWidget -- Spawn Actor's shape rather than Create Widget's, whose assignments
 * follow a finished Create. A DreamUI widget first reads its own data inside Initialize (PreConstruct,
 * On Initialized, the first evaluation of its bindings), so the values have to be in place before it.
 */
UCLASS()
class DREAMGUIK2NODES_API UK2Node_CreateDreamWidget : public UK2Node_ConstructObjectFromClass
{
	GENERATED_BODY()

public:
	UK2Node_CreateDreamWidget(const FObjectInitializer& ObjectInitializer);

	//~ Begin UEdGraphNode Interface.
	virtual void AllocateDefaultPins() override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	//~ End UEdGraphNode Interface.

	//~ Begin UK2Node Interface
	virtual FText GetMenuCategory() const override;
	virtual FText GetKeywords() const override;
	//~ End UK2Node Interface

	UEdGraphPin* GetOwningPlayerPin() const;

protected:
	virtual FText GetBaseNodeTitle() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTitleFormat() const override;
	virtual UClass* GetClassPinBaseClass() const override;
	virtual bool IsSpawnVarPin(UEdGraphPin* Pin) const override;
};
