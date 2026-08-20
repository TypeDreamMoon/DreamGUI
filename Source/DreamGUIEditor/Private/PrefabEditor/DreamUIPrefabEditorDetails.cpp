// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIPrefabEditorDetails.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Modules/ModuleManager.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/NotifyHook.h"
#include "DreamUIPrefabEditor.h"
#include "DetailLayoutBuilder.h"
#include "DreamWidgetDetailPropertyExtensionHandler.h"
#include "DreamUIPrefabOverrideDataViewer.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "DreamUIEditorTools.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamWidget.h"
#include "ScopedTransaction.h"
#include "SPositiveActionButton.h"
#include "Framework/Commands/GenericCommands.h"
#include "Styling/SlateIconFinder.h"
#include "Utils/DreamUIUtils.h"
#include "SourceCodeNavigation.h"
#include "PrefabAnimation/DreamUIDetailKeyframeHandler.h"
#include "AssetSelection.h"
#include "AssetRegistry/AssetData.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Engine/Blueprint.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "EditorClassUtils.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DreamGUIPrefabEditorDetailTab"

namespace
{
	/**
	 * Transient state belongs to an instance, never to a copy of one. UDreamUIBehaviour caches the
	 * widget it was registered against, and GetWidget() trusts that cache ahead of its own outer, so
	 * a component whose properties were copied wholesale would keep reporting -- and keep listening
	 * to -- whichever widget the source lived on. A freshly constructed component has these null.
	 *
	 * The whole property is reset rather than its references nulled one by one: transient references
	 * live inside containers as well as in properties of their own (UUIDropdown's created-item array
	 * is one property), and an array of nulls is not safer than an array of foreign pointers -- the
	 * dropdown walks that array without a validity check. Nothing here has to be preserved; every
	 * one of these is derived again on demand.
	 */
	void DreamUIClearTransientObjectReferences(UObject* InObject)
	{
		TArray<const FStructProperty*> EncounteredStructProperties;
		for (TFieldIterator<FProperty> PropertyIt(InObject->GetClass()); PropertyIt; ++PropertyIt)
		{
			if (!PropertyIt->HasAnyPropertyFlags(CPF_Transient))continue;
			EncounteredStructProperties.Reset();
			if (!PropertyIt->ContainsObjectReference(EncounteredStructProperties, EPropertyObjectReferenceType::Strong | EPropertyObjectReferenceType::Weak))continue;
			for (int32 ArrayIndex = 0; ArrayIndex < PropertyIt->ArrayDim; ArrayIndex++)
			{
				PropertyIt->ClearValue_InContainer(InObject, ArrayIndex);
			}
		}
	}
}

class FDreamWidgetComponentClassFilter : public IClassViewerFilter
{
public:
	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return IsComponentClassAllowed(InClass);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return InUnloadedClassData->IsChildOf(UDreamUIBehaviour::StaticClass())
			&& !InUnloadedClassData->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Hidden);
	}

	static bool IsComponentClassAllowed(const UClass* InClass)
	{
		if (InClass == nullptr)return false;
		if (!InClass->IsChildOf(UDreamUIBehaviour::StaticClass()))return false;
		if (InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Hidden | CLASS_Transient))return false;
		if (InClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !InClass->HasAnyClassFlags(CLASS_Native))//blueprint class
		{
			return true;
		}
		if (!InClass->HasMetaData("BlueprintSpawnableComponent"))return false;
		return true;
	}
};

/**
 * Whether a component of this class may be put on a widget at all. The clipboard is the one add path
 * that starts from an existing component rather than from the class picker, so without asking here a
 * class the picker refuses arrives on a widget by way of somewhere it was once allowed.
 *
 * Declared here rather than in the panel's header because the panel is a Slate widget no headless
 * test can construct; DreamPrefabPanelsAutomationTests declares these prototypes itself.
 */
bool DreamUIWidgetComponentClipboard_CanPasteClass(const UClass* InComponentClass)
{
	// The class picker's own filter, deliberately. It looks like a menu-presentation rule, but the
	// eight native behaviours it hides are the framework's own -- UDreamPanelSlot, UDreamVisual,
	// UDreamLayout, and the helpers a control creates for itself. Those are placed by whatever owns
	// them, so a hand-pasted second one is something the owner will fight with. A component being
	// present on a widget is not evidence a user may put another one somewhere else.
	return FDreamWidgetComponentClassFilter::IsComponentClassAllowed(InComponentClass);
}

/**
 * Whether a component may be taken off a widget to be put back down elsewhere -- what Copy, Cut and
 * Duplicate each end in. Asked here as well as at the paste, because a refusal that arrives only at
 * the paste arrives too late: Cut has already deleted the component by then, and the clipboard is one
 * static shared by every panel, so an item no paste will ever accept leaves Paste greyed out for
 * everything until something else is copied over the top.
 */
bool DreamUIWidgetComponentClipboard_CanTakeComponent(const UDreamUIBehaviour* InComponent)
{
	if (!IsValid(InComponent))return false;
	return DreamUIWidgetComponentClipboard_CanPasteClass(InComponent->GetClass());
}

/**
 * A clipboard-safe stand-alone copy of InSource, so that cutting -- which deletes the component the
 * copy came from -- still leaves something to paste.
 */
UDreamUIBehaviour* DreamUIWidgetComponentClipboard_Snapshot(UDreamUIBehaviour* InSource)
{
	if (!IsValid(InSource))return nullptr;
	auto Snapshot = NewObject<UDreamUIBehaviour>(GetTransientPackage(), InSource->GetClass(), NAME_None, RF_Transient);
	UEngine::FCopyPropertiesForUnrelatedObjectsParams Options;
	UEditorEngine::CopyPropertiesForUnrelatedObjects(InSource, Snapshot, Options);
	DreamUIClearTransientObjectReferences(Snapshot);
	return Snapshot;
}

/**
 * Recreate InSource on InTargetWidget. The component is added empty first so that the registration
 * UDreamWidget::AddComponent performs binds against the target widget's events, and only then are the
 * authored values copied over the top.
 */
UDreamUIBehaviour* DreamUIWidgetComponentClipboard_PasteOnto(UDreamWidget* InTargetWidget, UDreamUIBehaviour* InSource)
{
	if (!IsValid(InTargetWidget) || !IsValid(InSource))return nullptr;
	if (!DreamUIWidgetComponentClipboard_CanPasteClass(InSource->GetClass()))return nullptr;
	auto NewComponent = InTargetWidget->AddComponent(InSource->GetClass());
	if (!IsValid(NewComponent))return nullptr;
	UEngine::FCopyPropertiesForUnrelatedObjectsParams Options;
	UEditorEngine::CopyPropertiesForUnrelatedObjects(InSource, NewComponent, Options);
	DreamUIClearTransientObjectReferences(NewComponent);
	// Registration already happened, and UDreamUIBehaviour::OnUnregister unsubscribes through the cache
	// rather than through GetWidget(): left empty by the clear above, the component would still be
	// listening to this widget long after it was removed from it.
	NewComponent->GetWidget();
	return NewComponent;
}

