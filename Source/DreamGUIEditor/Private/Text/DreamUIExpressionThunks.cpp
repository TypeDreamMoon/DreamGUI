// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIExpressionThunks.h"

#include "DreamWidgetBlueprint.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
// An emit route may leave from an FDreamUIEventDelegate as well as from a multicast delegate, and the
// handler's parameter is then the one the struct declares it fires with.
#include "Event/DreamUIEventDelegate.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace DreamUIExpressionThunks
{
const TCHAR* GeneratedGraphPrefix = TEXT("__DreamBinding_");
const TCHAR* GeneratedEmitPrefix = TEXT("__DreamEmit_");
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

	/**
	 * The tree Generate is lowering, for the whole of one call.
	 *
	 * A file-scope pointer under a TGuardValue rather than a sixth parameter threaded through
	 * LowerWornStyles, WalkNode, LowerPropertyList, LowerProperty and LowerTwoWay -- five signatures
	 * that would exist to carry one thing none of them reads. This pass runs on the game thread from
	 * inside one compile and never re-enters; the guard is what says so out loud, and the alternative
	 * was five call sites to keep in step for a value constant across all of them.
	 */
	const FDreamUIAst* GLoweringAst = nullptr;

	/**
	 * Where the emit routes of the same call go, and which events they may not raise -- the same arrangement as
	 * GLoweringAst and for the same reason: LowerPropertyList is the one function that sees a route, and threading
	 * two more parameters through every walk above it would be five signatures carrying what none of them reads.
	 * Null outside Generate, and null inside it for a caller that asked for no routes.
	 */
	TArray<DreamUIExpressionThunks::FEmitRoute>* GEmitRoutes = nullptr;
	const TSet<FString>* GRefusedEvents = nullptr;

	struct FThunkContext
	{
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamUIDiagnosticBag* Diagnostics = nullptr;
		/**
		 * The code a refusal is raised under. BindingExpressionUnsupported for everything an expression can get
		 * wrong; an emit handler switches it to EmitArgumentMismatch for the one step that is the ROUTE's mistake
		 * rather than the expression's -- an argument of the wrong type for the event it is handed to.
		 */
		EDreamUIDiagnosticCode FailCode = EDreamUIDiagnosticCode::BindingExpressionUnsupported;
		/** Put in front of a refusal's message while set, so an argument's says which emit it belongs to. */
		FString FailPrefix;
		/**
		 * Names an expression may read that are not members of the class: an emit handler's parameters, which are
		 * whatever the source event sends (`OnValueChanged -> emit Changed(Value)`). Looked up before the class, as a
		 * function's parameters shadow its members in any language that has both.
		 */
		TMap<FString, UEdGraphPin*> LocalPins;
		/**
		 * The tree the expression came out of, for `@Name`.
		 *
		 * A resource is resolved HERE rather than substituted into the AST, and the difference is not
		 * style: the patcher owns this tree's byte offsets, so replacing `@Accent` with `#FF6600`
		 * inside it would leave every later edit measured against columns that no longer exist.
		 */
		const FDreamUIAst* Ast = nullptr;
		UEdGraph* Graph = nullptr;
		/** The exec chain's current tail; impure calls thread themselves onto it. */
		UEdGraphPin* LastExecPin = nullptr;
		bool bAnyImpureCall = false;
		bool bFailed = false;

		void Fail(const FDreamUISourceLocation& InLocation, const FString& InMessage)
		{
			if (!bFailed)
			{
				Diagnostics->AddError(FailCode, InLocation, FailPrefix + InMessage);
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
		if (InSource.IsLiteral() && InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text
			&& InPin->PinType.ContainerType == EPinContainerType::None)
		{
			// A text pin keeps its default in DefaultTextValue, and TrySetDefaultValue writes the STRING one, which a
			// text pin never reads: `"Hello"` handed to a text input compiled green and arrived empty. Only a string
			// literal is a text -- a number or a bool given where text is wanted is the author's type mistake, and
			// converting it silently would be the generator deciding what they meant.
			if (Classify(InSource.PinType) != EScalarKind::String)
			{
				InContext.Fail(InLocation, FString::Printf(
					TEXT("'%s' cannot be the '%s' input: it takes text, and only a quoted string is one"),
					*InSource.LiteralDefault, *InPin->PinName.ToString()));
				return false;
			}
			Schema->TrySetDefaultText(*InPin, FText::FromString(InSource.LiteralDefault));
			return true;
		}
		if (InSource.IsLiteral())
		{
			// Asked BEFORE the write, because TrySetDefaultValue returns void and does not refuse:
			// it writes whatever it is given and the pin later reads whatever FCString::Atof or
			// appStringToBool makes of it. `<- Scale(24px)` therefore compiled green and drove 24,
			// and `<- Toggle("yes")` drove true -- values nobody wrote, in a binding nothing flagged.
			// The schema's own judgement rather than a private one, so a literal this generator
			// accepts is exactly a literal the graph compiler would.
			const FString Refusal = Schema->IsPinDefaultValid(InPin, InSource.LiteralDefault, nullptr, FText::GetEmpty());
			if (!Refusal.IsEmpty())
			{
				InContext.Fail(InLocation, FString::Printf(
					TEXT("'%s' cannot be the '%s' input: %s"),
					*InSource.LiteralDefault, *InPin->PinName.ToString(), *Refusal));
				return false;
			}
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

	FEmitted EmitLiteral(const FDreamUIExpression& InExpression, FThunkContext& InContext)
	{
		FEmitted Literal;
		Literal.LiteralDefault = InExpression.LiteralRaw;
		EDreamUIValueKind Kind = InExpression.LiteralKind;

		if (Kind == EDreamUIValueKind::ResourceRef)
		{
			// `@Accent` in an expression: the entry's own literal, taken at emit time. Only the two
			// declared types a thunk pin can BE -- a Number and a String -- because an expression is
			// arithmetic and comparison, and a colour or a vector has no operator in this grammar to
			// take part in one. Refusing the rest here beats emitting a pin whose default reads
			// "#FF6600" and watching K2 get 0 out of it.
			const FDreamUIResource* Entry = InContext.Ast != nullptr
				? InContext.Ast->FindResource(InExpression.LiteralRaw) : nullptr;
			if (Entry == nullptr)
			{
				InContext.Fail(InExpression.Location, FString::Printf(
					TEXT("'@%s' names no entry in a resources block"), *InExpression.LiteralRaw));
				return FEmitted();
			}
			if (Entry->Value.Kind != EDreamUIValueKind::Number && Entry->Value.Kind != EDreamUIValueKind::String)
			{
				InContext.Fail(InExpression.Location, FString::Printf(
					TEXT("'@%s' is declared %s, and an expression can only carry a Number or a String -- assign it instead"),
					*InExpression.LiteralRaw, *Entry->TypeName));
				return FEmitted();
			}
			Literal.LiteralDefault = Entry->Value.Raw;
			Kind = Entry->Value.Kind;
		}

		switch (Kind)
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
		// A parameter of the function being generated: no node to make, the entry's own pin IS the value.
		if (UEdGraphPin* const* LocalPin = InContext.LocalPins.Find(InExpression.Symbol))
		{
			FEmitted Local;
			Local.Pin = *LocalPin;
			Local.PinType = Local.Pin->PinType;
			return Local;
		}

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
			// A variable added since the last compile: this pass runs BEFORE the skeleton regen, so
			// the node's own allocation resolved nothing. The type is known from the Blueprint's
			// variable description, and a pin with the right name and type is all the compiler
			// matches on.
			Result.Pin = Node->CreatePin(EGPD_Output, VariableType, FName(*InExpression.Symbol));
		}
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

		// The ARGUMENTS first, and this order is the whole correctness of a nested call.
		//
		// Emitting an argument can put its own impure call on the exec chain, and an argument has to
		// have RUN before the call that consumes it. Threading this node onto the chain before the
		// recursion put it ahead of its own operands: `SetTitle(NextPage())` ran SetTitle first and
		// read whatever NextPage's return pin held before NextPage executed -- the type's default,
		// every time, with no error anywhere. Wiring the value pins does not depend on the exec
		// chain, so nothing is lost by doing them first.
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

		// An impure call rides the exec chain; a pure one floats, like it would in a hand-made graph.
		// LastExecPin is now the tail the arguments left behind, so this lands after all of them.
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
		//
		// Gated on Number and NOT on "is a literal", which now matters: a ResourceRef literal
		// (`-@Gain`) carries the resource's NAME in LiteralRaw, so folding a '-' onto it would
		// produce "-Gain" and look the entry up under a name nothing declares. Those fall through
		// to the general path below, where EmitLiteral resolves the entry first and the math
		// library does the negating -- one node more, and right.
		if (InExpression.Symbol == TEXT("-") && InExpression.Operands[0].Kind == FDreamUIExpression::EKind::Literal
			&& InExpression.Operands[0].LiteralKind == EDreamUIValueKind::Number)
		{
			FEmitted Folded = EmitLiteral(InExpression.Operands[0], InContext);
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
		case FDreamUIExpression::EKind::Literal: return EmitLiteral(InExpression, InContext);
		case FDreamUIExpression::EKind::VariableRef: return EmitVariableRef(InExpression, InContext);
		case FDreamUIExpression::EKind::Call: return EmitCall(InExpression, InContext);
		case FDreamUIExpression::EKind::Unary: return EmitUnary(InExpression, InContext);
		case FDreamUIExpression::EKind::Binary: return EmitBinary(InExpression, InContext);
		default:
			InContext.Fail(InExpression.Location, TEXT("internal: unknown expression node"));
			return FEmitted();
		}
	}

	/**
	 * The graph name for one lowered property, guaranteed not to be another one's.
	 *
	 * `NodeId_PropertyName` with everything non-alphanumeric mapped to '_' is not an identity: '_' is
	 * both the separator and a legal character in either half, so `A_B` + `C`, `A` + `B_C` and
	 * `A.B` + `C` all come out `A_B_C`. Two properties sharing a graph name is worse than a clash --
	 * FBlueprintEditorUtils::CreateNewGraph renames the FIRST graph out of the way rather than
	 * refusing (BlueprintEditorUtils.cpp:2253), so both bindings end up naming the second
	 * expression's function, silently, and the displaced graph is left in FunctionGraphs forever.
	 *
	 * Only the second claimant is disambiguated, and with a hash of the pair AS WRITTEN rather than a
	 * counter: the ordinary file keeps names an author can recognise in the graph list, and a file
	 * that does collide gets the same names on every compile instead of names that move when a line
	 * is reordered.
	 */
	FString MakeThunkName(const TCHAR* InPrefix, const FString& InNodeId, const FString& InPropertyName,
		TSet<FString>& InOutClaimedNames)
	{
		FString Sanitized = InNodeId + TEXT("_") + InPropertyName;
		for (TCHAR& Char : Sanitized)
		{
			if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
			{
				Char = TEXT('_');
			}
		}

		FString Name = InPrefix + Sanitized;
		// An id and a property path, each of a legal length, add up past what an FName holds, and an FName past NAME_SIZE stops
		// the editor. Cut, with the whole spelling's hash, so the same pair keeps the same name; the room left is for the
		// disambiguation below.
		constexpr int32 MaxThunkName = NAME_SIZE - 32;
		if (Name.Len() > MaxThunkName)
		{
			Name = FString::Printf(TEXT("%s_%08X"), *Name.Left(MaxThunkName - 9), FCrc::StrCrc32(*Name));
		}
		if (InOutClaimedNames.Contains(Name))
		{
			// US-31, a character no .dui token can contain, so the two halves cannot be confused
			// with each other the way the sanitised spelling confuses them.
			const FString Key = FString(InPrefix) + InNodeId + TEXT("\x1f") + InPropertyName;
			const FString Disambiguated = FString::Printf(TEXT("%s_%08X"), *Name, FCrc::StrCrc32(*Key));
			Name = Disambiguated;
			for (int32 Attempt = 2; InOutClaimedNames.Contains(Name); ++Attempt)
			{
				// The same pair twice -- two `<-` lines on one property of one node. The file is its
				// own problem; this only has to make sure no graph is ever created over another.
				Name = FString::Printf(TEXT("%s_%d"), *Disambiguated, Attempt);
			}
		}
		InOutClaimedNames.Add(Name);
		return Name;
	}

	/**
	 * True when this graph is one this pass made, whatever it is called NOW.
	 *
	 * The prefix sweep alone was not enough, and the gap is the same one MakeThunkName closes from
	 * the other side: CreateNewGraph renames a displaced graph to "EdGraph_0" and leaves it in
	 * FunctionGraphs, where a prefix match can never find it again -- so every collision leaked one
	 * graph, and the class grew a function called EdGraph_0 on every compile from then on. The entry
	 * node's FunctionReference still holds the name the graph was CREATED with, which is the one
	 * record the rename does not touch.
	 */
	bool IsThunkGraphName(const FString& InName)
	{
		return InName.StartsWith(DreamUIExpressionThunks::GeneratedGraphPrefix)
			|| InName.StartsWith(DreamUIExpressionThunks::GeneratedEmitPrefix)
			|| InName.StartsWith(TEXT("__DreamTwoWayGet_"))
			|| InName.StartsWith(TEXT("__DreamTwoWaySet_"));
	}

	bool IsGeneratedThunkGraph(const UEdGraph* InGraph)
	{
		if (IsThunkGraphName(InGraph->GetName()))
		{
			return true;
		}
		for (const UEdGraphNode* Node : InGraph->Nodes)
		{
			const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node);
			if (Entry != nullptr && IsThunkGraphName(Entry->FunctionReference.GetMemberName().ToString()))
			{
				return true;
			}
		}
		return false;
	}

	/** The empty function graph plus its hand-made entry -- see LowerProperty for why by hand. */
	UEdGraph* BeginThunkGraph(UDreamWidgetBlueprint* InBlueprint, const FString& InName, UK2Node_FunctionEntry*& OutEntry)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(InBlueprint, FName(*InName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		InBlueprint->FunctionGraphs.Add(Graph);

		FGraphNodeCreator<UK2Node_FunctionEntry> EntryCreator(*Graph);
		OutEntry = EntryCreator.CreateNode(false);
		OutEntry->FunctionReference.SetSelfMember(Graph->GetFName());
		EntryCreator.Finalize();
		return Graph;
	}

	/** The result node with a reuse-or-create return pin, the exec chain's close, and the flags. */
	bool FinishThunkGraph(UEdGraph* InGraph, UK2Node_FunctionEntry* InEntry, const FEmitted& InRoot,
		FThunkContext& InContext, const FDreamUISourceLocation& InLocation)
	{
		FGraphNodeCreator<UK2Node_FunctionResult> ResultCreator(*InGraph);
		UK2Node_FunctionResult* Result = ResultCreator.CreateNode(false);
		Result->FunctionReference.SetSelfMember(InGraph->GetFName());
		ResultCreator.Finalize();
		// On every compile AFTER the first, Finalize resolves the self-reference against the
		// SKELETON -- which holds last compile's thunk -- and the node arrives with a ReturnValue
		// pin already grown from that signature. Creating a second one gave the function a phantom
		// parameter and failed the binding's shape check, so: reuse the inherited pin (re-typed, in
		// case the expression's type changed) and only create one when the node came bare.
		//
		// A real-typed expression returns FLOAT, not the double the math library computes in. The
		// binding's shape check compares the return property to the TARGET property with SameType,
		// and every bindable real property in the framework is a float UPROPERTY -- a double
		// return can never bind one. K2 treats the precisions as one connectable Real category, so
		// the double output narrows into the float return pin without a conversion node.
		FEdGraphPinType ReturnType = InRoot.PinType;
		if (ReturnType.PinCategory == UEdGraphSchema_K2::PC_Real && ReturnType.ContainerType == EPinContainerType::None)
		{
			ReturnType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		}
		UEdGraphPin* ReturnPin = Result->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Input);
		if (ReturnPin != nullptr)
		{
			ReturnPin->PinType = ReturnType;
		}
		else
		{
			ReturnPin = Result->CreateUserDefinedPin(UEdGraphSchema_K2::PN_ReturnValue, ReturnType, EGPD_Input);
		}
		if (ReturnPin == nullptr || !ConnectOrDefault(ReturnPin, InRoot, InContext, InLocation))
		{
			return false;
		}
		UEdGraphPin* ResultExecutePin = Result->FindPin(UEdGraphSchema_K2::PN_Execute);
		if (ResultExecutePin != nullptr && InContext.LastExecPin != nullptr)
		{
			GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(InContext.LastExecPin, ResultExecutePin);
		}
		InEntry->AddExtraFlags(FUNC_Private | (InContext.bAnyImpureCall ? 0 : FUNC_BlueprintPure));
		return true;
	}

	void LowerProperty(UDreamWidgetBlueprint* InBlueprint, const FString& InNodeId, FDreamUIProperty& InProperty,
		FDreamUIDiagnosticBag& InDiagnostics, TSet<FString>& InOutClaimedNames)
	{
		const FString ThunkName = MakeThunkName(DreamUIExpressionThunks::GeneratedGraphPrefix, InNodeId, InProperty.Name, InOutClaimedNames);

		// Assembled by hand rather than through AddFunctionGraph: its terminator pass looks the graph
		// name up on the SKELETON and inherits the signature it finds -- which, on every compile
		// after the first, is this very thunk, so the graph came pre-fitted with a second result
		// node and the function grew a phantom parameter. It also marks the Blueprint structurally
		// modified, which is no thing to do from inside the compile that is already running.
		UK2Node_FunctionEntry* Entry = nullptr;
		UEdGraph* Graph = BeginThunkGraph(InBlueprint, ThunkName, Entry);

		FThunkContext Context;
		Context.Blueprint = InBlueprint;
		Context.Diagnostics = &InDiagnostics;
		Context.Ast = GLoweringAst;
		Context.Graph = Graph;
		Context.LastExecPin = Entry->FindPin(UEdGraphSchema_K2::PN_Then);

		const FEmitted Root = EmitExpression(InProperty.BindingExpression.GetValue(), Context);
		if (Context.bFailed || !FinishThunkGraph(Graph, Entry, Root, Context, InProperty.Location))
		{
			// The expression STAYS on the property: with it set and no function name, the builder's
			// guard skips the binding silently -- the refusal already errored here, and clearing it
			// would send the property down the literal path to complain a second time about an
			// empty value that was never the author's mistake.
			FBlueprintEditorUtils::RemoveGraph(InBlueprint, Graph);
			return;
		}

		InProperty.BindingFunction = ThunkName;
		InProperty.BindingExpression.Reset();
	}

	void LowerTwoWay(UDreamWidgetBlueprint* InBlueprint, const FString& InNodeId, FDreamUIProperty& InProperty,
		TArray<FDreamUIProperty>& OutSynthesized, FDreamUIDiagnosticBag& InDiagnostics, TSet<FString>& InOutClaimedNames)
	{
		FEdGraphPinType VariableType;
		if (!FindVariablePinType(InBlueprint, InProperty.TwoWayProperty, VariableType))
		{
			InDiagnostics.AddError(EDreamUIDiagnosticCode::BindingExpressionUnsupported, InProperty.Location,
				FString::Printf(TEXT("'<-> %s' names no variable on this class (as of the previous compile)"), *InProperty.TwoWayProperty));
			return;
		}

		// The forward half: a generated getter reading the variable, standing exactly where any
		// bound function stands. TwoWayProperty stays set -- the builder reads it to fill the
		// binding's NotifyField and to prefer the silent setter.
		const FString GetterName = MakeThunkName(TEXT("__DreamTwoWayGet_"), InNodeId, InProperty.Name, InOutClaimedNames);
		{
			UK2Node_FunctionEntry* Entry = nullptr;
			UEdGraph* Graph = BeginThunkGraph(InBlueprint, GetterName, Entry);
			FThunkContext Context;
			Context.Blueprint = InBlueprint;
			Context.Diagnostics = &InDiagnostics;
			Context.Ast = GLoweringAst;
			Context.Graph = Graph;
			Context.LastExecPin = Entry->FindPin(UEdGraphSchema_K2::PN_Then);

			FDreamUIExpression VariableExpression;
			VariableExpression.Kind = FDreamUIExpression::EKind::VariableRef;
			VariableExpression.Symbol = InProperty.TwoWayProperty;
			VariableExpression.Location = InProperty.Location;
			const FEmitted Root = EmitExpression(VariableExpression, Context);
			if (Context.bFailed || !FinishThunkGraph(Graph, Entry, Root, Context, InProperty.Location))
			{
				FBlueprintEditorUtils::RemoveGraph(InBlueprint, Graph);
				return;
			}
		}

		// The reverse half: a generated setter the control's changed event routes into, writing the
		// variable through a real K2 variable-set -- which is what makes a FieldNotify variable
		// broadcast, which is what re-evaluates every OTHER binding reading it. The echo back into
		// THIS control dies at the silent setter the forward half prefers.
		const FString SetterName = MakeThunkName(TEXT("__DreamTwoWaySet_"), InNodeId, InProperty.Name, InOutClaimedNames);
		{
			UK2Node_FunctionEntry* Entry = nullptr;
			UEdGraph* Graph = BeginThunkGraph(InBlueprint, SetterName, Entry);
			// Reuse-or-create, the third door of the same trap the result node had: when a skeleton
			// from an earlier pass already holds this thunk, Finalize resolves the self-reference
			// and the entry arrives with the parameter pin grown; creating it again made NewValue1
			// and a two-parameter setter no event could route to.
			UEdGraphPin* NewValuePin = Entry->FindPin(TEXT("NewValue"), EGPD_Output);
			if (NewValuePin != nullptr)
			{
				NewValuePin->PinType = VariableType;
			}
			else
			{
				NewValuePin = Entry->CreateUserDefinedPin(TEXT("NewValue"), VariableType, EGPD_Output);
			}

			FGraphNodeCreator<UK2Node_VariableSet> SetCreator(*Graph);
			UK2Node_VariableSet* SetNode = SetCreator.CreateNode(false);
			SetNode->VariableReference.SetSelfMember(FName(*InProperty.TwoWayProperty));
			SetCreator.Finalize();

			FGraphNodeCreator<UK2Node_FunctionResult> ResultCreator(*Graph);
			UK2Node_FunctionResult* Result = ResultCreator.CreateNode(false);
			Result->FunctionReference.SetSelfMember(Graph->GetFName());
			ResultCreator.Finalize();

			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			UEdGraphPin* ValuePin = SetNode->FindPin(FName(*InProperty.TwoWayProperty), EGPD_Input);
			if (ValuePin == nullptr)
			{
				// Same pre-skeleton story as the getter: a variable added since the last compile
				// resolves no pin yet, so make the one the compiler will match by name and type.
				ValuePin = SetNode->CreatePin(EGPD_Input, VariableType, FName(*InProperty.TwoWayProperty));
			}
			UEdGraphPin* EntryThen = Entry->FindPin(UEdGraphSchema_K2::PN_Then);
			UEdGraphPin* SetExecute = SetNode->FindPin(UEdGraphSchema_K2::PN_Execute);
			UEdGraphPin* SetThen = SetNode->FindPin(UEdGraphSchema_K2::PN_Then);
			UEdGraphPin* ResultExecute = Result->FindPin(UEdGraphSchema_K2::PN_Execute);
			const bool bWired = NewValuePin != nullptr && ValuePin != nullptr
				&& Schema->TryCreateConnection(NewValuePin, ValuePin)
				&& EntryThen != nullptr && SetExecute != nullptr && Schema->TryCreateConnection(EntryThen, SetExecute)
				&& SetThen != nullptr && ResultExecute != nullptr && Schema->TryCreateConnection(SetThen, ResultExecute);
			if (!bWired)
			{
				InDiagnostics.AddError(EDreamUIDiagnosticCode::BindingExpressionUnsupported, InProperty.Location,
					FString::Printf(TEXT("internal: the reverse route for '<-> %s' would not wire"), *InProperty.TwoWayProperty));
				FBlueprintEditorUtils::RemoveGraph(InBlueprint, Graph);
				return;
			}
			// A setter has a side effect by definition; private, never pure.
			Entry->AddExtraFlags(FUNC_Private);
		}

		InProperty.BindingFunction = GetterName;

		// The synthesized route, appended to the SAME property list this binding came from, so a
		// `<->` inside a component block resolves its changed event on that same behaviour. Every
		// value control broadcasts through one conventional name; a destination without it fails
		// the builder's own EventNotFound with the file and line, which is the honest answer.
		FDreamUIProperty& Route = OutSynthesized.AddDefaulted_GetRef();
		Route.Name = TEXT("OnValueChangedBP");
		Route.EventHandler = SetterName;
		Route.Location = InProperty.Location;
	}

	/** "Picked(Number Index, Text Label)" -- an `events` entry as its author wrote it, for messages. */
	FString DescribeEvent(const FDreamUIEventDecl& InEvent)
	{
		TArray<FString> Parameters;
		for (const FDreamUIEventParam& Parameter : InEvent.Params)
		{
			Parameters.Add(Parameter.EnumPath.IsEmpty()
				? FString::Printf(TEXT("%s %s"), *Parameter.TypeName, *Parameter.Name)
				: FString::Printf(TEXT("%s %s %s"), *Parameter.TypeName, *Parameter.EnumPath, *Parameter.Name));
		}
		return FString::Printf(TEXT("%s(%s)"), *InEvent.Name, *FString::Join(Parameters, TEXT(", ")));
	}

	/**
	 * The first half of an emit route: check it against the file's `events` block, name its handler, and hand the
	 * rest to GenerateEmitHandlers through GEmitRoutes.
	 *
	 * Named here, before the builder, because the name is all the builder needs -- with EventHandler set the route
	 * is an ordinary one and is recorded like any other -- and the body cannot be made here at all: it has to take
	 * the source event's parameters, and only the builder knows whose event that is.
	 *
	 * A refusal leaves EventHandler EMPTY with EmitEvent still set, the same shape a refused `<-` leaves behind (an
	 * expression and no function name): the builder skips such a route without a word, because the word has been
	 * said here, and clearing EmitEvent too would send the line down the literal path to complain about a value
	 * that was never the author's mistake.
	 */
	void LowerEmit(const FString& InOwnerName, FDreamUIProperty& InProperty, FDreamUIDiagnosticBag& InDiagnostics,
		TSet<FString>& InOutClaimedNames)
	{
		if (GEmitRoutes == nullptr || !InProperty.EventHandler.IsEmpty())
		{
			return;
		}

		// This file's events only, never an import's: a dispatcher is a member of the class this file compiles into,
		// so `emit` raising another file's event would be raising something this class does not have.
		const FDreamUIEventDecl* Event = GLoweringAst != nullptr ? GLoweringAst->FindEvent(InProperty.EmitEvent) : nullptr;
		if (Event == nullptr)
		{
			TArray<FString> Declared;
			if (GLoweringAst != nullptr)
			{
				for (const FDreamUIEventDecl& Candidate : GLoweringAst->Events)
				{
					Declared.Add(Candidate.Name);
				}
			}
			InDiagnostics.AddError(EDreamUIDiagnosticCode::EmitUnknownEvent, InProperty.Location, Declared.Num() > 0
				? FString::Printf(TEXT("'emit %s' names no entry of this file's events block, which declares %s"),
					*InProperty.EmitEvent, *FString::Join(Declared, TEXT(", ")))
				: FString::Printf(TEXT("'emit %s' names no event: this file has no events block, and an emit raises one of its own file's events"),
					*InProperty.EmitEvent));
			return;
		}
		if (GRefusedEvents != nullptr && GRefusedEvents->Contains(Event->Name))
		{
			// Declared, and refused by the compiler with the reason on the declaration's own line. A second error
			// here would only say that line's mistake again from further away.
			return;
		}
		if (InProperty.EmitArguments.Num() != Event->Params.Num())
		{
			InDiagnostics.AddError(EDreamUIDiagnosticCode::EmitArgumentMismatch, InProperty.Location, FString::Printf(
				TEXT("'emit %s' passes %d argument(s), and the event is declared %s, which takes %d"),
				*Event->Name, InProperty.EmitArguments.Num(), *DescribeEvent(*Event), Event->Params.Num()));
			return;
		}

		const FString HandlerName = MakeThunkName(DreamUIExpressionThunks::GeneratedEmitPrefix, InOwnerName, InProperty.Name, InOutClaimedNames);
		InProperty.EventHandler = HandlerName;

		DreamUIExpressionThunks::FEmitRoute& Route = GEmitRoutes->AddDefaulted_GetRef();
		Route.HandlerName = HandlerName;
		Route.EventName = Event->Name;
		Route.Arguments = InProperty.EmitArguments;
		Route.Location = InProperty.Location;
	}

	/**
	 * Lower every `<-` expression and every `<->` in one property list, under one owner name.
	 *
	 * The owner name is whatever the thunk names itself after: a node id for a node's own lines, and
	 * `Style_<name>` for a style block's. It is a parameter rather than the node because a style is
	 * NOT a node -- it is one declaration several nodes wear -- and lowering it once per wearer would
	 * claim one graph name several times over.
	 */
	void LowerPropertyList(UDreamWidgetBlueprint* InBlueprint, const FString& InOwnerName,
		TArray<FDreamUIProperty>& InOutProperties, FDreamUIDiagnosticBag& InDiagnostics,
		TSet<FString>& InOutClaimedNames)
	{
		TArray<FDreamUIProperty> Synthesized;
		for (FDreamUIProperty& Property : InOutProperties)
		{
			if (!Property.EmitEvent.IsEmpty())
			{
				LowerEmit(InOwnerName, Property, InDiagnostics, InOutClaimedNames);
			}
			else if (Property.BindingExpression.IsSet())
			{
				LowerProperty(InBlueprint, InOwnerName, Property, InDiagnostics, InOutClaimedNames);
			}
			else if (!Property.TwoWayProperty.IsEmpty())
			{
				LowerTwoWay(InBlueprint, InOwnerName, Property, Synthesized, InDiagnostics, InOutClaimedNames);
			}
		}
		InOutProperties.Append(MoveTemp(Synthesized));
	}

	/**
	 * Every `-> emit` inside a `for` or `each` body, said out loud.
	 *
	 * Not lowered, for the reason the body's `<-` lines are not: its widgets are copies the loop makes, and a handler
	 * on the class has no way to say which copy fired or to read the item it was made for -- the arguments an author
	 * writes there (`emit Chosen(Item.Index)`) name the loop's variable, which the class does not have. Left alone
	 * it would be skipped in silence, which is the one outcome a route must never have.
	 */
	void ReportEmitsInLoopBody(const FDreamUINode& InNode, FDreamUIDiagnosticBag& InDiagnostics)
	{
		auto ReportList = [&InDiagnostics](const TArray<FDreamUIProperty>& InProperties)
		{
			for (const FDreamUIProperty& Property : InProperties)
			{
				if (!Property.EmitEvent.IsEmpty())
				{
					InDiagnostics.AddError(EDreamUIDiagnosticCode::EmitRouteUnsupported, Property.Location, FString::Printf(
						TEXT("'%s -> emit %s' is inside a for or each body, whose widgets are the loop's copies -- route the event to a function of the class and raise '%s' from there"),
						*Property.Name, *Property.EmitEvent, *Property.EmitEvent));
				}
			}
		};
		ReportList(InNode.Properties);
		ReportList(InNode.SlotProperties);
		for (const FDreamUIComponent& Component : InNode.Components)
		{
			ReportList(Component.Properties);
		}
		for (const FDreamUINode& Child : InNode.Children)
		{
			ReportEmitsInLoopBody(Child, InDiagnostics);
		}
	}

	void WalkNode(UDreamWidgetBlueprint* InBlueprint, FDreamUINode& InNode, FDreamUIDiagnosticBag& InDiagnostics,
		TSet<FString>& InOutClaimedNames)
	{
		auto LowerAll = [InBlueprint, &InNode, &InDiagnostics, &InOutClaimedNames](TArray<FDreamUIProperty>& InProperties)
		{
			LowerPropertyList(InBlueprint, InNode.Id, InProperties, InDiagnostics, InOutClaimedNames);
		};
		LowerAll(InNode.Properties);
		LowerAll(InNode.SlotProperties);
		for (FDreamUIComponent& Component : InNode.Components)
		{
			LowerAll(Component.Properties);
		}
		for (FDreamUINode& Child : InNode.Children)
		{
			// A loop body's bindings are not this pass's to lower: `Item.Member` reads belong to
			// the each machinery, applied per cell, and a thunk for one would ask the CLASS for a
			// variable only the iteration has.
			if (Child.Kind == EDreamUINodeKind::EachLoop || Child.Kind == EDreamUINodeKind::ForLoop)
			{
				if (GEmitRoutes != nullptr)
				{
					ReportEmitsInLoopBody(Child, InDiagnostics);
				}
				continue;
			}
			WalkNode(InBlueprint, Child, InDiagnostics, InOutClaimedNames);
		}
	}

	/** The style declaration InName resolves to, mutably, with FDreamUIAst::FindStyle's precedence. */
	FDreamUIStyle* FindMutableStyle(FDreamUIAst& InAst, const FString& InName)
	{
		for (FDreamUIStyle& Style : InAst.Styles)
		{
			if (Style.Name == InName)
			{
				return &Style;
			}
		}
		for (FDreamUIStyle& Style : InAst.ImportedStyles)
		{
			if (Style.Name == InName)
			{
				return &Style;
			}
		}
		return nullptr;
	}

	/**
	 * The half of this pass that was missing: the expressions written inside `style` blocks.
	 *
	 * A style property is a property like any other, and `<-` in one was the single place in the
	 * language where a binding could be written and then simply not exist. The walk above only ever
	 * saw nodes, so a style's expression reached the builder un-lowered, the builder's empty-name
	 * guard skipped it without a word, and the file compiled green with the property never driven --
	 * while a bare `<- F()` in the same block worked, because the parser fills BindingFunction for
	 * that shape at parse time. Sharing the bindings of a whole family of controls is what a style is
	 * FOR, so the silence was exactly where it was least affordable.
	 *
	 * One thunk per style property, not per wearer, and the name says `Style_<name>` for that reason:
	 * the graph belongs to the class, every node wearing the style binds to the same function, and
	 * lowering per wearer would claim one name several times and leave displaced graphs behind.
	 *
	 * Only styles a node actually WEARS. A declared-but-unworn style applies to nothing, so lowering
	 * it would add a graph nothing calls and could fail a compile over a line that changes no tree.
	 * The base chain is followed because the builder applies it (`style Danger : Button` runs
	 * Button's lines first), and the Lowered set is also what stops the cycle the builder reports at
	 * the wearing node.
	 */
	void LowerWornStyles(UDreamWidgetBlueprint* InBlueprint, FDreamUIAst& InAst,
		FDreamUIDiagnosticBag& InDiagnostics, TSet<FString>& InOutClaimedNames)
	{
		TArray<FString> Worn;
		InAst.ForEachNode([&Worn](const FDreamUINode& InNode)
		{
			if (!InNode.StyleName.IsEmpty())
			{
				Worn.AddUnique(InNode.StyleName);
			}
		});

		TSet<const FDreamUIStyle*> Lowered;
		for (const FString& WornName : Worn)
		{
			FString Link = WornName;
			while (!Link.IsEmpty())
			{
				FDreamUIStyle* Style = FindMutableStyle(InAst, Link);
				if (Style == nullptr || Lowered.Contains(Style))
				{
					break;
				}
				Lowered.Add(Style);
				const FString OwnerName = FString(TEXT("Style_")) + Style->Name;
				LowerPropertyList(InBlueprint, OwnerName, Style->Properties, InDiagnostics, InOutClaimedNames);
				// A style carries components and `@slot` lines too, and a line in either is a line like any other: an
				// expression there left un-lowered is the silent binding this pass was extended to styles to end. One
				// owner name for all three lists; a component's `Spacing` meeting the style's own `Spacing` is exactly
				// the collision MakeThunkName disambiguates.
				for (FDreamUIComponent& Component : Style->Components)
				{
					LowerPropertyList(InBlueprint, OwnerName, Component.Properties, InDiagnostics, InOutClaimedNames);
				}
				LowerPropertyList(InBlueprint, OwnerName, Style->SlotProperties, InDiagnostics, InOutClaimedNames);
				Link = Style->BaseName;
			}
		}
	}
}

namespace DreamUIExpressionThunksLocal
{
	/** What an emit handler has to take: the source event's own signature, or the one value a struct event fires with. */
	struct FSourceSignature
	{
		/** A multicast delegate's signature. The handler's parameters are copied from it whole, reference flags and all. */
		const UFunction* SignatureFunction = nullptr;
		/** An FDreamUIEventDelegate's value: nothing for one that fires empty, one parameter named Value otherwise. */
		TArray<TPair<FName, FEdGraphPinType>> Parameters;
	};

	/**
	 * The pin an FDreamUIEventDelegate's parameter type is, or false for the widths no Blueprint function can take.
	 *
	 * The inverse of UDreamUIEventDelegateParameterHelper::IsPropertyCompatible, which is what judges the handler
	 * when the route is checked and again when it fires: a pin made here becomes a parameter that function answers
	 * with the same type, which is the whole of what "fits" means for this kind of event.
	 */
	bool MakeStructEventPinType(const EDreamUIEventDelegateParameterType InType, FEdGraphPinType& OutPinType)
	{
		OutPinType = FEdGraphPinType();
		auto AsStruct = [&OutPinType](UScriptStruct* InStruct)
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = InStruct;
			return true;
		};
		auto AsObject = [&OutPinType](const FName InCategory, UClass* InClass)
		{
			OutPinType.PinCategory = InCategory;
			OutPinType.PinSubCategoryObject = InClass;
			return true;
		};
		switch (InType)
		{
		case EDreamUIEventDelegateParameterType::Bool: OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean; return true;
		case EDreamUIEventDelegateParameterType::Float:
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
			return true;
		case EDreamUIEventDelegateParameterType::Double:
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			return true;
		case EDreamUIEventDelegateParameterType::UInt8: OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte; return true;
		case EDreamUIEventDelegateParameterType::Int32: OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int; return true;
		case EDreamUIEventDelegateParameterType::Int64: OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64; return true;
		case EDreamUIEventDelegateParameterType::String: OutPinType.PinCategory = UEdGraphSchema_K2::PC_String; return true;
		case EDreamUIEventDelegateParameterType::Name: OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name; return true;
		case EDreamUIEventDelegateParameterType::Text: OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text; return true;
		case EDreamUIEventDelegateParameterType::Vector2: return AsStruct(TBaseStructure<FVector2D>::Get());
		case EDreamUIEventDelegateParameterType::Vector3: return AsStruct(TBaseStructure<FVector>::Get());
		case EDreamUIEventDelegateParameterType::Vector4: return AsStruct(TBaseStructure<FVector4>::Get());
		case EDreamUIEventDelegateParameterType::Color: return AsStruct(TBaseStructure<FColor>::Get());
		case EDreamUIEventDelegateParameterType::LinearColor: return AsStruct(TBaseStructure<FLinearColor>::Get());
		case EDreamUIEventDelegateParameterType::Quaternion: return AsStruct(TBaseStructure<FQuat>::Get());
		case EDreamUIEventDelegateParameterType::Rotator: return AsStruct(TBaseStructure<FRotator>::Get());
		case EDreamUIEventDelegateParameterType::Asset: return AsObject(UEdGraphSchema_K2::PC_Object, UObject::StaticClass());
		case EDreamUIEventDelegateParameterType::DreamWidget: return AsObject(UEdGraphSchema_K2::PC_Object, UDreamWidget::StaticClass());
		case EDreamUIEventDelegateParameterType::PointerEvent: return AsObject(UEdGraphSchema_K2::PC_Object, UDreamPointerEventData::StaticClass());
		case EDreamUIEventDelegateParameterType::Class: return AsObject(UEdGraphSchema_K2::PC_Class, UObject::StaticClass());
		default:
			// Int8, the unsigned widths past a byte, and a generic USTRUCT whose type the event does not say: no
			// Blueprint pin is any of them.
			return false;
		}
	}

	/**
	 * Find the event a recorded route leaves from, on the tree the builder just made, and say what its handler takes.
	 *
	 * Through ResolveDreamWidgetBindingTarget, the resolver the compiler checks routes with and the run time binds
	 * them through: the handler is built against the very object it will be attached to.
	 */
	bool ResolveSourceSignature(const FDreamWidgetEventBinding& InBinding, UDreamWidgetTree* InTree, FSourceSignature& OutSignature,
		FString& OutRefusal)
	{
		const UDreamWidget* Widget = InTree != nullptr ? InTree->FindWidgetByVariableName(InBinding.WidgetName) : nullptr;
		const UObject* Target = Widget != nullptr
			? ResolveDreamWidgetBindingTarget(Widget, InBinding.Target, InBinding.BehaviourIndex) : nullptr;
		const FProperty* EventProperty = Target != nullptr ? Target->GetClass()->FindPropertyByName(InBinding.EventName) : nullptr;

		if (const FMulticastDelegateProperty* Event = CastField<FMulticastDelegateProperty>(EventProperty))
		{
			if (Event->SignatureFunction == nullptr)
			{
				OutRefusal = FString::Printf(TEXT("'%s' declares no signature for a handler to copy"), *InBinding.EventName.ToString());
				return false;
			}
			OutSignature.SignatureFunction = Event->SignatureFunction;
			return true;
		}

		const FStructProperty* StructEvent = CastField<FStructProperty>(EventProperty);
		if (StructEvent != nullptr && StructEvent->Struct == FDreamUIEventDelegate::StaticStruct())
		{
			const FDreamUIEventDelegate* EventValue = StructEvent->ContainerPtrToValuePtr<FDreamUIEventDelegate>(Target);
			const EDreamUIEventDelegateParameterType ParameterType = EventValue != nullptr
				? EventValue->GetNativeParameterType() : EDreamUIEventDelegateParameterType::None;
			if (ParameterType == EDreamUIEventDelegateParameterType::Empty)
			{
				return true;
			}
			FEdGraphPinType PinType;
			if (!MakeStructEventPinType(ParameterType, PinType))
			{
				OutRefusal = FString::Printf(TEXT("'%s' fires with a %s, which no Blueprint function can take as a parameter"),
					*InBinding.EventName.ToString(), *UDreamUIEventDelegateParameterHelper::ParameterTypeToName(ParameterType));
				return false;
			}
			OutSignature.Parameters.Emplace(FName(TEXT("Value")), PinType);
			return true;
		}

		OutRefusal = FString::Printf(TEXT("internal: the route's event '%s' could not be found again on the hierarchy the file built"),
			*InBinding.EventName.ToString());
		return false;
	}

	/**
	 * `Cycle` handed to an enum parameter: a bare word the expression grammar can only read as a variable, meant as
	 * the enumerator. Taken as one when it names no parameter and no variable, and the enum has it -- the same order
	 * of meanings the word would have anywhere else, with the enumerator as the last resort rather than the first.
	 */
	bool TryEmitEnumeratorLiteral(const FDreamUIExpression& InArgument, const UEdGraphPin* InTarget, const FThunkContext& InContext,
		FEmitted& OutLiteral)
	{
		if (InArgument.Kind != FDreamUIExpression::EKind::VariableRef || InTarget == nullptr)
		{
			return false;
		}
		const UEnum* Enum = Cast<UEnum>(InTarget->PinType.PinSubCategoryObject.Get());
		if (Enum == nullptr
			|| (InTarget->PinType.PinCategory != UEdGraphSchema_K2::PC_Byte && InTarget->PinType.PinCategory != UEdGraphSchema_K2::PC_Enum))
		{
			return false;
		}
		FEdGraphPinType Unused;
		if (InContext.LocalPins.Contains(InArgument.Symbol) || FindVariablePinType(InContext.Blueprint, InArgument.Symbol, Unused))
		{
			return false;
		}
		const int32 Index = Enum->GetIndexByNameString(InArgument.Symbol);
		if (Index == INDEX_NONE)
		{
			return false;
		}
		OutLiteral = FEmitted();
		OutLiteral.LiteralDefault = Enum->GetNameStringByIndex(Index);
		OutLiteral.PinType = InTarget->PinType;
		return true;
	}

	/** Whether two pin types are the same declared type, for retyping a pin an old signature grew. */
	bool IsSameDeclaredType(const FEdGraphPinType& InA, const FEdGraphPinType& InB)
	{
		return InA.PinCategory == InB.PinCategory
			&& InA.PinSubCategoryObject == InB.PinSubCategoryObject
			&& InA.ContainerType == InB.ContainerType;
	}

	/**
	 * The body of one emit handler: a function taking what the source event sends, evaluating the arguments and
	 * calling the dispatcher -- "Call Picked", exactly the node an author would drag out of My Blueprint.
	 */
	bool BuildEmitHandler(UDreamWidgetBlueprint* InBlueprint, const DreamUIExpressionThunks::FEmitRoute& InRoute,
		const FSourceSignature& InSignature, FDreamUIDiagnosticBag& InDiagnostics)
	{
		const FDreamUIEventDecl* Event = GLoweringAst != nullptr ? GLoweringAst->FindEvent(InRoute.EventName) : nullptr;
		if (Event == nullptr || Event->Params.Num() != InRoute.Arguments.Num())
		{
			// Both were checked when the route was named; the AST is the one both halves read, so this is not a state
			// a file can reach.
			InDiagnostics.AddError(EDreamUIDiagnosticCode::EmitRouteUnsupported, InRoute.Location, FString::Printf(
				TEXT("internal: 'emit %s' no longer matches the events block it was checked against"), *InRoute.EventName));
			return false;
		}

		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(InBlueprint, FName(*InRoute.HandlerName), UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		InBlueprint->FunctionGraphs.Add(Graph);

		// The entry BARE, its reference set only after Finalize. BeginThunkGraph sets it first, and a thunk can live
		// with what follows from that -- Finalize resolves the reference against the skeleton, finds last compile's
		// function of this name and grows its parameters -- because a thunk's one parameter never changes. A
		// handler's parameters are the source event's, and an author moving the route to another event would get
		// the old event's parameters AND the new one's: a signature no event has, refused as a mismatch on a line
		// that is right. With no reference to resolve, the node grows nothing but its exec pin.
		FGraphNodeCreator<UK2Node_FunctionEntry> EntryCreator(*Graph);
		UK2Node_FunctionEntry* Entry = EntryCreator.CreateNode(/*bSelectNewNode*/false);
		EntryCreator.Finalize();
		Entry->FunctionReference.SetSelfMember(Graph->GetFName());
		if (InSignature.SignatureFunction != nullptr)
		{
			// The schema's own copy of a signature, the one "create a matching function" uses: a `const FText&`
			// parameter arrives by reference, and IsSignatureCompatibleWith compares those flags.
			Entry->CreateUserDefinedPinsForFunctionEntryExit(InSignature.SignatureFunction, /*bForFunctionEntry*/ true);
		}
		for (const TPair<FName, FEdGraphPinType>& Parameter : InSignature.Parameters)
		{
			Entry->CreateUserDefinedPin(Parameter.Key, Parameter.Value, EGPD_Output);
		}

		FThunkContext Context;
		Context.Blueprint = InBlueprint;
		Context.Diagnostics = &InDiagnostics;
		Context.Ast = GLoweringAst;
		Context.Graph = Graph;
		Context.LastExecPin = Entry->FindPin(UEdGraphSchema_K2::PN_Then);
		for (UEdGraphPin* Pin : Entry->Pins)
		{
			if (Pin != nullptr && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				Context.LocalPins.Add(Pin->PinName.ToString(), Pin);
			}
		}

		// The dispatcher, by self reference and by name. On the first compile that declares it the skeleton this
		// resolves against does not have it yet, so the node comes without its parameter pins and they are made here
		// by name and type, which is all the compile matches them on -- the getter nodes of the expressions above
		// rely on the same thing.
		FGraphNodeCreator<UK2Node_CallDelegate> CallCreator(*Graph);
		UK2Node_CallDelegate* Call = CallCreator.CreateNode(/*bSelectNewNode*/false);
		Call->DelegateReference.SetSelfMember(FName(*Event->Name));
		CallCreator.Finalize();

		TArray<UEdGraphPin*> ParameterPins;
		for (const FDreamUIEventParam& Parameter : Event->Params)
		{
			FEdGraphPinType DeclaredType;
			FString Reason;
			const bool bDeclared = DreamUIExpressionThunks::MakeDeclaredPinType(Parameter.TypeName, Parameter.EnumPath, DeclaredType, Reason);
			UEdGraphPin* Pin = Call->FindPin(FName(*Parameter.Name), EGPD_Input);
			if (Pin != nullptr)
			{
				// Grown from last compile's signature, which may say another type if the file changed it since. A
				// parent's dispatcher the file reuses is left as it is: it was accepted for having these types.
				if (bDeclared && !IsSameDeclaredType(Pin->PinType, DeclaredType)
					&& !(Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real && DeclaredType.PinCategory == UEdGraphSchema_K2::PC_Real))
				{
					Pin->PinType = DeclaredType;
				}
			}
			else if (bDeclared)
			{
				Pin = Call->CreatePin(EGPD_Input, DeclaredType, FName(*Parameter.Name));
			}
			if (Pin == nullptr)
			{
				Context.Fail(Parameter.Location, FString::Printf(TEXT("internal: '%s' has no pin for parameter '%s': %s"),
					*Event->Name, *Parameter.Name, *Reason));
				break;
			}
			ParameterPins.Add(Pin);
		}
		// And nothing else: a parameter the file has since removed is a pin the old signature grew, and an input the
		// call no longer has.
		for (int32 PinIndex = Call->Pins.Num() - 1; PinIndex >= 0 && !Context.bFailed; --PinIndex)
		{
			UEdGraphPin* Pin = Call->Pins[PinIndex];
			if (Pin != nullptr && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
				&& Pin->PinName != UEdGraphSchema_K2::PN_Self && !ParameterPins.Contains(Pin))
			{
				Call->RemovePin(Pin);
			}
		}

		for (int32 Index = 0; Index < InRoute.Arguments.Num() && !Context.bFailed; ++Index)
		{
			const FDreamUIExpression& Argument = InRoute.Arguments[Index];
			UEdGraphPin* ParameterPin = ParameterPins[Index];
			FEmitted Value;
			if (!TryEmitEnumeratorLiteral(Argument, ParameterPin, Context, Value))
			{
				Value = EmitExpression(Argument, Context);
				if (Context.bFailed)
				{
					break;
				}
			}
			// The one step that is the ROUTE's mistake rather than the expression's: the expression made a value, and
			// it is not the type the event declares for this parameter.
			Context.FailCode = EDreamUIDiagnosticCode::EmitArgumentMismatch;
			Context.FailPrefix = FString::Printf(TEXT("argument %d of 'emit %s' is its '%s', declared %s, and "),
				Index + 1, *Event->Name, *Event->Params[Index].Name, *Event->Params[Index].TypeName);
			ConnectOrDefault(ParameterPin, Value, Context, Argument.Location);
			Context.FailCode = EDreamUIDiagnosticCode::BindingExpressionUnsupported;
			Context.FailPrefix.Reset();
		}

		if (!Context.bFailed)
		{
			// After the arguments, so a call an argument makes has run before the broadcast reads its value -- the
			// order EmitCall keeps for the same reason.
			UEdGraphPin* Execute = Call->FindPin(UEdGraphSchema_K2::PN_Execute);
			if (Execute == nullptr || Context.LastExecPin == nullptr
				|| !GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Context.LastExecPin, Execute))
			{
				Context.Fail(InRoute.Location, FString::Printf(TEXT("internal: the call to '%s' would not wire into its handler"), *Event->Name));
			}
		}
		if (Context.bFailed)
		{
			FBlueprintEditorUtils::RemoveGraph(InBlueprint, Graph);
			return false;
		}

		// Private like every function this pass makes, and never pure: a broadcast is the side effect it exists for.
		Entry->AddExtraFlags(FUNC_Private);
		return true;
	}
}

bool DreamUIExpressionThunks::MakeDeclaredPinType(const FString& InTypeName, const FString& InEnumPath, FEdGraphPinType& OutPinType,
	FString& OutReason)
{
	OutPinType = FEdGraphPinType();
	const FString Type = InTypeName.TrimStartAndEnd();
	auto Is = [&Type](const TCHAR* InName) { return Type.Equals(InName, ESearchCase::IgnoreCase); };

	if (Is(TEXT("Text")))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		return true;
	}
	if (Is(TEXT("String")))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
		return true;
	}
	if (Is(TEXT("Number")))
	{
		// A double, which is what a Blueprint "Float" variable has been since reals: the language's Number is the
		// type an author gets by declaring a number in the Blueprint editor, and binds float targets by conversion.
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		return true;
	}
	if (Is(TEXT("Integer")))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		return true;
	}
	if (Is(TEXT("Bool")))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		return true;
	}
	if (Is(TEXT("Color")))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
		return true;
	}
	if (Is(TEXT("Vector2")))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
		return true;
	}
	if (Is(TEXT("Asset")))
	{
		// A HARD reference, unlike a `resources` Asset, and on purpose. A resource is a constant the Class Defaults
		// panel edits, where the asset picker of a soft pin is the point. A prop is a value a host hands in and this
		// file binds onto its widgets, every one of which holds what it draws by hard pointer -- so a soft prop would
		// put a load into every binding that reads it, and the builder already loads the asset a host's
		// `Icon = /Game/T_Icon` names, as it does for any object-typed property.
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		OutPinType.PinSubCategoryObject = UObject::StaticClass();
		return true;
	}
	if (Is(TEXT("Class")))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class;
		OutPinType.PinSubCategoryObject = UObject::StaticClass();
		return true;
	}
	if (Is(TEXT("Enum")))
	{
		const FString EnumPath = InEnumPath.TrimStartAndEnd();
		if (EnumPath.IsEmpty())
		{
			OutReason = TEXT("'Enum' is followed by the enum's path, as in 'Enum /Script/MyGame.EMyKind Kind'");
			return false;
		}
		UEnum* Enum = FindObject<UEnum>(nullptr, *EnumPath);
		if (Enum == nullptr)
		{
			Enum = LoadObject<UEnum>(nullptr, *EnumPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		if (Enum == nullptr)
		{
			OutReason = FString::Printf(TEXT("there is no enum at '%s'"), *EnumPath);
			return false;
		}
		if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Enum))
		{
			OutReason = FString::Printf(TEXT("'%s' is not a BlueprintType enum, so no Blueprint variable can hold one"), *EnumPath);
			return false;
		}
		// The byte pin of the enum, which is what a Blueprint variable of an enum type is; the compiler makes an
		// FEnumProperty of it for an `enum class` and an FByteProperty otherwise, exactly as for a hand-made one.
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		OutPinType.PinSubCategoryObject = Enum;
		return true;
	}
	OutReason = FString::Printf(
		TEXT("'%s' is not a type a prop or an event parameter can have -- Text, String, Number, Integer, Bool, Color, Vector2, Asset, Class or Enum <path>"),
		*Type);
	return false;
}

