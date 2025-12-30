// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUIPrefabEditor.h"
#include "LGUIPrefabEditorViewport.h"
#include "LGUIPrefabEditorScene.h"
#include "LexUIPrefabEditorDetails.h"
#include "LGUIPrefabRawDataViewer.h"
#include "EditorModeManager.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMeshActor.h"
#include "AssetSelection.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Misc/FeedbackContext.h"
#include "LGUIPrefabEditorCommand.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "LexUIEditorTools.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "SLexWidgetEditorHierarchyView.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexWidget.h"
#include "Framework/Commands/GenericCommands.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PrefabSystem/LexUIPrefabManager.h"
#include "Utils/LexUIUtils.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabEditor"



const FName PrefabEditorAppName = FName(TEXT("LexUIPrefabEditorApp"));

TArray<FLGUIPrefabEditor*> FLGUIPrefabEditor::PrefabEditorInstanceCollection;

struct FLGUIPrefabEditorTabs
{
	// Tab identifiers
	static const FName DetailsID;
	static const FName ViewportID;
	static const FName OutlinerID;
	static const FName PrefabRawDataViewerID;
};

const FName FLGUIPrefabEditorTabs::DetailsID(TEXT("Details"));
const FName FLGUIPrefabEditorTabs::ViewportID(TEXT("Viewport"));
const FName FLGUIPrefabEditorTabs::OutlinerID(TEXT("Outliner"));
const FName FLGUIPrefabEditorTabs::PrefabRawDataViewerID(TEXT("PrefabRawDataViewer"));

FName GetPrefabWorldName()
{
	static uint32 NameSuffix = 0;
	return FName(*FString::Printf(TEXT("PrefabEditorWorld_%d"), NameSuffix++));
}
FLGUIPrefabEditor::FLGUIPrefabEditor()
	:PreviewScene(FLGUIPrefabEditorScene::ConstructionValues().AllowAudioPlayback(true).ShouldSimulatePhysics(false).SetEditor(true).SetName(GetPrefabWorldName()))
{
	PrefabHelperObject = NewObject<ULexUIPrefabHelperObject>(GetTransientPackage(), NAME_None, EObjectFlags::RF_Transactional);
	PrefabEditorInstanceCollection.Add(this);

	GEditor->RegisterForUndo(this);
}
FLGUIPrefabEditor::~FLGUIPrefabEditor()
{
	PrefabHelperObject->ConditionalBeginDestroy();
	PrefabHelperObject = nullptr;

	PrefabEditorInstanceCollection.Remove(this);

	ULexUIManagerObject::MarkBroadcastLevelActorListChanged();
 	ULexUIManagerWorldSubsystem::GetInstance(GetWorld())->EventOnOutlineChanged.RemoveAll(this);
	ULexUIManagerWorldSubsystem::GetSelection(GetWorld())->OnSelectionChanged.RemoveAll(this);

	GEditor->UnregisterForUndo(this);
}

FLGUIPrefabEditor* FLGUIPrefabEditor::GetEditorForPrefabIfValid(ULexUIPrefab* InPrefab)
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

ULexUIPrefabHelperObject* FLGUIPrefabEditor::GetEditorPrefabHelperObjectForActor(AActor* InActor)
{
	for (auto Instance : PrefabEditorInstanceCollection)
	{
		if (InActor->GetWorld() == Instance->GetWorld())
		{
			return Instance->PrefabHelperObject;
		}
	}
	return nullptr;
}

bool FLGUIPrefabEditor::WorldIsPrefabEditor(UWorld* InWorld)
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

bool FLGUIPrefabEditor::ActorIsRootAgent(AActor* InActor)
{
	for (auto Instance : PrefabEditorInstanceCollection)
	{
		if (InActor == Instance->GetPreviewScene().GetRootAgentActor())
		{
			return true;
		}
	}
	return false;
}

void FLGUIPrefabEditor::IterateAllPrefabEditor(const TFunction<void(FLGUIPrefabEditor*)>& InFunction)
{
	for (auto Instance : PrefabEditorInstanceCollection)
	{
		InFunction(Instance);
	}
}

bool FLGUIPrefabEditor::RefreshOnSubPrefabDirty(ULexUIPrefab* InSubPrefab)
{
	return PrefabHelperObject->RefreshOnSubPrefabDirty(InSubPrefab);
}

