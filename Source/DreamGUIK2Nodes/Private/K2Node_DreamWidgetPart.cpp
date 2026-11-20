// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "K2Node_DreamWidgetPart.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintActionFilter.h"
#include "BlueprintNodeSignature.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamWidgetParts.h"
#include "Core/DreamWidgetTree.h"
#include "DreamUIBPLibrary.h"
#include "DreamWidgetBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "KismetCompiler.h"
#include "Kismet2/CompilerResultsLog.h"
#include "ObjectTools.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectHash.h"

#define LOCTEXT_NAMESPACE "K2Node_DreamWidgetPart"

const FName UK2Node_DreamWidgetPart::WidgetPinName(TEXT("Widget"));
const FName UK2Node_DreamWidgetPart::PartPinName(TEXT("Part"));
const FName UK2Node_DreamWidgetPartProperty::TargetPinName(TEXT("Target"));

namespace DreamWidgetPartLocal
{
	/** The four classes a part derives from. Everything the menu offers is under one of them. */
	TArray<UClass*, TInlineAllocator<4>> PartBases()
	{
		return { UDreamVisual::StaticClass(), UDreamLayoutContainer::StaticClass(), UDreamLayoutSelf::StaticClass(),
			UDreamUIBehaviour::StaticClass() };
	}

	/** The first native class at or above InClass: a Blueprint part is offered under the native class it derives from. */
	UClass* NearestNativeClass(UClass* InClass)
	{
		UClass* Class = InClass;
		while (Class != nullptr && !Class->HasAnyClassFlags(CLASS_Native))
		{
			Class = Class->GetSuperClass();
		}
		return Class;
	}

	/** The functions some property of InClass (or a base) names as its BlueprintGetter or BlueprintSetter. */
	TSet<FName> AccessorFunctionNames(const UClass* InClass)
	{
		TSet<FName> Names;
		for (TFieldIterator<FProperty> It(InClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			for (const TCHAR* Key : { TEXT("BlueprintGetter"), TEXT("BlueprintSetter") })
			{
				const FString& Name = It->GetMetaData(Key);
				if (!Name.IsEmpty())
				{
					Names.Add(FName(*Name));
				}
			}
		}
		return Names;
	}

	/**
	 * A function the menu may place on a part: one a graph can call on an object it holds. A property's getter or setter is
	 * left to the property's own Get and Set, which is how the graph shows it everywhere else.
	 */
	bool IsOfferedFunction(const UFunction* InFunction, const TSet<FName>& InAccessors)
	{
		return UEdGraphSchema_K2::CanUserKismetCallFunction(InFunction)
			&& !InFunction->HasAnyFunctionFlags(FUNC_Static | FUNC_Protected | FUNC_Private | FUNC_EditorOnly)
			&& !InFunction->HasMetaData(FBlueprintMetadata::MD_Protected)
			&& !InFunction->HasMetaData(TEXT("BlueprintGetter")) && !InFunction->HasMetaData(TEXT("BlueprintSetter"))
			&& !InAccessors.Contains(InFunction->GetFName());
	}

	/** A property the menu may get on a part: one a graph sees. */
	bool IsOfferedProperty(const FProperty* InProperty)
	{
		return InProperty->HasAnyPropertyFlags(CPF_BlueprintVisible)
			&& !InProperty->HasAnyPropertyFlags(CPF_Deprecated | CPF_EditorOnly)
			&& !InProperty->HasMetaData(FBlueprintMetadata::MD_Private) && !InProperty->HasMetaData(FBlueprintMetadata::MD_Protected);
	}

	/** And set: unless it is read-only to graphs. */
	bool IsWritableProperty(const FProperty* InProperty)
	{
		return !InProperty->HasAnyPropertyFlags(CPF_BlueprintReadOnly);
	}

	/**
	 * Every class whose members the menu offers, with the part class they are filed under: each native part class with
	 * itself (bOwn), and each class above a part base -- UDreamLayout, which container and self share, or
	 * UDreamWidgetSubObjectBehaviour -- once per base, so its members land under the part they are used on.
	 *
	 * Native classes only. A Blueprint part is read under the native class it derives from (the read narrows to the
	 * Blueprint class when placed), and the members it adds are on that typed output like any other object's.
	 */
	void ForEachPartClass(TFunctionRef<void(UClass* InPartClass, UClass* InDeclaring, bool bOwn)> InVisit)
	{
		TSet<UClass*> Visited;
		for (UClass* Base : PartBases())
		{
			TArray<UClass*> Classes;
			Classes.Add(Base);
			GetDerivedClasses(Base, Classes, /*bRecursive*/true);
			for (UClass* Class : Classes)
			{
				if (Class == nullptr || !Class->HasAnyClassFlags(CLASS_Native)
					|| Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists) || Visited.Contains(Class))
				{
					continue;
				}
				Visited.Add(Class);
				InVisit(Class, Class, /*bOwn*/true);
			}
			for (UClass* Above = Base->GetSuperClass(); Above != nullptr && Above != UObject::StaticClass(); Above = Above->GetSuperClass())
			{
				InVisit(Base, Above, /*bOwn*/false);
			}
		}
	}

	/** "Dream Text" for UDreamText: the class as the class picker names it. */
	FText DescribeClass(const UClass* InClass)
	{
		return InClass != nullptr ? InClass->GetDisplayNameText() : LOCTEXT("NoClass", "None");
	}

