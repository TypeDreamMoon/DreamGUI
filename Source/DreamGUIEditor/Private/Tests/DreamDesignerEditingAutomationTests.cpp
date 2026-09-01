// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "DreamUIEditorTools.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Preview/DreamWidgetDesignerScene.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Interaction/UITextInput.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Animation/DreamWidgetAnimation.h"
#include "Core/Components/DreamText.h"
#include "Designer/DreamWidgetPropertyBindingExtension.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_FunctionEntry.h"
#include "UObject/UObjectIterator.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_VariableGet.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"

/*
 * Editing a hierarchy through the designer.
 *
 * Every one of these operations used to build or destroy the object you had selected, because the
 * thing you had selected WAS the asset. It is a preview now, rebuilt from the authoring tree, so the
 * failure mode this file exists for is specific and nasty: the edit appears, works, survives every
 * check you would think to make -- and is gone the next time anything invalidates the preview.
 *
 * So each test does the edit and then REBUILDS, and asserts against the template. An assertion that
 * only looked at the preview immediately afterwards would pass on the broken version.
 *
 * These open a real designer rather than calling the primitives, because the claim is about the
 * routing: FDreamUIEditorTools has to notice that these widgets belong to one.
 */

namespace DreamDesignerEditingTestLocal
{
	struct FScopedDesigner
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamWidgetBlueprintEditor* Designer = nullptr;

		explicit FScopedDesigner(const TCHAR* InName)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr)
			{
				return;
			}
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			Tree->RootWidget->SetDisplayName(TEXT("Root"));
			// A panel on the root, so it can accept children at all.
			Tree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerCanvasPanel::StaticClass());
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
			Designer = static_cast<FDreamWidgetBlueprintEditor*>(
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Blueprint, false));
		}

		~FScopedDesigner()
		{
			if (GEditor != nullptr && Blueprint != nullptr)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Blueprint);
				// The close is deferred, and a toolkit that is still alive still ticks. Letting the
				// package leave the root set first put a rebuild on a half-collected asset.
				FSlateApplication::Get().Tick();
			}
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		UDreamWidget* PreviewRoot() const { return Designer != nullptr ? Designer->GetPreviewRootWidget() : nullptr; }
		UDreamWidget* TemplateRoot() const { return Blueprint->WidgetTree->RootWidget.Get(); }

		/** How many widgets the ASSET holds -- the only count that means anything after a rebuild. */
		int32 TemplateCount() const { return Blueprint->WidgetTree->CountWidgets(); }

		void Rebuild() const
		{
			if (Designer != nullptr && Designer->GetPreviewHost().IsValid())
			{
				Designer->GetPreviewHost()->RebuildPreview();
			}
		}

		UDreamWidget* FindTemplate(const FString& InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			Blueprint->WidgetTree->ForEachWidget([&Found, &InDisplayName](UDreamWidget* Widget)
			{
				if (Found == nullptr && Widget->GetDisplayName() == InDisplayName)
				{
					Found = Widget;
				}
			});
			return Found;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerToolsEditTheAssetTest,
	"DreamGUI.Designer.EditingThroughTheToolsReachesTheAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerToolsEditTheAssetTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerToolsEdit"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}
	const int32 CountBefore = Scoped.TemplateCount();

	// ---- create, the way the palette and the context menu do
	UDreamWidget* PreviewRoot = Scoped.PreviewRoot();
	UDreamWidget* Created = FDreamUIEditorTools::CreateWidgetAndReturn(
		[PreviewRoot]() { return PreviewRoot; }, TEXT("Made"), nullptr, nullptr);
	if (!TestNotNull(TEXT("Creating returns a widget"), Created))
	{
		return false;
	}
	TestEqual(TEXT("The asset gained one widget"), Scoped.TemplateCount(), CountBefore + 1);
	TestNotNull(TEXT("And it is in the authoring tree by name"), Scoped.FindTemplate(TEXT("Made")));
	// The one that matters: a create that only touched the preview passes everything above.
	Scoped.Rebuild();
	TestEqual(TEXT("It is still there after a rebuild"), Scoped.TemplateCount(), CountBefore + 1);
	TestNotNull(TEXT("And still has a preview"), Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(Scoped.FindTemplate(TEXT("Made"))));

	// ---- duplicate
	UDreamWidget* MadeTemplate = Scoped.FindTemplate(TEXT("Made"));
	UDreamWidget* MadePreview = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(MadeTemplate);
	FDreamUIEditorTools::DuplicateWidgets([MadePreview]() { return TArray<UDreamWidget*>{ MadePreview }; });
	Scoped.Rebuild();
	TestEqual(TEXT("Duplicating added one more"), Scoped.TemplateCount(), CountBefore + 2);

	// Names are what template-to-preview matching runs on, and what the compiler turns into
	// variables. A duplicate that shared either would put two widgets on one identity.
	{
		TSet<FName> ObjectNames;
		TSet<FString> DisplayNames;
		int32 Total = 0;
		bool bObjectNameClash = false;
		bool bDisplayNameClash = false;
		Scoped.Blueprint->WidgetTree->ForEachWidget([&](UDreamWidget* Widget)
		{
			Total++;
			bool bSeen = false;
			ObjectNames.Add(Widget->GetFName(), &bSeen);
			bObjectNameClash |= bSeen;
			bSeen = false;
			DisplayNames.Add(Widget->GetDisplayName(), &bSeen);
			bDisplayNameClash |= bSeen;
		});
		TestFalse(TEXT("No two templates share an object name"), bObjectNameClash);
		TestFalse(TEXT("No two templates share a display name"), bDisplayNameClash);
		TestEqual(TEXT("Every widget was visited"), ObjectNames.Num(), Total);
	}

	// ---- copy and paste
	FDreamUIEditorTools::CopyWidgets([MadePreview]() { return TArray<UDreamWidget*>{ MadePreview }; });
	TestTrue(TEXT("The clipboard has something"), FDreamWidgetBlueprintEditor::DesignerHasClipboardContent());
	UDreamWidget* PasteParent = Scoped.Designer->GetPreviewRootWidget();
	FDreamUIEditorTools::PasteWidgets([PasteParent]() { return TArray<UDreamWidget*>{ PasteParent }; });
	Scoped.Rebuild();
	TestEqual(TEXT("Pasting added one"), Scoped.TemplateCount(), CountBefore + 3);
	// Twice, because a clipboard that handed over its own objects would paste the same widget again
	// and the second paste would move rather than copy.
	UDreamWidget* PasteParent2 = Scoped.Designer->GetPreviewRootWidget();
	FDreamUIEditorTools::PasteWidgets([PasteParent2]() { return TArray<UDreamWidget*>{ PasteParent2 }; });
	Scoped.Rebuild();
	TestEqual(TEXT("Pasting again added another"), Scoped.TemplateCount(), CountBefore + 4);

	// ---- delete
	UDreamWidget* ToDelete = Scoped.FindTemplate(TEXT("Made"));
	UDreamWidget* ToDeletePreview = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(ToDelete);
	FDreamUIEditorTools::DeleteWidgets([ToDeletePreview]() { return TArray<UDreamWidget*>{ ToDeletePreview }; },
		FDreamUIEditorTools::EDeleteWidgetWarningType::DeleteSilently);
	Scoped.Rebuild();
	TestEqual(TEXT("Deleting removed one"), Scoped.TemplateCount(), CountBefore + 3);
	TestNull(TEXT("And it is gone from the authoring tree"), Scoped.FindTemplate(TEXT("Made")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRenameReachesTheAssetTest,
	"DreamGUI.Designer.RenamingReachesTheAssetAndTheVariable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerRenameReachesTheAssetTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerRename"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}
	UDreamWidget* PreviewRoot = Scoped.PreviewRoot();
	UDreamWidget* Created = FDreamUIEditorTools::CreateWidgetAndReturn(
		[PreviewRoot]() { return PreviewRoot; }, TEXT("Before"), nullptr, nullptr);
	if (!TestNotNull(TEXT("A widget to rename"), Created))
	{
		return false;
	}

	const FString Applied = Scoped.Designer->DesignerRenameWidget(Created, TEXT("After"));
	TestEqual(TEXT("The name asked for is the name applied"), Applied, FString(TEXT("After")));
	Scoped.Rebuild();
	TestNotNull(TEXT("The asset carries the new name"), Scoped.FindTemplate(TEXT("After")));
	TestNull(TEXT("And not the old one"), Scoped.FindTemplate(TEXT("Before")));

	// The display name IS the compiler's variable name, so a rename has to survive a compile.
	FKismetEditorUtilities::CompileBlueprint(Scoped.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	TestNotNull(TEXT("The class declares a variable of the new name"),
		Scoped.Blueprint->GeneratedClass->FindPropertyByName(FName(TEXT("After"))));
	TestNull(TEXT("And none of the old one"),
		Scoped.Blueprint->GeneratedClass->FindPropertyByName(FName(TEXT("Before"))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerComponentsAndPropertiesReachTheAssetTest,
	"DreamGUI.Designer.ComponentsAndDetailsEditsReachTheAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerComponentsAndPropertiesReachTheAssetTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerComponentsAndProperties"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}
	UDreamWidget* PreviewRoot = Scoped.PreviewRoot();
	UDreamWidget* Created = FDreamUIEditorTools::CreateWidgetAndReturn(
		[PreviewRoot]() { return PreviewRoot; }, TEXT("Subject"), nullptr, nullptr);
	if (!TestNotNull(TEXT("A widget to work on"), Created))
	{
		return false;
	}

	// ---- a behaviour is an instanced sub-object, so adding one to a preview builds it into the copy
	UClass* BehaviourClass = UDreamUIBehaviour::StaticClass();
	TArray<UClass*> ToAdd;
	// Any concrete behaviour will do; the claim is about where it lands, not what it does.
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(BehaviourClass) && *It != BehaviourClass
			&& !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			ToAdd.Add(*It);
			break;
		}
	}
	if (ToAdd.Num() > 0)
	{
		UDreamUIBehaviour* Added = Scoped.Designer->DesignerAddComponents(Created, ToAdd);
		TestNotNull(TEXT("A behaviour was added and has a preview"), Added);
		UDreamWidget* SubjectTemplate = Scoped.FindTemplate(TEXT("Subject"));
		if (TestNotNull(TEXT("The subject is in the asset"), SubjectTemplate))
		{
			TestEqual(TEXT("The TEMPLATE carries the behaviour"), SubjectTemplate->GetAllComponents().Num(), 1);
		}
		Scoped.Rebuild();
		SubjectTemplate = Scoped.FindTemplate(TEXT("Subject"));
		TestEqual(TEXT("And still does after a rebuild"), SubjectTemplate->GetAllComponents().Num(), 1);

		// Removal crosses the split by POSITION: the caller holds the preview's component, and the
		// template's is a different object that shares no name with it.
		UDreamWidget* Subject = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(SubjectTemplate);
		if (TestNotNull(TEXT("The subject has a preview to remove from"), Subject)
			&& TestEqual(TEXT("Which shows the behaviour"), Subject->GetAllComponents().Num(), 1))
		{
			TestTrue(TEXT("The removal was accepted"),
				Scoped.Designer->DesignerRemoveComponent(Subject, Subject->GetAllComponents()[0]));
			Scoped.Rebuild();
			SubjectTemplate = Scoped.FindTemplate(TEXT("Subject"));
			TestEqual(TEXT("And the ASSET lost the behaviour"), SubjectTemplate->GetAllComponents().Num(), 0);
		}

		// Put one back, so the details-panel half below runs against the same shape as before.
		Scoped.Designer->DesignerAddComponents(
			Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(SubjectTemplate), ToAdd);
	}

	// ---- a details-panel edit, through the same call the panel's notify hook makes
	UDreamWidget* SubjectTemplate = Scoped.FindTemplate(TEXT("Subject"));
	UDreamWidget* SubjectPreview = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(SubjectTemplate);
	if (TestNotNull(TEXT("The subject has a preview"), SubjectPreview))
	{
		Scoped.Designer->SelectWidgets(TSet<UDreamWidget*>{ SubjectPreview }, false, false);
		SubjectPreview->SetSizeDelta(FVector2D(77.0, 88.0));

		FProperty* AnchorDataProperty = UDreamWidget::StaticClass()->FindPropertyByName(UDreamWidget::GetPropertyName_AnchorData());
		FEditPropertyChain Chain;
		Chain.AddHead(AnchorDataProperty);
		Scoped.Designer->MigrateDetailsChangeToTemplate({ SubjectPreview }, Chain, /*bIsModify*/false);

		TestEqual(TEXT("The asset took the edited width"), (float)SubjectTemplate->GetSizeDelta().X, 77.0f, 0.001f);
		Scoped.Rebuild();
		UDreamWidget* Rebuilt = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(Scoped.FindTemplate(TEXT("Subject")));
		if (TestNotNull(TEXT("Still previewed after a rebuild"), Rebuilt))
		{
			TestEqual(TEXT("And the rebuilt preview shows it"), (float)Rebuilt->GetSizeDelta().X, 77.0f, 0.001f);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerWrapReachesTheAssetTest,
	"DreamGUI.Designer.WrapWithReachesTheAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerWrapReachesTheAssetTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerWrap"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}
	UDreamWidget* PreviewRoot = Scoped.PreviewRoot();
	FDreamUIEditorTools::CreateWidgetAndReturn([PreviewRoot]() { return PreviewRoot; }, TEXT("A"), nullptr, nullptr);
	// PreviewRoot is re-read: creating A rebuilt the preview, so the pointer above is stale. Every
	// structural edit does this, and it is the same contract FDreamWidgetReference documents --
	// resolve a preview when you need it, never keep one across an edit.
	PreviewRoot = Scoped.PreviewRoot();
	FDreamUIEditorTools::CreateWidgetAndReturn([PreviewRoot]() { return PreviewRoot; }, TEXT("B"), nullptr, nullptr);

	UDreamWidget* A = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(Scoped.FindTemplate(TEXT("A")));
	UDreamWidget* B = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(Scoped.FindTemplate(TEXT("B")));
	if (!TestNotNull(TEXT("Two widgets to wrap"), A) || B == nullptr)
	{
		return false;
	}
	const int32 CountBefore = Scoped.TemplateCount();

	// Wrap runs on the preview first because enclosing a selection needs real geometry; this is the
	// mirroring half, which is the part that reaches the asset.
	Scoped.Designer->SelectWidgets(TSet<UDreamWidget*>{ A, B }, false, false);
	Scoped.Designer->WrapSelectedWidgets(UDreamLayoutContainerVerticalBox::StaticClass());

	TestEqual(TEXT("The asset gained exactly the wrapper"), Scoped.TemplateCount(), CountBefore + 1);
	Scoped.Rebuild();
	TestEqual(TEXT("And still has it after a rebuild"), Scoped.TemplateCount(), CountBefore + 1);

	UDreamWidget* TemplateA = Scoped.FindTemplate(TEXT("A"));
	UDreamWidget* TemplateB = Scoped.FindTemplate(TEXT("B"));
	if (TestNotNull(TEXT("A survived"), TemplateA) && TemplateB != nullptr)
	{
		TestNotEqual(TEXT("A is no longer under the root"), (void*)TemplateA->GetParent(), (void*)Scoped.TemplateRoot());
		TestEqual(TEXT("A and B share their new parent"), TemplateA->GetParent(), TemplateB->GetParent());
		TestNotNull(TEXT("And that parent is the wrapper, with the panel it was asked for"),
			TemplateA->GetParent()->GetLayoutContainer());
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDeleteWarnsAboutGraphUseTest,
	"DreamGUI.Designer.DeletingAWidgetTheGraphsUseIsReportedFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerDeleteWarnsAboutGraphUseTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerDeleteWarning"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}
	UDreamWidget* PreviewRoot = Scoped.PreviewRoot();
	UDreamWidget* Used = FDreamUIEditorTools::CreateWidgetAndReturn(
		[PreviewRoot]() { return PreviewRoot; }, TEXT("UsedByGraph"), nullptr, nullptr);
	PreviewRoot = Scoped.PreviewRoot();
	UDreamWidget* Unused = FDreamUIEditorTools::CreateWidgetAndReturn(
		[PreviewRoot]() { return PreviewRoot; }, TEXT("NobodyReadsThis"), nullptr, nullptr);
	if (!TestNotNull(TEXT("A widget to reference"), Used) || !TestNotNull(TEXT("And one to leave alone"), Unused))
	{
		return false;
	}
	// The variables are the compiler's, so they only exist once it has run.
	FKismetEditorUtilities::CompileBlueprint(Scoped.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Scoped.Blueprint);
	if (!TestNotNull(TEXT("The Blueprint has an event graph"), Graph))
	{
		return false;
	}
	UK2Node_VariableGet* Getter = NewObject<UK2Node_VariableGet>(Graph);
	Getter->VariableReference.SetSelfMember(FName(TEXT("UsedByGraph")));
	Graph->AddNode(Getter, /*bFromUI*/false, /*bSelectNewNode*/false);
	Getter->CreateNewGuid();
	Getter->PostPlacedNewNode();
	Getter->AllocateDefaultPins();

	// Compiling reinstances the generated class, so the preview instance built from the old one is
	// trashed and the name map points at nothing. Republish before asking it for anything.
	Scoped.Rebuild();
	UDreamWidget* UsedPreview = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(Scoped.FindTemplate(TEXT("UsedByGraph")));
	UDreamWidget* UnusedPreview = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(Scoped.FindTemplate(TEXT("NobodyReadsThis")));
	if (!TestNotNull(TEXT("The referenced widget has a preview"), UsedPreview)
		|| !TestNotNull(TEXT("So does the other one"), UnusedPreview))
	{
		return false;
	}

	const TArray<FText> ForUsed = Scoped.Designer->CollectGraphReferencesToWidgets({ UsedPreview });
	TestEqual(TEXT("The graph use is reported"), ForUsed.Num(), 1);
	if (ForUsed.Num() == 1)
	{
		TestTrue(TEXT("And it names the variable"), ForUsed[0].ToString().Contains(TEXT("UsedByGraph")));
	}

	// The half that makes the report worth anything: a widget nothing reads must not be reported,
	// or the prompt appears on every delete and stops being read.
	TestEqual(TEXT("An unreferenced widget is not reported"),
		Scoped.Designer->CollectGraphReferencesToWidgets({ UnusedPreview }).Num(), 0);

	// Descendants count: deleting a parent takes its children's variables with it.
	UDreamWidget* RootPreview = Scoped.PreviewRoot();
	TestEqual(TEXT("Deleting the parent reports the child's use"),
		Scoped.Designer->CollectGraphReferencesToWidgets({ RootPreview }).Num(), 1);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerBindingIsAuthoredAgainstTheRightObjectTest,
	"DreamGUI.Designer.BindingIsOfferedAndAuthoredAgainstTheRightObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerBindingIsAuthoredAgainstTheRightObjectTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;
	using namespace DreamWidgetPropertyBindingExtension;

	FScopedDesigner Scoped(TEXT("DesignerBinding"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}
	UDreamWidget* PreviewRoot = Scoped.PreviewRoot();
	FDreamUIEditorTools::CreateWidgetAndReturn([PreviewRoot]() { return PreviewRoot; },
		TEXT("Label"), UDreamText::StaticClass(), nullptr);

	UDreamWidget* LabelTemplate = Scoped.FindTemplate(TEXT("Label"));
	UDreamWidget* Label = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(LabelTemplate);
	if (!TestNotNull(TEXT("The widget has a preview"), Label))
	{
		return false;
	}
	UDreamText* Text = Cast<UDreamText>(Label->GetVisual());
	if (!TestNotNull(TEXT("And a text visual"), Text))
	{
		return false;
	}

	// A visual has no name of its own, so a binding on one has to be found on the widget that owns it.
	const FBindingSite Site = ResolveBindingSite(Text);
	TestTrue(TEXT("The visual resolves to a site"), Site.IsValid());
	TestEqual(TEXT("Named after the WIDGET"), Site.WidgetName, FName(TEXT("Label")));
	TestTrue(TEXT("And marked as the visual"), Site.Target == EDreamWidgetBindingTarget::Visual);

	// Offered exactly where the compiler would accept it: a property with a setter, and not one
	// without. The two answers come from one function so the panel cannot offer what compiling refuses.
	FProperty* Kerning = UDreamText::StaticClass()->FindPropertyByName(FName(TEXT("bUseKerning")));
	if (TestNotNull(TEXT("The settable property exists"), Kerning))
	{
		TestTrue(TEXT("It is offered"), IsBindable(Text, Kerning));
	}
	FProperty* NoSetter = UDreamText::StaticClass()->FindPropertyByName(
		FName(TEXT("WidgetPropertyDataStartPosition")));
	if (NoSetter != nullptr)
	{
		TestFalse(TEXT("A property with no setter is not"), IsBindable(Text, NoSetter));
	}

	// Authoring writes to the Blueprint, which is the only place a binding can survive a rebuild.
	SetBinding(Scoped.Blueprint, Site, FName(TEXT("bUseKerning")), FName(TEXT("IsInitialized")));
	TestEqual(TEXT("The asset carries the binding"), Scoped.Blueprint->PropertyBindings.Num(), 1);
	if (Scoped.Blueprint->PropertyBindings.Num() == 1)
	{
		const FDreamWidgetPropertyBinding& Authored = Scoped.Blueprint->PropertyBindings[0];
		TestEqual(TEXT("Against the widget's name"), Authored.WidgetName, FName(TEXT("Label")));
		TestTrue(TEXT("On the visual"), Authored.Target == EDreamWidgetBindingTarget::Visual);
		TestEqual(TEXT("Calling the chosen function"), Authored.FunctionName, FName(TEXT("IsInitialized")));
	}

	// Rebinding replaces rather than accumulates: two bindings on one property would both run, and
	// which one won would be array order.
	SetBinding(Scoped.Blueprint, Site, FName(TEXT("bUseKerning")), FName(TEXT("IsRegistered")));
	TestEqual(TEXT("Rebinding replaces"), Scoped.Blueprint->PropertyBindings.Num(), 1);

	RemoveBinding(Scoped.Blueprint, Site, FName(TEXT("bUseKerning")));
	TestEqual(TEXT("And removal clears it"), Scoped.Blueprint->PropertyBindings.Num(), 0);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerCreateBindingBuildsAUsableFunctionTest,
	"DreamGUI.Designer.CreateBindingBuildsAFunctionThatCompilesAndBinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerCreateBindingBuildsAUsableFunctionTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;
	using namespace DreamWidgetPropertyBindingExtension;

	FScopedDesigner Scoped(TEXT("DesignerCreateBinding"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}
	UDreamWidget* PreviewRoot = Scoped.PreviewRoot();
	FDreamUIEditorTools::CreateWidgetAndReturn([PreviewRoot]() { return PreviewRoot; },
		TEXT("Label"), UDreamText::StaticClass(), nullptr);
	UDreamWidget* Label = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(Scoped.FindTemplate(TEXT("Label")));
	UDreamText* Text = IsValid(Label) ? Cast<UDreamText>(Label->GetVisual()) : nullptr;
	if (!TestNotNull(TEXT("A text visual to bind on"), Text))
	{
		return false;
	}
	const FBindingSite Site = ResolveBindingSite(Text);
	FProperty* TextProperty = UDreamText::StaticClass()->FindPropertyByName(FName(TEXT("Text")));
	if (!TestTrue(TEXT("The site resolves"), Site.IsValid()) || !TestNotNull(TEXT("Text exists"), TextProperty))
	{
		return false;
	}

	// The menu entry, minus the opening: a null designer skips only that.
	UEdGraph* Graph = CreateAndBindFunction(nullptr, Scoped.Blueprint, Site, TextProperty);
	if (!TestNotNull(TEXT("A function graph was built"), Graph))
	{
		return false;
	}

	// Pure: a binding is asked for a value, and an exec pin invites side effects in something that
	// runs every frame.
	UK2Node_FunctionEntry* Entry = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionEntry* Candidate = Cast<UK2Node_FunctionEntry>(Node))
		{
			Entry = Candidate;
			break;
		}
	}
	if (TestNotNull(TEXT("It has an entry node"), Entry))
	{
		TestTrue(TEXT("And is marked pure"), (Entry->GetExtraFlags() & FUNC_BlueprintPure) != 0);
	}

	// Bound to what was built, not to something the author still has to pick.
	const FDreamWidgetPropertyBinding* Authored = FindBinding(Scoped.Blueprint, Site, FName(TEXT("Text")));
	if (TestNotNull(TEXT("The property is bound"), Authored))
	{
		TestEqual(TEXT("To the new function"), Authored->FunctionName, Graph->GetFName());
	}

	// The real claim: the compiler accepts it. A graph of the wrong shape -- no return value, wrong
	// type, parameters -- would build fine here and fail only when someone compiled.
	FKismetEditorUtilities::CompileBlueprint(Scoped.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	UDreamWidgetGeneratedClass* GeneratedClass = Cast<UDreamWidgetGeneratedClass>(Scoped.Blueprint->GeneratedClass.Get());
	if (TestNotNull(TEXT("A class came out"), GeneratedClass))
	{
		TestEqual(TEXT("And carries the resolved binding"), GeneratedClass->GetPropertyBindings().Num(), 1);

		UFunction* Built = GeneratedClass->FindFunctionByName(Graph->GetFName());
		if (TestNotNull(TEXT("The function is on the class"), Built))
		{
			// One parm and it is the return: the shape CompilePropertyBindings insists on, which is
			// why the binding above survived the compile rather than being reported.
			TestEqual(TEXT("It takes no arguments"), (int32)Built->NumParms, 1);
			const FProperty* Return = Built->GetReturnProperty();
			if (TestNotNull(TEXT("It returns something"), Return))
			{
				TestTrue(TEXT("Of the bound property's type"), Return->SameType(TextProperty));
			}
		}
	}

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerCreateUnderTheCanvasTest,
	"DreamGUI.Designer.CreatingWithTheDesignCanvasSelectedLandsOnTheAuthoredRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerCreateUnderTheCanvasTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerCanvasParent"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer))
	{
		return false;
	}
	// The design canvas, which is what the palette hands over when nothing in the tree is selected --
	// and what the user has selected whenever they click the top row of the hierarchy. It is not a
	// widget anyone can parent to in a level, so the tools refuse it; in a designer it has to mean
	// the authored root, or a palette double-click there does nothing but log.
	FDreamWidgetDesignerScene* Scene = Scoped.Designer->GetPreviewScene();
	UDreamWidget* Canvas = Scene != nullptr ? Scene->GetRootAgent() : nullptr;
	if (!TestNotNull(TEXT("There is a design canvas"), Canvas))
	{
		return false;
	}
	TestTrue(TEXT("It really is the root agent"), FDreamWidgetBlueprintEditor::WidgetIsRootAgent(Canvas));
	TestTrue(TEXT("And the tools accept it as a parent inside a designer"),
		FDreamUIEditorTools::IsWidgetCompatibleWithDreamUIToolsMenu(Canvas));

	UDreamWidget* Created = FDreamUIEditorTools::CreateWidgetAndReturn(
		[Canvas]() { return Canvas; }, TEXT("FromCanvas"), nullptr, nullptr);
	TestNotNull(TEXT("A widget is created"), Created);

	Scoped.Rebuild();
	UDreamWidget* Template = Scoped.FindTemplate(TEXT("FromCanvas"));
	if (TestNotNull(TEXT("And it reached the asset"), Template))
	{
		// Under the authored root, not hanging off the canvas -- the canvas is not in the asset.
		TestEqual(TEXT("Parented to the authored root"),
			Template->GetParent(), Scoped.Blueprint->WidgetTree->RootWidget.Get());
	}

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPlaceAControlTest,
	"DreamGUI.Designer.PlacingAPaletteControlReachesTheAssetAsAnInstanceOfItsClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerPlaceAControlTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerPlaceControl"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}

	// The palette names a control by its ASSET path, and the class is the Blueprint's generated one.
	// The designer branch resolved it a second way -- LoadClass on that asset path -- which returns
	// null for every control in the palette, so double-clicking one did nothing but log.
	UDreamWidget* PreviewRoot = Scoped.PreviewRoot();
	UDreamWidget* Created = FDreamUIEditorTools::CreateUIControlsAndReturn(
		[PreviewRoot]() { return PreviewRoot; }, TEXT("/DreamGUI/Controls/BP_TextInput"), nullptr);
	if (!TestNotNull(TEXT("The control is placed"), Created))
	{
		return false;
	}

	Scoped.Rebuild();
	// Named after the Blueprint. The generated class is BP_TextInput_C, and that suffix would be what
	// the hierarchy showed and what the compiler made a variable of.
	UDreamWidget* Template = Scoped.FindTemplate(TEXT("BP_TextInput"));
	TestNull(TEXT("Not named after the generated class"), Scoped.FindTemplate(TEXT("BP_TextInput_C")));
	if (TestNotNull(TEXT("And it reached the asset"), Template))
	{
		// An INSTANCE of the control's class, not a flattened copy of its widgets -- which is the
		// whole reason controls became classes: fixing the shipped one reaches the ones already placed.
		UBlueprint* ControlBlueprint = LoadObject<UBlueprint>(nullptr, TEXT("/DreamGUI/Controls/BP_TextInput.BP_TextInput"));
		if (TestNotNull(TEXT("The control Blueprint loads"), ControlBlueprint))
		{
			TestTrue(TEXT("The placed widget is of its generated class"),
				Template->GetClass() == ControlBlueprint->GeneratedClass.Get());
		}
	}

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerComponentPropertyEditReachesTheAssetTest,
	"DreamGUI.Designer.EditingAComponentPropertyReachesTheComponentNotTheWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerComponentPropertyEditReachesTheAssetTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerComponentPropertyEdit"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}
	UDreamWidget* PreviewRoot = Scoped.PreviewRoot();
	UDreamWidget* Created = FDreamUIEditorTools::CreateWidgetAndReturn(
		[PreviewRoot]() { return PreviewRoot; }, TEXT("Host"), nullptr, nullptr);
	if (!TestNotNull(TEXT("A widget to hang a component on"), Created))
	{
		return false;
	}
	TArray<UClass*> ToAdd{ UUITextInput::StaticClass() };
	UDreamUIBehaviour* Component = Scoped.Designer->DesignerAddComponents(Created, ToAdd);
	if (!TestNotNull(TEXT("The component was added"), Component))
	{
		return false;
	}

	FProperty* MultiLine = UUITextInput::StaticClass()->FindPropertyByName(FName(TEXT("bAllowMultiLine")));
	if (!TestNotNull(TEXT("The component property exists"), MultiLine))
	{
		return false;
	}

	// The details panel shows a widget, its visual and its behaviours as peers. This is the edit made
	// on the COMPONENT, and the migration used to apply the chain to the WIDGET -- which asserts,
	// because bAllowMultiLine does not belong to UDreamWidget, and never reached the asset either.
	Cast<UUITextInput>(Component)->SetAllowMultiLine(true);
	FEditPropertyChain Chain;
	Chain.AddHead(MultiLine);
	Scoped.Designer->MigrateDetailsChangeToTemplate({ Component }, Chain, /*bIsModify*/false);

	UDreamWidget* HostTemplate = Scoped.FindTemplate(TEXT("Host"));
	if (TestNotNull(TEXT("The host is in the asset"), HostTemplate)
		&& TestEqual(TEXT("With its component"), HostTemplate->GetAllComponents().Num(), 1))
	{
		UUITextInput* TemplateInput = Cast<UUITextInput>(HostTemplate->GetAllComponents()[0]);
		if (TestNotNull(TEXT("Of the right class"), TemplateInput))
		{
			TestTrue(TEXT("And the edit reached the TEMPLATE's component"), TemplateInput->GetAllowMultiLine());
		}
	}

	// A chain whose property belongs to something else must be refused, not applied. This is the
	// assertion itself: applying a component's property to a widget crashes the editor outright.
	UDreamWidget* HostPreview = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(HostTemplate);
	if (TestNotNull(TEXT("The host has a preview"), HostPreview))
	{
		FEditPropertyChain Mismatched;
		Mismatched.AddHead(MultiLine);
		Scoped.Designer->MigrateDetailsChangeToTemplate({ HostPreview }, Mismatched, /*bIsModify*/false);
		TestTrue(TEXT("Migrating a component property onto a widget is refused rather than fatal"), true);
	}

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerAnimationReachesTheAssetTest,
	"DreamGUI.Designer.AnAnimationAuthoredInTheDesignerReachesTheAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerAnimationReachesTheAssetTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerAnimationHost"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer))
	{
		return false;
	}

	// The panel asks the editor where animations live. Under prefabs that was the one tree there was;
	// in a designer the preview is a copy that gets rebuilt, so an animation authored there is gone
	// the next time anything structural happens -- and never in the saved asset at all.
	UDreamWidget* Host = Scoped.Designer->GetAnimationHostWidget();
	if (!TestNotNull(TEXT("There is a widget to host animations"), Host))
	{
		return false;
	}
	UDreamWidgetTree* Tree = Scoped.Blueprint->WidgetTree;
	if (!TestNotNull(TEXT("The asset has a tree"), Tree))
	{
		return false;
	}
	TestEqual(TEXT("And it is the AUTHORED root, not the preview's"), Host, Tree->RootWidget.Get());

	// Which means a new animation lands in the asset.
	UDreamWidgetAnimationComponent* Animator =
		Cast<UDreamWidgetAnimationComponent>(Host->AddComponent(UDreamWidgetAnimationComponent::StaticClass()));
	if (TestNotNull(TEXT("An animation host component"), Animator))
	{
		UDreamWidgetAnimation* Sequence = Animator->AddNewAnimation();
		TestNotNull(TEXT("And an animation on it"), Sequence);

		Scoped.Rebuild();
		// Survives the rebuild, because it was never on the thing that gets rebuilt.
		UDreamWidget* RootAfter = Scoped.Blueprint->WidgetTree->RootWidget.Get();
		UDreamWidgetAnimationComponent* AnimatorAfter =
			IsValid(RootAfter) ? RootAfter->GetComponent<UDreamWidgetAnimationComponent>() : nullptr;
		if (TestNotNull(TEXT("The asset still carries the host"), AnimatorAfter))
		{
			TestEqual(TEXT("And the animation"), AnimatorAfter->GetSequenceArray().Num(), 1);
		}
	}

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRecompileDoesNotOrphanThePreviewTest,
	"DreamGUI.Designer.ARecompileDoesNotOrphanThePreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerRecompileDoesNotOrphanThePreviewTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerRecompilePreview"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer))
	{
		return false;
	}
	UDreamWidget* PreviewBefore = Scoped.PreviewRoot();
	if (!TestNotNull(TEXT("With a preview"), PreviewBefore))
	{
		return false;
	}
	TestTrue(TEXT("Which is registered in the preview world"), PreviewBefore->HasRegistered());

	// A recompile reinstances the preview along with every other instance of the class. Nothing here
	// asks the designer for anything afterwards, deliberately: the claim is about the object left
	// BEHIND, which no reachable pointer names any more.
	FKismetEditorUtilities::CompileBlueprint(Scoped.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

	// Registered and unowned is the defect. Left that way it survives until GC, and then reports
	// itself through UDreamWidget's last-resort cleanup at Error verbosity -- in whichever test was
	// running at the time, which is why this went unattributed for so long.
	TestFalse(TEXT("The replaced preview was unregistered, not abandoned"), PreviewBefore->HasRegistered());

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerClipboardKeepsSameNamedWidgetsApartTest,
	"DreamGUI.Designer.CopyingTwoSameNamedWidgetsPastesTwo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerClipboardKeepsSameNamedWidgetsApartTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerClipboardNames"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}
	// Two widgets that share a display name. The clipboard used to be keyed by name, so a pair like
	// this collapsed into one entry and pasted as one widget -- silently, which is the only reason it
	// survived as long as it did.
	UDreamWidget* PreviewRoot = Scoped.PreviewRoot();
	UDreamWidget* First = FDreamUIEditorTools::CreateWidgetAndReturn(
		[PreviewRoot]() { return PreviewRoot; }, TEXT("Twin"), nullptr, nullptr);
	UDreamWidget* Second = FDreamUIEditorTools::CreateWidgetAndReturn(
		[PreviewRoot]() { return PreviewRoot; }, TEXT("Twin"), nullptr, nullptr);
	if (!TestNotNull(TEXT("First twin created"), First) || !TestNotNull(TEXT("Second twin created"), Second))
	{
		return false;
	}
	Scoped.Rebuild();
	const int32 CountBefore = Scoped.TemplateCount();

	UDreamWidget* FirstPreview = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(Scoped.FindTemplate(TEXT("Twin")));
	TArray<UDreamWidget*> ToCopy;
	Scoped.Designer->GetPreviewHost()->GetPreviewRoot()->GetChildren();
	for (UDreamWidget* Child : Scoped.Designer->GetPreviewHost()->GetPreviewRoot()->GetChildren())
	{
		if (IsValid(Child) && Child->GetDisplayName().StartsWith(TEXT("Twin")))
		{
			ToCopy.Add(Child);
		}
	}
	if (!TestEqual(TEXT("Two same-named widgets to copy"), ToCopy.Num(), 2))
	{
		return false;
	}
	Scoped.Designer->DesignerCopyWidgets(ToCopy);
	TestTrue(TEXT("The clipboard has content"), FDreamWidgetBlueprintEditor::DesignerHasClipboardContent());

	const TArray<UDreamWidget*> Pasted = Scoped.Designer->DesignerPasteWidgets(Scoped.Designer->GetPreviewHost()->GetPreviewRoot());
	TestEqual(TEXT("Pasting two same-named widgets yields two"), Pasted.Num(), 2);
	Scoped.Rebuild();
	TestEqual(TEXT("And the asset gained two"), Scoped.TemplateCount(), CountBefore + 2);

	return true;
}

