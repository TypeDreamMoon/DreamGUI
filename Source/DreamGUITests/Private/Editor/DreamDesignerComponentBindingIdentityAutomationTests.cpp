// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Designer/DreamWidgetPropertyBindingExtension.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Driver/Designer/DreamSlatePanelDriver.h"
#include "DreamWidgetBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailTreeNode.h"
#include "InputCoreTypes.h"
#include "Interaction/UIEventTrigger.h"
#include "IPropertyRowGenerator.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"

namespace DreamDesignerComponentBindingIdentityTestLocal
{
	struct FScopedDesigner
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamWidgetBlueprintEditor* Designer = nullptr;

		FScopedDesigner()
		{
			const FString Name = TEXT("BP_ComponentBindingIdentity_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
			Package = CreatePackage(*(TEXT("/Temp/DreamGUITests/") + Name));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UDreamUserWidget::StaticClass(),
				Package, FName(*Name), BPTYPE_Normal, UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr)return;
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			Tree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerCanvasPanel::StaticClass());
			UDreamWidget* Subject = DreamWidgetTreeEditing::CreateWidget(Blueprint, UDreamWidget::StaticClass(), Tree->RootWidget, -1, TEXT("Subject"));
			if (Subject == nullptr)return;
			Subject->AddComponent<UUIEventTrigger>()->SetAllowEventBubbleUp(false);
			Subject->AddComponent<UUIEventTrigger>()->SetAllowEventBubbleUp(true);
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
			UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			Editors->OpenEditorForAsset(Blueprint);
			Designer = static_cast<FDreamWidgetBlueprintEditor*>(Editors->FindEditorForAsset(Blueprint, false));
			if (Designer != nullptr)Designer->GetPreviewHost()->RebuildPreviewIfInvalidated();
		}

		~FScopedDesigner()
		{
			if (Designer != nullptr)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Blueprint);
				FSlateApplication::Get().Tick();
			}
			if (Package != nullptr)Package->RemoveFromRoot();
		}

		UDreamWidget* Preview() const
		{
			UDreamWidget* Template = Blueprint != nullptr && Blueprint->WidgetTree != nullptr
				? Blueprint->WidgetTree->FindWidgetByVariableName(TEXT("Subject")) : nullptr;
			return Designer != nullptr ? Designer->GetPreviewHost()->FindPreviewForTemplate(Template) : nullptr;
		}
	};

	// The property editor's real customization builds these rows; generating their widgets directly
	// avoids depending on the Details tree's current expansion, scroll position or virtualized rows.
	TSharedPtr<SWidget> FindEventButton(const TArray<TSharedRef<IDetailTreeNode>>& Nodes, const FString& EventLabel)
	{
		using namespace DreamTests::DreamSlatePanel;
		for (const TSharedRef<IDetailTreeNode>& Node : Nodes)
		{
			const FNodeWidgets Widgets = Node->CreateNodeWidgets();
			if (Widgets.WholeRowWidget.IsValid() && FindTextBlock(Widgets.WholeRowWidget.ToSharedRef(), EventLabel).IsValid())
			{
				TSharedPtr<SWidget> Button = FindDescendant(Widgets.WholeRowWidget.ToSharedRef(), [](const TSharedRef<SWidget>& Candidate)
				{
					return Candidate->GetTypeAsString() == TEXT("SButton")
						&& FindDescendant(Candidate, [](const TSharedRef<SWidget>& Child)
						{
							return IsOfType(*Child, TEXT("SWidgetSwitcher"));
						}).IsValid();
				});
				if (Button.IsValid())return Button;
			}
			TArray<TSharedRef<IDetailTreeNode>> Children;
			Node->GetChildren(Children, true);
			if (TSharedPtr<SWidget> Button = FindEventButton(Children, EventLabel))return Button;
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDesignerReorderedComponentPropertyBindingTest,
	"DreamGUI.Designer.Components.PropertyBindingFollowsTheAuthoredInstanceAfterPreviewReorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerReorderedComponentPropertyBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerComponentBindingIdentityTestLocal;
	using namespace DreamWidgetPropertyBindingExtension;
	if (!TestTrue(TEXT("the real editor and Slate are available"), GEditor != nullptr && FSlateApplication::IsInitialized()))return false;
	FScopedDesigner Scoped;
	UDreamWidget* Preview = Scoped.Preview();
	if (!TestNotNull(TEXT("the designer has a live preview"), Preview)
		|| !TestEqual(TEXT("the widget has two same-class behaviours"), Preview->GetAllComponents().Num(), 2))return false;
	UDreamUIBehaviour* First = Preview->GetAllComponents()[0];
	UDreamUIBehaviour* Second = Preview->GetAllComponents()[1];
	Preview->MoveComponentToIndex(Second, 0);
	TestEqual(TEXT("the second authored component is now in the first preview slot"), Preview->GetAllComponents()[0], Second);
	const FProperty* Property = FindFProperty<FProperty>(UUIEventTrigger::StaticClass(), TEXT("AllowEventBubbleUp"));
	if (!TestNotNull(TEXT("the trigger exposes a bindable property"), Property)
		|| !TestTrue(TEXT("the designer offers the property binding operation"), IsBindable(Second, Property)))return false;

	const FBindingSite FirstSite = ResolveBindingSite(First);
	const FBindingSite SecondSite = ResolveBindingSite(Second);
	UDreamUIBehaviour* RuntimeOnly = Preview->AddComponent<UUIEventTrigger>();
	TestFalse(TEXT("a preview-only behaviour has no authoring site"), ResolveBindingSite(RuntimeOnly).IsValid());
	TestFalse(TEXT("a preview-only behaviour is not offered a binding"), IsBindable(RuntimeOnly, Property));
	// Use the public Create Binding operation, which creates real getter graphs and persistent routes.
	UEdGraph* FirstGraph = CreateAndBindFunction(nullptr, Scoped.Blueprint, FirstSite, Property);
	UEdGraph* SecondGraph = CreateAndBindFunction(nullptr, Scoped.Blueprint, SecondSite, Property);
	if (!TestNotNull(TEXT("the first getter graph was created"), FirstGraph)
		|| !TestNotNull(TEXT("the second getter graph was created"), SecondGraph)
		|| !TestEqual(TEXT("both authored properties have independent routes"), Scoped.Blueprint->PropertyBindings.Num(), 2))return false;
	for (const FDreamWidgetPropertyBinding& Binding : Scoped.Blueprint->PropertyBindings)
	{
		const int32 ExpectedIndex = Binding.FunctionName == FirstGraph->GetFName() ? 0 : 1;
		TestEqual(TEXT("each getter is bound to the component selected by identity"), Binding.BehaviourIndex, ExpectedIndex);
	}
	RemoveBinding(Scoped.Blueprint, SecondSite, Property->GetFName());
	if (TestEqual(TEXT("removing the second binding keeps one independent route"), Scoped.Blueprint->PropertyBindings.Num(), 1))
	{
		const FDreamWidgetPropertyBinding& Remaining = Scoped.Blueprint->PropertyBindings[0];
		TestEqual(TEXT("the remaining route belongs to the first authored behaviour"), Remaining.BehaviourIndex, 0);
		TestEqual(TEXT("the first getter remains bound"), Remaining.FunctionName, FirstGraph->GetFName());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDesignerReorderedComponentEventBindingTest,
	"DreamGUI.Designer.Components.EventAddButtonFollowsTheAuthoredInstanceAfterPreviewReorder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerReorderedComponentEventBindingTest::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerComponentBindingIdentityTestLocal;
	if (!TestTrue(TEXT("the real editor and Slate are available"), GEditor != nullptr && FSlateApplication::IsInitialized()))return false;
	FScopedDesigner Scoped;
	UDreamWidget* Preview = Scoped.Preview();
	if (!TestNotNull(TEXT("the designer has a live preview"), Preview)
		|| !TestEqual(TEXT("the widget has two same-class behaviours"), Preview->GetAllComponents().Num(), 2))return false;
	UDreamUIBehaviour* Selected = Preview->GetAllComponents()[1];
	Preview->MoveComponentToIndex(Selected, 0);
	TestEqual(TEXT("the selected behaviour moved to the first preview slot"), Preview->GetAllComponents()[0], Selected);
	const FProperty* Event = FindFProperty<FProperty>(UUIEventTrigger::StaticClass(), TEXT("OnPointerEnterBP"));
	if (!TestNotNull(TEXT("the trigger has a Blueprint event"), Event))return false;

	TSharedRef<IPropertyRowGenerator> Rows = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"))
		.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
	Rows->SetObjects(TArray<UObject*>{ Selected });
	TSharedPtr<SWidget> Button = FindEventButton(Rows->GetRootTreeNodes(), Event->GetDisplayNameText().ToString());
	if (!TestTrue(TEXT("the real event customization creates an Add button"), Button.IsValid()))return false;
	const FKeyEvent Accept(EKeys::SpaceBar, FModifierKeysState(), 0, false, 0, 0);
	TestTrue(TEXT("the event Add button accepts the keyboard press"), Button->OnKeyDown(FGeometry(), Accept).IsEventHandled());
	TestTrue(TEXT("the event Add button invokes its real handler"), Button->OnKeyUp(FGeometry(), Accept).IsEventHandled());
	if (!TestEqual(TEXT("the button creates one persistent event route"), Scoped.Blueprint->EventBindings.Num(), 1))return false;
	const FDreamWidgetEventBinding& Route = Scoped.Blueprint->EventBindings[0];
	TestEqual(TEXT("the route targets the second authored behaviour"), Route.BehaviourIndex, 1);
	TestEqual(TEXT("the route names the clicked event"), Route.EventName, Event->GetFName());
	bool bHandlerExists = false;
	for (UEdGraph* Graph : Scoped.Blueprint->UbergraphPages)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			const UK2Node_CustomEvent* Handler = Cast<UK2Node_CustomEvent>(Node);
			bHandlerExists |= Handler != nullptr && Handler->CustomFunctionName == Route.FunctionName;
		}
	}
	TestTrue(TEXT("the route has a real custom event handler"), bHandlerExists);
	return true;
}

#endif