	UClass* Authoritative(const TSubclassOf<UObject>& InClass)
	{
		UClass* Class = InClass.Get();
		return Class != nullptr ? Class->GetAuthoritativeClass() : nullptr;
	}

	/** The variable getter producing the value that arrives at InPin, skipping reroutes; nullptr when there is none. */
	const UK2Node_VariableGet* FindProducingVariable(const UEdGraphPin* InPin)
	{
		const UEdGraphPin* Pin = InPin;
		// A reroute chain is finite, but a corrupt graph must not hang the editor.
		for (int32 HopCount = 0; Pin != nullptr && HopCount < 64; ++HopCount)
		{
			if (Pin->Direction == EGPD_Input)
			{
				Pin = Pin->LinkedTo.Num() > 0 ? Pin->LinkedTo[0] : nullptr;
				continue;
			}
			UEdGraphNode* Node = Pin->GetOwningNodeUnchecked();
			if (Node == nullptr)
			{
				return nullptr;
			}
			if (const UK2Node_VariableGet* Variable = Cast<UK2Node_VariableGet>(Node))
			{
				return Variable;
			}
			int32 InputIndex = INDEX_NONE;
			int32 OutputIndex = INDEX_NONE;
			if (Node->ShouldDrawNodeAsControlPointOnly(InputIndex, OutputIndex) && Node->Pins.IsValidIndex(InputIndex))
			{
				Pin = Node->Pins[InputIndex];
				continue;
			}
			return nullptr;
		}
		return nullptr;
	}

	/**
	 * The check every node here makes before it is expanded: that it names a part class, that a widget feeds it, and --
	 * when that widget is one of an authored tree -- that the widget's part is still an InPartClass. Run from EarlyValidation,
	 * on the Blueprint's own graph and after the compile rebuilt the tree from the .dui, so it sees the node wired and the
	 * tree this compile produces.
	 */
	void CheckPart(const UEdGraphNode* InNode, const UEdGraphPin* InWidgetPin, UClass* InPartClass, FCompilerResultsLog& MessageLog)
	{
		if (InPartClass == nullptr)
		{
			MessageLog.Error(*LOCTEXT("NoPartClass",
				"@@ names no part class (it was deleted, or the node was made by hand). Delete it and pick it again from the widget's pin.").ToString(),
				InNode);
			return;
		}
		if (InWidgetPin == nullptr || InWidgetPin->LinkedTo.Num() == 0)
		{
			MessageLog.Error(*LOCTEXT("NoWidget", "@@ acts on a part of no widget. Connect the widget it belongs to.").ToString(), InNode);
			return;
		}

		FName VariableName;
		const UDreamWidget* Authored = DreamWidgetPartNodes::ResolveAuthoredWidget(InWidgetPin, &VariableName);
		if (Authored == nullptr || FDreamWidgetParts::Find(Authored, InPartClass) != nullptr)
		{
			// Either the widget is not one of an authored tree's -- nothing to check, and None is the answer at run time -- or
			// it has the part.
			return;
		}

		// Say what the widget is now, so the fix is obvious from the message: the .dui changed the node, and either the graph
		// follows it or the .dui goes back.
		const EDreamWidgetPartKind Kind = FDreamWidgetParts::KindOf(InPartClass);
		const UObject* Instead = nullptr;
		switch (Kind)
		{
		case EDreamWidgetPartKind::Visual:          Instead = Authored->GetVisual(); break;
		case EDreamWidgetPartKind::LayoutContainer: Instead = Authored->GetLayoutContainer(); break;
		case EDreamWidgetPartKind::LayoutSelf:      Instead = Authored->GetLayoutSelf(); break;
		default: break;
		}
		const FText Widget = FText::FromName(VariableName);
		const FText KindText = FDreamWidgetParts::DescribeKind(Kind);
		const FText Message = Instead != nullptr
			? FText::Format(LOCTEXT("PartChanged",
				"@@ needs {0}'s {1} to be a {2}, but {0}'s {1} is a {3} now. Pick the node again from {0}'s pin, or change {0} back."),
				Widget, KindText, DescribeClass(InPartClass), DescribeClass(Instead->GetClass()))
			: FText::Format(LOCTEXT("PartGone",
				"@@ needs {0}'s {1} to be a {2}, but {0} has no such {1} now. Remove the node, or give {0} that part again."),
				Widget, KindText, DescribeClass(InPartClass));
		MessageLog.Error(*Message.ToString(), InNode);
	}