bool FLGUIPrefabEditor::GetSelectedObjectsBounds(FBoxSphereBounds& OutResult)
{
	FBoxSphereBounds Bounds = FBoxSphereBounds(EForceInit::ForceInitToZero);
	bool IsFirstBounds = true;
	for (auto& Actor : SelectedActors)
	{
		auto Box = Actor->GetComponentsBoundingBox();
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

FBoxSphereBounds FLGUIPrefabEditor::GetAllObjectsBounds()
{
	FBoxSphereBounds Bounds;
	bool IsFirstBounds = true;
	for (auto& KeyValue : PrefabHelperObject->MapGuidToObject)
	{
		FBox Box; bool bIsValidBox = false;
		if (auto SceneComp = Cast<USceneComponent>(KeyValue.Value))
		{
			if (SceneComp->IsRegistered() && !SceneComp->IsVisualizationComponent())
			{
				if (auto Widget = Cast<ULexWidget>(SceneComp))
				{
					if (Widget->GetWidgetActiveInHierarchy())
					{
						Box = Widget->Bounds.GetBox();
						bIsValidBox = true;
					}
				}
				else if (auto PrimitiveComp = Cast<UPrimitiveComponent>(SceneComp))
				{
					Box = PrimitiveComp->Bounds.GetBox();
					bIsValidBox = true;
				}
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

bool FLGUIPrefabEditor::ActorBelongsToSubPrefab(AActor* InActor)
{
	return PrefabHelperObject->IsActorBelongsToSubPrefab(InActor);
}

bool FLGUIPrefabEditor::ActorIsSubPrefabRoot(AActor* InSubPrefabRootActor)
{
	return PrefabHelperObject->SubPrefabMap.Contains(InSubPrefabRootActor);
}

FLexUISubPrefabData FLGUIPrefabEditor::GetSubPrefabDataForActor(AActor* InSubPrefabActor)
{
	return PrefabHelperObject->GetSubPrefabData(InSubPrefabActor);
}

void FLGUIPrefabEditor::OpenSubPrefab(AActor* InSubPrefabActor)
{
	if (auto SubPrefabAsset = PrefabHelperObject->GetSubPrefabAsset(InSubPrefabActor))
	{
		auto PrefabEditor = FLGUIPrefabEditor::GetEditorForPrefabIfValid(SubPrefabAsset);
		if (!PrefabEditor)
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			AssetEditorSubsystem->OpenEditorForAsset(SubPrefabAsset);
		}
	}
}
void FLGUIPrefabEditor::SelectSubPrefab(AActor* InSubPrefabActor)
{
	if (auto SubPrefabAsset = PrefabHelperObject->GetSubPrefabAsset(InSubPrefabActor))
	{
		TArray<UObject*> ObjectsToSync;
		ObjectsToSync.Add(SubPrefabAsset);
		GEditor->SyncBrowserToObjects(ObjectsToSync);
	}
}

bool FLGUIPrefabEditor::GetAnythingDirty()const 
{ 
	return PrefabHelperObject->GetAnythingDirty();
}

void FLGUIPrefabEditor::CloseWithoutCheckDataDirty()
{
	PrefabHelperObject->SetNothingDirty();
	this->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed);
}

bool FLGUIPrefabEditor::OnRequestClose()
{
	if (GetAnythingDirty())
	{
		auto WarningMsg = LOCTEXT("OnCloseEditor_DataMissingWarning", "Are you sure you want to close prefab editor window? Property will lose if not hit Apply!");
		auto Result = FMessageDialog::Open(EAppMsgType::YesNo, WarningMsg);
		if (Result == EAppReturnType::Yes)
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	return true;
}

void FLGUIPrefabEditor::SyncSelection()
{
	SelectedActors = ULexUIManagerWorldSubsystem::GetSelection(GetWorld())->GetSelectedActors();
	OnSelectedWidgetsChanged.Broadcast();
	OutlinerPtr->RequestRefresh();
}

void FLGUIPrefabEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_LGUIPrefabEditor", "LGUIPrefab Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(FLGUIPrefabEditorTabs::ViewportID, FOnSpawnTab::CreateSP(this, &FLGUIPrefabEditor::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(FLGUIPrefabEditorTabs::DetailsID, FOnSpawnTab::CreateSP(this, &FLGUIPrefabEditor::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTabLabel", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(FLGUIPrefabEditorTabs::OutlinerID, FOnSpawnTab::CreateSP(this, &FLGUIPrefabEditor::SpawnTab_Outliner))
		.SetDisplayName(LOCTEXT("OutlinerTabLabel", "Outliner"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));

	InTabManager->RegisterTabSpawner(FLGUIPrefabEditorTabs::PrefabRawDataViewerID, FOnSpawnTab::CreateSP(this, &FLGUIPrefabEditor::SpawnTab_PrefabRawDataViewer))
		.SetDisplayName(LOCTEXT("PrefabRawDataViewerTabLabel", "PrefabRawDataViewer"))
		.SetGroup(WorkspaceMenuCategoryRef)
		;
}
void FLGUIPrefabEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(FLGUIPrefabEditorTabs::ViewportID);
	InTabManager->UnregisterTabSpawner(FLGUIPrefabEditorTabs::DetailsID);
	InTabManager->UnregisterTabSpawner(FLGUIPrefabEditorTabs::OutlinerID);
	InTabManager->UnregisterTabSpawner(FLGUIPrefabEditorTabs::PrefabRawDataViewerID);
}

void FLGUIPrefabEditor::PostUndo(bool bSuccess)
{
	ULexUIManagerWorldSubsystem::RefreshAllUI();
	SelectedActors = ULexUIManagerWorldSubsystem::GetSelection(GetWorld())->GetSelectedActors();
	OnSelectedWidgetsChanged.Broadcast();
	OutlinerPtr->RequestRefresh();
}
void FLGUIPrefabEditor::PostRedo(bool bSuccess)
{
	ULexUIManagerWorldSubsystem::RefreshAllUI();
	SelectedActors = ULexUIManagerWorldSubsystem::GetSelection(GetWorld())->GetSelectedActors();
	OnSelectedWidgetsChanged.Broadcast();
	OutlinerPtr->RequestRefresh();
}

void FLGUIPrefabEditor::InitPrefabEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost >& InitToolkitHost, ULexUIPrefab* InPrefab)
{
	PrefabBeingEdited = InPrefab;
	PrefabHelperObject->PrefabAsset = PrefabBeingEdited;
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

	FLGUIPrefabEditorCommand::Register();

	PrefabHelperObject->LoadPrefab(GetPreviewScene().GetWorld(), GetPreviewScene().GetParentComponentForPrefab(PrefabBeingEdited));
	if (!IsValid(PrefabHelperObject->LoadedRootActor))
	{
		auto MsgText = LOCTEXT("Error_LoadPrefabFail", "Load prefab fail! Nothing loaded!");
		FMessageDialog::Open(EAppMsgType::Ok, MsgText);
	}
	PrefabHelperObject->RootAgentActorForPrefabEditor = GetPreviewScene().GetRootAgentActor();
	PrefabHelperObject->MarkAsManagerObject();

	TSharedPtr<FLGUIPrefabEditor> PrefabEditorPtr = SharedThis(this);

	ViewportPtr = SNew(SLGUIPrefabEditorViewport, PrefabEditorPtr, PrefabBeingEdited->PrefabDataForPrefabEditor.ViewMode);
	
	DetailsPtr = SNew(SLexUIPrefabEditorDetails, PrefabEditorPtr);

	PrefabRawDataViewer = SNew(SLGUIPrefabRawDataViewer, PrefabEditorPtr, PrefabBeingEdited);
	
	ULexUIManagerWorldSubsystem::GetInstance(GetWorld())->EventOnOutlineChanged.AddSPLambda(this, [=, this]()
	{
		OutlinerPtr->RequestRefresh();
	});
	ULexUIManagerWorldSubsystem::GetSelection(GetWorld())->OnSelectionChanged.AddSPLambda(this, [=, this]
	{
		SyncSelection();
	});
	
	OutlinerPtr = SNew(SLexWidgetEditorHierarchyView, PrefabEditorPtr);

	BindCommands();
	ExtendToolbar();

	// Default layout
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_LGUIPrefabEditor_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Horizontal)
				->SetSizeCoefficient(0.9f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.2f)
					->AddTab(FLGUIPrefabEditorTabs::OutlinerID, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.6f)
					->AddTab(FLGUIPrefabEditorTabs::ViewportID, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.2f)
					->AddTab(FLGUIPrefabEditorTabs::DetailsID, ETabState::OpenedTab)
				)
			)
		);

	InitAssetEditor(Mode, InitToolkitHost, PrefabEditorAppName, StandaloneDefaultLayout, true, true, PrefabBeingEdited);

	// After opening a prefab, broadcast event to LGUIPrefabSequencerEditor
	FLexUIEditorTools::OnEditingPrefabChanged.Broadcast(GetPreviewScene().GetRootAgentActor());
}

TArray<AActor*> FLGUIPrefabEditor::GetAllActors()
{
	TArray<AActor*> AllActors;
	if (PrefabHelperObject->LoadedRootActor != nullptr)
	{
		FLexUIUtils::CollectChildrenActors(PrefabHelperObject->LoadedRootActor, AllActors, true);
	}
	return AllActors;
}

void FLGUIPrefabEditor::GetInitialViewSetting(FVector& OutLocation, FRotator& OutRotation, FVector& OutOrbitLocation, ELevelViewportType& OutViewType)
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
	OutViewType = PrefabEditorData.ViewportType;
}

AActor* FLGUIPrefabEditor::GetRootAgentActor()
{
	return GetPreviewScene().GetRootAgentActor();
}

AActor* FLGUIPrefabEditor::GetLoadedRootActor()
{
	return PrefabHelperObject->LoadedRootActor;
}

void FLGUIPrefabEditor::ApplyPrefab()
{
	OnApply();
}

void FLGUIPrefabEditor::SaveAsset_Execute()
{
	if (CheckBeforeSaveAsset())
	{
		if (GetAnythingDirty())
		{
			OnApply();//apply change
		}
		else
		{
			SaveViewState();
		}
		FAssetEditorToolkit::SaveAsset_Execute();//save asset
	}
}
void FLGUIPrefabEditor::OnApply()
{
	if (CheckBeforeSaveAsset())
	{
		SaveViewState();
		TSet<TWeakObjectPtr<ULexWidget>> ExpandWidgetSet;
		OutlinerPtr->GetExpandWidgets(ExpandWidgetSet);
		TSet<FGuid> UnexpandWidgetGuidArray;
		for (auto& KeyValue : PrefabHelperObject->MapGuidToObject)
		{
			if (auto Widget = Cast<ULexWidget>(KeyValue.Value))
			{
				if (!ExpandWidgetSet.Contains(Widget))
				{
					UnexpandWidgetGuidArray.Add(KeyValue.Key);
				}
			}
		}
		PrefabBeingEdited->PrefabDataForPrefabEditor.UnexpandWidgetSet = UnexpandWidgetGuidArray;
		PrefabBeingEdited->bThumbnailDirty = true;

		//refresh parameter, remove invalid
		for (auto& KeyValue : PrefabHelperObject->SubPrefabMap)
		{
			KeyValue.Value.CheckParameters();
		}

		FLexUIEditorTools::OnBeforeApplyPrefab.Broadcast(PrefabHelperObject);
		PrefabHelperObject->SavePrefab();
		FLexUIEditorTools::RefreshLevelLoadedPrefab();
		FLexUIEditorTools::RefreshOnSubPrefabChange(PrefabHelperObject->PrefabAsset);
	}
}

void FLGUIPrefabEditor::OnOpenRawDataViewerPanel()
{
	this->InvokeTab(FLGUIPrefabEditorTabs::PrefabRawDataViewerID);
}
void FLGUIPrefabEditor::OnOpenPrefabHelperObjectDetailsPanel()
{
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	AssetEditorSubsystem->OpenEditorForAsset(PrefabHelperObject);
}

void FLGUIPrefabEditor::SaveViewState()
{
	//save view location and rotation
	auto ViewTransform = ViewportPtr->GetViewportClient()->GetViewTransform();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewLocation = ViewTransform.GetLocation();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewRotation = ViewTransform.GetRotation();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewOrbitLocation = ViewTransform.GetLookAt();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewportType = ViewportPtr->GetViewportClient()->GetViewportType();
	if (auto RootAgentActor = GetPreviewScene().GetRootAgentActor())
	{
		if (auto Widget = Cast<ULexWidget>(RootAgentActor->GetRootComponent()))
		{
			PrefabBeingEdited->PrefabDataForPrefabEditor.CanvasSize = FIntPoint(Widget->GetWidth(), Widget->GetHeight());
		}
		if (auto Canvas = RootAgentActor->FindComponentByClass<ULexCanvas>())
		{
			PrefabBeingEdited->PrefabDataForPrefabEditor.bNeedCanvas = true;
			PrefabBeingEdited->PrefabDataForPrefabEditor.CanvasRenderMode = (uint8)Canvas->GetRenderMode();
		}
		else
		{
			PrefabBeingEdited->PrefabDataForPrefabEditor.bNeedCanvas = false;
		}
	}
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewMode = ViewportPtr->GetViewportClient()->GetViewMode();
}

void FLGUIPrefabEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PrefabBeingEdited);
	Collector.AddReferencedObject(PrefabHelperObject);
}

