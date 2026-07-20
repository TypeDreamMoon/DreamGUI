// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabEditor.h"
#include "LexUIPrefabEditorViewport.h"
#include "LexUIPrefabEditorDetails.h"
#include "LexUIPrefabRawDataViewer.h"
#include "EditorModeManager.h"
#include "GameFramework/Actor.h"
#include "AssetSelection.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Misc/FeedbackContext.h"
#include "LexUIPrefabEditorCommand.h"
#include "LexUIEditorTools.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "LexWidgetEditorHierarchyView.h"
#include "SLexUIPrefabPalette.h"
#include "LexUIPrefabBehaviourUtils.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraph/EdGraph.h"
#include "UMGStyle.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexLayoutContainerGrid.h"
#include "Framework/Commands/GenericCommands.h"
#include "PrefabSystem/LexUIPrefabInstanceScene.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PrefabAnimation/LexUIPrefabSequenceEditor.h"
#include "ScopedTransaction.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "LexUIPrefabEditor"

const FName PrefabEditorAppName = FName(TEXT("LexUIPrefabEditorApp"));

TArray<FLexUIPrefabEditor*> FLexUIPrefabEditor::PrefabEditorInstanceCollection;

struct FLexUIPrefabEditorTabs
{
	// Tab identifiers
	static const FName DetailsID;
	static const FName ViewportID;
	static const FName OutlinerID;
	static const FName PaletteID;
	static const FName SequencerID;
	static const FName PrefabRawDataViewerID;
};

const FName FLexUIPrefabEditorTabs::DetailsID(TEXT("Details"));
const FName FLexUIPrefabEditorTabs::ViewportID(TEXT("Viewport"));
const FName FLexUIPrefabEditorTabs::OutlinerID(TEXT("Outliner"));
const FName FLexUIPrefabEditorTabs::PaletteID(TEXT("Palette"));
const FName FLexUIPrefabEditorTabs::SequencerID(TEXT("Sequencer"));
const FName FLexUIPrefabEditorTabs::PrefabRawDataViewerID(TEXT("PrefabRawDataViewer"));

namespace LexUIPrefabEditorLocal
{
	enum ESaveOnApplyMode : int32
	{
		Never = 0,
		SuccessOnly = 1,
		Always = 2,
	};

	static const TCHAR* SaveOnApplySection = TEXT("LexUIPrefabEditor.Settings");
	static const TCHAR* SaveOnApplyKey = TEXT("SaveOnApply");

	static int32 GetSaveOnApplyMode()
	{
		int32 Mode = Never;
		if (GConfig)
		{
			GConfig->GetInt(SaveOnApplySection, SaveOnApplyKey, Mode, GEditorPerProjectIni);
		}
		return FMath::Clamp(Mode, static_cast<int32>(Never), static_cast<int32>(Always));
	}
}

FName GetPrefabWorldName()
{
	static uint32 NameSuffix = 0;
	return FName(*FString::Printf(TEXT("PrefabEditorWorld_%d"), NameSuffix++));
}
FLexUIPrefabEditor::FLexUIPrefabEditor()
{
	PrefabEditorInstanceCollection.Add(this);

	GEditor->RegisterForUndo(this);
}
FLexUIPrefabEditor::~FLexUIPrefabEditor()
{
	PrefabEditorInstanceCollection.Remove(this);

 	ULexUIManagerWorldSubsystem::GetInstance(GetWorld())->EventOnOutlineChanged.RemoveAll(this);
	ULexUIManagerWorldSubsystem::GetInstance(GetWorld())->bShouldTickInEditor = false;
	ULexUISelection::GetInstance(GetWorld())->OnSelectionChanged.RemoveAll(this);
	ULexUISelection::GetInstance(GetWorld())->SelectNone();

	GEditor->UnregisterForUndo(this);
}

FLexUIPrefabEditor* FLexUIPrefabEditor::GetEditorForPrefabIfValid(ULexUIPrefab* InPrefab)
{
	for (auto Instance : PrefabEditorInstanceCollection)
	{
		if (Instance->PrefabBeingEdited == InPrefab)
		{
			return Instance;
		}
	}
	return nullptr;
}

bool FLexUIPrefabEditor::WorldIsPrefabEditor(UWorld* InWorld)
{
	for (auto Instance : PrefabEditorInstanceCollection)
	{
		if (Instance->GetWorld() == InWorld)
		{
			return true;
		}
	}
	return false;
}

TWeakPtr<FLexUIPrefabEditor> FLexUIPrefabEditor::GetEditorByWorld(UWorld* InWorld)
{
	for (auto Instance : PrefabEditorInstanceCollection)
	{
		if (Instance->GetWorld() == InWorld)
		{
			return SharedThis(Instance);
		}
	}
	return nullptr;
}

bool FLexUIPrefabEditor::WidgetIsRootAgent(ULexWidget* InWidget)
{
	for (auto Instance : PrefabEditorInstanceCollection)
	{
		if (InWidget == Instance->GetPreviewScene()->GetRootAgent())
		{
			return true;
		}
	}
	return false;
}

void FLexUIPrefabEditor::IterateAllPrefabEditor(const TFunction<void(FLexUIPrefabEditor*)>& InFunction)
{
	for (auto Instance : PrefabEditorInstanceCollection)
	{
		InFunction(Instance);
	}
}

bool FLexUIPrefabEditor::RefreshOnSubPrefabDirty(ULexUIPrefab* InSubPrefab)
{
	return GetPrefabHelperObject()->RefreshOnSubPrefabDirty(InSubPrefab);
}

bool FLexUIPrefabEditor::GetSelectedObjectsBounds(FBoxSphereBounds& OutResult)
{
	FBoxSphereBounds Bounds = FBoxSphereBounds(EForceInit::ForceInitToZero);
	bool IsFirstBounds = true;
	for (auto& Widget : SelectedWidgets)
	{
		if (!Widget->GetWidgetActiveInHierarchy())
		{
			continue;
		}
		FBox LocalBounds;
		if (auto Visual = Widget->GetVisual())
		{
			FVector Min, Max;
			Visual->GetGeometryBounds3DInLocalSpace(Min, Max);
			LocalBounds = FBox(Min, Max);
		}
		else
		{
			auto Min2D = Widget->GetLocalSpaceLeftBottomPoint();
			auto Max2D = Widget->GetLocalSpaceRightTopPoint();
			LocalBounds = FBox(FVector(0, Min2D.X, Min2D.Y), FVector(0, Max2D.X, Max2D.Y));
		}
		auto Box = LocalBounds.TransformBy(Widget->GetWorldTransform());
		if (IsFirstBounds)
		{
			IsFirstBounds = false;
			Bounds = Box;
		}
		else
		{
			Bounds = Bounds + Box;
		}
	}
	OutResult = Bounds;
	return IsFirstBounds == false;
}

FBoxSphereBounds FLexUIPrefabEditor::GetAllObjectsBounds()
{
	FBoxSphereBounds Bounds;
	bool IsFirstBounds = true;
	for (auto& KeyValue : GetPrefabHelperObject()->MapGuidToObject)
	{
		FBox Box; bool bIsValidBox = false;
		if (auto Widget = Cast<ULexWidget>(KeyValue.Value))
		{
			if (Widget->GetWidgetActiveInHierarchy())
			{
				FBox LocalBounds;
				if (auto Visual = Widget->GetVisual())
				{
					FVector Min, Max;
					Visual->GetGeometryBounds3DInLocalSpace(Min, Max);
					LocalBounds = FBox(Min, Max);
				}
				else
				{
					auto Min2D = Widget->GetLocalSpaceLeftBottomPoint();
					auto Max2D = Widget->GetLocalSpaceRightTopPoint();
					LocalBounds = FBox(FVector(0, Min2D.X, Min2D.Y), FVector(0, Max2D.X, Max2D.Y));
				}
				Box = LocalBounds.TransformBy(Widget->GetWorldTransform());
				bIsValidBox = true;
			}
		}

		if (bIsValidBox)
		{
			if (IsFirstBounds)
			{
				IsFirstBounds = false;
				Bounds = Box;
			}
			else
			{
				Bounds = Bounds + Box;
			}
		}
	}
	return Bounds;
}

bool FLexUIPrefabEditor::WidgetBelongsToSubPrefab(ULexWidget* InWidget)
{
	return GetPrefabHelperObject()->IsWidgetBelongsToSubPrefab(InWidget);
}

bool FLexUIPrefabEditor::WidgetIsSubPrefabRoot(ULexWidget* InSubPrefabRootWidget)
{
	return GetPrefabHelperObject()->SubPrefabMap.Contains(InSubPrefabRootWidget);
}

FLexUISubPrefabData FLexUIPrefabEditor::GetSubPrefabDataForActor(ULexWidget* InSubPrefabWidget)
{
	return GetPrefabHelperObject()->GetSubPrefabData(InSubPrefabWidget);
}

