// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "SDreamWidgetDesignerDetails.h"
#include "SDreamWidgetComponentEditor.h"
#include "SDreamWidgetDetailsHeader.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Modules/ModuleManager.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/NotifyHook.h"
#include "DreamWidgetBlueprintEditor.h"
#include "DetailLayoutBuilder.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "DreamWidgetDetailPropertyExtensionHandler.h"
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
#include "Animation/DreamUIDetailKeyframeHandler.h"
#include "AssetSelection.h"
#include "AssetRegistry/AssetData.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Engine/Blueprint.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "EditorClassUtils.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DreamWidgetDesignerDetails"

namespace DreamWidgetDesignerDetailsLayout
{
	constexpr float ComponentsPaneFraction = 0.2f;
	constexpr float ComponentsPaneMinHeight = 200.0f;
	const FMargin PanelPadding(2, 2);
}

void SDreamWidgetDesignerDetails::Construct(const FArguments& Args, UWorld* InWorld)
{
	World = InWorld;
	DesignerPtr = FDreamWidgetBlueprintEditor::GetEditorByWorld(World.Get());
	if (DesignerPtr.IsValid())
	{
		//if open in DesignerEditor then sync selection by DesignerEditor
		DesignerPtr.Pin()->OnSelectionChanged.AddRaw(this, &SDreamWidgetDesignerDetails::OnEditorSelectionChanged);
	}
	else
	{
		//if open in WidgetInspector then sync selection by DreamUISelection
		UDreamUISelection::GetInstance(World.Get())->OnSelectionChanged.AddRaw(this, &SDreamWidgetDesignerDetails::OnEditorSelectionChanged);
	}

    FPropertyEditorModule& PropPlugin = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
    DetailsViewArgs.bLockable = true;
    // This panel, not GUnrealEd: an edit here has to be carried from the preview onto the
    // template, and NotifyPreChange/NotifyPostChange are where that happens.
    DetailsViewArgs.NotifyHook = this;
    DetailsViewArgs.ViewIdentifier = FName(TEXT("DreamWidgetDesigner"));
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
    DetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SDreamWidgetDesignerDetails::IsPropertyReadOnly));
    // Both, and see the header for why one is not enough.
    DetailsView->SetIsCustomRowReadOnlyDelegate(FIsCustomRowReadOnly::CreateSP(this, &SDreamWidgetDesignerDetails::IsCustomRowReadOnly));

	if (DesignerPtr.IsValid())
	{
		TSharedRef<FDreamUIDetailKeyframeHandler> KeyframeHandler = MakeShareable(new FDreamUIDetailKeyframeHandler(DesignerPtr.Pin()));
		DetailsView->SetKeyframeHandler(KeyframeHandler);
	}

	TSharedRef<FDreamWidgetDetailPropertyExtensionHandler> BindingHandler = MakeShareable(new FDreamWidgetDetailPropertyExtensionHandler(World.Get()));
	DetailsView->SetExtensionHandler(BindingHandler);

	ComponentEditor = SNew(SDreamWidgetComponentEditor)
		.GetWidgetContext(this, &SDreamWidgetDesignerDetails::GetSelectedWidgetContext)
		.CanEdit(this, &SDreamWidgetDesignerDetails::IsEditorAllowEditing)
		.OnSelectionChanged(this, &SDreamWidgetDesignerDetails::OnComponentSelectionChanged);

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
			.Value(DreamWidgetDesignerDetailsLayout::ComponentsPaneFraction)
			[
				SNew(SBox)
				.MinDesiredHeight(DreamWidgetDesignerDetailsLayout::ComponentsPaneMinHeight)
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
					.CanEdit(this, &SDreamWidgetDesignerDetails::IsEditorAllowEditing)
					.GetEditedObject_Lambda([this]() -> UObject*
					{
						const TArray<TWeakObjectPtr<UObject>>& EditedObjects = DetailsView->GetSelectedObjects();
						return EditedObjects.Num() == 1 ? EditedObjects[0].Get() : nullptr;
					})
				]
				// Why half this panel is grey. A disabled row with no explanation reads as a bug in
				// the editor, and the author's next move -- open the .dui -- is not one they can
				// guess from a greyed-out spin box. The same sentence names the file, because that
				// is the thing they have to go and edit.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(4, 4))
				[
					// The same shape as the "Arranged By" banner the widget customization draws, and
					// deliberately: both say "these fields are not yours to set, and here is who owns
					// them", so they should not look like two unrelated kinds of notice.
					SNew(STextBlock)
					.Visibility(this, &SDreamWidgetDesignerDetails::GetTextAuthoredBannerVisibility)
					.Text(this, &SDreamWidgetDesignerDetails::GetTextAuthoredBannerText)
					.ColorAndOpacity(FLinearColor(1.0f, 0.78f, 0.30f))
					.AutoWrapText(true)
					.Font(IDetailLayoutBuilder::GetDetailFont())
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

SDreamWidgetDesignerDetails::~SDreamWidgetDesignerDetails()
{
	if (TSharedPtr<FDreamWidgetBlueprintEditor> DesignerEditor = DesignerPtr.Pin())
	{
		DesignerEditor->OnSelectionChanged.RemoveAll(this);
	}
	if (auto Selection = UDreamUISelection::GetInstance(World.Get()))
	{
		Selection->OnSelectionChanged.RemoveAll(this);
	}
}

void SDreamWidgetDesignerDetails::NotifyPreChange(FEditPropertyChain* PropertyAboutToChange)
{
	// The pre-change pass writes nothing; it snapshots the destination so undo has the old value.
	if (PropertyAboutToChange != nullptr)
	{
		if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = DesignerPtr.Pin())
		{
			Editor->MigrateDetailsChangeToTemplate(GetEditedObjects(), *PropertyAboutToChange, /*bIsModify*/true);
		}
	}
}