void FLGUIPrefabEditor::SelectWidgets(const TSet<ULexWidget*>& Widgets, bool bAppendOrToggle, bool bNotifyGEditor)
{
	if (bIsSelecting)return;
	bIsSelecting = true;
	const FScopedTransaction Transaction(LOCTEXT("SelectionChanged_Transaction", "Select Widgets"));
	ULexUIManagerWorldSubsystem::GetSelection(GetWorld())->Modify();
	
	TSet<ULexWidget*> TempSelection;
	for (auto& Widget : Widgets)
	{
		if (IsValid(Widget))
		{
			TempSelection.Add(Widget);
		}
	}

	OnSelectedWidgetsChanging.Broadcast();

	if (!bAppendOrToggle)
	{
		SelectedActors.Empty();
		if (bNotifyGEditor)
		{
			ULexUIManagerWorldSubsystem::GetSelection(GetWorld())->SelectNone();
		}
	}

	for ( const auto& Widget : TempSelection )
	{
		if ( bAppendOrToggle && SelectedActors.Contains(Widget->GetOwner()) )
		{
			SelectedActors.Remove(Widget->GetOwner());
		}
		else
		{
			SelectedActors.Add(Widget->GetOwner());
		}
		if (bNotifyGEditor)
		{
			ULexUIManagerWorldSubsystem::GetSelection(GetWorld())->SelectActor(Widget->GetOwner());
		}
	}
	
	OnSelectedWidgetsChanged.Broadcast();
	bIsSelecting = false;
}

