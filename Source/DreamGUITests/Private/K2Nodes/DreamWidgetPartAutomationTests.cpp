// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "K2Node_DreamWidgetPart.h"

#include "Animation/DreamWidgetAnimationComponent.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintActionFilter.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintActionMenuUtils.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetParts.h"
#include "Core/DreamWidgetTree.h"
#include "DreamScopedWorld.h"
#include "DreamUIBPLibrary.h"
#include "DreamWidgetBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "UObject/Script.h"

/*
 * The Part node and the menu behind it (#108).
 *
 * A widget is composed -- visual, layout container, layout self, behaviours -- and a graph used to reach a part with
 * Get Visual and a Cast. The node's promises are what these tests pin:
 *
 *   - its output is the part's own class, narrowed to what the widget really has when it is placed;
 *   - it compiles to the part, and running the graph reaches it (the call lands on the widget's visual);
 *   - when the widget is an authored one, a compile after the part changed fails AT THE NODE -- through a hierarchy
 *     built in code and through a .dui, since the .dui is how a part usually changes;
 *   - the menu offers a widget's parts only while dragging off that widget, and files them under it.
 */

namespace DreamWidgetPartTestLocal
{
	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedBlueprint(const TCHAR* InName)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamWidgetPartTests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTextUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		FScopedBlueprint(const FScopedBlueprint&) = delete;
		FScopedBlueprint& operator=(const FScopedBlueprint&) = delete;

		UDreamWidget* AddWidget(const TCHAR* InDisplayName) const
		{
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			UDreamWidget* Widget = Tree->ConstructWidget<UDreamWidget>();
			Widget->SetDisplayName(InDisplayName);
			Widget->SetParentBeforeRegister(Tree->RootWidget.Get());
			return Widget;
		}

		UEdGraph* Graph() const
		{
			return Blueprint != nullptr && Blueprint->UbergraphPages.Num() > 0 ? Blueprint->UbergraphPages[0].Get() : nullptr;
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

	/** A .dui that has really been to disk, and is gone again when the test returns. */
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
			IFileManager::Get().Delete(*FilePath, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
		}

		FScopedDuiFile(const FScopedDuiFile&) = delete;
		FScopedDuiFile& operator=(const FScopedDuiFile&) = delete;

		bool Write(const TArray<FString>& InLines) const
		{
			return FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *FilePath);
		}

