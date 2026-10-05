// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "K2Node_CreateDreamWidget.h"
#include "K2Node_DreamGUICompRef.h"

#include "Components/ActorComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "DreamScopedWorld.h"
#include "DreamUIBPLibrary.h"
#include "DreamUIComponentReference.h"
#include "DreamUserWidgetTestTypes.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Knot.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/Package.h"
#include "UObject/Script.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

/*
 * The nodes in this module with logic worth pinning.
 *
 * Get Component for DreamGUIComponentReference had none, and it retypes its own output pin from the component
 * reference feeding it, which means it has to find the VARIABLE behind that pin -- through any
 * number of reroute knots, because that is how anybody tidies a wire. Both halves of the answer are
 * load-bearing and neither is visible from the graph: the type it lands on decides what downstream
 * nodes accept, and its answer to ReferencesVariable decides whether a rename finds it at all.
 *
 * Create Dream Widget grows its pins from the class picked on it and expands into two library calls
 * with the assignments between them; what it promises is about ORDER, which only running it shows.
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCreateDreamWidgetNodeTest,
	"DreamGUI.K2Node.CreateDreamWidgetAssignsItsSpawnPinsBeforeTheWidgetInitializes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Create Dream Widget, from the class picked on it to the widget a graph gets back, and the three UMG verbs that
 * widget is then put on screen and taken off with.
 *
 * Picking the class has to grow a pin for each Expose on Spawn property and keep Owning Player, which is the
 * node's own and would be dropped with the class's pins if it were mistaken for one; the result has to come out
 * as the class picked. Compiled and run, the pins' values have to be on the widget BEFORE it initializes -- On
 * Initialized and On Construct read them, which UMG's order (assign after Create) would not give -- and the widget
 * has to come back owned, held by the manager and off screen, the state every creation verb leaves it in.
 */