TArray<TWeakObjectPtr<ULexWidget>> FLGUIPrefabEditor::GetSelectedWidgets()
{
	TArray<TWeakObjectPtr<ULexWidget>> SelectedWidgets;
	for (auto Actor : SelectedActors)
	{
		if (Actor.IsValid())
		{
			if (auto Widget = Cast<ULexWidget>(Actor->GetRootComponent()))
			{
				SelectedWidgets.Add(Widget);
			}
		}
	}
	return SelectedWidgets;
}

bool FLGUIPrefabEditor::CheckBeforeSaveAsset()
{
	auto RootUIAgentActor = GetPreviewScene().GetRootAgentActor();
	//All actor should attach to prefab's root actor
	for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		if (AActor* ItemActor = *ActorItr)
		{
			if (ItemActor == PrefabHelperObject->LoadedRootActor)continue;
			if (ItemActor == RootUIAgentActor)continue;
			if (GetPreviewScene().IsWorldDefaultActor(ItemActor))continue;
			if (!ItemActor->IsAttachedTo(PrefabHelperObject->LoadedRootActor))
			{
				auto MsgText = LOCTEXT("Error_AllActor", "All prefab's actors must attach to prefab's root actor!");
				FMessageDialog::Open(EAppMsgType::Ok, MsgText);
				return false;
			}
		}
	}

	return true;
}

