// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "DreamRuntimeComponentBindingOrderTestTypes.h"
#include "DreamScopedWorld.h"
#include "DreamWidgetBlueprint.h"
#include "Engine/World.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

namespace DreamRuntimeComponentBindingOrderTestLocal
{
	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		FScopedBlueprint()
		{
			const FString Name = TEXT("BP_RuntimeComponentBinding_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
			Package = CreatePackage(*(TEXT("/Temp/DreamGUITests/") + Name));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamRuntimeComponentBindingOrderTestWidget::StaticClass(), Package, FName(*Name), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedBlueprint() { if (Package != nullptr)Package->RemoveFromRoot(); }
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamRuntimeComponentBindingOrderTest,
	"DreamGUI.Binding.RuntimeComponentBindingsKeepTheirAuthoredTargetsAfterInitializationReordering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamRuntimeComponentBindingOrderTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Unchanged order preserves property and event bindings"));
	OutTestCommands.Add(TEXT("Control"));
	OutBeautifiedNames.Add(TEXT("A property binding follows the authored component after reordering"));
	OutTestCommands.Add(TEXT("Property"));
	OutBeautifiedNames.Add(TEXT("An event binding follows the authored component after reordering"));
	OutTestCommands.Add(TEXT("Event"));
	OutBeautifiedNames.Add(TEXT("Removed targets cannot transfer bindings to the next component"));
	OutTestCommands.Add(TEXT("Removed"));
	OutBeautifiedNames.Add(TEXT("Nested initialization preserves the outer blueprint's authored targets"));
	OutTestCommands.Add(TEXT("Nested"));
	OutBeautifiedNames.Add(TEXT("Duplicating a reordered instance preserves its authored targets"));
	OutTestCommands.Add(TEXT("Duplicate"));
}

bool FDreamRuntimeComponentBindingOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamRuntimeComponentBindingOrderTestLocal;
	FScopedBlueprint Scoped;
	if (!TestNotNull(TEXT("the public factory creates a DreamGUI blueprint"), Scoped.Blueprint))return false;
	UDreamWidgetTree* Tree = Scoped.Blueprint->GetOrCreateWidgetTree();
	const bool bNested = Parameters == TEXT("Nested");
	const bool bRemoved = Parameters == TEXT("Removed");
	const bool bDuplicate = Parameters == TEXT("Duplicate");
	UDreamWidget* Subject = bNested
		? Tree->ConstructWidget<UDreamRuntimeComponentBindingOrderNestedWidget>()
		: Tree->ConstructWidget<UDreamWidget>();
	Subject->SetDisplayName(TEXT("Subject"));
	Subject->SetParentBeforeRegister(Tree->RootWidget);
	UDreamRuntimeComponentBindingOrderTestBehaviour* First = Subject->AddComponent<UDreamRuntimeComponentBindingOrderTestBehaviour>();
	UDreamRuntimeComponentBindingOrderTestBehaviour* Second = Subject->AddComponent<UDreamRuntimeComponentBindingOrderTestBehaviour>();
	if (!TestNotNull(TEXT("the first same-class component is authored"), First)
		|| !TestNotNull(TEXT("the second same-class component is authored"), Second))return false;
	First->Identity = TEXT("First");
	First->BoundValue = 11.0f;
	Second->Identity = TEXT("Second");
	Second->BoundValue = 22.0f;

	FDreamWidgetPropertyBinding PropertyBinding;
	PropertyBinding.WidgetName = TEXT("Subject");
	PropertyBinding.Target = EDreamWidgetBindingTarget::Behaviour;
	PropertyBinding.BehaviourIndex = 0;
	PropertyBinding.PropertyName = TEXT("BoundValue");
	PropertyBinding.FunctionName = TEXT("ReadBoundValue");
	Scoped.Blueprint->PropertyBindings.Add(PropertyBinding);
	FDreamWidgetEventBinding EventBinding;
	EventBinding.WidgetName = TEXT("Subject");
	EventBinding.Target = EDreamWidgetBindingTarget::Behaviour;
	EventBinding.BehaviourIndex = 0;
	EventBinding.EventName = TEXT("OnTriggered");
	EventBinding.FunctionName = TEXT("HandleTrigger");
	Scoped.Blueprint->EventBindings.Add(EventBinding);

	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Scoped.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	if (!TestEqual(TEXT("both authored bindings compile"), Results.NumErrors, 0))return false;
	UDreamWidgetGeneratedClass* Generated = Cast<UDreamWidgetGeneratedClass>(Scoped.Blueprint->GeneratedClass);
	if (!TestNotNull(TEXT("the compiler generated the runtime class"), Generated)
		|| !TestEqual(TEXT("the compiled class carries the property binding"), Generated->GetPropertyBindings().Num(), 1)
		|| !TestEqual(TEXT("the compiled class carries the event binding"), Generated->GetEventBindings().Num(), 1))return false;
	const bool bReorder = Parameters != TEXT("Control") && !bRemoved && !bNested;

	DreamTests::FScopedGameWorld TestWorld;
	UDreamRuntimeComponentBindingOrderTestWidget* Instance = Cast<UDreamRuntimeComponentBindingOrderTestWidget>(
		BeginCreateDreamWidget(TestWorld.World, Generated));
	if (!TestNotNull(TEXT("the public deferred-create path creates the compiled widget"), Instance))return false;
	ON_SCOPE_EXIT { if (IsValid(Instance))Instance->DestroyWidget(); };
	// Assign the real instance before its initialization hook, like an Expose on Spawn input.
	Instance->bReorderOnInitialized = bReorder;
	Instance->bRemoveFirstOnInitialized = bRemoved;
	if (!TestNotNull(TEXT("public FinishCreateDreamWidget initializes the configured instance"), FinishCreateDreamWidget(Instance)))return false;
	UDreamRuntimeComponentBindingOrderTestWidget* Copy = nullptr;
	ON_SCOPE_EXIT { if (IsValid(Copy))Copy->DestroyWidget(); };
	UDreamRuntimeComponentBindingOrderTestWidget* CheckedInstance = Instance;
	if (bDuplicate)
	{
		UDreamWidget* SourceSubject = Instance->GetWidgetTree()->FindWidgetByVariableName(TEXT("Subject"));
		if (!TestNotNull(TEXT("the source subject exists before duplication"), SourceSubject)
			|| !TestEqual(TEXT("the source still has its two authored components"), SourceSubject->GetAllComponents().Num(), 2))return false;
		const UDreamRuntimeComponentBindingOrderTestBehaviour* SourceFront =
			Cast<UDreamRuntimeComponentBindingOrderTestBehaviour>(SourceSubject->GetAllComponents()[0]);
		if (!TestNotNull(TEXT("the reordered source front component exists"), SourceFront)
			|| !TestEqual(TEXT("the source order differs from the authored order"), SourceFront->Identity, FName(TEXT("Second"))))return false;
		// Keep the source's changed order in the copy: there is no second reorder to mask a bad mapping.
		Instance->bReorderOnInitialized = false;
		Copy = Cast<UDreamRuntimeComponentBindingOrderTestWidget>(DuplicateDreamWidgetHierarchy(TestWorld.World, Instance, nullptr));
		if (!TestNotNull(TEXT("the public hierarchy duplication creates a user widget"), Copy))return false;
		CheckedInstance = Copy;
	}
	UDreamWidget* LiveSubject = CheckedInstance->GetWidgetTree()->FindWidgetByVariableName(TEXT("Subject"));
	if (!TestNotNull(TEXT("the live subject exists"), LiveSubject))return false;
	if (!bNested && !TestEqual(TEXT("initialization preserves the expected component count"),
		LiveSubject->GetAllComponents().Num(), bRemoved ? 1 : 2))return false;
	UDreamRuntimeComponentBindingOrderTestBehaviour* LiveFirst = nullptr;
	UDreamRuntimeComponentBindingOrderTestBehaviour* LiveSecond = nullptr;
	for (UDreamUIBehaviour* Component : LiveSubject->GetAllComponents())
	{
		UDreamRuntimeComponentBindingOrderTestBehaviour* Candidate = Cast<UDreamRuntimeComponentBindingOrderTestBehaviour>(Component);
		if (Candidate != nullptr && Candidate->Identity == TEXT("First"))LiveFirst = Candidate;
		if (Candidate != nullptr && Candidate->Identity == TEXT("Second"))LiveSecond = Candidate;
	}
	if (!TestNotNull(TEXT("the second authored identity survives instancing"), LiveSecond))return false;
	if (bRemoved)
	{
		TestNull(TEXT("initialization removed the authored first component"), LiveFirst);
		TestEqual(TEXT("the remaining component did not inherit the removed property binding"), LiveSecond->BoundValue, 22.0f);
		LiveSecond->Trigger();
		TestEqual(TEXT("the remaining component did not inherit the removed event binding"), CheckedInstance->TriggerCount, 0);
		return true;
	}
	if (!TestNotNull(TEXT("the first authored identity survives instancing"), LiveFirst))return false;
	TestEqual(TEXT("initialization performed the requested public reorder"), LiveSubject->GetAllComponents()[0],
		static_cast<UDreamUIBehaviour*>(bReorder || bNested ? LiveSecond : LiveFirst));

	if (Parameters != TEXT("Event"))
	{
		TestEqual(TEXT("the property binding drives the authored first component"), LiveFirst->BoundValue, 77.0f);
		TestEqual(TEXT("the unbound authored second component keeps its own value"), LiveSecond->BoundValue, 22.0f);
	}
	if (Parameters != TEXT("Property"))
	{
		LiveFirst->Trigger();
		TestEqual(TEXT("the authored first component dispatches the compiled route"), CheckedInstance->TriggerCount, 1);
		const int32 CountBeforeUnboundEvent = CheckedInstance->TriggerCount;
		LiveSecond->Trigger();
		TestEqual(TEXT("the unbound second component cannot dispatch that route"), CheckedInstance->TriggerCount, CountBeforeUnboundEvent);
	}
	return true;
}

#endif