void DreamUIExpressionThunks::GenerateEmitHandlers(UDreamWidgetBlueprint* InBlueprint, const FDreamUIAst& InAst,
	const TArray<FEmitRoute>& InRoutes, UDreamWidgetTree* InTree, TArray<FDreamWidgetEventBinding>& InOutEventBindings,
	FDreamUIDiagnosticBag& InDiagnostics)
{
	using namespace DreamUIExpressionThunksLocal;

	if (InBlueprint == nullptr || InRoutes.Num() == 0)
	{
		return;
	}
	TGuardValue<const FDreamUIAst*> LoweringAstGuard(GLoweringAst, &InAst);

	auto DropRoutes = [&InOutEventBindings](const FName InHandlerName)
	{
		InOutEventBindings.RemoveAll([InHandlerName](const FDreamWidgetEventBinding& InBinding)
		{
			return InBinding.FunctionName == InHandlerName;
		});
	};

	for (const FEmitRoute& Route : InRoutes)
	{
		const FName HandlerName(*Route.HandlerName);
		// The first route recorded under this handler. There is more than one when the line sits in a style several
		// nodes wear; each wearer's event is checked against the handler by the compile, as any route is, so a style
		// worn by two kinds of control that fire different signatures is said there, on the route that does not fit.
		const FDreamWidgetEventBinding* Binding = InOutEventBindings.FindByPredicate([HandlerName](const FDreamWidgetEventBinding& InBinding)
		{
			return InBinding.FunctionName == HandlerName;
		});
		if (Binding == nullptr)
		{
			// The builder recorded no route, and said why on this line (no such event, nothing to carry it): there is
			// nothing to give a body to.
			continue;
		}

		FSourceSignature Signature;
		FString Refusal;
		if (!ResolveSourceSignature(*Binding, InTree, Signature, Refusal))
		{
			InDiagnostics.AddError(EDreamUIDiagnosticCode::EmitRouteUnsupported, Route.Location, FString::Printf(
				TEXT("'emit %s' needs a handler that takes what its event sends, and %s"), *Route.EventName, *Refusal));
			DropRoutes(HandlerName);
			continue;
		}
		if (!BuildEmitHandler(InBlueprint, Route, Signature, InDiagnostics))
		{
			DropRoutes(HandlerName);
		}
	}
}