/**
 * One clipboard for every panel in the editor, so a component can be pasted onto another prefab.
 *
 * Reached through an accessor rather than named directly so that emptying it is a single call the
 * module can make: a strong pointer left holding a component releases it during static teardown,
 * after the object system it releases into has gone, and only in the sessions that happened to end
 * with something on the clipboard. DreamUIWidgetComponentClipboard_Reset below is that call.
 */
TStrongObjectPtr<UDreamUIBehaviour>& DreamUIWidgetComponentClipboard()
{
	static TStrongObjectPtr<UDreamUIBehaviour> Clipboard;
	return Clipboard;
}

/** Drop the clipboard while the editor is still up. Call from module shutdown. */
void DreamUIWidgetComponentClipboard_Reset()
{
	DreamUIWidgetComponentClipboard().Reset();
}

using FDreamWidgetComponentItem = TWeakObjectPtr<UDreamUIBehaviour>;

class FDreamWidgetComponentDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDreamWidgetComponentDragDropOp, FDecoratedDragDropOp)

	static TSharedRef<FDreamWidgetComponentDragDropOp> New(const FDreamWidgetComponentItem& InDraggedItem)
	{
		TSharedRef<FDreamWidgetComponentDragDropOp> Operation = MakeShared<FDreamWidgetComponentDragDropOp>();
		Operation->DraggedItem = InDraggedItem;
		Operation->SetToolTip(LOCTEXT("ReorderComponent", "Reorder component"), nullptr);
		Operation->SetupDefaults();
		Operation->Construct();
		return Operation;
	}

	FDreamWidgetComponentItem DraggedItem;
};

class SDreamWidgetComponentEditor : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal(UDreamWidget*, FOnGetWidgetContext);
	DECLARE_DELEGATE_RetVal(bool, FOnCanEdit);
	DECLARE_DELEGATE_OneParam(FOnComponentsSelectionChanged, const TArray<FDreamWidgetComponentItem>&);

	SLATE_BEGIN_ARGS(SDreamWidgetComponentEditor)
	{
	}
		SLATE_EVENT(FOnGetWidgetContext, GetWidgetContext)
		SLATE_EVENT(FOnCanEdit, CanEdit)
		SLATE_EVENT(FOnComponentsSelectionChanged, OnSelectionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		CommandList = MakeShareable(new FUICommandList);
		CommandList->MapAction(
			FGenericCommands::Get().Delete,
			FExecuteAction::CreateSP(this, &SDreamWidgetComponentEditor::HandleRemoveSelectedComponents),
			FCanExecuteAction::CreateSP(this, &SDreamWidgetComponentEditor::CanRemoveSelectedComponents)
		);
		CommandList->MapAction(
			FGenericCommands::Get().Copy,
			FExecuteAction::CreateSP(this, &SDreamWidgetComponentEditor::HandleCopySelectedComponent),
			FCanExecuteAction::CreateSP(this, &SDreamWidgetComponentEditor::CanCopySelectedComponent)
		);
		CommandList->MapAction(
			FGenericCommands::Get().Cut,
			FExecuteAction::CreateSP(this, &SDreamWidgetComponentEditor::HandleCutSelectedComponent),
			FCanExecuteAction::CreateSP(this, &SDreamWidgetComponentEditor::CanCutOrDuplicateSelectedComponent)
		);
		CommandList->MapAction(
			FGenericCommands::Get().Paste,
			FExecuteAction::CreateSP(this, &SDreamWidgetComponentEditor::HandlePasteComponent),
			FCanExecuteAction::CreateSP(this, &SDreamWidgetComponentEditor::CanPasteComponent)
		);
		CommandList->MapAction(
			FGenericCommands::Get().Duplicate,
			FExecuteAction::CreateSP(this, &SDreamWidgetComponentEditor::HandleDuplicateSelectedComponent),
			FCanExecuteAction::CreateSP(this, &SDreamWidgetComponentEditor::CanCutOrDuplicateSelectedComponent)
		);

		GetWidgetContext = InArgs._GetWidgetContext;
		CanEdit = InArgs._CanEdit;
		OnSelectionChanged = InArgs._OnSelectionChanged;

		ToolbarWidget =
		SNew(SBox)
		.Padding(FMargin(4, 2, 4, 2))
		[
			SNew(SPositiveActionButton)
			.IsEnabled(this, &SDreamWidgetComponentEditor::CanAddOrRemoveComponent)
			.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
			.Text(LOCTEXT("AddWidgetComponent", "Add Component"))
			.ToolTipText(LOCTEXT("AddWidgetComponentTooltip", "Add a DreamUI component to the selected widget"))
			.OnGetMenuContent(this, &SDreamWidgetComponentEditor::GenerateAddComponentMenu)
		]
		;

		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.Padding(FMargin(2))
				[
					SAssignNew(ComponentListView, SListView<FDreamWidgetComponentItem>)
					.ListItemsSource(&ComponentItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SDreamWidgetComponentEditor::HandleGenerateRow)
					.OnSelectionChanged(this, &SDreamWidgetComponentEditor::HandleSelectionChanged)
					.OnContextMenuOpening(this, &SDreamWidgetComponentEditor::OnContextMenuOpening)
				]
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Visibility(this, &SDreamWidgetComponentEditor::GetEmptyStateVisibility)
				.Text(this, &SDreamWidgetComponentEditor::GetEmptyStateText)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];

		RefreshComponents();
	}

	virtual bool SupportsKeyboardFocus() const override
	{
		return true;
	}

	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override
	{
		if (ComponentListView.IsValid())
		{
			FSlateApplication::Get().SetKeyboardFocus(ComponentListView, EFocusCause::SetDirectly);
			return FReply::Handled();
		}

		return SCompoundWidget::OnFocusReceived(MyGeometry, InFocusEvent);
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (CommandList.IsValid() && CommandList->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}

		return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}

	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		SCompoundWidget::OnDragEnter(MyGeometry, DragDropEvent);
	}

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (CanHandleAssetDrop(DragDropEvent))
		{
			if (TSharedPtr<FAssetDragDropOp> AssetDragOp = DragDropEvent.GetOperationAs<FAssetDragDropOp>())
			{
				AssetDragOp->SetToolTip(LOCTEXT("DropToAddComponent", "Drop to add component"), AssetDragOp->CurrentIconBrush);
			}
			return FReply::Handled();
		}
		if (TSharedPtr<FAssetDragDropOp> AssetDragOp = DragDropEvent.GetOperationAs<FAssetDragDropOp>())
		{
			AssetDragOp->ResetToDefaultToolTip();
		}
		return SCompoundWidget::OnDragOver(MyGeometry, DragDropEvent);
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (HandleAddComponentsFromAssetDrop(DragDropEvent))
		{
			return FReply::Handled();
		}
		return SCompoundWidget::OnDrop(MyGeometry, DragDropEvent);
	}

	TSharedRef<SWidget> GetToolbarWidget() const
	{
		return ToolbarWidget.ToSharedRef();
	}
	void RefreshComponents()
	{
		ComponentItems.Reset();

		if (const UDreamWidget* Widget = GetCurrentWidget())
		{
			for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
			{
				if (IsValid(Component))
				{
					ComponentItems.Add(Component);
				}
			}
		}

		if (ComponentListView.IsValid())
		{
			ComponentListView->RequestListRefresh();
		}
	}

	void ClearSelection()
	{
		if (ComponentListView.IsValid())
		{
			ComponentListView->ClearSelection();
		}
	}

	void SelectComponent(UDreamUIBehaviour* InComponent)
	{
		if (!ComponentListView.IsValid() || !IsValid(InComponent))
		{
			return;
		}

		const FDreamWidgetComponentItem Item = InComponent;
		ComponentListView->SetSelection(Item, ESelectInfo::Direct);
		ComponentListView->RequestScrollIntoView(Item);
	}

