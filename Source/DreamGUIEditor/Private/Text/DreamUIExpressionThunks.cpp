// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIExpressionThunks.h"

#include "DreamWidgetBlueprint.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace DreamUIExpressionThunks
{
const TCHAR* GeneratedGraphPrefix = TEXT("__DreamBinding_");
}

namespace DreamUIExpressionThunksLocal
{
	/** What one emitted sub-expression hands its consumer: a live pin, or a literal for the consumer's default. */
	struct FEmitted
	{
		UEdGraphPin* Pin = nullptr;
		FString LiteralDefault;
		FEdGraphPinType PinType;
		bool IsLiteral() const { return Pin == nullptr; }
	};

	enum class EScalarKind : uint8 { Bool, Int, Real, String, Other };

	EScalarKind Classify(const FEdGraphPinType& InType)
	{
		if (InType.ContainerType != EPinContainerType::None)
		{
			return EScalarKind::Other;
		}
		if (InType.PinCategory == UEdGraphSchema_K2::PC_Boolean) { return EScalarKind::Bool; }
		if (InType.PinCategory == UEdGraphSchema_K2::PC_Int) { return EScalarKind::Int; }
		if (InType.PinCategory == UEdGraphSchema_K2::PC_Real) { return EScalarKind::Real; }
		if (InType.PinCategory == UEdGraphSchema_K2::PC_String) { return EScalarKind::String; }
		return EScalarKind::Other;
	}

	FEdGraphPinType MakeScalarPinType(EScalarKind InKind)
	{
		FEdGraphPinType Type;
		switch (InKind)
		{
		case EScalarKind::Bool: Type.PinCategory = UEdGraphSchema_K2::PC_Boolean; break;
		case EScalarKind::Int: Type.PinCategory = UEdGraphSchema_K2::PC_Int; break;
		case EScalarKind::String: Type.PinCategory = UEdGraphSchema_K2::PC_String; break;
		case EScalarKind::Real:
		default:
			Type.PinCategory = UEdGraphSchema_K2::PC_Real;
			Type.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			break;
		}
		return Type;
	}

	struct FThunkContext
	{
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamUIDiagnosticBag* Diagnostics = nullptr;
		UEdGraph* Graph = nullptr;
		/** The exec chain's current tail; impure calls thread themselves onto it. */
		UEdGraphPin* LastExecPin = nullptr;
		bool bAnyImpureCall = false;
		bool bFailed = false;

		void Fail(const FDreamUISourceLocation& InLocation, const FString& InMessage)
		{
			if (!bFailed)
			{
				Diagnostics->AddError(EDreamUIDiagnosticCode::BindingExpressionUnsupported, InLocation, InMessage);
			}
			bFailed = true;
		}
	};

	/** The class whose members an expression may name: last compile's skeleton, else the parent. */
	UClass* GetSymbolClass(const UDreamWidgetBlueprint* InBlueprint)
	{
		if (InBlueprint->SkeletonGeneratedClass != nullptr)
		{
			return InBlueprint->SkeletonGeneratedClass;
		}
		return InBlueprint->ParentClass.Get();
	}