void FLexUIPrefabEditor::OpenSubPrefab(ULexWidget* InSubPrefabWidget)
{
	if (auto SubPrefabAsset = GetPrefabHelperObject()->GetSubPrefabAsset(InSubPrefabWidget))
	{
		auto PrefabEditor = FLexUIPrefabEditor::GetEditorForPrefabIfValid(SubPrefabAsset);
		if (!PrefabEditor)
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			AssetEditorSubsystem->OpenEditorForAsset(SubPrefabAsset);
		}
	}
}
void FLexUIPrefabEditor::SelectSubPrefab(ULexWidget* InSubPrefabWidget)
{
	if (auto SubPrefabAsset = GetPrefabHelperObject()->GetSubPrefabAsset(InSubPrefabWidget))
	{
		TArray<UObject*> ObjectsToSync;
		ObjectsToSync.Add(SubPrefabAsset);
		GEditor->SyncBrowserToObjects(ObjectsToSync);
	}
}

bool FLexUIPrefabEditor::GetAnythingDirty()const 
{ 
	return GetPrefabHelperObject()->GetAnythingDirty();
}

namespace
{
	void SyncWidgetRegisterStateAfterTransaction(FLexUIPrefabEditor* InEditor)
	{
		InEditor->GetPreviewScene()->GetRootAgent()->EnsureChildrenAfterTransaction();
		TArray<ULexWidget*> ReachableWidgets;
		ULexWidget::CollectChildrenWidgets(InEditor->GetPreviewScene()->GetRootAgent(), ReachableWidgets);

		TSet<ULexWidget*> AllWidgets;
		ForEachObjectOfClass(ULexWidget::StaticClass(), [&](UObject* Object)
		{
			if (auto Widget = Cast<ULexWidget>(Object))
			{
				if (Widget->GetWorld() == InEditor->GetWorld())
				{
					AllWidgets.Add(Widget);
				}
			}
		});
		for (auto Widget : AllWidgets)
		{
			if (!ReachableWidgets.Contains(Widget))
			{
				if (Widget->HasRegistered())
				{
					Widget->OnUnregister();
				}
			}
		}
	}
}

void FLexUIPrefabEditor::SyncSelection()
{
	SelectedWidgets = ULexUISelection::GetInstance(GetWorld())->GetSelectedWidgets();
	OnSelectionChanged.Broadcast();
	OutlinerPtr->RequestRefresh();
}

void FLexUIPrefabEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_LexUIPrefabEditor", "LexUIPrefab Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(FLexUIPrefabEditorTabs::ViewportID, FOnSpawnTab::CreateSP(this, &FLexUIPrefabEditor::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(FLexUIPrefabEditorTabs::DetailsID, FOnSpawnTab::CreateSP(this, &FLexUIPrefabEditor::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTabLabel", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(FLexUIPrefabEditorTabs::OutlinerID, FOnSpawnTab::CreateSP(this, &FLexUIPrefabEditor::SpawnTab_Outliner))
		.SetDisplayName(LOCTEXT("OutlinerTabLabel", "Outliner"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));

	InTabManager->RegisterTabSpawner(FLexUIPrefabEditorTabs::PaletteID, FOnSpawnTab::CreateSP(this, &FLexUIPrefabEditor::SpawnTab_Palette))
		.SetDisplayName(LOCTEXT("PaletteTabLabel", "Palette"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Kismet.Tabs.Palette"));

	InTabManager->RegisterTabSpawner(FLexUIPrefabEditorTabs::SequencerID, FOnSpawnTab::CreateSP(this, &FLexUIPrefabEditor::SpawnTab_Sequencer))
		.SetDisplayName(LOCTEXT("SequencerTabLabel", "Animations"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FUMGStyle::GetStyleSetName(), "Animations.TabIcon"));

	InTabManager->RegisterTabSpawner(FLexUIPrefabEditorTabs::PrefabRawDataViewerID, FOnSpawnTab::CreateSP(this, &FLexUIPrefabEditor::SpawnTab_PrefabRawDataViewer))
		.SetDisplayName(LOCTEXT("PrefabRawDataViewerTabLabel", "PrefabRawDataViewer"))
		.SetGroup(WorkspaceMenuCategoryRef)
		;
}
void FLexUIPrefabEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(FLexUIPrefabEditorTabs::ViewportID);
	InTabManager->UnregisterTabSpawner(FLexUIPrefabEditorTabs::DetailsID);
	InTabManager->UnregisterTabSpawner(FLexUIPrefabEditorTabs::OutlinerID);
	InTabManager->UnregisterTabSpawner(FLexUIPrefabEditorTabs::PaletteID);
	InTabManager->UnregisterTabSpawner(FLexUIPrefabEditorTabs::SequencerID);
	InTabManager->UnregisterTabSpawner(FLexUIPrefabEditorTabs::PrefabRawDataViewerID);
}

void FLexUIPrefabEditor::PostUndo(bool bSuccess)
{
	SyncWidgetRegisterStateAfterTransaction(this);
	ApplyDesignerState();
	ULexUIManagerWorldSubsystem::RefreshAllUI();
	ULexUIManagerWorldSubsystem::GetInstance(GetWorld())->EventOnOutlineChanged.Broadcast();
	SelectedWidgets = ULexUISelection::GetInstance(GetWorld())->GetSelectedWidgets();
	OnSelectionChanged.Broadcast();
	OutlinerPtr->RequestRefresh();
}
void FLexUIPrefabEditor::PostRedo(bool bSuccess)
{
	SyncWidgetRegisterStateAfterTransaction(this);
	ApplyDesignerState();
	ULexUIManagerWorldSubsystem::RefreshAllUI();
	SelectedWidgets = ULexUISelection::GetInstance(GetWorld())->GetSelectedWidgets();
	OnSelectionChanged.Broadcast();
	OutlinerPtr->RequestRefresh();
}

void FLexUIPrefabEditor::InitPrefabEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost >& InitToolkitHost, ULexUIPrefab* InPrefab)
{
	PrefabBeingEdited = InPrefab;
	if (PrefabBeingEdited->ReferenceClassList.Contains(nullptr))
	{
		auto MsgText = LOCTEXT("Error_PrefabMissingReferenceClass", "Prefab missing some class reference!");
		FMessageDialog::Open(EAppMsgType::Ok, MsgText);
	}
	if (PrefabBeingEdited->ReferenceAssetList.Contains(nullptr))
	{
		auto MsgText = LOCTEXT("Error_PrefabMissingReferenceAsset", "Prefab missing some asset reference!");
		FMessageDialog::Open(EAppMsgType::Ok, MsgText);
	}

	FLexUIPrefabEditorCommand::Register();

	PrefabBeingEdited->EnsureInstanceObjects();

	TSharedPtr<FLexUIPrefabEditor> PrefabEditorPtr = SharedThis(this);

	ViewportPtr = SNew(SLexUIPrefabEditorViewport, PrefabEditorPtr, PrefabBeingEdited->PrefabDataForPrefabEditor.ViewMode);
	
	DetailsPtr = SNew(SLexUIPrefabEditorDetails, GetWorld());

	PrefabRawDataViewer = SNew(SLexUIPrefabRawDataViewer, PrefabEditorPtr, PrefabBeingEdited);
	
	ULexUIManagerWorldSubsystem::GetInstance(GetWorld())->EventOnOutlineChanged.AddSPLambda(this, [=, this]()
	{
		OutlinerPtr->RequestRefresh();
	});
	ULexUIManagerWorldSubsystem::GetInstance(GetWorld())->bShouldTickInEditor = true;
	ULexUISelection::GetInstance(GetWorld())->OnSelectionChanged.AddSPLambda(this, [=, this]
	{
		SyncSelection();
	});
	
	OutlinerPtr = SNew(SLexWidgetEditorHierarchyView, GetWorld());
	PalettePtr = SNew(SLexUIPrefabPalette, SharedThis(this));
	if (ULexWidget* RootWidget = GetLoadedRootWidget())
	{
		const int32 RenameCount = FLexUIEditorTools::EnsureUniqueWidgetDisplayNames(RootWidget);
		if (RenameCount > 0)
		{
			FNotificationInfo Info(FText::Format(
				LOCTEXT("UniqueWidgetNamesOnOpen", "Renamed {0} duplicate widget name(s) using UMG-style numeric suffixes. Apply to save the migration."),
				FText::AsNumber(RenameCount)));
			Info.ExpireDuration = 6.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}
	ApplyDesignerState();

	SequencerPtr = SNew(SLexUIPrefabSequenceEditor);
	
	BindCommands();
	ExtendToolbar();

	// Default layout
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_LexUIPrefabEditor_Layout_v2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.85f)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Horizontal)
					->SetSizeCoefficient(0.9f)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.2f)
						->AddTab(FLexUIPrefabEditorTabs::OutlinerID, ETabState::OpenedTab)
						->AddTab(FLexUIPrefabEditorTabs::PaletteID, ETabState::OpenedTab)
						->SetForegroundTab(FLexUIPrefabEditorTabs::OutlinerID)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.6f)
						->AddTab(FLexUIPrefabEditorTabs::ViewportID, ETabState::OpenedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.2f)
						->AddTab(FLexUIPrefabEditorTabs::DetailsID, ETabState::OpenedTab)
					)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.3f)
					->SetForegroundTab(FLexUIPrefabEditorTabs::SequencerID)
					->AddTab(FLexUIPrefabEditorTabs::SequencerID, ETabState::OpenedTab)
				)
			)
		);

	InitAssetEditor(Mode, InitToolkitHost, PrefabEditorAppName, StandaloneDefaultLayout, true, true, PrefabBeingEdited);

	// After opening a prefab, broadcast event to LexUIPrefabSequencerEditor
	FLexUIEditorTools::OnEditingPrefabChanged.Broadcast(GetPreviewScene()->GetRootAgent());
}

