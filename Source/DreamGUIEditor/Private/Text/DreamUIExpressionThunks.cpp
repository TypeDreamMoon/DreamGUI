// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIExpressionThunks.h"

#include "DreamWidgetBlueprint.h"
// FindFieldId: whether the object a `<->` writes into can announce the member it wrote.
#include "Core/DreamUIBindingObserver.h"
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
#include "FieldNotification/FieldNotificationLibrary.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompilerMisc.h"

namespace DreamUIExpressionThunks
{
const TCHAR* GeneratedGraphPrefix = TEXT("__DreamBinding_");
const TCHAR* GeneratedEmitPrefix = TEXT("__DreamEmit_");
const TCHAR* GeneratedRoutePrefix = TEXT("__DreamRoute_");
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
			FailWith(FailCode, InLocation, InMessage);
		}

		/** Fail under a code of the refusal's own: a member path that does not resolve is that, whatever step was running. */
		void FailWith(const EDreamUIDiagnosticCode InCode, const FDreamUISourceLocation& InLocation, const FString& InMessage)
		{
			if (!bFailed)
			{
				Diagnostics->AddError(InCode, InLocation, FailPrefix + InMessage);
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

	/**
	 * The segments of a dotted symbol: `Player.Stats.Rank` is {Player, Stats, Rank}, a bare `Title` is {Title}. False for
	 * a spelling with an empty segment, which the parser does not make and a hand-built property might.
	 */
	bool SplitPath(const FString& InSymbol, TArray<FString>& OutSegments)
	{
		OutSegments.Reset();
		InSymbol.ParseIntoArray(OutSegments, TEXT("."), /*InCullEmpty*/false);
		for (FString& Segment : OutSegments)
		{
			Segment.TrimStartAndEndInline();
			if (Segment.IsEmpty())
			{
				return false;
			}
		}
		return OutSegments.Num() > 0;
	}

	/** `Player.Stats` -- the first InCount segments of a path, as written, for messages. */
	FString JoinPath(const TArray<FString>& InSegments, const int32 InCount)
	{
		FString Path;
		for (int32 Index = 0; Index < InCount && Index < InSegments.Num(); ++Index)
		{
			if (Index > 0)
			{
				Path += TEXT(".");
			}
			Path += InSegments[Index];
		}
		return Path;
	}

	/**
	 * The class an object pin holds, or null for every other pin -- a number, a struct, a soft reference, an array: the
	 * values a member path cannot go on through. The authoritative class, so a member reference made against it is the
	 * one the compiled class answers to rather than a skeleton's.
	 */
	UClass* GetPinObjectClass(const FEdGraphPinType& InType)
	{
		if (InType.ContainerType != EPinContainerType::None
			|| (InType.PinCategory != UEdGraphSchema_K2::PC_Object && InType.PinCategory != UEdGraphSchema_K2::PC_Interface))
		{
			return nullptr;
		}
		UClass* Class = Cast<UClass>(InType.PinSubCategoryObject.Get());
		return Class != nullptr ? Class->GetAuthoritativeClass() : nullptr;
	}

	FString DescribeClass(const UClass* InClass)
	{
		return InClass != nullptr ? InClass->GetName() : FString(TEXT("nothing"));
	}

	/**
	 * The property InName of InClass a graph can read, or null with OutWhyNot -- a sentence fragment naming the member and
	 * the class. Readable means BlueprintVisible: the hop is an external variable get, and a graph is refused one of any
	 * other property, which is the half an UnrealSharp author meets first (a [UProperty] is not BlueprintVisible until its
	 * flags say BlueprintReadOnly or BlueprintReadWrite).
	 */
	const FProperty* FindReadableMember(const UClass* InClass, const FString& InName, FString& OutWhyNot)
	{
		const FProperty* Property = FindFProperty<FProperty>(InClass, FName(*InName));
		if (Property == nullptr)
		{
			OutWhyNot = InClass->FindFunctionByName(FName(*InName)) != nullptr
				? FString::Printf(TEXT("'%s' is a function of %s, and a function is called: write '%s()'"), *InName, *DescribeClass(InClass), *InName)
				: FString::Printf(TEXT("%s has no member '%s'"), *DescribeClass(InClass), *InName);
			return nullptr;
		}
		if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
		{
			OutWhyNot = FString::Printf(
				TEXT("'%s' of %s is not BlueprintVisible, so no graph can read it -- give its UPROPERTY BlueprintReadOnly or BlueprintReadWrite (an UnrealSharp [UProperty] needs PropertyFlags.BlueprintReadOnly or PropertyFlags.BlueprintReadWrite)"),
				*InName, *DescribeClass(InClass));
			return nullptr;
		}
		return Property;
	}

	/** The function InName of InClass a graph can call, or null with OutWhyNot, the same kind of fragment. */
	UFunction* FindCallableMember(const UClass* InClass, const FString& InName, FString& OutWhyNot)
	{
		UFunction* Function = InClass->FindFunctionByName(FName(*InName));
		if (Function == nullptr)
		{
			OutWhyNot = FindFProperty<FProperty>(InClass, FName(*InName)) != nullptr
				? FString::Printf(TEXT("'%s' is a property of %s, not a function"), *InName, *DescribeClass(InClass))
				: FString::Printf(TEXT("%s has no function '%s'"), *DescribeClass(InClass), *InName);
			return nullptr;
		}
		if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
		{
			OutWhyNot = FString::Printf(
				TEXT("'%s' of %s is neither BlueprintCallable nor BlueprintPure, so no graph can call it -- mark its UFUNCTION one of them (an UnrealSharp [UFunction] needs FunctionFlags.BlueprintCallable)"),
				*InName, *DescribeClass(InClass));
			return nullptr;
		}
		return Function;
	}

	/**
	 * A function's inputs in order -- by value, or by const reference, which UHT flags an out parameter as well -- and its
	 * return value when OutReturn is given.
	 */
	void GetInputParameters(const UFunction* InFunction, TArray<const FProperty*>& OutInputs, const FProperty** OutReturn = nullptr)
	{
		OutInputs.Reset();
		for (TFieldIterator<FProperty> It(InFunction); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				if (OutReturn != nullptr)
				{
					*OutReturn = *It;
				}
			}
			else if (!It->HasAnyPropertyFlags(CPF_OutParm) || It->HasAnyPropertyFlags(CPF_ReferenceParm))
			{
				OutInputs.Add(*It);
			}
		}
	}

	/** "(float Amount, Text Label)" -- inputs as a message lists them. */
	FString DescribeInputs(const TArray<const FProperty*>& InInputs)
	{
		TArray<FString> Parts;
		for (const FProperty* Input : InInputs)
		{
			FEdGraphPinType Type;
			const FString TypeText = GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Input, Type)
				? UEdGraphSchema_K2::TypeToText(Type).ToString() : Input->GetCPPType();
			Parts.Add(FString::Printf(TEXT("%s %s"), *TypeText, *Input->GetName()));
		}
		return FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(", ")));
	}

	/** The same, for the pins a handler's entry grew from its event. */
	FString DescribePins(const TArray<UEdGraphPin*>& InPins)
	{
		TArray<FString> Parts;
		for (const UEdGraphPin* Pin : InPins)
		{
			Parts.Add(FString::Printf(TEXT("%s %s"), *UEdGraphSchema_K2::TypeToText(Pin->PinType).ToString(), *Pin->PinName.ToString()));
		}
		return FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(", ")));
	}

	/** `->`, `+=` or `=`, as the line wrote it, for messages. */
	const TCHAR* DescribeRouteOperator(const EDreamUIRouteOperator InOperator)
	{
		switch (InOperator)
		{
		case EDreamUIRouteOperator::Append: return TEXT("+=");
		case EDreamUIRouteOperator::Assign: return TEXT("=");
		default: return TEXT("->");
		}
	}

	/** What a member path comes to by its declared types, with no node made: the lowering's look ahead of the emission. */
	struct FResolvedPath
	{
		/** The pin type the last resolved segment holds. */
		FEdGraphPinType Type;
		/** The last resolved segment's property and the class it was looked up on; null while only the root is resolved. */
		const FProperty* LastProperty = nullptr;
		UClass* LastOwnerClass = nullptr;
	};

	/**
	 * The first InCount segments of InSegments, resolved the way EmitMemberPath will emit them: segment 0 a variable of the
	 * class (a `viewmodels` entry, a prop, any variable), each later one a readable property of the object class the one
	 * before holds. False after a refusal into InDiagnostics -- MemberPathNotFound, MemberPathThroughNonObject -- that
	 * quotes the whole path and names the hop and its class.
	 */
	bool ResolvePathType(const UDreamWidgetBlueprint* InBlueprint, const TArray<FString>& InSegments, const int32 InCount,
		const FDreamUISourceLocation& InLocation, FDreamUIDiagnosticBag& InDiagnostics, FResolvedPath& OutPath)
	{
		OutPath = FResolvedPath();
		const FString FullPath = JoinPath(InSegments, InSegments.Num());
		if (InSegments.Num() == 0 || !FindVariablePinType(InBlueprint, InSegments[0], OutPath.Type))
		{
			InDiagnostics.AddError(EDreamUIDiagnosticCode::MemberPathNotFound, InLocation, FString::Printf(
				TEXT("'%s' cannot be read: '%s' is not a variable of this class (as of the previous compile) -- a path starts at a viewmodels entry, a prop or a variable of the Blueprint"),
				*FullPath, InSegments.Num() > 0 ? *InSegments[0] : TEXT("")));
			return false;
		}
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		for (int32 Index = 1; Index < InCount && Index < InSegments.Num(); ++Index)
		{
			UClass* OwnerClass = GetPinObjectClass(OutPath.Type);
			if (OwnerClass == nullptr)
			{
				InDiagnostics.AddError(EDreamUIDiagnosticCode::MemberPathThroughNonObject, InLocation, FString::Printf(
					TEXT("'%s' cannot be read: '%s' holds a %s, not an object, so there is no '%s' to read from it"),
					*FullPath, *JoinPath(InSegments, Index), *UEdGraphSchema_K2::TypeToText(OutPath.Type).ToString(), *InSegments[Index]));
				return false;
			}
			FString WhyNot;
			const FProperty* Property = FindReadableMember(OwnerClass, InSegments[Index], WhyNot);
			if (Property == nullptr)
			{
				InDiagnostics.AddError(EDreamUIDiagnosticCode::MemberPathNotFound, InLocation,
					FString::Printf(TEXT("'%s' cannot be read: %s"), *FullPath, *WhyNot));
				return false;
			}
			if (!Schema->ConvertPropertyToPinType(Property, OutPath.Type))
			{
				InDiagnostics.AddError(EDreamUIDiagnosticCode::MemberPathNotFound, InLocation, FString::Printf(
					TEXT("'%s' cannot be read: '%s' of %s is of a type no graph pin can hold"), *FullPath, *InSegments[Index], *DescribeClass(OwnerClass)));
				return false;
			}
			OutPath.LastProperty = Property;
			OutPath.LastOwnerClass = OwnerClass;
		}
		return true;
	}

	/**
	 * The function `<-> Path.Member` writes back through when the member's class offers one: a BlueprintCallable
	 * Set<Member> -- or, for a bool, the spelling without its b that every setter in the library uses (SetIsDead for
	 * bIsDead) -- taking exactly one input a value of the member's type can feed. Preferred to a raw write because it is
	 * the class's own: a view model that has to do something when the value changes (mark itself dirty, clamp) can only
	 * do it there. Null when there is none.
	 */
	UFunction* FindMemberSetter(const UClass* InOwnerClass, const FProperty* InMember)
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		FEdGraphPinType MemberType;
		if (InOwnerClass == nullptr || InMember == nullptr || !Schema->ConvertPropertyToPinType(InMember, MemberType))
		{
			return nullptr;
		}
		const FName Candidates[] = { FName(*(TEXT("Set") + InMember->GetName())), MakeDreamWidgetSetterName(InMember) };
		for (const FName Candidate : Candidates)
		{
			UFunction* Function = InOwnerClass->FindFunctionByName(Candidate);
			if (Function == nullptr || !Function->HasAnyFunctionFlags(FUNC_BlueprintCallable)
				|| Function->HasAnyFunctionFlags(FUNC_BlueprintPure | FUNC_Static))
			{
				continue;
			}
			TArray<const FProperty*> Inputs;
			GetInputParameters(Function, Inputs);
			FEdGraphPinType ParameterType;
			if (Inputs.Num() == 1 && Schema->ConvertPropertyToPinType(Inputs[0], ParameterType)
				&& Schema->ArePinTypesCompatible(MemberType, ParameterType, nullptr))
			{
				return Function;
			}
		}
		return nullptr;
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

	/**
	 * The start of a member path -- or the whole of a bare name: a parameter of the function being generated (no node to
	 * make, the entry's own pin IS the value), else a variable of the class, read by a self get.
	 */
	FEmitted EmitPathRoot(const FString& InName, const FString& InFullPath, const FDreamUISourceLocation& InLocation, FThunkContext& InContext)
	{
		if (UEdGraphPin* const* LocalPin = InContext.LocalPins.Find(InName))
		{
			FEmitted Local;
			Local.Pin = *LocalPin;
			Local.PinType = Local.Pin->PinType;
			return Local;
		}

		FEdGraphPinType VariableType;
		if (!FindVariablePinType(InContext.Blueprint, InName, VariableType))
		{
			if (InFullPath == InName)
			{
				InContext.Fail(InLocation, FString::Printf(
					TEXT("'%s' is neither a variable nor a function on this class (as of the previous compile)"), *InName));
			}
			else
			{
				InContext.FailWith(EDreamUIDiagnosticCode::MemberPathNotFound, InLocation, FString::Printf(
					TEXT("'%s' cannot be read: '%s' is not a variable of this class (as of the previous compile) -- a path starts at a viewmodels entry, a prop or a variable of the Blueprint"),
					*InFullPath, *InName));
			}
			return FEmitted();
		}
		FGraphNodeCreator<UK2Node_VariableGet> Creator(*InContext.Graph);
		UK2Node_VariableGet* Node = Creator.CreateNode(false);
		Node->VariableReference.SetSelfMember(FName(*InName));
		Creator.Finalize();

		FEmitted Result;
		Result.Pin = Node->FindPin(FName(*InName));
		if (Result.Pin == nullptr)
		{
			// A variable added since the last compile: this pass runs BEFORE the skeleton regen, so
			// the node's own allocation resolved nothing. The type is known from the Blueprint's
			// variable description, and a pin with the right name and type is all the compiler
			// matches on. A `viewmodels` entry on the class's first compile is exactly this.
			Result.Pin = Node->CreatePin(EGPD_Output, VariableType, FName(*InName));
		}
		if (Result.Pin == nullptr)
		{
			InContext.Fail(InLocation, FString::Printf(TEXT("internal: getter for '%s' grew no pin"), *InName));
			return FEmitted();
		}
		Result.PinType = Result.Pin->PinType;
		return Result;
	}

	/**
	 * One hop of a member path: InSegments[InIndex] read off the object InPrevious holds, by an external variable get
	 * whose target is that object -- "Get Health (Target = Player)", the node an author would drag off the pin.
	 *
	 * No null guard: a getter is not evaluated while an object along its dependency paths is unset (the run time checks
	 * the chain first), and a handler or a setter puts its own IsValid branch in front of what it does with the object.
	 */
	FEmitted EmitMemberHop(const FEmitted& InPrevious, const TArray<FString>& InSegments, const int32 InIndex,
		const FDreamUISourceLocation& InLocation, FThunkContext& InContext)
	{
		const FString FullPath = JoinPath(InSegments, InSegments.Num());
		UClass* OwnerClass = InPrevious.IsLiteral() ? nullptr : GetPinObjectClass(InPrevious.PinType);
		if (OwnerClass == nullptr)
		{
			InContext.FailWith(EDreamUIDiagnosticCode::MemberPathThroughNonObject, InLocation, FString::Printf(
				TEXT("'%s' cannot be read: '%s' holds a %s, not an object, so there is no '%s' to read from it"),
				*FullPath, *JoinPath(InSegments, InIndex), *UEdGraphSchema_K2::TypeToText(InPrevious.PinType).ToString(), *InSegments[InIndex]));
			return FEmitted();
		}
		FString WhyNot;
		const FProperty* Property = FindReadableMember(OwnerClass, InSegments[InIndex], WhyNot);
		if (Property == nullptr)
		{
			InContext.FailWith(EDreamUIDiagnosticCode::MemberPathNotFound, InLocation,
				FString::Printf(TEXT("'%s' cannot be read: %s"), *FullPath, *WhyNot));
			return FEmitted();
		}
		// The class that declares the property, as the reference to it is recorded against when an author drags one out:
		// an inherited member stays the base's, and so resolves on any subclass the object turns out to be.
		UClass* MemberParent = Property->GetOwnerClass() != nullptr ? Property->GetOwnerClass()->GetAuthoritativeClass() : OwnerClass;

		FGraphNodeCreator<UK2Node_VariableGet> Creator(*InContext.Graph);
		UK2Node_VariableGet* Node = Creator.CreateNode(false);
		Node->VariableReference.SetExternalMember(Property->GetFName(), MemberParent);
		Creator.Finalize();

		UEdGraphPin* ValuePin = Node->FindPin(Property->GetFName(), EGPD_Output);
		UEdGraphPin* TargetPin = Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
		if (ValuePin == nullptr || TargetPin == nullptr || !GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(InPrevious.Pin, TargetPin))
		{
			InContext.Fail(InLocation, FString::Printf(TEXT("internal: the read of '%s' in '%s' would not wire"), *InSegments[InIndex], *FullPath));
			return FEmitted();
		}
		FEmitted Result;
		Result.Pin = ValuePin;
		Result.PinType = ValuePin->PinType;
		return Result;
	}

	/** The first InCount segments of a path, emitted: the root, then one external get per hop. */
	FEmitted EmitMemberPath(const TArray<FString>& InSegments, const int32 InCount, const FDreamUISourceLocation& InLocation, FThunkContext& InContext)
	{
		if (InSegments.Num() == 0 || InCount <= 0)
		{
			InContext.Fail(InLocation, TEXT("internal: an empty member path"));
			return FEmitted();
		}
		const FString FullPath = JoinPath(InSegments, InSegments.Num());
		FEmitted Current = EmitPathRoot(InSegments[0], FullPath, InLocation, InContext);
		for (int32 Index = 1; Index < InCount && Index < InSegments.Num() && !InContext.bFailed; ++Index)
		{
			Current = EmitMemberHop(Current, InSegments, Index, InLocation, InContext);
		}
		return InContext.bFailed ? FEmitted() : Current;
	}

	/**
	 * A variable of the class (`Title`), or a member path through objects (`Player.Stats.Rank`) -- which used to be
	 * refused here outright, a graph having no node that gets a sub-property by name. It has one per hop: an external
	 * get whose target is the object the hop before it read.
	 */
	FEmitted EmitVariableRef(const FDreamUIExpression& InExpression, FThunkContext& InContext)
	{
		TArray<FString> Segments;
		if (!SplitPath(InExpression.Symbol, Segments))
		{
			InContext.Fail(InExpression.Location, FString::Printf(TEXT("'%s' is neither a variable nor a member path"), *InExpression.Symbol));
			return FEmitted();
		}
		return EmitMemberPath(Segments, Segments.Num(), InExpression.Location, InContext);
	}

	FEmitted EmitCall(const FDreamUIExpression& InExpression, FThunkContext& InContext)
	{
		TArray<FString> Segments;
		if (!SplitPath(InExpression.Symbol, Segments))
		{
			InContext.Fail(InExpression.Location, FString::Printf(TEXT("'%s' is not a function or a member function"), *InExpression.Symbol));
			return FEmitted();
		}

		// `Player.FormatGold(…)`: the receiver path first, read like any member path, and the function looked up on the
		// class it reaches -- callable from a graph, or there is no node to call it with.
		FEmitted Receiver;
		UFunction* Function = nullptr;
		if (Segments.Num() > 1)
		{
			Receiver = EmitMemberPath(Segments, Segments.Num() - 1, InExpression.Location, InContext);
			if (InContext.bFailed)
			{
				return FEmitted();
			}
			UClass* ReceiverClass = GetPinObjectClass(Receiver.PinType);
			if (ReceiverClass == nullptr)
			{
				InContext.FailWith(EDreamUIDiagnosticCode::MemberPathThroughNonObject, InExpression.Location, FString::Printf(
					TEXT("'%s()' cannot be called: '%s' holds a %s, not an object, so it has no function '%s'"),
					*InExpression.Symbol, *JoinPath(Segments, Segments.Num() - 1),
					*UEdGraphSchema_K2::TypeToText(Receiver.PinType).ToString(), *Segments.Last()));
				return FEmitted();
			}
			FString WhyNot;
			Function = FindCallableMember(ReceiverClass, Segments.Last(), WhyNot);
			if (Function == nullptr)
			{
				InContext.FailWith(EDreamUIDiagnosticCode::MemberPathNotFound, InExpression.Location,
					FString::Printf(TEXT("'%s()' cannot be called: %s"), *InExpression.Symbol, *WhyNot));
				return FEmitted();
			}
		}
		else
		{
			Function = FindSelfFunction(InContext.Blueprint, InExpression.Symbol);
			if (Function == nullptr)
			{
				InContext.Fail(InExpression.Location, FString::Printf(
					TEXT("'%s' is not a function on this class (as of the previous compile)"), *InExpression.Symbol));
				return FEmitted();
			}
		}

		TArray<const FProperty*> InputParameters;
		const FProperty* ReturnParameter = nullptr;
		GetInputParameters(Function, InputParameters, &ReturnParameter);
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

		// A member function is called ON the object the receiver path read: that object is the node's target. A static
		// one has no target to give -- its hidden self pin is the class default, as it is for any library function.
		if (Receiver.Pin != nullptr && !Function->HasAnyFunctionFlags(FUNC_Static))
		{
			UEdGraphPin* TargetPin = Node->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
			if (TargetPin == nullptr || !GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Receiver.Pin, TargetPin))
			{
				InContext.Fail(InExpression.Location, FString::Printf(TEXT("internal: the call to '%s' would not take its target"), *InExpression.Symbol));
				return FEmitted();
			}
		}

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
			|| InName.StartsWith(DreamUIExpressionThunks::GeneratedRoutePrefix)
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

	void AddDependency(TArray<TArray<FString>>& OutPaths, TArray<FString>&& InPath)
	{
		if (InPath.Num() > 0)
		{
			OutPaths.AddUnique(MoveTemp(InPath));
		}
	}

	/**
	 * Every member path InExpression reads, into OutPaths: a variable or a path its segments, a no-argument call its
	 * receiver's segments and its own name as one path (`Count()` is {Count}, `Player.GetHealthPercent()` is {Player,
	 * GetHealthPercent}), a literal nothing. A call WITH arguments clears bInOutComplete -- it may read what its arguments
	 * do not show -- and still records its receiver and whatever its arguments read, so the binding is at least re-read
	 * when those change.
	 *
	 * Read off the expression rather than off the nodes the emission made, which is what keeps the two from disagreeing
	 * about meaning: a name the emission resolved is a name read, whatever node read it.
	 */
	void CollectDependencies(const FDreamUIExpression& InExpression, TArray<TArray<FString>>& OutPaths, bool& bInOutComplete)
	{
		switch (InExpression.Kind)
		{
		case FDreamUIExpression::EKind::VariableRef:
		{
			TArray<FString> Segments;
			if (SplitPath(InExpression.Symbol, Segments))
			{
				AddDependency(OutPaths, MoveTemp(Segments));
			}
			break;
		}
		case FDreamUIExpression::EKind::Call:
		{
			TArray<FString> Segments;
			const bool bSplit = SplitPath(InExpression.Symbol, Segments);
			if (InExpression.Operands.Num() == 0)
			{
				if (bSplit)
				{
					AddDependency(OutPaths, MoveTemp(Segments));
				}
				break;
			}
			bInOutComplete = false;
			if (bSplit && Segments.Num() > 1)
			{
				Segments.RemoveAt(Segments.Num() - 1);
				AddDependency(OutPaths, MoveTemp(Segments));
			}
			for (const FDreamUIExpression& Operand : InExpression.Operands)
			{
				CollectDependencies(Operand, OutPaths, bInOutComplete);
			}
			break;
		}
		case FDreamUIExpression::EKind::Unary:
		case FDreamUIExpression::EKind::Binary:
			for (const FDreamUIExpression& Operand : InExpression.Operands)
			{
				CollectDependencies(Operand, OutPaths, bInOutComplete);
			}
			break;
		default:
			// A literal reads nothing; a resource is a constant of the class's defaults, which no binding re-reads.
			break;
		}
	}

	/** Record what InExpression reads onto the line, as the builder will copy it onto the binding. */
	void RecordDependencies(FDreamUIProperty& InOutProperty, const FDreamUIExpression& InExpression)
	{
		InOutProperty.BindingDependencies.Reset();
		bool bComplete = true;
		CollectDependencies(InExpression, InOutProperty.BindingDependencies, bComplete);
		InOutProperty.bBindingDependenciesRecorded = true;
		InOutProperty.bBindingDependenciesComplete = bComplete;
	}

	/** One path, complete: a `<->`'s variable or member path, a bare `F()`'s function. */
	void RecordPathDependency(FDreamUIProperty& InOutProperty, const TArray<FString>& InSegments)
	{
		InOutProperty.BindingDependencies.Reset();
		InOutProperty.BindingDependencies.Add(InSegments);
		InOutProperty.bBindingDependenciesRecorded = true;
		InOutProperty.bBindingDependenciesComplete = true;
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

		// What the line reads, recorded off the expression before it goes: from here on the line is a function name.
		RecordDependencies(InProperty, InProperty.BindingExpression.GetValue());
		InProperty.BindingFunction = ThunkName;
		InProperty.BindingExpression.Reset();
	}

	/**
	 * `Prop <- F()`, which the parser hands over as BindingFunction alone: nothing to lower -- the function is the binding
	 * -- and its read recorded like any expression's, {F}, complete.
	 *
	 * A dotted one (`Prop <- Player.GetHealthPercent()`, should a front end keep that shape here) names no function of the
	 * class: it is lowered as the call it is, into a thunk like any other member call.
	 */
	void LowerBareCall(UDreamWidgetBlueprint* InBlueprint, const FString& InNodeId, FDreamUIProperty& InProperty,
		FDreamUIDiagnosticBag& InDiagnostics, TSet<FString>& InOutClaimedNames)
	{
		FString FunctionName = InProperty.BindingFunction.TrimStartAndEnd();
		FunctionName.RemoveFromEnd(TEXT("()"));
		FunctionName.TrimStartAndEndInline();
		if (FunctionName.Contains(TEXT(".")))
		{
			FDreamUIExpression Call;
			Call.Kind = FDreamUIExpression::EKind::Call;
			Call.Symbol = FunctionName;
			Call.Location = InProperty.Location;
			InProperty.BindingFunction.Reset();
			InProperty.BindingExpression = MoveTemp(Call);
			LowerProperty(InBlueprint, InNodeId, InProperty, InDiagnostics, InOutClaimedNames);
			return;
		}
		if (!FunctionName.IsEmpty())
		{
			RecordPathDependency(InProperty, { FunctionName });
		}
	}

	/**
	 * `if IsValid(InReceiver)`, threaded onto the exec chain: returns the branch's Then pin, from which the work on the
	 * receiver runs, with OutElse the pin from which nothing does. LastExecPin moves to Then. Null after a refusal.
	 *
	 * What a handler and a setter put in front of an object a member path read, where a getter puts nothing: they run on
	 * an event, whenever it fires, and a view model nobody has handed over yet is an ordinary state rather than a bug --
	 * the click does nothing, instead of an Accessed None every time.
	 */
	UEdGraphPin* EmitValidityBranch(const FEmitted& InReceiver, FThunkContext& InContext, const FDreamUISourceLocation& InLocation,
		UEdGraphPin*& OutElse)
	{
		OutElse = nullptr;
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		UFunction* IsValidFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(FName(TEXT("IsValid")));
		if (IsValidFunction == nullptr || InReceiver.Pin == nullptr || InContext.LastExecPin == nullptr)
		{
			InContext.Fail(InLocation, TEXT("internal: the check that the receiver is set could not be made"));
			return nullptr;
		}

		FGraphNodeCreator<UK2Node_CallFunction> CheckCreator(*InContext.Graph);
		UK2Node_CallFunction* Check = CheckCreator.CreateNode(/*bSelectNewNode*/false);
		Check->SetFromFunction(IsValidFunction);
		CheckCreator.Finalize();

		FGraphNodeCreator<UK2Node_IfThenElse> BranchCreator(*InContext.Graph);
		UK2Node_IfThenElse* Branch = BranchCreator.CreateNode(/*bSelectNewNode*/false);
		BranchCreator.Finalize();

		UEdGraphPin* ObjectPin = Check->FindPin(FName(TEXT("Object")), EGPD_Input);
		UEdGraphPin* IsSetPin = Check->GetReturnValuePin();
		UEdGraphPin* ConditionPin = Branch->GetConditionPin();
		UEdGraphPin* BranchExecute = Branch->GetExecPin();
		UEdGraphPin* Then = Branch->GetThenPin();
		OutElse = Branch->GetElsePin();
		const bool bWired = ObjectPin != nullptr && IsSetPin != nullptr && ConditionPin != nullptr && BranchExecute != nullptr
			&& Then != nullptr && OutElse != nullptr
			&& Schema->TryCreateConnection(InReceiver.Pin, ObjectPin)
			&& Schema->TryCreateConnection(IsSetPin, ConditionPin)
			&& Schema->TryCreateConnection(InContext.LastExecPin, BranchExecute);
		if (!bWired)
		{
			InContext.Fail(InLocation, TEXT("internal: the check that the receiver is set would not wire"));
			return nullptr;
		}
		InContext.LastExecPin = Then;
		return Then;
	}

	/**
	 * The body of `Prop <-> Path.Member`'s setter, below an entry taking NewValue: read the receiver (`Path`), do nothing
	 * while it is unset, then write NewValue into Member -- through InSetter when the class has one, otherwise by a set of
	 * the property followed by an announcement of the field on THAT object.
	 *
	 * The announcement is explicit because nothing else makes it. The Kismet compiler gives a set of a FieldNotify
	 * variable its broadcast only when a Blueprint class declares the variable (FKismetCompilerUtilities::
	 * IsPropertyUsesFieldNotificationSetValueAndBroadcast), and then aims it at the node's target -- which is the view
	 * model, and enough. A C++ or UnrealSharp view model's property is written raw, and without this call no binding
	 * reading it, on this widget or any other, would hear the change. Skipped where the Kismet compiler already makes
	 * it, so a change is announced once.
	 */
	bool BuildMemberWrite(UDreamWidgetBlueprint* InBlueprint, UEdGraph* InGraph, UK2Node_FunctionEntry* InEntry, UEdGraphPin* InNewValuePin,
		const TArray<FString>& InSegments, const FResolvedPath& InPath, UFunction* InSetter, const FDreamUIProperty& InProperty,
		FDreamUIDiagnosticBag& InDiagnostics)
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		FThunkContext Context;
		Context.Blueprint = InBlueprint;
		Context.Diagnostics = &InDiagnostics;
		Context.Ast = GLoweringAst;
		Context.Graph = InGraph;
		Context.LastExecPin = InEntry->FindPin(UEdGraphSchema_K2::PN_Then);

		const FEmitted Receiver = EmitMemberPath(InSegments, InSegments.Num() - 1, InProperty.Location, Context);
		UEdGraphPin* Else = nullptr;
		if (Context.bFailed || EmitValidityBranch(Receiver, Context, InProperty.Location, Else) == nullptr)
		{
			return false;
		}

		const FString Written = FString::Printf(TEXT("'%s <-> %s'"), *InProperty.Name, *InProperty.TwoWayProperty);
		UEdGraphPin* Tail = nullptr;
		if (InSetter != nullptr)
		{
			FGraphNodeCreator<UK2Node_CallFunction> CallCreator(*InGraph);
			UK2Node_CallFunction* Call = CallCreator.CreateNode(/*bSelectNewNode*/false);
			Call->SetFromFunction(InSetter);
			CallCreator.Finalize();

			TArray<const FProperty*> Inputs;
			GetInputParameters(InSetter, Inputs);
			UEdGraphPin* TargetPin = Call->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
			UEdGraphPin* ValuePin = Inputs.Num() == 1 ? Call->FindPin(Inputs[0]->GetFName(), EGPD_Input) : nullptr;
			UEdGraphPin* CallExecute = Call->GetExecPin();
			const bool bWired = TargetPin != nullptr && ValuePin != nullptr && CallExecute != nullptr
				&& Schema->TryCreateConnection(Receiver.Pin, TargetPin)
				&& Schema->TryCreateConnection(InNewValuePin, ValuePin)
				&& Schema->TryCreateConnection(Context.LastExecPin, CallExecute);
			if (!bWired)
			{
				InDiagnostics.AddError(EDreamUIDiagnosticCode::BindingExpressionUnsupported, InProperty.Location,
					FString::Printf(TEXT("internal: the write-back of %s through '%s' would not wire"), *Written, *InSetter->GetName()));
				return false;
			}
			Tail = Call->GetThenPin();
		}
		else
		{
			const FProperty* Member = InPath.LastProperty;
			UClass* MemberParent = Member->GetOwnerClass() != nullptr ? Member->GetOwnerClass()->GetAuthoritativeClass() : InPath.LastOwnerClass;

			FGraphNodeCreator<UK2Node_VariableSet> SetCreator(*InGraph);
			UK2Node_VariableSet* Set = SetCreator.CreateNode(/*bSelectNewNode*/false);
			Set->VariableReference.SetExternalMember(Member->GetFName(), MemberParent);
			SetCreator.Finalize();

			UEdGraphPin* TargetPin = Set->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
			UEdGraphPin* ValuePin = Set->FindPin(Member->GetFName(), EGPD_Input);
			UEdGraphPin* SetExecute = Set->GetExecPin();
			const bool bWired = TargetPin != nullptr && ValuePin != nullptr && SetExecute != nullptr
				&& Schema->TryCreateConnection(Receiver.Pin, TargetPin)
				&& Schema->TryCreateConnection(InNewValuePin, ValuePin)
				&& Schema->TryCreateConnection(Context.LastExecPin, SetExecute);
			if (!bWired)
			{
				InDiagnostics.AddError(EDreamUIDiagnosticCode::BindingExpressionUnsupported, InProperty.Location,
					FString::Printf(TEXT("internal: the write-back of %s would not wire"), *Written));
				return false;
			}
			Tail = Set->GetThenPin();

			const FName MemberName = Member->GetFName();
			if (DreamUIBindingPath::FindFieldId(InPath.LastOwnerClass, MemberName).IsValid()
				&& !FKismetCompilerUtilities::IsPropertyUsesFieldNotificationSetValueAndBroadcast(Member))
			{
				UFunction* BroadcastFunction = UFieldNotificationLibrary::StaticClass()->FindFunctionByName(FName(TEXT("BroadcastFieldValueChanged")));
				if (BroadcastFunction == nullptr)
				{
					InDiagnostics.AddError(EDreamUIDiagnosticCode::BindingExpressionUnsupported, InProperty.Location,
						TEXT("internal: the field notification library has no BroadcastFieldValueChanged"));
					return false;
				}
				FGraphNodeCreator<UK2Node_CallFunction> AnnounceCreator(*InGraph);
				UK2Node_CallFunction* Announce = AnnounceCreator.CreateNode(/*bSelectNewNode*/false);
				Announce->SetFromFunction(BroadcastFunction);
				AnnounceCreator.Finalize();

				UEdGraphPin* ObjectPin = Announce->FindPin(FName(TEXT("Object")), EGPD_Input);
				UEdGraphPin* FieldPin = Announce->FindPin(FName(TEXT("FieldId")), EGPD_Input);
				UEdGraphPin* AnnounceExecute = Announce->GetExecPin();
				const bool bAnnounceWired = ObjectPin != nullptr && FieldPin != nullptr && AnnounceExecute != nullptr && Tail != nullptr
					&& Schema->TryCreateConnection(Receiver.Pin, ObjectPin)
					&& Schema->TryCreateConnection(Tail, AnnounceExecute);
				if (!bAnnounceWired)
				{
					InDiagnostics.AddError(EDreamUIDiagnosticCode::BindingExpressionUnsupported, InProperty.Location,
						FString::Printf(TEXT("internal: the announcement after the write-back of %s would not wire"), *Written));
					return false;
				}
				// FFieldNotificationId by its text form, the one a struct pin's default always is: the field's name.
				Schema->TrySetDefaultValue(*FieldPin, FString::Printf(TEXT("(FieldName=\"%s\")"), *MemberName.ToString()));
				Tail = Announce->GetThenPin();
			}
		}

		FGraphNodeCreator<UK2Node_FunctionResult> ResultCreator(*InGraph);
		UK2Node_FunctionResult* Result = ResultCreator.CreateNode(/*bSelectNewNode*/false);
		Result->FunctionReference.SetSelfMember(InGraph->GetFName());
		ResultCreator.Finalize();
		UEdGraphPin* ResultExecute = Result->FindPin(UEdGraphSchema_K2::PN_Execute);
		const bool bClosed = ResultExecute != nullptr && Tail != nullptr
			&& Schema->TryCreateConnection(Tail, ResultExecute)
			&& Schema->TryCreateConnection(Else, ResultExecute);
		if (!bClosed)
		{
			InDiagnostics.AddError(EDreamUIDiagnosticCode::BindingExpressionUnsupported, InProperty.Location,
				FString::Printf(TEXT("internal: the setter of %s would not close"), *Written));
			return false;
		}
		return true;
	}

	void LowerTwoWay(UDreamWidgetBlueprint* InBlueprint, const FString& InNodeId, FDreamUIProperty& InProperty,
		TArray<FDreamUIProperty>& OutSynthesized, FDreamUIDiagnosticBag& InDiagnostics, TSet<FString>& InOutClaimedNames)
	{
		TArray<FString> Segments;
		if (!SplitPath(InProperty.TwoWayProperty, Segments))
		{
			InDiagnostics.AddError(EDreamUIDiagnosticCode::BindingExpressionUnsupported, InProperty.Location,
				FString::Printf(TEXT("'<-> %s' names neither a variable nor a member path"), *InProperty.TwoWayProperty));
			return;
		}
		// `Value <-> Volume` mirrors a variable of the class, written by a self set whose FieldNotify broadcast the
		// Kismet compiler supplies. `Value <-> Settings.Volume` mirrors a member of an object the class holds.
		const bool bMemberPath = Segments.Num() > 1;

		FEdGraphPinType VariableType;
		FResolvedPath Path;
		UFunction* MemberSetter = nullptr;
		if (!bMemberPath)
		{
			if (!FindVariablePinType(InBlueprint, InProperty.TwoWayProperty, VariableType))
			{
				InDiagnostics.AddError(EDreamUIDiagnosticCode::BindingExpressionUnsupported, InProperty.Location,
					FString::Printf(TEXT("'<-> %s' names no variable on this class (as of the previous compile)"), *InProperty.TwoWayProperty));
				return;
			}
		}
		else
		{
			if (!ResolvePathType(InBlueprint, Segments, Segments.Num(), InProperty.Location, InDiagnostics, Path))
			{
				return;
			}
			VariableType = Path.Type;
			// Writable through the class's own setter, or, failing one, by a set -- which a graph may make of no property
			// that is BlueprintReadOnly. Said here, before either half is made: a mirror that cannot write back is a
			// one-way binding the author did not write, and the slider would move and spring back.
			MemberSetter = FindMemberSetter(Path.LastOwnerClass, Path.LastProperty);
			if (MemberSetter == nullptr && Path.LastProperty->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
			{
				InDiagnostics.AddError(EDreamUIDiagnosticCode::TwoWayTargetReadOnly, InProperty.Location, FString::Printf(
					TEXT("'%s <-> %s' cannot write the value back: '%s' is read-only to graphs on %s, and %s has no BlueprintCallable %s taking a %s. Make the property BlueprintReadWrite, give the class that setter, or bind it one way with '<-'"),
					*InProperty.Name, *InProperty.TwoWayProperty, *Segments.Last(), *DescribeClass(Path.LastOwnerClass),
					*DescribeClass(Path.LastOwnerClass), *(TEXT("Set") + Segments.Last()), *UEdGraphSchema_K2::TypeToText(Path.Type).ToString()));
				return;
			}
		}

		// The forward half: a generated getter reading the variable, standing exactly where any
		// bound function stands. TwoWayProperty stays set -- the builder reads it to fill the
		// binding's NotifyField and to prefer the silent setter.
		const FString GetterName = MakeThunkName(TEXT("__DreamTwoWayGet_"), InNodeId, InProperty.Name, InOutClaimedNames);
		UEdGraph* GetterGraph = nullptr;
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
			GetterGraph = Graph;
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

			if (bMemberPath)
			{
				// The member's own object, not the class: a write into what the receiver path reaches, guarded and
				// announced -- see BuildMemberWrite.
				if (NewValuePin == nullptr
					|| !BuildMemberWrite(InBlueprint, Graph, Entry, NewValuePin, Segments, Path, MemberSetter, InProperty, InDiagnostics))
				{
					if (NewValuePin == nullptr)
					{
						InDiagnostics.AddError(EDreamUIDiagnosticCode::BindingExpressionUnsupported, InProperty.Location,
							FString::Printf(TEXT("internal: the reverse route for '<-> %s' grew no NewValue"), *InProperty.TwoWayProperty));
					}
					FBlueprintEditorUtils::RemoveGraph(InBlueprint, Graph);
					FBlueprintEditorUtils::RemoveGraph(InBlueprint, GetterGraph);
					return;
				}
				Entry->AddExtraFlags(FUNC_Private);
				InProperty.BindingFunction = GetterName;
				RecordPathDependency(InProperty, Segments);

				FDreamUIProperty& Route = OutSynthesized.AddDefaulted_GetRef();
				Route.Name = TEXT("OnValueChangedBP");
				Route.EventHandler = SetterName;
				Route.Location = InProperty.Location;
				return;
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
				FBlueprintEditorUtils::RemoveGraph(InBlueprint, GetterGraph);
				return;
			}
			// A setter has a side effect by definition; private, never pure.
			Entry->AddExtraFlags(FUNC_Private);
		}

		InProperty.BindingFunction = GetterName;
		RecordPathDependency(InProperty, Segments);

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
		Route.Kind = DreamUIExpressionThunks::FEmitRoute::EKind::Emit;
		Route.HandlerName = HandlerName;
		Route.EventName = Event->Name;
		Route.Arguments = InProperty.EmitArguments;
		Route.Location = InProperty.Location;
	}

	/**
	 * The first half of `Event -> Path.Func(args)` (or `+=`, or `=`: the operator says what happens to the event's other
	 * listeners, which is the builder's to hold the author to, and changes nothing here): check the receiver path and the
	 * function against the classes they name, name the handler, and hand the rest to GenerateEmitHandlers through
	 * GEmitRoutes -- the emit route's arrangement, for the emit route's reason: the handler takes what the source event
	 * sends, and only the builder knows whose event that is.
	 *
	 * Checked here as far as the AST allows, so a misspelling is said at its line before anything is built: the path
	 * (MemberPathNotFound, MemberPathThroughNonObject), the function (RouteMemberFunctionNotFound) and, when parentheses
	 * were written, the argument count (RouteArgumentMismatch). Whether the event's own parameters fit a function written
	 * without them waits for the second half. A refusal leaves EventHandler empty, which the builder skips in silence,
	 * as it does a refused emit.
	 */
	void LowerRoute(UDreamWidgetBlueprint* InBlueprint, const FString& InOwnerName, FDreamUIProperty& InProperty,
		FDreamUIDiagnosticBag& InDiagnostics, TSet<FString>& InOutClaimedNames)
	{
		if (GEmitRoutes == nullptr || !InProperty.EventHandler.IsEmpty())
		{
			return;
		}
		const FString Written = FString::Printf(TEXT("'%s %s %s'"), *InProperty.Name, DescribeRouteOperator(InProperty.RouteOperator), *InProperty.RouteTarget);

		TArray<FString> Segments;
		if (!SplitPath(InProperty.RouteTarget, Segments) || Segments.Num() < 2)
		{
			InDiagnostics.AddError(EDreamUIDiagnosticCode::RouteMemberFunctionNotFound, InProperty.Location, FString::Printf(
				TEXT("%s names no object to call a function on: a route to a member is 'Path.Func', and one to this class's own function is a bare name"),
				*Written));
			return;
		}
		FResolvedPath Receiver;
		if (!ResolvePathType(InBlueprint, Segments, Segments.Num() - 1, InProperty.Location, InDiagnostics, Receiver))
		{
			return;
		}
		UClass* ReceiverClass = GetPinObjectClass(Receiver.Type);
		if (ReceiverClass == nullptr)
		{
			InDiagnostics.AddError(EDreamUIDiagnosticCode::MemberPathThroughNonObject, InProperty.Location, FString::Printf(
				TEXT("%s cannot call '%s': '%s' holds a %s, not an object"), *Written, *Segments.Last(),
				*JoinPath(Segments, Segments.Num() - 1), *UEdGraphSchema_K2::TypeToText(Receiver.Type).ToString()));
			return;
		}
		FString WhyNot;
		UFunction* Function = FindCallableMember(ReceiverClass, Segments.Last(), WhyNot);
		if (Function != nullptr && Function->HasAnyFunctionFlags(FUNC_BlueprintPure))
		{
			// A pure function has no exec pins to call it on, and returns a value nobody would read: an event routed to
			// one would do nothing at all, which is worth saying rather than compiling.
			WhyNot = FString::Printf(TEXT("'%s' of %s is BlueprintPure -- it only returns a value, so an event has nothing to call it for"),
				*Segments.Last(), *DescribeClass(ReceiverClass));
			Function = nullptr;
		}
		if (Function == nullptr)
		{
			InDiagnostics.AddError(EDreamUIDiagnosticCode::RouteMemberFunctionNotFound, InProperty.Location,
				FString::Printf(TEXT("%s calls nothing: %s"), *Written, *WhyNot));
			return;
		}
		if (InProperty.bRouteHasArgumentList)
		{
			TArray<const FProperty*> Inputs;
			GetInputParameters(Function, Inputs);
			if (Inputs.Num() != InProperty.RouteArguments.Num())
			{
				InDiagnostics.AddError(EDreamUIDiagnosticCode::RouteArgumentMismatch, InProperty.Location, FString::Printf(
					TEXT("%s passes %d argument(s), and %s takes %d: %s%s"), *Written, InProperty.RouteArguments.Num(),
					*Segments.Last(), Inputs.Num(), *Segments.Last(), *DescribeInputs(Inputs)));
				return;
			}
		}

		const FString HandlerName = MakeThunkName(DreamUIExpressionThunks::GeneratedRoutePrefix, InOwnerName, InProperty.Name, InOutClaimedNames);
		InProperty.EventHandler = HandlerName;

		DreamUIExpressionThunks::FEmitRoute& Route = GEmitRoutes->AddDefaulted_GetRef();
		Route.Kind = DreamUIExpressionThunks::FEmitRoute::EKind::MemberCall;
		Route.HandlerName = HandlerName;
		Route.RouteTarget = JoinPath(Segments, Segments.Num());
		Route.Arguments = InProperty.RouteArguments;
		Route.bHasArgumentList = InProperty.bRouteHasArgumentList;
		Route.Location = InProperty.Location;
	}

	/**
	 * Lower every `<-` expression and every `<->` in one property list, under one owner name, recording what each reads;
	 * and name the handlers of its `-> emit` and `-> Path.Func` routes.
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
			else if (!Property.RouteTarget.IsEmpty())
			{
				LowerRoute(InBlueprint, InOwnerName, Property, InDiagnostics, InOutClaimedNames);
			}
			else if (Property.BindingExpression.IsSet())
			{
				LowerProperty(InBlueprint, InOwnerName, Property, InDiagnostics, InOutClaimedNames);
			}
			else if (!Property.TwoWayProperty.IsEmpty())
			{
				LowerTwoWay(InBlueprint, InOwnerName, Property, Synthesized, InDiagnostics, InOutClaimedNames);
			}
			else if (!Property.BindingFunction.IsEmpty())
			{
				LowerBareCall(InBlueprint, InOwnerName, Property, InDiagnostics, InOutClaimedNames);
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
		// A single-cast delegate (`OnInit = Handler`) is the third kind a route may leave from; its one listener takes
		// what its signature says, exactly as a multicast event's every listener does.
		if (const FDelegateProperty* SingleCast = CastField<FDelegateProperty>(EventProperty))
		{
			if (SingleCast->SignatureFunction == nullptr)
			{
				OutRefusal = FString::Printf(TEXT("'%s' declares no signature for a handler to copy"), *InBinding.EventName.ToString());
				return false;
			}
			OutSignature.SignatureFunction = SingleCast->SignatureFunction;
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
	 * A route handler's graph and its entry, taking what InSignature describes, with OutContext aimed at the graph and
	 * the entry's parameters in scope by name (OutParameterPins lists them in the event's order). Shared by the emit
	 * handler and the member route's, which differ only in what they call.
	 */
	UEdGraph* BeginHandlerGraph(UDreamWidgetBlueprint* InBlueprint, const FString& InHandlerName, const FSourceSignature& InSignature,
		FDreamUIDiagnosticBag& InDiagnostics, FThunkContext& OutContext, UK2Node_FunctionEntry*& OutEntry, TArray<UEdGraphPin*>& OutParameterPins)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(InBlueprint, FName(*InHandlerName), UEdGraph::StaticClass(),
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

		OutContext.Blueprint = InBlueprint;
		OutContext.Diagnostics = &InDiagnostics;
		OutContext.Ast = GLoweringAst;
		OutContext.Graph = Graph;
		OutContext.LastExecPin = Entry->FindPin(UEdGraphSchema_K2::PN_Then);
		OutParameterPins.Reset();
		for (UEdGraphPin* Pin : Entry->Pins)
		{
			if (Pin != nullptr && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				OutContext.LocalPins.Add(Pin->PinName.ToString(), Pin);
				OutParameterPins.Add(Pin);
			}
		}
		OutEntry = Entry;
		return Graph;
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

		FThunkContext Context;
		UK2Node_FunctionEntry* Entry = nullptr;
		TArray<UEdGraphPin*> EventPins;
		UEdGraph* Graph = BeginHandlerGraph(InBlueprint, InRoute.HandlerName, InSignature, InDiagnostics, Context, Entry, EventPins);

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

	/**
	 * The body of one `Event -> Path.Func` handler: a function taking what the source event sends, reading the receiver
	 * (`Path`), doing nothing while it is unset, and calling Func on it -- with the arguments written between the
	 * parentheses, evaluated like any `<-` expression with the event's parameters in scope by name; or, with none
	 * written, with nothing (a Func that takes nothing) or with the event's own parameters passed on (a Func that takes
	 * exactly those). Anything else is RouteArgumentMismatch, said here because only here are both signatures known.
	 *
	 * The receiver is read when the event fires, not when the widget is made: a view model handed over or swapped later
	 * is the one the next click calls.
	 */
	bool BuildRouteHandler(UDreamWidgetBlueprint* InBlueprint, const DreamUIExpressionThunks::FEmitRoute& InRoute, const FName InEventName,
		const FSourceSignature& InSignature, FDreamUIDiagnosticBag& InDiagnostics)
	{
		TArray<FString> Segments;
		if (!SplitPath(InRoute.RouteTarget, Segments) || Segments.Num() < 2)
		{
			// Checked when the route was named; not a state a file can reach.
			InDiagnostics.AddError(EDreamUIDiagnosticCode::RouteMemberFunctionNotFound, InRoute.Location, FString::Printf(
				TEXT("internal: '-> %s' no longer names a member function"), *InRoute.RouteTarget));
			return false;
		}
		const FString FunctionName = Segments.Last();
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

		FThunkContext Context;
		UK2Node_FunctionEntry* Entry = nullptr;
		TArray<UEdGraphPin*> EventPins;
		UEdGraph* Graph = BeginHandlerGraph(InBlueprint, InRoute.HandlerName, InSignature, InDiagnostics, Context, Entry, EventPins);

		// The receiver path, read like any member path -- with the event's parameters shadowing the class's members, as
		// they do in the arguments.
		const FEmitted Receiver = EmitMemberPath(Segments, Segments.Num() - 1, InRoute.Location, Context);
		UFunction* Function = nullptr;
		if (!Context.bFailed)
		{
			UClass* ReceiverClass = GetPinObjectClass(Receiver.PinType);
			FString WhyNot = FString::Printf(TEXT("'%s' holds no object"), *JoinPath(Segments, Segments.Num() - 1));
			Function = ReceiverClass != nullptr ? FindCallableMember(ReceiverClass, FunctionName, WhyNot) : nullptr;
			if (Function == nullptr || Function->HasAnyFunctionFlags(FUNC_BlueprintPure))
			{
				Context.FailWith(EDreamUIDiagnosticCode::RouteMemberFunctionNotFound, InRoute.Location, FString::Printf(
					TEXT("'%s -> %s' calls nothing: %s"), *InEventName.ToString(), *InRoute.RouteTarget,
					Function == nullptr ? *WhyNot : TEXT("it is BlueprintPure")));
			}
		}

		UK2Node_CallFunction* Call = nullptr;
		TArray<const FProperty*> Inputs;
		if (!Context.bFailed)
		{
			GetInputParameters(Function, Inputs);
			FGraphNodeCreator<UK2Node_CallFunction> CallCreator(*Graph);
			Call = CallCreator.CreateNode(/*bSelectNewNode*/false);
			Call->SetFromFunction(Function);
			CallCreator.Finalize();
			if (!Function->HasAnyFunctionFlags(FUNC_Static))
			{
				UEdGraphPin* TargetPin = Call->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
				if (TargetPin == nullptr || !Schema->TryCreateConnection(Receiver.Pin, TargetPin))
				{
					Context.Fail(InRoute.Location, FString::Printf(TEXT("internal: the call to '%s' would not take its target"), *InRoute.RouteTarget));
				}
			}
		}

		if (!Context.bFailed && InRoute.bHasArgumentList)
		{
			if (InRoute.Arguments.Num() != Inputs.Num())
			{
				Context.FailWith(EDreamUIDiagnosticCode::RouteArgumentMismatch, InRoute.Location, FString::Printf(
					TEXT("'-> %s(...)' passes %d argument(s), and %s takes %d: %s%s"), *InRoute.RouteTarget, InRoute.Arguments.Num(),
					*FunctionName, Inputs.Num(), *FunctionName, *DescribeInputs(Inputs)));
			}
			for (int32 Index = 0; Index < InRoute.Arguments.Num() && !Context.bFailed; ++Index)
			{
				const FDreamUIExpression& Argument = InRoute.Arguments[Index];
				UEdGraphPin* ParameterPin = Call->FindPin(Inputs[Index]->GetFName(), EGPD_Input);
				if (ParameterPin == nullptr)
				{
					Context.Fail(Argument.Location, FString::Printf(TEXT("internal: '%s' grew no pin for its parameter '%s'"),
						*FunctionName, *Inputs[Index]->GetName()));
					break;
				}
				FEmitted Value;
				if (!TryEmitEnumeratorLiteral(Argument, ParameterPin, Context, Value))
				{
					Value = EmitExpression(Argument, Context);
					if (Context.bFailed)
					{
						break;
					}
				}
				// The step that is the ROUTE's mistake rather than the expression's: a value of a type the function's
				// parameter does not take.
				Context.FailCode = EDreamUIDiagnosticCode::RouteArgumentMismatch;
				Context.FailPrefix = FString::Printf(TEXT("argument %d of '%s(...)' is its '%s', a %s, and "), Index + 1, *InRoute.RouteTarget,
					*Inputs[Index]->GetName(), *UEdGraphSchema_K2::TypeToText(ParameterPin->PinType).ToString());
				ConnectOrDefault(ParameterPin, Value, Context, Argument.Location);
				Context.FailCode = EDreamUIDiagnosticCode::BindingExpressionUnsupported;
				Context.FailPrefix.Reset();
			}
		}
		else if (!Context.bFailed && Inputs.Num() > 0)
		{
			// No parentheses, and a function that takes something: what the event sends is what it gets, parameter for
			// parameter, or the line says how to give it what it takes instead.
			bool bFits = Inputs.Num() == EventPins.Num();
			for (int32 Index = 0; Index < Inputs.Num() && bFits; ++Index)
			{
				UEdGraphPin* ParameterPin = Call->FindPin(Inputs[Index]->GetFName(), EGPD_Input);
				bFits = ParameterPin != nullptr && Schema->TryCreateConnection(EventPins[Index], ParameterPin);
			}
			if (!bFits)
			{
				Context.FailWith(EDreamUIDiagnosticCode::RouteArgumentMismatch, InRoute.Location, FString::Printf(
					TEXT("'%s -> %s' passes on what '%s' sends, %s, and %s takes %s -- write the arguments it takes, '%s(...)', or route an event that sends them"),
					*InEventName.ToString(), *InRoute.RouteTarget, *InEventName.ToString(), *DescribePins(EventPins),
					*FunctionName, *DescribeInputs(Inputs), *InRoute.RouteTarget));
			}
		}

		if (!Context.bFailed)
		{
			// After the arguments, so a call an argument makes has run before the check and the call that reads it -- the
			// order EmitCall keeps for the same reason.
			UEdGraphPin* Else = nullptr;
			UEdGraphPin* Then = EmitValidityBranch(Receiver, Context, InRoute.Location, Else);
			UEdGraphPin* CallExecute = Call->GetExecPin();
			if (Then != nullptr && (CallExecute == nullptr || !Schema->TryCreateConnection(Then, CallExecute)))
			{
				Context.Fail(InRoute.Location, FString::Printf(TEXT("internal: the call to '%s' would not wire into its handler"), *InRoute.RouteTarget));
			}
		}
		if (Context.bFailed)
		{
			FBlueprintEditorUtils::RemoveGraph(InBlueprint, Graph);
			return false;
		}

		// Private like every function this pass makes, and never pure: the call is the side effect it exists for.
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

		const bool bMemberCall = Route.Kind == FEmitRoute::EKind::MemberCall;
		const FName EventName = Binding->EventName;
		FSourceSignature Signature;
		FString Refusal;
		if (!ResolveSourceSignature(*Binding, InTree, Signature, Refusal))
		{
			if (bMemberCall)
			{
				InDiagnostics.AddError(EDreamUIDiagnosticCode::RouteArgumentMismatch, Route.Location, FString::Printf(
					TEXT("'-> %s' needs a handler that takes what its event sends, and %s"), *Route.RouteTarget, *Refusal));
			}
			else
			{
				InDiagnostics.AddError(EDreamUIDiagnosticCode::EmitRouteUnsupported, Route.Location, FString::Printf(
					TEXT("'emit %s' needs a handler that takes what its event sends, and %s"), *Route.EventName, *Refusal));
			}
			DropRoutes(HandlerName);
			continue;
		}
		const bool bBuilt = bMemberCall
			? BuildRouteHandler(InBlueprint, Route, EventName, Signature, InDiagnostics)
			: BuildEmitHandler(InBlueprint, Route, Signature, InDiagnostics);
		if (!bBuilt)
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