FLGUIPrefabEditorScene& FLGUIPrefabEditor::GetPreviewScene()
{ 
	return PreviewScene;
}

UWorld* FLGUIPrefabEditor::GetWorld()
{
	return PreviewScene.GetWorld();
}

void FLGUIPrefabEditor::BindCommands()
{
	const FLGUIPrefabEditorCommand& PrefabEditorCommands = FLGUIPrefabEditorCommand::Get();
	ToolkitCommands->MapAction(
		PrefabEditorCommands.Apply,
		FExecuteAction::CreateSP(this, &FLGUIPrefabEditor::OnApply),
		FCanExecuteAction(),
		FIsActionChecked()
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.RawDataViewer,
		FExecuteAction::CreateSP(this, &FLGUIPrefabEditor::OnOpenRawDataViewerPanel),
		FCanExecuteAction(),
		FIsActionChecked()
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.OpenPrefabHelperObject,
		FExecuteAction::CreateSP(this, &FLGUIPrefabEditor::OnOpenPrefabHelperObjectDetailsPanel),
		FCanExecuteAction(),
		FIsActionChecked()
	);

	TFunction<AActor*()> GetSelectedActor = [this]()
	{
		if (this->GetSelectedActors().Num() == 1)
		{
			auto Actor = this->GetSelectedActors()[0];
			if (Actor.IsValid())
			{
				return Actor.Get();
			}
		}
		return (AActor*)nullptr;
	};
	TFunction<TArray<AActor*>()> GetSelectedActorArray = [this]()
	{
		TArray<AActor*> SelectedActors;
		if (this->GetSelectedActors().Num() > 0)
		{
			for (auto Actor : this->GetSelectedActors())
			{
				if (Actor.IsValid())
				{
					SelectedActors.Add(Actor.Get());
				}
			}
			return SelectedActors;
		}
		return SelectedActors;
	};
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Copy,
		FExecuteAction::CreateStatic(&FLexUIEditorTools::CopyActors, GetSelectedActorArray),
		FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanCopyActor, GetSelectedActorArray),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Cut,
		FExecuteAction::CreateSPLambda(this, [=, this]()
		{
			FLexUIEditorTools::CutActors(GetSelectedActorArray);
			OutlinerPtr->RequestRefresh();
		}),
		FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanCutActor, GetSelectedActorArray),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Paste,
		FExecuteAction::CreateSPLambda(this, [=, this]()
		{
			FLexUIEditorTools::PasteActors(GetSelectedActorArray);
			OutlinerPtr->RequestRefresh();
		}),
		FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanPasteActor, GetSelectedActor),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSPLambda(this, [=, this]()
		{
			FLexUIEditorTools::DuplicateActors(GetSelectedActorArray);
			OutlinerPtr->RequestRefresh();
		}),
		FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanDuplicateActor, GetSelectedActorArray),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSPLambda(this, [=, this]()
		{
			FLexUIEditorTools::DeleteActors(GetSelectedActorArray);
			OutlinerPtr->RequestRefresh();
		}),
		FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanDeleteActor, GetSelectedActorArray),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
}
void FLGUIPrefabEditor::ExtendToolbar()
{
	const FName MenuName = GetToolMenuToolbarName();
	if (!UToolMenus::Get()->IsMenuRegistered(MenuName))
	{
		UToolMenus::Get()->RegisterMenu(MenuName, "AssetEditor.DefaultToolBar", EMultiBoxType::ToolBar);
	}

	UToolMenu* ToolBar = UToolMenus::Get()->FindMenu(MenuName);

	FToolMenuInsert InsertAfterAssetSection("Asset", EToolMenuInsertType::After);
	{
		auto ApplyButtonMenuEntry = FToolMenuEntry::InitToolBarButton(FLGUIPrefabEditorCommand::Get().Apply
			, LOCTEXT("Apply", "Apply")
			, TAttribute<FText>(this, &FLGUIPrefabEditor::GetApplyButtonStatusTooltip)
			, TAttribute<FSlateIcon>(this, &FLGUIPrefabEditor::GetApplyButtonStatusImage));

		FToolMenuSection& Section = ToolBar->AddSection("LGUIPrefabCommands", TAttribute<FText>(), InsertAfterAssetSection);
		Section.AddEntry(ApplyButtonMenuEntry);
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(FLGUIPrefabEditorCommand::Get().RawDataViewer));
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(FLGUIPrefabEditorCommand::Get().OpenPrefabHelperObject));
	}
}