bool FDreamCreateDreamWidgetNodeTest::RunTest(const FString& Parameters)
{
	using namespace DreamGUIK2NodeTestLocal;
	FScopedBlueprint Fixture(TEXT("BP_CreateDreamWidget"));
	UEdGraph* Graph = Fixture.Graph();
	if (!TestNotNull(TEXT("the fixture has a graph"), Graph))
	{
		return false;
	}

	static const FName MadeName(TEXT("Made"));
	static const FName PlayerName(TEXT("Player"));
	FEdGraphPinType WidgetPinType;
	WidgetPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	WidgetPinType.PinSubCategoryObject = UDreamUserWidget::StaticClass();
	FEdGraphPinType PlayerPinType;
	PlayerPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	PlayerPinType.PinSubCategoryObject = APlayerController::StaticClass();
	if (!TestTrue(TEXT("the fixture declares a widget to store the result in"),
			FBlueprintEditorUtils::AddMemberVariable(Fixture.Blueprint, MadeName, WidgetPinType))
		|| !TestTrue(TEXT("and a player to own it"),
			FBlueprintEditorUtils::AddMemberVariable(Fixture.Blueprint, PlayerName, PlayerPinType)))
	{
		return false;
	}

	UK2Node_CreateDreamWidget* Node = NewObject<UK2Node_CreateDreamWidget>(Graph);
	Graph->AddNode(Node, /*bFromUI*/false, /*bSelectNewNode*/false);
	Node->AllocateDefaultPins();
	TestEqual(TEXT("in the menu it is Create Dream Widget"),
		Node->GetNodeTitle(ENodeTitleType::MenuTitle).ToString(), FString(TEXT("Create Dream Widget")));

	// What picking a class on the node does in the editor.
	UEdGraphPin* ClassPin = Node->GetClassPin();
	if (!TestNotNull(TEXT("the node has a class pin"), ClassPin))
	{
		return false;
	}
	ClassPin->DefaultObject = UDreamCreateWidgetNodeFixture::StaticClass();
	Node->PinDefaultValueChanged(ClassPin);

	UEdGraphPin* CaptionPin = Node->FindPin(TEXT("Caption"));
	UEdGraphPin* CountPin = Node->FindPin(TEXT("Count"));
	UEdGraphPin* OwningPlayerPin = Node->GetOwningPlayerPin();
	UEdGraphPin* ResultPin = Node->GetResultPin();
	if (!TestNotNull(TEXT("the class's Expose on Spawn string became a pin"), CaptionPin)
		|| !TestNotNull(TEXT("and so did its integer"), CountPin)
		|| !TestNotNull(TEXT("Owning Player survived the class change"), OwningPlayerPin)
		|| !TestNotNull(TEXT("the node has a result"), ResultPin))
	{
		return false;
	}
	TestEqual(TEXT("the result is typed as the class picked"),
		(const UObject*)ResultPin->PinType.PinSubCategoryObject.Get(), (const UObject*)UDreamCreateWidgetNodeFixture::StaticClass());
	TestTrue(TEXT("the title names the class"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(
		UDreamCreateWidgetNodeFixture::StaticClass()->GetDisplayNameText().ToString()));
	CaptionPin->DefaultValue = TEXT("Spawned");
	CountPin->DefaultValue = TEXT("7");

	// MakeWidget: Made = Create Dream Widget(fixture class, Caption "Spawned", Count 7, Owning Player = Player)
	FGraphNodeCreator<UK2Node_CustomEvent> EventCreator(*Graph);
	UK2Node_CustomEvent* Event = EventCreator.CreateNode(/*bSelectNewNode*/false);
	EventCreator.Finalize();
	Event->CustomFunctionName = TEXT("MakeWidget");

	UK2Node_VariableGet* PlayerGetter = NewObject<UK2Node_VariableGet>(Graph);
	Graph->AddNode(PlayerGetter, false, false);
	PlayerGetter->VariableReference.SetSelfMember(PlayerName);
	PlayerGetter->AllocateDefaultPins();

	UK2Node_VariableSet* Setter = NewObject<UK2Node_VariableSet>(Graph);
	Graph->AddNode(Setter, false, false);
	Setter->VariableReference.SetSelfMember(MadeName);
	Setter->AllocateDefaultPins();

	UEdGraphPin* EventThen = Event->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* PlayerOutput = PlayerGetter->FindPin(PlayerName, EGPD_Output);
	UEdGraphPin* MadeInput = Setter->FindPin(MadeName, EGPD_Input);
	if (!TestTrue(TEXT("every pin the graph is wired through exists"),
		EventThen != nullptr && PlayerOutput != nullptr && MadeInput != nullptr && Setter->GetExecPin() != nullptr))
	{
		return false;
	}
	EventThen->MakeLinkTo(Node->GetExecPin());
	Node->GetThenPin()->MakeLinkTo(Setter->GetExecPin());
	PlayerOutput->MakeLinkTo(OwningPlayerPin);
	ResultPin->MakeLinkTo(MadeInput);

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

	DreamTests::FScopedGameWorld TestWorld;
	UWorld* World = TestWorld.World;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
	UDreamScreenUISubsystem* Screen = UDreamScreenUISubsystem::Get(World);
	if (!TestNotNull(TEXT("the world has a UI manager"), Manager) || !TestNotNull(TEXT("and a screen"), Screen))
	{
		return false;
	}
	// A game has begun play before anything in it makes a widget, and On Construct waits for it.
	if (!Manager->HasBegunPlay())
	{
		Manager->OnWorldBeginPlay(*World);
	}

	// Before any player exists: there is no player screen to go on, though there is still a screen.
	UDreamUserWidget* Ownerless = Cast<UDreamUserWidget>(
		UDreamUIBPLibrary::CreateDreamWidgetOfClass(World, UDreamCreateWidgetNodeFixture::StaticClass()));
	if (TestNotNull(TEXT("a widget made with no player in the world"), Ownerless))
	{
		AddExpectedError(TEXT("has no owning player"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("Add to Player Screen refuses a widget with no owning player"), Ownerless->AddToPlayerScreen());
		TestFalse(TEXT("and leaves it off screen"), Ownerless->IsInViewport());
		Ownerless->AddToViewport();
		TestTrue(TEXT("Add to Viewport puts it on the first screen all the same"), Ownerless->IsInViewport());
		Screen->RemoveFromViewport(Ownerless);
	}

	APlayerController* Player = World->SpawnActor<APlayerController>();
	AActor* Actor = World->SpawnActor<AActor>(Fixture.Blueprint->GeneratedClass);
	const FObjectProperty* PlayerProperty = FindFProperty<FObjectProperty>(Fixture.Blueprint->GeneratedClass, PlayerName);
	const FObjectProperty* MadeProperty = FindFProperty<FObjectProperty>(Fixture.Blueprint->GeneratedClass, MadeName);
	UFunction* MakeWidget = Actor != nullptr ? Actor->FindFunction(FName(TEXT("MakeWidget"))) : nullptr;
	if (!TestTrue(TEXT("a player, and the compiled actor with its event and both variables"),
		Player != nullptr && PlayerProperty != nullptr && MadeProperty != nullptr && MakeWidget != nullptr))
	{
		return false;
	}
	PlayerProperty->SetObjectPropertyValue_InContainer(Actor, Player);
	// With no net driver, a controller is local only with a local player behind it (5.8), which a game's own always
	// has and one spawned bare here has not -- and the node refuses a controller that is not local, as UMG does.
	// Lent for the call, so the world tears down a controller as bare as the test made it.
	Player->Player = NewObject<ULocalPlayer>(GEngine);
	// The test world never initialized its actors for play, and AActor::ProcessEvent drops a call into
	// such a world without a word unless the editor allows script execution for the call.
	{
		FEditorScriptExecutionGuard ScriptGuard;
		Actor->ProcessEvent(MakeWidget, nullptr);
	}
	Player->Player = nullptr;

	UDreamCreateWidgetNodeFixture* Made = Cast<UDreamCreateWidgetNodeFixture>(MadeProperty->GetObjectPropertyValue_InContainer(Actor));
	if (!TestNotNull(TEXT("running it made a widget of the class picked"), Made))
	{
		return false;
	}
	TestEqual(TEXT("Caption holds the pin's value"), Made->Caption, FString(TEXT("Spawned")));
	TestEqual(TEXT("and Count holds its"), Made->Count, 7);
	TestEqual(TEXT("On Initialized already read the caption"), Made->CaptionAtInitialized, FString(TEXT("Spawned")));
	TestEqual(TEXT("and the count"), Made->CountAtInitialized, 7);
	TestTrue(TEXT("and already answered to the player wired in"), Made->OwnerAtInitialized.Get() == Player);
	TestTrue(TEXT("On Construct ran and read the caption too"), Made->bConstructed && Made->CaptionAtConstruct == TEXT("Spawned"));
	TestTrue(TEXT("the widget belongs to that player"), Made->GetOwningPlayer() == Player);
	TestTrue(TEXT("the manager holds it until it is added"), Manager->IsWidgetParked(Made));
	TestFalse(TEXT("and it is not on screen"), Made->IsInViewport());

	Made->AddToViewport(5);
	TestTrue(TEXT("Add to Viewport puts it on screen"), Made->IsInViewport());
	TestFalse(TEXT("and the manager lets go of it"), Manager->IsWidgetParked(Made));
	TestTrue(TEXT("Remove from Parent takes it off"), Made->RemoveFromParent());
	TestFalse(TEXT("so it is off screen"), Made->IsInViewport());
	TestTrue(TEXT("and held again, alive"), IsValid(Made) && Manager->IsWidgetParked(Made));
	TestTrue(TEXT("Add to Player Screen puts it on its owner's screen"), Made->AddToPlayerScreen());
	TestTrue(TEXT("where it is on screen"), Made->IsInViewport());

	// A widget in somebody else's hierarchy is not pulled out of it.
	UDreamUserWidget* Nested = Cast<UDreamUserWidget>(
		UDreamUIBPLibrary::CreateDreamWidgetOfClass(World, UDreamCreateWidgetNodeFixture::StaticClass()));
	if (TestNotNull(TEXT("a second widget"), Nested) && TestTrue(TEXT("nested under the first"), Nested->TrySetParent(Made, false)))
	{
		AddExpectedError(TEXT("already has a parent"), EAutomationExpectedErrorFlags::Contains, 1);
		Nested->AddToViewport();
		TestTrue(TEXT("Add to Viewport leaves a widget that has a parent where it is"), Nested->GetParent() == Made);
		TestFalse(TEXT("and does not make it a page"), Screen->IsInViewport(Nested));
	}

	Screen->RemoveAllUI();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCreateDreamWidgetOfClassStillCompilesTest,
	"DreamGUI.K2Node.AGraphThatAlreadyCallsCreateDreamWidgetOfClassStillCompiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Create Dream Widget Of Class is BlueprintInternalUseOnly now that the Create Dream Widget node stands for it, as UMG's
 * Create Widget node stands for UWidgetBlueprintLibrary::Create. That is a decision about the menu; a project's graphs
 * that already call the function are not the menu's to break, so one has to go on compiling.
 */
bool FDreamCreateDreamWidgetOfClassStillCompilesTest::RunTest(const FString& Parameters)
{
	using namespace DreamGUIK2NodeTestLocal;
	UFunction* Function = UDreamUIBPLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UDreamUIBPLibrary, CreateDreamWidgetOfClass));
	if (!TestNotNull(TEXT("the function is still there"), Function))
	{
		return false;
	}
	TestFalse(TEXT("the Blueprint menu no longer offers it"), UEdGraphSchema_K2::CanUserKismetCallFunction(Function));

	FScopedBlueprint Fixture(TEXT("BP_CreateDreamWidgetOfClassCall"));
	UEdGraph* Graph = Fixture.Graph();
	if (!TestNotNull(TEXT("the fixture has a graph"), Graph))
	{
		return false;
	}
	FGraphNodeCreator<UK2Node_CustomEvent> EventCreator(*Graph);
	UK2Node_CustomEvent* Event = EventCreator.CreateNode(/*bSelectNewNode*/false);
	EventCreator.Finalize();
	Event->CustomFunctionName = TEXT("MakeWidget");

	// The node a graph placed before the function was hidden: a plain call, with a class picked.
	UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
	Graph->AddNode(Call, false, false);
	Call->SetFromFunction(Function);
	Call->AllocateDefaultPins();
	UEdGraphPin* ClassPin = Call->FindPin(TEXT("InWidgetClass"));
	UEdGraphPin* EventThen = Event->FindPin(UEdGraphSchema_K2::PN_Then);
	if (!TestTrue(TEXT("the call has its class pin and the event its exec"), ClassPin != nullptr && EventThen != nullptr))
	{
		return false;
	}
	ClassPin->DefaultObject = UDreamCreateWidgetNodeFixture::StaticClass();
	EventThen->MakeLinkTo(Call->GetExecPin());

	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	if (!TestEqual(TEXT("a graph that calls it compiles"), Results.NumErrors, 0))
	{
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			AddInfo(Message->ToText().ToString());
		}
		return false;
	}
	return true;
}

#endif
