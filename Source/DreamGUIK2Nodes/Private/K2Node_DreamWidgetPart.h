// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "BlueprintNodeSpawner.h"
#include "K2Node_DreamWidgetPart.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FCompilerResultsLog;
class FKismetCompilerContext;
class UDreamWidget;
class UEdGraphPin;
class UFunction;

/**
 * Reaching a widget's parts -- its visual, layout container, layout self and behaviours -- from a graph, typed.
 *
 * A widget is composed, so the graph meets it as a UDreamWidget and the interesting API lives on its parts. Reaching one
 * used to take Get Visual and a Cast, with a branch for a cast that could fail. The nodes here take the widget itself:
 * "Set Text" with PlayerLabel as its target, the part looked up inside the node when it compiles. The compiler already
 * knows what every authored widget is made of, because it builds the tree, so when the widget is an authored one the
 * compile checks that its part still has what the node uses. A .dui that turns the Text into a RectBlock fails the
 * compile at the node instead of doing nothing at run time.
 *
 * Fed by anything else (a function input, an array element) the nodes still work; a widget without the part is None.
 */
namespace DreamWidgetPartNodes
{
	/**
	 * The authored widget the value on InPin is, or nullptr when that can not be known.
	 *
	 * InPin is either an output that carries a widget -- a member variable's getter, through any reroutes -- or an input
	 * linked to one. The widget is looked up in the authoring hierarchy of the Blueprint that declares the variable (or the
	 * nearest ancestor that authors a tree), which is the hierarchy the current compile builds, never the class's copy of
	 * the last one.
	 */
	DREAMGUIK2NODES_API const UDreamWidget* ResolveAuthoredWidget(const UEdGraphPin* InPin, FName* OutVariableName = nullptr);

	/** "Dream Text (Visual)": a part class as the menu and the nodes name it. */
	DREAMGUIK2NODES_API FText DescribePart(const UClass* InPartClass);
}

/**
 * "Get <Part>": the part itself, typed as its class. For handing the part on -- to a function of the graph's own, a
 * variable, an array. To call or set something on a part, the menu places the nodes below, which need no read in front.
 */
UCLASS()
class DREAMGUIK2NODES_API UK2Node_DreamWidgetPart : public UK2Node
{
	GENERATED_BODY()

public:
	static const FName WidgetPinName;
	static const FName PartPinName;

	/** The class the output is typed as. The widget's part is that class or a subclass of it. */
	UPROPERTY()
	TSubclassOf<UObject> PartClass;

	UEdGraphPin* GetWidgetPin() const;
	UEdGraphPin* GetPartPin() const;

	// UEdGraphNode interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetKeywords() const override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	// End of UEdGraphNode interface

	// UK2Node interface
	virtual bool IsNodePure() const override { return true; }
	/** The checks, here and not in ValidateNodeDuringCompilation: that runs after ExpandNode has unwired the node. */
	virtual void EarlyValidation(FCompilerResultsLog& MessageLog) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual FBlueprintNodeSignature GetSignature() const override;
	virtual bool ReferencesVariable(const FName& InVarName, const UStruct* InScope) const override;
	// End of UK2Node interface
};

/**
 * A call to a part's function, its target the widget: "Set Text" off PlayerLabel, one node. It is an ordinary function
 * call in every other way -- pins, purity, latent, defaults -- and compiles to the read of the part and that call.
 */
UCLASS()
class DREAMGUIK2NODES_API UK2Node_DreamWidgetPartCall : public UK2Node_CallFunction
{
	GENERATED_BODY()

public:
	/** The part class the function needs: the function's own class, or the part base it was offered under. */
	UPROPERTY()
	TSubclassOf<UObject> PartClass;

	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
	// End of UEdGraphNode interface

	// UK2Node interface
	virtual void EarlyValidation(FCompilerResultsLog& MessageLog) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FBlueprintNodeSignature GetSignature() const override;
	// End of UK2Node interface

	// UK2Node_CallFunction interface
	/** The target takes the widget, not the part: that is the whole node. */
	virtual UEdGraphPin* CreateSelfPin(const UFunction* Function) override;
	// End of UK2Node_CallFunction interface
};

/**
 * "Get Spacing" / "Set Spacing" on a part, its target the widget. A property with a BlueprintGetter or BlueprintSetter
 * goes through it, as everywhere else in a graph.
 */
UCLASS()
class DREAMGUIK2NODES_API UK2Node_DreamWidgetPartProperty : public UK2Node
{
	GENERATED_BODY()

public:
	static const FName TargetPinName;

	UPROPERTY()
	TSubclassOf<UObject> PartClass;

	UPROPERTY()
	TFieldPath<FProperty> Property;

	/** Set rather than get: an exec node with the value as its input. */
	UPROPERTY()
	bool bWrite = false;

	UEdGraphPin* GetTargetPin() const;
	UEdGraphPin* GetValuePin() const;

	// UEdGraphNode interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	// End of UEdGraphNode interface

	// UK2Node interface
	virtual bool IsNodePure() const override { return !bWrite; }
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void EarlyValidation(FCompilerResultsLog& MessageLog) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual FText GetMenuCategory() const override;
	virtual FBlueprintNodeSignature GetSignature() const override;
	// End of UK2Node interface
};

/**
 * The menu actions behind the nodes above. One reads a part of a given class; the others call one function of a part
 * class, or get or set one of its properties. All of them show only while dragging off a widget pin whose authored widget
 * has a matching part, and they are filed under that widget and part, so a widget's parts read as one list.
 */
UCLASS(Transient)
class DREAMGUIK2NODES_API UDreamWidgetPartNodeSpawner : public UBlueprintNodeSpawner
{
	GENERATED_BODY()

public:
	/** For a read action, the native class the part's own class is nearest to; otherwise the class the member needs. */
	UPROPERTY()
	TObjectPtr<UClass> PartClass = nullptr;

	/** The function a call action places, or nullptr. */
	UPROPERTY()
	TObjectPtr<UFunction> Function = nullptr;

	/** The property a get or set action places, or null. */
	TFieldPath<FProperty> Property;
	bool bWrite = false;

	static UDreamWidgetPartNodeSpawner* CreateRead(UClass* InPartClass);
	static UDreamWidgetPartNodeSpawner* CreateCall(UClass* InPartClass, UFunction* InFunction);
	static UDreamWidgetPartNodeSpawner* CreateProperty(UClass* InPartClass, FProperty* InProperty, bool bInWrite);

	/** Whether this action reads the part itself, rather than calling, getting or setting something on it. */
	bool IsRead() const { return Function == nullptr && Property.Get() == nullptr; }

	/** The part of InWidget this action is about, or nullptr when this action does not apply to InWidget. */
	UObject* FindMatchingPart(const UDreamWidget* InWidget) const;

	// UBlueprintNodeSpawner interface
	virtual FBlueprintNodeSignature GetSpawnerSignature() const override;
	virtual FBlueprintActionUiSpec GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const override;
	virtual bool IsTemplateNodeFilteredOut(FBlueprintActionFilter const& Filter) const override;
	// End of UBlueprintNodeSpawner interface

private:
	/** The authored widget behind the one pin the menu was opened from, or nullptr. */
	static const UDreamWidget* ResolveContextWidget(const TArray<UEdGraphPin*>& InPins, FName* OutVariableName = nullptr);
};
