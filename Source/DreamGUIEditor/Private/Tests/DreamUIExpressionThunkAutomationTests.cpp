// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprintTestTypes.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Text/DreamUIExpressionThunks.h"

#include "HAL/FileManager.h"
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

#endif
