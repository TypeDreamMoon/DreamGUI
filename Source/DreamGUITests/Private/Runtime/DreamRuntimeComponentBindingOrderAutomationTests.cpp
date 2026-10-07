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
}

bool FDreamRuntimeComponentBindingOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamRuntimeComponentBindingOrderTestLocal;
	FScopedBlueprint Scoped;
	if (!TestNotNull(TEXT("the public factory creates a DreamGUI blueprint"), Scoped.Blueprint))return false;
	UDreamWidgetTree* Tree = Scoped.Blueprint->GetOrCreateWidgetTree();
	UDreamWidget* Subject = Tree->ConstructWidget<UDreamWidget>();
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
	const bool bReorder = Parameters != TEXT("Control");

	DreamTests::FScopedGameWorld TestWorld;
	UDreamRuntimeComponentBindingOrderTestWidget* Instance = Cast<UDreamRuntimeComponentBindingOrderTestWidget>(
		BeginCreateDreamWidget(TestWorld.World, Generated));
	if (!TestNotNull(TEXT("the public deferred-create path creates the compiled widget"), Instance))return false;
	ON_SCOPE_EXIT { if (IsValid(Instance))Instance->DestroyWidget(); };
	// Assign the real instance before its initialization hook, like an Expose on Spawn input.
	Instance->bReorderOnInitialized = bReorder;
	if (!TestNotNull(TEXT("public FinishCreateDreamWidget initializes the configured instance"), FinishCreateDreamWidget(Instance)))return false;
	UDreamWidget* LiveSubject = Instance->GetWidgetTree()->FindWidgetByVariableName(TEXT("Subject"));
	if (!TestNotNull(TEXT("the live subject exists"), LiveSubject)
		|| !TestEqual(TEXT("initialization preserves both components"), LiveSubject->GetAllComponents().Num(), 2))return false;
	UDreamRuntimeComponentBindingOrderTestBehaviour* LiveFirst = nullptr;
	UDreamRuntimeComponentBindingOrderTestBehaviour* LiveSecond = nullptr;
	for (UDreamUIBehaviour* Component : LiveSubject->GetAllComponents())
	{
		UDreamRuntimeComponentBindingOrderTestBehaviour* Candidate = Cast<UDreamRuntimeComponentBindingOrderTestBehaviour>(Component);
		if (Candidate != nullptr && Candidate->Identity == TEXT("First"))LiveFirst = Candidate;
		if (Candidate != nullptr && Candidate->Identity == TEXT("Second"))LiveSecond = Candidate;
	}
	if (!TestNotNull(TEXT("the first authored identity survives instancing"), LiveFirst)
		|| !TestNotNull(TEXT("the second authored identity survives instancing"), LiveSecond))return false;
	TestEqual(TEXT("NativeOnInitialized performed the requested public reorder"), LiveSubject->GetAllComponents()[0],
		static_cast<UDreamUIBehaviour*>(bReorder ? LiveSecond : LiveFirst));

	if (Parameters != TEXT("Event"))
	{
		TestEqual(TEXT("the property binding drives the authored first component"), LiveFirst->BoundValue, 77.0f);
		TestEqual(TEXT("the unbound authored second component keeps its own value"), LiveSecond->BoundValue, 22.0f);
	}
	if (Parameters != TEXT("Property"))
	{
		LiveFirst->Trigger();
		TestEqual(TEXT("the authored first component dispatches the compiled route"), Instance->TriggerCount, 1);
		const int32 CountBeforeUnboundEvent = Instance->TriggerCount;
		LiveSecond->Trigger();
		TestEqual(TEXT("the unbound second component cannot dispatch that route"), Instance->TriggerCount, CountBeforeUnboundEvent);
	}
	return true;
}

#endif