	/**
	 * The expansion every node here shares: the widget's links moved onto a read of its part, narrowed to InPartClass.
	 * @return the narrowed part, for the caller to wire into whatever acts on it; nullptr after reporting why not
	 */
	UEdGraphPin* SpawnPartRead(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node* InNode,
		UEdGraphPin& InWidgetPin, UClass* InPartClass)
	{
		UFunction* Function = UDreamUIBPLibrary::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UDreamUIBPLibrary, K2_GetWidgetPart));
		if (Function == nullptr || InPartClass == nullptr)
		{
			return nullptr;
		}

		UK2Node_CallFunction* Read = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(InNode, SourceGraph);
		Read->SetFromFunction(Function);
		Read->AllocateDefaultPins();
		CompilerContext.MessageLog.NotifyIntermediateObjectCreation(Read, InNode);
		// The parameter names of K2_GetWidgetPart, which are what its pins are called.
		CompilerContext.MovePinLinksToIntermediate(InWidgetPin, *Read->FindPinChecked(TEXT("Widget")));
		Read->FindPinChecked(TEXT("PartClass"))->DefaultObject = InPartClass;

		// The library answers with an object that is already an InPartClass or None, but the type system only knows UObject;
		// a pure cast narrows it without a branch, and can not fail on anything the function returns.
		UK2Node_DynamicCast* Narrow = CompilerContext.SpawnIntermediateNode<UK2Node_DynamicCast>(InNode, SourceGraph);
		Narrow->SetPurity(true);
		Narrow->TargetType = InPartClass;
		Narrow->AllocateDefaultPins();
		CompilerContext.MessageLog.NotifyIntermediateObjectCreation(Narrow, InNode);
		if (!CompilerContext.GetSchema()->TryCreateConnection(Read->GetReturnValuePin(), Narrow->GetCastSourcePin()))
		{
			CompilerContext.MessageLog.Error(*LOCTEXT("NarrowNotConnected", "@@ could not type the part it reads.").ToString(), InNode);
			return nullptr;
		}
		return Narrow->GetCastResultPin();
	}

	/** Wire FromPin -- the widget the menu was opened from -- to InTarget, and say whether that happened. */
	bool WireWidget(const UEdGraphSchema* InSchema, UEdGraphPin* FromPin, UEdGraphPin* InTarget)
	{
		return InSchema != nullptr && FromPin != nullptr && InTarget != nullptr && FromPin->Direction == EGPD_Output
			&& InSchema->TryCreateConnection(FromPin, InTarget);
	}

	/** "Target is the widget's Dream Text (Visual)": the second line of a call or property node's title. */
	FText TargetLine(const UClass* InPartClass)
	{
		return FText::Format(LOCTEXT("TargetLine", "Target is the widget's {0}"), DreamWidgetPartNodes::DescribePart(InPartClass));
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Shared
// ---------------------------------------------------------------------------------------------------------------------

const UDreamWidget* DreamWidgetPartNodes::ResolveAuthoredWidget(const UEdGraphPin* InPin, FName* OutVariableName)
{
	const UK2Node_VariableGet* Variable = DreamWidgetPartLocal::FindProducingVariable(InPin);
	if (Variable == nullptr || Variable->VariableReference.IsLocalScope())
	{
		return nullptr;
	}
	const FName VariableName = Variable->GetVarName();
	if (OutVariableName != nullptr)
	{
		*OutVariableName = VariableName;
	}

	// The class that holds the variable, then up: a subclass's graph reads the widgets its parent authored, and only the
	// class that authors a tree has them. The authoring hierarchy, not the generated class's copy -- a compile clears that
	// copy first and replaces it last, so in between, which is when these nodes are checked, it is missing or one compile old.
	for (UClass* Class = Variable->GetVariableSourceClass(); Class != nullptr; Class = Class->GetSuperClass())
	{
		const UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(UBlueprint::GetBlueprintFromClass(Class));
		if (Blueprint == nullptr)
		{
			if (Class->HasAnyClassFlags(CLASS_Native))
			{
				break;
			}
			continue;
		}
		TArray<UDreamWidget*> Widgets;
		Blueprint->GetAllSourceWidgets(Widgets);
		for (const UDreamWidget* Widget : Widgets)
		{
			if (Widget != nullptr && UDreamWidgetTree::MakeWidgetVariableName(Widget) == VariableName)
			{
				return Widget;
			}
		}
	}
	return nullptr;
}

FText DreamWidgetPartNodes::DescribePart(const UClass* InPartClass)
{
	return FText::Format(LOCTEXT("PartDescription", "{0} ({1})"), DreamWidgetPartLocal::DescribeClass(InPartClass),
		FDreamWidgetParts::DescribeKind(FDreamWidgetParts::KindOf(InPartClass)));
}

// ---------------------------------------------------------------------------------------------------------------------
// Get <Part>
// ---------------------------------------------------------------------------------------------------------------------

UEdGraphPin* UK2Node_DreamWidgetPart::GetWidgetPin() const
{
	return FindPin(WidgetPinName, EGPD_Input);
}

UEdGraphPin* UK2Node_DreamWidgetPart::GetPartPin() const
{
	return FindPin(PartPinName, EGPD_Output);
}

void UK2Node_DreamWidgetPart::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UDreamWidget::StaticClass(), WidgetPinName);

	UClass* OutputClass = DreamWidgetPartLocal::Authoritative(PartClass);
	UEdGraphPin* Output = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object,
		OutputClass != nullptr ? OutputClass : UObject::StaticClass(), PartPinName);
	Output->PinFriendlyName = DreamWidgetPartLocal::DescribeClass(OutputClass);

	Super::AllocateDefaultPins();
}

FText UK2Node_DreamWidgetPart::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	// Fixed by the part class alone, never by the wiring: a title is how a node is found in search results and in the
	// compiler log, and a title that changed with whatever fed the node would hide it under a name nobody searched for.
	return FText::Format(LOCTEXT("NodeTitle", "Get {0}"), DreamWidgetPartNodes::DescribePart(DreamWidgetPartLocal::Authoritative(PartClass)));
}