void FLexUIPrefabEditor::GetInitialViewSetting(FVector& OutLocation, FRotator& OutRotation, FVector& OutOrbitLocation, ELevelViewportType& OutViewType)
{
	auto& PrefabEditorData = PrefabBeingEdited->PrefabDataForPrefabEditor;
	auto SceneBounds = this->GetAllObjectsBounds();
	if (PrefabEditorData.ViewLocation == FVector::ZeroVector && PrefabEditorData.ViewRotation == FRotator::ZeroRotator)
	{
		OutLocation = FVector(-SceneBounds.SphereRadius * 1.2f, SceneBounds.Origin.Y, SceneBounds.Origin.Z);
		OutRotation = FRotator::ZeroRotator;
	}
	else
	{
		OutLocation = PrefabEditorData.ViewLocation;
		OutRotation = PrefabEditorData.ViewRotation;
	}
	if (PrefabEditorData.ViewOrbitLocation == FVector::ZeroVector)
	{
		OutOrbitLocation = SceneBounds.Origin;
	}
	else
	{
		OutOrbitLocation = PrefabEditorData.ViewOrbitLocation;
	}
	OutViewType = (ELevelViewportType)PrefabEditorData.ViewportType;
}

ULexWidget* FLexUIPrefabEditor::GetRootAgentWidget()
{
	return GetPreviewScene()->GetRootAgent();
}

ULexWidget* FLexUIPrefabEditor::GetLoadedRootWidget()
{
	return GetPrefabHelperObject()->LoadedRootWidget;
}

FName FLexUIPrefabEditor::GetSequencerTabID()
{
	return FLexUIPrefabEditorTabs::SequencerID;
}

void FLexUIPrefabEditor::SaveAsset_Execute()
{
	ApplyPrefabChanges();
	if (bLastApplySerializationSucceeded)
	{
		SaveAppliedPrefabToDisk();
	}
}

void FLexUIPrefabEditor::SaveAppliedPrefabToDisk()
{
	SaveEditorState();
	FAssetEditorToolkit::SaveAsset_Execute();
	FLexUIEditorTools::RefreshLoadedPrefab();
	FLexUIEditorTools::RefreshOnSubPrefabChange(GetPrefabHelperObject()->PrefabAsset);
}

void FLexUIPrefabEditor::OnOpenRawDataViewerPanel()
{
	this->InvokeTab(FLexUIPrefabEditorTabs::PrefabRawDataViewerID);
}
void FLexUIPrefabEditor::OnOpenPrefabHelperObjectDetailsPanel()
{
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	AssetEditorSubsystem->OpenEditorForAsset(GetPrefabHelperObject());
}

UBlueprint* FLexUIPrefabEditor::GetOrCreateBehaviourBlueprint()
{
	ULexWidget* RootWidget = GetLoadedRootWidget();
	if (RootWidget == nullptr || PrefabBeingEdited == nullptr)return nullptr;

	UBlueprint* Blueprint = LexUIPrefabBehaviourUtils::FindBehaviourBlueprint(RootWidget, PrefabBeingEdited);
	if (Blueprint == nullptr)
	{
		Blueprint = LexUIPrefabBehaviourUtils::CreateBehaviourBlueprint(PrefabBeingEdited, RootWidget);
		if (Blueprint == nullptr)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("Error_CreateBehaviourBlueprint", "Failed to create the behaviour blueprint."));
			return nullptr;
		}
		// the attached script is part of the prefab now
		if (auto Helper = GetPrefabHelperObject())
		{
			Helper->Modify();
			Helper->SetAnythingDirty();
		}
		FNotificationInfo Info(FText::Format(LOCTEXT("BehaviourBlueprintCreated", "Created {0} and attached it to the prefab root widget.")
			, FText::FromString(Blueprint->GetName())));
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	return Blueprint;
}

void FLexUIPrefabEditor::CreateOrOpenBehaviourBlueprint()
{
	if (UBlueprint* Blueprint = GetOrCreateBehaviourBlueprint())
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
	}
}

void FLexUIPrefabEditor::PromoteToBehaviourVariable(UObject* InTarget)
{
	if (InTarget == nullptr)return;
	UBlueprint* Blueprint = GetOrCreateBehaviourBlueprint();
	if (Blueprint == nullptr)return;
	ULexWidget* RootWidget = GetLoadedRootWidget();

	const FString VariableName = LexUIPrefabBehaviourUtils::MakeVariableNameForTarget(InTarget);
	FText Message;
	const bool bSuccess = LexUIPrefabBehaviourUtils::PromoteToVariable(Blueprint, RootWidget, InTarget, VariableName, Message);
	if (bSuccess)
	{
		if (auto Helper = GetPrefabHelperObject())
		{
			Helper->Modify();
			Helper->SetAnythingDirty();
		}
	}
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 5.0f;
	if (!bSuccess)
	{
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
	}
	FSlateNotificationManager::Get().AddNotification(Info);
}

void FLexUIPrefabEditor::AddEventHandler(const LexUIPrefabBehaviourUtils::FDiscoveredEvent& InEvent)
{
	UBlueprint* Blueprint = GetOrCreateBehaviourBlueprint();
	if (Blueprint == nullptr)return;
	ULexWidget* RootWidget = GetLoadedRootWidget();

	FText Message;
	const FName HandlerName = LexUIPrefabBehaviourUtils::AddEventHandler(Blueprint, RootWidget, InEvent, Message);
	const bool bSuccess = !HandlerName.IsNone();
	if (bSuccess)
	{
		if (auto Helper = GetPrefabHelperObject())
		{
			Helper->Modify();
			Helper->SetAnythingDirty();
		}
	}
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 5.0f;
	if (!bSuccess)
	{
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
	}
	FSlateNotificationManager::Get().AddNotification(Info);

	if (bSuccess)
	{
		// open the blueprint at the generated function graph, UMG "+" style
		if (UEdGraph* FuncGraph = FindObject<UEdGraph>(Blueprint, *HandlerName.ToString()))
		{
			FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(FuncGraph, false);
		}
		else
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
		}
	}
}

namespace
{
	// A widget's axis-aligned bounds in ITS PARENT's frame. LGUI's UI plane is YZ
	// (RelativeLocation.Y = horizontal, .Z = vertical, .X = the plane normal), confirmed by
	// ULexWidget::CalculateAnchorFromTransform. Local-space edges are pushed through the
	// widget's local transform so per-widget scale/rotation is accounted for (AABB via min/max).
	struct FParentSpaceRect
	{
		double Left = 0, Right = 0, Bottom = 0, Top = 0;
		double CenterH() const { return (Left + Right) * 0.5; }
		double CenterV() const { return (Bottom + Top) * 0.5; }
	};
	FParentSpaceRect GetParentSpaceRect(ULexWidget* W)
	{
		const FTransform LocalTM = W->GetLocalTransform();
		const double LocalLeft = W->GetLocalSpaceLeft();
		const double LocalRight = W->GetLocalSpaceRight();
		const double LocalBottom = W->GetLocalSpaceBottom();
		const double LocalTop = W->GetLocalSpaceTop();
		//local (normal, horizontal, vertical) = (X, Y, Z)
		const FVector Corners[4] =
		{
			LocalTM.TransformPosition(FVector(0, LocalLeft,  LocalBottom)),
			LocalTM.TransformPosition(FVector(0, LocalRight, LocalBottom)),
			LocalTM.TransformPosition(FVector(0, LocalLeft,  LocalTop)),
			LocalTM.TransformPosition(FVector(0, LocalRight, LocalTop)),
		};
		FParentSpaceRect R;
		R.Left = R.Right = Corners[0].Y;
		R.Bottom = R.Top = Corners[0].Z;
		for (const FVector& C : Corners)
		{
			R.Left = FMath::Min(R.Left, C.Y);
			R.Right = FMath::Max(R.Right, C.Y);
			R.Bottom = FMath::Min(R.Bottom, C.Z);
			R.Top = FMath::Max(R.Top, C.Z);
		}
		return R;
	}
	// Selected widgets that share a parent. Returns false (and warns) on cross-parent selections;
	// parentless widgets (e.g. the prefab root) are dropped. When bRefuseLayoutParent, a shared
	// parent that owns a layout container is refused too: it drives its children's positions and
	// rebuilds on any SetAnchoredPosition, so an align/distribute would be silently overwritten
	// (wrapping is fine there, so that caller passes false). OutParent gets the shared parent.
	bool GatherSharedParentSelection(const TArray<TWeakObjectPtr<ULexWidget>>& InSelection, int32 InMinCount, bool bRefuseLayoutParent, TArray<ULexWidget*>& OutWidgets, ULexWidget*& OutParent)
	{
		OutWidgets.Reset();
		OutParent = nullptr;
		ULexWidget* CommonParent = nullptr;
		bool bParentSet = false;
		for (const TWeakObjectPtr<ULexWidget>& Weak : InSelection)
		{
			ULexWidget* W = Weak.Get();
			if (W == nullptr) continue;
			ULexWidget* Parent = W->GetParent();
			if (Parent == nullptr) continue;//root / detached: has no sibling frame
			if (!bParentSet) { CommonParent = Parent; bParentSet = true; }
			else if (Parent != CommonParent)
			{
				FNotificationInfo Info(NSLOCTEXT("LexUIPrefabEditor", "SharedParentRequired", "This action needs the selected widgets to share a parent."));
				Info.ExpireDuration = 4.0f;
				Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
				FSlateNotificationManager::Get().AddNotification(Info);
				OutWidgets.Reset();
				return false;
			}
			OutWidgets.Add(W);
		}
		if (bRefuseLayoutParent && bParentSet && CommonParent != nullptr && CommonParent->GetLayoutContainer() != nullptr)
		{
			FNotificationInfo Info(NSLOCTEXT("LexUIPrefabEditor", "AlignLayoutParent", "The shared parent has a layout container that positions its children -- align/distribute would be overridden by the layout."));
			Info.ExpireDuration = 5.0f;
			Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
			FSlateNotificationManager::Get().AddNotification(Info);
			OutWidgets.Reset();
			return false;
		}
		OutParent = CommonParent;
		return OutWidgets.Num() >= InMinCount;
	}
}