	bool FindVariablePinType(const UDreamWidgetBlueprint* InBlueprint, const FString& InName, FEdGraphPinType& OutType)
	{
		const FName VariableName(*InName);
		for (const FBPVariableDescription& Variable : InBlueprint->NewVariables)
		{
			if (Variable.VarName == VariableName)
			{
				OutType = Variable.VarType;
				return true;
			}
		}
		for (const FBPVariableDescription& Variable : InBlueprint->GeneratedVariables)
		{
			if (Variable.VarName == VariableName)
			{
				OutType = Variable.VarType;
				return true;
			}
		}
		if (UClass* SymbolClass = GetSymbolClass(InBlueprint))
		{
			if (const FProperty* Property = FindFProperty<FProperty>(SymbolClass, VariableName))
			{
				return GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, OutType);
			}
		}
		return false;
	}

	UFunction* FindSelfFunction(const UDreamWidgetBlueprint* InBlueprint, const FString& InName)
	{
		UClass* SymbolClass = GetSymbolClass(InBlueprint);
		return SymbolClass != nullptr ? SymbolClass->FindFunctionByName(FName(*InName)) : nullptr;
	}

	/** Feed InSource into InPin: a connection for a live pin, a default value for a literal. */
	bool ConnectOrDefault(UEdGraphPin* InPin, const FEmitted& InSource, FThunkContext& InContext, const FDreamUISourceLocation& InLocation)
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		if (InSource.IsLiteral())
		{
			Schema->TrySetDefaultValue(*InPin, InSource.LiteralDefault);
			return true;
		}
		if (!Schema->TryCreateConnection(InSource.Pin, InPin))
		{
			InContext.Fail(InLocation, FString::Printf(
				TEXT("a value of type '%s' cannot feed the '%s' input, and the generator knows no conversion between them"),
				*InSource.PinType.PinCategory.ToString(), *InPin->PinName.ToString()));
			return false;
		}
		return true;
	}

	FEmitted EmitExpression(const FDreamUIExpression& InExpression, FThunkContext& InContext);

	/** A UKismetMathLibrary call with A/B-style inputs; returns its output pin emitted. */
	FEmitted EmitMathCall(const TCHAR* InFunctionName, std::initializer_list<TPair<const TCHAR*, const FEmitted*>> InArguments,
		FThunkContext& InContext, const FDreamUISourceLocation& InLocation)
	{
		UFunction* Function = UKismetMathLibrary::StaticClass()->FindFunctionByName(FName(InFunctionName));
		if (Function == nullptr)
		{
			InContext.Fail(InLocation, FString::Printf(TEXT("internal: math library has no '%s'"), InFunctionName));
			return FEmitted();
		}
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*InContext.Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode(/*bSelectNewNode*/false);
		Node->SetFromFunction(Function);
		Creator.Finalize();

		for (const TPair<const TCHAR*, const FEmitted*>& Argument : InArguments)
		{
			UEdGraphPin* Pin = Node->FindPin(FName(Argument.Key));
			if (Pin == nullptr)
			{
				InContext.Fail(InLocation, FString::Printf(TEXT("internal: '%s' has no '%s' pin"), InFunctionName, Argument.Key));
				return FEmitted();
			}
			if (!ConnectOrDefault(Pin, *Argument.Value, InContext, InLocation))
			{
				return FEmitted();
			}
		}
		FEmitted Result;
		Result.Pin = Node->GetReturnValuePin();
		if (Result.Pin == nullptr)
		{
			InContext.Fail(InLocation, FString::Printf(TEXT("internal: '%s' returned nothing"), InFunctionName));
			return FEmitted();
		}
		Result.PinType = Result.Pin->PinType;
		return Result;
	}

	/** Promote an Int emitted value to Real via Conv_IntToDouble. Literals just re-type. */
	FEmitted PromoteIntToReal(const FEmitted& InValue, FThunkContext& InContext, const FDreamUISourceLocation& InLocation)
	{
		if (InValue.IsLiteral())
		{
			FEmitted Promoted = InValue;
			Promoted.PinType = MakeScalarPinType(EScalarKind::Real);
			return Promoted;
		}
		return EmitMathCall(TEXT("Conv_IntToDouble"), {{TEXT("InInt"), &InValue}}, InContext, InLocation);
	}

	/** The math-library spelling for a binary operator over one scalar kind, or null. */
	const TCHAR* ResolveBinaryFunction(const FString& InOperator, EScalarKind InOperands, EScalarKind& OutResult)
	{
		OutResult = EScalarKind::Bool;
		if (InOperands == EScalarKind::Bool)
		{
			if (InOperator == TEXT("&&")) { return TEXT("BooleanAND"); }
			if (InOperator == TEXT("||")) { return TEXT("BooleanOR"); }
			if (InOperator == TEXT("==")) { return TEXT("EqualEqual_BoolBool"); }
			if (InOperator == TEXT("!=")) { return TEXT("NotEqual_BoolBool"); }
			return nullptr;
		}
		if (InOperands == EScalarKind::Int)
		{
			if (InOperator == TEXT("==")) { return TEXT("EqualEqual_IntInt"); }
			if (InOperator == TEXT("!=")) { return TEXT("NotEqual_IntInt"); }
			if (InOperator == TEXT("<")) { return TEXT("Less_IntInt"); }
			if (InOperator == TEXT("<=")) { return TEXT("LessEqual_IntInt"); }
			if (InOperator == TEXT(">")) { return TEXT("Greater_IntInt"); }
			if (InOperator == TEXT(">=")) { return TEXT("GreaterEqual_IntInt"); }
			OutResult = EScalarKind::Int;
			if (InOperator == TEXT("+")) { return TEXT("Add_IntInt"); }
			if (InOperator == TEXT("-")) { return TEXT("Subtract_IntInt"); }
			if (InOperator == TEXT("*")) { return TEXT("Multiply_IntInt"); }
			if (InOperator == TEXT("%")) { return TEXT("Percent_IntInt"); }
			return nullptr;
		}
		if (InOperands == EScalarKind::Real)
		{
			if (InOperator == TEXT("==")) { return TEXT("EqualEqual_DoubleDouble"); }
			if (InOperator == TEXT("!=")) { return TEXT("NotEqual_DoubleDouble"); }
			if (InOperator == TEXT("<")) { return TEXT("Less_DoubleDouble"); }
			if (InOperator == TEXT("<=")) { return TEXT("LessEqual_DoubleDouble"); }
			if (InOperator == TEXT(">")) { return TEXT("Greater_DoubleDouble"); }
			if (InOperator == TEXT(">=")) { return TEXT("GreaterEqual_DoubleDouble"); }
			OutResult = EScalarKind::Real;
			if (InOperator == TEXT("+")) { return TEXT("Add_DoubleDouble"); }
			if (InOperator == TEXT("-")) { return TEXT("Subtract_DoubleDouble"); }
			if (InOperator == TEXT("*")) { return TEXT("Multiply_DoubleDouble"); }
			if (InOperator == TEXT("%")) { return TEXT("Percent_DoubleDouble"); }
			return nullptr;
		}
		if (InOperands == EScalarKind::String)
		{
			if (InOperator == TEXT("==")) { return TEXT("EqualEqual_StrStr"); }
			if (InOperator == TEXT("!=")) { return TEXT("NotEqual_StrStr"); }
			OutResult = EScalarKind::String;
			if (InOperator == TEXT("+")) { return TEXT("Concat_StrStr"); }
			return nullptr;
		}
		return nullptr;
	}

	FEmitted EmitLiteral(const FDreamUIExpression& InExpression)
	{
		FEmitted Literal;
		Literal.LiteralDefault = InExpression.LiteralRaw;
		switch (InExpression.LiteralKind)
		{
		case EDreamUIValueKind::Number:
			Literal.PinType = MakeScalarPinType(EScalarKind::Real);
			break;
		case EDreamUIValueKind::String:
			Literal.PinType = MakeScalarPinType(EScalarKind::String);
			break;
		default:
			// true / false -- the only identifiers the parser lets through as literals.
			Literal.PinType = MakeScalarPinType(EScalarKind::Bool);
			break;
		}
		return Literal;
	}

	FEmitted EmitVariableRef(const FDreamUIExpression& InExpression, FThunkContext& InContext)
	{
		FEdGraphPinType VariableType;
		if (!FindVariablePinType(InContext.Blueprint, InExpression.Symbol, VariableType))
		{
			InContext.Fail(InExpression.Location, FString::Printf(
				TEXT("'%s' is neither a variable nor a function on this class (as of the previous compile)"), *InExpression.Symbol));
			return FEmitted();
		}
		FGraphNodeCreator<UK2Node_VariableGet> Creator(*InContext.Graph);
		UK2Node_VariableGet* Node = Creator.CreateNode(false);
		Node->VariableReference.SetSelfMember(FName(*InExpression.Symbol));
		Creator.Finalize();

		FEmitted Result;
		Result.Pin = Node->FindPin(FName(*InExpression.Symbol));
		if (Result.Pin == nullptr)
		{
			InContext.Fail(InExpression.Location, FString::Printf(TEXT("internal: getter for '%s' grew no pin"), *InExpression.Symbol));
			return FEmitted();
		}
		Result.PinType = Result.Pin->PinType;
		return Result;
	}

	FEmitted EmitCall(const FDreamUIExpression& InExpression, FThunkContext& InContext)
	{
		UFunction* Function = FindSelfFunction(InContext.Blueprint, InExpression.Symbol);
		if (Function == nullptr)
		{
			InContext.Fail(InExpression.Location, FString::Printf(
				TEXT("'%s' is not a function on this class (as of the previous compile)"), *InExpression.Symbol));
			return FEmitted();
		}

		TArray<const FProperty*> InputParameters;
		const FProperty* ReturnParameter = nullptr;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnParameter = *It;
			}
			else if (!It->HasAnyPropertyFlags(CPF_OutParm) || It->HasAnyPropertyFlags(CPF_ReferenceParm))
			{
				InputParameters.Add(*It);
			}
		}
		if (ReturnParameter == nullptr)
		{
			InContext.Fail(InExpression.Location, FString::Printf(TEXT("'%s' returns nothing, so it cannot appear in an expression"), *InExpression.Symbol));
			return FEmitted();
		}
		if (InputParameters.Num() != InExpression.Operands.Num())
		{
			InContext.Fail(InExpression.Location, FString::Printf(
				TEXT("'%s' takes %d argument(s), and the expression passes %d"),
				*InExpression.Symbol, InputParameters.Num(), InExpression.Operands.Num()));
			return FEmitted();
		}

		FGraphNodeCreator<UK2Node_CallFunction> Creator(*InContext.Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode(false);
		Node->SetFromFunction(Function);
		Creator.Finalize();

		// An impure call rides the exec chain; a pure one floats, like it would in a hand-made graph.
		if (!Function->HasAnyFunctionFlags(FUNC_BlueprintPure))
		{
			InContext.bAnyImpureCall = true;
			UEdGraphPin* ExecutePin = Node->FindPin(UEdGraphSchema_K2::PN_Execute);
			if (ExecutePin != nullptr && InContext.LastExecPin != nullptr)
			{
				GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(InContext.LastExecPin, ExecutePin);
			}
			InContext.LastExecPin = Node->FindPin(UEdGraphSchema_K2::PN_Then);
		}

		for (int32 Index = 0; Index < InputParameters.Num(); ++Index)
		{
			const FEmitted Argument = EmitExpression(InExpression.Operands[Index], InContext);
			if (InContext.bFailed)
			{
				return FEmitted();
			}
			UEdGraphPin* Pin = Node->FindPin(InputParameters[Index]->GetFName());
			if (Pin == nullptr || !ConnectOrDefault(Pin, Argument, InContext, InExpression.Operands[Index].Location))
			{
				return FEmitted();
			}
		}

		FEmitted Result;
		Result.Pin = Node->FindPin(ReturnParameter->GetFName());
		if (Result.Pin == nullptr)
		{
			Result.Pin = Node->GetReturnValuePin();
		}
		if (Result.Pin == nullptr)
		{
			InContext.Fail(InExpression.Location, FString::Printf(TEXT("internal: the call to '%s' grew no return pin"), *InExpression.Symbol));
			return FEmitted();
		}
		Result.PinType = Result.Pin->PinType;
		return Result;
	}

	FEmitted EmitUnary(const FDreamUIExpression& InExpression, FThunkContext& InContext)
	{
		// Unary minus on a literal folds into the literal, which is also the only spelling that
		// reaches negative literals here (the lexer keeps `= -5` and tuples on the old path).
		if (InExpression.Symbol == TEXT("-") && InExpression.Operands[0].Kind == FDreamUIExpression::EKind::Literal
			&& InExpression.Operands[0].LiteralKind == EDreamUIValueKind::Number)
		{
			FEmitted Folded = EmitLiteral(InExpression.Operands[0]);
			Folded.LiteralDefault = TEXT("-") + Folded.LiteralDefault;
			return Folded;
		}

		const FEmitted Operand = EmitExpression(InExpression.Operands[0], InContext);
		if (InContext.bFailed)
		{
			return FEmitted();
		}
		const EScalarKind Kind = Classify(Operand.PinType);
		if (InExpression.Symbol == TEXT("!"))
		{
			if (Kind != EScalarKind::Bool)
			{
				InContext.Fail(InExpression.Location, TEXT("'!' expects a bool"));
				return FEmitted();
			}
			return EmitMathCall(TEXT("Not_PreBool"), {{TEXT("A"), &Operand}}, InContext, InExpression.Location);
		}
		// Unary minus on a live value: multiply by -1 in the operand's own width.
		FEmitted MinusOne;
		MinusOne.LiteralDefault = TEXT("-1");
		MinusOne.PinType = Operand.PinType;
		if (Kind == EScalarKind::Int)
		{
			return EmitMathCall(TEXT("Multiply_IntInt"), {{TEXT("A"), &Operand}, {TEXT("B"), &MinusOne}}, InContext, InExpression.Location);
		}
		if (Kind == EScalarKind::Real)
		{
			return EmitMathCall(TEXT("Multiply_DoubleDouble"), {{TEXT("A"), &Operand}, {TEXT("B"), &MinusOne}}, InContext, InExpression.Location);
		}
		InContext.Fail(InExpression.Location, TEXT("unary '-' expects a number"));
		return FEmitted();
	}

	FEmitted EmitBinary(const FDreamUIExpression& InExpression, FThunkContext& InContext)
	{
		FEmitted Left = EmitExpression(InExpression.Operands[0], InContext);
		if (InContext.bFailed) { return FEmitted(); }
		FEmitted Right = EmitExpression(InExpression.Operands[1], InContext);
		if (InContext.bFailed) { return FEmitted(); }

		EScalarKind LeftKind = Classify(Left.PinType);
		EScalarKind RightKind = Classify(Right.PinType);

		// Literals adopt the other side's kind; two literals settle on the left's.
		if (Left.IsLiteral() && !Right.IsLiteral()) { Left.PinType = Right.PinType; LeftKind = RightKind; }
		if (Right.IsLiteral() && !Left.IsLiteral()) { Right.PinType = Left.PinType; RightKind = LeftKind; }

		// Mixed int/real promotes the int side, the way every C-family author already expects.
		if (LeftKind == EScalarKind::Int && RightKind == EScalarKind::Real)
		{
			Left = PromoteIntToReal(Left, InContext, InExpression.Location);
			if (InContext.bFailed) { return FEmitted(); }
			LeftKind = EScalarKind::Real;
		}
		else if (LeftKind == EScalarKind::Real && RightKind == EScalarKind::Int)
		{
			Right = PromoteIntToReal(Right, InContext, InExpression.Location);
			if (InContext.bFailed) { return FEmitted(); }
			RightKind = EScalarKind::Real;
		}

		if (LeftKind != RightKind || LeftKind == EScalarKind::Other)
		{
			InContext.Fail(InExpression.Location, FString::Printf(
				TEXT("'%s' has no meaning between these operand types; move the logic into a function and bind that"),
				*InExpression.Symbol));
			return FEmitted();
		}

		EScalarKind ResultKind = EScalarKind::Bool;
		const TCHAR* FunctionName = ResolveBinaryFunction(InExpression.Symbol, LeftKind, ResultKind);
		if (FunctionName == nullptr)
		{
			InContext.Fail(InExpression.Location, FString::Printf(
				TEXT("'%s' is not defined for this operand type"), *InExpression.Symbol));
			return FEmitted();
		}
		return EmitMathCall(FunctionName, {{TEXT("A"), &Left}, {TEXT("B"), &Right}}, InContext, InExpression.Location);
	}

	FEmitted EmitExpression(const FDreamUIExpression& InExpression, FThunkContext& InContext)
	{
		switch (InExpression.Kind)
		{
		case FDreamUIExpression::EKind::Literal: return EmitLiteral(InExpression);
		case FDreamUIExpression::EKind::VariableRef: return EmitVariableRef(InExpression, InContext);
		case FDreamUIExpression::EKind::Call: return EmitCall(InExpression, InContext);
		case FDreamUIExpression::EKind::Unary: return EmitUnary(InExpression, InContext);
		case FDreamUIExpression::EKind::Binary: return EmitBinary(InExpression, InContext);
		default:
			InContext.Fail(InExpression.Location, TEXT("internal: unknown expression node"));
			return FEmitted();
		}
	}

	FString MakeThunkName(const FString& InNodeId, const FString& InPropertyName)
	{
		FString Sanitized = InNodeId + TEXT("_") + InPropertyName;
		for (TCHAR& Char : Sanitized)
		{
			if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
			{
				Char = TEXT('_');
			}
		}
		return DreamUIExpressionThunks::GeneratedGraphPrefix + Sanitized;
	}

	void LowerProperty(UDreamWidgetBlueprint* InBlueprint, const FString& InNodeId, FDreamUIProperty& InProperty, FDreamUIDiagnosticBag& InDiagnostics)
	{
		const FString ThunkName = MakeThunkName(InNodeId, InProperty.Name);

		// Assembled by hand rather than through AddFunctionGraph: its terminator pass looks the graph
		// name up on the SKELETON and inherits the signature it finds -- which, on every compile
		// after the first, is this very thunk, so the graph came pre-fitted with a second result
		// node and the function grew a phantom parameter. It also marks the Blueprint structurally
		// modified, which is no thing to do from inside the compile that is already running.
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(InBlueprint, FName(*ThunkName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		InBlueprint->FunctionGraphs.Add(Graph);

		FGraphNodeCreator<UK2Node_FunctionEntry> EntryCreator(*Graph);
		UK2Node_FunctionEntry* Entry = EntryCreator.CreateNode(false);
		Entry->FunctionReference.SetSelfMember(Graph->GetFName());
		EntryCreator.Finalize();

		FThunkContext Context;
		Context.Blueprint = InBlueprint;
		Context.Diagnostics = &InDiagnostics;
		Context.Graph = Graph;
		Context.LastExecPin = Entry->FindPin(UEdGraphSchema_K2::PN_Then);

		const FEmitted Root = EmitExpression(InProperty.BindingExpression.GetValue(), Context);
		if (Context.bFailed)
		{
			// The expression STAYS on the property: with it set and no function name, the builder's
			// guard skips the binding silently -- the refusal already errored here, and clearing it
			// would send the property down the literal path to complain a second time about an
			// empty value that was never the author's mistake.
			FBlueprintEditorUtils::RemoveGraph(InBlueprint, Graph);
			return;
		}

		// The result node, typed by what the expression turned out to be. The compiler's existing
		// signature check then compares this against the destination property and reports any
		// mismatch with the message it always used.
		FGraphNodeCreator<UK2Node_FunctionResult> ResultCreator(*Graph);
		UK2Node_FunctionResult* Result = ResultCreator.CreateNode(false);
		Result->FunctionReference.SetSelfMember(Graph->GetFName());
		ResultCreator.Finalize();
		// On every compile AFTER the first, Finalize resolves the self-reference against the
		// SKELETON -- which holds last compile's thunk -- and the node arrives with a ReturnValue
		// pin already grown from that signature. Creating a second one gave the function a phantom
		// parameter and failed the binding's shape check, so: reuse the inherited pin (re-typed, in
		// case the expression's type changed) and only create one when the node came bare.
		UEdGraphPin* ReturnPin = Result->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Input);
		if (ReturnPin != nullptr)
		{
			ReturnPin->PinType = Root.PinType;
		}
		else
		{
			ReturnPin = Result->CreateUserDefinedPin(UEdGraphSchema_K2::PN_ReturnValue, Root.PinType, EGPD_Input);
		}
		if (ReturnPin == nullptr
			|| !ConnectOrDefault(ReturnPin, Root, Context, InProperty.Location))
		{
			FBlueprintEditorUtils::RemoveGraph(InBlueprint, Graph);
			return;
		}

		// Close the exec chain, and declare purity honestly: a thunk that called something impure
		// is not pure, and lying about it is how side effects run at unpredictable times.
		UEdGraphPin* ResultExecutePin = Result->FindPin(UEdGraphSchema_K2::PN_Execute);
		if (ResultExecutePin != nullptr && Context.LastExecPin != nullptr)
		{
			GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Context.LastExecPin, ResultExecutePin);
		}
		Entry->AddExtraFlags(FUNC_Private | (Context.bAnyImpureCall ? 0 : FUNC_BlueprintPure));

		InProperty.BindingFunction = ThunkName;
		InProperty.BindingExpression.Reset();
	}

	void WalkNode(UDreamWidgetBlueprint* InBlueprint, FDreamUINode& InNode, FDreamUIDiagnosticBag& InDiagnostics)
	{
		auto LowerAll = [InBlueprint, &InNode, &InDiagnostics](TArray<FDreamUIProperty>& InProperties)
		{
			for (FDreamUIProperty& Property : InProperties)
			{
				if (Property.BindingExpression.IsSet())
				{
					LowerProperty(InBlueprint, InNode.Id, Property, InDiagnostics);
				}
			}
		};
		LowerAll(InNode.Properties);
		LowerAll(InNode.SlotProperties);
		for (FDreamUIComponent& Component : InNode.Components)
		{
			LowerAll(Component.Properties);
		}
		for (FDreamUINode& Child : InNode.Children)
		{
			WalkNode(InBlueprint, Child, InDiagnostics);
		}
	}
}

void DreamUIExpressionThunks::Generate(UDreamWidgetBlueprint* InBlueprint, FDreamUIAst& InAst, FDreamUIDiagnosticBag& InDiagnostics)
{
	using namespace DreamUIExpressionThunksLocal;

	// Regenerate-each-compile, the same contract GeneratedVariables live under: drop every graph
	// this pass ever made, then rebuild the ones the file still asks for. A copy of the array,
	// because RemoveGraph edits it under the loop.
	TArray<TObjectPtr<UEdGraph>> Graphs = InBlueprint->FunctionGraphs;
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph != nullptr && Graph->GetName().StartsWith(GeneratedGraphPrefix))
		{
			FBlueprintEditorUtils::RemoveGraph(InBlueprint, Graph);
		}
	}

	if (InAst.bHasRoot)
	{
		WalkNode(InBlueprint, InAst.Root, InDiagnostics);
	}
}