private:
	UDreamWidget* GetCurrentWidget() const
	{
		return GetWidgetContext.IsBound() ? GetWidgetContext.Execute() : nullptr;
	}

	bool CanAddOrRemoveComponent() const
	{
		return IsValid(GetCurrentWidget()) && (!CanEdit.IsBound() || CanEdit.Execute());
	}

	bool CanRemoveSelectedComponents() const
	{
		if (!CanAddOrRemoveComponent() || !ComponentListView.IsValid())
		{
			return false;
		}

		TArray<FDreamWidgetComponentItem> SelectedItems;
		ComponentListView->GetSelectedItems(SelectedItems);
		return SelectedItems.Num() > 0;
	}

	UDreamUIBehaviour* GetSelectedComponent() const
	{
		if (!ComponentListView.IsValid())
		{
			return nullptr;
		}

		TArray<FDreamWidgetComponentItem> SelectedItems;
		ComponentListView->GetSelectedItems(SelectedItems);
		return (SelectedItems.Num() > 0 && SelectedItems[0].IsValid()) ? SelectedItems[0].Get() : nullptr;
	}

	/** The widget and whatever holds it both change when its component list does, so both are recorded. */
	void ModifyWidgetForComponentEdit(UDreamWidget* Widget)
	{
		if (UObject* WidgetOuter = Widget->GetOuter())
		{
			WidgetOuter->SetFlags(RF_Transactional);
			WidgetOuter->Modify();
		}
		Widget->SetFlags(RF_Transactional);
		Widget->Modify();
	}

	/** Record and announce a newly added component. Call inside the caller's transaction, not after it. */
	void FinishComponentAdd(UDreamWidget* Widget, UDreamUIBehaviour* NewComponent)
	{
		NewComponent->SetFlags(RF_Transactional);
		NewComponent->Modify();
		FDreamUIUtils::NotifyPropertyChanged(Widget, UDreamWidget::GetPropertyName_Components());

		RefreshComponents();
		SelectComponent(NewComponent);
	}

	bool CanCopySelectedComponent() const
	{
		return DreamUIWidgetComponentClipboard_CanTakeComponent(GetSelectedComponent());
	}

	bool CanModifySelectedComponent() const
	{
		return CanAddOrRemoveComponent() && IsValid(GetSelectedComponent());
	}

	/** Both recreate the selected component through the paste, so both answer for its class as well. */
	bool CanCutOrDuplicateSelectedComponent() const
	{
		return CanModifySelectedComponent() && DreamUIWidgetComponentClipboard_CanTakeComponent(GetSelectedComponent());
	}

	bool CanPasteComponent() const
	{
		if (!CanAddOrRemoveComponent() || !DreamUIWidgetComponentClipboard().IsValid())
		{
			return false;
		}
		return DreamUIWidgetComponentClipboard_CanPasteClass(DreamUIWidgetComponentClipboard()->GetClass());
	}

	void HandleCopySelectedComponent()
	{
		if (!CanCopySelectedComponent())
		{
			return;
		}
		DreamUIWidgetComponentClipboard().Reset(DreamUIWidgetComponentClipboard_Snapshot(GetSelectedComponent()));
	}

	void HandleCutSelectedComponent()
	{
		UDreamWidget* Widget = GetCurrentWidget();
		UDreamUIBehaviour* Component = GetSelectedComponent();
		if (!CanCutOrDuplicateSelectedComponent() || !IsValid(Widget) || Component->GetWidget() != Widget)
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("CutDreamWidgetComponent_Transaction", "Cut DreamUI Component"));
		ModifyWidgetForComponentEdit(Widget);

		DreamUIWidgetComponentClipboard().Reset(DreamUIWidgetComponentClipboard_Snapshot(Component));
		Component->SetFlags(RF_Transactional);
		Component->Modify();
		Widget->RemoveComponent(Component);
		FDreamUIUtils::NotifyPropertyChanged(Widget, UDreamWidget::GetPropertyName_Components());

		RefreshComponents();
		ClearSelection();
	}

	void HandlePasteComponent()
	{
		UDreamWidget* Widget = GetCurrentWidget();
		if (!CanPasteComponent() || !IsValid(Widget))
		{
			return;
		}

		//ModifyWidgetForComponentEdit has already recorded the widget, so a transaction left to commit
		//on the failure branch is an undo step that undoes nothing
		FScopedTransaction Transaction(LOCTEXT("PasteDreamWidgetComponent_Transaction", "Paste DreamUI Component"));
		ModifyWidgetForComponentEdit(Widget);

		UDreamUIBehaviour* NewComponent = DreamUIWidgetComponentClipboard_PasteOnto(Widget, DreamUIWidgetComponentClipboard().Get());
		if (!IsValid(NewComponent))
		{
			Transaction.Cancel();
			return;
		}
		FinishComponentAdd(Widget, NewComponent);
	}

	void HandleDuplicateSelectedComponent()
	{
		UDreamWidget* Widget = GetCurrentWidget();
		UDreamUIBehaviour* Component = GetSelectedComponent();
		if (!CanCutOrDuplicateSelectedComponent() || !IsValid(Widget) || Component->GetWidget() != Widget)
		{
			return;
		}

		FScopedTransaction Transaction(LOCTEXT("DuplicateDreamWidgetComponent_Transaction", "Duplicate DreamUI Component"));
		ModifyWidgetForComponentEdit(Widget);

		UDreamUIBehaviour* NewComponent = DreamUIWidgetComponentClipboard_PasteOnto(Widget, Component);
		if (!IsValid(NewComponent))
		{
			Transaction.Cancel();
			return;
		}
		FinishComponentAdd(Widget, NewComponent);
	}

	FReply HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FDreamWidgetComponentItem InItem)
	{
		if (!InItem.IsValid() || !CanAddOrRemoveComponent())
		{
			return FReply::Unhandled();
		}

		return FReply::Handled().BeginDragDrop(FDreamWidgetComponentDragDropOp::New(InItem));
	}

	TOptional<EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, FDreamWidgetComponentItem TargetItem) const
	{
		if (!CanAddOrRemoveComponent() || !TargetItem.IsValid())
		{
			return TOptional<EItemDropZone>();
		}

		if (TSharedPtr<FDreamWidgetComponentDragDropOp> DragOp = DragDropEvent.GetOperationAs<FDreamWidgetComponentDragDropOp>())
		{
			if (!DragOp->DraggedItem.IsValid() || DragOp->DraggedItem == TargetItem)
			{
				return TOptional<EItemDropZone>();
			}
			return DropZone;
		}

		return TOptional<EItemDropZone>();
	}

	FReply HandleAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, FDreamWidgetComponentItem TargetItem)
	{
		UDreamWidget* Widget = GetCurrentWidget();
		if (!CanAddOrRemoveComponent() || !IsValid(Widget) || !TargetItem.IsValid())
		{
			return FReply::Unhandled();
		}

		TSharedPtr<FDreamWidgetComponentDragDropOp> DragOp = DragDropEvent.GetOperationAs<FDreamWidgetComponentDragDropOp>();
		if (!DragOp.IsValid() || !DragOp->DraggedItem.IsValid() || DragOp->DraggedItem == TargetItem)
		{
			return FReply::Unhandled();
		}

		const int32 TargetIndex = ComponentItems.IndexOfByKey(TargetItem);
		if (TargetIndex == INDEX_NONE)
		{
			return FReply::Unhandled();
		}

		const int32 DesiredIndex = (DropZone == EItemDropZone::BelowItem) ? (TargetIndex + 1) : TargetIndex;
		if (MoveComponentToIndex(DragOp->DraggedItem.Get(), DesiredIndex))
		{
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	bool CanHandleAssetDrop(const FDragDropEvent& DragDropEvent) const
	{
		if (!CanAddOrRemoveComponent())
		{
			return false;
		}

		TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
		return Operation.IsValid() && Operation->IsOfType<FAssetDragDropOp>();
	}

	bool HandleAddComponentsFromAssetDrop(const FDragDropEvent& DragDropEvent)
	{
		if (!CanHandleAssetDrop(DragDropEvent))
		{
			return false;
		}

		UDreamWidget* Widget = GetCurrentWidget();
		if (!IsValid(Widget))
		{
			return false;
		}

		TArray<FAssetData> DroppedAssetData = AssetUtil::ExtractAssetDataFromDrag(DragDropEvent.GetOperation());
		if (DroppedAssetData.Num() == 0)
		{
			return false;
		}

		TArray<UClass*> ComponentClassesToAdd;
		for (const FAssetData& AssetData : DroppedAssetData)
		{
			if (UClass* ComponentClass = ResolveComponentClassFromAsset(AssetData))
			{
				ComponentClassesToAdd.Add(ComponentClass);
			}
		}

		if (ComponentClassesToAdd.Num() == 0)
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("AddDreamWidgetComponentByDragDrop_Transaction", "Add DreamUI Component"));
		if (UObject* WidgetOuter = Widget->GetOuter())
		{
			WidgetOuter->SetFlags(RF_Transactional);
			WidgetOuter->Modify();
		}
		Widget->SetFlags(RF_Transactional);
		Widget->Modify();
		auto PrefabEditor = FDreamUIPrefabEditor::GetEditorByWorld(Widget->GetWorld());
		if (PrefabEditor.IsValid())
		{
			PrefabEditor.Pin()->GetPrefabHelperObject()->SetAnythingDirty();
		}

		UDreamUIBehaviour* LastAddedComponent = nullptr;
		for (UClass* ComponentClass : ComponentClassesToAdd)
		{
			UDreamUIBehaviour* NewComponent = Widget->AddComponent(ComponentClass);
			if (!IsValid(NewComponent))
			{
				continue;
			}

			NewComponent->SetFlags(RF_Transactional);
			NewComponent->Modify();
			LastAddedComponent = NewComponent;
		}

		if (!IsValid(LastAddedComponent))
		{
			return false;
		}

		Widget->OnUnregister();
		Widget->OnRegister();
		FDreamUIUtils::NotifyPropertyChanged(Widget, UDreamWidget::GetPropertyName_Components());

		RefreshComponents();
		SelectComponent(LastAddedComponent);
		return true;
	}

	UClass* ResolveComponentClassFromAsset(const FAssetData& AssetData) const
	{
		UObject* Asset = AssetData.GetAsset();
		if (!IsValid(Asset))
		{
			return nullptr;
		}

		if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
		{
			UClass* GeneratedClass = Blueprint->GeneratedClass;
			return FDreamWidgetComponentClassFilter::IsComponentClassAllowed(GeneratedClass) ? GeneratedClass : nullptr;
		}

		if (UClass* ComponentClass = Cast<UClass>(Asset))
		{
			return FDreamWidgetComponentClassFilter::IsComponentClassAllowed(ComponentClass) ? ComponentClass : nullptr;
		}

		return nullptr;
	}

	bool MoveComponentToIndex(UDreamUIBehaviour* Component, int32 NewIndex)
	{
		UDreamWidget* Widget = GetCurrentWidget();
		if (!CanAddOrRemoveComponent() || !IsValid(Widget) || !IsValid(Component) || Component->GetWidget() != Widget)
		{
			return false;
		}

		const int32 CurrentIndex = ComponentItems.IndexOfByKey(Component);
		if (CurrentIndex == INDEX_NONE)
		{
			return false;
		}

		const int32 ClampedIndex = FMath::Clamp(NewIndex, 0, ComponentItems.Num());
		if (CurrentIndex == ClampedIndex || (CurrentIndex == ComponentItems.Num() - 1 && ClampedIndex == ComponentItems.Num()))
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("ReorderDreamWidgetComponent_Transaction", "Reorder DreamUI Component"));
		if (UObject* WidgetOuter = Widget->GetOuter())
		{
			WidgetOuter->SetFlags(RF_Transactional);
			WidgetOuter->Modify();
		}
		Widget->SetFlags(RF_Transactional);
		Widget->Modify();
		auto PrefabEditor = FDreamUIPrefabEditor::GetEditorByWorld(Widget->GetWorld());
		if (PrefabEditor.IsValid())
		{
			PrefabEditor.Pin()->GetPrefabHelperObject()->SetAnythingDirty();
		}

		Widget->MoveComponentToIndex(Component, ClampedIndex);
		FDreamUIUtils::NotifyPropertyChanged(Widget, UDreamWidget::GetPropertyName_Components());

		RefreshComponents();
		SelectComponent(Component);
		return true;
	}

	EVisibility GetEmptyStateVisibility() const
	{
		return ComponentItems.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
	}

	FText GetEmptyStateText() const
	{
		return IsValid(GetCurrentWidget())
			? LOCTEXT("NoWidgetComponents", "No components on this widget")
			: LOCTEXT("SelectWidgetForComponents", "Select a widget to edit components");
	}

	TSharedRef<ITableRow> HandleGenerateRow(FDreamWidgetComponentItem InItem, const TSharedRef<STableViewBase>& OwnerTable)
	{
		return
		SNew(STableRow<FDreamWidgetComponentItem>, OwnerTable)
		.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("SceneOutliner.TableViewRow"))
		.Padding(FMargin(0, 4, 0, 4))
		.ShowSelection(true)
		.OnDragDetected(this, &SDreamWidgetComponentEditor::HandleDragDetected, InItem)
		.OnCanAcceptDrop(this, &SDreamWidgetComponentEditor::HandleCanAcceptDrop)
		.OnAcceptDrop(this, &SDreamWidgetComponentEditor::HandleAcceptDrop)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image_Lambda([=, this]()
				{
					if (InItem.IsValid())
					{
						return FSlateIconFinder::FindIconBrushForClass(InItem->GetClass());
					}
					return (const FSlateBrush*)nullptr;
				})
			]
			+SHorizontalBox::Slot()
			[
				SNew(STextBlock)
				.Text(GetComponentText(InItem.Get()))
				.ToolTipText(GetComponentTooltipText(InItem.Get()))
			]
		];
	}

	void HandleSelectionChanged(FDreamWidgetComponentItem InItem, ESelectInfo::Type SelectInfo)
	{
		if (SelectInfo != ESelectInfo::Direct && ComponentListView.IsValid())
		{
			FSlateApplication::Get().SetKeyboardFocus(ComponentListView, EFocusCause::SetDirectly);
		}

		BroadcastSelectionChanged();
	}

	TSharedPtr<SWidget> OnContextMenuOpening()
	{
		FMenuBuilder MenuBuilder(true, nullptr);
		
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("EditComponent", "EDIT"));
		MenuBuilder.PushCommandList(CommandList.ToSharedRef());
		{
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
		}
		MenuBuilder.PopCommandList();
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection(NAME_None, LOCTEXT("ComponentAsset", "ASSET"));
		{
			TArray<FDreamWidgetComponentItem> SelectedItems;
			if (ComponentListView.IsValid())
			{
				ComponentListView->GetSelectedItems(SelectedItems);
			}
			UDreamUIBehaviour* SelectedComp = (SelectedItems.Num() > 0 && SelectedItems[0].IsValid()) ? SelectedItems[0].Get() : nullptr;
			UClass* ComponentClass = IsValid(SelectedComp) ? SelectedComp->GetClass() : nullptr;
			bool bHasSelection = ComponentClass != nullptr;

			FText EditLabel = bHasSelection
				? FText::Format(LOCTEXT("EditComponentClass_Label", "Edit {0}"), FText::FromString(ComponentClass->GetName()))
				: LOCTEXT("EditComponentClass_LabelNoSel", "Edit");

			MenuBuilder.AddMenuEntry(
				EditLabel,
				LOCTEXT("EditComponentClass_Tooltip", "Open Blueprint editor or C++ source for this component class"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SDreamWidgetComponentEditor::HandleEditComponentClass),
					FCanExecuteAction::CreateLambda([bHasSelection]() { return bHasSelection; })
				)
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("FindInContentBrowser_Label", "Find Class in Content Browser"),
				LOCTEXT("FindInContentBrowser_Tooltip", "Locate the Blueprint class in the Content Browser"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SDreamWidgetComponentEditor::HandleFindClassInContentBrowser),
					FCanExecuteAction::CreateSP(this, &SDreamWidgetComponentEditor::CanFindClassInContentBrowser)
				)
			);
		}
		MenuBuilder.EndSection();
		
		return MenuBuilder.MakeWidget();
	}

	void BroadcastSelectionChanged()
	{
		if (!OnSelectionChanged.IsBound())
		{
			return;
		}

		TArray<FDreamWidgetComponentItem> SelectedItems;
		if (ComponentListView.IsValid())
		{
			ComponentListView->GetSelectedItems(SelectedItems);
		}
		OnSelectionChanged.Execute(SelectedItems);
	}

	TSharedRef<SWidget> GenerateAddComponentMenu()
	{
		FClassViewerInitializationOptions Options;
		Options.Mode = EClassViewerMode::ClassPicker;
		Options.DisplayMode = EClassViewerDisplayMode::TreeView;
		Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::Dynamic;
		Options.bShowObjectRootClass = false;
		Options.bShowNoneOption = false;
		Options.bExpandAllNodes = true;
		Options.bShowUnloadedBlueprints = true;

		Options.ClassFilters.Add(MakeShared<FDreamWidgetComponentClassFilter>());

		FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
		return SNew(SBox)
			.WidthOverride(320.0f)
			.HeightOverride(400.0f)
			[
				ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateSP(this, &SDreamWidgetComponentEditor::HandleComponentClassPicked))
			];
	}

	void HandleComponentClassPicked(UClass* InClass)
	{
		FSlateApplication::Get().DismissAllMenus();

		UDreamWidget* Widget = GetCurrentWidget();
		if (!CanAddOrRemoveComponent() || !IsValid(Widget) || InClass == nullptr)
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("AddDreamWidgetComponent_Transaction", "Add DreamUI Component"));
		if (UObject* WidgetOuter = Widget->GetOuter())
		{
			WidgetOuter->SetFlags(RF_Transactional);
			WidgetOuter->Modify();
		}
		Widget->SetFlags(RF_Transactional);
		Widget->Modify();

		UDreamUIBehaviour* NewComponent = Widget->AddComponent(InClass);
		if (!IsValid(NewComponent))
		{
			return;
		}

		NewComponent->SetFlags(RF_Transactional);
		NewComponent->Modify();
		FDreamUIUtils::NotifyPropertyChanged(Widget, UDreamWidget::GetPropertyName_Components());

		RefreshComponents();
		SelectComponent(NewComponent);
	}

	void HandleEditComponentClass()
	{
		TArray<FDreamWidgetComponentItem> SelectedItems;
		if (ComponentListView.IsValid())
		{
			ComponentListView->GetSelectedItems(SelectedItems);
		}
		if (SelectedItems.Num() == 0 || !SelectedItems[0].IsValid())
		{
			return;
		}
		UClass* ComponentClass = SelectedItems[0]->GetClass();
		if (UBlueprint* Blueprint = Cast<UBlueprint>(ComponentClass->ClassGeneratedBy))
		{
			GEditor->EditObject(Blueprint);
		}
		else
		{
			FSourceCodeNavigation::NavigateToClass(ComponentClass);
		}
	}

	void HandleFindClassInContentBrowser()
	{
		TArray<FDreamWidgetComponentItem> SelectedItems;
		if (ComponentListView.IsValid())
		{
			ComponentListView->GetSelectedItems(SelectedItems);
		}
		if (SelectedItems.Num() == 0 || !SelectedItems[0].IsValid())
		{
			return;
		}
		UClass* ComponentClass = SelectedItems[0]->GetClass();
		if (UBlueprint* Blueprint = Cast<UBlueprint>(ComponentClass->ClassGeneratedBy))
		{
			TArray<UObject*> Objects;
			Objects.Add(Blueprint);
			GEditor->SyncBrowserToObjects(Objects);
		}
	}

	bool CanFindClassInContentBrowser() const
	{
		TArray<FDreamWidgetComponentItem> SelectedItems;
		if (ComponentListView.IsValid())
		{
			ComponentListView->GetSelectedItems(SelectedItems);
		}
		if (SelectedItems.Num() == 0 || !SelectedItems[0].IsValid())
		{
			return false;
		}
		UClass* ComponentClass = SelectedItems[0]->GetClass();
		return Cast<UBlueprint>(ComponentClass->ClassGeneratedBy) != nullptr;
	}

	void HandleRemoveSelectedComponents()
	{
		UDreamWidget* Widget = GetCurrentWidget();
		if (!CanRemoveSelectedComponents() || !IsValid(Widget) || !ComponentListView.IsValid())
		{
			return;
		}

		TArray<FDreamWidgetComponentItem> SelectedItems;
		ComponentListView->GetSelectedItems(SelectedItems);

		const FScopedTransaction Transaction(LOCTEXT("RemoveDreamWidgetComponent_Transaction", "Remove DreamUI Component"));
		if (UObject* WidgetOuter = Widget->GetOuter())
		{
			WidgetOuter->SetFlags(RF_Transactional);
			WidgetOuter->Modify();
		}
		Widget->SetFlags(RF_Transactional);
		Widget->Modify();

		for (const FDreamWidgetComponentItem& Item : SelectedItems)
		{
			UDreamUIBehaviour* Component = Item.Get();
			if (!IsValid(Component) || Component->GetWidget() != Widget)
			{
				continue;
			}

			Component->SetFlags(RF_Transactional);
			Component->Modify();
			Widget->RemoveComponent(Component);
			FDreamUIUtils::NotifyPropertyChanged(Widget, UDreamWidget::GetPropertyName_Components());
		}

		RefreshComponents();
		ClearSelection();
	}

	static FText GetComponentText(const UDreamUIBehaviour* InComponent)
	{
		if (!IsValid(InComponent))
		{
			return LOCTEXT("InvalidDreamWidgetComponent", "Invalid Component");
		}
		return InComponent->GetClass()->GetDisplayNameText();
	}
	static FText GetComponentTooltipText(const UDreamUIBehaviour* InComponent)
	{
		if (!IsValid(InComponent))
		{
			return LOCTEXT("InvalidDreamWidgetComponent", "Invalid Component");
		}

		return FText::FromString(InComponent->GetName());
	}

	FOnGetWidgetContext GetWidgetContext;
	FOnCanEdit CanEdit;
	FOnComponentsSelectionChanged OnSelectionChanged;
	TArray<FDreamWidgetComponentItem> ComponentItems;
	TSharedPtr<SWidget> ToolbarWidget;
	TSharedPtr<SListView<FDreamWidgetComponentItem>> ComponentListView;
	TSharedPtr<FUICommandList> CommandList;
};