void FLexUIPrefabEditor::AlignSelectedWidgets(ELexUIWidgetAlignType AlignType)
{
	TArray<ULexWidget*> Widgets;
	ULexWidget* CommonParent = nullptr;
	if (!GatherSharedParentSelection(GetSelectedWidgets(), 2, /*bRefuseLayoutParent*/true, Widgets, CommonParent)) return;

	// Selection bound in the shared parent's frame.
	TArray<FParentSpaceRect> Rects;
	Rects.Reserve(Widgets.Num());
	double GroupLeft = TNumericLimits<double>::Max(), GroupRight = TNumericLimits<double>::Lowest();
	double GroupBottom = TNumericLimits<double>::Max(), GroupTop = TNumericLimits<double>::Lowest();
	for (ULexWidget* W : Widgets)
	{
		const FParentSpaceRect R = GetParentSpaceRect(W);
		Rects.Add(R);
		GroupLeft = FMath::Min(GroupLeft, R.Left);
		GroupRight = FMath::Max(GroupRight, R.Right);
		GroupBottom = FMath::Min(GroupBottom, R.Bottom);
		GroupTop = FMath::Max(GroupTop, R.Top);
	}
	const double GroupCenterH = (GroupLeft + GroupRight) * 0.5;
	const double GroupCenterV = (GroupBottom + GroupTop) * 0.5;

	const FScopedTransaction Transaction(NSLOCTEXT("LexUIPrefabEditor", "AlignWidgets", "Align Widgets"));
	for (int32 i = 0; i < Widgets.Num(); i++)
	{
		ULexWidget* W = Widgets[i];
		const FParentSpaceRect& R = Rects[i];
		//parent-frame delta applies straight to anchoredPosition (X=horizontal, Y=vertical), same as the arrow-key nudge
		FVector2D AnchoredPos = W->GetAnchoredPosition();
		switch (AlignType)
		{
		case ELexUIWidgetAlignType::LeftEdge:         AnchoredPos.X += GroupLeft - R.Left; break;
		case ELexUIWidgetAlignType::RightEdge:        AnchoredPos.X += GroupRight - R.Right; break;
		case ELexUIWidgetAlignType::HorizontalCenter: AnchoredPos.X += GroupCenterH - R.CenterH(); break;
		case ELexUIWidgetAlignType::TopEdge:          AnchoredPos.Y += GroupTop - R.Top; break;
		case ELexUIWidgetAlignType::BottomEdge:       AnchoredPos.Y += GroupBottom - R.Bottom; break;
		case ELexUIWidgetAlignType::VerticalCenter:   AnchoredPos.Y += GroupCenterV - R.CenterV(); break;
		}
		W->Modify();
		W->SetAnchoredPosition(AnchoredPos);
	}

	if (auto Helper = GetPrefabHelperObject())
	{
		Helper->Modify();
		Helper->SetAnythingDirty();
	}
	//menu action (not a drag): repaint now so it shows even when the preview realtime is off
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid()) ViewportPtr->GetViewportClient()->Invalidate();
}

void FLexUIPrefabEditor::DistributeSelectedWidgets(bool bHorizontal)
{
	TArray<ULexWidget*> Widgets;
	ULexWidget* CommonParent = nullptr;
	if (!GatherSharedParentSelection(GetSelectedWidgets(), 3, /*bRefuseLayoutParent*/true, Widgets, CommonParent)) return;

	// Pair each widget with its parent-frame rect and sort along the distribute axis by low edge.
	struct FEntry { ULexWidget* Widget; FParentSpaceRect Rect; };
	TArray<FEntry> Entries;
	Entries.Reserve(Widgets.Num());
	for (ULexWidget* W : Widgets) { Entries.Add({ W, GetParentSpaceRect(W) }); }
	auto LowEdge = [bHorizontal](const FParentSpaceRect& R) { return bHorizontal ? R.Left : R.Bottom; };
	auto HighEdge = [bHorizontal](const FParentSpaceRect& R) { return bHorizontal ? R.Right : R.Top; };
	Entries.Sort([&](const FEntry& A, const FEntry& B) { return LowEdge(A.Rect) < LowEdge(B.Rect); });

	// Equal-gap distribution: the two outermost widgets stay put; the rest are spaced so every
	// adjacent gap is identical. gap = (span - sum of sizes) / (count - 1).
	const double SpanLow = LowEdge(Entries[0].Rect);
	const double SpanHigh = HighEdge(Entries.Last().Rect);
	double SizeSum = 0;
	for (const FEntry& E : Entries) { SizeSum += HighEdge(E.Rect) - LowEdge(E.Rect); }
	const double Gap = (SpanHigh - SpanLow - SizeSum) / (Entries.Num() - 1);

	const FScopedTransaction Transaction(NSLOCTEXT("LexUIPrefabEditor", "DistributeWidgets", "Distribute Widgets"));
	double Cursor = HighEdge(Entries[0].Rect);//trailing edge of the fixed first widget
	for (int32 i = 1; i < Entries.Num() - 1; i++)
	{
		const double TargetLow = Cursor + Gap;
		const double Delta = TargetLow - LowEdge(Entries[i].Rect);
		ULexWidget* W = Entries[i].Widget;
		FVector2D AnchoredPos = W->GetAnchoredPosition();
		if (bHorizontal) AnchoredPos.X += Delta; else AnchoredPos.Y += Delta;
		W->Modify();
		W->SetAnchoredPosition(AnchoredPos);
		Cursor = TargetLow + (HighEdge(Entries[i].Rect) - LowEdge(Entries[i].Rect));
	}

	if (auto Helper = GetPrefabHelperObject())
	{
		Helper->Modify();
		Helper->SetAnythingDirty();
	}
	//menu action (not a drag): repaint now so it shows even when the preview realtime is off
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid()) ViewportPtr->GetViewportClient()->Invalidate();
}

