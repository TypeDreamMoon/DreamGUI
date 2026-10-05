// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamScopedWorld.h"
#include "DreamViewModelCompileTestTypes.h"
#include "DreamViewModelTestTypes.h"
#include "DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprintTestTypes.h"
#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetEachBinding.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetViewModelSlot.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIExpressionThunks.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "FieldNotificationId.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"

/*
 * The compiler's half of plan 17: what a `viewmodels` block declares, and what a member path lowers into.
 *
 * Two levels, on purpose. The Thunk tests hand the thunk pass an AST built here and read what it wrote back -- the
 * function names, the recorded dependencies, the diagnostics -- with no parser and no builder between, so they say
 * what this pass does and nothing else. The Compile tests write a real file and compile a real Blueprint, and are the
 * ones that say the lanes meet: the parser's spelling, the builder's records, the class that comes out, and the
 * generated functions run on a live instance through ProcessEvent.
 */

namespace DreamUIViewModelCompileTestLocal
{
	using DreamTests::FScopedGameWorld;

	struct FScopedDuiFile
	{
		explicit FScopedDuiFile(const TCHAR* InFileName)
		{
			FilePath = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), InFileName));
			FPaths::NormalizeFilename(FilePath);
		}
		~FScopedDuiFile()
		{
			IFileManager::Get().Delete(*FilePath, false, true, true);
		}
		bool Write(const TArray<FString>& InLines) const
		{
			return FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *FilePath);
		}
		FString FilePath;
	};

	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedBlueprint(const TCHAR* InName, UClass* InParentClass = nullptr)
		{
			const FString PackageName = FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName);
			Package = CreatePackage(*PackageName);
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				InParentClass != nullptr ? InParentClass : UDreamTextUserWidgetBindingBase::StaticClass(), Package, FName(InName),
				BPTYPE_Normal, UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}
		~FScopedBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}
		bool SetDuiFilePath(const FString& InFilePath) const
		{
			UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
				? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
			if (Defaults == nullptr)
			{
				return false;
			}
			Defaults->SourceFile.FilePath = InFilePath;
			return true;
		}
		UClass* GetClass() const
		{
			return Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
		}
	};

	void Compile(UDreamWidgetBlueprint* InBlueprint, FCompilerResultsLog& OutResults)
	{
		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection, &OutResults);
	}

	/** Write the file, point the Blueprint at it and compile; false (with a test failure) when a step did not happen. */
	bool WriteAndCompile(FAutomationTestBase& InTest, const FScopedDuiFile& InFile, const TArray<FString>& InLines,
		const FScopedBlueprint& InFixture, FCompilerResultsLog& OutResults)
	{
		if (!InTest.TestTrue(TEXT("the fixture file was written"), InFile.Write(InLines))
			|| !InTest.TestNotNull(TEXT("the Blueprint was created"), InFixture.Blueprint)
			|| !InTest.TestTrue(TEXT("its Source File was set"), InFixture.SetDuiFilePath(InFile.FilePath)))
		{
			return false;
		}
		Compile(InFixture.Blueprint, OutResults);
		return true;
	}

	/** Every message of a compile, joined, so a failed expectation can say what the compile did say. */
	FString JoinMessages(const FCompilerResultsLog& InResults)
	{
		FString All;
		for (const TSharedRef<FTokenizedMessage>& Message : InResults.Messages)
		{
			All += Message->ToText().ToString() + TEXT(" | ");
		}
		return All;
	}

	FString Code(const EDreamUIDiagnosticCode InCode)
	{
		return FDreamUIDiagnostic::CodeToString(InCode);
	}

	const FDreamWidgetPropertyBinding* FindBinding(const TArray<FDreamWidgetPropertyBinding>& InBindings, const TCHAR* InWidget, const TCHAR* InProperty)
	{
		return InBindings.FindByPredicate([InWidget, InProperty](const FDreamWidgetPropertyBinding& InBinding)
		{
			return InBinding.WidgetName == FName(InWidget) && InBinding.PropertyName == FName(InProperty);
		});
	}

	/** Whether InBinding recorded the path InSegments, segment for segment. */
	bool HasDependency(const FDreamWidgetPropertyBinding& InBinding, const TArray<FString>& InSegments)
	{
		return InBinding.Dependencies.ContainsByPredicate([&InSegments](const FDreamWidgetBindingPath& InPath)
		{
			if (InPath.Segments.Num() != InSegments.Num())
			{
				return false;
			}
			for (int32 Index = 0; Index < InSegments.Num(); ++Index)
			{
				if (InPath.Segments[Index] != FName(*InSegments[Index]))
				{
					return false;
				}
			}
			return true;
		});
	}

	/** The same question of a line of the AST, as the thunk pass left it. */
	bool HasAstDependency(const FDreamUIProperty& InProperty, const TArray<FString>& InSegments)
	{
		return InProperty.BindingDependencies.ContainsByPredicate([&InSegments](const TArray<FString>& InPath)
		{
			return InPath == InSegments;
		});
	}

	UEdGraph* FindGraph(const UDreamWidgetBlueprint* InBlueprint, const FString& InName)
	{
		for (UEdGraph* Graph : InBlueprint->FunctionGraphs)
		{
			if (Graph != nullptr && Graph->GetName() == InName)
			{
				return Graph;
			}
		}
		return nullptr;
	}

	bool GraphCalls(const UEdGraph* InGraph, const TCHAR* InFunctionName)
	{
		for (const UEdGraphNode* Node : InGraph->Nodes)
		{
			const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			if (Call != nullptr && Call->FunctionReference.GetMemberName() == FName(InFunctionName))
			{
				return true;
			}
		}
		return false;
	}

	bool GraphSets(const UEdGraph* InGraph, const TCHAR* InMemberName)
	{
		for (const UEdGraphNode* Node : InGraph->Nodes)
		{
			const UK2Node_VariableSet* Set = Cast<UK2Node_VariableSet>(Node);
			if (Set != nullptr && Set->VariableReference.GetMemberName() == FName(InMemberName))
			{
				return true;
			}
		}
		return false;
	}

	/** Write an object into InOwner's object variable InName, as a host or a Blueprint Set would (without announcing it). */
	bool SetObjectVariable(UObject* InOwner, const TCHAR* InName, UObject* InValue)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(InOwner->GetClass(), FName(InName));
		if (Property == nullptr)
		{
			return false;
		}
		Property->SetObjectPropertyValue_InContainer(InOwner, InValue);
		return true;
	}

	/** Run InOwner's function InName -- a generated one, private or not -- handing its one real parameter InValue when given. */
	bool CallFunction(UObject* InOwner, const TCHAR* InName, const TOptional<float> InValue = TOptional<float>())
	{
		UFunction* Function = InOwner->FindFunction(FName(InName));
		if (Function == nullptr)
		{
			return false;
		}
		FStructOnScope Frame(Function);
		if (InValue.IsSet())
		{
			for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				if (It->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					continue;
				}
				if (const FFloatProperty* AsFloat = CastField<FFloatProperty>(*It))
				{
					AsFloat->SetPropertyValue_InContainer(Frame.GetStructMemory(), InValue.GetValue());
				}
				else if (const FDoubleProperty* AsDouble = CastField<FDoubleProperty>(*It))
				{
					AsDouble->SetPropertyValue_InContainer(Frame.GetStructMemory(), static_cast<double>(InValue.GetValue()));
				}
				break;
			}
		}
		InOwner->ProcessEvent(Function, Frame.GetStructMemory());
		return true;
	}

	/** Run a generated getter and read the real number it returns. */
	bool ReadReal(UObject* InOwner, const TCHAR* InName, double& OutValue)
	{
		UFunction* Function = InOwner->FindFunction(FName(InName));
		if (Function == nullptr)
		{
			return false;
		}
		FStructOnScope Frame(Function);
		InOwner->ProcessEvent(Function, Frame.GetStructMemory());
		const FProperty* Return = Function->GetReturnProperty();
		if (const FFloatProperty* AsFloat = CastField<FFloatProperty>(Return))
		{
			OutValue = AsFloat->GetPropertyValue_InContainer(Frame.GetStructMemory());
			return true;
		}
		if (const FDoubleProperty* AsDouble = CastField<FDoubleProperty>(Return))
		{
			OutValue = AsDouble->GetPropertyValue_InContainer(Frame.GetStructMemory());
			return true;
		}
		return false;
	}

	/** Run a generated getter and read the text it returns. */
	bool ReadText(UObject* InOwner, const TCHAR* InName, FText& OutValue)
	{
		UFunction* Function = InOwner->FindFunction(FName(InName));
		if (Function == nullptr)
		{
			return false;
		}
		FStructOnScope Frame(Function);
		InOwner->ProcessEvent(Function, Frame.GetStructMemory());
		const FTextProperty* Return = CastField<FTextProperty>(Function->GetReturnProperty());
		if (Return == nullptr)
		{
			return false;
		}
		OutValue = Return->GetPropertyValue_InContainer(Frame.GetStructMemory());
		return true;
	}

	// --- hand-built AST, for the Thunk tests ------------------------------------------------------------------------

	FDreamUIExpression MakeVariable(const TCHAR* InSymbol)
	{
		FDreamUIExpression Expression;
		Expression.Kind = FDreamUIExpression::EKind::VariableRef;
		Expression.Symbol = InSymbol;
		Expression.Location = FDreamUISourceLocation(1, 1);
		return Expression;
	}

	FDreamUIExpression MakeCall(const TCHAR* InSymbol, TArray<FDreamUIExpression> InArguments = TArray<FDreamUIExpression>())
	{
		FDreamUIExpression Expression;
		Expression.Kind = FDreamUIExpression::EKind::Call;
		Expression.Symbol = InSymbol;
		Expression.Operands = MoveTemp(InArguments);
		Expression.Location = FDreamUISourceLocation(1, 1);
		return Expression;
	}

	FDreamUIExpression MakeNumber(const TCHAR* InRaw)
	{
		FDreamUIExpression Expression;
		Expression.Kind = FDreamUIExpression::EKind::Literal;
		Expression.LiteralKind = EDreamUIValueKind::Number;
		Expression.LiteralRaw = InRaw;
		Expression.Location = FDreamUISourceLocation(1, 1);
		return Expression;
	}

	FDreamUIExpression MakeString(const TCHAR* InRaw)
	{
		FDreamUIExpression Expression = MakeNumber(InRaw);
		Expression.LiteralKind = EDreamUIValueKind::String;
		return Expression;
	}

	FDreamUIExpression MakeOperator(const TCHAR* InOperator, TArray<FDreamUIExpression> InOperands)
	{
		FDreamUIExpression Expression;
		Expression.Kind = InOperands.Num() == 1 ? FDreamUIExpression::EKind::Unary : FDreamUIExpression::EKind::Binary;
		Expression.Symbol = InOperator;
		Expression.Operands = MoveTemp(InOperands);
		Expression.Location = FDreamUISourceLocation(1, 1);
		return Expression;
	}

	/** A widget node under the root holding the one line InProperty -- built whole, so no reference outlives an append. */
	void AddNode(FDreamUIAst& InAst, const TCHAR* InId, FDreamUIProperty InProperty, const TCHAR* InType = TEXT("Text"))
	{
		FDreamUINode Node;
		Node.Kind = EDreamUINodeKind::Widget;
		Node.TypeName = InType;
		Node.Id = InId;
		InProperty.Location = FDreamUISourceLocation(InAst.Root.Children.Num() + 2, 5);
		Node.Properties.Add(MoveTemp(InProperty));
		InAst.Root.Children.Add(MoveTemp(Node));
	}

	FDreamUIProperty MakeBinding(const TCHAR* InName, FDreamUIExpression InExpression)
	{
		FDreamUIProperty Property;
		Property.Name = InName;
		Property.BindingExpression = MoveTemp(InExpression);
		return Property;
	}

	/** An AST with a root and nothing else, to add nodes to. */
	FDreamUIAst MakeAst()
	{
		FDreamUIAst Ast;
		Ast.bHasRoot = true;
		Ast.Root.Kind = EDreamUINodeKind::Widget;
		Ast.Root.TypeName = TEXT("Widget");
		Ast.Root.Id = TEXT("Root");
		Ast.Root.Location = FDreamUISourceLocation(1, 1);
		return Ast;
	}

	/** What a `viewmodels { <Class> <Name> }` entry declares, given to a Blueprint by hand: the variable the thunk pass reads. */
	void DeclareViewModelVariable(UDreamWidgetBlueprint* InBlueprint, const TCHAR* InName, UClass* InClass)
	{
		FBPVariableDescription Variable;
		Variable.VarName = FName(InName);
		Variable.VarGuid = FGuid::NewDeterministicGuid(InName);
		Variable.VarType = FEdGraphPinType(UEdGraphSchema_K2::PC_Object, NAME_None, InClass, EPinContainerType::None, false, FEdGraphTerminalType());
		Variable.PropertyFlags = CPF_Edit | CPF_BlueprintVisible;
		InBlueprint->GeneratedVariables.Add(MoveTemp(Variable));
	}

	bool BagHas(const FDreamUIDiagnosticBag& InBag, const EDreamUIDiagnosticCode InCode)
	{
		return InBag.Diagnostics.ContainsByPredicate([InCode](const FDreamUIDiagnostic& InDiagnostic)
		{
			return InDiagnostic.Code == InCode && InDiagnostic.IsError();
		});
	}
}

