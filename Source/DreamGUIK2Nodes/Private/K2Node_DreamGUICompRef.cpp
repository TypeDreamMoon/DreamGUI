// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "K2Node_DreamGUICompRef.h"
#include "EdGraphSchema_K2.h"
#include "KismetCompiler.h"
#include "Textures/SlateIcon.h"
#include "BlueprintNodeSignature.h"
#include "Engine/Blueprint.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "K2Node_Variable.h"
#include "K2Node_CallFunction.h"
#include "DreamUIBPLibrary.h"
#include "DreamUIComponentReference.h"

#define LOCTEXT_NAMESPACE "UK2Node_DreamGUICompRef_GetComponent"

void UK2Node_DreamGUICompRef_GetComponent::AllocateDefaultPins()
{
	//input pin
	UScriptStruct* compRefScriptStruct = FDreamUIComponentReference::StaticStruct();
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, compRefScriptStruct, TEXT("DreamGUI Component Reference"));
	//output pin
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, TEXT("Output"));
}
void UK2Node_DreamGUICompRef_GetComponent::ReconstructNode()
{
	//SetOutputPinType();
	Super::ReconstructNode();
}
void UK2Node_DreamGUICompRef_GetComponent::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	Super::ReallocatePinsDuringReconstruction(OldPins);
	// AllocateDefaultPins just made the output a wildcard again, and the links are still on the old
	// pins, so the type has to be recovered from the OLD input pin. Doing it here rather than only in
	// PostReconstructNode means the pin already carries its class while RewireOldPinsToNewPins runs,
	// so whatever is downstream is not briefly told it is connected to a wildcard.
	SetOutputPinTypeFromInputPin(OldPins.Num() >= 1 ? OldPins[0] : nullptr);
}
void UK2Node_DreamGUICompRef_GetComponent::PostReconstructNode()
{
	SetOutputPinType();
	Super::PostReconstructNode();
}
FText UK2Node_DreamGUICompRef_GetComponent::GetNodeTitle(ENodeTitleType::Type TitleType)const
{
	//if (TitleType == ENodeTitleType::FullTitle)
	//{
	//	return autoOutputTypeSuccess ? LOCTEXT("UK2Node_DreamGUI_GetComponentTitle", ".") : LOCTEXT("UK2Node_DreamGUI_GetComponentTitle", "!Get");
	//}
	return LOCTEXT("GetComponentTitle_Full", "Get Component for DreamGUIComponentReference");
}
FText UK2Node_DreamGUICompRef_GetComponent::GetCompactNodeTitle()const
{
	return autoOutputTypeSuccess ? LOCTEXT("UK2Node_DreamGUI_GetCompactNodeTitle", "\x2022") : LOCTEXT("UK2Node_DreamGUI_GetCompactNodeTitle", "!Get");
}
FText UK2Node_DreamGUICompRef_GetComponent::GetKeywords() const
{
	return FText(LOCTEXT("UK2Node_DreamGUI_GetKeywords", "ComponentRef"));
}
FText UK2Node_DreamGUICompRef_GetComponent::GetTooltipText()const
{
	return LOCTEXT("GetComponent_Tooltip", "Get component from DreamGUIComponentReference.\
\nIf the node is \"!Get\", that means auto cast failed, so you need to cast the result ActorComponent to your desired type.");
}
//TSharedPtr<SWidget> UK2Node_DreamGUICompRef_GetComponent::CreateNodeImage() const
//{
//	return SPinTypeSelector::ConstructPinTypeImage(Pins[0]);
//}
//FSlateIcon UK2Node_DreamGUICompRef_GetComponent::GetIconAndTint(FLinearColor& OutColor)const
//{
//	return Super::GetIconAndTint(OutColor);
//}
void UK2Node_DreamGUICompRef_GetComponent::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar)const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* RetRefNodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(RetRefNodeSpawner != nullptr);
		ActionRegistrar.AddBlueprintAction(ActionKey, RetRefNodeSpawner);
	}
}
FBlueprintNodeSignature UK2Node_DreamGUICompRef_GetComponent::GetSignature()const
{
	FBlueprintNodeSignature nodeSignature = Super::GetSignature();
	return nodeSignature;
}
FText UK2Node_DreamGUICompRef_GetComponent::GetMenuCategory()const
{
	return LOCTEXT("UK2Node_DreamGUI_GetMenuCategory", "DreamGUI");
}