void FLexUIPrefabEditor::WrapSelectedWidgets(ELexUIWrapType WrapType)
{
	TArray<ULexWidget*> Widgets;
	ULexWidget* CommonParent = nullptr;
	// wrapping is fine under a layout parent (the wrapper just becomes a layout child), so don't refuse it
	if (!GatherSharedParentSelection(GetSelectedWidgets(), 1, /*bRefuseLayoutParent*/false, Widgets, CommonParent)) return;
	if (CommonParent == nullptr) return;

	// Selection bound in the shared parent's frame, and the lowest sibling slot to drop the wrapper into.
	double GroupLeft = TNumericLimits<double>::Max(), GroupRight = TNumericLimits<double>::Lowest();
	double GroupBottom = TNumericLimits<double>::Max(), GroupTop = TNumericLimits<double>::Lowest();
	for (ULexWidget* W : Widgets)
	{
		const FParentSpaceRect R = GetParentSpaceRect(W);
		GroupLeft = FMath::Min(GroupLeft, R.Left);
		GroupRight = FMath::Max(GroupRight, R.Right);
		GroupBottom = FMath::Min(GroupBottom, R.Bottom);
		GroupTop = FMath::Max(GroupTop, R.Top);
	}
	const double GroupCenterH = (GroupLeft + GroupRight) * 0.5;
	const double GroupCenterV = (GroupBottom + GroupTop) * 0.5;
	int32 MinSiblingIndex = TNumericLimits<int32>::Max();
	{
		const TArray<ULexWidget*>& Siblings = CommonParent->GetChildren();
		for (ULexWidget* W : Widgets)
		{
			const int32 Idx = Siblings.IndexOfByKey(W);
			if (Idx != INDEX_NONE) MinSiblingIndex = FMath::Min(MinSiblingIndex, Idx);
		}
	}
	if (MinSiblingIndex == TNumericLimits<int32>::Max()) MinSiblingIndex = -1;

	const FScopedTransaction Transaction(NSLOCTEXT("LexUIPrefabEditor", "WrapWidgets", "Wrap Widgets"));
	if (auto Helper = GetPrefabHelperObject())
	{
		Helper->Modify();
		Helper->SetAnythingDirty();
	}
	CommonParent->Modify();

	// New container, inserted where the selection was, sized to enclose it.
	FString WrapperName;
	switch (WrapType)
	{
	case ELexUIWrapType::HorizontalBox: WrapperName = TEXT("HorizontalBox"); break;
	case ELexUIWrapType::VerticalBox:   WrapperName = TEXT("VerticalBox"); break;
	case ELexUIWrapType::Grid:          WrapperName = TEXT("Grid"); break;
	default:                            WrapperName = TEXT("Widget"); break;
	}
	ULexWidget* Wrapper = NewObject<ULexWidget>(CommonParent->GetOuter(), ULexWidget::StaticClass(), NAME_None, RF_Public | RF_Transactional);
	Wrapper->Modify();//enroll the new widget in the transaction so undo/redo restores it (and re-registers it), like DeleteWidgets
	Wrapper->SetDisplayName(FLexUIEditorTools::MakeUniqueWidgetDisplayName(CommonParent, WrapperName));
	Wrapper->OnRegister();
	Wrapper->SetParent(CommonParent, false, MinSiblingIndex);
	Wrapper->SetPivot(FVector2D(0.5f, 0.5f));
	Wrapper->SetWidth(GroupRight - GroupLeft);
	Wrapper->SetHeight(GroupTop - GroupBottom);
	// move the wrapper's parent-frame center onto the selection center (same proven basis as align)
	{
		const FParentSpaceRect WrapperRect = GetParentSpaceRect(Wrapper);
		FVector2D AnchoredPos = Wrapper->GetAnchoredPosition();
		AnchoredPos.X += GroupCenterH - WrapperRect.CenterH();
		AnchoredPos.Y += GroupCenterV - WrapperRect.CenterV();
		Wrapper->SetAnchoredPosition(AnchoredPos);
	}

	// Reparent the selection under the wrapper, keeping world position so nothing visually jumps.
	// A layout container (added next) then arranges them.
	for (ULexWidget* W : Widgets)
	{
		// keepWorld preserves the pivot transform but NOT the anchor-driven size: a stretch-anchored
		// child (AnchorMin != AnchorMax) resolves its width/height against the parent, so moving it
		// under the differently-sized wrapper would resize it. Snapshot the rendered extent and
		// restore it after reparenting (a no-op for fixed-anchor children, whose size is parent-independent).
		const float OldWidth = W->GetWidth();
		const float OldHeight = W->GetHeight();
		W->Modify();
		W->SetParent(Wrapper, true);
		W->SetWidth(OldWidth);
		W->SetHeight(OldHeight);
	}

	switch (WrapType)
	{
	case ELexUIWrapType::HorizontalBox:
		if (auto FlexBox = Wrapper->CreateNewLayoutContainer<ULexLayoutContainerFlexBox>()) FlexBox->SetDirection(ELexLayoutFlexBoxDirectionType::Horizontal);
		break;
	case ELexUIWrapType::VerticalBox:
		if (auto FlexBox = Wrapper->CreateNewLayoutContainer<ULexLayoutContainerFlexBox>()) FlexBox->SetDirection(ELexLayoutFlexBoxDirectionType::Vertical);
		break;
	case ELexUIWrapType::Grid:
		Wrapper->CreateNewLayoutContainer<ULexLayoutContainerGrid>();
		break;
	default:
		break;//plain widget container
	}

	SelectWidgets(TSet<ULexWidget*>{ Wrapper }, false);

	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid()) ViewportPtr->GetViewportClient()->Invalidate();
}

void FLexUIPrefabEditor::SaveEditorState()
{
	//save view location and rotation
	auto ViewTransform = ViewportPtr->GetViewportClient()->GetViewTransform();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewLocation = ViewTransform.GetLocation();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewRotation = ViewTransform.GetRotation();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewOrbitLocation = ViewTransform.GetLookAt();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewportType = ViewportPtr->GetViewportClient()->GetViewportType();
	auto RootAgentWidget = GetPreviewScene()->GetRootAgent();
	PrefabBeingEdited->PrefabDataForPrefabEditor.CanvasSize = FIntPoint(RootAgentWidget->GetWidth(), RootAgentWidget->GetHeight());
	auto RootCanvas = RootAgentWidget->GetComponent<ULexCanvas>();
	PrefabBeingEdited->PrefabDataForPrefabEditor.CanvasRenderMode = (uint8)RootCanvas->GetRenderMode();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewMode = ViewportPtr->GetViewportClient()->GetViewMode();

	TSet<TWeakObjectPtr<ULexWidget>> ExpandWidgetSet;
	OutlinerPtr->GetExpandWidgets(ExpandWidgetSet);
	TSet<FGuid> UnexpandWidgetGuidArray;
	for (auto& KeyValue : GetPrefabHelperObject()->MapGuidToObject)
	{
		if (auto Widget = Cast<ULexWidget>(KeyValue.Value))
		{
			if (!ExpandWidgetSet.Contains(Widget))
			{
				UnexpandWidgetGuidArray.Add(KeyValue.Key);
			}
		}
	}
	PrefabBeingEdited->PrefabDataForPrefabEditor.UnexpandedWidgetSet = UnexpandWidgetGuidArray;
	PrefabBeingEdited->bThumbnailDirty = true;

	//refresh parameter, remove invalid
	for (auto& KeyValue : GetPrefabHelperObject()->SubPrefabMap)
	{
		KeyValue.Value.CheckParameters();
	}
}