FText FLGUIPrefabEditor::GetApplyButtonStatusTooltip()const
{
	return GetAnythingDirty() ? LOCTEXT("Apply_Tooltip", "Dirty, need to apply") : LOCTEXT("Apply_Tooltip", "Good to go");
}
FSlateIcon FLGUIPrefabEditor::GetApplyButtonStatusImage()const
{
	static const FName CompileStatusBackground("Blueprint.CompileStatus.Background");
	static const FName CompileStatusUnknown("Blueprint.CompileStatus.Overlay.Unknown");
	static const FName CompileStatusGood("Blueprint.CompileStatus.Overlay.Good");

	return FSlateIcon(FAppStyle::GetAppStyleSetName(), CompileStatusBackground, NAME_None, GetAnythingDirty() ? CompileStatusUnknown : CompileStatusGood);
}

TSharedRef<SDockTab> FLGUIPrefabEditor::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ViewportTab_Title", "Viewport"))
		[
			ViewportPtr.ToSharedRef()
		];
}
TSharedRef<SDockTab> FLGUIPrefabEditor::SpawnTab_Details(const FSpawnTabArgs& Args)
{
	// Spawn the tab
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTab_Title", "Details"))
		[
			DetailsPtr.ToSharedRef()
		];
}
TSharedRef<SDockTab> FLGUIPrefabEditor::SpawnTab_Outliner(const FSpawnTabArgs& Args)
{
	// Spawn the tab
	return SNew(SDockTab)
		.Label(LOCTEXT("OutlinerTab_Title", "Outliner"))
		[
			OutlinerPtr.ToSharedRef()
		];
}