void DreamUIExpressionThunks::Generate(UDreamWidgetBlueprint* InBlueprint, FDreamUIAst& InAst, FDreamUIDiagnosticBag& InDiagnostics,
	TArray<FEmitRoute>* OutEmitRoutes, const TSet<FString>* InRefusedEvents)
{
	using namespace DreamUIExpressionThunksLocal;

	// Regenerate-each-compile, the same contract GeneratedVariables live under: drop every graph
	// this pass ever made, then rebuild the ones the file still asks for. A copy of the array,
	// because RemoveGraph edits it under the loop.
	TArray<TObjectPtr<UEdGraph>> Graphs = InBlueprint->FunctionGraphs;
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph == nullptr)
		{
			continue;
		}
		if (IsGeneratedThunkGraph(Graph))
		{
			FBlueprintEditorUtils::RemoveGraph(InBlueprint, Graph);
		}
	}

	if (InAst.bHasRoot)
	{
		// Fresh per compile, like the graphs themselves. Its only job is that no two properties in
		// ONE file claim one graph name; nothing about it has to survive to the next compile.
		TSet<FString> ClaimedNames;
		// The AST, for the whole of the lowering: `@Name` inside an expression resolves against the
		// resources block of the file the expression was written in, and the guard is what scopes it.
		TGuardValue<const FDreamUIAst*> LoweringAstGuard(GLoweringAst, &InAst);
		// And where the emit routes go, for the same span; see GEmitRoutes.
		TGuardValue<TArray<FEmitRoute>*> EmitRoutesGuard(GEmitRoutes, OutEmitRoutes);
		TGuardValue<const TSet<FString>*> RefusedEventsGuard(GRefusedEvents, InRefusedEvents);
		// Styles first, and sharing the node walk's claim set: a style thunk is named after the
		// style, so the only way it can meet a node's name is a node literally called Style_<name>,
		// and one set across both passes is what makes MakeThunkName disambiguate that instead of
		// letting CreateNewGraph displace the first graph silently.
		LowerWornStyles(InBlueprint, InAst, InDiagnostics, ClaimedNames);
		WalkNode(InBlueprint, InAst.Root, InDiagnostics, ClaimedNames);
	}
}
