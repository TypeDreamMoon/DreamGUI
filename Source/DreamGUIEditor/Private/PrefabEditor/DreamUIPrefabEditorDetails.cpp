// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIPrefabEditorDetails.h"
#include "SDreamWidgetComponentEditor.h"
#include "SDreamWidgetDetailsHeader.h"
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

namespace DreamUIPrefabDetailsLayout
{
	constexpr float ComponentsPaneFraction = 0.2f;
	constexpr float ComponentsPaneMinHeight = 200.0f;
	const FMargin PanelPadding(2, 2);
}

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
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Vertical)
			// Components: the add button sits inside the same bordered block as the list it adds to.
			+ SSplitter::Slot()
			.Resizable(true)
			.SizeRule(SSplitter::ESizeRule::FractionOfParent)
			.Value(DreamUIPrefabDetailsLayout::ComponentsPaneFraction)
			[
				SNew(SBox)
				.MinDesiredHeight(DreamUIPrefabDetailsLayout::ComponentsPaneMinHeight)
				.Padding(FMargin(0, 2))
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("SCSEditor.Background"))
					.Padding(4.f)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ComponentsPanel")))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0, 0, 0, 2))
						[
							ComponentEditor->GetToolbarWidget()
						]
						+ SVerticalBox::Slot()
						[
							ComponentEditor.ToSharedRef()
						]
					]
				]
			]
			// Properties of the selected widget or component.
			+ SSplitter::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0, 2))
				[
					SNew(SDreamWidgetDetailsHeader)
					.NameAreaWidget(DetailsView->GetNameAreaWidget())
					.CanEdit(this, &SDreamUIPrefabEditorDetails::IsEditorAllowEditing)
					.GetEditedObject_Lambda([this]() -> UObject*
					{
						const TArray<TWeakObjectPtr<UObject>>& EditedObjects = DetailsView->GetSelectedObjects();
						return EditedObjects.Num() == 1 ? EditedObjects[0].Get() : nullptr;
					})
				]
				+ SVerticalBox::Slot()
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