TSharedRef<SDockTab> FLGUIPrefabEditor::SpawnTab_PrefabRawDataViewer(const FSpawnTabArgs& Args)
{
	// Spawn the tab
	return SNew(SDockTab)
		.Label(LOCTEXT("OverrideParameterTab_Title", "PrefabRawData"))
		[
			PrefabRawDataViewer.ToSharedRef()
		];
}

bool FLGUIPrefabEditor::IsFilteredActor(const AActor* Actor)
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

void FLGUIPrefabEditor::OnOutlinerActorDoubleClick(AActor* Actor)
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

FName FLGUIPrefabEditor::GetToolkitFName() const
{
	return FName("LGUIPrefabEditor");
}
FText FLGUIPrefabEditor::GetBaseToolkitName() const
{
	return LOCTEXT("LGUIPrefabEditorAppLabel", "LGUI Prefab Editor");
}
FText FLGUIPrefabEditor::GetToolkitName() const
{
	return FText::FromString(PrefabBeingEdited->GetName());
}
FText FLGUIPrefabEditor::GetToolkitToolTipText() const
{
	return FAssetEditorToolkit::GetToolTipTextForObject(PrefabBeingEdited);
}
FLinearColor FLGUIPrefabEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}
FString FLGUIPrefabEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("LGUIPrefabEditor");
}
FString FLGUIPrefabEditor::GetDocumentationLink() const
{
	return TEXT("");
}
void FLGUIPrefabEditor::OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit)
{

}
void FLGUIPrefabEditor::OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit)
{

}