void SDreamWidgetDesignerDetails::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FEditPropertyChain* PropertyThatChanged)
{
	// Not while a slider is being dragged. Every interactive tick would otherwise copy the value
	// across and mark the asset modified; the committed change that follows carries the result.
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive || PropertyThatChanged == nullptr)
	{
		return;
	}
	TSharedPtr<FDreamWidgetBlueprintEditor> Editor = DesignerPtr.Pin();
	if (!Editor.IsValid())
	{
		return;
	}

	// The objects the EVENT names, not the ones the hierarchy selected.
	//
	// A widget's interesting properties mostly do not live on the widget: Padding and the alignments
	// belong to its UDreamPanelSlot, spacing and layout rules to its UDreamLayoutContainer, Text and
	// FontSize to its visual. Those rows reach this panel through AddExternalObjects, so they are
	// never in GetSelectedObjects -- and MigratePropertyToTemplate refuses any object the changed
	// property does not belong to, by design, because applying a chain to the wrong object asserts.
	//
	// Half the fix. The mirror also had no case for those sub-objects, so getting the right object
	// here still wrote nothing until FDreamWidgetPreviewHost learned them; see ESubObjectSite there
	// for why the two are separate. Both were silent, and both looked like the write-back's fault:
	// edit a slot's Padding, watch the preview move, find the template unchanged and the .dui
	// untouched. Only properties sitting on UDreamWidget itself ever reached the asset.
	//
	// The two things worth knowing about the event, because both were guessed wrong once:
	// PropertyNode.cpp calls this hook unconditionally, external-object rows included, and
	// TopLevelObjects is filled from FObjectBaseAddress::Object -- which for such a row IS the
	// external object. The fallback below is for callers that construct an event without them.
	TArray<UObject*> EditedObjects;
	const int32 NumEdited = PropertyChangedEvent.GetNumObjectsBeingEdited();
	EditedObjects.Reserve(NumEdited);
	for (int32 Index = 0; Index < NumEdited; ++Index)
	{
		if (UObject* Edited = const_cast<UObject*>(PropertyChangedEvent.GetObjectBeingEdited(Index)))
		{
			EditedObjects.Add(Edited);
		}
	}
	if (EditedObjects.Num() == 0)
	{
		EditedObjects = GetEditedObjects();
	}

	// Modify first, for the same reason NotifyPreChange does it: the destination has to be in the
	// transaction before it is written or the edit cannot be undone. Pre-change only ever saw the
	// SELECTION, so an external object reaching here has never been snapshotted -- and Modify inside
	// an open transaction is idempotent, so doing it again for a selected one costs nothing.
	Editor->MigrateDetailsChangeToTemplate(EditedObjects, *PropertyThatChanged, /*bIsModify*/true);
	Editor->MigrateDetailsChangeToTemplate(EditedObjects, *PropertyThatChanged, /*bIsModify*/false);
}

void SDreamWidgetDesignerDetails::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	// FNotifyHook's OTHER overload. The property grid never calls it -- PropertyNode.cpp always
	// builds a chain -- so for a long time it was dead weight whose absence nobody could observe.
	// The transform section is what made it load-bearing: it is a port of the engine's
	// FComponentTransformDetails, which has no property node and notifies through THIS overload.
	// Every rotation and scale typed into the panel arrived here, fell into the base class's empty
	// default, and the template never heard -- the preview moved, the asset kept its old transform,
	// and the .dui stayed exactly as it was. The gate never fired either, because nothing it guards
	// was ever asked to do anything.
	if (PropertyThatChanged == nullptr)
	{
		return;
	}
	// A one-link chain is not a degraded stand-in; it is the exact shape the mirror wants. The chain
	// walk copies the MEMBER the head names, whole -- and a caller with no property node is always
	// editing a member, never a leaf inside one.
	FEditPropertyChain Chain;
	Chain.AddHead(PropertyThatChanged);
	NotifyPostChange(PropertyChangedEvent, &Chain);
}