// =====================================================================================================================
// Thunk: the pass on its own
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelThunkDependenciesTest,
	"DreamGUI.Text.ViewModel.Thunk.EveryLineRecordsWhatItReads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * BindingDependencies, line by line, straight off the pass: a member path is one path, a no-argument call its receiver
 * and its name as one path, a call with arguments makes the line incomplete and still records its receiver and what its
 * arguments read, a literal records nothing and is complete, a bare `F()` (the parser's BindingFunction shape) is {F}, a
 * dotted bare call is lowered rather than left as a name no class has, a `<->` path is its path -- and nothing inside a
 * loop body is touched at all.
 */
bool FDreamUIViewModelThunkDependenciesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCompileTestLocal;

	FScopedBlueprint Fixture(TEXT("BP_ViewModelThunkDependencies"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint))
	{
		return false;
	}
	DeclareViewModelVariable(Fixture.Blueprint, TEXT("Player"), UDreamTestPlayerVM::StaticClass());

	FDreamUIAst Ast = MakeAst();
	AddNode(Ast, TEXT("Health"), MakeBinding(TEXT("RenderOpacity"), MakeVariable(TEXT("Player.Health"))));
	AddNode(Ast, TEXT("Alive"), MakeBinding(TEXT("bWidgetActive"), MakeOperator(TEXT("&&"), {
		MakeOperator(TEXT(">"), { MakeVariable(TEXT("Player.Health")), MakeNumber(TEXT("0")) }),
		MakeOperator(TEXT("!"), { MakeCall(TEXT("IsBusy")) }) })));
	AddNode(Ast, TEXT("Gold"), MakeBinding(TEXT("Text"), MakeCall(TEXT("Player.FormatGold"), { MakeVariable(TEXT("Player.Gold")) })));
	AddNode(Ast, TEXT("Constant"), MakeBinding(TEXT("RenderOpacity"), MakeNumber(TEXT("0.5"))));
	AddNode(Ast, TEXT("Rank"), MakeBinding(TEXT("Text"), MakeVariable(TEXT("Player.Stats.ClassName"))));
	{
		FDreamUIProperty Bare;
		Bare.Name = TEXT("bWidgetActive");
		Bare.BindingFunction = TEXT("IsBusy");
		AddNode(Ast, TEXT("Busy"), MoveTemp(Bare));
	}
	{
		// `Percent <- Player.GetHealthPercent()` in the shape a bare call travels in: a name, which no class has.
		FDreamUIProperty DottedBare;
		DottedBare.Name = TEXT("RenderOpacity");
		DottedBare.BindingFunction = TEXT("Player.GetHealthPercent");
		AddNode(Ast, TEXT("Percent"), MoveTemp(DottedBare));
	}
	{
		FDreamUIProperty TwoWay;
		TwoWay.Name = TEXT("Value");
		TwoWay.TwoWayProperty = TEXT("Player.Volume");
		AddNode(Ast, TEXT("Volume"), MoveTemp(TwoWay));
	}
	{
		// A loop body: the pass never looks inside one.
		FDreamUINode Loop;
		Loop.Kind = EDreamUINodeKind::ForLoop;
		Loop.LoopVariable = TEXT("Item");
		Loop.LoopSourceFunction = TEXT("Player.Items");
		Loop.bLoopSourceIsFunction = false;
		FDreamUINode Row;
		Row.Kind = EDreamUINodeKind::Widget;
		Row.TypeName = TEXT("Text");
		Row.Id = TEXT("Row");
		Row.Properties.Add(MakeBinding(TEXT("Text"), MakeVariable(TEXT("Item.Name"))));
		Loop.Children.Add(MoveTemp(Row));
		Ast.Root.Children.Add(MoveTemp(Loop));
	}

	FDreamUIDiagnosticBag Diagnostics;
	TArray<DreamUIExpressionThunks::FEmitRoute> Routes;
	DreamUIExpressionThunks::Generate(Fixture.Blueprint, Ast, Diagnostics, &Routes, nullptr);
	TestFalse(*FString::Printf(TEXT("the lowering raises nothing, saw [%s]"), *Diagnostics.ToString()), Diagnostics.HasErrors());

	const TArray<FDreamUINode>& Nodes = Ast.Root.Children;
	if (!TestEqual(TEXT("every node is still there"), Nodes.Num(), 9))
	{
		return false;
	}
	auto Line = [&Nodes](const int32 InIndex) -> const FDreamUIProperty& { return Nodes[InIndex].Properties[0]; };

	const FDreamUIProperty& Health = Line(0);
	TestTrue(TEXT("Player.Health is lowered into a thunk"), Health.BindingFunction.StartsWith(DreamUIExpressionThunks::GeneratedGraphPrefix));
	TestTrue(TEXT("its reads are recorded"), Health.bBindingDependenciesRecorded);
	TestTrue(TEXT("and complete"), Health.bBindingDependenciesComplete);
	TestEqual(TEXT("one path"), Health.BindingDependencies.Num(), 1);
	TestTrue(TEXT("{Player, Health}"), HasAstDependency(Health, { TEXT("Player"), TEXT("Health") }));

	const FDreamUIProperty& Alive = Line(1);
	TestTrue(TEXT("a comparison and a negation are complete"), Alive.bBindingDependenciesRecorded && Alive.bBindingDependenciesComplete);
	TestEqual(TEXT("two paths, the repeated one once"), Alive.BindingDependencies.Num(), 2);
	TestTrue(TEXT("{Player, Health}"), HasAstDependency(Alive, { TEXT("Player"), TEXT("Health") }));
	TestTrue(TEXT("{IsBusy}, a no-argument call"), HasAstDependency(Alive, { TEXT("IsBusy") }));

	const FDreamUIProperty& Gold = Line(2);
	TestTrue(TEXT("a call with arguments is recorded"), Gold.bBindingDependenciesRecorded);
	TestFalse(TEXT("and incomplete"), Gold.bBindingDependenciesComplete);
	TestTrue(TEXT("its receiver {Player}"), HasAstDependency(Gold, { TEXT("Player") }));
	TestTrue(TEXT("and its argument's {Player, Gold}"), HasAstDependency(Gold, { TEXT("Player"), TEXT("Gold") }));

	const FDreamUIProperty& Constant = Line(3);
	TestTrue(TEXT("a literal is recorded"), Constant.bBindingDependenciesRecorded);
	TestTrue(TEXT("complete"), Constant.bBindingDependenciesComplete);
	TestEqual(TEXT("and reads nothing"), Constant.BindingDependencies.Num(), 0);

	const FDreamUIProperty& Rank = Line(4);
	TestTrue(TEXT("a two-hop path is lowered"), Rank.BindingFunction.StartsWith(DreamUIExpressionThunks::GeneratedGraphPrefix));
	TestTrue(TEXT("{Player, Stats, ClassName}"), HasAstDependency(Rank, { TEXT("Player"), TEXT("Stats"), TEXT("ClassName") }));

	const FDreamUIProperty& Busy = Line(5);
	TestEqual(TEXT("a bare call keeps its function"), Busy.BindingFunction, FString(TEXT("IsBusy")));
	TestTrue(TEXT("and records it, complete"), Busy.bBindingDependenciesRecorded && Busy.bBindingDependenciesComplete);
	TestTrue(TEXT("{IsBusy}"), HasAstDependency(Busy, { TEXT("IsBusy") }));

	const FDreamUIProperty& Percent = Line(6);
	TestTrue(TEXT("a dotted bare call is lowered into a thunk"), Percent.BindingFunction.StartsWith(DreamUIExpressionThunks::GeneratedGraphPrefix));
	TestTrue(TEXT("{Player, GetHealthPercent}, complete"), HasAstDependency(Percent, { TEXT("Player"), TEXT("GetHealthPercent") })
		&& Percent.bBindingDependenciesComplete);

	const FDreamUINode& VolumeNode = Nodes[7];
	const FDreamUIProperty& Volume = VolumeNode.Properties[0];
	TestTrue(TEXT("<-> Player.Volume gets its getter"), Volume.BindingFunction.StartsWith(TEXT("__DreamTwoWayGet_")));
	TestTrue(TEXT("{Player, Volume}, complete"), HasAstDependency(Volume, { TEXT("Player"), TEXT("Volume") }) && Volume.bBindingDependenciesComplete);
	TestTrue(TEXT("and the reverse route into its setter"), VolumeNode.Properties.ContainsByPredicate([](const FDreamUIProperty& InProperty)
	{
		return InProperty.Name == TEXT("OnValueChangedBP") && InProperty.EventHandler.StartsWith(TEXT("__DreamTwoWaySet_"));
	}));

	const FDreamUIProperty& LoopLine = Nodes[8].Children[0].Properties[0];
	TestFalse(TEXT("a loop body's line is not recorded"), LoopLine.bBindingDependenciesRecorded);
	TestTrue(TEXT("nor lowered"), LoopLine.BindingExpression.IsSet() && LoopLine.BindingFunction.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelThunkRefusalsTest,
	"DreamGUI.Text.ViewModel.Thunk.APathOrARouteThatDoesNotResolveIsRefusedUnderItsOwnCode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Each refusal under its own code, hop by hop: a member the class does not have and one no graph can read
 * (MemberPathNotFound), a hop through a number (MemberPathThroughNonObject), a write-back into a read-only member with no
 * setter (TwoWayTargetReadOnly), a route to a function that is missing, not callable or pure (RouteMemberFunctionNotFound),
 * and a route passing the wrong number of arguments (RouteArgumentMismatch). A refused line keeps no function name and a
 * refused route no handler, which is what lets the builder skip both without saying it twice.
 */
bool FDreamUIViewModelThunkRefusalsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCompileTestLocal;

	auto MakeRoute = [](const TCHAR* InTarget, const bool bInArgumentList, TArray<FDreamUIExpression> InArguments)
	{
		FDreamUIProperty Route;
		Route.Name = TEXT("OnClicked");
		Route.RouteTarget = InTarget;
		Route.bRouteHasArgumentList = bInArgumentList;
		Route.RouteArguments = MoveTemp(InArguments);
		return Route;
	};
	auto MakeTwoWay = [](const TCHAR* InPath)
	{
		FDreamUIProperty TwoWay;
		TwoWay.Name = TEXT("Value");
		TwoWay.TwoWayProperty = InPath;
		return TwoWay;
	};

	struct FCase
	{
		const TCHAR* Name;
		EDreamUIDiagnosticCode Code;
		FDreamUIProperty Line;
	};
	TArray<FCase> Cases;
	Cases.Add({ TEXT("Misspelt"), EDreamUIDiagnosticCode::MemberPathNotFound, MakeBinding(TEXT("RenderOpacity"), MakeVariable(TEXT("Player.Helth"))) });
	Cases.Add({ TEXT("NotVisible"), EDreamUIDiagnosticCode::MemberPathNotFound, MakeBinding(TEXT("RenderOpacity"), MakeVariable(TEXT("Hidden.Secret"))) });
	Cases.Add({ TEXT("NoRoot"), EDreamUIDiagnosticCode::MemberPathNotFound, MakeBinding(TEXT("RenderOpacity"), MakeVariable(TEXT("Nobody.Health"))) });
	Cases.Add({ TEXT("MissingCall"), EDreamUIDiagnosticCode::MemberPathNotFound, MakeBinding(TEXT("Text"), MakeCall(TEXT("Player.FormatSilver"), { MakeNumber(TEXT("1")) })) });
	Cases.Add({ TEXT("ThroughANumber"), EDreamUIDiagnosticCode::MemberPathThroughNonObject, MakeBinding(TEXT("RenderOpacity"), MakeVariable(TEXT("Player.Health.Max"))) });
	Cases.Add({ TEXT("ReadOnly"), EDreamUIDiagnosticCode::TwoWayTargetReadOnly, MakeTwoWay(TEXT("Player.Level")) });
	Cases.Add({ TEXT("NoFunction"), EDreamUIDiagnosticCode::RouteMemberFunctionNotFound, MakeRoute(TEXT("Player.Nope"), true, {}) });
	Cases.Add({ TEXT("NotCallable"), EDreamUIDiagnosticCode::RouteMemberFunctionNotFound, MakeRoute(TEXT("Hidden.Poke"), true, {}) });
	Cases.Add({ TEXT("Pure"), EDreamUIDiagnosticCode::RouteMemberFunctionNotFound, MakeRoute(TEXT("Hidden.Peek"), true, {}) });
	Cases.Add({ TEXT("Count"), EDreamUIDiagnosticCode::RouteArgumentMismatch, MakeRoute(TEXT("Player.Heal"), true, { MakeNumber(TEXT("1")), MakeNumber(TEXT("2")) }) });

	for (FCase& Case : Cases)
	{
		FScopedBlueprint Fixture(*FString::Printf(TEXT("BP_ViewModelThunkRefusal%s"), Case.Name));
		if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint))
		{
			continue;
		}
		DeclareViewModelVariable(Fixture.Blueprint, TEXT("Player"), UDreamTestPlayerVM::StaticClass());
		DeclareViewModelVariable(Fixture.Blueprint, TEXT("Hidden"), UDreamVMCompileTestHiddenVM::StaticClass());

		FDreamUIAst Ast = MakeAst();
		AddNode(Ast, TEXT("Subject"), Case.Line);
		FDreamUIDiagnosticBag Diagnostics;
		TArray<DreamUIExpressionThunks::FEmitRoute> Routes;
		DreamUIExpressionThunks::Generate(Fixture.Blueprint, Ast, Diagnostics, &Routes, nullptr);

		const FString Codes = Diagnostics.ToString();
		TestTrue(*FString::Printf(TEXT("%s is refused as %s, saw [%s]"), Case.Name, *Code(Case.Code), *Codes), BagHas(Diagnostics, Case.Code));
		TestEqual(*FString::Printf(TEXT("%s is refused once"), Case.Name), Diagnostics.NumErrors(), 1);
		const FDreamUIProperty& Line = Ast.Root.Children[0].Properties[0];
		TestTrue(*FString::Printf(TEXT("%s keeps no function name"), Case.Name), Line.BindingFunction.IsEmpty());
		TestTrue(*FString::Printf(TEXT("%s keeps no handler"), Case.Name), Line.EventHandler.IsEmpty());
		TestEqual(*FString::Printf(TEXT("%s queues no handler"), Case.Name), Routes.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelThunkRouteNamingTest,
	"DreamGUI.Text.ViewModel.Thunk.AMemberRouteIsNamedAndQueuedWhateverItsOperator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The first half of `-> Path.Func`: a handler name in EventHandler, under the route prefix, and the route queued with
 * its target and arguments for the second half. `+=` and `=` lower exactly as `->` does -- which delegate kind takes
 * which is the builder's to hold the author to.
 */
bool FDreamUIViewModelThunkRouteNamingTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCompileTestLocal;

	FScopedBlueprint Fixture(TEXT("BP_ViewModelThunkRouteNaming"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint))
	{
		return false;
	}
	DeclareViewModelVariable(Fixture.Blueprint, TEXT("Player"), UDreamTestPlayerVM::StaticClass());

	const EDreamUIRouteOperator Operators[] = { EDreamUIRouteOperator::Arrow, EDreamUIRouteOperator::Append, EDreamUIRouteOperator::Assign };
	const TCHAR* Ids[] = { TEXT("Arrow"), TEXT("Append"), TEXT("Assign") };
	FDreamUIAst Ast = MakeAst();
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FDreamUIProperty Route;
		Route.Name = TEXT("OnClicked");
		Route.RouteTarget = TEXT("Player.Heal");
		Route.bRouteHasArgumentList = true;
		Route.RouteArguments.Add(MakeNumber(TEXT("25")));
		Route.RouteOperator = Operators[Index];
		AddNode(Ast, Ids[Index], MoveTemp(Route), TEXT("Native.Button"));
	}

	FDreamUIDiagnosticBag Diagnostics;
	TArray<DreamUIExpressionThunks::FEmitRoute> Routes;
	DreamUIExpressionThunks::Generate(Fixture.Blueprint, Ast, Diagnostics, &Routes, nullptr);
	TestFalse(*FString::Printf(TEXT("the routes are accepted, saw [%s]"), *Diagnostics.ToString()), Diagnostics.HasErrors());
	if (!TestEqual(TEXT("one handler queued per route"), Routes.Num(), 3))
	{
		return false;
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FDreamUIProperty& Line = Ast.Root.Children[Index].Properties[0];
		const FString Expected = FString::Printf(TEXT("%s%s_OnClicked"), DreamUIExpressionThunks::GeneratedRoutePrefix, Ids[Index]);
		TestEqual(*FString::Printf(TEXT("%s: the handler's name is written in"), Ids[Index]), Line.EventHandler, Expected);
		TestEqual(*FString::Printf(TEXT("%s: and queued under it"), Ids[Index]), Routes[Index].HandlerName, Expected);
		TestTrue(*FString::Printf(TEXT("%s: as a member call"), Ids[Index]), Routes[Index].Kind == DreamUIExpressionThunks::FEmitRoute::EKind::MemberCall);
		TestEqual(*FString::Printf(TEXT("%s: of Player.Heal"), Ids[Index]), Routes[Index].RouteTarget, FString(TEXT("Player.Heal")));
		TestTrue(*FString::Printf(TEXT("%s: with its argument"), Ids[Index]), Routes[Index].bHasArgumentList && Routes[Index].Arguments.Num() == 1);
	}
	return true;
}