FReply FLGUIPrefabEditor::TryHandleAssetDragDropOperation(const FDragDropEvent& DragDropEvent, ULexWidget* InParentWidget)
{
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (Operation.IsValid() && Operation->IsOfType<FAssetDragDropOp>())
	{
		TArray< FAssetData > DroppedAssetData = AssetUtil::ExtractAssetDataFromDrag(Operation);
		const int32 NumAssets = DroppedAssetData.Num();

		if (NumAssets > 0)
		{
			TArray<ULexUIPrefab*> PrefabsToLoad;
			TArray<UClass*> PotentialActorClassesToLoad;
			TArray<UStaticMesh*> PotentialStaticMeshesToLoad;
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

				UClass* AssetClass = AssetData.GetClass();
				UObject* Asset = AssetData.GetAsset();
				UBlueprint* BPClass = Cast<UBlueprint>(Asset);
				UClass* PotentialActorClass = nullptr;
				if ((BPClass != nullptr) && (BPClass->GeneratedClass != nullptr))
				{
					if (IsSupportedActorClass(BPClass->GeneratedClass))
					{
						PotentialActorClass = BPClass->GeneratedClass;
					}
				}
				else if (AssetClass->IsChildOf(UClass::StaticClass()))
				{
					UClass* AssetAsClass = CastChecked<UClass>(Asset);
					if (IsSupportedActorClass(AssetAsClass))
					{
						PotentialActorClass = AssetAsClass;
					}
				}
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
				else if (auto StaticMeshAsset = Cast<UStaticMesh>(Asset))
				{
					PotentialStaticMeshesToLoad.Add(StaticMeshAsset);
				}
				else if (PotentialActorClass != nullptr)
				{
					PotentialActorClassesToLoad.Add(PotentialActorClass);
				}
			}

			auto CurrentSelectedActor = InParentWidget != nullptr ? InParentWidget->GetOwner() :
			(SelectedActors.Num() > 0 ? SelectedActors[0] : nullptr);
			if (PrefabsToLoad.Num() > 0 || PotentialActorClassesToLoad.Num() > 0 || PotentialStaticMeshesToLoad.Num() > 0)
			{
				if (CurrentSelectedActor == nullptr)
				{
					auto MsgText = LOCTEXT("Error_NeedParentNode", "Please select a actor as parent actor");
					FMessageDialog::Open(EAppMsgType::Ok, MsgText);
					return FReply::Unhandled();
				}
				if (CurrentSelectedActor == GetPreviewScene().GetRootAgentActor())
				{
					auto MsgText = FText::Format(LOCTEXT("Error_RootCannotBeParentNode", "{0} cannot be parent actor of child prefab, please choose another actor."), FText::FromString(FLGUIPrefabEditorScene::RootAgentActorName));
					FMessageDialog::Open(EAppMsgType::Ok, MsgText);
					return FReply::Unhandled();
				}
			}
			else
			{
				return FReply::Unhandled();
			}

			GEditor->BeginTransaction(LOCTEXT("CreateFromAssetDrop_Transaction", "LGUI Create from asset drop"));
			TArray<AActor*> CreatedActorArray;
			if (PrefabsToLoad.Num() > 0)
			{
				PrefabHelperObject->SetCanNotifyAttachment(false);
				for (auto& PrefabAsset : PrefabsToLoad)
				{
					TMap<FGuid, TObjectPtr<UObject>> SubPrefabMapGuidToObject;
					TMap<TObjectPtr<AActor>, FLexUISubPrefabData> SubSubPrefabMap;
					auto LoadedSubPrefabRootActor = PrefabAsset->LoadPrefabWithExistingObjects(GetPreviewScene().GetWorld()
						, CurrentSelectedActor->GetRootComponent()
						, SubPrefabMapGuidToObject, SubSubPrefabMap
					);

					PrefabHelperObject->MakePrefabAsSubPrefab(PrefabAsset, LoadedSubPrefabRootActor, SubPrefabMapGuidToObject, {});
					CreatedActorArray.Add(LoadedSubPrefabRootActor);
				}
				OnApply();
				PrefabHelperObject->SetCanNotifyAttachment(true);

				if (OutlinerPtr.IsValid())
				{
					ULexUIManagerObject::AddOneShotTickFunction([=, this] {
						for (auto& Actor : CreatedActorArray)
						{
							//OutlinerPtr->UnexpandActorForDragDroppedPrefab(Actor);
						}
						OutlinerPtr->RequestRefresh();
						}, 1);//delay execute, because the outliner not create actor yet
				}
			}
			if (PotentialActorClassesToLoad.Num() > 0)
			{
				for (auto& ActorClass : PotentialActorClassesToLoad)
				{
					if (auto Actor = this->GetWorld()->SpawnActor<AActor>(ActorClass, FActorSpawnParameters()))
					{
						if (auto RootComp = Actor->GetRootComponent())
						{
							RootComp->AttachToComponent(CurrentSelectedActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
							CreatedActorArray.Add(Actor);
						}
						else
						{
							Actor->ConditionalBeginDestroy();
						}
					}
				}
			}
			if (PotentialStaticMeshesToLoad.Num() > 0)
			{
				for (auto& Mesh : PotentialStaticMeshesToLoad)
				{
					auto MeshActor = this->GetWorld()->SpawnActor<AStaticMeshActor>();
					MeshActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
					MeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
					MeshActor->GetStaticMeshComponent()->AttachToComponent(CurrentSelectedActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
					MeshActor->SetActorLabel(Mesh->GetName());
					CreatedActorArray.Add(MeshActor);
				}
			}
			if (CreatedActorArray.Num() > 0)
			{
				ULexUIManagerWorldSubsystem::GetSelection(GetWorld())->SelectNone();
				for (auto& Actor : CreatedActorArray)
				{
					ULexUIManagerWorldSubsystem::GetSelection(GetWorld())->SelectActor(Actor);
				}
			}
			GEditor->EndTransaction();
		}

		return FReply::Handled();
	}
	return FReply::Unhandled();
}



#undef LOCTEXT_NAMESPACE