FText UK2Node_DreamWidgetPart::GetTooltipText() const
{
	return FText::Format(LOCTEXT("NodeTooltip",
		"The widget's {0}, already typed: no cast.\n"
		"When the widget is one of the tree's, the compile checks it still has this part, so a .dui that changes it fails here "
		"instead of returning None at run time. Any other widget without the part gives None."),
		DreamWidgetPartNodes::DescribePart(DreamWidgetPartLocal::Authoritative(PartClass)));
}

FText UK2Node_DreamWidgetPart::GetKeywords() const
{
	return LOCTEXT("NodeKeywords", "part visual layout behaviour component get typed");
}

void UK2Node_DreamWidgetPart::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (!DreamWidgetPartLocal::WireWidget(GetSchema(), FromPin, GetWidgetPin()))
	{
		Super::AutowireNewNode(FromPin);
	}

	// The read action is filed under a native class, which may be a base of the part the widget really has (a Blueprint
	// behaviour, say); the output follows the widget, so it carries everything the part offers.
	if (const UDreamWidget* Widget = DreamWidgetPartNodes::ResolveAuthoredWidget(GetWidgetPin()))
	{
		UObject* Part = FDreamWidgetParts::Find(Widget, DreamWidgetPartLocal::Authoritative(PartClass));
		UClass* Actual = Part != nullptr ? Part->GetClass()->GetAuthoritativeClass() : nullptr;
		if (Actual != nullptr && Actual != DreamWidgetPartLocal::Authoritative(PartClass))
		{
			PartClass = Actual;
			if (UEdGraphPin* Output = GetPartPin())
			{
				// Only while the node is being placed, when nothing is linked to the output yet, so no link can refuse it.
				Output->PinType.PinSubCategoryObject = Actual;
				Output->PinFriendlyName = DreamWidgetPartLocal::DescribeClass(Actual);
			}
		}
	}
}

void UK2Node_DreamWidgetPart::EarlyValidation(FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);
	DreamWidgetPartLocal::CheckPart(this, GetWidgetPin(), DreamWidgetPartLocal::Authoritative(PartClass), MessageLog);
}

void UK2Node_DreamWidgetPart::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UEdGraphPin* WidgetPin = GetWidgetPin();
	UEdGraphPin* PartPin = GetPartPin();
	UEdGraphPin* Part = WidgetPin != nullptr && PartPin != nullptr
		? DreamWidgetPartLocal::SpawnPartRead(CompilerContext, SourceGraph, this, *WidgetPin, DreamWidgetPartLocal::Authoritative(PartClass))
		: nullptr;
	if (Part != nullptr)
	{
		CompilerContext.MovePinLinksToIntermediate(*PartPin, *Part);
	}
	// Unwired either way: when the read could not be made, EarlyValidation has said why, and the node is not compiled as if
	// it had been expanded.
	BreakAllNodeLinks();
}

void UK2Node_DreamWidgetPart::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	// Each node class registers the actions that place it (the database insists on that); the three share one walk of the
	// part classes.
	UClass* ActionKey = GetClass();
	if (!ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		return;
	}
	DreamWidgetPartLocal::ForEachPartClass([&ActionRegistrar, ActionKey](UClass* InPartClass, UClass* /*InDeclaring*/, bool bOwn)
	{
		if (bOwn)
		{
			ActionRegistrar.AddBlueprintAction(ActionKey, UDreamWidgetPartNodeSpawner::CreateRead(InPartClass));
		}
	});
}

FText UK2Node_DreamWidgetPart::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "DreamGUI|Parts");
}

FBlueprintNodeSignature UK2Node_DreamWidgetPart::GetSignature() const
{
	FBlueprintNodeSignature Signature = Super::GetSignature();
	if (UClass* Class = DreamWidgetPartLocal::Authoritative(PartClass))
	{
		Signature.AddSubObject(Class);
	}
	return Signature;
}

bool UK2Node_DreamWidgetPart::ReferencesVariable(const FName& InVarName, const UStruct* InScope) const
{
	// The node depends on the variable feeding it: its check reads that widget. Answering truthfully keeps it in "find
	// references" and in the list a rename or a retype of the variable reconstructs.
	const UK2Node_VariableGet* Variable = DreamWidgetPartLocal::FindProducingVariable(GetWidgetPin());
	return Variable != nullptr && Variable->ReferencesVariable(InVarName, InScope);
}

// ---------------------------------------------------------------------------------------------------------------------
// A part's function, called on the widget
// ---------------------------------------------------------------------------------------------------------------------

UEdGraphPin* UK2Node_DreamWidgetPartCall::CreateSelfPin(const UFunction* Function)
{
	UEdGraphPin* Pin = Super::CreateSelfPin(Function);
	if (Pin != nullptr)
	{
		// Not PSC_Self and not the function's class: the target is any widget, and the part is found inside it.
		Pin->PinType.PinSubCategory = NAME_None;
		Pin->PinType.PinSubCategoryObject = UDreamWidget::StaticClass();
	}
	return Pin;
}

FText UK2Node_DreamWidgetPartCall::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::FullTitle)
	{
		return FText::Format(LOCTEXT("CallTitle", "{0}\n{1}"), Super::GetNodeTitle(ENodeTitleType::ListView),
			DreamWidgetPartLocal::TargetLine(DreamWidgetPartLocal::Authoritative(PartClass)));
	}
	return Super::GetNodeTitle(TitleType);
}

FText UK2Node_DreamWidgetPartCall::GetTooltipText() const
{
	return FText::Format(LOCTEXT("CallTooltip", "{0}\n\nTarget is a widget: the call lands on its {1}. When the widget is one of the tree's, the compile checks it still has that part."),
		Super::GetTooltipText(), DreamWidgetPartNodes::DescribePart(DreamWidgetPartLocal::Authoritative(PartClass)));
}

void UK2Node_DreamWidgetPartCall::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (!DreamWidgetPartLocal::WireWidget(GetSchema(), FromPin, FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input)))
	{
		Super::AutowireNewNode(FromPin);
	}
}

void UK2Node_DreamWidgetPartCall::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	// Not the function call's own checks: they run after ExpandNode, on a node that is unwired by then, and the ordinary call
	// it expanded into makes them all. Ours ran in EarlyValidation.
	UK2Node::ValidateNodeDuringCompilation(MessageLog);
}

void UK2Node_DreamWidgetPartCall::EarlyValidation(FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);
	if (GetTargetFunction() == nullptr)
	{
		MessageLog.Error(*LOCTEXT("CallNoFunction", "@@ calls a function that no longer exists. Delete it and pick it again from the widget's pin.").ToString(), this);
		return;
	}
	DreamWidgetPartLocal::CheckPart(this, FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input), DreamWidgetPartLocal::Authoritative(PartClass), MessageLog);
}

void UK2Node_DreamWidgetPartCall::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	// Not the function call's own expansion: the call is made again below, as an ordinary one, and that one expands itself.
	UK2Node::ExpandNode(CompilerContext, SourceGraph);

	UFunction* Function = GetTargetFunction();
	UEdGraphPin* SelfPin = FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
	UEdGraphPin* Part = Function != nullptr && SelfPin != nullptr
		? DreamWidgetPartLocal::SpawnPartRead(CompilerContext, SourceGraph, this, *SelfPin, DreamWidgetPartLocal::Authoritative(PartClass))
		: nullptr;
	if (Part == nullptr)
	{
		BreakAllNodeLinks();
		return;
	}

	UK2Node_CallFunction* Call = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	Call->SetFromFunction(Function);
	Call->AllocateDefaultPins();
	CompilerContext.MessageLog.NotifyIntermediateObjectCreation(Call, this);
	for (UEdGraphPin* Pin : Pins)
	{
		// Everything but the target moves across as it is -- exec, arguments, results, and the defaults typed into them.
		if (Pin == nullptr || Pin == SelfPin || Pin->ParentPin != nullptr)
		{
			continue;
		}
		if (UEdGraphPin* Into = Call->FindPin(Pin->PinName, Pin->Direction))
		{
			CompilerContext.MovePinLinksToIntermediate(*Pin, *Into);
		}
	}
	if (!CompilerContext.GetSchema()->TryCreateConnection(Part, Call->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input)))
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("CallNotConnected", "@@ could not call its function on the part.").ToString(), this);
	}
	BreakAllNodeLinks();
}

void UK2Node_DreamWidgetPartCall::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (!ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		return;
	}
	DreamWidgetPartLocal::ForEachPartClass([&ActionRegistrar, ActionKey](UClass* InPartClass, UClass* InDeclaring, bool /*bOwn*/)
	{
		const TSet<FName> Accessors = DreamWidgetPartLocal::AccessorFunctionNames(InPartClass);
		for (TFieldIterator<UFunction> It(InDeclaring, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (DreamWidgetPartLocal::IsOfferedFunction(*It, Accessors))
			{
				ActionRegistrar.AddBlueprintAction(ActionKey, UDreamWidgetPartNodeSpawner::CreateCall(InPartClass, *It));
			}
		}
	});
}

FBlueprintNodeSignature UK2Node_DreamWidgetPartCall::GetSignature() const
{
	FBlueprintNodeSignature Signature = Super::GetSignature();
	if (UClass* Class = DreamWidgetPartLocal::Authoritative(PartClass))
	{
		Signature.AddSubObject(Class);
	}
	return Signature;
}

// ---------------------------------------------------------------------------------------------------------------------
// A part's property, got or set on the widget
// ---------------------------------------------------------------------------------------------------------------------

UEdGraphPin* UK2Node_DreamWidgetPartProperty::GetTargetPin() const
{
	return FindPin(TargetPinName, EGPD_Input);
}

UEdGraphPin* UK2Node_DreamWidgetPartProperty::GetValuePin() const
{
	const FProperty* Field = Property.Get();
	return Field != nullptr ? FindPin(Field->GetFName(), bWrite ? EGPD_Input : EGPD_Output) : nullptr;
}

void UK2Node_DreamWidgetPartProperty::AllocateDefaultPins()
{
	if (bWrite)
	{
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);
	}
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UDreamWidget::StaticClass(), TargetPinName);
	if (const FProperty* Field = Property.Get())
	{
		FEdGraphPinType PinType;
		GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Field, PinType);
		UEdGraphPin* Value = CreatePin(bWrite ? EGPD_Input : EGPD_Output, PinType, Field->GetFName());
		Value->PinFriendlyName = Field->GetDisplayNameText();
		Value->PinToolTip = Field->GetToolTipText().ToString();
	}
	Super::AllocateDefaultPins();
}