bool FLexUIPrefabEditor::ApplyPrefabChanges()
{
	ULexUIPrefab* Prefab = GetPrefabBeingEdited();
	ULexUIPrefabHelperObject* Helper = IsValid(Prefab) ? Prefab->GetPrefabHelperObject() : nullptr;
	if (!IsValid(Helper) || !IsValid(Helper->LoadedRootWidget))
	{
		bLastApplyHadProblems = true;
		bLastApplySerializationSucceeded = false;
		return false;
	}

	bLastApplyHadProblems = false;
	bLastApplySerializationSucceeded = false;
	FLexUIEditorTools::OnBeforeApplyPrefab.Broadcast(Helper);
	if (ULexWidget* RootWidget = GetLoadedRootWidget())
	{
		const int32 RenameCount = FLexUIEditorTools::EnsureUniqueWidgetDisplayNames(RootWidget);
		if (RenameCount > 0)
		{
			FNotificationInfo Info(FText::Format(
				LOCTEXT("UniqueWidgetNamesOnApply", "Renamed {0} duplicate widget name(s) using UMG-style numeric suffixes."),
				FText::AsNumber(RenameCount)));
			Info.ExpireDuration = 5.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
			if (OutlinerPtr.IsValid())
			{
				OutlinerPtr->RequestRefresh();
			}
		}
	}

	// UMG BindWidget-style pass, run just before the prefab is written: auto-wire any null
	// Instance-Editable companion variable to the same-named descendant, and warn about
	// bindings the serializer would silently drop (dangling / not Instance-Editable / ambiguous).
	if (ULexWidget* RootWidget = GetLoadedRootWidget())
	{
		TArray<FString> BoundDetails, Problems;
		LexUIPrefabBehaviourUtils::AutoBindAndValidate(RootWidget, Prefab, BoundDetails, Problems);
		if (BoundDetails.Num() > 0)
		{
			Helper->Modify();
			Helper->SetAnythingDirty();
			FNotificationInfo Info(FText::Format(LOCTEXT("AutoBindOnApply", "Auto-bound {0} variable(s):\n{1}")
				, FText::AsNumber(BoundDetails.Num()), FText::FromString(FString::Join(BoundDetails, TEXT("\n")))));
			Info.ExpireDuration = 5.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
		if (Problems.Num() > 0)
		{
			bLastApplyHadProblems = true;
			FNotificationInfo Info(FText::Format(LOCTEXT("AutoBindProblemsOnApply", "Companion binding issues:\n{0}")
				, FText::FromString(FString::Join(Problems, TEXT("\n")))));
			Info.ExpireDuration = 8.0f;
			Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}

	bLastApplySerializationSucceeded = Helper->SavePrefab();
	if (!bLastApplySerializationSucceeded)
	{
		bLastApplyHadProblems = true;
		FNotificationInfo Info(LOCTEXT("ApplySerializationFailed", "Apply failed while serializing the prefab. Current changes were not written to the asset."));
		Info.ExpireDuration = 8.0f;
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.ErrorWithColor"));
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	return !bLastApplyHadProblems;
}

void FLexUIPrefabEditor::OnApply()
{
	const bool bApplySucceeded = ApplyPrefabChanges();
	const int32 SaveMode = LexUIPrefabEditorLocal::GetSaveOnApplyMode();
	const bool bShouldSave = SaveMode == LexUIPrefabEditorLocal::Always
		|| (SaveMode == LexUIPrefabEditorLocal::SuccessOnly && bApplySucceeded);
	if (bLastApplySerializationSucceeded && bShouldSave)
	{
		SaveAppliedPrefabToDisk();
	}
}

void FLexUIPrefabEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PrefabBeingEdited);
}

void FLexUIPrefabEditor::SelectWidgets(const TSet<ULexWidget*>& Widgets, bool bAppendOrToggle, bool bNotifyGEditor)
{
	if (bIsSelecting)return;
	bIsSelecting = true;
	
	TSet<ULexWidget*> TempSelection;
	for (auto& Widget : Widgets)
	{
		if (IsValid(Widget))
		{
			TempSelection.Add(Widget);
		}
	}

	if (!bAppendOrToggle)
	{
		SelectedWidgets.Empty();
		if (bNotifyGEditor)
		{
			ULexUISelection::GetInstance(GetWorld())->SelectNone();
		}
	}

	for ( const auto& Widget : TempSelection )
	{
		if ( bAppendOrToggle && SelectedWidgets.Contains(Widget) )
		{
			SelectedWidgets.Remove(Widget);
		}
		else
		{
			SelectedWidgets.Add(Widget);
		}
		if (bNotifyGEditor)
		{
			ULexUISelection::GetInstance(GetWorld())->SelectWidget(Widget);
		}
	}
	
	OnSelectionChanged.Broadcast();
	bIsSelecting = false;
}

FGuid FLexUIPrefabEditor::FindWidgetGuid(const ULexWidget* Widget) const
{
	if (!Widget || !PrefabBeingEdited || !PrefabBeingEdited->GetPrefabHelperObject())return FGuid();
	for (const auto& Pair : PrefabBeingEdited->GetPrefabHelperObject()->MapGuidToObject)
	{
		if (Pair.Value == Widget)return Pair.Key;
	}
	return FGuid();
}

FGuid FLexUIPrefabEditor::FindOrAddWidgetGuid(ULexWidget* Widget)
{
	if (!Widget)return FGuid();
	if (const FGuid Existing = FindWidgetGuid(Widget); Existing.IsValid())return Existing;
	if (ULexUIPrefabHelperObject* Helper = GetPrefabHelperObject())
	{
		Helper->Modify();
		const FGuid NewGuid = FGuid::NewGuid();
		Helper->MapGuidToObject.Add(NewGuid, Widget);
		return NewGuid;
	}
	return FGuid();
}

void FLexUIPrefabEditor::ApplyDesignerState()
{
	if (!PrefabBeingEdited || !GetPrefabHelperObject())return;
	const TSet<FGuid>& HiddenSet = PrefabBeingEdited->PrefabDataForPrefabEditor.HiddenWidgetSet;
	for (const auto& Pair : GetPrefabHelperObject()->MapGuidToObject)
	{
		if (ULexWidget* Widget = Cast<ULexWidget>(Pair.Value))
		{
			Widget->SetHiddenInDesigner(HiddenSet.Contains(Pair.Key));
		}
	}
}

bool FLexUIPrefabEditor::IsWidgetHiddenInDesigner(const ULexWidget* Widget) const
{
	return Widget && Widget->GetHiddenInDesigner();
}

void FLexUIPrefabEditor::SetWidgetHiddenInDesigner(ULexWidget* Widget, bool bHidden)
{
	if (!Widget || !PrefabBeingEdited || IsWidgetHiddenInDesigner(Widget) == bHidden)return;
	const FScopedTransaction Transaction(LOCTEXT("ToggleDesignerVisibility", "Toggle Designer Visibility"));
	PrefabBeingEdited->Modify();
	const FGuid Guid = FindOrAddWidgetGuid(Widget);
	if (!Guid.IsValid())return;
	if (bHidden)PrefabBeingEdited->PrefabDataForPrefabEditor.HiddenWidgetSet.Add(Guid);
	else PrefabBeingEdited->PrefabDataForPrefabEditor.HiddenWidgetSet.Remove(Guid);
	Widget->SetHiddenInDesigner(bHidden);
	PrefabBeingEdited->MarkPackageDirty();
	if (OutlinerPtr.IsValid())OutlinerPtr->RequestRefresh();
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid())ViewportPtr->GetViewportClient()->Invalidate();
}

bool FLexUIPrefabEditor::IsWidgetLockedInDesigner(const ULexWidget* Widget) const
{
	if (!PrefabBeingEdited)return false;
	const FGuid Guid = FindWidgetGuid(Widget);
	return Guid.IsValid() && PrefabBeingEdited->PrefabDataForPrefabEditor.LockedWidgetSet.Contains(Guid);
}

void FLexUIPrefabEditor::SetWidgetLockedInDesigner(ULexWidget* Widget, bool bLocked, bool bRecursive)
{
	if (!Widget || !PrefabBeingEdited)return;
	TArray<ULexWidget*> Widgets{ Widget };
	if (bRecursive)
	{
		TArray<ULexWidget*> Descendants;
		ULexWidget::CollectChildrenWidgets(Widget, Descendants);
		Widgets.Append(Descendants);
	}
	const FScopedTransaction Transaction(LOCTEXT("ToggleDesignerLock", "Toggle Designer Lock"));
	PrefabBeingEdited->Modify();
	for (ULexWidget* Item : Widgets)
	{
		const FGuid Guid = FindOrAddWidgetGuid(Item);
		if (!Guid.IsValid())continue;
		if (bLocked)PrefabBeingEdited->PrefabDataForPrefabEditor.LockedWidgetSet.Add(Guid);
		else PrefabBeingEdited->PrefabDataForPrefabEditor.LockedWidgetSet.Remove(Guid);
	}
	PrefabBeingEdited->MarkPackageDirty();
	OnSelectionChanged.Broadcast();
	if (OutlinerPtr.IsValid())OutlinerPtr->RequestRefresh();
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid())ViewportPtr->GetViewportClient()->Invalidate();
}

bool FLexUIPrefabEditor::IsDesignerGridSnapEnabled() const
{
	return PrefabBeingEdited && PrefabBeingEdited->PrefabDataForPrefabEditor.bDesignerGridSnapEnabled;
}

void FLexUIPrefabEditor::ToggleDesignerGridSnap()
{
	if (!PrefabBeingEdited)return;
	PrefabBeingEdited->Modify();
	PrefabBeingEdited->PrefabDataForPrefabEditor.bDesignerGridSnapEnabled = !IsDesignerGridSnapEnabled();
	PrefabBeingEdited->MarkPackageDirty();
}

float FLexUIPrefabEditor::GetDesignerGridSize() const
{
	return PrefabBeingEdited ? FMath::Max(1.0f, PrefabBeingEdited->PrefabDataForPrefabEditor.DesignerGridSize) : 10.0f;
}

void FLexUIPrefabEditor::SetDesignerGridSize(float GridSize)
{
	if (!PrefabBeingEdited)return;
	PrefabBeingEdited->Modify();
	PrefabBeingEdited->PrefabDataForPrefabEditor.DesignerGridSize = FMath::Max(1.0f, GridSize);
	PrefabBeingEdited->MarkPackageDirty();
}

bool FLexUIPrefabEditor::GetShowDesignerGuides() const
{
	return PrefabBeingEdited && PrefabBeingEdited->PrefabDataForPrefabEditor.bShowDesignerGuides;
}

void FLexUIPrefabEditor::ToggleDesignerGuides()
{
	if (!PrefabBeingEdited)return;
	PrefabBeingEdited->Modify();
	PrefabBeingEdited->PrefabDataForPrefabEditor.bShowDesignerGuides = !GetShowDesignerGuides();
	PrefabBeingEdited->MarkPackageDirty();
}

float FLexUIPrefabEditor::SnapDesignerValue(float Value) const
{
	if (!IsDesignerGridSnapEnabled())return Value;
	return FMath::GridSnap(Value, GetDesignerGridSize());
}

TSharedPtr<SWidget> FLexUIPrefabEditor::BuildWidgetContextMenu()
{
	return OutlinerPtr.IsValid() ? OutlinerPtr->BuildContextMenu() : nullptr;
}

FLexUIPrefabInstanceScene* FLexUIPrefabEditor::GetPreviewScene()
{ 
	return PrefabBeingEdited->GetPrefabInstanceScene();
}

UWorld* FLexUIPrefabEditor::GetWorld()
{
	return PrefabBeingEdited->GetPrefabInstanceScene()->GetWorld();
}