/**
 * The name and class strip above the details view.
 *
 * The stock name area only fills itself in for actors and actor components -- a UDreamWidget is
 * neither -- and the editable box it would offer renames the UObject rather than the DisplayName
 * that every other panel shows, so only its icon and lock button are reused here. The class link is
 * rebuilt from Tick because FEditorClassUtils::GetSourceLink builds against a fixed class, while the
 * panel moves between widgets and behaviours as the selection changes.
 */
class SDreamWidgetDetailsHeader : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal(UObject*, FOnGetEditedObject);
	DECLARE_DELEGATE_RetVal(bool, FOnCanEdit);

	SLATE_BEGIN_ARGS(SDreamWidgetDetailsHeader)
	{
	}
		SLATE_EVENT(FOnGetEditedObject, GetEditedObject)
		SLATE_EVENT(FOnCanEdit, CanEdit)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, NameAreaWidget)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		GetEditedObject = InArgs._GetEditedObject;
		CanEdit = InArgs._CanEdit;

		ChildSlot
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				InArgs._NameAreaWidget.IsValid() ? InArgs._NameAreaWidget.ToSharedRef() : SNullWidget::NullWidget
			]
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(4, 0))
			[
				// The name is pushed from Tick, deliberately NOT bound. An SEditableTextBox
				// re-runs OnVerifyTextChanged every time its text changes -- including a change the
				// binding pushes -- and a passing verification calls SetError(""), which destroys
				// the error popup's window then and there. Bindings are updated during Slate's
				// child walk, so that destroy removes a slot from the window overlay while the walk
				// is iterating it, and the walk reads the count it took before the slot went away.
				SAssignNew(NameBox, SEditableTextBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.HintText(LOCTEXT("WidgetNameHint", "Name"))
				.IsEnabled(this, &SDreamWidgetDetailsHeader::CanRename)
				.SelectAllTextWhenFocused(true)
				.RevertTextOnEscape(true)
				.OnVerifyTextChanged(this, &SDreamWidgetDetailsHeader::VerifyDisplayName)
				.OnTextCommitted(this, &SDreamWidgetDetailsHeader::OnDisplayNameCommitted)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(SourceLinkBox, SBox)
			]
		];

		RebuildSourceLink();
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		UObject* EditedObject = GetCurrentObject();
		if (EditedObject != CachedObject.Get(true))
		{
			CachedObject = EditedObject;
			RebuildSourceLink();
		}
		// Never while the user is in the box: this would overwrite what they are typing.
		if (NameBox.IsValid() && !NameBox->HasAnyUserFocusOrFocusedDescendants())
		{
			const FText Current = GetEditedObjectText();
			if (!NameBox->GetText().EqualTo(Current))
			{
				NameBox->SetText(Current);
			}
		}
	}