		FString FilePath;
	};

	void Compile(UDreamWidgetBlueprint* InBlueprint, FCompilerResultsLog& OutResults)
	{
		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection, &OutResults);
	}

	FString JoinMessages(const FCompilerResultsLog& InResults)
	{
		FString All;
		for (const TSharedRef<FTokenizedMessage>& Message : InResults.Messages)
		{
			All += Message->ToText().ToString() + TEXT(" | ");
		}
		return All;
	}

	/** A getter of a member variable, the node dragging the variable into the graph makes. Only after a compile declared it. */
	UK2Node_VariableGet* AddVariableGet(UEdGraph* InGraph, const TCHAR* InVariableName)
	{
		UK2Node_VariableGet* Node = NewObject<UK2Node_VariableGet>(InGraph);
		Node->VariableReference.SetSelfMember(FName(InVariableName));
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		InGraph->AddNode(Node, /*bUserAction*/false, /*bSelectNewNode*/false);
		return Node;
	}

	/**
	 * What picking an action off a widget pin does: the spawner makes the node, and the menu then autowires it to the pin
	 * it was opened from. Driven through the spawner so the delegate that fills the node in is the one under test too.
	 */
	template<typename TNode>
	TNode* PlaceFromPin(UDreamWidgetPartNodeSpawner* InSpawner, UEdGraph* InGraph, UEdGraphPin* InFromPin)
	{
		TNode* Node = Cast<TNode>(InSpawner->Invoke(InGraph, IBlueprintNodeBinder::FBindingSet(), FVector2D(300.0, 0.0)));
		if (Node != nullptr)
		{
			Node->AutowireNewNode(InFromPin);
		}
		return Node;
	}

	UFunction* SetTextFunction()
	{
		return UDreamText::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UDreamText, SetText));
	}

	/** The first function UDreamVisual itself declares that a graph can call: something every visual takes. */
	UFunction* AnyVisualFunction()
	{
		for (TFieldIterator<UFunction> It(UDreamVisual::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (UEdGraphSchema_K2::CanUserKismetCallFunction(*It) && !It->HasAnyFunctionFlags(FUNC_Static | FUNC_Protected | FUNC_Private))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** ApplyLabel: PlayerLabel -> Set Text, the target the widget itself. Returns the call node, or nullptr. */
	UK2Node_DreamWidgetPartCall* AddApplyLabel(FAutomationTestBase& InTest, UEdGraph* InGraph, UEdGraphPin* InWidgetOutput, const TCHAR* InText)
	{
		UK2Node_DreamWidgetPartCall* Call = PlaceFromPin<UK2Node_DreamWidgetPartCall>(
			UDreamWidgetPartNodeSpawner::CreateCall(UDreamText::StaticClass(), SetTextFunction()), InGraph, InWidgetOutput);
		if (!InTest.TestNotNull(TEXT("the action placed a call"), Call))
		{
			return nullptr;
		}
		if (UEdGraphPin* Value = Call->FindPin(TEXT("Value")))
		{
			Value->DefaultTextValue = FText::FromString(InText);
		}
		FGraphNodeCreator<UK2Node_CustomEvent> EventCreator(*InGraph);
		UK2Node_CustomEvent* Event = EventCreator.CreateNode(/*bSelectNewNode*/false);
		EventCreator.Finalize();
		Event->CustomFunctionName = TEXT("ApplyLabel");
		Event->FindPin(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(Call->GetExecPin());
		return Call;
	}

	int32 CountOccurrences(const FString& InText, const TCHAR* InNeedle)
	{
		int32 Count = 0;
		for (int32 From = InText.Find(InNeedle); From != INDEX_NONE; From = InText.Find(InNeedle, ESearchCase::IgnoreCase, ESearchDir::FromStart, From + 1))
		{
			++Count;
		}
		return Count;
	}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetPartFindTest,
	"DreamGUI.K2Node.APartIsFoundByItsClassAndAWrongClassFindsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetPartFindTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetPartTestLocal;
	FScopedBlueprint Fixture(TEXT("BP_PartFind"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;

	// One widget with all four kinds at once: the case a widget class per role could not type, and the reason parts are
	// typed one by one instead.
	UDreamWidget* Column = Fixture.AddWidget(TEXT("Column"));
	UDreamText* Text = Cast<UDreamText>(Column->CreateNewVisual(UDreamText::StaticClass()));
	UObject* Box = Column->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass());
	UObject* Animator = Column->AddComponent(UDreamWidgetAnimationComponent::StaticClass());
	if (!TestTrue(TEXT("the widget has a visual, a container and a behaviour"), Text != nullptr && Box != nullptr && Animator != nullptr))
	{
		return false;
	}

	TestEqual(TEXT("the visual is found by its class"), FDreamWidgetParts::Find(Column, UDreamText::StaticClass()), (UObject*)Text);
	TestEqual(TEXT("and by a base of it"), FDreamWidgetParts::Find(Column, UDreamVisual::StaticClass()), (UObject*)Text);
	TestEqual(TEXT("the container by its"), FDreamWidgetParts::Find(Column, UDreamLayoutContainerVerticalBox::StaticClass()), Box);
	TestEqual(TEXT("the behaviour by its"), FDreamWidgetParts::Find(Column, UDreamWidgetAnimationComponent::StaticClass()), Animator);
	TestNull(TEXT("a visual of another class is not found"), FDreamWidgetParts::Find(Column, UDreamRectBlock::StaticClass()));
	TestNull(TEXT("nor a container of another"), FDreamWidgetParts::Find(Column, UDreamLayoutContainerHorizontalBox::StaticClass()));
	TestNull(TEXT("and a widget that is not there has no parts"), FDreamWidgetParts::Find(nullptr, UDreamText::StaticClass()));

	TestEqual(TEXT("what the node compiles to answers the same"),
		UDreamUIBPLibrary::K2_GetWidgetPart(Column, UDreamText::StaticClass()), (UObject*)Text);
	TestNull(TEXT("and None for a part the widget lacks"), UDreamUIBPLibrary::K2_GetWidgetPart(Column, UDreamRectBlock::StaticClass()));

	TestEqual(TEXT("C++ reads the visual typed"), Column->GetVisual<UDreamText>(), Text);
	TestNull(TEXT("and gets nullptr for the wrong type"), Column->GetVisual<UDreamRectBlock>());
	TestEqual(TEXT("and the container"), (UObject*)Column->GetLayoutContainer<UDreamLayoutContainerVerticalBox>(), Box);

	TArray<UObject*> All;
	FDreamWidgetParts::GetAll(Column, All);
	TestEqual(TEXT("every part is listed, in order"), All.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetPartPlaceAndRunTest,
	"DreamGUI.K2Node.SetTextOffAWidgetIsOneNodeAndRunningItReachesTheVisual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetPartPlaceAndRunTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetPartTestLocal;
	FScopedBlueprint Fixture(TEXT("BP_PartPlaceAndRun"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
	UDreamWidget* Label = Fixture.AddWidget(TEXT("PlayerLabel"));
	Label->CreateNewVisual(UDreamText::StaticClass());
	{
		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		if (!TestEqual(TEXT("the hierarchy compiles"), Results.NumErrors, 0))
		{
			AddInfo(JoinMessages(Results));
			return false;
		}
	}

	UEdGraph* Graph = Fixture.Graph();
	UK2Node_VariableGet* Getter = Graph != nullptr ? AddVariableGet(Graph, TEXT("PlayerLabel")) : nullptr;
	UEdGraphPin* WidgetOutput = Getter != nullptr ? Getter->FindPin(FName(TEXT("PlayerLabel")), EGPD_Output) : nullptr;
	if (!TestNotNull(TEXT("the graph reads the widget's variable"), WidgetOutput)) return false;
	TestEqual(TEXT("the pin resolves to the authored widget"),
		DreamWidgetPartNodes::ResolveAuthoredWidget(WidgetOutput), (const UDreamWidget*)Label);

	// Picking Set Text off the widget pin places one node: the call, its target the widget.
	const int32 NodesBefore = Graph->Nodes.Num();
	UK2Node_DreamWidgetPartCall* Call = AddApplyLabel(*this, Graph, WidgetOutput, TEXT("Placed by the graph"));
	if (Call == nullptr) return false;
	TestEqual(TEXT("one node for the call, one for the event that drives it -- no read of the part between"), Graph->Nodes.Num(), NodesBefore + 2);
	TestEqual(TEXT("calling SetText"), Call->GetTargetFunction(), SetTextFunction());
	UEdGraphPin* Target = Call->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
	if (!TestNotNull(TEXT("the call has a target"), Target)) return false;
	TestTrue(TEXT("wired straight to the widget"), Target->LinkedTo.Contains(WidgetOutput));
	TestEqual(TEXT("which takes any widget"),
		(const UObject*)Target->PinType.PinSubCategoryObject.Get(), (const UObject*)UDreamWidget::StaticClass());

	// A member filed under a base class is one node as well, and takes the widget the same way.
	UFunction* VisualFunction = AnyVisualFunction();
	if (TestNotNull(TEXT("UDreamVisual declares a function a graph can call"), VisualFunction))
	{
		UK2Node_DreamWidgetPartCall* BaseCall = PlaceFromPin<UK2Node_DreamWidgetPartCall>(
			UDreamWidgetPartNodeSpawner::CreateCall(UDreamVisual::StaticClass(), VisualFunction), Graph, WidgetOutput);
		if (TestNotNull(TEXT("a base-class action places a call"), BaseCall))
		{
			TestTrue(TEXT("its target the widget too"), BaseCall->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input)->LinkedTo.Contains(WidgetOutput));
			Graph->RemoveNode(BaseCall);
		}
	}

	// The part itself, for handing on, is still one action away -- typed as the widget's own visual.
	UK2Node_DreamWidgetPart* Read = PlaceFromPin<UK2Node_DreamWidgetPart>(UDreamWidgetPartNodeSpawner::CreateRead(UDreamVisual::StaticClass()), Graph, WidgetOutput);
	if (TestNotNull(TEXT("the read action places a read"), Read))
	{
		TestEqual(TEXT("narrowed to the widget's own visual"),
			(const UObject*)Read->GetPartPin()->PinType.PinSubCategoryObject.Get(), (const UObject*)UDreamText::StaticClass());
		Graph->RemoveNode(Read);
	}

	{
		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		if (!TestEqual(TEXT("a graph calling Set Text on the widget compiles"), Results.NumErrors, 0))
		{
			AddInfo(JoinMessages(Results));
			return false;
		}
	}

	DreamTests::FScopedGameWorld TestWorld;
	UWorld* World = TestWorld.World;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
	if (Manager != nullptr && !Manager->HasBegunPlay())
	{
		Manager->OnWorldBeginPlay(*World);
	}
	UDreamUserWidget* Instance = Cast<UDreamUserWidget>(UDreamUIBPLibrary::CreateDreamWidgetOfClass(World,
		TSubclassOf<UDreamUserWidget>(Fixture.Blueprint->GeneratedClass.Get())));
	if (!TestNotNull(TEXT("the class makes an instance"), Instance)) return false;
	UFunction* ApplyLabel = Instance->FindFunction(FName(TEXT("ApplyLabel")));
	UDreamWidget* InstanceLabel = Instance->GetWidgetFromName(FName(TEXT("PlayerLabel")));
	if (!TestTrue(TEXT("with its event and its PlayerLabel"), ApplyLabel != nullptr && InstanceLabel != nullptr)) return false;
	{
		FEditorScriptExecutionGuard ScriptGuard;
		Instance->ProcessEvent(ApplyLabel, nullptr);
	}
	const UDreamText* InstanceText = InstanceLabel->GetVisual<UDreamText>();
	if (TestNotNull(TEXT("the instance's PlayerLabel draws text"), InstanceText))
	{
		TestEqual(TEXT("and running the graph set it"), InstanceText->GetText().ToString(), FString(TEXT("Placed by the graph")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetPartLostInTreeTest,
	"DreamGUI.K2Node.ACallTheWidgetsPartNoLongerTakesFailsTheCompileAtTheNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetPartLostInTreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetPartTestLocal;
	FScopedBlueprint Fixture(TEXT("BP_PartLostInTree"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
	UDreamWidget* Label = Fixture.AddWidget(TEXT("PlayerLabel"));
	Label->CreateNewVisual(UDreamText::StaticClass());
	{
		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		if (!TestEqual(TEXT("the hierarchy compiles"), Results.NumErrors, 0)) return false;
	}

	UEdGraph* Graph = Fixture.Graph();
	UEdGraphPin* WidgetOutput = AddVariableGet(Graph, TEXT("PlayerLabel"))->FindPin(FName(TEXT("PlayerLabel")), EGPD_Output);
	if (AddApplyLabel(*this, Graph, WidgetOutput, TEXT("Name")) == nullptr) return false;
	// And a call any visual takes, which must survive the swap below.
	UFunction* VisualFunction = AnyVisualFunction();
	if (!TestNotNull(TEXT("UDreamVisual declares a function a graph can call"), VisualFunction)) return false;
	PlaceFromPin<UK2Node_DreamWidgetPartCall>(UDreamWidgetPartNodeSpawner::CreateCall(UDreamVisual::StaticClass(), VisualFunction), Graph, WidgetOutput);
	{
		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		if (!TestEqual(TEXT("it compiles while the widget draws text"), Results.NumErrors, 0))
		{
			AddInfo(JoinMessages(Results));
			return false;
		}
	}

	// The widget stops being a Text. Set Text needs one; the visual's own function does not.
	Label->CreateNewVisual(UDreamRectBlock::StaticClass());
	if (!TestTrue(TEXT("the widget draws a RectBlock now"), Label->GetVisual<UDreamRectBlock>() != nullptr)) return false;

	AddExpectedError(TEXT("PlayerLabel's Visual"), EAutomationExpectedErrorFlags::Contains, 0);
	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	TestTrue(TEXT("the compile fails"), Results.NumErrors > 0);
	const FString All = JoinMessages(Results);
	if (!TestTrue(TEXT("and says which widget changed, and into what"),
		All.Contains(TEXT("PlayerLabel's Visual")) && All.Contains(UDreamRectBlock::StaticClass()->GetDisplayNameText().ToString())))
	{
		AddInfo(All);
	}
	TestEqual(TEXT("once: the call every visual takes still compiles"), CountOccurrences(All, TEXT("needs PlayerLabel's Visual")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetPartLostInDuiTest,
	"DreamGUI.K2Node.ADuiThatTurnsTheTextIntoARectBlockFailsAtSetText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetPartLostInDuiTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetPartTestLocal;
	FScopedDuiFile Source(TEXT("PartLostInDui.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    Text PlayerLabel {"),
		TEXT("        Text = \"Name\""),
		TEXT("    }"),
		TEXT("}"),
	}))) return false;

	FScopedBlueprint Fixture(TEXT("BP_PartLostInDui"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
	if (!TestTrue(TEXT("and points at the file"), Fixture.SetDuiFilePath(Source.FilePath))) return false;
	{
		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		if (!TestEqual(TEXT("the file compiles"), Results.NumErrors, 0))
		{
			AddInfo(JoinMessages(Results));
			return false;
		}
	}

	UEdGraph* Graph = Fixture.Graph();
	UEdGraphPin* WidgetOutput = AddVariableGet(Graph, TEXT("PlayerLabel"))->FindPin(FName(TEXT("PlayerLabel")), EGPD_Output);
	if (!TestNotNull(TEXT("the file's node is a variable"), WidgetOutput)) return false;
	if (AddApplyLabel(*this, Graph, WidgetOutput, TEXT("Name")) == nullptr) return false;
	{
		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		if (!TestEqual(TEXT("the graph compiles against the file's Text"), Results.NumErrors, 0))
		{
			AddInfo(JoinMessages(Results));
			return false;
		}
	}

	// One word in the file. The same compile rebuilds the tree and checks the graph, so it has to see the new tree.
	if (!TestTrue(TEXT("the file now says RectBlock"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    RectBlock PlayerLabel {"),
		TEXT("    }"),
		TEXT("}"),
	}))) return false;

	AddExpectedError(TEXT("PlayerLabel's Visual"), EAutomationExpectedErrorFlags::Contains, 0);
	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	const FString All = JoinMessages(Results);
	if (!TestTrue(TEXT("the first compile after the edit fails at Set Text"), Results.NumErrors > 0 && All.Contains(TEXT("PlayerLabel's Visual"))))
	{
		AddInfo(All);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetPartMenuTest,
	"DreamGUI.K2Node.TheMenuOffersAWidgetsPartsOnlyWhileDraggingOffThatWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetPartMenuTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetPartTestLocal;
	FScopedBlueprint Fixture(TEXT("BP_PartMenu"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
	UDreamWidget* Label = Fixture.AddWidget(TEXT("PlayerLabel"));
	Label->CreateNewVisual(UDreamText::StaticClass());
	Label->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass());
	{
		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		if (!TestEqual(TEXT("the hierarchy compiles"), Results.NumErrors, 0)) return false;
	}
	UEdGraph* Graph = Fixture.Graph();
	UEdGraphPin* WidgetOutput = AddVariableGet(Graph, TEXT("PlayerLabel"))->FindPin(FName(TEXT("PlayerLabel")), EGPD_Output);
	if (!TestNotNull(TEXT("the graph reads the widget"), WidgetOutput)) return false;

	FBlueprintActionFilter Dragging;
	Dragging.Context.Blueprints.Add(Fixture.Blueprint);
	Dragging.Context.Graphs.Add(Graph);
	Dragging.Context.Pins.Add(WidgetOutput);
	FBlueprintActionFilter NoPin;
	NoPin.Context.Blueprints.Add(Fixture.Blueprint);
	NoPin.Context.Graphs.Add(Graph);

	UDreamWidgetPartNodeSpawner* ReadText = UDreamWidgetPartNodeSpawner::CreateRead(UDreamText::StaticClass());
	UDreamWidgetPartNodeSpawner* ReadVisual = UDreamWidgetPartNodeSpawner::CreateRead(UDreamVisual::StaticClass());
	UDreamWidgetPartNodeSpawner* ReadRect = UDreamWidgetPartNodeSpawner::CreateRead(UDreamRectBlock::StaticClass());
	UDreamWidgetPartNodeSpawner* ReadBox = UDreamWidgetPartNodeSpawner::CreateRead(UDreamLayoutContainerVerticalBox::StaticClass());
	UDreamWidgetPartNodeSpawner* CallSetText = UDreamWidgetPartNodeSpawner::CreateCall(UDreamText::StaticClass(), SetTextFunction());

	TestFalse(TEXT("dragging off PlayerLabel offers its Text"), ReadText->IsTemplateNodeFilteredOut(Dragging));
	TestFalse(TEXT("and its VerticalBox"), ReadBox->IsTemplateNodeFilteredOut(Dragging));
	TestFalse(TEXT("and Set Text"), CallSetText->IsTemplateNodeFilteredOut(Dragging));
	TestTrue(TEXT("not a RectBlock it does not have"), ReadRect->IsTemplateNodeFilteredOut(Dragging));
	TestTrue(TEXT("nor the visual a second time under a base class"), ReadVisual->IsTemplateNodeFilteredOut(Dragging));
	TestTrue(TEXT("and nothing at all with no widget pin to drag"), ReadText->IsTemplateNodeFilteredOut(NoPin));
	TestTrue(TEXT("no call either"), CallSetText->IsTemplateNodeFilteredOut(NoPin));

	const FString SetTextName = ObjectTools::GetUserFacingFunctionName(SetTextFunction(), /*bAllowFriendlyNames*/true).ToString();
	const FBlueprintActionUiSpec Spec = CallSetText->GetUiSpec(Dragging.Context, IBlueprintNodeBinder::FBindingSet());
	TestTrue(TEXT("a call is filed under the widget"), Spec.Category.ToString().Contains(TEXT("Player Label")));
	TestTrue(TEXT("and the part"), Spec.Category.ToString().Contains(UDreamText::StaticClass()->GetDisplayNameText().ToString()));
	TestEqual(TEXT("under the function's own name"), Spec.MenuName.ToString(), SetTextName);

	// The same through the editor's own context menu, so every rejection test the action database runs is in the way --
	// the pin-compatibility one included, which builds the template node and would drop an action whose node could not
	// take the dragged pin.
	int32 Registered = 0;
	for (const auto& Entry : FBlueprintActionDatabase::Get().GetAllActions())
	{
		for (const UBlueprintNodeSpawner* Spawner : Entry.Value)
		{
			Registered += Cast<UDreamWidgetPartNodeSpawner>(Spawner) != nullptr ? 1 : 0;
		}
	}
	const uint32 Targets = EContextTargetFlags::TARGET_Blueprint | EContextTargetFlags::TARGET_PinObject
		| EContextTargetFlags::TARGET_SiblingPinObjects | EContextTargetFlags::TARGET_BlueprintLibraries | EContextTargetFlags::TARGET_SubComponents;
	FBlueprintActionMenuBuilder Menu;
	const double Start = FPlatformTime::Seconds();
	FBlueprintActionMenuUtils::MakeContextMenu(Dragging.Context, /*bIsContextSensitive*/true, Targets, Menu);
	const double Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
	// Once more: the first menu builds every action's template node, the engine's own included; this is what each later one costs.
	double RepeatMilliseconds = 0.0;
	{
		FBlueprintActionMenuBuilder Again;
		const double AgainStart = FPlatformTime::Seconds();
		FBlueprintActionMenuUtils::MakeContextMenu(Dragging.Context, /*bIsContextSensitive*/true, Targets, Again);
		RepeatMilliseconds = (FPlatformTime::Seconds() - AgainStart) * 1000.0;
	}

	const FProperty* SpacingProperty = UDreamLayoutContainerStackBox::StaticClass()->FindPropertyByName(TEXT("Spacing"));
	if (!TestNotNull(TEXT("the stack box has a Spacing property"), SpacingProperty)) return false;
	const FString SetSpacingName = FString::Printf(TEXT("Set %s"), *SpacingProperty->GetDisplayNameText().ToString());
	const FString GetSpacingName = FString::Printf(TEXT("Get %s"), *SpacingProperty->GetDisplayNameText().ToString());
	int32 UnderWidget = 0;
	bool bSetTextListed = false;
	bool bVerticalBoxListed = false;
	int32 SetSpacingEntries = 0;
	bool bGetSpacingListed = false;
	for (int32 Index = 0; Index < Menu.GetNumActions(); ++Index)
	{
		const TSharedPtr<FEdGraphSchemaAction>& Action = Menu.GetSchemaAction(Index);
		const FString Category = Action.IsValid() ? Action->GetCategory().ToString() : FString();
		if (!Category.Contains(TEXT("Player Label")))
		{
			continue;
		}
		++UnderWidget;
		const FString Name = Action->GetMenuDescription().ToString();
		bSetTextListed |= Name == SetTextName && Category.Contains(UDreamText::StaticClass()->GetDisplayNameText().ToString());
		const bool bUnderBox = Category.Contains(UDreamLayoutContainerVerticalBox::StaticClass()->GetDisplayNameText().ToString());
		bVerticalBoxListed |= bUnderBox;
		SetSpacingEntries += bUnderBox && Name == SetSpacingName ? 1 : 0;
		bGetSpacingListed |= bUnderBox && Name == GetSpacingName;
	}
	AddInfo(FString::Printf(TEXT("%d part actions registered; the menu off PlayerLabel lists %d of %d entries under it, built in %.0f ms, again in %.0f ms"),
		Registered, UnderWidget, Menu.GetNumActions(), Milliseconds, RepeatMilliseconds));
	TestTrue(TEXT("the editor's menu off PlayerLabel lists Set Text under its Text"), bSetTextListed);
	TestTrue(TEXT("and its VerticalBox's actions beside it"), bVerticalBoxListed);
	TestTrue(TEXT("a property the part shows to graphs can be read"), bGetSpacingListed);
	TestEqual(TEXT("and set once: as the property, not again as the BlueprintSetter function behind it"), SetSpacingEntries, 1);

	// Picking Get Spacing places one node: the property, its target the widget, its value the property's type.
	UK2Node_DreamWidgetPartProperty* GetSpacing = PlaceFromPin<UK2Node_DreamWidgetPartProperty>(UDreamWidgetPartNodeSpawner::CreateProperty(
		UDreamLayoutContainerStackBox::StaticClass(), const_cast<FProperty*>(SpacingProperty), /*bInWrite*/false), Graph, WidgetOutput);
	if (TestNotNull(TEXT("the property action placed a node"), GetSpacing))
	{
		TestTrue(TEXT("its target the widget"), GetSpacing->GetTargetPin()->LinkedTo.Contains(WidgetOutput));
		const UEdGraphPin* Value = GetSpacing->GetValuePin();
		TestTrue(TEXT("its output a float"), Value != nullptr && Value->Direction == EGPD_Output
			&& (Value->PinType.PinCategory == UEdGraphSchema_K2::PC_Real || Value->PinType.PinCategory == UEdGraphSchema_K2::PC_Float));
	}
	UK2Node_DreamWidgetPartProperty* SetSpacing = PlaceFromPin<UK2Node_DreamWidgetPartProperty>(UDreamWidgetPartNodeSpawner::CreateProperty(
		UDreamLayoutContainerStackBox::StaticClass(), const_cast<FProperty*>(SpacingProperty), /*bInWrite*/true), Graph, WidgetOutput);
	if (TestNotNull(TEXT("and Set Spacing places one"), SetSpacing) && GetSpacing != nullptr)
	{
		// Driven, so the compile below has to expand it: the event -> Set Spacing(Get Spacing + 4).
		FGraphNodeCreator<UK2Node_CustomEvent> EventCreator(*Graph);
		UK2Node_CustomEvent* Event = EventCreator.CreateNode(false);
		EventCreator.Finalize();
		Event->CustomFunctionName = TEXT("Widen");
		Event->FindPin(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(SetSpacing->FindPin(UEdGraphSchema_K2::PN_Execute));
		SetSpacing->GetValuePin()->MakeLinkTo(GetSpacing->GetValuePin());
		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		if (!TestEqual(TEXT("a graph getting and setting the container's Spacing on the widget compiles"), Results.NumErrors, 0))
		{
			AddInfo(JoinMessages(Results));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR