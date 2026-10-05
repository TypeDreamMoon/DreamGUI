// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "K2Node_CreateDreamWidget.h"

#include "Core/DreamUserWidget.h"
#include "DreamUIBPLibrary.h"
#include "EdGraphSchema_K2.h"
#include "GameFramework/PlayerController.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "KismetCompilerMisc.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(K2Node_CreateDreamWidget)

#define LOCTEXT_NAMESPACE "K2Node_CreateDreamWidget"

namespace DreamCreateWidgetNodeLocal
{
	/** Named as BeginDeferredCreateDreamWidget names its parameter, so the expansion can move links by name. */
	const FName OwningPlayerPinName(TEXT("OwningPlayer"));
}

UK2Node_CreateDreamWidget::UK2Node_CreateDreamWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeTooltip = LOCTEXT("NodeTooltip",
		"Creates a DreamUI widget, off screen until it is added somewhere (Add to Viewport, Add Child).\n"
		"The class's Expose on Spawn properties -- every prop of a .dui file -- are set before the widget initializes, "
		"so Pre Construct, On Initialized and its bindings already see them.");
}

void UK2Node_CreateDreamWidget::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	UEdGraphPin* OwningPlayerPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, APlayerController::StaticClass(),
		DreamCreateWidgetNodeLocal::OwningPlayerPinName);
	SetPinToolTip(*OwningPlayerPin, LOCTEXT("OwningPlayerPinDescription",
		"The local player the widget belongs to: whose screen it goes on, whose focus and input it takes. Empty means the first local player."));
}

FText UK2Node_CreateDreamWidget::GetBaseNodeTitle() const
{
	return LOCTEXT("BaseTitle", "Create Dream Widget");
}

FText UK2Node_CreateDreamWidget::GetDefaultNodeTitle() const
{
	return GetBaseNodeTitle();
}

FText UK2Node_CreateDreamWidget::GetNodeTitleFormat() const
{
	// The second line is the node's subtitle: next to UMG's Create Widget in the same graph, the class
	// name alone does not say which framework the widget belongs to.
	return LOCTEXT("TitleFormat", "Create {ClassName} Widget\nDreamGUI");
}

UClass* UK2Node_CreateDreamWidget::GetClassPinBaseClass() const
{
	return UDreamUserWidget::StaticClass();
}

FText UK2Node_CreateDreamWidget::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "DreamGUI|Create");
}

FText UK2Node_CreateDreamWidget::GetKeywords() const
{
	return LOCTEXT("Keywords", "Create New Widget DreamUI DreamGUI");
}

UEdGraphPin* UK2Node_CreateDreamWidget::GetOwningPlayerPin() const
{
	UEdGraphPin* Pin = FindPin(DreamCreateWidgetNodeLocal::OwningPlayerPinName);
	check(Pin == nullptr || Pin->Direction == EGPD_Input);
	return Pin;
}

bool UK2Node_CreateDreamWidget::IsSpawnVarPin(UEdGraphPin* Pin) const
{
	return Super::IsSpawnVarPin(Pin) && Pin->PinName != DreamCreateWidgetNodeLocal::OwningPlayerPinName;
}

