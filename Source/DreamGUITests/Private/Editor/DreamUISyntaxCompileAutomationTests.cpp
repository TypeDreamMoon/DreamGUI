// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "DreamEventBindingTestTypes.h"
#include "DreamWidgetBehaviourTestTypes.h"
#include "DreamWidgetBlueprintTestTypes.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIExpressionThunks.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUISourceWatcher.h"
#include "Text/DreamUITextBuilder.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * What a .dui declares for the class it compiles into, end to end: a real file, a real compile, and the class
 * that comes out. `props` become variables a host sets, `events` become dispatchers a host routes, an
 * `-> emit` line becomes a handler that raises one, and a widget nobody named keeps the variable the run time
 * needs without offering it to anyone. Every one of these is a claim about the compiler's populate stage, which no
 * smaller test can reach.
 */

namespace DreamUISyntaxCompileTestLocal
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

	/** Write the file, make the Blueprint, point one at the other and compile; false (with a test failure) when any step did not happen. */
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

	/** The first behaviour of type T on InRoot or anything under it. */
	template <typename T>
	T* FindBehaviour(UDreamWidget* InRoot)
	{
		TArray<UDreamWidget*> Pending;
		if (InRoot != nullptr)
		{
			Pending.Add(InRoot);
		}
		while (Pending.Num() > 0)
		{
			UDreamWidget* Widget = Pending.Pop();
			for (UDreamUIBehaviour* Behaviour : Widget->GetAllComponents())
			{
				if (T* Typed = Cast<T>(Behaviour))
				{
					return Typed;
				}
			}
			Pending.Append(Widget->GetChildren());
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxPropsCompileTest,
	"DreamGUI.Text.Syntax.APropsBlockCompilesIntoInstanceEditableVariablesWithTheirDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * One prop of every type the language has, each with the default the file gives it -- or none, for the two that
 * name assets. What comes out has to be what a host can use: a variable of the matching type, editable on the
 * INSTANCE (a host's `Caption = "Language"` is an instance edit), read-write in graphs, offered on spawn, holding
 * the file's default on the class defaults, and generated rather than authored, so nobody's My Blueprint list grows
 * variables the next compile would rewrite.
 */
bool FDreamUISyntaxPropsCompileTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxCompileTestLocal;

	const UEnum* LookEnum = StaticEnum<EDreamWidgetVisibility>();
	FScopedDuiFile File(TEXT("SyntaxProps.dui"));
	FScopedBlueprint Fixture(TEXT("BP_SyntaxProps"));
	FCompilerResultsLog Results;
	if (!WriteAndCompile(*this, File, {
		TEXT("class /Temp/DreamGUITests/BP_SyntaxProps"),
		TEXT("props {"),
		TEXT("    Text Caption = \"Language\""),
		TEXT("    String Code = \"en\""),
		TEXT("    Number Ratio = 0.5"),
		TEXT("    Integer Count = 3"),
		TEXT("    Bool bLit = true"),
		TEXT("    Color Tint = #FF0000"),
		TEXT("    Vector2 Nudge = (4, 8)"),
		TEXT("    Asset Picture"),
		TEXT("    Class Kind"),
		FString::Printf(TEXT("    Enum %s Look = Collapsed"), *LookEnum->GetPathName()),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("    }"),
		TEXT("}")}, Fixture, Results))
	{
		return false;
	}
	TestEqual(*FString::Printf(TEXT("the props compile clean, saw [%s]"), *JoinMessages(Results)), Results.NumErrors, 0);

	UClass* Class = Fixture.Blueprint->GeneratedClass;
	if (!TestNotNull(TEXT("a class came out"), Class))
	{
		return false;
	}

	// The type of each, by the property class the compiler made of it.
	const FTextProperty* Caption = FindFProperty<FTextProperty>(Class, TEXT("Caption"));
	const FStrProperty* Code = FindFProperty<FStrProperty>(Class, TEXT("Code"));
	const FDoubleProperty* Ratio = FindFProperty<FDoubleProperty>(Class, TEXT("Ratio"));
	const FIntProperty* Count = FindFProperty<FIntProperty>(Class, TEXT("Count"));
	const FBoolProperty* Lit = FindFProperty<FBoolProperty>(Class, TEXT("bLit"));
	const FStructProperty* Tint = FindFProperty<FStructProperty>(Class, TEXT("Tint"));
	const FStructProperty* Nudge = FindFProperty<FStructProperty>(Class, TEXT("Nudge"));
	const FObjectProperty* Picture = FindFProperty<FObjectProperty>(Class, TEXT("Picture"));
	const FClassProperty* Kind = FindFProperty<FClassProperty>(Class, TEXT("Kind"));
	const FEnumProperty* Look = FindFProperty<FEnumProperty>(Class, TEXT("Look"));
	TestNotNull(TEXT("Text is an FText"), Caption);
	TestNotNull(TEXT("String is an FString"), Code);
	TestNotNull(TEXT("Number is a double"), Ratio);
	TestNotNull(TEXT("Integer is an int32"), Count);
	TestNotNull(TEXT("Bool is a bool"), Lit);
	TestTrue(TEXT("Color is an FLinearColor"), Tint != nullptr && Tint->Struct == TBaseStructure<FLinearColor>::Get());
	TestTrue(TEXT("Vector2 is an FVector2D"), Nudge != nullptr && Nudge->Struct == TBaseStructure<FVector2D>::Get());
	TestTrue(TEXT("Asset is a hard object reference, not a class"), Picture != nullptr && CastField<FClassProperty>(Picture) == nullptr);
	TestNotNull(TEXT("Class is a class reference"), Kind);
	TestTrue(TEXT("Enum is the enum named"), Look != nullptr && Look->GetEnum() == LookEnum);

	// What a host can do with them.
	for (const TCHAR* Name : { TEXT("Caption"), TEXT("Code"), TEXT("Ratio"), TEXT("Count"), TEXT("bLit"),
		TEXT("Tint"), TEXT("Nudge"), TEXT("Picture"), TEXT("Kind"), TEXT("Look") })
	{
		const FProperty* Property = Class->FindPropertyByName(Name);
		if (!TestNotNull(*FString::Printf(TEXT("'%s' is declared"), Name), Property))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("'%s' is editable"), Name), Property->HasAnyPropertyFlags(CPF_Edit));
		TestFalse(*FString::Printf(TEXT("'%s' is editable on instances"), Name), Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance));
		TestTrue(*FString::Printf(TEXT("'%s' is visible to graphs"), Name), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
		TestFalse(*FString::Printf(TEXT("'%s' is writable from graphs"), Name), Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
		TestTrue(*FString::Printf(TEXT("'%s' is offered on spawn"), Name), Property->HasAnyPropertyFlags(CPF_ExposeOnSpawn));
		TestEqual(*FString::Printf(TEXT("'%s' is not one of the author's own variables"), Name),
			FBlueprintEditorUtils::FindNewVariableIndex(Fixture.Blueprint, FName(Name)), static_cast<int32>(INDEX_NONE));
	}

	// The file's defaults, on the class defaults.
	const UObject* Defaults = Class->GetDefaultObject();
	if (Caption != nullptr)
	{
		TestEqual(TEXT("the Text default"), Caption->GetPropertyValue_InContainer(Defaults).ToString(), FString(TEXT("Language")));
	}
	if (Code != nullptr)
	{
		TestEqual(TEXT("the String default"), Code->GetPropertyValue_InContainer(Defaults), FString(TEXT("en")));
	}
	if (Ratio != nullptr)
	{
		TestEqual(TEXT("the Number default"), Ratio->GetPropertyValue_InContainer(Defaults), 0.5);
	}
	if (Count != nullptr)
	{
		TestEqual(TEXT("the Integer default"), Count->GetPropertyValue_InContainer(Defaults), 3);
	}
	if (Lit != nullptr)
	{
		TestTrue(TEXT("the Bool default"), Lit->GetPropertyValue_InContainer(Defaults));
	}
	if (Tint != nullptr && Tint->Struct == TBaseStructure<FLinearColor>::Get())
	{
		const FLinearColor* Color = Tint->ContainerPtrToValuePtr<FLinearColor>(Defaults);
		TestTrue(TEXT("the Color default"), Color != nullptr && Color->Equals(FLinearColor(1.0f, 0.0f, 0.0f, 1.0f)));
	}
	if (Nudge != nullptr && Nudge->Struct == TBaseStructure<FVector2D>::Get())
	{
		const FVector2D* Vector = Nudge->ContainerPtrToValuePtr<FVector2D>(Defaults);
		TestTrue(TEXT("the Vector2 default"), Vector != nullptr && Vector->Equals(FVector2D(4.0, 8.0)));
	}
	if (Look != nullptr && Look->GetUnderlyingProperty() != nullptr)
	{
		const int64 Value = Look->GetUnderlyingProperty()->GetSignedIntPropertyValue(Look->ContainerPtrToValuePtr<void>(Defaults));
		TestEqual(TEXT("the Enum default"), Value, static_cast<int64>(EDreamWidgetVisibility::Collapsed));
	}

	// Declared afresh by every compile, as the widget variables are: a second compile has each once, and no more.
	FCompilerResultsLog SecondResults;
	Compile(Fixture.Blueprint, SecondResults);
	TestEqual(TEXT("the recompile is clean"), SecondResults.NumErrors, 0);
	int32 CaptionCount = 0;
	for (const FBPVariableDescription& Variable : Fixture.Blueprint->GeneratedVariables)
	{
		CaptionCount += Variable.VarName == FName(TEXT("Caption")) ? 1 : 0;
	}
	TestEqual(TEXT("one generated variable per prop, not one per compile"), CaptionCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxPropBindingTest,
	"DreamGUI.Text.Syntax.ABindingCanReadAPropTheSameFileDeclares",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `Text <- Caption`, where Caption is a prop of the same file -- the line every pure-.dui component is made of.
 *
 * The prop has to be declared before the expression is lowered, because lowering it is asking the class for a
 * variable called Caption; on the class's first compile there is no previous one to ask. And the binding has to
 * drive the widget with the prop's value on a live instance, which is the half a compile can pass without.
 */
bool FDreamUISyntaxPropBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxCompileTestLocal;

	FScopedDuiFile File(TEXT("SyntaxPropBinding.dui"));
	FScopedBlueprint Fixture(TEXT("BP_SyntaxPropBinding"));
	FCompilerResultsLog Results;
	if (!WriteAndCompile(*this, File, {
		TEXT("class /Temp/DreamGUITests/BP_SyntaxPropBinding"),
		TEXT("props {"),
		TEXT("    Text Caption = \"Hello\""),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("        Text <- Caption"),
		TEXT("    }"),
		TEXT("}")}, Fixture, Results))
	{
		return false;
	}
	TestEqual(*FString::Printf(TEXT("a binding to a prop compiles on the class's first compile, saw [%s]"), *JoinMessages(Results)),
		Results.NumErrors, 0);

	UClass* Class = Fixture.Blueprint->GeneratedClass;
	if (!TestNotNull(TEXT("a class came out"), Class))
	{
		return false;
	}
	TArray<FDreamWidgetPropertyBinding> Bindings;
	UDreamWidgetGeneratedClass::CollectPropertyBindings(Class, Bindings);
	TestTrue(TEXT("the binding resolved onto the title's text"), Bindings.ContainsByPredicate([](const FDreamWidgetPropertyBinding& InBinding)
	{
		return InBinding.WidgetName == FName(TEXT("Title")) && InBinding.PropertyName == FName(TEXT("Text"));
	}));

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UDreamUserWidget* Instance = CreateDreamWidget(World, Class);
	if (TestNotNull(TEXT("the class instantiates"), Instance))
	{
		UDreamWidget* Title = Instance->GetWidgetTree() != nullptr
			? Instance->GetWidgetTree()->FindWidgetByVariableName(FName(TEXT("Title"))) : nullptr;
		UDreamText* TitleText = Title != nullptr ? Cast<UDreamText>(Title->GetVisual()) : nullptr;
		if (TestNotNull(TEXT("the instance has the title's text"), TitleText))
		{
			TestEqual(TEXT("the binding drove it with the prop's value"), TitleText->GetText().ToString(), FString(TEXT("Hello")));
		}
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxEventsCompileTest,
	"DreamGUI.Text.Syntax.AnEventsBlockCompilesIntoDispatchersWithTheirParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `events { Picked(Integer Index, Text Label)  Closed }` -- two Event Dispatchers, as if made in the Blueprint
 * editor: multicast delegate members a host can bind (assignable) and this class can call (callable), whose
 * signatures carry the parameters in order. Generated, not authored -- no NewVariables entry -- and their signature
 * graphs are transient, so a save never keeps a graph the next compile would make again. A second compile has them
 * once each.
 */
bool FDreamUISyntaxEventsCompileTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxCompileTestLocal;

	FScopedDuiFile File(TEXT("SyntaxEvents.dui"));
	FScopedBlueprint Fixture(TEXT("BP_SyntaxEvents"));
	FCompilerResultsLog Results;
	if (!WriteAndCompile(*this, File, {
		TEXT("class /Temp/DreamGUITests/BP_SyntaxEvents"),
		TEXT("events {"),
		TEXT("    Picked(Integer Index, Text Label)"),
		TEXT("    Closed"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("    }"),
		TEXT("}")}, Fixture, Results))
	{
		return false;
	}
	TestEqual(*FString::Printf(TEXT("the events compile clean, saw [%s]"), *JoinMessages(Results)), Results.NumErrors, 0);

	auto CheckDispatchers = [this, &Fixture](const TCHAR* InWhen)
	{
		UClass* Class = Fixture.Blueprint->GeneratedClass;
		const FMulticastDelegateProperty* Picked = Class != nullptr ? FindFProperty<FMulticastDelegateProperty>(Class, TEXT("Picked")) : nullptr;
		const FMulticastDelegateProperty* Closed = Class != nullptr ? FindFProperty<FMulticastDelegateProperty>(Class, TEXT("Closed")) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s: Picked is a dispatcher"), InWhen), Picked)
			|| !TestNotNull(*FString::Printf(TEXT("%s: Closed is a dispatcher"), InWhen), Closed))
		{
			return;
		}
		TestTrue(*FString::Printf(TEXT("%s: a host can bind it and the class can call it"), InWhen),
			Picked->HasAllPropertyFlags(CPF_BlueprintAssignable | CPF_BlueprintCallable));
		const UFunction* Signature = Picked->SignatureFunction;
		if (!TestNotNull(*FString::Printf(TEXT("%s: Picked has a signature"), InWhen), Signature))
		{
			return;
		}
		TArray<const FProperty*> SignatureParameters;
		for (TFieldIterator<FProperty> It(Signature); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			SignatureParameters.Add(*It);
		}
		if (TestEqual(*FString::Printf(TEXT("%s: Picked takes two parameters"), InWhen), SignatureParameters.Num(), 2))
		{
			TestTrue(*FString::Printf(TEXT("%s: the first is the Integer Index"), InWhen),
				CastField<FIntProperty>(SignatureParameters[0]) != nullptr && SignatureParameters[0]->GetFName() == FName(TEXT("Index")));
			TestTrue(*FString::Printf(TEXT("%s: the second is the Text Label"), InWhen),
				CastField<FTextProperty>(SignatureParameters[1]) != nullptr && SignatureParameters[1]->GetFName() == FName(TEXT("Label")));
		}
		TestTrue(*FString::Printf(TEXT("%s: Closed takes nothing"), InWhen),
			Closed->SignatureFunction != nullptr && Closed->SignatureFunction->NumParms == 0);
		TestEqual(*FString::Printf(TEXT("%s: Picked is not one of the author's own variables"), InWhen),
			FBlueprintEditorUtils::FindNewVariableIndex(Fixture.Blueprint, FName(TEXT("Picked"))), static_cast<int32>(INDEX_NONE));
		for (const UEdGraph* Graph : Fixture.Blueprint->DelegateSignatureGraphs)
		{
			TestTrue(*FString::Printf(TEXT("%s: every signature graph in the asset is transient"), InWhen),
				Graph == nullptr || Graph->HasAnyFlags(RF_Transient));
		}
	};
	CheckDispatchers(TEXT("first compile"));

	FCompilerResultsLog SecondResults;
	Compile(Fixture.Blueprint, SecondResults);
	TestEqual(TEXT("the recompile is clean"), SecondResults.NumErrors, 0);
	CheckDispatchers(TEXT("second compile"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxEmitTest,
	"DreamGUI.Text.Syntax.AnEmitRouteCompilesIntoAHandlerThatBroadcastsTheDispatcher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `OnPoked -> emit Picked(Index)`: a behaviour's event raising the file's own dispatcher, with a prop for its
 * argument. The handler is generated with the source event's signature (none, for OnPoked), the route is recorded
 * against it like any `->`, and on a live instance firing the behaviour reaches a listener bound to the dispatcher
 * with the prop's value -- which is the only assertion here that every earlier stage could not pass without.
 */
bool FDreamUISyntaxEmitTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxCompileTestLocal;

	FScopedDuiFile File(TEXT("SyntaxEmit.dui"));
	FScopedBlueprint Fixture(TEXT("BP_SyntaxEmit"));
	FCompilerResultsLog Results;
	if (!WriteAndCompile(*this, File, {
		TEXT("class /Temp/DreamGUITests/BP_SyntaxEmit"),
		TEXT("props {"),
		TEXT("    Integer Index = 7"),
		TEXT("}"),
		TEXT("events {"),
		TEXT("    Picked(Integer Index)"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Widget Button {"),
		// The behaviour's reflected path, asked of the class: it lives in whichever module holds this test.
		FString::Printf(TEXT("        + %s {"), *UDreamUIEventTestBehaviour::StaticClass()->GetPathName()),
		TEXT("            OnPoked -> emit Picked(Index)"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")}, Fixture, Results))
	{
		return false;
	}
	TestEqual(*FString::Printf(TEXT("the emit route compiles clean, saw [%s]"), *JoinMessages(Results)), Results.NumErrors, 0);

	UClass* Class = Fixture.Blueprint->GeneratedClass;
	if (!TestNotNull(TEXT("a class came out"), Class))
	{
		return false;
	}

	// The handler: generated, named after the node and the event it leaves from, taking what OnPoked sends.
	const FName HandlerName(*FString::Printf(TEXT("%sButton_OnPoked"), DreamUIExpressionThunks::GeneratedEmitPrefix));
	const UFunction* Handler = Class->FindFunctionByName(HandlerName);
	if (!TestNotNull(TEXT("the handler is a function of the class"), Handler))
	{
		return false;
	}
	TestEqual(TEXT("it takes what OnPoked sends, which is nothing"), static_cast<int32>(Handler->NumParms), 0);

	TArray<FDreamWidgetEventBinding> Routes;
	UDreamWidgetGeneratedClass::CollectEventBindings(Class, Routes);
	TestTrue(TEXT("the route is recorded against the handler, as any route is"), Routes.ContainsByPredicate(
		[&HandlerName](const FDreamWidgetEventBinding& InRoute)
		{
			return InRoute.FunctionName == HandlerName && InRoute.EventName == FName(TEXT("OnPoked"));
		}));

	const FMulticastDelegateProperty* Dispatcher = FindFProperty<FMulticastDelegateProperty>(Class, TEXT("Picked"));
	if (!TestNotNull(TEXT("the dispatcher it raises is declared"), Dispatcher))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UDreamUserWidget* Instance = CreateDreamWidget(World, Class);
	if (TestNotNull(TEXT("the class instantiates"), Instance))
	{
		// A listener on the dispatcher, the way a host's route or a Bind Event node attaches one.
		TStrongObjectPtr<UDreamEventBindingTestHandler> Listener(NewObject<UDreamEventBindingTestHandler>());
		FScriptDelegate Delegate;
		Delegate.BindUFunction(Listener.Get(), GET_FUNCTION_NAME_CHECKED(UDreamEventBindingTestHandler, HandleInt));
		Dispatcher->AddDelegate(MoveTemp(Delegate), Instance);

		UDreamUIEventTestBehaviour* Poker = FindBehaviour<UDreamUIEventTestBehaviour>(Instance->GetContentRoot());
		if (TestNotNull(TEXT("the behaviour is on the live hierarchy"), Poker))
		{
			TestEqual(TEXT("nothing has fired yet"), Listener->IntCallCount, 0);
			Poker->Poke();
			TestEqual(TEXT("firing the source event raised the dispatcher once"), Listener->IntCallCount, 1);
			TestEqual(TEXT("with the prop's value for its argument"), Listener->LastInt, 7);
		}
	}
	World->DestroyWorld(false);

	// Regenerated, not accumulated: one handler after a second compile, under the same name.
	FCompilerResultsLog SecondResults;
	Compile(Fixture.Blueprint, SecondResults);
	TestEqual(TEXT("the recompile is clean"), SecondResults.NumErrors, 0);
	int32 HandlerGraphs = 0;
	for (const UEdGraph* Graph : Fixture.Blueprint->FunctionGraphs)
	{
		HandlerGraphs += Graph != nullptr && Graph->GetName().StartsWith(DreamUIExpressionThunks::GeneratedEmitPrefix) ? 1 : 0;
	}
	TestEqual(TEXT("still exactly one handler graph"), HandlerGraphs, 1);
	TestNotNull(TEXT("under its deterministic name"), Fixture.Blueprint->GeneratedClass->FindFunctionByName(HandlerName));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxAnonymousWidgetTest,
	"DreamGUI.Text.Syntax.AnAnonymousWidgetsVariableExistsButIsHidden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A widget nobody named still needs its class variable -- the run time finds the widget of every binding and route
 * through one -- but under a name the parser made up, which moves when the file's shape does. So it is declared and
 * hidden: not visible to graphs (which also keeps it out of My Blueprint's list) and not editable, while a named
 * sibling stays exactly what it always was. The made-up names are read off the parser rather than spelled here, so
 * the test holds whatever rule makes them.
 */
bool FDreamUISyntaxAnonymousWidgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxCompileTestLocal;

	const TArray<FString> Lines = {
		TEXT("class /Temp/DreamGUITests/BP_SyntaxAnonymous"),
		TEXT("Widget Root {"),
		TEXT("    Widget {"),
		TEXT("        Text { Text = \"Status\" }"),
		TEXT("    }"),
		TEXT("    Text Named {"),
		TEXT("    }"),
		TEXT("}")};

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag ParseDiagnostics;
	if (!TestTrue(TEXT("the file parses"), FDreamUISourceFile::Parse(FString::Join(Lines, TEXT("\n")), TEXT("SyntaxAnonymous.dui"), Ast, ParseDiagnostics)))
	{
		return false;
	}
	TArray<FName> AnonymousNames;
	Ast.ForEachNode([&AnonymousNames](const FDreamUINode& InNode)
	{
		if (InNode.bAnonymous)
		{
			AnonymousNames.Add(FName(*UDreamWidgetTree::SanitizeIdentifier(InNode.Id)));
		}
	});
	TestEqual(TEXT("the parser made a name for each unnamed widget"), AnonymousNames.Num(), 2);

	FScopedDuiFile File(TEXT("SyntaxAnonymous.dui"));
	FScopedBlueprint Fixture(TEXT("BP_SyntaxAnonymous"));
	FCompilerResultsLog Results;
	if (!WriteAndCompile(*this, File, Lines, Fixture, Results))
	{
		return false;
	}
	TestEqual(*FString::Printf(TEXT("unnamed widgets compile clean, saw [%s]"), *JoinMessages(Results)), Results.NumErrors, 0);
	UClass* Class = Fixture.Blueprint->GeneratedClass;
	if (!TestNotNull(TEXT("a class came out"), Class))
	{
		return false;
	}

	for (const FName Name : AnonymousNames)
	{
		const FProperty* Property = Class->FindPropertyByName(Name);
		if (!TestNotNull(*FString::Printf(TEXT("'%s' has its class variable"), *Name.ToString()), Property))
		{
			continue;
		}
		TestFalse(*FString::Printf(TEXT("'%s' is not visible to graphs or My Blueprint"), *Name.ToString()),
			Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
		TestFalse(*FString::Printf(TEXT("'%s' is not editable"), *Name.ToString()), Property->HasAnyPropertyFlags(CPF_Edit));
	}
	const FProperty* Named = Class->FindPropertyByName(TEXT("Named"));
	TestTrue(TEXT("a named widget's variable is as visible as ever"), Named != nullptr && Named->HasAnyPropertyFlags(CPF_BlueprintVisible));

	// And the hidden variable is bound on an instance like any other: that is what it exists for.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UDreamUserWidget* Instance = CreateDreamWidget(World, Class);
	if (TestNotNull(TEXT("the class instantiates"), Instance))
	{
		for (const FName Name : AnonymousNames)
		{
			const FObjectPropertyBase* Property = CastField<FObjectPropertyBase>(Class->FindPropertyByName(Name));
			TestTrue(*FString::Printf(TEXT("'%s' points at its widget on the instance"), *Name.ToString()),
				Property != nullptr && Property->GetObjectPropertyValue_InContainer(Instance) != nullptr);
		}
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxSourceClassResolverTest,
	"DreamGUI.Text.Syntax.TheSourceClassResolverFindsTheBlueprintBuiltFromAFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `use "Row.dui" as Row` on a file with no `class` line means the class of the Blueprint whose Source File is
 * Row.dui, and only the editor knows which that is. The editor module installs the answer into the builder; this
 * asks it, through the builder's own hook, for a file whose Blueprint is loaded and compiled.
 */
bool FDreamUISyntaxSourceClassResolverTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxCompileTestLocal;

	FScopedDuiFile File(TEXT("SyntaxResolver.dui"));
	FScopedBlueprint Fixture(TEXT("BP_SyntaxResolver"));
	FCompilerResultsLog Results;
	// No `class` line: the case the resolver exists for.
	if (!WriteAndCompile(*this, File, {
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("    }"),
		TEXT("}")}, Fixture, Results))
	{
		return false;
	}
	TestEqual(TEXT("the component compiles clean"), Results.NumErrors, 0);

	TFunction<UClass*(const FString&)>& Resolver = FDreamUITextBuilder::SourceClassResolver();
	if (!TestTrue(TEXT("the editor module installed a resolver"), static_cast<bool>(Resolver)))
	{
		return false;
	}
	TestEqual(TEXT("it answers the file with the Blueprint built from it"),
		Resolver(File.FilePath), static_cast<UClass*>(Fixture.Blueprint->GeneratedClass));
	// Asked again, from what it remembered: the same answer, checked afresh rather than trusted.
	TestEqual(TEXT("and gives the same answer the second time"),
		FDreamUISourceWatcher::FindClassForSource(File.FilePath), static_cast<UClass*>(Fixture.Blueprint->GeneratedClass));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxDiagnosticsTest,
	"DreamGUI.Text.Syntax.EveryPropsEventsAndEmitMistakeIsACompileError",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * One file per mistake, each failing its compile under its own code -- the code is what an editor keys its fix on,
 * and a mistake reported under a neighbour's code sends the author to the wrong line of the language reference.
 */
bool FDreamUISyntaxDiagnosticsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxCompileTestLocal;

	const FString PokerPath = UDreamUIEventTestBehaviour::StaticClass()->GetPathName();
	struct FCase
	{
		const TCHAR* Name;
		EDreamUIDiagnosticCode Code;
		TArray<FString> Lines;
	};
	const TArray<FCase> Cases = {
		// A type the language does not have.
		{ TEXT("PropType"), EDreamUIDiagnosticCode::PropTypeUnknown, {
			TEXT("props {"),
			TEXT("    Banana Peel"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("}") } },
		// A prop named like a widget of the same file: two members of one name.
		{ TEXT("PropName"), EDreamUIDiagnosticCode::PropNameTaken, {
			TEXT("props {"),
			TEXT("    Text Title"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("    Text Title {"),
			TEXT("    }"),
			TEXT("}") } },
		// A default the type cannot hold.
		{ TEXT("PropDefault"), EDreamUIDiagnosticCode::PropDefaultInvalid, {
			TEXT("props {"),
			TEXT("    Integer Count = 1.5"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("}") } },
		// An event named like a prop.
		{ TEXT("EventName"), EDreamUIDiagnosticCode::EventNameTaken, {
			TEXT("props {"),
			TEXT("    Integer Picked"),
			TEXT("}"),
			TEXT("events {"),
			TEXT("    Picked"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("}") } },
		// An emit of an event this file does not declare.
		{ TEXT("EmitUnknown"), EDreamUIDiagnosticCode::EmitUnknownEvent, {
			TEXT("events {"),
			TEXT("    Picked"),
			TEXT("}"),
			TEXT("Widget Root {"),
			FString::Printf(TEXT("    + %s {"), *PokerPath),
			TEXT("        OnPoked -> emit Dropped()"),
			TEXT("    }"),
			TEXT("}") } },
		// An emit passing fewer arguments than the event takes.
		{ TEXT("EmitCount"), EDreamUIDiagnosticCode::EmitArgumentMismatch, {
			TEXT("events {"),
			TEXT("    Picked(Integer Index)"),
			TEXT("}"),
			TEXT("Widget Root {"),
			FString::Printf(TEXT("    + %s {"), *PokerPath),
			TEXT("        OnPoked -> emit Picked()"),
			TEXT("    }"),
			TEXT("}") } },
		// And one passing an argument of the wrong type.
		{ TEXT("EmitType"), EDreamUIDiagnosticCode::EmitArgumentMismatch, {
			TEXT("events {"),
			TEXT("    Picked(Integer Index)"),
			TEXT("}"),
			TEXT("Widget Root {"),
			FString::Printf(TEXT("    + %s {"), *PokerPath),
			TEXT("        OnPoked -> emit Picked(\"seven\")"),
			TEXT("    }"),
			TEXT("}") } },
	};

	for (const FCase& Case : Cases)
	{
		const FString CodeText = FDreamUIDiagnostic::CodeToString(Case.Code);
		AddExpectedError(CodeText, EAutomationExpectedErrorFlags::Contains, 0);

		FScopedDuiFile File(*FString::Printf(TEXT("SyntaxMistake%s.dui"), Case.Name));
		FScopedBlueprint Fixture(*FString::Printf(TEXT("BP_SyntaxMistake%s"), Case.Name));
		FCompilerResultsLog Results;
		if (!WriteAndCompile(*this, File, Case.Lines, Fixture, Results))
		{
			continue;
		}
		const FString Messages = JoinMessages(Results);
		TestTrue(*FString::Printf(TEXT("%s fails the compile"), Case.Name), Results.NumErrors > 0);
		TestTrue(*FString::Printf(TEXT("%s is reported as %s, saw [%s]"), Case.Name, *CodeText, *Messages), Messages.Contains(CodeText));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxHostBindsAPropTest,
	"DreamGUI.Text.Syntax.AHostCanBindAComponentsPropThoughItHasNoSetter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `Caption <- GetTitleText()` on an instance of a pure-.dui component, from its host. Caption is a `props` entry -- a
 * Blueprint variable, so no SetCaption exists -- and the binding is the one the setter rule lets through: the run time
 * writes the variable into the instance and announces it, and the component's own `Text <- Caption` shows it.
 */
bool FDreamUISyntaxHostBindsAPropTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxCompileTestLocal;

	FScopedDuiFile CardFile(TEXT("SyntaxHostedCard.dui"));
	FScopedBlueprint Card(TEXT("BP_SyntaxHostedCard"));
	FCompilerResultsLog CardResults;
	if (!WriteAndCompile(*this, CardFile, {
		TEXT("class /Temp/DreamGUITests/BP_SyntaxHostedCard"),
		TEXT("props {"),
		TEXT("    Text Caption = \"unset\""),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("        Text <- Caption"),
		TEXT("    }"),
		TEXT("}")}, Card, CardResults))
	{
		return false;
	}
	TestEqual(*FString::Printf(TEXT("the component compiles, saw [%s]"), *JoinMessages(CardResults)), CardResults.NumErrors, 0);

	FScopedDuiFile HostFile(TEXT("SyntaxPropHost.dui"));
	FScopedBlueprint Host(TEXT("BP_SyntaxPropHost"));
	FCompilerResultsLog HostResults;
	if (!WriteAndCompile(*this, HostFile, {
		TEXT("class /Temp/DreamGUITests/BP_SyntaxPropHost"),
		FString::Printf(TEXT("use \"%s\" as Card"), *CardFile.FilePath),
		TEXT("Widget Root {"),
		TEXT("    Card Item {"),
		TEXT("        Caption <- GetTitleText()"),
		TEXT("    }"),
		TEXT("}")}, Host, HostResults))
	{
		return false;
	}
	TestEqual(*FString::Printf(TEXT("the host binds the prop although it has no setter, saw [%s]"), *JoinMessages(HostResults)),
		HostResults.NumErrors, 0);

	UClass* HostClass = Host.Blueprint->GeneratedClass;
	if (!TestNotNull(TEXT("the host's class came out"), HostClass))
	{
		return false;
	}
	TArray<FDreamWidgetPropertyBinding> Bindings;
	UDreamWidgetGeneratedClass::CollectPropertyBindings(HostClass, Bindings);
	const FDreamWidgetPropertyBinding* Binding = Bindings.FindByPredicate([](const FDreamWidgetPropertyBinding& InBinding)
	{
		return InBinding.WidgetName == FName(TEXT("Item")) && InBinding.PropertyName == FName(TEXT("Caption"));
	});
	if (TestNotNull(TEXT("the binding was recorded"), Binding))
	{
		TestTrue(TEXT("as a direct write: no setter named"), Binding->SetterName.IsNone());
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UDreamUserWidget* Instance = CreateDreamWidget(World, HostClass);
	if (TestNotNull(TEXT("the host instantiates"), Instance))
	{
		Instance->EvaluatePropertyBindings();
		UDreamUserWidget* Item = Instance->GetWidgetTree() != nullptr
			? Cast<UDreamUserWidget>(Instance->GetWidgetTree()->FindWidgetByVariableName(FName(TEXT("Item")))) : nullptr;
		if (TestNotNull(TEXT("the host has the component instance"), Item))
		{
			const FTextProperty* Caption = FindFProperty<FTextProperty>(Item->GetClass(), TEXT("Caption"));
			if (TestNotNull(TEXT("the instance has the prop"), Caption))
			{
				TestEqual(TEXT("the host's binding wrote it"), Caption->GetPropertyValue_InContainer(Item).ToString(), FString(TEXT("bound")));
			}
			Item->EvaluatePropertyBindings();
			UDreamWidget* Title = Item->GetWidgetTree() != nullptr
				? Item->GetWidgetTree()->FindWidgetByVariableName(FName(TEXT("Title"))) : nullptr;
			UDreamText* TitleText = Title != nullptr ? Cast<UDreamText>(Title->GetVisual()) : nullptr;
			if (TestNotNull(TEXT("the component has its title"), TitleText))
			{
				TestEqual(TEXT("and its own binding shows the value"), TitleText->GetText().ToString(), FString(TEXT("bound")));
			}
		}
	}
	World->DestroyWorld(false);
	return true;
}

#endif
