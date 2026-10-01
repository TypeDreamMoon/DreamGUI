// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "K2Node_DreamGUICompRef.h"

#include "Components/ActorComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "DreamUIComponentReference.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Knot.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

/*
 * The one node in this module with logic worth pinning, and it had none.
 *
 * Get Component for DreamGUIComponentReference retypes its own output pin from the component
 * reference feeding it, which means it has to find the VARIABLE behind that pin -- through any
 * number of reroute knots, because that is how anybody tidies a wire. Both halves of the answer are
 * load-bearing and neither is visible from the graph: the type it lands on decides what downstream
 * nodes accept, and its answer to ReferencesVariable decides whether a rename finds it at all.
 */

namespace DreamGUIK2NodeTestLocal
{
	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UBlueprint* Blueprint = nullptr;

		/**
		 * @param InParentClass  An actor by default, because that is the blueprint kind guaranteed to come
		 *                       with an event graph -- most tests here need a graph to put nodes in, nothing
		 *                       more. A test that runs what it compiles passes a plain object, which needs no
		 *                       world to be made in.
		 */
		explicit FScopedBlueprint(const TCHAR* InName, UClass* InParentClass = AActor::StaticClass())
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUIK2NodeTests/%s"), InName));
			Package->AddToRoot();
			Blueprint = FKismetEditorUtilities::CreateBlueprint(
				InParentClass, Package, FName(InName), BPTYPE_Normal,
				UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		}