// =====================================================================================================================
// Compile: a real file, a real class
// =====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelDeclareTest,
	"DreamGUI.Text.ViewModel.Compile.AViewModelsBlockDeclaresFieldNotifyVariablesAndSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * One entry of every source, the class named every way a .dui names one (a reflected name, the same with its U, a full
 * path). Each becomes an object variable of its class that a host can set -- instance-editable, read-write in graphs,
 * on the spawn node, FieldNotify, in "ViewModels" -- and a slot on the generated class saying where its object comes
 * from. A second compile declares each once.
 */
bool FDreamUIViewModelDeclareTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCompileTestLocal;

	const FString ItemPath = UDreamTestItemVM::StaticClass()->GetPathName();
	FScopedDuiFile File(TEXT("ViewModelDeclare.dui"));
	FScopedBlueprint Fixture(TEXT("BP_ViewModelDeclare"));
	FCompilerResultsLog Results;
	if (!WriteAndCompile(*this, File, {
		TEXT("class /Temp/DreamGUITests/BP_ViewModelDeclare"),
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("    UDreamTestStatsVM Stats = new"),
		FString::Printf(TEXT("    %s Stash = global \"Stash\""), *ItemPath),
		TEXT("    DreamTestItemVM Shared = global"),
		TEXT("    DreamTestPlainModel Plain = parent"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("    }"),
		TEXT("}")}, Fixture, Results))
	{
		return false;
	}
	TestEqual(*FString::Printf(TEXT("the viewmodels compile clean, saw [%s]"), *JoinMessages(Results)), Results.NumErrors, 0);

	UDreamWidgetGeneratedClass* Class = Cast<UDreamWidgetGeneratedClass>(Fixture.GetClass());
	if (!TestNotNull(TEXT("a class came out"), Class))
	{
		return false;
	}

	struct FExpected
	{
		const TCHAR* Name;
		UClass* Class;
		EDreamViewModelSource Source;
		FName SourceName;
	};
	const FExpected Expected[] = {
		{ TEXT("Player"), UDreamTestPlayerVM::StaticClass(), EDreamViewModelSource::Host, FName(NAME_None) },
		{ TEXT("Stats"), UDreamTestStatsVM::StaticClass(), EDreamViewModelSource::New, FName(NAME_None) },
		{ TEXT("Stash"), UDreamTestItemVM::StaticClass(), EDreamViewModelSource::Global, FName(TEXT("Stash")) },
		{ TEXT("Shared"), UDreamTestItemVM::StaticClass(), EDreamViewModelSource::Global, FName(NAME_None) },
		{ TEXT("Plain"), UDreamTestPlainModel::StaticClass(), EDreamViewModelSource::Parent, FName(NAME_None) },
	};

	auto CheckDeclared = [this, &Fixture, &Expected](const TCHAR* InWhen)
	{
		UDreamWidgetGeneratedClass* Generated = Cast<UDreamWidgetGeneratedClass>(Fixture.GetClass());
		if (!TestNotNull(*FString::Printf(TEXT("%s: a class came out"), InWhen), Generated))
		{
			return;
		}
		for (const FExpected& Entry : Expected)
		{
			const FObjectProperty* Property = FindFProperty<FObjectProperty>(Generated, Entry.Name);
			if (!TestNotNull(*FString::Printf(TEXT("%s: '%s' is an object variable"), InWhen, Entry.Name), Property))
			{
				continue;
			}
			TestTrue(*FString::Printf(TEXT("%s: '%s' holds its class"), InWhen, Entry.Name), Property->PropertyClass == Entry.Class);
			TestTrue(*FString::Printf(TEXT("%s: '%s' is editable on instances"), InWhen, Entry.Name),
				Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance));
			TestTrue(*FString::Printf(TEXT("%s: '%s' is read-write in graphs"), InWhen, Entry.Name),
				Property->HasAnyPropertyFlags(CPF_BlueprintVisible) && !Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
			TestTrue(*FString::Printf(TEXT("%s: '%s' is offered on spawn"), InWhen, Entry.Name), Property->HasAnyPropertyFlags(CPF_ExposeOnSpawn));
			TestTrue(*FString::Printf(TEXT("%s: '%s' is FieldNotify"), InWhen, Entry.Name), Property->HasMetaData(FBlueprintMetadata::MD_FieldNotify)
				&& Generated->FieldNotifies.Contains(FFieldNotificationId(FName(Entry.Name))));
			TestEqual(*FString::Printf(TEXT("%s: '%s' is filed under ViewModels"), InWhen, Entry.Name),
				Property->GetMetaData(TEXT("Category")), FString(TEXT("ViewModels")));
			TestEqual(*FString::Printf(TEXT("%s: '%s' is not one of the author's own variables"), InWhen, Entry.Name),
				FBlueprintEditorUtils::FindNewVariableIndex(Fixture.Blueprint, FName(Entry.Name)), static_cast<int32>(INDEX_NONE));
		}

		const TArray<FDreamWidgetViewModelSlot>& Slots = Generated->GetViewModelSlots();
		if (!TestEqual(*FString::Printf(TEXT("%s: one slot per entry"), InWhen), Slots.Num(), static_cast<int32>(UE_ARRAY_COUNT(Expected))))
		{
			return;
		}
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			const FExpected& Entry = Expected[Index];
			TestEqual(*FString::Printf(TEXT("%s: slot %d is '%s'"), InWhen, Index, Entry.Name), Slots[Index].VariableName, FName(Entry.Name));
			TestTrue(*FString::Printf(TEXT("%s: of its class"), InWhen), Slots[Index].Class.Get() == Entry.Class);
			TestTrue(*FString::Printf(TEXT("%s: '%s' comes from where the file says"), InWhen, Entry.Name), Slots[Index].Source == Entry.Source);
			TestEqual(*FString::Printf(TEXT("%s: '%s' under the name written"), InWhen, Entry.Name), Slots[Index].SourceName, Entry.SourceName);
		}
		TestTrue(*FString::Printf(TEXT("%s: the Blueprint keeps the same list"), InWhen), Fixture.Blueprint->ViewModelSlots == Slots);
	};
	CheckDeclared(TEXT("first compile"));

	FCompilerResultsLog SecondResults;
	Compile(Fixture.Blueprint, SecondResults);
	TestEqual(TEXT("the recompile is clean"), SecondResults.NumErrors, 0);
	CheckDeclared(TEXT("second compile"));
	int32 PlayerCount = 0;
	for (const FBPVariableDescription& Variable : Fixture.Blueprint->GeneratedVariables)
	{
		PlayerCount += Variable.VarName == FName(TEXT("Player")) ? 1 : 0;
	}
	TestEqual(TEXT("one generated variable per entry, not one per compile"), PlayerCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelDeclareMistakesTest,
	"DreamGUI.Text.ViewModel.Compile.EveryViewModelsMistakeIsACompileError",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * One file per mistake, each failing its compile under its own code: a type nothing resolves (6015), a name a widget or
 * a prop already holds (6016), a source the class cannot honour -- `= new` of an abstract class (6017). An empty
 * `global ""` is the parser's (2021) and is pinned with the syntax.
 */
bool FDreamUIViewModelDeclareMistakesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCompileTestLocal;

	struct FCase
	{
		const TCHAR* Name;
		EDreamUIDiagnosticCode Code;
		TArray<FString> Lines;
	};
	const TArray<FCase> Cases = {
		{ TEXT("UnknownClass"), EDreamUIDiagnosticCode::ViewModelClassUnknown, {
			TEXT("viewmodels {"),
			TEXT("    NoSuchViewModelClass Player"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("    Text Title {"),
			TEXT("    }"),
			TEXT("}") } },
		{ TEXT("NameIsAWidget"), EDreamUIDiagnosticCode::ViewModelNameTaken, {
			TEXT("viewmodels {"),
			TEXT("    DreamTestPlayerVM Title"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("    Text Title {"),
			TEXT("    }"),
			TEXT("}") } },
		{ TEXT("NameIsAProp"), EDreamUIDiagnosticCode::ViewModelNameTaken, {
			TEXT("props {"),
			TEXT("    Text Player"),
			TEXT("}"),
			TEXT("viewmodels {"),
			TEXT("    DreamTestPlayerVM Player"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("    Text Title {"),
			TEXT("    }"),
			TEXT("}") } },
		{ TEXT("NewOfAbstract"), EDreamUIDiagnosticCode::ViewModelSourceInvalid, {
			TEXT("viewmodels {"),
			TEXT("    DreamViewModel Base = new"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("    Text Title {"),
			TEXT("    }"),
			TEXT("}") } },
	};

	for (const FCase& Case : Cases)
	{
		const FString CodeText = Code(Case.Code);
		AddExpectedError(CodeText, EAutomationExpectedErrorFlags::Contains, 0);

		FScopedDuiFile File(*FString::Printf(TEXT("ViewModelMistake%s.dui"), Case.Name));
		FScopedBlueprint Fixture(*FString::Printf(TEXT("BP_ViewModelMistake%s"), Case.Name));
		FCompilerResultsLog Results;
		if (!WriteAndCompile(*this, File, Case.Lines, Fixture, Results))
		{
			continue;
		}
		const FString Messages = JoinMessages(Results);
		TestTrue(*FString::Printf(TEXT("%s fails the compile"), Case.Name), Results.NumErrors > 0);
		TestTrue(*FString::Printf(TEXT("%s is reported as %s, saw [%s]"), Case.Name, *CodeText, *Messages), Messages.Contains(CodeText));
		TestEqual(*FString::Printf(TEXT("%s is reported once"), Case.Name), Results.NumErrors, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelPathMistakesTest,
	"DreamGUI.Text.ViewModel.Compile.AMemberPathOrARouteThatDoesNotFitIsACompileError",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The member-path and route mistakes of plan 17's table, each through a real file: `Player.Helth` and a member no graph
 * can read (5023), `Player.Health.Max` (5024), `<-> Player.Level` (6020), a route to a function that is not there or not
 * callable (6018), and a route whose arguments do not fit -- passing on what OnClicked sends to Heal(float), the wrong
 * count, the wrong type (6019).
 */
bool FDreamUIViewModelPathMistakesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCompileTestLocal;

	struct FCase
	{
		const TCHAR* Name;
		EDreamUIDiagnosticCode Code;
		TArray<FString> Body;
	};
	const TArray<FCase> Cases = {
		{ TEXT("Misspelt"), EDreamUIDiagnosticCode::MemberPathNotFound, { TEXT("    Text Subject { RenderOpacity <- Player.Helth }") } },
		{ TEXT("NotVisible"), EDreamUIDiagnosticCode::MemberPathNotFound, { TEXT("    Text Subject { RenderOpacity <- Hidden.Secret }") } },
		{ TEXT("ThroughANumber"), EDreamUIDiagnosticCode::MemberPathThroughNonObject, { TEXT("    Text Subject { RenderOpacity <- Player.Health.Max }") } },
		{ TEXT("ReadOnly"), EDreamUIDiagnosticCode::TwoWayTargetReadOnly, { TEXT("    Native.Slider Subject { Value <-> Player.Level }") } },
		{ TEXT("NoFunction"), EDreamUIDiagnosticCode::RouteMemberFunctionNotFound, { TEXT("    Native.Button Subject { OnClicked -> Player.Nope() }") } },
		{ TEXT("NotCallable"), EDreamUIDiagnosticCode::RouteMemberFunctionNotFound, { TEXT("    Native.Button Subject { OnClicked -> Hidden.Poke() }") } },
		{ TEXT("Forwarded"), EDreamUIDiagnosticCode::RouteArgumentMismatch, { TEXT("    Native.Button Subject { OnClicked -> Player.Heal }") } },
		{ TEXT("Count"), EDreamUIDiagnosticCode::RouteArgumentMismatch, { TEXT("    Native.Button Subject { OnClicked -> Player.Heal(1, 2) }") } },
		{ TEXT("Type"), EDreamUIDiagnosticCode::RouteArgumentMismatch, { TEXT("    Native.Button Subject { OnClicked -> Player.Heal(\"lots\") }") } },
	};

	for (const FCase& Case : Cases)
	{
		const FString CodeText = Code(Case.Code);
		AddExpectedError(CodeText, EAutomationExpectedErrorFlags::Contains, 0);

		TArray<FString> Lines = {
			FString::Printf(TEXT("class /Temp/DreamGUITests/BP_ViewModelPath%s"), Case.Name),
			TEXT("viewmodels {"),
			TEXT("    DreamTestPlayerVM Player"),
			TEXT("    DreamVMCompileTestHiddenVM Hidden"),
			TEXT("}"),
			TEXT("Widget Root {") };
		Lines.Append(Case.Body);
		Lines.Add(TEXT("}"));

		FScopedDuiFile File(*FString::Printf(TEXT("ViewModelPath%s.dui"), Case.Name));
		FScopedBlueprint Fixture(*FString::Printf(TEXT("BP_ViewModelPath%s"), Case.Name));
		FCompilerResultsLog Results;
		if (!WriteAndCompile(*this, File, Lines, Fixture, Results))
		{
			continue;
		}
		const FString Messages = JoinMessages(Results);
		TestTrue(*FString::Printf(TEXT("%s fails the compile"), Case.Name), Results.NumErrors > 0);
		TestTrue(*FString::Printf(TEXT("%s is reported as %s, saw [%s]"), Case.Name, *CodeText, *Messages), Messages.Contains(CodeText));
		TestEqual(*FString::Printf(TEXT("%s is reported once, and nothing follows from it"), Case.Name), Results.NumErrors, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelDependenciesTest,
	"DreamGUI.Text.ViewModel.Compile.TheClassBindingsCarryWhatTheyRead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * What the thunk pass records reaches the generated class through the builder and CompilePropertyBindings: each
 * binding's Dependencies, with bDependenciesRecorded, and bDependenciesComplete false for the one line that calls a
 * function with arguments. What the run time decides subscribe-or-poll from.
 */
bool FDreamUIViewModelDependenciesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCompileTestLocal;

	FScopedDuiFile File(TEXT("ViewModelDependencies.dui"));
	FScopedBlueprint Fixture(TEXT("BP_ViewModelDependencies"));
	FCompilerResultsLog Results;
	if (!WriteAndCompile(*this, File, {
		TEXT("class /Temp/DreamGUITests/BP_ViewModelDependencies"),
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Text HealthBar { RenderOpacity <- Player.Health }"),
		TEXT("    Text Alive { bWidgetActive <- Player.Health > 0 && !IsBusy() }"),
		TEXT("    Text Gold { Text <- Player.FormatGold(Player.Gold) }"),
		TEXT("    Text Constant { RenderOpacity <- 0.5 }"),
		TEXT("    Text Title { Text <- GetTitleText() }"),
		TEXT("    Text Percent { RenderOpacity <- Player.GetHealthPercent() }"),
		TEXT("    Text Rank { Text <- Player.Stats.ClassName }"),
		TEXT("    Native.Slider Volume { Value <-> Player.Volume }"),
		TEXT("}")}, Fixture, Results))
	{
		return false;
	}
	TestEqual(*FString::Printf(TEXT("the file compiles clean, saw [%s]"), *JoinMessages(Results)), Results.NumErrors, 0);

	TArray<FDreamWidgetPropertyBinding> Bindings;
	UDreamWidgetGeneratedClass::CollectPropertyBindings(Fixture.GetClass(), Bindings);

	struct FExpected
	{
		const TCHAR* Widget;
		const TCHAR* Property;
		bool bComplete;
		TArray<TArray<FString>> Paths;
	};
	const TArray<FExpected> Expected = {
		{ TEXT("HealthBar"), TEXT("RenderOpacity"), true, { { TEXT("Player"), TEXT("Health") } } },
		{ TEXT("Alive"), TEXT("bWidgetActive"), true, { { TEXT("Player"), TEXT("Health") }, { TEXT("IsBusy") } } },
		{ TEXT("Gold"), TEXT("Text"), false, { { TEXT("Player") }, { TEXT("Player"), TEXT("Gold") } } },
		{ TEXT("Constant"), TEXT("RenderOpacity"), true, {} },
		{ TEXT("Title"), TEXT("Text"), true, { { TEXT("GetTitleText") } } },
		{ TEXT("Percent"), TEXT("RenderOpacity"), true, { { TEXT("Player"), TEXT("GetHealthPercent") } } },
		{ TEXT("Rank"), TEXT("Text"), true, { { TEXT("Player"), TEXT("Stats"), TEXT("ClassName") } } },
		{ TEXT("Volume"), TEXT("Value"), true, { { TEXT("Player"), TEXT("Volume") } } },
	};
	for (const FExpected& Entry : Expected)
	{
		const FDreamWidgetPropertyBinding* Binding = FindBinding(Bindings, Entry.Widget, Entry.Property);
		if (!TestNotNull(*FString::Printf(TEXT("%s.%s is bound"), Entry.Widget, Entry.Property), Binding))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s.%s recorded what it reads"), Entry.Widget, Entry.Property), Binding->bDependenciesRecorded);
		TestEqual(*FString::Printf(TEXT("%s.%s is %s"), Entry.Widget, Entry.Property, Entry.bComplete ? TEXT("complete") : TEXT("incomplete")),
			Binding->bDependenciesComplete, Entry.bComplete);
		TestEqual(*FString::Printf(TEXT("%s.%s reads %d path(s)"), Entry.Widget, Entry.Property, Entry.Paths.Num()),
			Binding->Dependencies.Num(), Entry.Paths.Num());
		for (const TArray<FString>& Path : Entry.Paths)
		{
			TestTrue(*FString::Printf(TEXT("%s.%s reads %s"), Entry.Widget, Entry.Property, *FString::Join(Path, TEXT("."))),
				HasDependency(*Binding, Path));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelGetterTest,
	"DreamGUI.Text.ViewModel.Thunk.AGetterThroughAPathReturnsTheViewModelsValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The generated getters, run: `Player.Health` reads the view model the instance holds, and `Player.Stats.ClassName`
 * reads through the second object -- the external gets wired to the objects the hops before them read. Run by name
 * through ProcessEvent, so the result is the graph's and not whatever the run time made of it.
 */
bool FDreamUIViewModelGetterTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCompileTestLocal;

	FScopedDuiFile File(TEXT("ViewModelGetter.dui"));
	FScopedBlueprint Fixture(TEXT("BP_ViewModelGetter"));
	FCompilerResultsLog Results;
	if (!WriteAndCompile(*this, File, {
		TEXT("class /Temp/DreamGUITests/BP_ViewModelGetter"),
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Text HealthBar { RenderOpacity <- Player.Health }"),
		TEXT("    Text Rank { Text <- Player.Stats.ClassName }"),
		TEXT("}")}, Fixture, Results))
	{
		return false;
	}
	TestEqual(*FString::Printf(TEXT("the getters compile, saw [%s]"), *JoinMessages(Results)), Results.NumErrors, 0);
	UClass* Class = Fixture.GetClass();
	const FString HealthThunk = FString::Printf(TEXT("%sHealthBar_RenderOpacity"), DreamUIExpressionThunks::GeneratedGraphPrefix);
	const FString RankThunk = FString::Printf(TEXT("%sRank_Text"), DreamUIExpressionThunks::GeneratedGraphPrefix);
	if (!TestNotNull(TEXT("a class came out"), Class)
		|| !TestNotNull(TEXT("with the Health getter"), Class->FindFunctionByName(FName(*HealthThunk)))
		|| !TestNotNull(TEXT("and the ClassName getter"), Class->FindFunctionByName(FName(*RankThunk))))
	{
		return false;
	}
	TestTrue(TEXT("a getter is pure: it reads, and checks nothing"), Class->FindFunctionByName(FName(*HealthThunk))->HasAnyFunctionFlags(FUNC_BlueprintPure));

	FScopedGameWorld TestWorld;
	UDreamUserWidget* Instance = CreateDreamWidget(TestWorld.World, Class);
	if (!TestNotNull(TEXT("the class instantiates"), Instance))
	{
		return false;
	}
	UDreamTestPlayerVM* Player = NewObject<UDreamTestPlayerVM>(Instance);
	UDreamTestStatsVM* Stats = NewObject<UDreamTestStatsVM>(Player);
	Player->Health = 42.f;
	Player->Stats = Stats;
	Stats->ClassName = FText::FromString(TEXT("Android"));
	TestTrue(TEXT("the view model is handed over"), SetObjectVariable(Instance, TEXT("Player"), Player));

	double Health = 0.0;
	if (TestTrue(TEXT("the Health getter runs"), ReadReal(Instance, *HealthThunk, Health)))
	{
		TestEqual(TEXT("and returns the view model's Health"), Health, 42.0);
	}
	FText ClassName;
	if (TestTrue(TEXT("the ClassName getter runs"), ReadText(Instance, *RankThunk, ClassName)))
	{
		TestEqual(TEXT("and reads through Stats"), ClassName.ToString(), FString(TEXT("Android")));
	}

	// Another view model handed over: the getter reads whatever the variable holds when it runs.
	UDreamTestPlayerVM* Other = NewObject<UDreamTestPlayerVM>(Instance);
	Other->Health = 7.f;
	SetObjectVariable(Instance, TEXT("Player"), Other);
	if (ReadReal(Instance, *HealthThunk, Health))
	{
		TestEqual(TEXT("a replaced view model is the one read"), Health, 7.0);
	}
	Instance->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelTwoWayTest,
	"DreamGUI.Text.ViewModel.Compile.ATwoWayPathWritesBackThroughTheSetterOrWritesAndAnnounces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `Value <-> Player.Volume` writes back through SetVolume, the view model's own setter; `Value <-> Player.Brightness`,
 * whose class has no SetBrightness, writes the property and announces the field on the view model -- a native class,
 * whose property no Kismet-made broadcast covers. Neither does anything while no view model is set. Read off the
 * generated graphs and then run.
 */
bool FDreamUIViewModelTwoWayTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCompileTestLocal;

	FScopedDuiFile File(TEXT("ViewModelTwoWay.dui"));
	FScopedBlueprint Fixture(TEXT("BP_ViewModelTwoWay"));
	FCompilerResultsLog Results;
	if (!WriteAndCompile(*this, File, {
		TEXT("class /Temp/DreamGUITests/BP_ViewModelTwoWay"),
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Native.Slider Vol { Value <-> Player.Volume }"),
		TEXT("    Native.Slider Bright { Value <-> Player.Brightness }"),
		TEXT("}")}, Fixture, Results))
	{
		return false;
	}
	TestEqual(*FString::Printf(TEXT("the two-way paths compile, saw [%s]"), *JoinMessages(Results)), Results.NumErrors, 0);

	const FString VolumeSetter = TEXT("__DreamTwoWaySet_Vol_Value");
	const FString BrightnessSetter = TEXT("__DreamTwoWaySet_Bright_Value");
	const UEdGraph* VolumeGraph = FindGraph(Fixture.Blueprint, VolumeSetter);
	const UEdGraph* BrightnessGraph = FindGraph(Fixture.Blueprint, BrightnessSetter);
	if (TestNotNull(TEXT("Volume's setter graph is generated"), VolumeGraph))
	{
		TestTrue(TEXT("it calls SetVolume"), GraphCalls(VolumeGraph, TEXT("SetVolume")));
		TestFalse(TEXT("rather than writing the property"), GraphSets(VolumeGraph, TEXT("Volume")));
		TestTrue(TEXT("behind a check that the view model is set"), GraphCalls(VolumeGraph, TEXT("IsValid")));
	}
	if (TestNotNull(TEXT("Brightness's setter graph is generated"), BrightnessGraph))
	{
		TestTrue(TEXT("it writes the property"), GraphSets(BrightnessGraph, TEXT("Brightness")));
		TestTrue(TEXT("and announces it"), GraphCalls(BrightnessGraph, TEXT("BroadcastFieldValueChanged")));
	}

	TArray<FDreamWidgetEventBinding> Events;
	UDreamWidgetGeneratedClass::CollectEventBindings(Fixture.GetClass(), Events);
	TestTrue(TEXT("the slider's change routes into Volume's setter"), Events.ContainsByPredicate([&VolumeSetter](const FDreamWidgetEventBinding& InRoute)
	{
		return InRoute.EventName == FName(TEXT("OnValueChangedBP")) && InRoute.FunctionName == FName(*VolumeSetter);
	}));

	UClass* Class = Fixture.GetClass();
	FScopedGameWorld TestWorld;
	UDreamUserWidget* Instance = Class != nullptr ? CreateDreamWidget(TestWorld.World, Class) : nullptr;
	if (!TestNotNull(TEXT("the class instantiates"), Instance))
	{
		return false;
	}

	// Nothing set: the write-back has nowhere to go and goes nowhere, without an Accessed None.
	TestTrue(TEXT("Volume's setter runs with no view model"), CallFunction(Instance, *VolumeSetter, 0.9f));
	TestTrue(TEXT("Brightness's setter runs with no view model"), CallFunction(Instance, *BrightnessSetter, 0.9f));

	UDreamTestPlayerVM* Player = NewObject<UDreamTestPlayerVM>(Instance);
	SetObjectVariable(Instance, TEXT("Player"), Player);

	CallFunction(Instance, *VolumeSetter, 0.75f);
	TestTrue(TEXT("the write-back went through SetVolume"), Player->SetVolumeCount >= 1);
	TestEqual(TEXT("which took the value"), Player->Volume, 0.75f);

	int32 BrightnessAnnouncements = 0;
	const UE::FieldNotification::FFieldId BrightnessField = UDreamTestPlayerVM::FFieldNotificationClassDescriptor::Brightness;
	const FDelegateHandle Handle = Player->AddFieldValueChangedDelegate(BrightnessField,
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&BrightnessAnnouncements](UObject*, UE::FieldNotification::FFieldId)
		{
			++BrightnessAnnouncements;
		}));
	CallFunction(Instance, *BrightnessSetter, 0.25f);
	TestEqual(TEXT("the write-back wrote Brightness"), Player->Brightness, 0.25f);
	TestTrue(TEXT("and announced it on the view model"), BrightnessAnnouncements >= 1);
	Player->RemoveFieldValueChangedDelegate(BrightnessField, Handle);

	Instance->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRouteTest,
	"DreamGUI.Text.ViewModel.Compile.ARouteToAViewModelFunctionGetsAHandlerThatCallsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `-> Player.Apply()`, `-> Player.Heal(25)` and `OnValueChangedBP -> Player.SetVolume`: a generated handler each, taking
 * what its event sends, the route recorded against it like any route, and on a live instance the call made on whatever
 * view model the instance holds when the event fires -- nothing while it holds none, the written argument, the event's
 * own value passed on.
 */
bool FDreamUIViewModelRouteTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCompileTestLocal;

	FScopedDuiFile File(TEXT("ViewModelRoute.dui"));
	FScopedBlueprint Fixture(TEXT("BP_ViewModelRoute"));
	FCompilerResultsLog Results;
	if (!WriteAndCompile(*this, File, {
		TEXT("class /Temp/DreamGUITests/BP_ViewModelRoute"),
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Native.Button Apply { OnClicked -> Player.Apply() }"),
		TEXT("    Native.Button Heal { OnClicked -> Player.Heal(25) }"),
		TEXT("    Native.Slider Volume { OnValueChangedBP -> Player.SetVolume }"),
		TEXT("}")}, Fixture, Results))
	{
		return false;
	}
	TestEqual(*FString::Printf(TEXT("the routes compile, saw [%s]"), *JoinMessages(Results)), Results.NumErrors, 0);

	UClass* Class = Fixture.GetClass();
	if (!TestNotNull(TEXT("a class came out"), Class))
	{
		return false;
	}
	struct FExpected
	{
		const TCHAR* Widget;
		const TCHAR* Event;
		int32 Parameters;
	};
	const FExpected Expected[] = {
		{ TEXT("Apply"), TEXT("OnClicked"), 0 },
		{ TEXT("Heal"), TEXT("OnClicked"), 0 },
		{ TEXT("Volume"), TEXT("OnValueChangedBP"), 1 },
	};
	TArray<FDreamWidgetEventBinding> Events;
	UDreamWidgetGeneratedClass::CollectEventBindings(Class, Events);
	for (const FExpected& Entry : Expected)
	{
		const FName HandlerName(*FString::Printf(TEXT("%s%s_%s"), DreamUIExpressionThunks::GeneratedRoutePrefix, Entry.Widget, Entry.Event));
		const UFunction* Handler = Class->FindFunctionByName(HandlerName);
		if (!TestNotNull(*FString::Printf(TEXT("%s's handler is a function of the class"), Entry.Widget), Handler))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s's handler takes what %s sends"), Entry.Widget, Entry.Event),
			static_cast<int32>(Handler->NumParms), Entry.Parameters);
		TestTrue(*FString::Printf(TEXT("%s's route is recorded against it"), Entry.Widget), Events.ContainsByPredicate(
			[&HandlerName, &Entry](const FDreamWidgetEventBinding& InRoute)
			{
				return InRoute.FunctionName == HandlerName && InRoute.EventName == FName(Entry.Event)
					&& InRoute.WidgetName == FName(Entry.Widget);
			}));
	}

	FScopedGameWorld TestWorld;
	UDreamUserWidget* Instance = CreateDreamWidget(TestWorld.World, Class);
	if (!TestNotNull(TEXT("the class instantiates"), Instance))
	{
		return false;
	}
	const FString ApplyHandler = FString::Printf(TEXT("%sApply_OnClicked"), DreamUIExpressionThunks::GeneratedRoutePrefix);
	const FString HealHandler = FString::Printf(TEXT("%sHeal_OnClicked"), DreamUIExpressionThunks::GeneratedRoutePrefix);
	const FString VolumeHandler = FString::Printf(TEXT("%sVolume_OnValueChangedBP"), DreamUIExpressionThunks::GeneratedRoutePrefix);

	// No view model yet: every handler runs and calls nothing.
	TestTrue(TEXT("Apply's handler runs with no view model"), CallFunction(Instance, *ApplyHandler));
	TestTrue(TEXT("Volume's handler runs with no view model"), CallFunction(Instance, *VolumeHandler, 0.3f));

	UDreamTestPlayerVM* Player = NewObject<UDreamTestPlayerVM>(Instance);
	SetObjectVariable(Instance, TEXT("Player"), Player);

	CallFunction(Instance, *ApplyHandler);
	TestEqual(TEXT("Apply() was called"), Player->ApplyCount, 1);
	CallFunction(Instance, *HealHandler);
	TestEqual(TEXT("Heal got the written argument"), Player->LastHealAmount, 25.f);
	CallFunction(Instance, *VolumeHandler, 0.3f);
	TestTrue(TEXT("SetVolume got the event's value"), Player->SetVolumeCount >= 1 && FMath::IsNearlyEqual(Player->Volume, 0.3f));

	// And through the control itself: the route is an ordinary one, bound like any other.
	UDreamButton* ApplyButton = Cast<UDreamButton>(Instance->GetWidgetFromName(TEXT("Apply")));
	if (TestNotNull(TEXT("the Apply button is on the instance"), ApplyButton))
	{
		ApplyButton->OnClicked.Broadcast();
		TestEqual(TEXT("clicking it calls Apply on the view model"), Player->ApplyCount, 2);
	}
	Instance->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelSingleCastTest,
	"DreamGUI.Text.ViewModel.Compile.ASingleCastDelegateTakesAHandlerOfItsSignature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The third kind of event a route may leave from: a single-cast delegate, its one listener set with `=`. A class
 * function of its signature is accepted, a generated `= Player.SetVolume` handler takes what it sends, and a handler
 * of another shape is EventHandlerSignatureMismatch exactly as on a multicast event.
 */
bool FDreamUIViewModelSingleCastTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCompileTestLocal;

	const FString BehaviourPath = UDreamVMCompileTestBehaviour::StaticClass()->GetPathName();
	FScopedDuiFile File(TEXT("ViewModelSingleCast.dui"));
	FScopedBlueprint Fixture(TEXT("BP_ViewModelSingleCast"), UDreamVMCompileTestWidget::StaticClass());
	FCompilerResultsLog Results;
	if (!WriteAndCompile(*this, File, {
		TEXT("class /Temp/DreamGUITests/BP_ViewModelSingleCast"),
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Widget Picker {"),
		FString::Printf(TEXT("        + %s {"), *BehaviourPath),
		TEXT("            OnPicked = Player.SetVolume"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("    Widget Handled {"),
		FString::Printf(TEXT("        + %s {"), *BehaviourPath),
		TEXT("            OnPicked = HandlePicked"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")}, Fixture, Results))
	{
		return false;
	}
	TestEqual(*FString::Printf(TEXT("single-cast routes compile, saw [%s]"), *JoinMessages(Results)), Results.NumErrors, 0);

	UClass* Class = Fixture.GetClass();
	if (!TestNotNull(TEXT("a class came out"), Class))
	{
		return false;
	}
	const FName RouteHandler(*FString::Printf(TEXT("%sPicker_OnPicked"), DreamUIExpressionThunks::GeneratedRoutePrefix));
	const UFunction* Handler = Class->FindFunctionByName(RouteHandler);
	if (TestNotNull(TEXT("the member route's handler is generated"), Handler))
	{
		TestEqual(TEXT("taking the one value OnPicked sends"), static_cast<int32>(Handler->NumParms), 1);
	}
	TArray<FDreamWidgetEventBinding> Events;
	UDreamWidgetGeneratedClass::CollectEventBindings(Class, Events);
	TestTrue(TEXT("Picker's OnPicked routes into it"), Events.ContainsByPredicate([&RouteHandler](const FDreamWidgetEventBinding& InRoute)
	{
		return InRoute.WidgetName == FName(TEXT("Picker")) && InRoute.EventName == FName(TEXT("OnPicked")) && InRoute.FunctionName == RouteHandler;
	}));
	TestTrue(TEXT("Handled's OnPicked routes into HandlePicked"), Events.ContainsByPredicate([](const FDreamWidgetEventBinding& InRoute)
	{
		return InRoute.WidgetName == FName(TEXT("Handled")) && InRoute.EventName == FName(TEXT("OnPicked"))
			&& InRoute.FunctionName == FName(TEXT("HandlePicked"));
	}));

	// Fired on a live instance: the delegate's one listener is the route's.
	FScopedGameWorld TestWorld;
	UDreamVMCompileTestWidget* Instance = Cast<UDreamVMCompileTestWidget>(CreateDreamWidget(TestWorld.World, Class));
	if (TestNotNull(TEXT("the class instantiates"), Instance))
	{
		UDreamTestPlayerVM* Player = NewObject<UDreamTestPlayerVM>(Instance);
		SetObjectVariable(Instance, TEXT("Player"), Player);
		UDreamWidget* Picker = Instance->GetWidgetFromName(TEXT("Picker"));
		UDreamWidget* Handled = Instance->GetWidgetFromName(TEXT("Handled"));
		UDreamVMCompileTestBehaviour* PickerEvents = Picker != nullptr ? Picker->GetComponent<UDreamVMCompileTestBehaviour>() : nullptr;
		UDreamVMCompileTestBehaviour* HandledEvents = Handled != nullptr ? Handled->GetComponent<UDreamVMCompileTestBehaviour>() : nullptr;
		if (TestNotNull(TEXT("Picker carries the behaviour"), PickerEvents) && TestNotNull(TEXT("and so does Handled"), HandledEvents))
		{
			PickerEvents->Pick(0.4f);
			TestTrue(TEXT("Picker's pick reached SetVolume"), Player->SetVolumeCount >= 1 && FMath::IsNearlyEqual(Player->Volume, 0.4f));
			HandledEvents->Pick(0.6f);
			TestEqual(TEXT("Handled's pick reached HandlePicked"), Instance->PickedCount, 1);
			TestEqual(TEXT("with its value"), Instance->LastPicked, 0.6f);
		}
		Instance->DestroyWidget();
	}

	// A handler of another shape: refused as it would be on a multicast event.
	AddExpectedError(TEXT("its parameters do not match the event's"), EAutomationExpectedErrorFlags::Contains, 0);
	FScopedDuiFile WrongFile(TEXT("ViewModelSingleCastWrong.dui"));
	FScopedBlueprint WrongFixture(TEXT("BP_ViewModelSingleCastWrong"), UDreamVMCompileTestWidget::StaticClass());
	FCompilerResultsLog WrongResults;
	if (!WriteAndCompile(*this, WrongFile, {
		TEXT("class /Temp/DreamGUITests/BP_ViewModelSingleCastWrong"),
		TEXT("Widget Root {"),
		TEXT("    Widget Handled {"),
		FString::Printf(TEXT("        + %s {"), *BehaviourPath),
		TEXT("            OnPicked = HandleWrongShape"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")}, WrongFixture, WrongResults))
	{
		return false;
	}
	TestTrue(TEXT("a handler of the wrong shape fails the compile"), WrongResults.NumErrors > 0);
	TArray<FDreamWidgetEventBinding> WrongEvents;
	UDreamWidgetGeneratedClass::CollectEventBindings(WrongFixture.GetClass(), WrongEvents);
	TestFalse(TEXT("and its route does not reach the class"), WrongEvents.ContainsByPredicate([](const FDreamWidgetEventBinding& InRoute)
	{
		return InRoute.FunctionName == FName(TEXT("HandleWrongShape"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelLoopItemTest,
	"DreamGUI.Text.ViewModel.Compile.ALoopBodyIsCheckedAgainstItsElementClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `for Item in Player.Items`, Items a TArray<UDreamTestItemVM*>: the element class is known, so the body is checked
 * against it now instead of skipped in silence later. `Item.Nmae` is LoopItemMemberNotFound; `-> Item.Use()` fits;
 * `-> Item.Nope()` and `-> Item.UseWithValue` on OnClicked (which sends nothing for the float it takes) are
 * LoopItemRouteMismatch.
 */
bool FDreamUIViewModelLoopItemTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelCompileTestLocal;

	// The one that fits first: it compiles, and the loop reaches the class with its route.
	{
		FScopedDuiFile File(TEXT("ViewModelLoopFits.dui"));
		FScopedBlueprint Fixture(TEXT("BP_ViewModelLoopFits"));
		FCompilerResultsLog Results;
		if (!WriteAndCompile(*this, File, {
			TEXT("class /Temp/DreamGUITests/BP_ViewModelLoopFits"),
			TEXT("viewmodels {"),
			TEXT("    DreamTestPlayerVM Player"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("    for Item in Player.Items {"),
			TEXT("        Native.Button Use {"),
			TEXT("            OnClicked -> Item.Use()"),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("}")}, Fixture, Results))
		{
			return false;
		}
		TestEqual(*FString::Printf(TEXT("a body that fits its element class compiles, saw [%s]"), *JoinMessages(Results)), Results.NumErrors, 0);
		TArray<FDreamWidgetEachBinding> Loops;
		UDreamWidgetGeneratedClass::CollectEachBindings(Fixture.GetClass(), Loops);
		if (TestEqual(TEXT("the class carries the loop"), Loops.Num(), 1))
		{
			TestEqual(TEXT("from a member path"), Loops[0].SourcePath.Num(), 2);
			TestEqual(TEXT("with its route"), Loops[0].EntryRoutes.Num(), 1);
		}
	}

	struct FCase
	{
		const TCHAR* Name;
		EDreamUIDiagnosticCode Code;
		const TCHAR* Line;
		const TCHAR* Node;
	};
	const FCase Cases[] = {
		{ TEXT("Member"), EDreamUIDiagnosticCode::LoopItemMemberNotFound, TEXT("Text <- Item.Nmae"), TEXT("Text") },
		{ TEXT("Function"), EDreamUIDiagnosticCode::LoopItemRouteMismatch, TEXT("OnClicked -> Item.Nope()"), TEXT("Native.Button") },
		{ TEXT("Forwarded"), EDreamUIDiagnosticCode::LoopItemRouteMismatch, TEXT("OnClicked -> Item.UseWithValue"), TEXT("Native.Button") },
	};
	for (const FCase& Case : Cases)
	{
		const FString CodeText = Code(Case.Code);
		AddExpectedError(CodeText, EAutomationExpectedErrorFlags::Contains, 0);

		FScopedDuiFile File(*FString::Printf(TEXT("ViewModelLoop%s.dui"), Case.Name));
		FScopedBlueprint Fixture(*FString::Printf(TEXT("BP_ViewModelLoop%s"), Case.Name));
		FCompilerResultsLog Results;
		if (!WriteAndCompile(*this, File, {
			FString::Printf(TEXT("class /Temp/DreamGUITests/BP_ViewModelLoop%s"), Case.Name),
			TEXT("viewmodels {"),
			TEXT("    DreamTestPlayerVM Player"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("    for Item in Player.Items {"),
			FString::Printf(TEXT("        %s Row {"), Case.Node),
			FString::Printf(TEXT("            %s"), Case.Line),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("}")}, Fixture, Results))
		{
			continue;
		}
		const FString Messages = JoinMessages(Results);
		TestTrue(*FString::Printf(TEXT("%s fails the compile"), Case.Name), Results.NumErrors > 0);
		TestTrue(*FString::Printf(TEXT("%s is reported as %s, saw [%s]"), Case.Name, *CodeText, *Messages), Messages.Contains(CodeText));
	}
	return true;
}

#endif