/*
 * After a structural edit the preview comes back BY ITSELF.
 *
 * Every other test in this file calls Rebuild() straight after the edit, because what they are
 * asserting is that the edit reached the asset. That call is also what hides this: a structural edit
 * recompiles the Blueprint, reinstancing replaces the preview object, the host tears the old one
 * down -- and if nothing puts it back, the designer is left showing an empty canvas until the author
 * does something that happens to rebuild. A manual Rebuild() in the test IS that something.
 *
 * So this one edits and then ticks, which is all the running editor does, and asks the viewport's
 * question rather than the asset's: is there a preview, and does it have the widgets.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPreviewSurvivesAStructuralEditTest,
	"DreamGUI.Designer.ThePreviewComesBackAfterAStructuralEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerPreviewSurvivesAStructuralEditTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("PreviewSurvivesEdit"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}

	UDreamWidget* PreviewRoot = Scoped.PreviewRoot();
	UDreamWidget* Created = FDreamUIEditorTools::CreateWidgetAndReturn(
		[PreviewRoot]() { return PreviewRoot; }, TEXT("Kept"), nullptr, nullptr);
	if (!TestNotNull(TEXT("Creating returns a widget"), Created))
	{
		return false;
	}

	// One tick, the way the editor pays for a stale preview. No manual RebuildPreview anywhere.
	Scoped.Designer->Tick(0.0f);

	if (!TestNotNull(TEXT("There is still a preview root after the edit"), Scoped.PreviewRoot()))
	{
		return false;
	}
	TestNotNull(TEXT("and the created widget has a preview counterpart"),
		Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(Scoped.FindTemplate(TEXT("Kept"))));

	// Duplicate is the same gesture with more of the tree moving, and it is the one that was seen
	// blanking the canvas in the running editor.
	UDreamWidget* KeptPreview = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(
		Scoped.FindTemplate(TEXT("Kept")));
	FDreamUIEditorTools::DuplicateWidgets([KeptPreview]() { return TArray<UDreamWidget*>{ KeptPreview }; });
	Scoped.Designer->Tick(0.0f);

	if (!TestNotNull(TEXT("There is still a preview root after a duplicate"), Scoped.PreviewRoot()))
	{
		return false;
	}
	// The count is the part an "is it null" check cannot see: a preview that rebuilt from an empty
	// class is not null, it is blank.
	int32 PreviewCount = 0;
	TFunction<void(UDreamWidget*)> Count = [&Count, &PreviewCount](UDreamWidget* Widget)
	{
		if (Widget == nullptr) { return; }
		PreviewCount++;
		for (UDreamWidget* Child : Widget->GetChildren()) { Count(Child); }
	};
	Count(Scoped.PreviewRoot());
	TestEqual(TEXT("and it holds one preview widget per template widget"), PreviewCount, Scoped.TemplateCount());
	return true;
}

/*
 * Copy and paste move a whole subtree, and copying does not empty what it copied.
 *
 * The clipboard is a UDreamWidgetTree of its own and the copy into it was DuplicateObject, which has
 * the same hole the designer's duplicate had: children are outered to the tree, not to their parent,
 * so they are not duplicated -- the clipboard copy came out pointing at the ASSET's live children,
 * and RestoreParentLinksRecursive then re-parented them onto it. Copying a panel took its contents
 * out of the hierarchy. The canvas going blank in the running editor is what that looks like.
 *
 * The existing copy/paste test copies a leaf, which cannot tell any of this apart.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerClipboardCarriesTheSubtreeTest,
	"DreamGUI.Designer.CopyingAWidgetWithChildrenLeavesItIntact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerClipboardCarriesTheSubtreeTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("ClipboardSubtree"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}
	UDreamWidget* Root = Scoped.TemplateRoot();
	UDreamWidget* Panel = DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), Root, -1, TEXT("Panel"));
	UDreamWidget* ChildA = DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), Panel, -1, TEXT("ChildA"));
	DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), ChildA, -1, TEXT("Grandchild"));
	DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), Panel, -1, TEXT("ChildB"));
	Scoped.Rebuild();
	const int32 CountBefore = Scoped.TemplateCount();

	UDreamWidget* PanelPreview = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(Panel);
	if (!TestNotNull(TEXT("the panel has a preview to select"), PanelPreview))
	{
		return false;
	}

	FDreamUIEditorTools::CopyWidgets([PanelPreview]() { return TArray<UDreamWidget*>{ PanelPreview }; });
	TestTrue(TEXT("the clipboard has something"), FDreamWidgetBlueprintEditor::DesignerHasClipboardContent());

	// The half that was broken: a copy must not reach into the asset.
	TestEqual(TEXT("copying changed nothing in the asset"), Scoped.TemplateCount(), CountBefore);
	TestEqual(TEXT("the copied panel still has both children"), Panel->GetChildren().Num(), 2);
	TestEqual(TEXT("ChildA is still under it"), ChildA->GetParent(), Panel);
	TestEqual(TEXT("and still has its grandchild"), ChildA->GetChildren().Num(), 1);

	UDreamWidget* PasteParent = Scoped.Designer->GetPreviewRootWidget();
	FDreamUIEditorTools::PasteWidgets([PasteParent]() { return TArray<UDreamWidget*>{ PasteParent }; });
	Scoped.Rebuild();
	TestEqual(TEXT("pasting brought the whole subtree, not just its root"),
		Scoped.TemplateCount(), CountBefore + 4);
	TestEqual(TEXT("and the original is still whole"), Panel->GetChildren().Num(), 2);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerSubObjectEditsReachTheAssetTest,
	"DreamGUI.Designer.SubObjectEditsReachTheAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The mirror has to carry the sub-objects a details panel edits, not only the widget itself.
 *
 * Most of what a designer touches does not live on the UDreamWidget: Padding and the alignments are
 * on its UDreamPanelSlot, spacing and layout rules on its UDreamLayoutContainer. Those rows reach the
 * panel through AddExternalObjects and used to be dropped by the mirror, which asked
 * ResolveBindingSite -- the right question for BINDINGS, which can name only the widget, its visual
 * and its behaviours.
 *
 * Nothing about it looked broken: the preview moved, the viewport re-laid-out, and the value stayed
 * until the next rebuild. Downstream it read as a write-back failure, because a .dui is written by
 * comparing the file against the ASSET, and the asset never heard about the edit.
 */
