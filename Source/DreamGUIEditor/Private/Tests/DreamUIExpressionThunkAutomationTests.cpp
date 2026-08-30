// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprintTestTypes.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Text/DreamUIExpressionThunks.h"

#include "EdGraphSchema_K2.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

/*
 * `<- expression` end to end: a real file, a real compile, and a generated pure function standing
 * where the expression was. The structural claims -- one graph, regenerated not accumulated, the
 * binding resolved onto it, purity and signature right -- are exactly the ones no smaller test can
 * make, because the thunk pass lives inside the compiler's populate stage.
 */

namespace DreamUIExpressionThunkTestLocal
{
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

		explicit FScopedBlueprint(const TCHAR* InName)
		{
			const FString PackageName = FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName);
			Package = CreatePackage(*PackageName);
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTextUserWidgetBindingBase::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
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
	};

	void Compile(UDreamWidgetBlueprint* InBlueprint, FCompilerResultsLog& OutResults)
	{
		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection, &OutResults);
	}

	int32 CountGeneratedGraphs(const UDreamWidgetBlueprint* InBlueprint)
	{
		int32 Count = 0;
		for (const UEdGraph* Graph : InBlueprint->FunctionGraphs)
		{
			if (Graph != nullptr && Graph->GetName().StartsWith(DreamUIExpressionThunks::GeneratedGraphPrefix))
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIExpressionThunkCompilesTest,
	"DreamGUI.Text.Expression.AnExpressionCompilesIntoOnePureFunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIExpressionThunkCompilesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIExpressionThunkTestLocal;

	FScopedDuiFile File(TEXT("ThunkFixture.dui"));
	if (!TestTrue(TEXT("Fixture written"), File.Write({
		TEXT("class /Temp/DreamGUITests/BP_ThunkFixture"),
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("        bWidgetActive <- !IsBusy()"),
		TEXT("    }"),
		TEXT("}")})))
	{
		return false;
	}
	FScopedBlueprint Fixture(TEXT("BP_ThunkFixture"));
	if (!TestTrue(TEXT("Blueprint created"), Fixture.Blueprint != nullptr)
		|| !TestTrue(TEXT("Path set"), Fixture.SetDuiFilePath(File.FilePath)))
	{
		return false;
	}

	FCompilerResultsLog FirstResults;
	Compile(Fixture.Blueprint, FirstResults);
	TestEqual(TEXT("The expression compile has no errors"), FirstResults.NumErrors, 0);

	// The thunk: one function, named from the node and property, pure, private, bool-returning,
	// exactly one parameter (the return) -- which is the shape CompilePropertyBindings demands.
	const FName ThunkName(TEXT("__DreamBinding_Title_bWidgetActive"));
	UFunction* Thunk = Fixture.Blueprint->GeneratedClass->FindFunctionByName(ThunkName);
	if (!TestTrue(TEXT("The generated function exists on the class"), Thunk != nullptr))
	{
		return false;
	}
	TestEqual(TEXT("Return is its only parameter"), static_cast<int32>(Thunk->NumParms), 1);
	TestTrue(TEXT("It returns a bool"), CastField<FBoolProperty>(Thunk->GetReturnProperty()) != nullptr);
	TestTrue(TEXT("It is pure"), Thunk->HasAnyFunctionFlags(FUNC_BlueprintPure));

	// The binding resolved onto it, through the machinery that existed before expressions did.
	TArray<FDreamWidgetPropertyBinding> Bindings;
	UDreamWidgetGeneratedClass::CollectPropertyBindings(Fixture.Blueprint->GeneratedClass, Bindings);
	const bool bBindingResolved = Bindings.ContainsByPredicate([&ThunkName](const FDreamWidgetPropertyBinding& InBinding)
	{
		return InBinding.FunctionName == ThunkName;
	});
	TestTrue(TEXT("The property binding names the thunk"), bBindingResolved);

	// Regenerate-each-compile: a second compile leaves ONE graph, not two, and the same name.
	FCompilerResultsLog SecondResults;
	Compile(Fixture.Blueprint, SecondResults);
	TestEqual(TEXT("The recompile has no errors"), SecondResults.NumErrors, 0);
	TestEqual(TEXT("Still exactly one generated graph"), CountGeneratedGraphs(Fixture.Blueprint), 1);
	TestTrue(TEXT("...still findable by its deterministic name"),
		Fixture.Blueprint->GeneratedClass->FindFunctionByName(ThunkName) != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIExpressionThunkRefusalTest,
	"DreamGUI.Text.Expression.AnUnloweredExpressionFailsTheCompileWithDUI5011",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIExpressionThunkRefusalTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIExpressionThunkTestLocal;

	AddExpectedError(TEXT("DUI5011"), EAutomationExpectedErrorFlags::Contains, 0);

	FScopedDuiFile File(TEXT("ThunkRefusalFixture.dui"));
	if (!TestTrue(TEXT("Fixture written"), File.Write({
		TEXT("class /Temp/DreamGUITests/BP_ThunkRefusal"),
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("        bWidgetActive <- GetTitleText() && IsBusy()"),
		TEXT("    }"),
		TEXT("}")})))
	{
		return false;
	}
	FScopedBlueprint Fixture(TEXT("BP_ThunkRefusal"));
	if (!TestTrue(TEXT("Blueprint created"), Fixture.Blueprint != nullptr)
		|| !TestTrue(TEXT("Path set"), Fixture.SetDuiFilePath(File.FilePath)))
	{
		return false;
	}

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	// An FText has no '&&': the generator refuses, the compile errors, and no half-made graph
	// survives to confuse the next one.
	TestTrue(TEXT("The compile reports errors"), Results.NumErrors > 0);
	TestEqual(TEXT("No generated graph is left behind"), CountGeneratedGraphs(Fixture.Blueprint), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITwoWayBindingTest,
	"DreamGUI.Text.Expression.ATwoWayBindingDesugarsIntoBothHalves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITwoWayBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIExpressionThunkTestLocal;

	FScopedDuiFile File(TEXT("TwoWayFixture.dui"));
	if (!TestTrue(TEXT("Fixture written"), File.Write({
		TEXT("class /Temp/DreamGUITests/BP_TwoWayFixture"),
		TEXT("Widget Root {"),
		TEXT("    Widget Knob {"),
		TEXT("        + UIToggle {"),
		TEXT("        }"),
		TEXT("        bIsOn <-> bMuted"),
		TEXT("    }"),
		TEXT("}")})))
	{
		return false;
	}
	FScopedBlueprint Fixture(TEXT("BP_TwoWayFixture"));
	if (!TestTrue(TEXT("Blueprint created"), Fixture.Blueprint != nullptr)
		|| !TestTrue(TEXT("Path set"), Fixture.SetDuiFilePath(File.FilePath)))
	{
		return false;
	}
	// The variable the two sides mirror. Bool, so the 5.x "Blueprint float is a double" width trap
	// stays out of this test's way.
	FEdGraphPinType BoolType;
	BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	if (!TestTrue(TEXT("Variable added"), FBlueprintEditorUtils::AddMemberVariable(Fixture.Blueprint, TEXT("bMuted"), BoolType)))
	{
		return false;
	}

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	TestEqual(TEXT("The two-way compile has no errors"), Results.NumErrors, 0);

	// Both generated halves, with the shapes the two routes demand.
	UFunction* Getter = Fixture.Blueprint->GeneratedClass->FindFunctionByName(TEXT("__DreamTwoWayGet_Knob_bIsOn"));
	UFunction* Setter = Fixture.Blueprint->GeneratedClass->FindFunctionByName(TEXT("__DreamTwoWaySet_Knob_bIsOn"));
	if (!TestTrue(TEXT("The getter thunk exists"), Getter != nullptr)
		|| !TestTrue(TEXT("The setter thunk exists"), Setter != nullptr))
	{
		return false;
	}
	TestEqual(TEXT("Getter: return only"), static_cast<int32>(Getter->NumParms), 1);
	TestTrue(TEXT("Getter returns bool"), CastField<FBoolProperty>(Getter->GetReturnProperty()) != nullptr);
	TestTrue(TEXT("Getter is pure"), Getter->HasAnyFunctionFlags(FUNC_BlueprintPure));
	if (Setter->NumParms != 1)
	{
		FString ParameterDump;
		for (TFieldIterator<FProperty> It(Setter); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			ParameterDump += FString::Printf(TEXT("[%s:%s:%s] "), *It->GetName(), *It->GetClass()->GetName(),
				It->HasAnyPropertyFlags(CPF_ReturnParm) ? TEXT("ret") : (It->HasAnyPropertyFlags(CPF_OutParm) ? TEXT("out") : TEXT("in")));
		}
		AddInfo(FString::Printf(TEXT("setter params: %s"), *ParameterDump));
	}
	TestEqual(TEXT("Setter: one input, no return"), static_cast<int32>(Setter->NumParms), 1);
	TestTrue(TEXT("Setter is not pure -- it has a side effect"), !Setter->HasAnyFunctionFlags(FUNC_BlueprintPure));

	// The forward half: a property binding whose function is the getter, whose subscription keys on
	// the VARIABLE, and whose push goes through the silent setter -- the echo dies there.
	TArray<FDreamWidgetPropertyBinding> Bindings;
	UDreamWidgetGeneratedClass::CollectPropertyBindings(Fixture.Blueprint->GeneratedClass, Bindings);
	const FDreamWidgetPropertyBinding* Forward = Bindings.FindByPredicate([](const FDreamWidgetPropertyBinding& InBinding)
	{
		return InBinding.FunctionName == TEXT("__DreamTwoWayGet_Knob_bIsOn");
	});
	if (!TestTrue(TEXT("The forward binding resolved"), Forward != nullptr))
	{
		return false;
	}
	TestEqual(TEXT("...subscribing to the variable"), Forward->NotifyField, FName(TEXT("bMuted")));
	TestEqual(TEXT("...pushing through the silent setter"), Forward->SetterName, FName(TEXT("SetIsOnWithoutNotify")));

	// The reverse half: a synthesized event route from the control's one conventional changed
	// event into the setter thunk.
	TArray<FDreamWidgetEventBinding> Events;
	UDreamWidgetGeneratedClass::CollectEventBindings(Fixture.Blueprint->GeneratedClass, Events);
	const bool bReverseRouted = Events.ContainsByPredicate([](const FDreamWidgetEventBinding& InBinding)
	{
		return InBinding.EventName == TEXT("OnValueChangedBP")
			&& InBinding.FunctionName == TEXT("__DreamTwoWaySet_Knob_bIsOn");
	});
	TestTrue(TEXT("The reverse route resolved"), bReverseRouted);

	// Regeneration: the second compile leaves exactly one pair, same names.
	FCompilerResultsLog SecondResults;
	Compile(Fixture.Blueprint, SecondResults);
	TestEqual(TEXT("The recompile has no errors"), SecondResults.NumErrors, 0);
	TestTrue(TEXT("The pair survives by its deterministic names"),
		Fixture.Blueprint->GeneratedClass->FindFunctionByName(TEXT("__DreamTwoWayGet_Knob_bIsOn")) != nullptr
		&& Fixture.Blueprint->GeneratedClass->FindFunctionByName(TEXT("__DreamTwoWaySet_Knob_bIsOn")) != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIEachCompilesTest,
	"DreamGUI.Text.Expression.AnEachBlockCompilesIntoTemplateAndBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIEachCompilesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIExpressionThunkTestLocal;

	FScopedDuiFile File(TEXT("EachFixture.dui"));
	if (!TestTrue(TEXT("Fixture written"), File.Write({
		TEXT("class /Temp/DreamGUITests/BP_EachFixture"),
		TEXT("Widget Root {"),
		TEXT("    Widget List {"),
		TEXT("        + UIListView {"),
		TEXT("        }"),
		TEXT("        each Item in GetRows() {"),
		TEXT("            Widget Row {"),
		TEXT("                Text Label {"),
		TEXT("                    Text <- Item.Title"),
		TEXT("                }"),
		TEXT("            }"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")})))
	{
		return false;
	}
	FScopedBlueprint Fixture(TEXT("BP_EachFixture"));
	if (!TestTrue(TEXT("Blueprint created"), Fixture.Blueprint != nullptr)
		|| !TestTrue(TEXT("Path set"), Fixture.SetDuiFilePath(File.FilePath)))
	{
		return false;
	}

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	TestEqual(TEXT("The each compile has no errors"), Results.NumErrors, 0);

	TArray<FDreamWidgetEachBinding> EachBindings;
	UDreamWidgetGeneratedClass::CollectEachBindings(Fixture.Blueprint->GeneratedClass, EachBindings);
	if (!TestEqual(TEXT("One each binding on the class"), EachBindings.Num(), 1))
	{
		return false;
	}
	const FDreamWidgetEachBinding& Each = EachBindings[0];
	TestEqual(TEXT("Hosted by the list widget"), Each.HostWidgetName, FName(TEXT("List")));
	TestEqual(TEXT("Templated by the row"), Each.TemplateWidgetName, FName(TEXT("Row")));
	TestEqual(TEXT("Fed by the function"), Each.SourceName, FName(TEXT("GetRows")));
	TestTrue(TEXT("...as a function"), Each.bSourceIsFunction);
	if (!TestEqual(TEXT("One item binding inside"), Each.EntryBindings.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("The item binding lands on the label"), Each.EntryBindings[0].TargetWidgetDisplayName, FName(TEXT("Label")));
	TestEqual(TEXT("...on its Text"), Each.EntryBindings[0].PropertyName, FName(TEXT("Text")));
	TestEqual(TEXT("...through the setter"), Each.EntryBindings[0].SetterName, FName(TEXT("SetText")));
	TestEqual(TEXT("...from the item's member"), Each.EntryBindings[0].ItemMember, FName(TEXT("Title")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIEachMisplacedTest,
	"DreamGUI.Text.Expression.AnEachWithoutAListViewIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIEachMisplacedTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIExpressionThunkTestLocal;

	AddExpectedError(TEXT("DUI5012"), EAutomationExpectedErrorFlags::Contains, 0);
	// The fixture's only content IS the broken block, so the refusal leaves nothing to build and
	// the empty-tree error legitimately follows it.
	AddExpectedError(TEXT("DUI6002"), EAutomationExpectedErrorFlags::Contains, 0);

	FScopedDuiFile File(TEXT("EachMisplacedFixture.dui"));
	if (!TestTrue(TEXT("Fixture written"), File.Write({
		TEXT("class /Temp/DreamGUITests/BP_EachMisplaced"),
		TEXT("Widget Root {"),
		TEXT("    each Item in GetRows() {"),
		TEXT("        Widget Row {"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")})))
	{
		return false;
	}
	FScopedBlueprint Fixture(TEXT("BP_EachMisplaced"));
	if (!TestTrue(TEXT("Blueprint created"), Fixture.Blueprint != nullptr)
		|| !TestTrue(TEXT("Path set"), Fixture.SetDuiFilePath(File.FilePath)))
	{
		return false;
	}
	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	TestTrue(TEXT("The compile reports the misplaced each"), Results.NumErrors > 0);

	TArray<FDreamWidgetEachBinding> EachBindings;
	UDreamWidgetGeneratedClass::CollectEachBindings(Fixture.Blueprint->GeneratedClass, EachBindings);
	TestEqual(TEXT("Nothing half-made reaches the class"), EachBindings.Num(), 0);
	return true;
}

#endif
