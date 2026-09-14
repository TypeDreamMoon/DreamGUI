// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "K2Node_DreamGUICompRef.h"

#include "Components/ActorComponent.h"
#include "DreamUIComponentReference.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_Knot.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/Package.h"

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

		explicit FScopedBlueprint(const TCHAR* InName)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUIK2NodeTests/%s"), InName));
			Package->AddToRoot();
			// An actor parent, because that is the blueprint kind guaranteed to come with an
			// event graph -- these tests need a graph to put nodes in, nothing more.
			Blueprint = FKismetEditorUtilities::CreateBlueprint(
				AActor::StaticClass(), Package, FName(InName), BPTYPE_Normal,
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

#endif