bool FDreamDesignerSubObjectEditsReachTheAssetTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerSubObjectEdits"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}
	UDreamWidget* ChildTemplate = DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), Scoped.TemplateRoot(), -1, TEXT("Child"));
	Scoped.Rebuild();
	ChildTemplate = Scoped.FindTemplate(TEXT("Child"));
	UDreamWidget* ChildPreview = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(ChildTemplate);
	if (!TestNotNull(TEXT("The child has a preview"), ChildPreview))
	{
		return false;
	}

	// ---- the panel slot
	UDreamPanelSlot* PreviewSlot = ChildPreview->GetPanelSlot();
	if (!TestNotNull(TEXT("Registration gave the preview child a panel slot"), PreviewSlot))
	{
		return false;
	}
	// Removed on purpose, to put the template in the state a .dui-built tree is routinely in.
	//
	// A designer-created widget goes through AddChild, which mints the slot, so the template has one
	// already -- that was the assumption this test was written on and it was wrong for the case that
	// matters. The builder only mints a slot for a node that HAS `@slot` properties
	// (DreamUITextBuilder::BuildSlotProperties), and registration, which would mint it otherwise,
	// never happens to an authoring tree. So a widget whose .dui says nothing about its slot reaches
	// the designer with no slot on the template at all, and the first padding anyone sets there has
	// nothing to write to. Refusing that one edit and accepting every later one is its own bug.
	ChildTemplate->RemovePanelSlot();
	TestNull(TEXT("the template is in the state a .dui builds"), ChildTemplate->GetPanelSlot());

	PreviewSlot->Padding = FMargin(12.0f);
	FProperty* PaddingProperty = UDreamPanelSlot::StaticClass()->FindPropertyByName(
		GET_MEMBER_NAME_CHECKED(UDreamPanelSlot, Padding));
	FEditPropertyChain PaddingChain;
	PaddingChain.AddHead(PaddingProperty);
	Scoped.Designer->MigrateDetailsChangeToTemplate({ PreviewSlot }, PaddingChain, /*bIsModify*/false);

	UDreamPanelSlot* TemplateSlot = ChildTemplate->GetPanelSlot();
	if (TestNotNull(TEXT("The edit minted the template's slot"), TemplateSlot))
	{
		TestEqual(TEXT("and the asset took the padding"), TemplateSlot->Padding.Left, 12.0f);
	}
	Scoped.Rebuild();
	UDreamWidget* Rebuilt = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(
		Scoped.FindTemplate(TEXT("Child")));
	if (TestNotNull(TEXT("Still previewed after a rebuild"), Rebuilt)
		&& TestNotNull(TEXT("with a slot"), Rebuilt->GetPanelSlot()))
	{
		// The assertion the preview-only version of this test would have passed without the fix.
		TestEqual(TEXT("And the rebuilt preview shows it"), Rebuilt->GetPanelSlot()->Padding.Left, 12.0f);
	}

	// ---- the layout container, which is authored and so is never minted here
	UDreamWidget* RootTemplate = Scoped.TemplateRoot();
	UDreamWidget* RootPreview = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(RootTemplate);
	UDreamLayoutContainerCanvasPanel* PreviewCanvas =
		Cast<UDreamLayoutContainerCanvasPanel>(RootPreview != nullptr ? RootPreview->GetLayoutContainer() : nullptr);
	if (TestNotNull(TEXT("The preview root has the canvas layout"), PreviewCanvas))
	{
		FProperty* SortProperty = UDreamLayoutContainerCanvasPanel::StaticClass()->FindPropertyByName(
			GET_MEMBER_NAME_CHECKED(UDreamLayoutContainerCanvasPanel, bSortChildrenByZOrder));
		if (TestNotNull(TEXT("which has a rule to edit"), SortProperty))
		{
			PreviewCanvas->bSortChildrenByZOrder = false;
			FEditPropertyChain LayoutChain;
			LayoutChain.AddHead(SortProperty);
			Scoped.Designer->MigrateDetailsChangeToTemplate({ PreviewCanvas }, LayoutChain, /*bIsModify*/false);

			UDreamLayoutContainerCanvasPanel* TemplateCanvas =
				Cast<UDreamLayoutContainerCanvasPanel>(RootTemplate->GetLayoutContainer());
			if (TestNotNull(TEXT("The template root still has its layout"), TemplateCanvas))
			{
				TestFalse(TEXT("and the asset took the layout rule"), TemplateCanvas->bSortChildrenByZOrder);
			}
		}
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRotationEditKeepsTheMirrorInStepTest,
	"DreamGUI.Designer.ARotationEditKeepsTheEulerMirrorInStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The transform section edits the QUAT; the .dui spells the EULER. Between them sits the mirror:
 * the panel's edit reaches the template as a raw quaternion copy, and the write-back then prints
 * the template's RelativeRotationEuler. Nothing syncs the two on that path except the
 * PostEditChangeProperty branch this test exists to pin -- MigratePropertyValue notifies through
 * it, and before the branch learned to reseed the euler, the file received whatever rotation the
 * template happened to hold last.
 *
 * Driven through the same call the panel's notify hook makes, with the chain the chainless
 * overload builds: one link, the widget's own RelativeRotation.
 */
bool FDreamDesignerRotationEditKeepsTheMirrorInStepTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerRotationEdit"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer) || Scoped.PreviewRoot() == nullptr)
	{
		return false;
	}
	UDreamWidget* ChildTemplate = DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), Scoped.TemplateRoot(), -1, TEXT("Spinner"));
	Scoped.Rebuild();
	ChildTemplate = Scoped.FindTemplate(TEXT("Spinner"));
	UDreamWidget* ChildPreview = Scoped.Designer->GetPreviewHost()->FindPreviewForTemplate(ChildTemplate);
	if (!TestNotNull(TEXT("The spinner has a preview"), ChildPreview))
	{
		return false;
	}

	// What the transform section does: the quat, through the setter, on the preview.
	ChildPreview->SetRelativeRotation(FRotator(0, 0, 30).Quaternion());
	TestEqual(TEXT("the preview's own mirror followed, via the setter"),
		ChildPreview->GetRelativeRotationEuler(), FRotator(0, 0, 30));

	FProperty* QuatProperty = UDreamWidget::StaticClass()->FindPropertyByName(
		UDreamWidget::GetPropertyName_RelativeRotation());
	FEditPropertyChain Chain;
	Chain.AddHead(QuatProperty);
	Scoped.Designer->MigrateDetailsChangeToTemplate({ ChildPreview }, Chain, /*bIsModify*/false);

	TestTrue(TEXT("the template took the quaternion"),
		ChildTemplate->GetRelativeRotation().Equals(FRotator(0, 0, 30).Quaternion()));
	// The half the write-back reads. A raw property copy leaves it at zero; only the
	// PostEditChangeProperty reseed brings it along.
	TestEqual(TEXT("and its euler mirror followed, via PostEditChangeProperty"),
		ChildTemplate->GetRelativeRotationEuler(), FRotator(0, 0, 30));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRootCanBeDeletedAndReplacedTest,
	"DreamGUI.Designer.TheRootCanBeDeletedAndTheEmptyTreeRefilled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerRootCanBeDeletedAndReplacedTest::RunTest(const FString&)
{
	using namespace DreamDesignerEditingTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerRootDelete"));
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer))
	{
		return false;
	}
	UDreamWidgetTree* Tree = Scoped.Blueprint->GetOrCreateWidgetTree();
	UDreamWidget* OldRoot = Tree->RootWidget.Get();
	if (!TestNotNull(TEXT("there is a root to delete"), OldRoot))
	{
		return false;
	}

	// Refused for years on the grounds that nothing downstream could answer for an empty hierarchy.
	// UMG has always allowed it (UWidgetTree::RemoveWidget nulls the root out), and there was no way
	// to replace a root you had outgrown short of deleting the asset.
	TestTrue(TEXT("the root can be deleted"), DreamWidgetTreeEditing::DeleteWidget(Scoped.Blueprint, OldRoot));
	TestNull(TEXT("and the tree is empty afterwards"), Tree->RootWidget.Get());

	// The other half, and the reason the first is not a one-way door: an empty tree takes the next
	// widget as its root.
	UDreamWidget* NewRoot = DreamWidgetTreeEditing::CreateWidget(
		Scoped.Blueprint, UDreamWidget::StaticClass(), nullptr, -1, TEXT("Replacement"));
	if (!TestNotNull(TEXT("a widget can be created into the empty tree"), NewRoot))
	{
		return false;
	}
	TestTrue(TEXT("and it became the root"), Tree->RootWidget.Get() == NewRoot);
	TestNull(TEXT("with nothing above it"), NewRoot->GetParent());

	// Stretched, like the one the factory makes: a root left at the widget default is a fixed rect
	// nothing sizes, and the designer would show the whole hierarchy at whatever that rect happens
	// to be.
	const FDreamUIAnchorData& Anchors = NewRoot->GetAnchorData();
	TestTrue(TEXT("the replacement root fills its parent"),
		Anchors.AnchorMin.IsNearlyZero() && Anchors.AnchorMax.Equals(FVector2D(1.0, 1.0))
			&& Anchors.SizeDelta.IsNearlyZero());

	// And the designer survives the round trip: a preview built from an emptied and refilled tree is
	// the assertion that would fail if any of the null-root guards downstream had been optimistic.
	Scoped.Designer->GetPreviewHost()->RebuildPreview();
	TestNotNull(TEXT("the preview rebuilt around the new root"), Scoped.PreviewRoot());
	return true;
}


#endif
