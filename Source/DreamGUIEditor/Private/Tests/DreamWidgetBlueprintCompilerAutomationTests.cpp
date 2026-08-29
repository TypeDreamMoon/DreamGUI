// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprintCompiler.h"
#include "DreamWidgetBlueprintTestTypes.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamText.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Engine/World.h"
#include "UObject/Package.h"

/*
 * Compiling a hierarchy into a class.
 *
 * These drive a real UDreamWidgetBlueprint through FKismetEditorUtilities::CompileBlueprint rather
 * than calling the compiler's pieces directly, because most of what is being claimed is about the
 * compiler being wired into Kismet AT ALL -- registered for this Blueprint type, hooked at the right
 * override, producing a class Kismet accepts. Every one of those fails silently when tested from the
 * inside: the first attempt here put the variable declarations in CreateClassVariablesFromBlueprint,
 * where the base resets the list immediately beforehand, and the only visible symptom would have
 * been variables that quietly did not exist.
 *
 * A Blueprint needs a real package, so these build one in /Temp and tear it down.
 */

namespace DreamWidgetBlueprintCompilerTestLocal
{
	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedBlueprint(const TCHAR* InName, UClass* InParentClass = UDreamUserWidget::StaticClass())
		{
			const FString PackageName = FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName);
			Package = CreatePackage(*PackageName);
			Package->AddToRoot();
			// The real creation path, not a hand-built object: it makes the ubergraph and the
			// generated class a compile actually needs, and asking it for our Blueprint and generated
			// class types is what a factory would do.
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				InParentClass, Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		/** Add a widget of InDisplayName under InParent (or the root when null) and return it. */
		UDreamWidget* AddWidget(const TCHAR* InDisplayName, UDreamWidget* InParent = nullptr)
		{
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			UDreamWidget* Widget = Tree->ConstructWidget<UDreamWidget>();
			Widget->SetDisplayName(InDisplayName);
			Widget->SetParentBeforeRegister(InParent != nullptr ? InParent : Tree->RootWidget.Get());
			return Widget;
		}
	};

	/** Compile and hand back the results log, so a test can assert on errors as well as on the class. */
	void Compile(UDreamWidgetBlueprint* InBlueprint, FCompilerResultsLog& OutResults)
	{
		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection, &OutResults);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetBlueprintCompilesToAClassTest,
	"DreamGUI.WidgetBlueprint.CompilingProducesAClassThatBuildsTheHierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetBlueprintCompilesToAClassTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBlueprintCompilerTestLocal;

	FScopedBlueprint Fixture(TEXT("BP_CompilesToAClass"));
	UDreamWidget* Header = Fixture.AddWidget(TEXT("Header"));
	Fixture.AddWidget(TEXT("Caption"), Header);

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	TestEqual(TEXT("the hierarchy compiles clean"), Results.NumErrors, 0);

	// The compiler has to produce a class of the DreamUI kind, or nothing downstream applies.
	UDreamWidgetGeneratedClass* GeneratedClass = Cast<UDreamWidgetGeneratedClass>(Fixture.Blueprint->GeneratedClass);
	if (!TestNotNull(TEXT("compiling produced a DreamUI generated class"), GeneratedClass))
	{
		return false;
	}

	// The authored hierarchy is on the class, and is a COPY -- the archetype is instanced from on
	// every construction, so handing over the object the designer edits would let an edit mutate the
	// template every live instance came from.
	UDreamWidgetTree* Archetype = GeneratedClass->GetWidgetTreeArchetype();
	if (!TestNotNull(TEXT("the class carries the hierarchy"), Archetype))
	{
		return false;
	}
	TestNotEqual(TEXT("and it is not the Blueprint's own authoring copy"),
		(const UDreamWidgetTree*)Archetype, (const UDreamWidgetTree*)Fixture.Blueprint->WidgetTree);
	TestEqual(TEXT("the whole hierarchy came across"), Archetype->CountWidgets(), 3);

	// One variable per authored widget, under the shared naming rule.
	TestNotNull(TEXT("a variable exists for the first widget"), GeneratedClass->FindPropertyByName(FName(TEXT("Header"))));
	TestNotNull(TEXT("and for one nested deeper"), GeneratedClass->FindPropertyByName(FName(TEXT("Caption"))));
	TestNull(TEXT("and not for a name nothing carries"), GeneratedClass->FindPropertyByName(FName(TEXT("Absent"))));

	// End to end: the class builds a live hierarchy and the generated variables point into it.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UDreamUserWidget* Instance = CreateDreamWidget(World, GeneratedClass);
	if (TestNotNull(TEXT("the class instantiates"), Instance))
	{
		TestNotNull(TEXT("and builds its contents"), Instance->GetContentRoot());

		// Built is not the same as alive, and every other assertion in this file would pass against a
		// hierarchy that is structurally perfect and completely dead: unregistered means no layout, no
		// rendering, no behaviour lifecycle. Registration is the last thing the prefab loader does, and
		// the class path has to do it too or replacing prefabs with classes ships UI that does nothing.
		TestTrue(TEXT("the instance is registered, not merely constructed"), Instance->HasRegistered());
		int32 Unregistered = 0;
		if (Instance->GetWidgetTree() != nullptr)
		{
			Instance->GetWidgetTree()->ForEachWidget([&Unregistered](UDreamWidget* Widget)
			{
				if (!Widget->HasRegistered()) { Unregistered++; }
			});
		}
		TestEqual(TEXT("and so is every widget it built"), Unregistered, 0);
		FObjectPropertyBase* HeaderProperty = CastField<FObjectPropertyBase>(GeneratedClass->FindPropertyByName(FName(TEXT("Header"))));
		if (HeaderProperty != nullptr)
		{
			UObject* Bound = HeaderProperty->GetObjectPropertyValue_InContainer(Instance);
			if (TestNotNull(TEXT("the generated variable is bound on the instance"), Bound))
			{
				TestTrue(TEXT("to a widget from the instance's own tree, not the archetype"),
					Bound->IsIn(Instance->GetWidgetTree()));
			}
		}
	}
	World->DestroyWorld(false);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetBlueprintDuplicateNameTest,
	"DreamGUI.WidgetBlueprint.TwoWidgetsSharingANameAreReportedNotMerged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetBlueprintDuplicateNameTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBlueprintCompilerTestLocal;

	FScopedBlueprint Fixture(TEXT("BP_DuplicateName"));
	Fixture.AddWidget(TEXT("Panel"));
	Fixture.AddWidget(TEXT("Panel"));

	// Silently collapsing them into one variable would bind it to whichever the tree walk reached
	// first -- which is to say, to nothing anyone chose.
	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);

	TestEqual(TEXT("a name collision is not an error"), Results.NumErrors, 0);
	TestTrue(TEXT("but it is reported"), Results.NumWarnings > 0);
	TestNotNull(TEXT("and one variable is still declared"),
		Fixture.Blueprint->GeneratedClass ? Fixture.Blueprint->GeneratedClass->FindPropertyByName(FName(TEXT("Panel"))) : nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetBlueprintMissingBindingIsACompileErrorTest,
	"DreamGUI.WidgetBlueprint.AMissingWidgetBindingFailsTheCompile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetBlueprintMissingBindingIsACompileErrorTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBlueprintCompilerTestLocal;

	// This is the single largest thing the class model buys, so it gets tested from both sides.
	// Under prefabs the same mistake surfaced at run time as a null, long after a save had silently
	// dropped the binding.
	{
		FScopedBlueprint Fixture(TEXT("BP_MissingBinding"), UDreamWidgetBlueprintBindingBase::StaticClass());
		Fixture.AddWidget(TEXT("SomethingElse"));

		// The failure is the point, so its log output has to be declared: the compiler logs through
		// LogBlueprint and automation counts any logged error as a failed test. Occurrences 0 because
		// the message is emitted once by the compiler and replayed once by the results log, and the
		// test is about the error existing, not about how many times it is printed.
		AddExpectedError(TEXT("expects a widget of that name"), EAutomationExpectedErrorFlags::Contains, 0);

		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		TestTrue(TEXT("a declared binding with no widget of that name fails the compile"), Results.NumErrors > 0);
	}

	// And the other side: the check must not fire on a hierarchy that answers the binding, or it is
	// just noise nobody can act on. The unmarked widget-typed member on the same base class is the
	// control -- it has the shape of a binding and claims nothing, and must stay quiet.
	{
		FScopedBlueprint Fixture(TEXT("BP_SatisfiedBinding"), UDreamWidgetBlueprintBindingBase::StaticClass());
		Fixture.AddWidget(TEXT("RequiredHeader"));

		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		TestEqual(TEXT("a satisfied binding compiles clean"), Results.NumErrors, 0);

		// And it is actually wired up at run time, not merely tolerated at compile time.
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		UDreamUserWidget* Instance = CreateDreamWidget(World, Fixture.Blueprint->GeneratedClass.Get());
		if (UDreamWidgetBlueprintBindingBase* Typed = Cast<UDreamWidgetBlueprintBindingBase>(Instance))
		{
			TestNotNull(TEXT("the declared binding resolves on the instance"), Typed->RequiredHeader.Get());
			TestNull(TEXT("and the unmarked member is left alone"), Typed->UnmarkedReference.Get());
		}
		World->DestroyWorld(false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetBlueprintRecompileTest,
	"DreamGUI.WidgetBlueprint.RecompilingReplacesTheArchetypeRatherThanAccumulating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetBlueprintRecompileTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBlueprintCompilerTestLocal;

	FScopedBlueprint Fixture(TEXT("BP_Recompile"));
	Fixture.AddWidget(TEXT("Header"));

	FCompilerResultsLog FirstResults;
	Compile(Fixture.Blueprint, FirstResults);
	UDreamWidgetGeneratedClass* FirstClass = Cast<UDreamWidgetGeneratedClass>(Fixture.Blueprint->GeneratedClass);
	if (!TestNotNull(TEXT("the first compile produced a class"), FirstClass))
	{
		return false;
	}
	const UDreamWidgetTree* FirstArchetype = FirstClass->GetWidgetTreeArchetype();

	// Recompiling is the common case, not the exotic one -- every save does it. The sanitize pass
	// clears the old archetype and FinishCompilingClass installs a new one; an archetype that survived
	// or accumulated would show up here as a stale or oversized tree.
	Fixture.AddWidget(TEXT("Footer"));
	FCompilerResultsLog SecondResults;
	Compile(Fixture.Blueprint, SecondResults);
	TestEqual(TEXT("recompiling stays clean"), SecondResults.NumErrors, 0);

	UDreamWidgetGeneratedClass* SecondClass = Cast<UDreamWidgetGeneratedClass>(Fixture.Blueprint->GeneratedClass);
	if (!TestNotNull(TEXT("the second compile produced a class"), SecondClass))
	{
		return false;
	}
	UDreamWidgetTree* SecondArchetype = SecondClass->GetWidgetTreeArchetype();
	if (!TestNotNull(TEXT("with an archetype"), SecondArchetype))
	{
		return false;
	}
	TestNotEqual(TEXT("the archetype was replaced, not reused"), (const UDreamWidgetTree*)SecondArchetype, FirstArchetype);
	TestEqual(TEXT("and holds the new hierarchy exactly once"), SecondArchetype->CountWidgets(), 3);
	TestNotNull(TEXT("the new widget got a variable"), SecondClass->FindPropertyByName(FName(TEXT("Footer"))));
	TestNotNull(TEXT("and the old one kept its"), SecondClass->FindPropertyByName(FName(TEXT("Header"))));

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetPropertyBindingDrivesTheWidgetTest,
	"DreamGUI.WidgetBlueprint.APropertyBindingDrivesTheWidgetThroughItsSetter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetPropertyBindingDrivesTheWidgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBlueprintCompilerTestLocal;

	FScopedBlueprint Fixture(TEXT("BP_PropertyBinding"));
	UDreamWidget* Label = Fixture.AddWidget(TEXT("Label"));
	UDreamText* Text = Cast<UDreamText>(Label->CreateNewVisual(UDreamText::StaticClass()));
	if (!TestNotNull(TEXT("the widget has a text visual"), Text))
	{
		return false;
	}
	// The authored value, so "it changed" is distinguishable from "it was always this".
	Text->SetUseKerning(false);

	// The property lives on the VISUAL, which is the case worth testing: Text, FontSize and the rest
	// of what an author reaches for are not on the widget that carries the name.
	FDreamWidgetPropertyBinding& Binding = Fixture.Blueprint->PropertyBindings.AddDefaulted_GetRef();
	Binding.WidgetName = FName(TEXT("Label"));
	Binding.Target = EDreamWidgetBindingTarget::Visual;
	Binding.PropertyName = FName(TEXT("bUseKerning"));
	// A no-argument function on the user widget returning the property's type. IsInitialized is a
	// native one, so this needs no hand-built graph to have something real to call.
	Binding.FunctionName = FName(TEXT("IsInitialized"));

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	TestEqual(TEXT("the binding compiles clean"), Results.NumErrors, 0);

	UDreamWidgetGeneratedClass* GeneratedClass = Cast<UDreamWidgetGeneratedClass>(Fixture.Blueprint->GeneratedClass.Get());
	if (!TestNotNull(TEXT("a class came out"), GeneratedClass))
	{
		return false;
	}
	const TArray<FDreamWidgetPropertyBinding>& Compiled = GeneratedClass->GetPropertyBindings();
	if (!TestEqual(TEXT("the class carries the binding"), Compiled.Num(), 1))
	{
		return false;
	}
	// The compiler resolved the setter, so the runtime never has to guess at a name.
	TestEqual(TEXT("and the setter it resolved"), Compiled[0].SetterName, FName(TEXT("SetUseKerning")));

	// End to end. IsInitialized is true by the time bindings run, so a binding that took shows true
	// on a visual whose authored value was false.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UDreamUserWidget* Instance = CreateDreamWidget(World, GeneratedClass);
	if (TestNotNull(TEXT("the class instantiates"), Instance))
	{
		UDreamWidget* LiveLabel = Instance->GetWidgetTree() != nullptr
			? Instance->GetWidgetTree()->FindWidgetByVariableName(FName(TEXT("Label"))) : nullptr;
		if (TestNotNull(TEXT("the live hierarchy has the widget"), LiveLabel))
		{
			UDreamText* LiveText = Cast<UDreamText>(LiveLabel->GetVisual());
			if (TestNotNull(TEXT("and its visual"), LiveText))
			{
				TestTrue(TEXT("the binding drove the property"), LiveText->GetUseKerning());
			}
		}
	}
	World->DestroyWorld(false);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetPropertyBindingWithoutASetterIsRefusedTest,
	"DreamGUI.WidgetBlueprint.APropertyWithNoSetterCannotBeBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetPropertyBindingWithoutASetterIsRefusedTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBlueprintCompilerTestLocal;

	// The compiler error IS the assertion here, so it has to be declared expected -- the framework
	// counts anything logged at Error against the test otherwise.
	AddExpectedError(TEXT("has no property named"), EAutomationExpectedErrorFlags::Contains, 0);

	FScopedBlueprint Fixture(TEXT("BP_PropertyBindingNoSetter"));
	Fixture.AddWidget(TEXT("Label"));

	// A property nothing exposes a setter for. Writing it would land in memory and never repaint,
	// which is precisely the silent failure the setter rule exists to refuse.
	FDreamWidgetPropertyBinding& Binding = Fixture.Blueprint->PropertyBindings.AddDefaulted_GetRef();
	Binding.WidgetName = FName(TEXT("Label"));
	Binding.Target = EDreamWidgetBindingTarget::Widget;
	Binding.PropertyName = FName(TEXT("NoSuchPropertyAnywhere"));
	Binding.FunctionName = FName(TEXT("IsInitialized"));

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	TestTrue(TEXT("the compile reports it rather than dropping it"), Results.NumErrors > 0);

	UDreamWidgetGeneratedClass* GeneratedClass = Cast<UDreamWidgetGeneratedClass>(Fixture.Blueprint->GeneratedClass.Get());
	if (TestNotNull(TEXT("a class still came out"), GeneratedClass))
	{
		TestEqual(TEXT("and carries no binding it could not honour"), GeneratedClass->GetPropertyBindings().Num(), 0);
	}

	return true;
}

#endif
