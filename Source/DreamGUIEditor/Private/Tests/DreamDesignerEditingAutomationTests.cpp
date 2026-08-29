// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "DreamUIEditorTools.h"
#include "PrefabEditor/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamText.h"
#include "PrefabEditor/DreamWidgetPropertyBindingExtension.h"
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
		Scoped.Designer->MigrateDetailsChangeToTemplate(Chain, /*bIsModify*/false);

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

#endif