void FLexUIPrefabEditor::BindCommands()
{
	const FLexUIPrefabEditorCommand& PrefabEditorCommands = FLexUIPrefabEditorCommand::Get();
	ToolkitCommands->MapAction(
		PrefabEditorCommands.Apply,
		FExecuteAction::CreateSP(this, &FLexUIPrefabEditor::OnApply),
		FCanExecuteAction(),
		FIsActionChecked()
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.SaveOnApply_Never,
		FExecuteAction::CreateSP(this, &FLexUIPrefabEditor::SetSaveOnApplyMode, static_cast<int32>(LexUIPrefabEditorLocal::Never)),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FLexUIPrefabEditor::IsSaveOnApplyMode, static_cast<int32>(LexUIPrefabEditorLocal::Never))
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.SaveOnApply_SuccessOnly,
		FExecuteAction::CreateSP(this, &FLexUIPrefabEditor::SetSaveOnApplyMode, static_cast<int32>(LexUIPrefabEditorLocal::SuccessOnly)),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FLexUIPrefabEditor::IsSaveOnApplyMode, static_cast<int32>(LexUIPrefabEditorLocal::SuccessOnly))
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.SaveOnApply_Always,
		FExecuteAction::CreateSP(this, &FLexUIPrefabEditor::SetSaveOnApplyMode, static_cast<int32>(LexUIPrefabEditorLocal::Always)),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FLexUIPrefabEditor::IsSaveOnApplyMode, static_cast<int32>(LexUIPrefabEditorLocal::Always))
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.RawDataViewer,
		FExecuteAction::CreateSP(this, &FLexUIPrefabEditor::OnOpenRawDataViewerPanel),
		FCanExecuteAction(),
		FIsActionChecked()
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.OpenPrefabHelperObject,
		FExecuteAction::CreateSP(this, &FLexUIPrefabEditor::OnOpenPrefabHelperObjectDetailsPanel),
		FCanExecuteAction(),
		FIsActionChecked()
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.OpenBehaviourBlueprint,
		FExecuteAction::CreateSP(this, &FLexUIPrefabEditor::CreateOrOpenBehaviourBlueprint),
		FCanExecuteAction(),
		FIsActionChecked()
	);

	TFunction<ULexWidget*()> GetSelectedWidget = [this]()
	{
		if (this->GetSelectedWidgets().Num() == 1)
		{
			auto Actor = this->GetSelectedWidgets()[0];
			if (Actor.IsValid())
			{
				return Actor.Get();
			}
		}
		return (ULexWidget*)nullptr;
	};
	TFunction<TArray<ULexWidget*>()> GetSelectedWidgetArray = [this]()
	{
		TArray<ULexWidget*> TempSelectedActors;
		if (this->GetSelectedWidgets().Num() > 0)
		{
			for (auto Actor : this->GetSelectedWidgets())
			{
				if (Actor.IsValid())
				{
					TempSelectedActors.Add(Actor.Get());
				}
			}
			return TempSelectedActors;
		}
		return TempSelectedActors;
	};
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Copy,
		FExecuteAction::CreateStatic(&FLexUIEditorTools::CopyWidgets, GetSelectedWidgetArray),
		FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanCopyWidget, GetSelectedWidgetArray),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Cut,
		FExecuteAction::CreateSPLambda(this, [=, this]()
		{
			FLexUIEditorTools::CutWidgets(GetSelectedWidgetArray);
			OutlinerPtr->RequestRefresh();
		}),
		FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanCutWidget, GetSelectedWidgetArray),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Paste,
		FExecuteAction::CreateSPLambda(this, [=, this]()
		{
			FLexUIEditorTools::PasteWidgets(GetSelectedWidgetArray);
			OutlinerPtr->RequestRefresh();
		}),
		FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanPasteWidget, GetSelectedWidget),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSPLambda(this, [=, this]()
		{
			FLexUIEditorTools::DuplicateWidgets(GetSelectedWidgetArray);
			OutlinerPtr->RequestRefresh();
		}),
		FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanDuplicateWidget, GetSelectedWidgetArray),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSPLambda(this, [=, this]()
		{
			FLexUIEditorTools::DeleteWidgets(GetSelectedWidgetArray);
			OutlinerPtr->RequestRefresh();
		}),
		FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanDeleteWidget, GetSelectedWidgetArray),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
}
void FLexUIPrefabEditor::ExtendToolbar()
{
	const FName MenuName = GetToolMenuToolbarName();
	if (!UToolMenus::Get()->IsMenuRegistered(MenuName))
	{
		UToolMenus::Get()->RegisterMenu(MenuName, "AssetEditor.DefaultToolBar", EMultiBoxType::ToolBar);
	}

	UToolMenu* ToolBar = UToolMenus::Get()->FindMenu(MenuName);

	FToolMenuInsert InsertAfterAssetSection("Asset", EToolMenuInsertType::After);
	{
		auto ApplyButtonMenuEntry = FToolMenuEntry::InitToolBarButton(FLexUIPrefabEditorCommand::Get().Apply
			, LOCTEXT("Apply", "Apply")
			, TAttribute<FText>(this, &FLexUIPrefabEditor::GetApplyButtonStatusTooltip)
			, TAttribute<FSlateIcon>(this, &FLexUIPrefabEditor::GetApplyButtonStatusImage));
		ApplyButtonMenuEntry.StyleNameOverride = "CalloutToolbar";

		auto ApplyOptionsMenuEntry = FToolMenuEntry::InitComboButton(
			"ApplyOptions",
			FUIAction(),
			FNewToolMenuDelegate::CreateSP(this, &FLexUIPrefabEditor::GenerateApplyOptionsMenu),
			LOCTEXT("ApplyOptionsTooltip", "Options to customize how LexUI prefabs are applied"));
		ApplyOptionsMenuEntry.StyleNameOverride = "CalloutToolbar";
		ApplyOptionsMenuEntry.ToolBarData.bSimpleComboBox = true;
		
		auto BehaviourButton = FToolMenuEntry::InitToolBarButton(FLexUIPrefabEditorCommand::Get().OpenBehaviourBlueprint
			, TAttribute<FText>(), TAttribute<FText>()
			, FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Blueprints"));

		FToolMenuSection& Section = ToolBar->AddSection("LexUIPrefabCommands", TAttribute<FText>(), InsertAfterAssetSection);
		Section.AddEntry(ApplyButtonMenuEntry);
		Section.AddEntry(ApplyOptionsMenuEntry);
		Section.AddEntry(BehaviourButton);
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(FLexUIPrefabEditorCommand::Get().RawDataViewer));
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(FLexUIPrefabEditorCommand::Get().OpenPrefabHelperObject));
	}
}

void FLexUIPrefabEditor::GenerateApplyOptionsMenu(UToolMenu* InMenu)
{
	FToolMenuSection& Section = InMenu->AddSection("ApplyOptions");
	Section.AddSubMenu(
		"SaveOnApply",
		LOCTEXT("SaveOnApplySubMenu", "Save on Apply"),
		LOCTEXT("SaveOnApplySubMenuTooltip", "Determines when the prefab asset is saved after Apply."),
		FNewToolMenuDelegate::CreateSP(this, &FLexUIPrefabEditor::GenerateSaveOnApplyMenu));
}

void FLexUIPrefabEditor::GenerateSaveOnApplyMenu(UToolMenu* InMenu)
{
	FToolMenuSection& Section = InMenu->AddSection("SaveOnApply");
	const FLexUIPrefabEditorCommand& Commands = FLexUIPrefabEditorCommand::Get();
	Section.AddMenuEntry(Commands.SaveOnApply_Never);
	Section.AddMenuEntry(Commands.SaveOnApply_SuccessOnly);
	Section.AddMenuEntry(Commands.SaveOnApply_Always);
}

void FLexUIPrefabEditor::SetSaveOnApplyMode(int32 InMode)
{
	if (!GConfig)
	{
		return;
	}
	const int32 ClampedMode = FMath::Clamp(
		InMode,
		static_cast<int32>(LexUIPrefabEditorLocal::Never),
		static_cast<int32>(LexUIPrefabEditorLocal::Always));
	GConfig->SetInt(
		LexUIPrefabEditorLocal::SaveOnApplySection,
		LexUIPrefabEditorLocal::SaveOnApplyKey,
		ClampedMode,
		GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

bool FLexUIPrefabEditor::IsSaveOnApplyMode(int32 InMode)const
{
	return LexUIPrefabEditorLocal::GetSaveOnApplyMode() == InMode;
}

FText FLexUIPrefabEditor::GetApplyButtonStatusTooltip()const
{
	if (GetAnythingDirty())
	{
		return LOCTEXT("Apply_Tooltip", "Changes need to be applied");
	}
	if (bLastApplyHadProblems)
	{
		return LOCTEXT("ApplyProblems_Tooltip", "Applied with validation issues");
	}
	return LOCTEXT("ApplyGood_Tooltip", "Prefab is up to date");
}
FSlateIcon FLexUIPrefabEditor::GetApplyButtonStatusImage()const
{
	static const FName CompileStatusBackground("Blueprint.CompileStatus.Background");
	static const FName CompileStatusUnknown("Blueprint.CompileStatus.Overlay.Unknown");
	static const FName CompileStatusGood("Blueprint.CompileStatus.Overlay.Good");
	static const FName CompileStatusError("Blueprint.CompileStatus.Overlay.Error");

	const FName Overlay = GetAnythingDirty() ? CompileStatusUnknown : (bLastApplyHadProblems ? CompileStatusError : CompileStatusGood);
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), CompileStatusBackground, NAME_None, Overlay);
}

TSharedRef<SDockTab> FLexUIPrefabEditor::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ViewportTab_Title", "Viewport"))
		[
			ViewportPtr.ToSharedRef()
		];
}
TSharedRef<SDockTab> FLexUIPrefabEditor::SpawnTab_Details(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTab_Title", "Details"))
		[
			DetailsPtr.ToSharedRef()
		];
}
TSharedRef<SDockTab> FLexUIPrefabEditor::SpawnTab_Outliner(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("OutlinerTab_Title", "Outliner"))
		[
			OutlinerPtr.ToSharedRef()
		];
}