FText UK2Node_DreamWidgetPartProperty::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const FProperty* Field = Property.Get();
	const FText Name = Field != nullptr ? Field->GetDisplayNameText() : LOCTEXT("PropertyMissing", "(missing property)");
	const FText Title = FText::Format(bWrite ? LOCTEXT("SetTitle", "Set {0}") : LOCTEXT("GetTitle", "Get {0}"), Name);
	if (TitleType == ENodeTitleType::FullTitle)
	{
		return FText::Format(LOCTEXT("PropertyTitle", "{0}\n{1}"), Title, DreamWidgetPartLocal::TargetLine(DreamWidgetPartLocal::Authoritative(PartClass)));
	}
	return Title;
}

FText UK2Node_DreamWidgetPartProperty::GetTooltipText() const
{
	const FProperty* Field = Property.Get();
	return FText::Format(LOCTEXT("PropertyTooltip", "{0}\n\nTarget is a widget: this {1} its {2}. When the widget is one of the tree's, the compile checks it still has that part."),
		Field != nullptr ? Field->GetToolTipText() : FText::GetEmpty(),
		bWrite ? LOCTEXT("PropertySets", "sets the property on") : LOCTEXT("PropertyGets", "reads the property of"),
		DreamWidgetPartNodes::DescribePart(DreamWidgetPartLocal::Authoritative(PartClass)));
}

void UK2Node_DreamWidgetPartProperty::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (!DreamWidgetPartLocal::WireWidget(GetSchema(), FromPin, GetTargetPin()))
	{
		Super::AutowireNewNode(FromPin);
	}
}

void UK2Node_DreamWidgetPartProperty::EarlyValidation(FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);
	if (Property.Get() == nullptr)
	{
		MessageLog.Error(*LOCTEXT("PropertyNoField", "@@ names a property that no longer exists. Delete it and pick it again from the widget's pin.").ToString(), this);
		return;
	}
	DreamWidgetPartLocal::CheckPart(this, GetTargetPin(), DreamWidgetPartLocal::Authoritative(PartClass), MessageLog);
}

void UK2Node_DreamWidgetPartProperty::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	FProperty* Field = Property.Get();
	UEdGraphPin* TargetPin = GetTargetPin();
	UEdGraphPin* ValuePin = GetValuePin();
	UEdGraphPin* Part = Field != nullptr && TargetPin != nullptr && ValuePin != nullptr
		? DreamWidgetPartLocal::SpawnPartRead(CompilerContext, SourceGraph, this, *TargetPin, DreamWidgetPartLocal::Authoritative(PartClass))
		: nullptr;
	if (Part == nullptr)
	{
		BreakAllNodeLinks();
		return;
	}

	// The engine's own variable nodes, aimed at the part: they go through a BlueprintGetter or BlueprintSetter when the
	// property has one, exactly as a Get or Set dragged off the part would.
	UK2Node_Variable* Access = nullptr;
	if (bWrite)
	{
		UK2Node_VariableSet* Set = CompilerContext.SpawnIntermediateNode<UK2Node_VariableSet>(this, SourceGraph);
		Set->VariableReference.SetExternalMember(Field->GetFName(), Field->GetOwnerClass());
		Set->AllocateDefaultPins();
		CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_Execute), *Set->GetExecPin());
		CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_Then), *Set->GetThenPin());
		CompilerContext.MovePinLinksToIntermediate(*ValuePin, *Set->FindPinChecked(Field->GetFName(), EGPD_Input));
		Access = Set;
	}
	else
	{
		UK2Node_VariableGet* Get = CompilerContext.SpawnIntermediateNode<UK2Node_VariableGet>(this, SourceGraph);
		Get->VariableReference.SetExternalMember(Field->GetFName(), Field->GetOwnerClass());
		Get->AllocateDefaultPins();
		CompilerContext.MovePinLinksToIntermediate(*ValuePin, *Get->FindPinChecked(Field->GetFName(), EGPD_Output));
		Access = Get;
	}
	CompilerContext.MessageLog.NotifyIntermediateObjectCreation(Access, this);
	if (!CompilerContext.GetSchema()->TryCreateConnection(Part, Access->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input)))
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("PropertyNotConnected", "@@ could not reach the property on the part.").ToString(), this);
	}
	BreakAllNodeLinks();
}

void UK2Node_DreamWidgetPartProperty::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (!ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		return;
	}
	DreamWidgetPartLocal::ForEachPartClass([&ActionRegistrar, ActionKey](UClass* InPartClass, UClass* InDeclaring, bool /*bOwn*/)
	{
		for (TFieldIterator<FProperty> It(InDeclaring, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (DreamWidgetPartLocal::IsOfferedProperty(*It))
			{
				ActionRegistrar.AddBlueprintAction(ActionKey, UDreamWidgetPartNodeSpawner::CreateProperty(InPartClass, *It, /*bInWrite*/false));
				if (DreamWidgetPartLocal::IsWritableProperty(*It))
				{
					ActionRegistrar.AddBlueprintAction(ActionKey, UDreamWidgetPartNodeSpawner::CreateProperty(InPartClass, *It, /*bInWrite*/true));
				}
			}
		}
	});
}

FText UK2Node_DreamWidgetPartProperty::GetMenuCategory() const
{
	return LOCTEXT("PropertyMenuCategory", "DreamGUI|Parts");
}

