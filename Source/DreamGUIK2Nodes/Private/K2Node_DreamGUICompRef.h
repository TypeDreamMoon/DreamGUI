// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "K2Node.h"
#include "K2Node_DreamGUICompRef.generated.h"


class FBlueprintActionDatabaseRegistrar;
class UEdGraphPin;
struct FEdGraphPinType;

UCLASS()
class DREAMGUIK2NODES_API UK2Node_DreamGUICompRef_GetComponent : public UK2Node
{
	GENERATED_BODY()
public:
	// UEdGraphNode interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetKeywords() const override;
	virtual FText GetTooltipText() const override;
	//virtual TSharedPtr<SWidget> CreateNodeImage() const override;
	//virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin)override;
	virtual void NodeConnectionListChanged()override;
	virtual void ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const override;
	// End of UEdGraphNode interface

	// UK2Node interface
	virtual void ReconstructNode()override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void PostReconstructNode() override;
	virtual bool IsNodePure() const override { return true; }
	virtual bool ShouldDrawCompact() const override { return true; }
	//virtual int32 GetNodeRefreshPriority() const override { return EBaseNodeRefreshPriority::Low_UsesDependentWildcard; }

	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void NotifyPinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual FText GetCompactNodeTitle()const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FBlueprintNodeSignature GetSignature() const override;
	virtual FText GetMenuCategory() const override;
	virtual bool IsActionFilteredOut(class FBlueprintActionFilter const& Filter) override;
	virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const override;
	virtual bool ReferencesVariable(const FName& InVarName, const UStruct* InScope)const override;
	// End of UK2Node interface

private:
	/**
	 * The variable node feeding InInputPin, followed through any reroute nodes in between.
	 * @return the variable node, or nullptr when the pin is unconnected or fed by something else
	 */
	static class UK2Node_Variable* FindSourceVariableNode(const UEdGraphPin* InInputPin);
	/**
	 * The component class declared by the component reference that InInputPin is fed from.
	 * @return the class, or nullptr when it can not be resolved (which is what makes this node "!Get")
	 */
	static UClass* ResolveComponentClass(const UEdGraphPin* InInputPin);
	/** Retype the output pin from this node's own input pin. */
	void SetOutputPinType();
	/**
	 * Retype the output pin from an input pin that is not necessarily this node's current one:
	 * during reconstruction the fresh pins carry no links yet, only the old ones do.
	 */
	void SetOutputPinTypeFromInputPin(const UEdGraphPin* InInputPin);
	bool autoOutputTypeSuccess = false;
};