TSharedRef<SDockTab> FLexUIPrefabEditor::SpawnTab_Palette(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("PaletteTab_Title", "Palette"))
		[
			PalettePtr.ToSharedRef()
		];
}

TSharedRef<SDockTab> FLexUIPrefabEditor::SpawnTab_Sequencer(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("SequencerTab_Title", "Animations"))
		[
			SequencerPtr.ToSharedRef()
		];
}

TSharedRef<SDockTab> FLexUIPrefabEditor::SpawnTab_PrefabRawDataViewer(const FSpawnTabArgs& Args)
{
	// Spawn the tab
	return SNew(SDockTab)
		.Label(LOCTEXT("OverrideParameterTab_Title", "PrefabRawData"))
		[
			PrefabRawDataViewer.ToSharedRef()
		];
}

bool FLexUIPrefabEditor::IsFilteredActor(const AActor* Actor)
{
	if (Actor == nullptr)
	{
		return false;
	}

	if (!Actor->IsListedInSceneOutliner())
	{
		return false;
	}
	return true;
}

void FLexUIPrefabEditor::OnOutlinerActorDoubleClick(AActor* Actor)
{
	// Create a bounding volume of all of the selected actors.
	FBox BoundingBox(ForceInit);

	TArray<AActor*> Actors;
	Actors.Add(Actor);

	for (int32 ActorIdx = 0; ActorIdx < Actors.Num(); ActorIdx++)
	{
		AActor* TempActor = Actors[ActorIdx];

		if (TempActor)
		{
			TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(TempActor);

			for (int32 ComponentIndex = 0; ComponentIndex < PrimitiveComponents.Num(); ++ComponentIndex)
			{
				UPrimitiveComponent* PrimitiveComponent = PrimitiveComponents[ComponentIndex];

				if (PrimitiveComponent->IsRegistered())
				{
					// Some components can have huge bounds but are not visible.  Ignore these components unless it is the only component on the actor 
					const bool bIgnore = PrimitiveComponents.Num() > 1 && PrimitiveComponent->GetIgnoreBoundsForEditorFocus();

					if (!bIgnore)
					{
						FBox LocalBox(ForceInit);
						if (GLevelEditorModeTools().ComputeBoundingBoxForViewportFocus(TempActor, PrimitiveComponent, LocalBox))
						{
							BoundingBox += LocalBox;
						}
						else
						{
							BoundingBox += PrimitiveComponent->Bounds.GetBox();
						}
					}
				}
			}
		}
	}

	ViewportPtr->GetViewportClient()->FocusViewportOnBox(BoundingBox);
}

FName FLexUIPrefabEditor::GetToolkitFName() const
{
	return FName("LexUIPrefabEditor");
}
FText FLexUIPrefabEditor::GetBaseToolkitName() const
{
	return LOCTEXT("LexUIPrefabEditorAppLabel", "LexUI Prefab Editor");
}
FText FLexUIPrefabEditor::GetToolkitName() const
{
	return FText::FromString(PrefabBeingEdited->GetName());
}
FText FLexUIPrefabEditor::GetToolkitToolTipText() const
{
	return FAssetEditorToolkit::GetToolTipTextForObject(PrefabBeingEdited);
}
FLinearColor FLexUIPrefabEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}
FString FLexUIPrefabEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("LexUIPrefabEditor");
}
FString FLexUIPrefabEditor::GetDocumentationLink() const
{
	return TEXT("");
}
void FLexUIPrefabEditor::OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit)
{

}
void FLexUIPrefabEditor::OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit)
{

}

FReply FLexUIPrefabEditor::TryHandleAssetDragDropOperation(const FDragDropEvent& DragDropEvent, ULexWidget* InParentWidget)
{
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (Operation.IsValid() && Operation->IsOfType<FAssetDragDropOp>())
	{
		TArray< FAssetData > DroppedAssetData = AssetUtil::ExtractAssetDataFromDrag(Operation);
		const int32 NumAssets = DroppedAssetData.Num();

		if (NumAssets > 0)
		{
			TArray<ULexUIPrefab*> PrefabsToLoad;
			auto IsSupportedActorClass = [](UClass* ActorClass) {
				if (ActorClass->HasAnyClassFlags(EClassFlags::CLASS_NotPlaceable | EClassFlags::CLASS_Abstract))
					return false;
				if (!ActorClass->IsChildOf(AActor::StaticClass()))return false;
				return true;
			};
			for (int32 DroppedAssetIdx = 0; DroppedAssetIdx < NumAssets; ++DroppedAssetIdx)
			{
				const FAssetData& AssetData = DroppedAssetData[DroppedAssetIdx];

				if (!AssetData.IsAssetLoaded())
				{
					GWarn->StatusUpdate(DroppedAssetIdx, NumAssets, FText::Format(LOCTEXT("LoadingAsset", "Loading Asset {0}"), FText::FromName(AssetData.AssetName)));
				}

				UObject* Asset = AssetData.GetAsset();
				if (auto PrefabAsset = Cast<ULexUIPrefab>(Asset))
				{
					if (PrefabAsset->IsPrefabBelongsToThisSubPrefab(this->PrefabBeingEdited, true))
					{
						auto MsgText = LOCTEXT("Error_EndlessNestedPrefab", "Operation error! Target prefab have this prefab as child prefab, which will result in cyclic nested prefab!");
						FMessageDialog::Open(EAppMsgType::Ok, MsgText);
						return FReply::Unhandled();
					}
					if (this->PrefabBeingEdited == PrefabAsset)
					{
						auto MsgText = LOCTEXT("Error_SelfPrefabAsSubPrefab", "Operation error! Target prefab is same of this one, self cannot be self's child!");
						FMessageDialog::Open(EAppMsgType::Ok, MsgText);
						return FReply::Unhandled();
					}
					if (PrefabAsset->PrefabVersion <= (uint16)ELexUIPrefabVersion::OldVersion)
					{
						auto MsgText = LOCTEXT("Error_UnsupportOldPrefabVersion", "Operation error! Target prefab's version is too old! Please make it newer: open the prefab and hit \"Save\" button.");
						FMessageDialog::Open(EAppMsgType::Ok, MsgText);
						return FReply::Unhandled();
					}

					PrefabsToLoad.Add(PrefabAsset);
				}
			}

			auto CurrentSelectedWidget = InParentWidget != nullptr ? InParentWidget :
			(SelectedWidgets.Num() > 0 ? SelectedWidgets[0] : nullptr);
			if (PrefabsToLoad.Num() > 0)
			{
				if (CurrentSelectedWidget == nullptr)
				{
					auto MsgText = LOCTEXT("Error_NeedParentNode", "Please select a actor as parent actor");
					FMessageDialog::Open(EAppMsgType::Ok, MsgText);
					return FReply::Unhandled();
				}
				if (CurrentSelectedWidget == GetPreviewScene()->GetRootAgent())
				{
					auto MsgText = FText::Format(LOCTEXT("Error_RootCannotBeParentNode", "{0} cannot be parent actor of child prefab, please choose another actor."), FText::FromString(FLexUIPrefabInstanceScene::RootAgentActorName));
					FMessageDialog::Open(EAppMsgType::Ok, MsgText);
					return FReply::Unhandled();
				}
			}
			else
			{
				return FReply::Unhandled();
			}

			GEditor->BeginTransaction(LOCTEXT("CreateFromAssetDrop_Transaction", "LexUI Create from asset drop"));
			TArray<ULexWidget*> CreatedWidgetArray;
			if (PrefabsToLoad.Num() > 0)
			{
				for (auto& PrefabAsset : PrefabsToLoad)
				{
					TMap<FGuid, TObjectPtr<UObject>> SubPrefabMapGuidToObject;
					TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> SubSubPrefabMap;
					auto LoadedSubPrefabRootActor = PrefabAsset->LoadPrefabWithExistingObjects(
						GetPreviewScene()->GetWorld()
						, CurrentSelectedWidget->GetOuter()
						, CurrentSelectedWidget.Get()
						, SubPrefabMapGuidToObject, SubSubPrefabMap
					);

					GetPrefabHelperObject()->MakePrefabAsSubPrefab(PrefabAsset, LoadedSubPrefabRootActor, SubPrefabMapGuidToObject, {});
					FLexUIEditorTools::EnsureUniqueWidgetDisplayNames(GetLoadedRootWidget());
					CreatedWidgetArray.Add(LoadedSubPrefabRootActor);
				}

				if (OutlinerPtr.IsValid())
				{
					ULexUIManagerObject::AddOneShotTickFunction([=, this] {
						for (auto& Actor : CreatedWidgetArray)
						{
							//OutlinerPtr->UnexpandActorForDragDroppedPrefab(Actor);
						}
						OutlinerPtr->RequestRefresh();
						}, 1);//delay execute, because the outliner not create actor yet
				}
			}
			if (CreatedWidgetArray.Num() > 0)
			{
				ULexUISelection::GetInstance(GetWorld())->SelectNone();
				for (auto& Widget : CreatedWidgetArray)
				{
					ULexUISelection::GetInstance(GetWorld())->SelectWidget(Widget);
				}
			}
			GEditor->EndTransaction();
		}

		return FReply::Handled();
	}
	return FReply::Unhandled();
}



#undef LOCTEXT_NAMESPACE
