// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "DetailLayoutBuilder.h"
#include "ScopedTransaction.h"
#include "SPositiveActionButton.h"
#include "SourceCodeNavigation.h"
#include "AssetSelection.h"
#include "AssetRegistry/AssetData.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Engine/Blueprint.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "UObject/StrongObjectPtr.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "DreamUIEditorTools.h"
#include "DreamWidgetBlueprintEditor.h"
#include "Utils/DreamUIUtils.h"

/**
 * The component list of the prefab editor's details panel: the behaviours on the selected widget,
 * with add / remove / reorder / copy / paste and asset drops. Used by SDreamUIPrefabEditorDetails
 * in the prefab editor and by the Widget Inspector window.
 *
 * The class body is written inline here, as it was when it lived inside the details panel's .cpp;
 * the free functions below it are out-of-line in SDreamWidgetComponentEditor.cpp so headless tests
 * can link against them (DreamPrefabPanelsAutomationTests declares the prototypes itself).
 */

/** Whether a component of this class may be put on a widget at all. */
bool DreamUIWidgetComponentClipboard_CanPasteClass(const UClass* InComponentClass);
/** Whether this component can be cut or copied. */
bool DreamUIWidgetComponentClipboard_CanTakeComponent(const UDreamUIBehaviour* InComponent);
/** A stand-alone copy of the component, outered to the transient package, for the clipboard to hold. */
UDreamUIBehaviour* DreamUIWidgetComponentClipboard_Snapshot(UDreamUIBehaviour* InSource);
/** A new component on InTargetWidget with InSource's properties; null when the class is refused. */
UDreamUIBehaviour* DreamUIWidgetComponentClipboard_PasteOnto(UDreamWidget* InTargetWidget, UDreamUIBehaviour* InSource);
/** The one clipboard every panel shares. */
TStrongObjectPtr<UDreamUIBehaviour>& DreamUIWidgetComponentClipboard();
/** Drop the clipboard while the editor is still up. Called from module shutdown. */
void DreamUIWidgetComponentClipboard_Reset();

#define LOCTEXT_NAMESPACE "DreamGUIPrefabEditorDetailTab"

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
	virtual ~SDreamWidgetComponentEditor() override
	{
		BindComponentsChangedEvent(nullptr);
	}

	void RefreshComponents()
	{
		ComponentItems.Reset();

		UDreamWidget* CurrentWidget = GetCurrentWidget();
		BindComponentsChangedEvent(CurrentWidget);
		if (const UDreamWidget* Widget = CurrentWidget)
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

	/** Follow the widget's own component list so additions made outside this panel (the panel-type
	 *  dropdown adding a ContentWidget, for one) show up without a re-select. */
	void BindComponentsChangedEvent(UDreamWidget* Widget)
	{
		if (BoundWidget.Get() == Widget)
		{
			return;
		}
		if (UDreamWidget* Previous = BoundWidget.Get())
		{
			Previous->GetComponentsChangedEvent().Remove(ComponentsChangedHandle);
		}
		ComponentsChangedHandle.Reset();
		BoundWidget = Widget;
		if (IsValid(Widget))
		{
			ComponentsChangedHandle = Widget->GetComponentsChangedEvent().AddSP(this, &SDreamWidgetComponentEditor::HandleComponentsChanged);
		}
	}

	void HandleComponentsChanged(EDreamWidgetComponentsChangedType)
	{
		RefreshComponents();
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
		// On the template when a designer owns this widget; the preview's copy goes with the rebuild.
		if (auto Designer = FDreamWidgetBlueprintEditor::GetEditorByWorld(Widget->GetWorld()).Pin();
			Designer.IsValid() && Designer->GetTemplateWidget(Widget) != nullptr)
		{
			Designer->DesignerRemoveComponent(Widget, Component);
		}
		else
		{
			Widget->RemoveComponent(Component);
		}
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
		auto PrefabEditor = FDreamWidgetBlueprintEditor::GetEditorByWorld(Widget->GetWorld());
		if (PrefabEditor.IsValid())
		{
			PrefabEditor.Pin()->MarkDesignChanged();
		}

		// A behaviour is an instanced sub-object of the widget, so adding one to a preview builds it
		// into the copy the next rebuild throws away.
		if (PrefabEditor.IsValid() && PrefabEditor.Pin()->GetTemplateWidget(Widget) != nullptr)
		{
			UDreamUIBehaviour* Added = PrefabEditor.Pin()->DesignerAddComponents(Widget, ComponentClassesToAdd);
			if (!IsValid(Added))
			{
				return false;
			}
			RefreshComponents();
			SelectComponent(Added);
			return true;
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
		auto PrefabEditor = FDreamWidgetBlueprintEditor::GetEditorByWorld(Widget->GetWorld());
		if (PrefabEditor.IsValid())
		{
			PrefabEditor.Pin()->MarkDesignChanged();
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
			// See the cut path: a designer's widget keeps its behaviours on the template.
			if (auto Designer = FDreamWidgetBlueprintEditor::GetEditorByWorld(Widget->GetWorld()).Pin();
				Designer.IsValid() && Designer->GetTemplateWidget(Widget) != nullptr)
			{
				Designer->DesignerRemoveComponent(Widget, Component);
			}
			else
			{
				Widget->RemoveComponent(Component);
			}
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
	TWeakObjectPtr<UDreamWidget> BoundWidget;
	FDelegateHandle ComponentsChangedHandle;
};

#undef LOCTEXT_NAMESPACE