TArray<UObject*> SDreamWidgetDesignerDetails::GetEditedObjects() const
{
	TArray<UObject*> Result;
	if (!DetailsView.IsValid())
	{
		return Result;
	}
	for (const TWeakObjectPtr<UObject>& Object : DetailsView->GetSelectedObjects())
	{
		if (Object.IsValid())
		{
			Result.Add(Object.Get());
		}
	}
	return Result;
}

bool SDreamWidgetDesignerDetails::IsEditorAllowEditing()const
{
	// Structural editing, which is what this gates: the rename box in the header, and the component
	// list's add / remove / cut / paste. A `.dui` owns both -- the display name IS the node's id, and
	// a behaviour is a `+ Class { }` line -- so they are drawn disabled rather than left live to fail
	// on click. The primitives underneath refuse anyway; this is the half the author can see.
	if (IsTextAuthoredHierarchy())
	{
		return false;
	}
	//no sub prefabs: nothing else in the design is owned by another asset
	return true;
}

bool SDreamWidgetDesignerDetails::IsTextAuthoredHierarchy() const
{
	const TSharedPtr<FDreamWidgetBlueprintEditor> Designer = DesignerPtr.Pin();
	return Designer.IsValid() && DreamUITextAuthoring::IsTextAuthored(Designer->GetWidgetBlueprint());
}

EVisibility SDreamWidgetDesignerDetails::GetTextAuthoredBannerVisibility() const
{
	return IsTextAuthoredHierarchy() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SDreamWidgetDesignerDetails::GetTextAuthoredBannerText() const
{
	const TSharedPtr<FDreamWidgetBlueprintEditor> Designer = DesignerPtr.Pin();
	const FString FileName = Designer.IsValid()
		? DreamUITextAuthoring::GetAuthoredSourceFileName(Designer->GetWidgetBlueprint()) : FString();
	// Names the file, says what IS editable, and says what happens to the rest. All three, because a
	// banner that only says "read only" leaves the author with the same question they started with.
	return FText::Format(LOCTEXT("TextAuthoredBanner",
		"This hierarchy is authored in {0}. Layout, slot and style values are edited here and written back to the file; structure, names and bindings are written in the text, and edits to anything greyed out would be lost at the next compile."),
		FText::FromString(FileName));
}

UDreamWidget* SDreamWidgetDesignerDetails::GetSelectedWidgetContext() const
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

void SDreamWidgetDesignerDetails::OnEditorSelectionChanged()
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

void SDreamWidgetDesignerDetails::OnComponentSelectionChanged(const TArray<TWeakObjectPtr<UDreamUIBehaviour>>& SelectedComponents)
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
		else if (DesignerPtr.IsValid())
		{
			TArray<UObject*> SelectedWidgetObjects;
			auto RootWidget = DesignerPtr.Pin()->GetRootAgentWidget();
			for (const TWeakObjectPtr<UDreamWidget>& Widget : DesignerPtr.Pin()->GetSelectedWidgets())
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

void SDreamWidgetDesignerDetails::Refresh()
{
	if (DetailsView.IsValid())
	{
		DetailsView->ForceRefresh();
	}
}

bool SDreamWidgetDesignerDetails::IsPropertyReadOnly(const FPropertyAndParent& InPropertyAndParent)
{
	// FPropertyAndParent::Objects, never the panel's selection: the widget's visual, its panel slot
	// and its layouts are shown through AddExternalObjects, so a great many rows in this panel belong
	// to an object that is NOT what the hierarchy has selected -- and those are exactly the objects
	// the .dui can still write. Reading the selection here would have marked all of them read-only.
	for (const TWeakObjectPtr<UObject>& Object : InPropertyAndParent.Objects)
	{
		// Any, not all. A mixed selection where one widget is text-authored has to lock the row: the
		// edit would otherwise go through for the whole selection and be thrown away for one of them.
		if (Object.IsValid() && DreamUITextAuthoring::IsPropertyReadOnly(
			Object.Get(), &InPropertyAndParent.Property, InPropertyAndParent.ParentProperties))
		{
			return true;
		}
	}
	return false;
}

bool SDreamWidgetDesignerDetails::IsCustomRowReadOnly(FName InRowName, FName InCategoryName) const
{
	if (!DetailsView.IsValid())
	{
		return false;
	}
	// The selection, because a custom row hands over no objects at all -- see the header. That is why
	// this answer is per category rather than per property, and why the gate refuses by default here.
	for (const TWeakObjectPtr<UObject>& Object : DetailsView->GetSelectedObjects())
	{
		if (Object.IsValid() && DreamUITextAuthoring::IsCustomRowReadOnly(Object.Get(), InRowName, InCategoryName))
		{
			return true;
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