FBlueprintNodeSignature UK2Node_DreamWidgetPartProperty::GetSignature() const
{
	FBlueprintNodeSignature Signature = Super::GetSignature();
	if (UClass* Class = DreamWidgetPartLocal::Authoritative(PartClass))
	{
		Signature.AddSubObject(Class);
	}
	if (FProperty* Field = Property.Get())
	{
		Signature.AddSubObject(Field);
	}
	Signature.AddKeyValue(bWrite ? TEXT("Set") : TEXT("Get"));
	return Signature;
}

// ---------------------------------------------------------------------------------------------------------------------
// The menu actions
// ---------------------------------------------------------------------------------------------------------------------

namespace DreamWidgetPartSpawnerLocal
{
	void CustomizeRead(UEdGraphNode* InNode, bool /*bIsTemplateNode*/, UClass* InPartClass)
	{
		if (UK2Node_DreamWidgetPart* Node = Cast<UK2Node_DreamWidgetPart>(InNode))
		{
			Node->PartClass = InPartClass;
		}
	}

	void CustomizeCall(UEdGraphNode* InNode, bool /*bIsTemplateNode*/, UClass* InPartClass, UFunction* InFunction)
	{
		if (UK2Node_DreamWidgetPartCall* Node = Cast<UK2Node_DreamWidgetPartCall>(InNode))
		{
			// Before the pins are made: the spawner runs this ahead of AllocateDefaultPins.
			Node->SetFromFunction(InFunction);
			Node->PartClass = InPartClass;
		}
	}

	void CustomizeProperty(UEdGraphNode* InNode, bool /*bIsTemplateNode*/, UClass* InPartClass, TFieldPath<FProperty> InProperty, bool bInWrite)
	{
		if (UK2Node_DreamWidgetPartProperty* Node = Cast<UK2Node_DreamWidgetPartProperty>(InNode))
		{
			Node->PartClass = InPartClass;
			Node->Property = InProperty;
			Node->bWrite = bInWrite;
		}
	}

	UDreamWidgetPartNodeSpawner* Make(TSubclassOf<UEdGraphNode> InNodeClass, UClass* InPartClass)
	{
		UDreamWidgetPartNodeSpawner* Spawner = NewObject<UDreamWidgetPartNodeSpawner>(GetTransientPackage());
		Spawner->NodeClass = InNodeClass;
		Spawner->PartClass = InPartClass;
		// Everything the menu shows is filled in by the Create functions, so listing these never has to build a template
		// node to ask it; the category is replaced per menu with the widget and the part.
		FBlueprintActionUiSpec& Spec = Spawner->DefaultMenuSignature;
		Spec.Category = LOCTEXT("SpawnerCategory", "DreamGUI|Parts");
		Spec.DocExcerptTag = TEXT(" ");
		return Spawner;
	}
}

UDreamWidgetPartNodeSpawner* UDreamWidgetPartNodeSpawner::CreateRead(UClass* InPartClass)
{
	UDreamWidgetPartNodeSpawner* Spawner = DreamWidgetPartSpawnerLocal::Make(UK2Node_DreamWidgetPart::StaticClass(), InPartClass);
	Spawner->CustomizeNodeDelegate = FCustomizeNodeDelegate::CreateStatic(&DreamWidgetPartSpawnerLocal::CustomizeRead, InPartClass);
	FBlueprintActionUiSpec& Spec = Spawner->DefaultMenuSignature;
	Spec.MenuName = FText::Format(LOCTEXT("ReadMenuName", "Get {0}"), DreamWidgetPartNodes::DescribePart(InPartClass));
	Spec.Tooltip = FText::Format(LOCTEXT("ReadTooltip", "This widget's {0} itself, already typed, to hand on."),
		DreamWidgetPartNodes::DescribePart(InPartClass));
	Spec.Keywords = FText::Format(LOCTEXT("ReadKeywords", "part {0} {1}"), FText::FromString(InPartClass->GetName()),
		FDreamWidgetParts::DescribeKind(FDreamWidgetParts::KindOf(InPartClass)));
	Spec.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.Default_16x");
	return Spawner;
}

UDreamWidgetPartNodeSpawner* UDreamWidgetPartNodeSpawner::CreateCall(UClass* InPartClass, UFunction* InFunction)
{
	UDreamWidgetPartNodeSpawner* Spawner = DreamWidgetPartSpawnerLocal::Make(UK2Node_DreamWidgetPartCall::StaticClass(), InPartClass);
	Spawner->Function = InFunction;
	Spawner->CustomizeNodeDelegate = FCustomizeNodeDelegate::CreateStatic(&DreamWidgetPartSpawnerLocal::CustomizeCall, InPartClass, InFunction);
	FBlueprintActionUiSpec& Spec = Spawner->DefaultMenuSignature;
	Spec.MenuName = ObjectTools::GetUserFacingFunctionName(InFunction, /*bAllowFriendlyNames*/true);
	Spec.Tooltip = FText::FromString(ObjectTools::GetDefaultTooltipForFunction(InFunction));
	Spec.Keywords = UK2Node_CallFunction::GetKeywordsForFunction(InFunction);
	Spec.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Kismet.AllClasses.FunctionIcon");
	return Spawner;
}

