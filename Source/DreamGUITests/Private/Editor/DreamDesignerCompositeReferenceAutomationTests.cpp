// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamDesignerCompositeReferenceTestTypes.h"
#include "DreamOnDiskFixture.h"
#include "DreamWidgetBlueprint.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Designer/SDreamWidgetDesignerDetails.h"
#include "Editor.h"
#include "Event/DreamUIEventDelegate.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailTreeNode.h"
#include "Interaction/UIButton.h"
#include "IPropertyRowGenerator.h"
#include "IPropertyTypeCustomization.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/Material.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace DreamDesignerCompositeReferenceTestLocal
{
	FDreamUIEventDelegate* ClickEvent(UUIButton* InButton)
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(UUIButton::StaticClass(), TEXT("OnClick"));
		return InButton != nullptr && Property != nullptr
			? Property->ContainerPtrToValuePtr<FDreamUIEventDelegate>(InButton) : nullptr;
	}

	FScriptArrayHelper EventList(FDreamUIEventDelegate* InEvent)
	{
		const FArrayProperty* Property = FindFProperty<FArrayProperty>(FDreamUIEventDelegate::StaticStruct(), TEXT("EventList"));
		check(Property != nullptr && InEvent != nullptr);
		return FScriptArrayHelper(Property, Property->ContainerPtrToValuePtr<void>(InEvent));
	}

	FDreamUIEventDelegateData* Entry(FDreamUIEventDelegate* InEvent, int32 InIndex)
	{
		if (InEvent == nullptr) return nullptr;
		FScriptArrayHelper List = EventList(InEvent);
		return List.IsValidIndex(InIndex) ? reinterpret_cast<FDreamUIEventDelegateData*>(List.GetRawPtr(InIndex)) : nullptr;
	}

	UObject* Reference(FDreamUIEventDelegateData* InEntry, const TCHAR* InName)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(FDreamUIEventDelegateData::StaticStruct(), InName);
		check(Property != nullptr && InEntry != nullptr);
		return Property->GetObjectPropertyValue_InContainer(InEntry);
	}

	UDreamWidget* FindWidget(UDreamWidgetBlueprint* InBlueprint, const TCHAR* InDisplayName)
	{
		UDreamWidget* Found = nullptr;
		if (InBlueprint != nullptr && InBlueprint->WidgetTree != nullptr)
		{
			InBlueprint->WidgetTree->ForEachWidget([&](UDreamWidget* Widget)
			{
				if (Widget->GetDisplayName() == InDisplayName) Found = Widget;
			});
		}
		return Found;
	}

	struct FScopedDesigner
	{
		DreamOnDiskFixture::FScopedOnDiskPackage Disk;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamWidgetBlueprintEditor* Designer = nullptr;
		UObject* Asset = nullptr;

		explicit FScopedDesigner(const TCHAR* InName) : Disk(InName)
		{
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamUserWidget::StaticClass(), Disk.Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr) return;
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			Tree->RootWidget->SetDisplayName(TEXT("Root"));
			Tree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerCanvasPanel::StaticClass());
			UDreamWidget* Sender = DreamWidgetTreeEditing::CreateWidget(Blueprint, UDreamWidget::StaticClass(),
				Tree->RootWidget, -1, TEXT("Sender"));
			UDreamWidget* Receiver = DreamWidgetTreeEditing::CreateWidget(Blueprint, UDreamWidget::StaticClass(),
				Tree->RootWidget, -1, TEXT("Receiver"));
			if (Sender == nullptr || Receiver == nullptr) return;
			UUIButton* Button = Sender->AddComponent<UUIButton>();
			UDreamDesignerCompositeReferenceTestBehaviour* Behaviour =
				Receiver->AddComponent<UDreamDesignerCompositeReferenceTestBehaviour>();
			if (Button == nullptr || Behaviour == nullptr) return;
			FDreamUIEventDelegate* Event = ClickEvent(Button);
			if (Event == nullptr) return;
			// Existing legacy entries may still be removed/reordered in the native button's panel.
			// No disabled picker or unsupported authoring action is required by this test.
			Event->AddFunctionBinding(Receiver, Behaviour, TEXT("Touch"), EDreamUIEventDelegateParameterType::Empty, false);
			Event->AddFunctionBinding(Receiver, Behaviour, TEXT("TakeWidget"), EDreamUIEventDelegateParameterType::DreamWidget, false);
			FindFProperty<FObjectPropertyBase>(FDreamUIEventDelegateData::StaticStruct(), TEXT("ReferenceObject"))
				->SetObjectPropertyValue_InContainer(Entry(Event, 1), Receiver);
			Asset = UMaterial::GetDefaultMaterial(MD_Surface);
			Behaviour->Value.Widget = Receiver;
			Behaviour->Value.Asset = Asset;
			Behaviour->Values.Add(Behaviour->Value);
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
			UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			Editors->OpenEditorForAsset(Blueprint);
			Designer = static_cast<FDreamWidgetBlueprintEditor*>(Editors->FindEditorForAsset(Blueprint, false));
			if (Designer != nullptr) Designer->GetPreviewHost()->RebuildPreviewIfInvalidated();
		}

		~FScopedDesigner() { Close(); }

		void Close()
		{
			if (Designer != nullptr)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Blueprint);
				FSlateApplication::Get().Tick();
				Designer = nullptr;
			}
		}

		UDreamWidget* TemplateReceiver() const { return FindWidget(Blueprint, TEXT("Receiver")); }
		UUIButton* TemplateButton() const
		{
			UDreamWidget* Sender = FindWidget(Blueprint, TEXT("Sender"));
			return Sender != nullptr ? Sender->GetComponent<UUIButton>() : nullptr;
		}
		UDreamWidget* PreviewReceiver() const
		{
			return Designer != nullptr ? Designer->GetPreviewHost()->FindPreviewForTemplate(TemplateReceiver()) : nullptr;
		}
		UUIButton* PreviewButton() const
		{
			UDreamWidget* Sender = Designer != nullptr
				? Designer->GetPreviewHost()->FindPreviewForTemplate(FindWidget(Blueprint, TEXT("Sender"))) : nullptr;
			return Sender != nullptr ? Sender->GetComponent<UUIButton>() : nullptr;
		}
	};

	/**
	 * The production event panel has an empty header and draws only custom child rows, so its parent
	 * handle is not in GetRootTreeNodes. Capture it without changing the property's nodes or notify hook.
	 */
	class FEventHandleCapture : public IPropertyTypeCustomization
	{
	public:
		explicit FEventHandleCapture(TSharedPtr<IPropertyHandle>& InHandle) : Handle(InHandle) {}
		virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InHandle, FDetailWidgetRow&,
			IPropertyTypeCustomizationUtils&) override { Handle = InHandle; }
		virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InHandle, IDetailChildrenBuilder& InBuilder,
			IPropertyTypeCustomizationUtils&) override
		{
			InBuilder.AddProperty(InHandle->GetChildHandle(TEXT("EventList")).ToSharedRef());
		}
	private:
		TSharedPtr<IPropertyHandle>& Handle;
	};

	/** Real PropertyEditor operations, notifying the same details widget as the designer's visible panel. */
	struct FScopedRows
	{
		TSharedPtr<IPropertyHandle> EventHandle;
		TSharedPtr<IPropertyRowGenerator> Generator;

		FScopedRows(FScopedDesigner& InScoped, UObject* InObject)
		{
			FPropertyRowGeneratorArgs Args;
			Args.NotifyHook = InScoped.Designer->GetDesignerDetailsWidget().Get();
			Generator = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"))
				.CreatePropertyRowGenerator(Args);
			Generator->RegisterInstancedCustomPropertyTypeLayout(FDreamUIEventDelegate::StaticStruct()->GetFName(),
				FOnGetPropertyTypeCustomizationInstance::CreateLambda([this]() -> TSharedRef<IPropertyTypeCustomization>
				{
					return MakeShared<FEventHandleCapture>(EventHandle);
				}));
			Generator->SetObjects({ InObject });
		}

		static TSharedPtr<IPropertyHandle> FindIn(const TSharedRef<IDetailTreeNode>& InNode, FName InProperty)
		{
			if (TSharedPtr<IPropertyHandle> Handle = InNode->CreatePropertyHandle())
			{
				if (Handle->GetProperty() != nullptr && Handle->GetProperty()->GetFName() == InProperty) return Handle;
			}
			TArray<TSharedRef<IDetailTreeNode>> Children;
			InNode->GetChildren(Children, true);
			for (const TSharedRef<IDetailTreeNode>& Child : Children)
			{
				if (TSharedPtr<IPropertyHandle> Handle = FindIn(Child, InProperty)) return Handle;
			}
			return nullptr;
		}

		TSharedPtr<IPropertyHandle> Find(FName InProperty) const
		{
			if (InProperty == TEXT("OnClick") && EventHandle.IsValid()) return EventHandle;
			for (const TSharedRef<IDetailTreeNode>& Root : Generator->GetRootTreeNodes())
			{
				if (TSharedPtr<IPropertyHandle> Handle = FindIn(Root, InProperty)) return Handle;
			}
			return nullptr;
		}
	};

	bool CheckInitialPreview(FAutomationTestBase& InTest, FScopedDesigner& InScoped)
	{
		if (!InTest.TestNotNull(TEXT("the real designer opened"), InScoped.Designer)
			|| !InTest.TestNotNull(TEXT("the preview's button exists"), InScoped.PreviewButton())
			|| !InTest.TestNotNull(TEXT("the preview's receiver exists"), InScoped.PreviewReceiver())) return false;
		FDreamUIEventDelegateData* Binding = Entry(ClickEvent(InScoped.PreviewButton()), 1);
		if (!InTest.TestNotNull(TEXT("the authored widget binding was instanced"), Binding)) return false;
		// Establish that generated-class deep instancing already worked before exercising write-back.
		const bool bHelperMapped = InTest.TestTrue(TEXT("instancing mapped the helper into the preview"),
			Reference(Binding, TEXT("HelperWidget")) == InScoped.PreviewReceiver());
		const bool bArgumentMapped = InTest.TestTrue(TEXT("instancing mapped the argument into the preview"),
			Reference(Binding, TEXT("ReferenceObject")) == InScoped.PreviewReceiver());
		return bHelperMapped && bArgumentMapped;
	}

	void CheckTemplateBinding(FAutomationTestBase& InTest, FScopedDesigner& InScoped, int32 InIndex)
	{
		FDreamUIEventDelegateData* Binding = Entry(ClickEvent(InScoped.TemplateButton()), InIndex);
		if (!InTest.TestNotNull(TEXT("the surviving template binding exists"), Binding)) return;
		InTest.TestTrue(TEXT("the asset's helper names its own receiver"), Reference(Binding, TEXT("HelperWidget")) == InScoped.TemplateReceiver());
		InTest.TestTrue(TEXT("the asset's argument names its own receiver"), Reference(Binding, TEXT("ReferenceObject")) == InScoped.TemplateReceiver());
	}

	void CheckRebuiltBinding(FAutomationTestBase& InTest, FScopedDesigner& InScoped, int32 InIndex)
	{
		FDreamUIEventDelegate* Event = ClickEvent(InScoped.PreviewButton());
		FDreamUIEventDelegateData* Binding = Entry(Event, InIndex);
		if (!InTest.TestNotNull(TEXT("the rebuilt preview has the surviving binding"), Binding)) return;
		InTest.TestTrue(TEXT("the rebuilt helper belongs to the new preview"), Reference(Binding, TEXT("HelperWidget")) == InScoped.PreviewReceiver());
		InTest.TestTrue(TEXT("the rebuilt argument belongs to the new preview"), Reference(Binding, TEXT("ReferenceObject")) == InScoped.PreviewReceiver());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDesignerLegacyRemovalPreservesNestedReferencesTest,
	"DreamGUI.Designer.CompositeReferences.LegacyRemovalPreservesReferencesThroughUndoRebuildAndSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerLegacyRemovalPreservesNestedReferencesTest::RunTest(const FString&)
{
	using namespace DreamDesignerCompositeReferenceTestLocal;
	FScopedDesigner Scoped(TEXT("DesignerLegacyRemovalReferences"));
	if (!CheckInitialPreview(*this, Scoped)) return false;
	{
		FScopedRows Rows(Scoped, Scoped.PreviewButton());
		TSharedPtr<IPropertyHandle> Click = Rows.Find(TEXT("OnClick"));
		TSharedPtr<IPropertyHandle> List = Click.IsValid() ? Click->GetChildHandle(TEXT("EventList")) : nullptr;
		TSharedPtr<IPropertyHandleArray> Array = List.IsValid() ? List->AsArray() : nullptr;
		if (!TestTrue(TEXT("the native button exposes its legacy list handle"), Array.IsValid())) return false;
		TestTrue(TEXT("the supported legacy delete goes through PropertyEditor"), Array->DeleteItem(0) == FPropertyAccess::Success);
	}
	TestEqual(TEXT("the removal reached the asset"), EventList(ClickEvent(Scoped.TemplateButton())).Num(), 1);
	CheckTemplateBinding(*this, Scoped, 0);
	TestTrue(TEXT("the list removal undoes"), GEditor->UndoTransaction());
	TestEqual(TEXT("undo restores both authored entries"), EventList(ClickEvent(Scoped.TemplateButton())).Num(), 2);
	CheckTemplateBinding(*this, Scoped, 1);
	TestTrue(TEXT("the list removal redoes"), GEditor->RedoTransaction());
	TestEqual(TEXT("redo removes only the selected entry"), EventList(ClickEvent(Scoped.TemplateButton())).Num(), 1);
	CheckTemplateBinding(*this, Scoped, 0);
	TWeakObjectPtr<UDreamWidget> OutgoingReceiver = Scoped.PreviewReceiver();
	TestTrue(TEXT("there is a live outgoing receiver to replace"), OutgoingReceiver.IsValid());
	Scoped.Designer->GetPreviewHost()->RebuildPreview();
	TestFalse(TEXT("the outgoing preview receiver was destroyed"), OutgoingReceiver.IsValid());
	CheckTemplateBinding(*this, Scoped, 0);
	CheckRebuiltBinding(*this, Scoped, 0);

	FString Error;
	if (!TestTrue(TEXT("the edited asset saves without a reference into the preview"), Scoped.Disk.Save(Scoped.Blueprint, Error)))
	{
		AddInfo(Error);
		return false;
	}
	Scoped.Close();
	UDreamWidgetBlueprint* Loaded = Cast<UDreamWidgetBlueprint>(Scoped.Disk.Reload(Error));
	if (!TestNotNull(TEXT("the edited asset was serialized and reloaded"), Loaded)) { AddInfo(Error); return false; }
	TestNotEqual(TEXT("reload produced another blueprint"), Loaded, Scoped.Blueprint);
	UDreamWidget* Sender = FindWidget(Loaded, TEXT("Sender"));
	UDreamWidget* Receiver = FindWidget(Loaded, TEXT("Receiver"));
	if (!TestNotNull(TEXT("the authored sender was loaded"), Sender)
		|| !TestNotNull(TEXT("the authored receiver was loaded"), Receiver)) return false;
	FDreamUIEventDelegate* LoadedEvent = ClickEvent(Sender != nullptr ? Sender->GetComponent<UUIButton>() : nullptr);
	FDreamUIEventDelegateData* LoadedBinding = Entry(LoadedEvent, 0);
	if (!TestNotNull(TEXT("the surviving authored binding was loaded"), LoadedBinding)) return false;
	TestEqual(TEXT("only the surviving entry was serialized"), EventList(LoadedEvent).Num(), 1);
	TestTrue(TEXT("the serialized helper names the loaded receiver"), Reference(LoadedBinding, TEXT("HelperWidget")) == Receiver);
	TestTrue(TEXT("the serialized argument names the loaded receiver"), Reference(LoadedBinding, TEXT("ReferenceObject")) == Receiver);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDesignerLegacyReorderPreservesNestedReferencesTest,
	"DreamGUI.Designer.CompositeReferences.LegacyReorderPreservesTheNewPreviewsReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerLegacyReorderPreservesNestedReferencesTest::RunTest(const FString&)
{
	using namespace DreamDesignerCompositeReferenceTestLocal;
	FScopedDesigner Scoped(TEXT("DesignerLegacyReorderReferences"));
	if (!CheckInitialPreview(*this, Scoped)) return false;
	{
		FScopedRows Rows(Scoped, Scoped.PreviewButton());
		TSharedPtr<IPropertyHandle> Click = Rows.Find(TEXT("OnClick"));
		TSharedPtr<IPropertyHandle> List = Click.IsValid() ? Click->GetChildHandle(TEXT("EventList")) : nullptr;
		TSharedPtr<IPropertyHandleArray> Array = List.IsValid() ? List->AsArray() : nullptr;
		if (!TestTrue(TEXT("the native legacy event list is editable"), Array.IsValid())) return false;
		TestTrue(TEXT("the supported reorder goes through PropertyEditor"), Array->SwapItems(0, 1) == FPropertyAccess::Success);
	}
	FDreamUIEventDelegateData* First = Entry(ClickEvent(Scoped.TemplateButton()), 0);
	if (!TestNotNull(TEXT("the reordered binding reached the template"), First)) return false;
	TestEqual(TEXT("the widget-argument binding moved first"),
		FindFProperty<FNameProperty>(FDreamUIEventDelegateData::StaticStruct(), TEXT("FunctionName"))->GetPropertyValue_InContainer(First),
		FName(TEXT("TakeWidget")));
	CheckTemplateBinding(*this, Scoped, 0);
	Scoped.Designer->GetPreviewHost()->RebuildPreview();
	CheckRebuiltBinding(*this, Scoped, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamDesignerStructAndArrayEditsPreserveReferencesTest,
	"DreamGUI.Designer.CompositeReferences.StructAndArrayEditsPreserveWidgetAndAssetReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerStructAndArrayEditsPreserveReferencesTest::RunTest(const FString&)
{
	using namespace DreamDesignerCompositeReferenceTestLocal;
	FScopedDesigner Scoped(TEXT("DesignerStructAndArrayReferences"));
	if (!CheckInitialPreview(*this, Scoped)) return false;
	UDreamDesignerCompositeReferenceTestBehaviour* Preview =
		Scoped.PreviewReceiver()->GetComponent<UDreamDesignerCompositeReferenceTestBehaviour>();
	UDreamDesignerCompositeReferenceTestBehaviour* Template =
		Scoped.TemplateReceiver()->GetComponent<UDreamDesignerCompositeReferenceTestBehaviour>();
	if (!TestNotNull(TEXT("the editable behaviour was instanced"), Preview)
		|| !TestNotNull(TEXT("its authored counterpart exists"), Template)
		|| !TestNotNull(TEXT("an ordinary external material asset exists"), Scoped.Asset)) return false;
	TestTrue(TEXT("struct instancing remapped its widget"), Preview->Value.Widget == Scoped.PreviewReceiver());
	if (!TestEqual(TEXT("the editable array was instanced"), Preview->Values.Num(), 1)) return false;
	TestTrue(TEXT("array instancing remapped its widget"), Preview->Values[0].Widget == Scoped.PreviewReceiver());
	{
		FScopedRows Rows(Scoped, Preview);
		TSharedPtr<IPropertyHandle> Value = Rows.Find(TEXT("Value"));
		TSharedPtr<IPropertyHandle> Count = Value.IsValid() ? Value->GetChildHandle(TEXT("Count")) : nullptr;
		if (!TestTrue(TEXT("a normal scalar inside the editable struct has a handle"), Count.IsValid())) return false;
		// Edit a number, not a reference picker: copying the surrounding struct must not corrupt its unedited references.
		TestTrue(TEXT("the scalar edit goes through PropertyEditor"), Count->SetValue(17) == FPropertyAccess::Success);
		TSharedPtr<IPropertyHandle> Values = Rows.Find(TEXT("Values"));
		TSharedPtr<IPropertyHandleArray> Array = Values.IsValid() ? Values->AsArray() : nullptr;
		if (!TestTrue(TEXT("the editable struct array has a handle"), Array.IsValid())) return false;
		TestTrue(TEXT("the array duplicate goes through PropertyEditor"), Array->DuplicateItem(0) == FPropertyAccess::Success);
	}
	TestEqual(TEXT("the struct edit reached the template"), Template->Value.Count, 17);
	TestTrue(TEXT("the unedited struct widget still belongs to the asset"), Template->Value.Widget == Scoped.TemplateReceiver());
	TestTrue(TEXT("the ordinary struct asset reference was preserved"), Template->Value.Asset == Scoped.Asset);
	TestEqual(TEXT("the duplicate reached the template array"), Template->Values.Num(), 2);
	for (const FDreamDesignerCompositeReferenceTestValue& Value : Template->Values)
	{
		TestTrue(TEXT("each array widget reference belongs to the asset"), Value.Widget == Scoped.TemplateReceiver());
		TestTrue(TEXT("each ordinary array asset reference was preserved"), Value.Asset == Scoped.Asset);
	}
	TWeakObjectPtr<UDreamWidget> OutgoingReceiver = Scoped.PreviewReceiver();
	TestTrue(TEXT("the struct and array name a live outgoing preview before rebuilding"), OutgoingReceiver.IsValid());
	Scoped.Designer->GetPreviewHost()->RebuildPreview();
	TestFalse(TEXT("the old preview has been destroyed"), OutgoingReceiver.IsValid());
	UDreamWidget* NewReceiver = Scoped.PreviewReceiver();
	if (!TestNotNull(TEXT("the receiver was rebuilt"), NewReceiver)) return false;
	Preview = NewReceiver->GetComponent<UDreamDesignerCompositeReferenceTestBehaviour>();
	if (!TestNotNull(TEXT("the behaviour was rebuilt"), Preview)) return false;
	TestEqual(TEXT("the scalar survived rebuilding"), Preview->Value.Count, 17);
	TestTrue(TEXT("the rebuilt struct widget points into the new preview"), Preview->Value.Widget == Scoped.PreviewReceiver());
	for (const FDreamDesignerCompositeReferenceTestValue& Value : Preview->Values)
	{
		TestTrue(TEXT("the rebuilt array widget points into the new preview"), Value.Widget == Scoped.PreviewReceiver());
		TestTrue(TEXT("the rebuilt array still names the ordinary material asset"), Value.Asset == Scoped.Asset);
	}
	return true;
}

#endif