		~FScopedBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		UEdGraph* Graph() const
		{
			return Blueprint != nullptr && Blueprint->UbergraphPages.Num() > 0 ? Blueprint->UbergraphPages[0] : nullptr;
		}
	};

	/** The struct pin type the node's input expects, and the type of the variable feeding it. */
	FEdGraphPinType ComponentReferencePinType()
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = FDreamUIComponentReference::StaticStruct();
		return PinType;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGUICompRefNodeUnresolvedOutputTest,
	"DreamGUI.K2Node.AnUnfedComponentReferenceNodeSaysSoOnEveryFaceItHas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGUICompRefNodeUnresolvedOutputTest::RunTest(const FString& Parameters)
{
	using namespace DreamGUIK2NodeTestLocal;
	FScopedBlueprint Fixture(TEXT("BP_CompRefUnfed"));
	UEdGraph* Graph = Fixture.Graph();
	if (!TestNotNull(TEXT("the fixture has a graph"), Graph))
	{
		return false;
	}

	UK2Node_DreamGUICompRef_GetComponent* Node = NewObject<UK2Node_DreamGUICompRef_GetComponent>(Graph);
	Graph->AddNode(Node, /*bFromUI*/false, /*bSelectNewNode*/false);
	Node->AllocateDefaultPins();

	if (!TestEqual(TEXT("the node has an input and an output"), Node->Pins.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("the input takes a component reference"),
		(const UObject*)Node->Pins[0]->PinType.PinSubCategoryObject.Get(),
		(const UObject*)FDreamUIComponentReference::StaticStruct());

	// Nothing feeds it, so there is no variable to read a component class off.
	Node->NodeConnectionListChanged();
	TestEqual(TEXT("the output falls back to a plain ActorComponent"),
		(const UObject*)Node->Pins[1]->PinType.PinSubCategoryObject.Get(),
		(const UObject*)UActorComponent::StaticClass());
	TestEqual(TEXT("and the compact face says the cast is the caller's"),
		Node->GetCompactNodeTitle().ToString(), FString(TEXT("!Get")));
	TestTrue(TEXT("the full title says why, since the compact one has no room to"),
		Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(TEXT("cast the result yourself")));

	// The compile-time half, which was an empty body behind a @todo: a failed auto-cast said nothing
	// at all, and the symptom is a cast that fails at run time with nothing in the compile log.
	FCompilerResultsLog MessageLog;
	Node->ValidateNodeDuringCompilation(MessageLog);
	int32 NoteCount = 0;
	for (const TSharedRef<FTokenizedMessage>& Message : MessageLog.Messages)
	{
		if (Message->GetSeverity() == EMessageSeverity::Info)
		{
			++NoteCount;
		}
	}
	TestEqual(TEXT("compiling it leaves a note explaining the plain ActorComponent"), NoteCount, 1);
	TestEqual(TEXT("and no error, because this is a legitimate state"), MessageLog.NumErrors, 0);
	TestEqual(TEXT("nor a warning"), MessageLog.NumWarnings, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGUICompRefNodeFollowsRerouteToVariableTest,
	"DreamGUI.K2Node.AComponentReferenceRoutedThroughAKnotStillNamesItsVariable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGUICompRefNodeFollowsRerouteToVariableTest::RunTest(const FString& Parameters)
{
	using namespace DreamGUIK2NodeTestLocal;
	FScopedBlueprint Fixture(TEXT("BP_CompRefRouted"));
	UEdGraph* Graph = Fixture.Graph();
	if (!TestNotNull(TEXT("the fixture has a graph"), Graph))
	{
		return false;
	}

	static const FName VariableName(TEXT("MyComponentRef"));
	if (!TestTrue(TEXT("the fixture declares a component reference variable"),
		FBlueprintEditorUtils::AddMemberVariable(Fixture.Blueprint, VariableName, ComponentReferencePinType())))
	{
		return false;
	}

	UK2Node_VariableGet* VariableNode = NewObject<UK2Node_VariableGet>(Graph);
	Graph->AddNode(VariableNode, false, false);
	VariableNode->VariableReference.SetSelfMember(VariableName);
	VariableNode->AllocateDefaultPins();

	// The reroute knot is the whole point: a graph anybody has tidied has one of these in the middle
	// of the wire, and looking only at the other end of the link finds a node that holds no value.
	UK2Node_Knot* Knot = NewObject<UK2Node_Knot>(Graph);
	Graph->AddNode(Knot, false, false);
	Knot->AllocateDefaultPins();

	UK2Node_DreamGUICompRef_GetComponent* Node = NewObject<UK2Node_DreamGUICompRef_GetComponent>(Graph);
	Graph->AddNode(Node, false, false);
	Node->AllocateDefaultPins();

	UEdGraphPin* VariableOutput = VariableNode->FindPin(VariableName, EGPD_Output);
	if (!TestNotNull(TEXT("the variable node has an output pin"), VariableOutput))
	{
		return false;
	}
	VariableOutput->MakeLinkTo(Knot->GetInputPin());
	Knot->GetOutputPin()->MakeLinkTo(Node->Pins[0]);

	// The question the engine asks while renaming, deleting or listing references. Answering false --
	// which is what the node used to do unconditionally -- reported the variable as unreferenced.
	TestTrue(TEXT("the node names the variable on the far side of the knot"),
		Node->ReferencesVariable(VariableName, nullptr));
	TestFalse(TEXT("and does not claim variables it has nothing to do with"),
		Node->ReferencesVariable(FName(TEXT("SomethingElse")), nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGUICompRefNodeCompilesAndCastsTest,
	"DreamGUI.K2Node.AComponentReferenceNodeCompilesAndCastsTheTypeItNarrowsTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The node had never compiled.
 *
 * Its expansion looked the library function up by a name the library does not declare, and asked for an input pin by a
 * name that function does not have either, so every graph using it failed with "the function has not been found" -- and
 * nothing here compiled a graph with it in. Its output type is read off the CLASS DEFAULT of the variable feeding it,
 * which an instance can override, and the expansion retyped the function's result to it with no cast: a component of
 * another class went downstream under a type it does not have. This compiles the node into a custom event that stores
 * its result, then runs the event on an instance holding a component of the advertised class and on one holding
 * another, and reads back what was stored.
 */
bool FDreamGUICompRefNodeCompilesAndCastsTest::RunTest(const FString& Parameters)
{
	using namespace DreamGUIK2NodeTestLocal;
	FScopedBlueprint Fixture(TEXT("BP_CompRefCompiles"), UObject::StaticClass());
	UEdGraph* Graph = Fixture.Graph();
	if (!TestNotNull(TEXT("the fixture has a graph"), Graph))
	{
		return false;
	}

	static const FName ReferenceName(TEXT("MyComponentRef"));
	static const FName StoredName(TEXT("Stored"));
	FEdGraphPinType SceneComponentPinType;
	SceneComponentPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	SceneComponentPinType.PinSubCategoryObject = USceneComponent::StaticClass();
	if (!TestTrue(TEXT("the fixture declares a component reference"),
			FBlueprintEditorUtils::AddMemberVariable(Fixture.Blueprint, ReferenceName, ComponentReferencePinType()))
		|| !TestTrue(TEXT("and a scene component to store the result in"),
			FBlueprintEditorUtils::AddMemberVariable(Fixture.Blueprint, StoredName, SceneComponentPinType)))
	{
		return false;
	}
	// The class the node narrows to is the reference's class DEFAULT, so the class has to exist first.
	FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	const FStructProperty* DefaultsReference = FindFProperty<FStructProperty>(Fixture.Blueprint->GeneratedClass, ReferenceName);
	if (!TestNotNull(TEXT("the class declares the reference"), DefaultsReference))
	{
		return false;
	}
	*DefaultsReference->ContainerPtrToValuePtr<FDreamUIComponentReference>(Fixture.Blueprint->GeneratedClass->GetDefaultObject())
		= FDreamUIComponentReference(USceneComponent::StaticClass());

	// StoreComponent: Stored = GetComponent(MyComponentRef)
	FGraphNodeCreator<UK2Node_CustomEvent> EventCreator(*Graph);
	UK2Node_CustomEvent* Event = EventCreator.CreateNode(/*bSelectNewNode*/false);
	EventCreator.Finalize();
	Event->CustomFunctionName = TEXT("StoreComponent");

	UK2Node_VariableGet* Getter = NewObject<UK2Node_VariableGet>(Graph);
	Graph->AddNode(Getter, false, false);
	Getter->VariableReference.SetSelfMember(ReferenceName);
	Getter->AllocateDefaultPins();

	UK2Node_DreamGUICompRef_GetComponent* Node = NewObject<UK2Node_DreamGUICompRef_GetComponent>(Graph);
	Graph->AddNode(Node, false, false);
	Node->AllocateDefaultPins();

	UK2Node_VariableSet* Setter = NewObject<UK2Node_VariableSet>(Graph);
	Graph->AddNode(Setter, false, false);
	Setter->VariableReference.SetSelfMember(StoredName);
	Setter->AllocateDefaultPins();

	UEdGraphPin* EventThen = Event->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* ReferenceOutput = Getter->FindPin(ReferenceName, EGPD_Output);
	UEdGraphPin* StoredInput = Setter->FindPin(StoredName, EGPD_Input);
	if (!TestTrue(TEXT("every pin the graph is wired through exists"),
		EventThen != nullptr && ReferenceOutput != nullptr && StoredInput != nullptr && Setter->GetExecPin() != nullptr))
	{
		return false;
	}
	EventThen->MakeLinkTo(Setter->GetExecPin());
	ReferenceOutput->MakeLinkTo(Node->Pins[0]);
	// What wiring it in the editor does: the output takes the class the reference names.
	Node->NodeConnectionListChanged();
	TestEqual(TEXT("the node narrows its output to the class the reference names"),
		(const UObject*)Node->Pins[1]->PinType.PinSubCategoryObject.Get(), (const UObject*)USceneComponent::StaticClass());
	Node->Pins[1]->MakeLinkTo(StoredInput);

	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	if (!TestEqual(TEXT("a graph using the node compiles"), Results.NumErrors, 0))
	{
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			AddInfo(Message->ToText().ToString());
		}
		return false;
	}

	UClass* CompiledClass = Fixture.Blueprint->GeneratedClass;
	const FStructProperty* Reference = FindFProperty<FStructProperty>(CompiledClass, ReferenceName);
	const FObjectProperty* Stored = FindFProperty<FObjectProperty>(CompiledClass, StoredName);
	const TStrongObjectPtr<UObject> Instance(NewObject<UObject>(GetTransientPackage(), CompiledClass));
	UFunction* StoreComponent = Instance->FindFunction(FName(TEXT("StoreComponent")));
	if (!TestTrue(TEXT("the compiled class has the event and both variables"),
		Reference != nullptr && Stored != nullptr && StoreComponent != nullptr))
	{
		return false;
	}

	const TStrongObjectPtr<USceneComponent> SceneComponent(NewObject<USceneComponent>(GetTransientPackage()));
	*Reference->ContainerPtrToValuePtr<FDreamUIComponentReference>(Instance.Get()) = FDreamUIComponentReference(SceneComponent.Get());
	Instance->ProcessEvent(StoreComponent, nullptr);
	TestTrue(TEXT("a component of the advertised class comes through"),
		Stored->GetObjectPropertyValue_InContainer(Instance.Get()) == SceneComponent.Get());

	// An instance's own reference, to a component that is not a scene component at all.
	const TStrongObjectPtr<UInputComponent> OtherComponent(NewObject<UInputComponent>(GetTransientPackage()));
	*Reference->ContainerPtrToValuePtr<FDreamUIComponentReference>(Instance.Get()) = FDreamUIComponentReference(OtherComponent.Get());
	Instance->ProcessEvent(StoreComponent, nullptr);
	TestNull(TEXT("and one of another class comes through as None, not as a scene component it is not"),
		Stored->GetObjectPropertyValue_InContainer(Instance.Get()));
	return true;
}

#endif