UDreamWidgetPartNodeSpawner* UDreamWidgetPartNodeSpawner::CreateProperty(UClass* InPartClass, FProperty* InProperty, bool bInWrite)
{
	UDreamWidgetPartNodeSpawner* Spawner = DreamWidgetPartSpawnerLocal::Make(UK2Node_DreamWidgetPartProperty::StaticClass(), InPartClass);
	Spawner->Property = InProperty;
	Spawner->bWrite = bInWrite;
	Spawner->CustomizeNodeDelegate = FCustomizeNodeDelegate::CreateStatic(&DreamWidgetPartSpawnerLocal::CustomizeProperty,
		InPartClass, TFieldPath<FProperty>(InProperty), bInWrite);
	FBlueprintActionUiSpec& Spec = Spawner->DefaultMenuSignature;
	Spec.MenuName = FText::Format(bInWrite ? LOCTEXT("SetPropertyMenuName", "Set {0}") : LOCTEXT("GetPropertyMenuName", "Get {0}"),
		InProperty->GetDisplayNameText());
	Spec.Tooltip = InProperty->GetToolTipText();
	Spec.Keywords = FText::FromString(InProperty->GetName());
	Spec.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Kismet.AllClasses.VariableIcon");
	return Spawner;
}

UObject* UDreamWidgetPartNodeSpawner::FindMatchingPart(const UDreamWidget* InWidget) const
{
	if (InWidget == nullptr || PartClass == nullptr)
	{
		return nullptr;
	}
	if (!IsRead())
	{
		return FDreamWidgetParts::Find(InWidget, PartClass);
	}
	// A read is offered once per part, under the native class nearest to the part's own: offering it for every base
	// the part has would list "Get Visual" beside "Get Text" for the same object.
	TArray<UObject*> Parts;
	FDreamWidgetParts::GetAll(InWidget, Parts);
	for (UObject* Part : Parts)
	{
		if (DreamWidgetPartLocal::NearestNativeClass(Part->GetClass()) == PartClass)
		{
			return Part;
		}
	}
	return nullptr;
}

const UDreamWidget* UDreamWidgetPartNodeSpawner::ResolveContextWidget(const TArray<UEdGraphPin*>& InPins, FName* OutVariableName)
{
	// Only while dragging off exactly one widget-carrying output: these actions are about that widget, and listed anywhere
	// else they would be two thousand entries about nothing.
	if (InPins.Num() != 1 || InPins[0] == nullptr || InPins[0]->Direction != EGPD_Output)
	{
		return nullptr;
	}

	// One menu asks this for every action it lists, all in the same frame and about the same pin. Remember the last answer
	// for that pin and frame; the frame number keeps a pin that was freed and reallocated from being taken for the old one.
	struct FCache
	{
		const UEdGraphPin* Pin = nullptr;
		uint64 Frame = MAX_uint64;
		TWeakObjectPtr<const UDreamWidget> Widget;
		FName VariableName;
	};
	static FCache Cache;
	const UEdGraphPin* Pin = InPins[0];
	if (Cache.Pin != Pin || Cache.Frame != GFrameCounter)
	{
		Cache.Pin = Pin;
		Cache.Frame = GFrameCounter;
		Cache.VariableName = NAME_None;
		Cache.Widget = DreamWidgetPartNodes::ResolveAuthoredWidget(Pin, &Cache.VariableName);
	}
	if (OutVariableName != nullptr)
	{
		*OutVariableName = Cache.VariableName;
	}
	return Cache.Widget.Get();
}

bool UDreamWidgetPartNodeSpawner::IsTemplateNodeFilteredOut(FBlueprintActionFilter const& Filter) const
{
	// The nodes themselves filter nothing, so asking the base would only build a template.
	return FindMatchingPart(ResolveContextWidget(Filter.Context.Pins)) == nullptr;
}

FBlueprintActionUiSpec UDreamWidgetPartNodeSpawner::GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const
{
	FBlueprintActionUiSpec Spec = Super::GetUiSpec(Context, Bindings);

	// Filed under the widget, then the part as the widget really has it: dragging off PlayerLabel lists "Player Label",
	// and under it "Dream Text (Visual)" with everything that part takes, its base classes' members included.
	FName VariableName;
	const UDreamWidget* Widget = ResolveContextWidget(Context.Pins, &VariableName);
	if (const UObject* Part = FindMatchingPart(Widget))
	{
		Spec.Category = FText::Format(LOCTEXT("ContextCategory", "{0}|{1}"),
			FText::FromString(FName::NameToDisplayString(VariableName.ToString(), /*bIsBool*/false)),
			DreamWidgetPartNodes::DescribePart(Part->GetClass()));
		if (IsRead())
		{
			Spec.MenuName = FText::Format(LOCTEXT("ContextReadMenuName", "Get {0}"), DreamWidgetPartNodes::DescribePart(Part->GetClass()));
		}
	}
	return Spec;
}

FBlueprintNodeSignature UDreamWidgetPartNodeSpawner::GetSpawnerSignature() const
{
	FBlueprintNodeSignature Signature(NodeClass);
	Signature.AddSubObject(PartClass.Get());
	if (Function != nullptr)
	{
		Signature.AddSubObject(Function.Get());
	}
	if (FProperty* Field = Property.Get())
	{
		Signature.AddSubObject(Field);
		Signature.AddKeyValue(bWrite ? TEXT("Set") : TEXT("Get"));
	}
	return Signature;
}

#undef LOCTEXT_NAMESPACE