UK2Node_Variable* UK2Node_DreamGUICompRef_GetComponent::FindSourceVariableNode(const UEdGraphPin* InInputPin)
{
	// Follow the wire, do not just look at the other end of it. A reroute node is drawn as a control
	// point on a wire and carries no value of its own, so a component reference routed through one -
	// which is how anybody tidies up a graph - still comes from the same variable.
	const UEdGraphPin* Pin = InInputPin;
	// A reroute chain is finite, but a corrupt graph must not hang the editor.
	for (int32 HopCount = 0; Pin != nullptr && HopCount < 32; ++HopCount)
	{
		const UEdGraphPin* NextPin = nullptr;
		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (LinkedPin == nullptr)continue;
			UEdGraphNode* LinkedNode = LinkedPin->GetOwningNodeUnchecked();
			if (LinkedNode == nullptr)continue;
			if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(LinkedNode))
			{
				return VariableNode;
			}
			int32 ControlPointInputPinIndex = INDEX_NONE;
			int32 ControlPointOutputPinIndex = INDEX_NONE;
			if (LinkedNode->ShouldDrawNodeAsControlPointOnly(ControlPointInputPinIndex, ControlPointOutputPinIndex)
				&& LinkedNode->Pins.IsValidIndex(ControlPointInputPinIndex))
			{
				NextPin = LinkedNode->Pins[ControlPointInputPinIndex];
				break;
			}
		}
		Pin = NextPin;
	}
	return nullptr;
}
UClass* UK2Node_DreamGUICompRef_GetComponent::ResolveComponentClass(const UEdGraphPin* InInputPin)
{
	UK2Node_Variable* VariableNode = FindSourceVariableNode(InInputPin);
	if (VariableNode == nullptr)
	{
		return nullptr;
	}
	UBlueprint* Blueprint = VariableNode->GetBlueprint();
	if (Blueprint == nullptr)
	{
		return nullptr;
	}
	UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (GeneratedClass == nullptr)
	{
		return nullptr;
	}
	// The component class is authored as a default value, so it is read off the class default object.
	UObject* ObjectInstance = GeneratedClass->GetDefaultObject();
	if (ObjectInstance == nullptr)
	{
		return nullptr;
	}
	const FStructProperty* StructProperty = FindFProperty<FStructProperty>(GeneratedClass, VariableNode->GetVarName());
	if (StructProperty == nullptr || StructProperty->Struct != FDreamUIComponentReference::StaticStruct())
	{
		return nullptr;
	}
	const FDreamUIComponentReference* StructPtr = StructProperty->ContainerPtrToValuePtr<FDreamUIComponentReference>(ObjectInstance);
	return StructPtr != nullptr ? StructPtr->GetComponentClass().Get() : nullptr;
}
void UK2Node_DreamGUICompRef_GetComponent::SetOutputPinType()
{
	SetOutputPinTypeFromInputPin(Pins.Num() >= 1 ? Pins[0] : nullptr);
}
void UK2Node_DreamGUICompRef_GetComponent::SetOutputPinTypeFromInputPin(const UEdGraphPin* InInputPin)
{
	if (Pins.Num() < 2)
	{
		return;
	}
	UEdGraphPin* outputPin = Pins[1];
	outputPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	if (UClass* ComponentClass = ResolveComponentClass(InInputPin))
	{
		outputPin->PinType.PinSubCategoryObject = ComponentClass;
		autoOutputTypeSuccess = true;
	}
	else
	{
		// Nothing to cast to: the caller has to cast the ActorComponent themselves, which is what the
		// "!Get" title says.
		outputPin->PinType.PinSubCategoryObject = UActorComponent::StaticClass();
		//outputPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
		//outputPin->PinType.PinSubCategoryObject = nullptr;
		autoOutputTypeSuccess = false;
	}
}
void UK2Node_DreamGUICompRef_GetComponent::ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog)const
{
	Super::ValidateNodeDuringCompilation(MessageLog);
	//@todo: give some hint when class change.
	//if (!autoOutputTypeSuccess)
	//{
	//	auto msg = FString(TEXT("Auto cast fail! You need to cast the result ActorComponent to your desired type."));
	//	MessageLog.Note(*msg);
	//}
}
void UK2Node_DreamGUICompRef_GetComponent::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	SetOutputPinType();
}
void UK2Node_DreamGUICompRef_GetComponent::NodeConnectionListChanged()
{
	Super::NodeConnectionListChanged();
	// Deleting the variable that feeds this node destroys the variable node, and
	// UEdGraphNode::BreakAllNodeLinks notifies the neighbouring NODE, never the individual pin - so
	// this is the only callback that runs. Without retyping here the output pin kept advertising the
	// class of a variable that no longer exists.
	SetOutputPinType();
}
void UK2Node_DreamGUICompRef_GetComponent::NotifyPinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::NotifyPinConnectionListChanged(Pin);
	SetOutputPinType();
}
void UK2Node_DreamGUICompRef_GetComponent::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UFunction* BlueprintFunction = UDreamUIBPLibrary::StaticClass()->FindFunctionByName("K2_DreamGUICompRef_GetComponent");
	if (!BlueprintFunction)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("InvalidFunctionName", "The function has not been found.").ToString(), this);
		return;
	}

	UK2Node_CallFunction* CallFunction = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);

	CallFunction->SetFromFunction(BlueprintFunction);
	CallFunction->AllocateDefaultPins();
	CompilerContext.MessageLog.NotifyIntermediateObjectCreation(CallFunction, this);

	CompilerContext.MovePinLinksToIntermediate(*Pins[0], *CallFunction->FindPinChecked(TEXT("InDreamGUICompRef")));
	auto FunctionResultPin = CallFunction->FindPinChecked(TEXT("OutResult"));
	FunctionResultPin->PinType.PinCategory = Pins[1]->PinType.PinCategory;
	FunctionResultPin->PinType.PinSubCategoryObject = Pins[1]->PinType.PinSubCategoryObject;
	CompilerContext.MovePinLinksToIntermediate(*Pins[1], *FunctionResultPin);

	//After we are done we break all links to this node (not the internally created one)
	BreakAllNodeLinks();
}
bool UK2Node_DreamGUICompRef_GetComponent::IsActionFilteredOut(FBlueprintActionFilter const& Filter)
{
	return false;
}
bool UK2Node_DreamGUICompRef_GetComponent::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason)const
{
	return false;
}
bool UK2Node_DreamGUICompRef_GetComponent::ReferencesVariable(const FName& InVarName, const UStruct* InScope)const
{
	// This is a question, not an instruction. The engine asks it while walking a graph's node list --
	// renaming a variable, deleting one, deciding whether a private variable is still used -- so
	// reconstructing the node from in here rebuilt this node's pins underneath that walk, and the
	// unconditional false told every one of those callers that this node uses no variable at all:
	// renames reported the variable unreferenced and "find references" never listed the node.
	//
	// What this node depends on is the variable feeding its input pin: SetOutputPinType reads that
	// variable's component reference to decide the output type, so a rename or a delete of it is
	// exactly what this node needs to be counted in. Ask the variable node itself, which knows its own
	// name and scope. Answering truthfully is also what puts this node in the list
	// FBlueprintEditorUtils::ChangeMemberVariableType reconstructs, which is how the output pin gets
	// retyped when the variable's type changes.
	if (const UK2Node_Variable* VariableNode = FindSourceVariableNode(Pins.Num() >= 1 ? Pins[0] : nullptr))
	{
		return VariableNode->ReferencesVariable(InVarName, InScope);
	}
	return false;
}

#undef LOCTEXT_NAMESPACE