private:
	UObject* GetCurrentObject() const
	{
		return GetEditedObject.IsBound() ? GetEditedObject.Execute() : nullptr;
	}

	UDreamWidget* GetCurrentWidget() const
	{
		return Cast<UDreamWidget>(GetCurrentObject());
	}

	FText GetEditedObjectText() const
	{
		UObject* EditedObject = GetCurrentObject();
		if (!IsValid(EditedObject))
		{
			return FText::GetEmpty();
		}
		if (auto Widget = Cast<UDreamWidget>(EditedObject))
		{
			return FText::FromString(Widget->GetDisplayName());
		}
		return FText::FromString(EditedObject->GetName());
	}

	bool CanRename() const
	{
		return IsValid(GetCurrentWidget()) && (!CanEdit.IsBound() || CanEdit.Execute());
	}

	bool VerifyDisplayName(const FText& InText, FText& OutErrorMessage) const
	{
		const FString ProposedName = InText.ToString().TrimStartAndEnd();
		if (ProposedName.IsEmpty())
		{
			OutErrorMessage = LOCTEXT("EmptyWidgetName", "Widget name cannot be empty.");
			return false;
		}
		return FName::IsValidXName(ProposedName, FString(INVALID_OBJECTNAME_CHARACTERS) + TEXT("/"), &OutErrorMessage);
	}

	void OnDisplayNameCommitted(const FText& InText, ETextCommit::Type CommitInfo)
	{
		UDreamWidget* Widget = GetCurrentWidget();
		if (!CanRename() || !IsValid(Widget))
		{
			return;
		}
		const FString ProposedName = InText.ToString().TrimStartAndEnd();
		if (ProposedName.IsEmpty() || ProposedName == Widget->GetDisplayName())
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("ChangeWidgetName_Transaction", "Change Name"));
		Widget->SetFlags(RF_Transactional);
		Widget->Modify();
		const FString UniqueName = FDreamUIEditorTools::MakeUniqueWidgetDisplayName(Widget, ProposedName, Widget);
		FDreamUIUtils::ChangePropertyWithNotify(Widget, UDreamWidget::GetPropertyName_DisplayName(), [Widget, UniqueName]()
		{
			Widget->SetDisplayName(UniqueName);
		});
	}

	void RebuildSourceLink()
	{
		UObject* EditedObject = GetCurrentObject();
		if (!IsValid(EditedObject))
		{
			SourceLinkBox->SetContent(SNullWidget::NullWidget);
			return;
		}

		FEditorClassUtils::FSourceLinkParams Params;
		Params.Object = EditedObject;
		Params.bUseDefaultFormat = true;
		Params.bEmptyIfNoLink = true;
		SourceLinkBox->SetContent(FEditorClassUtils::GetSourceLink(EditedObject->GetClass(), Params));
	}

	FOnGetEditedObject GetEditedObject;
	FOnCanEdit CanEdit;
	TWeakObjectPtr<UObject> CachedObject;
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SBox> SourceLinkBox;
};

void SDreamUIPrefabEditorDetails::Construct(const FArguments& Args, UWorld* InWorld)
{
	World = InWorld;
	PrefabEditorPtr = FDreamUIPrefabEditor::GetEditorByWorld(World.Get());
	if (PrefabEditorPtr.IsValid())
	{
		//if open in PrefabEditor then sync selection by PrefabEditor
		PrefabEditorPtr.Pin()->OnSelectionChanged.AddRaw(this, &SDreamUIPrefabEditorDetails::OnEditorSelectionChanged);
	}
	else
	{
		//if open in WidgetInspector then sync selection by DreamUISelection
		UDreamUISelection::GetInstance(World.Get())->OnSelectionChanged.AddRaw(this, &SDreamUIPrefabEditorDetails::OnEditorSelectionChanged);
	}

    FPropertyEditorModule& PropPlugin = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
    DetailsViewArgs.bLockable = true;
    DetailsViewArgs.NotifyHook = GUnrealEd;
    DetailsViewArgs.ViewIdentifier = FName(TEXT("DreamUIPrefabEditor"));
    DetailsViewArgs.bCustomNameAreaLocation = true;
    DetailsViewArgs.bCustomFilterAreaLocation = false;
    DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Hide;
    // A UDreamWidget is a plain UObject: under the actor/component filters the name area resolves to
    // nothing at all and the header comes up blank whatever it is docked into.
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ObjectsUseNameArea;
    DetailsViewArgs.bShowOptions = true;
	DetailsViewArgs.bAllowSearch = true;
	//the stock label renames the UObject; SDreamWidgetDetailsHeader edits DisplayName instead
	DetailsViewArgs.bShowObjectLabel = false;
    //DetailsViewArgs.HostCommandList = InCommandList;

    DetailsView = PropPlugin.CreateDetailView(DetailsViewArgs);
    DetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SDreamUIPrefabEditorDetails::IsPropertyReadOnly));

	if (PrefabEditorPtr.IsValid())
	{
		TSharedRef<FDreamUIDetailKeyframeHandler> KeyframeHandler = MakeShareable(new FDreamUIDetailKeyframeHandler(PrefabEditorPtr.Pin()));
		DetailsView->SetKeyframeHandler(KeyframeHandler);
	}

	TSharedRef<FDreamWidgetDetailPropertyExtensionHandler> BindingHandler = MakeShareable(new FDreamWidgetDetailPropertyExtensionHandler(World.Get()));
	DetailsView->SetExtensionHandler(BindingHandler);

	ComponentEditor = SNew(SDreamWidgetComponentEditor)
		.GetWidgetContext(this, &SDreamUIPrefabEditorDetails::GetSelectedWidgetContext)
		.CanEdit(this, &SDreamUIPrefabEditorDetails::IsEditorAllowEditing)
		.OnSelectionChanged(this, &SDreamUIPrefabEditorDetails::OnComponentSelectionChanged);

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(FMargin(2, 2))
			.AutoHeight()
			[
				ComponentEditor->GetToolbarWidget()
			]
			+ SVerticalBox::Slot()
			.Padding(FMargin(2, 2))
			.AutoHeight()
			[
				SNew(SBox)
				.Visibility(this, &SDreamUIPrefabEditorDetails::GetPrefabButtonVisibility)
				.IsEnabled(this, &SDreamUIPrefabEditorDetails::IsPrefabButtonEnable)
				.HeightOverride(this, &SDreamUIPrefabEditorDetails::GetPrefabButtonHeight)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FMargin(4, 0))
					[
						SNew(SBox)
						.HAlign(EHorizontalAlignment::HAlign_Center)
						.VAlign(EVerticalAlignment::VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("PrefabFunctions", "Prefab"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.2f)
					.Padding(FMargin(2, 0))
					[
						SNew(SButton)
						.OnClicked_Lambda([=, this]() {
							if (PrefabEditorPtr.IsValid())
							{
								PrefabEditorPtr.Pin()->OpenSubPrefab(CachedWidget.Get());
							}
							return FReply::Handled();
						})
						[
							SNew(SBox)
							.HAlign(EHorizontalAlignment::HAlign_Center)
							.VAlign(EVerticalAlignment::VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("OpenPrefab", "Open"))
								.Font(IDetailLayoutBuilder::GetDetailFont())
							]
						]
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.2f)
					.Padding(FMargin(2, 0))
					[
						SNew(SButton)
						.OnClicked_Lambda([=, this]() {
							if (PrefabEditorPtr.IsValid())
							{
								PrefabEditorPtr.Pin()->SelectSubPrefab(CachedWidget.Get());
							}
							return FReply::Handled();
						})
						[
							SNew(SBox)
							.HAlign(EHorizontalAlignment::HAlign_Center)
							.VAlign(EVerticalAlignment::VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SelectPrefab", "Select"))
								.Font(IDetailLayoutBuilder::GetDetailFont())
							]
						]
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					.Padding(FMargin(2, 0))
					[
						SNew(SComboButton)
						.HasDownArrow(true)
						.ToolTipText(LOCTEXT("PrefabOverride", "Edit override parameters for this prefab"))
						.ButtonContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("OverrideButton", "Prefab Override Properties"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
						.MenuContent()
						[
							SNew(SBox)
							.Padding(FMargin(4, 4))
							[
								SNew(SHorizontalBox)
								+SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SVerticalBox)
									+SVerticalBox::Slot()
									.AutoHeight()
									[
										SNew(SHorizontalBox)
										+SHorizontalBox::Slot()
										.AutoWidth()
										[
											SAssignNew(PrefabOverrideDataViewer, SDreamUIPrefabOverrideDataViewer, [=, this]()
											{
												return CachedWidget.Get();
											})
											.AfterRevertPrefab_Lambda([=, this](UDreamUIPrefab* PrefabAsset) {
												})
											.AfterApplyPrefab_Lambda([=, this](UDreamUIPrefab* PrefabAsset){
												FDreamUIEditorTools::RefreshLoadedPrefab();
												FDreamUIEditorTools::RefreshOnSubPrefabChange(PrefabAsset);
												FDreamUIEditorTools::RefreshOpenedPrefabEditor(PrefabAsset);
												})
										]
									]
								]
							]
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			[
				SNew(SSplitter)
				.Orientation(EOrientation::Orient_Vertical)
				+ SSplitter::Slot()
				.Resizable(true)
				.SizeRule(SSplitter::ESizeRule::FractionOfParent)
				.Value(0.2f)
				[
					SNew(SBox)
					.MinDesiredHeight(200)
					.Padding(FMargin(0, 2))
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("SCSEditor.Background"))
						.Padding(4.f)
						.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ComponentsPanel")))
						[
							ComponentEditor.ToSharedRef()
						]
					]
				]
				+ SSplitter::Slot()
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0, 2))
					[
						SNew(SDreamWidgetDetailsHeader)
						.NameAreaWidget(DetailsView->GetNameAreaWidget())
						.CanEdit(this, &SDreamUIPrefabEditorDetails::IsEditorAllowEditing)
						.GetEditedObject_Lambda([=, this]() -> UObject*
						{
							const TArray<TWeakObjectPtr<UObject>>& EditedObjects = DetailsView->GetSelectedObjects();
							return EditedObjects.Num() == 1 ? EditedObjects[0].Get() : nullptr;
						})
					]
					+SVerticalBox::Slot()
					.Padding(FMargin(0, 2))
					[
						DetailsView.ToSharedRef()
					]
				]
			]
		];
}

SDreamUIPrefabEditorDetails::~SDreamUIPrefabEditorDetails()
{
	if (TSharedPtr<FDreamUIPrefabEditor> PrefabEditor = PrefabEditorPtr.Pin())
	{
		PrefabEditor->OnSelectionChanged.RemoveAll(this);
	}
	if (auto Selection = UDreamUISelection::GetInstance(World.Get()))
	{
		Selection->OnSelectionChanged.RemoveAll(this);
	}
}

bool SDreamUIPrefabEditorDetails::IsPrefabButtonEnable()const
{
	if (PrefabEditorPtr.IsValid() && CachedWidget.IsValid())
	{
		return PrefabEditorPtr.Pin()->WidgetIsSubPrefabRoot(CachedWidget.Get());
	}
	return false;
}

FOptionalSize SDreamUIPrefabEditorDetails::GetPrefabButtonHeight()const
{
	return IsPrefabButtonEnable() ? 26 : 0;
}

EVisibility SDreamUIPrefabEditorDetails::GetPrefabButtonVisibility()const
{
	return IsPrefabButtonEnable() ? EVisibility::Visible : EVisibility::Hidden;
}

bool SDreamUIPrefabEditorDetails::IsEditorAllowEditing()const
{
	if (PrefabEditorPtr.IsValid() && CachedWidget.IsValid())
	{
		return !PrefabEditorPtr.Pin()->WidgetBelongsToSubPrefab(CachedWidget.Get());
	}
	return true;
}

UDreamWidget* SDreamUIPrefabEditorDetails::GetSelectedWidgetContext() const
{
	if (auto Selection = UDreamUISelection::GetInstance(World.Get()))
	{
		auto SelectedWidgets = Selection->GetSelectedWidgets();
		if (SelectedWidgets.Num() > 0 && SelectedWidgets[0].IsValid())
		{
			return SelectedWidgets[0].Get();
		}
	}
	return nullptr;
}

void SDreamUIPrefabEditorDetails::OnEditorSelectionChanged()
{
	if (bIsSelectFromComponentList)return;
	bIsSelectFromDreamUIEditor = true;
	auto Selection = UDreamUISelection::GetInstance(World.Get());
	auto SelectedWidgets = Selection->GetSelectedWidgets();
	auto SelectedComponents = Selection->GetSelectedComponents();
	if (SelectedWidgets.Num() > 0)
	{
		if (auto Widget = SelectedWidgets[0].Get())
		{
			if (Widget->GetWorld() != World.Get())
			{
				bIsSelectFromDreamUIEditor = false;
				return;
			}

			CachedWidget = Widget;
			PrefabOverrideDataViewer->RefreshDataContent();
			if (ComponentEditor)
			{
				ComponentEditor->RefreshComponents();
				ComponentEditor->ClearSelection();
			}
		}

		TArray<UObject*> SelectedObjectList;
		for (int32 i = 0; i < SelectedWidgets.Num(); i++)
		{
			auto Widget = SelectedWidgets[i];
			if (Widget.IsValid())
			{
				if (Widget->GetWorld() != World.Get())
				{
					continue;
				}

				Widget->SetFlags(RF_Transactional);
				ForEachObjectWithOuter(Widget.Get(), [=](UObject* Object) {
					Object->SetFlags(RF_Transactional);
				});
				SelectedObjectList.Add(Widget.Get());
			}
		}

		if (DetailsView)
		{
			DetailsView->SetObjects(SelectedObjectList, true);
		}
		if (SelectedObjectList.Num() == 0)
		{
			CachedWidget = nullptr;
			PrefabOverrideDataViewer->RefreshDataContent();
			if (ComponentEditor)
			{
				ComponentEditor->RefreshComponents();
				ComponentEditor->ClearSelection();
			}
		}
		else
		{
			if (SelectedComponents.Num() > 0)
			{
				if (ComponentEditor)
				{
					ComponentEditor->SelectComponent(SelectedComponents[0].Get());
				}
			}
		}
	}
	else
	{
		TArray<UObject*> SelectedObjectList;
		if (DetailsView)
		{
			DetailsView->SetObjects(SelectedObjectList, true);
		}
		CachedWidget = nullptr;
		PrefabOverrideDataViewer->RefreshDataContent();
		if (ComponentEditor)
		{
			ComponentEditor->RefreshComponents();
			ComponentEditor->ClearSelection();
		}
	}
	bIsSelectFromDreamUIEditor = false;
}

void SDreamUIPrefabEditorDetails::OnComponentSelectionChanged(const TArray<TWeakObjectPtr<UDreamUIBehaviour>>& SelectedComponents)
{
	bIsSelectFromComponentList = true;

	TArray<UObject*> SelectedObjects;
	TArray<UDreamUIBehaviour*> ValidSelectedComponents;
	for (const TWeakObjectPtr<UDreamUIBehaviour>& SelectedComponent : SelectedComponents)
	{
		if (UDreamUIBehaviour* Component = SelectedComponent.Get())
		{
			Component->SetFlags(RF_Transactional);
			ForEachObjectWithOuter(Component, [=](UObject* Object) {
				Object->SetFlags(RF_Transactional);
			});
			SelectedObjects.Add(Component);
			ValidSelectedComponents.Add(Component);
		}
	}

	if (DetailsView.IsValid())
	{
		if (SelectedObjects.Num() > 0)
		{
			DetailsView->SetObjects(SelectedObjects);
		}
		else if (PrefabEditorPtr.IsValid())
		{
			TArray<UObject*> SelectedWidgetObjects;
			auto RootWidget = PrefabEditorPtr.Pin()->GetRootAgentWidget();
			for (const TWeakObjectPtr<UDreamWidget>& Widget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
			{
				if (Widget.IsValid() && (Widget->IsChildOf(RootWidget) || Widget.Get() == RootWidget))
				{
					SelectedWidgetObjects.Add(Widget.Get());
				}
			}
			DetailsView->SetObjects(SelectedWidgetObjects, true);
		}
	}

	if (!bIsSelectFromDreamUIEditor)
	{
		auto Selection = UDreamUISelection::GetInstance(World.Get());
		Selection->ClearComponentSelection();
		for (UDreamUIBehaviour* Component : ValidSelectedComponents)
		{
			Selection->SelectComponent(Component);
		}
	}
	bIsSelectFromComponentList = false;
}

void SDreamUIPrefabEditorDetails::Refresh()
{
	if (DetailsView.IsValid())
	{
		DetailsView->ForceRefresh();
	}
}

bool SDreamUIPrefabEditorDetails::IsPropertyReadOnly(const FPropertyAndParent& InPropertyAndParent)
{
	return false;
}

#undef LOCTEXT_NAMESPACE