void UK2Node_CreateDreamWidget::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	static const FName BeginFunctionName = GET_FUNCTION_NAME_CHECKED(UDreamUIBPLibrary, BeginDeferredCreateDreamWidget);
	static const FName FinishFunctionName = GET_FUNCTION_NAME_CHECKED(UDreamUIBPLibrary, FinishDeferredCreateDreamWidget);
	static const FName WorldContextParamName(TEXT("WorldContextObject"));
	static const FName WidgetTypeParamName(TEXT("WidgetType"));
	static const FName WidgetParamName(TEXT("Widget"));

	UEdGraphPin* NodeExec = GetExecPin();
	UEdGraphPin* NodeThen = GetThenPin();
	UEdGraphPin* NodeWorldContext = GetWorldContextPin();
	UEdGraphPin* NodeClass = GetClassPin();
	UEdGraphPin* NodeOwningPlayer = GetOwningPlayerPin();
	UEdGraphPin* NodeResult = GetResultPin();

	// Before any link moves: what the pins say now is what the assignments are generated against.
	UClass* ClassToSpawn = GetClassToSpawn();
	UClass* LiteralClass = NodeClass != nullptr ? Cast<UClass>(NodeClass->DefaultObject) : nullptr;
	if (NodeClass == nullptr || (NodeClass->LinkedTo.Num() == 0 && LiteralClass == nullptr))
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("MissingClass_Error", "@@ must have a class specified.").ToString(), this);
		// The only error this node should produce: unlinked, it is not also an "unexpected node".
		BreakAllNodeLinks();
		return;
	}

	// Begin: make the widget, owned by the player the node names.
	UK2Node_CallFunction* BeginNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	BeginNode->FunctionReference.SetExternalMember(BeginFunctionName, UDreamUIBPLibrary::StaticClass());
	BeginNode->AllocateDefaultPins();
	UEdGraphPin* BeginExec = BeginNode->GetExecPin();
	UEdGraphPin* BeginWorldContext = BeginNode->FindPinChecked(WorldContextParamName);
	UEdGraphPin* BeginClass = BeginNode->FindPinChecked(WidgetTypeParamName);
	UEdGraphPin* BeginOwningPlayer = BeginNode->FindPinChecked(DreamCreateWidgetNodeLocal::OwningPlayerPinName);
	UEdGraphPin* BeginResult = BeginNode->GetReturnValuePin();

	CompilerContext.MovePinLinksToIntermediate(*NodeExec, *BeginExec);
	if (NodeClass->LinkedTo.Num() > 0)
	{
		CompilerContext.MovePinLinksToIntermediate(*NodeClass, *BeginClass);
	}
	else
	{
		BeginClass->DefaultObject = LiteralClass;
	}
	if (NodeWorldContext != nullptr)
	{
		CompilerContext.MovePinLinksToIntermediate(*NodeWorldContext, *BeginWorldContext);
	}
	if (NodeOwningPlayer != nullptr)
	{
		CompilerContext.MovePinLinksToIntermediate(*NodeOwningPlayer, *BeginOwningPlayer);
	}

	// Finish: initialize and register it. Its result is the node's, typed as the class picked.
	UK2Node_CallFunction* FinishNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	FinishNode->FunctionReference.SetExternalMember(FinishFunctionName, UDreamUIBPLibrary::StaticClass());
	FinishNode->AllocateDefaultPins();
	UEdGraphPin* FinishExec = FinishNode->GetExecPin();
	UEdGraphPin* FinishThen = FinishNode->GetThenPin();
	UEdGraphPin* FinishWidget = FinishNode->FindPinChecked(WidgetParamName);
	UEdGraphPin* FinishResult = FinishNode->GetReturnValuePin();

	CompilerContext.MovePinLinksToIntermediate(*NodeThen, *FinishThen);
	// Begin's result as the class picked too, not only Finish's: a field-notify property -- every prop of
	// a .dui file -- is assigned through a setter whose target pin is the property's own class, and the
	// schema refuses to connect a plain user widget to that, silently, leaving the setter on nothing.
	BeginResult->PinType = NodeResult->PinType;
	BeginResult->MakeLinkTo(FinishWidget);
	FinishResult->PinType = NodeResult->PinType;
	CompilerContext.MovePinLinksToIntermediate(*NodeResult, *FinishResult);

	// The Expose on Spawn assignments, between the two: on the widget Begin made, before Finish
	// initializes it. A pin left at the class default produces none, unless the class pin is wired and
	// the class is only known when the graph runs.
	UEdGraphPin* LastThen = FKismetCompilerUtilities::GenerateAssignmentNodes(
		CompilerContext, SourceGraph, BeginNode, this, BeginResult, ClassToSpawn, BeginClass);
	LastThen->MakeLinkTo(FinishExec);

	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE
