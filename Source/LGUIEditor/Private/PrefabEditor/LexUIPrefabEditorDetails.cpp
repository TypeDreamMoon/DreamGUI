// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabEditorDetails.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Modules/ModuleManager.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/NotifyHook.h"
#include "LexUIPrefabEditor.h"
#include "DetailLayoutBuilder.h"
#include "LexWidgetDetailPropertyExtensionHandler.h"
#include "LexUIPrefabOverrideDataViewer.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "LexUIEditorTools.h"
#include "Core/LexUIBehaviour.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexWidget.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "ScopedTransaction.h"
#include "SPositiveActionButton.h"
#include "Framework/Commands/GenericCommands.h"
#include "Styling/SlateIconFinder.h"
#include "Utils/LexUIUtils.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabEditorDetailTab"

using FLexWidgetComponentItem = TWeakObjectPtr<ULexUIBehaviour>;

class FLexWidgetComponentClassFilter : public IClassViewerFilter
{
public:
	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return IsComponentClassAllowed(InClass);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return InUnloadedClassData->IsChildOf(ULexUIBehaviour::StaticClass())
			&& !InUnloadedClassData->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Hidden);
	}

private:
	static bool IsComponentClassAllowed(const UClass* InClass)
	{
		return InClass != nullptr
			&& InClass->IsChildOf(ULexUIBehaviour::StaticClass())
			&& !InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Hidden | CLASS_Transient)
			&& InClass->HasMetaData("BlueprintSpawnableComponent")
				;
	}
};

class SLexWidgetComponentEditor : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal(ULexWidget*, FOnGetWidgetContext);
	DECLARE_DELEGATE_RetVal(bool, FOnCanEdit);
	DECLARE_DELEGATE_OneParam(FOnComponentsSelectionChanged, const TArray<FLexWidgetComponentItem>&);

	SLATE_BEGIN_ARGS(SLexWidgetComponentEditor)
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
			FExecuteAction::CreateSP(this, &SLexWidgetComponentEditor::HandleRemoveSelectedComponents),
			FCanExecuteAction::CreateSP(this, &SLexWidgetComponentEditor::CanRemoveSelectedComponents)
		);
		
		GetWidgetContext = InArgs._GetWidgetContext;
		CanEdit = InArgs._CanEdit;
		OnSelectionChanged = InArgs._OnSelectionChanged;

		ToolbarWidget =
		SNew(SBox)
		.Padding(FMargin(0, 0, 4, 0))
		[
			SNew(SPositiveActionButton)
			.IsEnabled(this, &SLexWidgetComponentEditor::CanAddOrRemoveComponent)
			.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
			.Text(LOCTEXT("AddWidgetComponent", "Add Component"))
			.ToolTipText(LOCTEXT("AddWidgetComponentTooltip", "Add a LexUI component to the selected widget"))
			.OnGetMenuContent(this, &SLexWidgetComponentEditor::GenerateAddComponentMenu)
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
					SAssignNew(ComponentListView, SListView<FLexWidgetComponentItem>)
					.ListItemsSource(&ComponentItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SLexWidgetComponentEditor::HandleGenerateRow)
					.OnSelectionChanged(this, &SLexWidgetComponentEditor::HandleSelectionChanged)
					.OnContextMenuOpening(this, &SLexWidgetComponentEditor::OnContextMenuOpening)
				]
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Visibility(this, &SLexWidgetComponentEditor::GetEmptyStateVisibility)
				.Text(this, &SLexWidgetComponentEditor::GetEmptyStateText)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];

		RefreshComponents();
	}

	TSharedRef<SWidget> GetToolbarWidget() const
	{
		return ToolbarWidget.ToSharedRef();
	}
	void RefreshComponents()
	{
		ComponentItems.Reset();

		if (const ULexWidget* Widget = GetCurrentWidget())
		{
			for (ULexUIBehaviour* Component : Widget->GetAllComponents())
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

	void SelectComponent(ULexUIBehaviour* InComponent)
	{
		if (!ComponentListView.IsValid() || !IsValid(InComponent))
		{
			return;
		}

		const FLexWidgetComponentItem Item = InComponent;
		ComponentListView->SetSelection(Item, ESelectInfo::Direct);
		ComponentListView->RequestScrollIntoView(Item);
	}

private:
	ULexWidget* GetCurrentWidget() const
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

		TArray<FLexWidgetComponentItem> SelectedItems;
		ComponentListView->GetSelectedItems(SelectedItems);
		return SelectedItems.Num() > 0;
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

	TSharedRef<ITableRow> HandleGenerateRow(FLexWidgetComponentItem InItem, const TSharedRef<STableViewBase>& OwnerTable)
	{
		return
		SNew(STableRow<FLexWidgetComponentItem>, OwnerTable)
		.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("SceneOutliner.TableViewRow"))
		.Padding(FMargin(0, 4, 0, 4))
		.ShowSelection(true)
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

	void HandleSelectionChanged(FLexWidgetComponentItem InItem, ESelectInfo::Type SelectInfo)
	{
		BroadcastSelectionChanged();
	}

	TSharedPtr<SWidget> OnContextMenuOpening()
	{
		FMenuBuilder MenuBuilder(true, nullptr);
		MenuBuilder.PushCommandList(CommandList.ToSharedRef());
		{
			// MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
			// MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
			// MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
			// MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
		}
		MenuBuilder.PopCommandList();
		return MenuBuilder.MakeWidget();
	}

	void BroadcastSelectionChanged()
	{
		if (!OnSelectionChanged.IsBound())
		{
			return;
		}

		TArray<FLexWidgetComponentItem> SelectedItems;
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

		Options.ClassFilters.Add(MakeShared<FLexWidgetComponentClassFilter>());

		FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
		return SNew(SBox)
			.WidthOverride(320.0f)
			.HeightOverride(400.0f)
			[
				ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateSP(this, &SLexWidgetComponentEditor::HandleComponentClassPicked))
			];
	}

	void HandleComponentClassPicked(UClass* InClass)
	{
		FSlateApplication::Get().DismissAllMenus();

		ULexWidget* Widget = GetCurrentWidget();
		if (!CanAddOrRemoveComponent() || !IsValid(Widget) || InClass == nullptr)
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("AddLexWidgetComponent_Transaction", "Add LexUI Component"));
		if (UObject* WidgetOuter = Widget->GetOuter())
		{
			WidgetOuter->SetFlags(RF_Transactional);
			WidgetOuter->Modify();
		}
		Widget->SetFlags(RF_Transactional);
		Widget->Modify();

		ULexUIBehaviour* NewComponent = Widget->AddComponent(InClass);
		if (!IsValid(NewComponent))
		{
			return;
		}

		NewComponent->SetFlags(RF_Transactional);
		NewComponent->Modify();
		Widget->OnUnregister();
		Widget->OnRegister();
		FLexUIUtils::NotifyPropertyChanged(Widget, ULexWidget::GetPropertyName_Components());

		RefreshComponents();
		SelectComponent(NewComponent);
	}

	void HandleRemoveSelectedComponents()
	{
		ULexWidget* Widget = GetCurrentWidget();
		if (!CanRemoveSelectedComponents() || !IsValid(Widget) || !ComponentListView.IsValid())
		{
			return;
		}

		TArray<FLexWidgetComponentItem> SelectedItems;
		ComponentListView->GetSelectedItems(SelectedItems);

		const FScopedTransaction Transaction(LOCTEXT("RemoveLexWidgetComponent_Transaction", "Remove LexUI Component"));
		if (UObject* WidgetOuter = Widget->GetOuter())
		{
			WidgetOuter->SetFlags(RF_Transactional);
			WidgetOuter->Modify();
		}
		Widget->SetFlags(RF_Transactional);
		Widget->Modify();

		for (const FLexWidgetComponentItem& Item : SelectedItems)
		{
			ULexUIBehaviour* Component = Item.Get();
			if (!IsValid(Component) || Component->GetWidget() != Widget)
			{
				continue;
			}

			Component->SetFlags(RF_Transactional);
			Component->Modify();
			Widget->RemoveComponent(Component);
		}

		RefreshComponents();
		ClearSelection();
	}

	static FText GetComponentText(const ULexUIBehaviour* InComponent)
	{
		if (!IsValid(InComponent))
		{
			return LOCTEXT("InvalidLexWidgetComponent", "Invalid Component");
		}
		return InComponent->GetClass()->GetDisplayNameText();
	}
	static FText GetComponentTooltipText(const ULexUIBehaviour* InComponent)
	{
		if (!IsValid(InComponent))
		{
			return LOCTEXT("InvalidLexWidgetComponent", "Invalid Component");
		}

		return FText::FromString(InComponent->GetName());
	}

	FOnGetWidgetContext GetWidgetContext;
	FOnCanEdit CanEdit;
	FOnComponentsSelectionChanged OnSelectionChanged;
	TArray<FLexWidgetComponentItem> ComponentItems;
	TSharedPtr<SWidget> ToolbarWidget;
	TSharedPtr<SListView<FLexWidgetComponentItem>> ComponentListView;
	TSharedPtr<FUICommandList> CommandList;
};

void SLexUIPrefabEditorDetails::Construct(const FArguments& Args, TSharedPtr<FLexUIPrefabEditor> InPrefabEditor)
{
	PrefabEditorPtr = InPrefabEditor;

	InPrefabEditor->OnSelectedWidgetsChanged.AddRaw(this, &SLexUIPrefabEditorDetails::OnEditorSelectionChanged);

    FPropertyEditorModule& PropPlugin = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.bUpdatesFromSelection = true;
    DetailsViewArgs.bLockable = true;
    DetailsViewArgs.NotifyHook = GUnrealEd;
    DetailsViewArgs.ViewIdentifier = FName(TEXT("LexUIPrefabEditor"));
    DetailsViewArgs.bCustomNameAreaLocation = true;
    DetailsViewArgs.bCustomFilterAreaLocation = false;
    DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Hide;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ComponentsAndActorsUseNameArea;
    DetailsViewArgs.bShowOptions = true;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.bShowObjectLabel = true;
    //DetailsViewArgs.HostCommandList = InCommandList;

    DetailsView = PropPlugin.CreateDetailView(DetailsViewArgs);
    DetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SLexUIPrefabEditorDetails::IsPropertyReadOnly));

	TSharedRef<FLexWidgetDetailPropertyExtensionHandler> BindingHandler = MakeShareable(new FLexWidgetDetailPropertyExtensionHandler(PrefabEditorPtr));
	DetailsView->SetExtensionHandler(BindingHandler);

	ComponentEditor = SNew(SLexWidgetComponentEditor)
		.GetWidgetContext(this, &SLexUIPrefabEditorDetails::GetSelectedWidgetContext)
		.CanEdit(this, &SLexUIPrefabEditorDetails::IsEditorAllowEditing)
		.OnSelectionChanged(this, &SLexUIPrefabEditorDetails::OnComponentSelectionChanged);

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
				.Visibility(this, &SLexUIPrefabEditorDetails::GetPrefabButtonVisibility)
				.IsEnabled(this, &SLexUIPrefabEditorDetails::IsPrefabButtonEnable)
				.HeightOverride(this, &SLexUIPrefabEditorDetails::GetPrefabButtonHeight)
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
							PrefabEditorPtr.Pin()->OpenSubPrefab(CachedActor.Get());
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
							PrefabEditorPtr.Pin()->SelectSubPrefab(CachedActor.Get());
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
											SAssignNew(PrefabOverrideDataViewer, SLexUIPrefabOverrideDataViewer, [=, this]()
											{
												return CachedActor.Get();
											})
											.AfterRevertPrefab_Lambda([=, this](ULexUIPrefab* PrefabAsset) {
												})
											.AfterApplyPrefab_Lambda([=, this](ULexUIPrefab* PrefabAsset){
												FLexUIEditorTools::RefreshLevelLoadedPrefab();
												FLexUIEditorTools::RefreshOnSubPrefabChange(PrefabAsset);
												FLexUIEditorTools::RefreshOpenedPrefabEditor(PrefabAsset);
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
					.Padding(FMargin(0, 2))
					[
						DetailsView.ToSharedRef()
					]
				]
			]
		];
}

SLexUIPrefabEditorDetails::~SLexUIPrefabEditorDetails()
{
}

bool SLexUIPrefabEditorDetails::IsPrefabButtonEnable()const
{
	if (PrefabEditorPtr.IsValid() && CachedActor.IsValid())
	{
		return PrefabEditorPtr.Pin()->ActorIsSubPrefabRoot(CachedActor.Get());
	}
	return false;
}

FOptionalSize SLexUIPrefabEditorDetails::GetPrefabButtonHeight()const
{
	return IsPrefabButtonEnable() ? 26 : 0;
}

EVisibility SLexUIPrefabEditorDetails::GetPrefabButtonVisibility()const
{
	return IsPrefabButtonEnable() ? EVisibility::Visible : EVisibility::Hidden;
}

bool SLexUIPrefabEditorDetails::IsEditorAllowEditing()const
{
	if (PrefabEditorPtr.IsValid() && CachedActor.IsValid())
	{
		return !PrefabEditorPtr.Pin()->ActorBelongsToSubPrefab(CachedActor.Get());
	}
	return true;
}

ULexWidget* SLexUIPrefabEditorDetails::GetSelectedWidgetContext() const
{
	if (!PrefabEditorPtr.IsValid())
	{
		return nullptr;
	}

	auto SelectedWidgets = PrefabEditorPtr.Pin()->GetSelectedWidgets();
	if (SelectedWidgets.Num() > 0 && SelectedWidgets[0].IsValid())
	{
		return SelectedWidgets[0].Get();
	}
	return nullptr;
}

void SLexUIPrefabEditorDetails::OnEditorSelectionChanged()
{
	if (bIsSelectFromDetails)return;
	bIsSelectFromLexUIEditor = true;
	auto SelectedWidgets = PrefabEditorPtr.Pin()->GetSelectedWidgets();
	if (SelectedWidgets.Num() > 0)
	{
		if (auto Widget = SelectedWidgets[0].Get())
		{
			if (Widget->GetWorld() != PrefabEditorPtr.Pin()->GetWorld())
			{
				bIsSelectFromLexUIEditor = false;
				return;
			}

			CachedActor = Widget;
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
			auto SelectedObject = SelectedWidgets[i];
			if (SelectedObject.IsValid())
			{
				if (SelectedObject->GetWorld() != PrefabEditorPtr.Pin()->GetWorld())
				{
					continue;
				}

				SelectedObjectList.Add(SelectedObject.Get());
			}
		}

		if (DetailsView)
		{
			DetailsView->SetObjects(SelectedObjectList, true);
		}
		if (SelectedObjectList.Num() == 0)
		{
			CachedActor = nullptr;
			PrefabOverrideDataViewer->RefreshDataContent();
			if (ComponentEditor)
			{
				ComponentEditor->RefreshComponents();
				ComponentEditor->ClearSelection();
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
		CachedActor = nullptr;
		PrefabOverrideDataViewer->RefreshDataContent();
		if (ComponentEditor)
		{
			ComponentEditor->RefreshComponents();
			ComponentEditor->ClearSelection();
		}
	}
	bIsSelectFromLexUIEditor = false;
}

void SLexUIPrefabEditorDetails::OnComponentSelectionChanged(const TArray<TWeakObjectPtr<ULexUIBehaviour>>& SelectedComponents)
{
	bIsSelectFromDetails = true;

	TArray<UObject*> SelectedObjects;
	TArray<ULexUIBehaviour*> ValidSelectedComponents;
	for (const TWeakObjectPtr<ULexUIBehaviour>& SelectedComponent : SelectedComponents)
	{
		if (ULexUIBehaviour* Component = SelectedComponent.Get())
		{
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
			for (const TWeakObjectPtr<ULexWidget>& SelectedWidget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
			{
				if (SelectedWidget.IsValid() && SelectedWidget->GetWorld() == PrefabEditorPtr.Pin()->GetWorld())
				{
					SelectedWidgetObjects.Add(SelectedWidget.Get());
				}
			}
			DetailsView->SetObjects(SelectedWidgetObjects, true);
		}
	}

	if (!bIsSelectFromLexUIEditor && PrefabEditorPtr.IsValid())
	{
		auto* Selection = ULexUIManagerWorldSubsystem::GetSelection(PrefabEditorPtr.Pin()->GetWorld());
		Selection->ClearComponentSelection();
		for (ULexUIBehaviour* Component : ValidSelectedComponents)
		{
			Selection->SelectComponent(Component);
		}
	}

	bIsSelectFromDetails = false;
}

bool SLexUIPrefabEditorDetails::IsPropertyReadOnly(const FPropertyAndParent& InPropertyAndParent)
{
	return false;
}

#undef LOCTEXT_NAMESPACE