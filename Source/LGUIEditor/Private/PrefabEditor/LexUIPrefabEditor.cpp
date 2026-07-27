// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabEditor.h"
#include "LexUIPrefabEditorViewport.h"
#include "LexUIPrefabEditorDetails.h"
#include "LexUIPrefabRawDataViewer.h"
#include "LexUIPrefabOverridesViewer.h"
#include "LexUIPrefabBehaviourViewer.h"
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
#include "LexUIBehaviourEditorBackend.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Kismet2/SClassPickerDialog.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintEditor.h"//SummonSearchUI (Find References)
#include "EdGraph/EdGraph.h"
#include "K2Node_CustomEvent.h"
#include "UMGStyle.h"
#include "Core/LexUIManager.h"
#include "Core/LexUISettings.h"
#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexLayoutContainerGrid.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Interaction/LexContentWidget.h"
#include "Framework/Commands/GenericCommands.h"
#include "PrefabSystem/LexUIPrefabInstanceScene.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PrefabAnimation/LexUIPrefabSequenceEditor.h"
#include "ScopedTransaction.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "SourceCodeNavigation.h"
#include "Event/LexUIEventDelegate.h"
#include "Utils/LexUIUtils.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequenceComponent.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequence.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "Logging/TokenizedMessage.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"

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
	static const FName PrefabOverridesViewerID;
	static const FName PrefabBehaviourViewerID;
	static const FName CompilerResultsID;
};

const FName FLexUIPrefabEditorTabs::DetailsID(TEXT("Details"));
const FName FLexUIPrefabEditorTabs::ViewportID(TEXT("Viewport"));
const FName FLexUIPrefabEditorTabs::OutlinerID(TEXT("Outliner"));
const FName FLexUIPrefabEditorTabs::PaletteID(TEXT("Palette"));
const FName FLexUIPrefabEditorTabs::SequencerID(TEXT("Sequencer"));
const FName FLexUIPrefabEditorTabs::PrefabRawDataViewerID(TEXT("PrefabRawDataViewer"));
const FName FLexUIPrefabEditorTabs::PrefabOverridesViewerID(TEXT("PrefabOverridesViewer"));
const FName FLexUIPrefabEditorTabs::PrefabBehaviourViewerID(TEXT("PrefabBehaviourViewer"));
const FName FLexUIPrefabEditorTabs::CompilerResultsID(TEXT("CompilerResults"));

namespace LexUIPrefabEditorLocal
{
	class FBehaviourClassFilter final : public IClassViewerFilter
	{
	public:
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass,
			TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return InClass != nullptr
				&& InClass->IsChildOf(ULexUIBehaviour::StaticClass())
				&& !InClass->HasAnyClassFlags(DisallowedFlags);
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions,
			const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData,
			TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return InUnloadedClassData->IsChildOf(ULexUIBehaviour::StaticClass())
				&& !InUnloadedClassData->HasAnyClassFlags(DisallowedFlags);
		}

		static constexpr EClassFlags DisallowedFlags = CLASS_Abstract | CLASS_Deprecated
			| CLASS_NewerVersionExists | CLASS_Hidden | CLASS_HideDropDown | CLASS_Transient;
	};

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
}
FLexUIPrefabEditor::~FLexUIPrefabEditor()
{
	PrefabEditorInstanceCollection.Remove(this);

	UWorld* EditorWorld = nullptr;
	if (IsValid(PrefabBeingEdited))
	{
		if (FLexUIPrefabInstanceScene* PreviewScene = PrefabBeingEdited->GetPrefabInstanceScene())
		{
			EditorWorld = PreviewScene->GetWorld();
		}
	}
	if (ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(EditorWorld))
	{
		Manager->OnLexUIWidgetOutlinerChanged.RemoveAll(this);
		Manager->EventOnOutlineChanged.RemoveAll(this);
		Manager->bShouldTickInEditor = false;
	}
	if (ULexUISelection* Selection = ULexUISelection::GetInstance(EditorWorld))
	{
		Selection->OnSelectionChanged.RemoveAll(this);
		Selection->SelectNone();
	}

	if (bRegisteredForUndo && GEditor)
	{
		GEditor->UnregisterForUndo(this);
		bRegisteredForUndo = false;
	}
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
	void SyncWidgetRegisterStateAfterTransaction(ULexWidget* RootAgent, UWorld* EditorWorld)
	{
		if (!IsValid(RootAgent) || !IsValid(EditorWorld))
		{
			return;
		}

		RootAgent->EnsureChildrenAfterTransaction();
		TArray<ULexWidget*> ReachableWidgets;
		ULexWidget::CollectChildrenWidgets(RootAgent, ReachableWidgets);

		TSet<ULexWidget*> AllWidgets;
		ForEachObjectOfClass(ULexWidget::StaticClass(), [&](UObject* Object)
		{
			if (auto Widget = Cast<ULexWidget>(Object))
			{
				if (Widget->GetWorld() == EditorWorld)
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
		.SetDisplayName(LOCTEXT("OutlinerTabLabel", "Hierarchy"))
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

	InTabManager->RegisterTabSpawner(FLexUIPrefabEditorTabs::PrefabOverridesViewerID, FOnSpawnTab::CreateSP(this, &FLexUIPrefabEditor::SpawnTab_PrefabOverridesViewer))
		.SetDisplayName(LOCTEXT("PrefabOverridesViewerTabLabel", "Overrides"))
		.SetGroup(WorkspaceMenuCategoryRef)
		;

	InTabManager->RegisterTabSpawner(FLexUIPrefabEditorTabs::PrefabBehaviourViewerID, FOnSpawnTab::CreateSP(this, &FLexUIPrefabEditor::SpawnTab_PrefabBehaviourViewer))
		.SetDisplayName(LOCTEXT("PrefabBehaviourViewerTabLabel", "Behaviour"))
		.SetGroup(WorkspaceMenuCategoryRef)
		;

	InTabManager->RegisterTabSpawner(FLexUIPrefabEditorTabs::CompilerResultsID, FOnSpawnTab::CreateSP(this, &FLexUIPrefabEditor::SpawnTab_CompilerResults))
		.SetDisplayName(LOCTEXT("CompilerResultsTabLabel", "Compiler Results"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Message"));
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
	InTabManager->UnregisterTabSpawner(FLexUIPrefabEditorTabs::PrefabOverridesViewerID);
	InTabManager->UnregisterTabSpawner(FLexUIPrefabEditorTabs::PrefabBehaviourViewerID);
	InTabManager->UnregisterTabSpawner(FLexUIPrefabEditorTabs::CompilerResultsID);
}

void FLexUIPrefabEditor::PostUndo(bool bSuccess)
{
	HandlePostTransaction(bSuccess);
}
void FLexUIPrefabEditor::PostRedo(bool bSuccess)
{
	HandlePostTransaction(bSuccess);
}

void FLexUIPrefabEditor::HandlePostTransaction(bool bSuccess)
{
	if (!bSuccess || !IsValid(PrefabBeingEdited))
	{
		return;
	}

	FLexUIPrefabInstanceScene* PreviewScene = PrefabBeingEdited->GetPrefabInstanceScene();
	if (!PreviewScene)
	{
		return;
	}
	UWorld* EditorWorld = PreviewScene->GetWorld();
	ULexWidget* RootAgent = PreviewScene->GetRootAgent();
	if (!IsValid(EditorWorld) || !IsValid(RootAgent))
	{
		return;
	}

	SyncWidgetRegisterStateAfterTransaction(RootAgent, EditorWorld);
	ApplyDesignerState();
	ULexUIManagerWorldSubsystem::RefreshAllUI(EditorWorld);
	if (ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(EditorWorld))
	{
		Manager->MarkLexUIWidgetOutlinerChanged();
	}
	if (ULexUISelection* Selection = ULexUISelection::GetInstance(EditorWorld))
	{
		SelectedWidgets = Selection->GetSelectedWidgets();
	}
	else
	{
		SelectedWidgets.Reset();
	}
	OnSelectionChanged.Broadcast();
	if (OutlinerPtr.IsValid())
	{
		OutlinerPtr->RequestRefresh();
	}
}

void FLexUIPrefabEditor::InitPrefabEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost >& InitToolkitHost, ULexUIPrefab* InPrefab)
{
	PrefabBeingEdited = InPrefab;
	FMessageLogInitializationOptions LogOptions;
	LogOptions.bShowFilters = true;
	LogOptions.bShowPages = true;
	LogOptions.bAllowClear = true;
	LogOptions.bDiscardDuplicates = false;
	LogOptions.bShowInLogWindow = false;
	LogOptions.MaxPageCount = 50;
	CompilerResultsListing = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog")
		.CreateLogListing(*FString::Printf(TEXT("LexUIPrefabCompiler_%p"), this), LogOptions);

	FLexUIPrefabEditorCommand::Register();

	PrefabBeingEdited->EnsureInstanceObjects();

	TSharedPtr<FLexUIPrefabEditor> PrefabEditorPtr = SharedThis(this);

	ViewportPtr = SNew(SLexUIPrefabEditorViewport, PrefabEditorPtr, PrefabBeingEdited->PrefabDataForPrefabEditor.ViewMode);
	
	DetailsPtr = SNew(SLexUIPrefabEditorDetails, GetWorld());

	PrefabRawDataViewer = SNew(SLexUIPrefabRawDataViewer, PrefabEditorPtr, PrefabBeingEdited);

	PrefabOverridesViewer = SNew(SLexUIPrefabOverridesViewer, PrefabEditorPtr, PrefabBeingEdited);

	PrefabBehaviourViewer = SNew(SLexUIPrefabBehaviourViewer, PrefabEditorPtr, PrefabBeingEdited);
	
	ULexUIManagerWorldSubsystem::GetInstance(GetWorld())->OnLexUIWidgetOutlinerChanged.AddSPLambda(this, [=, this]()
	{
		if (OutlinerPtr.IsValid())
		{
			OutlinerPtr->RequestRefresh();
		}
		if (DetailsPtr.IsValid())
		{
			DetailsPtr->Refresh();
		}
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
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_LexUIPrefabEditor_Layout_v3")
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
					->AddTab(FLexUIPrefabEditorTabs::CompilerResultsID, ETabState::OpenedTab)
				)
			)
		);

	InitAssetEditor(Mode, InitToolkitHost, PrefabEditorAppName, StandaloneDefaultLayout, true, true, PrefabBeingEdited);
	if (!bRegisteredForUndo && GEditor)
	{
		GEditor->RegisterForUndo(this);
		bRegisteredForUndo = true;
	}

	// After opening a prefab, broadcast event to LexUIPrefabSequencerEditor
	FLexUIEditorTools::OnEditingPrefabChanged.Broadcast(GetLoadedRootWidget());
	RunInitialReferenceValidation();
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

ULexUIPrefabSequence* FLexUIPrefabEditor::GetAnimationBeingEdited()const
{
	return SequencerPtr.IsValid() ? SequencerPtr->GetPrefabSequence() : nullptr;
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

void FLexUIPrefabEditor::OnOpenOverridesViewerPanel()
{
	this->InvokeTab(FLexUIPrefabEditorTabs::PrefabOverridesViewerID);
}

void FLexUIPrefabEditor::OnOpenBehaviourViewerPanel()
{
	this->InvokeTab(FLexUIPrefabEditorTabs::PrefabBehaviourViewerID);
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

	UBlueprint* Blueprint = nullptr;
	if (ULexUIBehaviour* PrimaryBehaviour = GetPrimaryBehaviour())
	{
		Blueprint = Cast<UBlueprint>(PrimaryBehaviour->GetClass()->ClassGeneratedBy);
		if (Blueprint == nullptr)
		{
			return nullptr;
		}
	}
	if (Blueprint == nullptr)
	{
		Blueprint = LexUIPrefabBehaviourUtils::CreateBehaviourBlueprint(PrefabBeingEdited, RootWidget);
		if (Blueprint == nullptr)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("Error_CreateBehaviourBlueprint", "Failed to create the behaviour blueprint."));
			return nullptr;
		}
		if (!AssignBehaviourClass(Blueprint->GeneratedClass))
		{
			return nullptr;
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
	UClass* BehaviourClass = GetEffectiveBehaviourClass();
	if (BehaviourClass == nullptr)
	{
		CreateAndAssignBehaviourBlueprint();
		return;
	}
	if (TSharedPtr<ILexUIBehaviourEditorBackend> Backend = GetBehaviourEditorBackend())
	{
		if (Backend->OpenClass(BehaviourClass))
		{
			return;
		}
	}
	if (UObject* GeneratedBy = BehaviourClass->ClassGeneratedBy)
	{
		if (GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(GeneratedBy))
		{
			return;
		}
	}
	FSourceCodeNavigation::NavigateToClass(BehaviourClass);
}

void FLexUIPrefabEditor::CreateAndAssignBehaviourBlueprint()
{
	ULexWidget* RootWidget = GetLoadedRootWidget();
	if (!IsValid(RootWidget) || !IsValid(PrefabBeingEdited))return;

	if (UBlueprint* ExistingBlueprint = Cast<UBlueprint>(GetEffectiveBehaviourClass() ? GetEffectiveBehaviourClass()->ClassGeneratedBy : nullptr))
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ExistingBlueprint);
		return;
	}

	UBlueprint* Blueprint = LexUIPrefabBehaviourUtils::CreateBehaviourBlueprint(PrefabBeingEdited, RootWidget);
	if (Blueprint == nullptr || !AssignBehaviourClass(Blueprint->GeneratedClass))
	{
		FNotificationInfo Info(LOCTEXT("Error_CreateAndAssignBehaviourBlueprint", "Failed to create or assign the behaviour Blueprint."));
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.ErrorWithColor"));
		Info.ExpireDuration = 6.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
}

UClass* FLexUIPrefabEditor::GetEffectiveBehaviourClass() const
{
	if (!IsValid(PrefabBeingEdited))return nullptr;
	if (UClass* ExplicitClass = PrefabBeingEdited->GetBehaviourClass())
	{
		return ExplicitClass;
	}
	if (ULexWidget* RootWidget = const_cast<FLexUIPrefabEditor*>(this)->GetLoadedRootWidget())
	{
		if (ULexUIBehaviour* LegacyCompanion = LexUIPrefabBehaviourUtils::FindBehaviourComponent(RootWidget, PrefabBeingEdited))
		{
			return LegacyCompanion->GetClass();
		}
	}
	return nullptr;
}

ULexUIBehaviour* FLexUIPrefabEditor::GetPrimaryBehaviour() const
{
	ULexWidget* RootWidget = const_cast<FLexUIPrefabEditor*>(this)->GetLoadedRootWidget();
	UClass* BehaviourClass = GetEffectiveBehaviourClass();
	if (!IsValid(RootWidget) || !IsValid(BehaviourClass))return nullptr;

	ULexUIBehaviour* Match = nullptr;
	for (ULexUIBehaviour* Component : RootWidget->GetAllComponents())
	{
		if (IsValid(Component) && Component->GetClass() == BehaviourClass)
		{
			if (Match != nullptr)return nullptr;
			Match = Component;
		}
	}
	return Match;
}

TSharedPtr<ILexUIBehaviourEditorBackend> FLexUIPrefabEditor::GetBehaviourEditorBackend() const
{
	return FLexUIBehaviourEditorBackendRegistry::Get().FindBackend(GetEffectiveBehaviourClass());
}

bool FLexUIPrefabEditor::CanAuthorBehaviour() const
{
	UClass* BehaviourClass = GetEffectiveBehaviourClass();
	if (BehaviourClass == nullptr)return true;
	TSharedPtr<ILexUIBehaviourEditorBackend> Backend = GetBehaviourEditorBackend();
	return Backend.IsValid()
		&& (Backend->CanPromoteToVariable(BehaviourClass) || Backend->CanAddEventHandler(BehaviourClass));
}

void FLexUIPrefabEditor::PickBehaviourClass()
{
	FClassViewerInitializationOptions Options;
	Options.DisplayMode = EClassViewerDisplayMode::TreeView;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.bShowNoneOption = true;
	Options.bShowUnloadedBlueprints = true;
	Options.bEnableClassDynamicLoading = true;
	Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::Dynamic;
	Options.InitiallySelectedClass = GetEffectiveBehaviourClass();
	Options.ClassFilters.Add(MakeShared<LexUIPrefabEditorLocal::FBehaviourClassFilter>());

	UClass* ChosenClass = GetEffectiveBehaviourClass();
	if (!SClassPickerDialog::PickClass(LOCTEXT("PickPrefabBehaviourClass", "Pick Root Behaviour for LexUI Prefab"),
		Options, ChosenClass, ULexUIBehaviour::StaticClass()))
	{
		return;
	}
	if (ChosenClass == nullptr)
	{
		RemovePrimaryBehaviour();
	}
	else
	{
		AssignBehaviourClass(ChosenClass);
	}
}

bool FLexUIPrefabEditor::AssignBehaviourClass(UClass* InClass)
{
	ULexWidget* RootWidget = GetLoadedRootWidget();
	ULexUIPrefabHelperObject* Helper = GetPrefabHelperObject();
	if (!IsValid(InClass) || !InClass->IsChildOf(ULexUIBehaviour::StaticClass())
		|| InClass->HasAnyClassFlags(LexUIPrefabEditorLocal::FBehaviourClassFilter::DisallowedFlags)
		|| !IsValid(RootWidget) || !IsValid(Helper))
	{
		return false;
	}

	TArray<ULexUIBehaviour*> MatchingComponents;
	for (ULexUIBehaviour* Component : RootWidget->GetAllComponents())
	{
		if (IsValid(Component) && Component->GetClass() == InClass)
		{
			MatchingComponents.Add(Component);
		}
	}
	if (MatchingComponents.Num() > 1)
	{
		PendingBehaviourWarnings.Add(FString::Printf(TEXT("Root widget has %d instances of primary behaviour class '%s'. Remove duplicates before assigning it."), MatchingComponents.Num(), *InClass->GetName()));
		FNotificationInfo Info(FText::FromString(PendingBehaviourWarnings.Last()));
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
		Info.ExpireDuration = 7.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("AssignPrefabBehaviourTransaction", "Assign Prefab Behaviour"));
	PrefabBeingEdited->Modify();
	Helper->Modify();
	RootWidget->SetFlags(RF_Transactional);
	RootWidget->Modify();

	ULexUIBehaviour* OldBehaviour = GetPrimaryBehaviour();
	ULexUIBehaviour* NewBehaviour = MatchingComponents.IsEmpty() ? nullptr : MatchingComponents[0];
	const bool bCreatedNew = NewBehaviour == nullptr;
	if (bCreatedNew)
	{
		NewBehaviour = RootWidget->AddComponent(InClass);
		if (!IsValid(NewBehaviour))return false;
		NewBehaviour->SetFlags(RF_Transactional);
		NewBehaviour->Modify();
	}

	if (IsValid(OldBehaviour) && OldBehaviour != NewBehaviour)
	{
		if (!ReplacePrimaryBehaviour(OldBehaviour, NewBehaviour, bCreatedNew))return false;
	}

	PrefabBeingEdited->SetBehaviourClass(InClass);
	Helper->SetAnythingDirty();
	FLexUIUtils::NotifyPropertyChanged(RootWidget, ULexWidget::GetPropertyName_Components());
	SelectWidgets(TSet<ULexWidget*>{RootWidget}, false);
	return true;
}

bool FLexUIPrefabEditor::ReplacePrimaryBehaviour(ULexUIBehaviour* InOldBehaviour, ULexUIBehaviour* InNewBehaviour, bool bNewBehaviourWasCreated)
{
	ULexWidget* RootWidget = GetLoadedRootWidget();
	ULexUIPrefabHelperObject* Helper = GetPrefabHelperObject();
	if (!IsValid(InOldBehaviour) || !IsValid(InNewBehaviour) || !IsValid(RootWidget) || !IsValid(Helper))return false;

	const int32 OldIndex = RootWidget->GetAllComponents().IndexOfByKey(InOldBehaviour);
	const FName OldName = InOldBehaviour->GetFName();
	InOldBehaviour->SetFlags(RF_Transactional);
	InOldBehaviour->Modify();
	InNewBehaviour->SetFlags(RF_Transactional);
	InNewBehaviour->Modify();

	if (bNewBehaviourWasCreated)
	{
		UObject* OldDefault = InOldBehaviour->GetClass()->GetDefaultObject();
		for (TFieldIterator<FProperty> It(InOldBehaviour->GetClass()); It; ++It)
		{
			FProperty* OldProperty = *It;
			if (!OldProperty->HasAnyPropertyFlags(CPF_Edit)
				|| OldProperty->HasAnyPropertyFlags(CPF_Transient | CPF_DisableEditOnInstance))continue;

			FProperty* NewProperty = FindFProperty<FProperty>(InNewBehaviour->GetClass(), OldProperty->GetFName());
			if (NewProperty != nullptr && OldProperty->SameType(NewProperty))
			{
				void* Destination = NewProperty->ContainerPtrToValuePtr<void>(InNewBehaviour);
				const void* Source = OldProperty->ContainerPtrToValuePtr<void>(InOldBehaviour);
				NewProperty->CopyCompleteValue(Destination, Source);
			}
			else if (OldDefault != nullptr && !OldProperty->Identical_InContainer(InOldBehaviour, OldDefault))
			{
				PendingBehaviourWarnings.Add(FString::Printf(TEXT("Behaviour property '%s.%s' could not be migrated to '%s'."),
					*InOldBehaviour->GetClass()->GetName(), *OldProperty->GetName(), *InNewBehaviour->GetClass()->GetName()));
			}
		}
	}

	TArray<ULexWidget*> WidgetStack;
	WidgetStack.Add(RootWidget);
	while (!WidgetStack.IsEmpty())
	{
		ULexWidget* Widget = WidgetStack.Pop();
		for (ULexUIBehaviour* Component : Widget->GetAllComponents())
		{
			if (!IsValid(Component))continue;
			for (TFieldIterator<FStructProperty> It(Component->GetClass()); It; ++It)
			{
				FStructProperty* StructProperty = *It;
				if (StructProperty->Struct == FLexUIEventDelegate::StaticStruct())
				{
					FLexUIEventDelegate* Event = StructProperty->ContainerPtrToValuePtr<FLexUIEventDelegate>(Component);
					Event->ReplaceBindingTarget(InOldBehaviour, InNewBehaviour);
				}
			}
		}
		WidgetStack.Append(Widget->GetChildren());
	}

	TMap<UObject*, UObject*> ReplacementMap;
	ReplacementMap.Add(InOldBehaviour, InNewBehaviour);
	for (const TPair<FGuid, TObjectPtr<UObject>>& Pair : Helper->MapGuidToObject)
	{
		UObject* Object = Pair.Value.Get();
		if (IsValid(Object) && Object != RootWidget && Object != InOldBehaviour)
		{
			FArchiveReplaceObjectRef<UObject> ReplaceReferences(Object, ReplacementMap,
				EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
		}
	}

	FGuid OldGuid;
	FGuid NewGuid;
	for (const TPair<FGuid, TObjectPtr<UObject>>& Pair : Helper->MapGuidToObject)
	{
		if (Pair.Value == InOldBehaviour)OldGuid = Pair.Key;
		if (Pair.Value == InNewBehaviour)NewGuid = Pair.Key;
	}
	if (OldGuid.IsValid())
	{
		if (NewGuid.IsValid() && NewGuid != OldGuid)Helper->MapGuidToObject.Remove(NewGuid);
		Helper->MapGuidToObject.FindOrAdd(OldGuid) = InNewBehaviour;
	}

	RootWidget->RemoveComponent(InOldBehaviour);
	if (bNewBehaviourWasCreated)
	{
		const FName ReplacedName = MakeUniqueObjectName(RootWidget, InOldBehaviour->GetClass(),
			FName(*(OldName.ToString() + TEXT("_Replaced"))));
		InOldBehaviour->Rename(*ReplacedName.ToString(), RootWidget, REN_DontCreateRedirectors);
		InNewBehaviour->Rename(*OldName.ToString(), RootWidget, REN_DontCreateRedirectors);
		// Refresh serialized event helper names after preserving the old component object name.
		TArray<ULexWidget*> RefreshStack;
		RefreshStack.Add(RootWidget);
		while (!RefreshStack.IsEmpty())
		{
			ULexWidget* Widget = RefreshStack.Pop();
			for (ULexUIBehaviour* Component : Widget->GetAllComponents())
			{
				if (!IsValid(Component))continue;
				for (TFieldIterator<FStructProperty> It(Component->GetClass()); It; ++It)
				{
					FStructProperty* StructProperty = *It;
					if (StructProperty->Struct == FLexUIEventDelegate::StaticStruct())
					{
						StructProperty->ContainerPtrToValuePtr<FLexUIEventDelegate>(Component)->ReplaceBindingTarget(InNewBehaviour, InNewBehaviour);
					}
				}
			}
			RefreshStack.Append(Widget->GetChildren());
		}
	}
	if (OldIndex != INDEX_NONE)RootWidget->MoveComponentToIndex(InNewBehaviour, OldIndex);
	return true;
}

void FLexUIPrefabEditor::RemovePrimaryBehaviour()
{
	ULexUIBehaviour* OldBehaviour = GetPrimaryBehaviour();
	if (!IsValid(PrefabBeingEdited))return;
	if (FMessageDialog::Open(EAppMsgType::YesNo,
		LOCTEXT("RemovePrimaryBehaviourConfirm", "Remove the primary Behaviour component from this prefab? The script asset will not be deleted.")) != EAppReturnType::Yes)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("RemovePrefabBehaviourTransaction", "Remove Prefab Behaviour"));
	PrefabBeingEdited->Modify();
	if (IsValid(OldBehaviour))
	{
		ULexWidget* RootWidget = GetLoadedRootWidget();
		ULexUIPrefabHelperObject* Helper = GetPrefabHelperObject();
		if (!IsValid(RootWidget) || !IsValid(Helper))
		{
			return;
		}
		RootWidget->Modify();
		OldBehaviour->Modify();
		TMap<UObject*, UObject*> ReplacementMap;
		ReplacementMap.Add(OldBehaviour, nullptr);
		for (const TPair<FGuid, TObjectPtr<UObject>>& Pair : Helper->MapGuidToObject)
		{
			UObject* Object = Pair.Value.Get();
			if (IsValid(Object) && Object != RootWidget && Object != OldBehaviour)
			{
				FArchiveReplaceObjectRef<UObject> ReplaceReferences(Object, ReplacementMap,
					EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
			}
		}
		for (auto It = Helper->MapGuidToObject.CreateIterator(); It; ++It)
		{
			if (It.Value() == OldBehaviour)It.RemoveCurrent();
		}
		RootWidget->RemoveComponent(OldBehaviour);
		FLexUIUtils::NotifyPropertyChanged(RootWidget, ULexWidget::GetPropertyName_Components());
		Helper->SetAnythingDirty();
	}
	PrefabBeingEdited->SetBehaviourClass(nullptr);
}

void FLexUIPrefabEditor::PromoteToBehaviourVariable(UObject* InTarget)
{
	if (InTarget == nullptr)return;
	if (GetEffectiveBehaviourClass() == nullptr && GetOrCreateBehaviourBlueprint() == nullptr)return;
	ULexUIBehaviour* PrimaryBehaviour = GetPrimaryBehaviour();
	TSharedPtr<ILexUIBehaviourEditorBackend> Backend = GetBehaviourEditorBackend();
	if (!IsValid(PrimaryBehaviour) || !Backend.IsValid() || !Backend->CanPromoteToVariable(PrimaryBehaviour->GetClass()))
	{
		FNotificationInfo Info(LOCTEXT("BehaviourBackendCannotPromote", "This Behaviour backend cannot generate variables. Declare the reflected property in the script, then Apply to auto-bind it."));
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
		Info.ExpireDuration = 7.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	FText Message;
	const bool bSuccess = Backend->PromoteToVariable(GetLoadedRootWidget(), PrimaryBehaviour, InTarget, Message);
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

bool FLexUIPrefabEditor::CanAddEventHandler(ELexUIBehaviourHandlerType InHandlerType) const
{
	UClass* BehaviourClass = GetEffectiveBehaviourClass();
	if (BehaviourClass == nullptr)
	{
		return true;
	}
	TSharedPtr<ILexUIBehaviourEditorBackend> Backend = GetBehaviourEditorBackend();
	return Backend.IsValid() && Backend->CanAddEventHandler(BehaviourClass, InHandlerType);
}

void FLexUIPrefabEditor::AddEventHandler(const LexUIPrefabBehaviourUtils::FDiscoveredEvent& InEvent,
	ELexUIBehaviourHandlerType InHandlerType)
{
	if (!IsValid(InEvent.Component) || InEvent.EventProperty == nullptr)
	{
		return;
	}
	FLexUIEventDelegate* LiveEvent = InEvent.EventProperty->ContainerPtrToValuePtr<FLexUIEventDelegate>(InEvent.Component);
	if (LiveEvent->IsBound())
	{
		FNotificationInfo Info(LOCTEXT("EventHandlerAlreadyBound", "This event already has a binding."));
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}
	if (GetEffectiveBehaviourClass() == nullptr && GetOrCreateBehaviourBlueprint() == nullptr)return;
	ULexUIBehaviour* PrimaryBehaviour = GetPrimaryBehaviour();
	TSharedPtr<ILexUIBehaviourEditorBackend> Backend = GetBehaviourEditorBackend();
	if (!IsValid(PrimaryBehaviour) || !Backend.IsValid() || !Backend->CanAddEventHandler(PrimaryBehaviour->GetClass(), InHandlerType))
	{
		FNotificationInfo Info(LOCTEXT("BehaviourBackendCannotAddEvent", "This Behaviour backend cannot generate event handlers. Declare a compatible reflected function in the script and bind it in Details."));
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
		Info.ExpireDuration = 7.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	FText Message;
	const FName HandlerName = Backend->AddEventHandler(GetLoadedRootWidget(), PrimaryBehaviour,
		InEvent.Component, InEvent.EventProperty->GetFName(), InHandlerType, Message);
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
		if (UBlueprint* Blueprint = Cast<UBlueprint>(PrimaryBehaviour->GetClass()->ClassGeneratedBy))
		{
			if (InHandlerType == ELexUIBehaviourHandlerType::Function)
			{
				if (UEdGraph* FuncGraph = FindObject<UEdGraph>(Blueprint, *HandlerName.ToString()))
				{
					FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(FuncGraph, false);
					return;
				}
			}
			else if (UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint))
			{
				TArray<UK2Node_CustomEvent*> EventNodes;
				EventGraph->GetNodesOfClass(EventNodes);
				for (UK2Node_CustomEvent* EventNode : EventNodes)
				{
					if (IsValid(EventNode) && EventNode->CustomFunctionName == HandlerName)
					{
						FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(EventNode, false);
						return;
					}
				}
			}
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

	TSet<const ULexWidget*> SelectedSet;
	for (ULexWidget* Widget : Widgets)
	{
		if (IsValid(Widget)) SelectedSet.Add(Widget);
	}
	int32 ValidChildCount = 0;
	int32 ReplacedChildCount = 0;
	for (const ULexWidget* Child : CommonParent->GetChildren())
	{
		if (!IsValid(Child)) continue;
		++ValidChildCount;
		ReplacedChildCount += SelectedSet.Contains(Child) ? 1 : 0;
	}
	const int32 ParentCapacity = CommonParent->GetMaxChildrenCapacity();
	if (ReplacedChildCount == 0
		|| (ParentCapacity != INDEX_NONE && ValidChildCount - ReplacedChildCount + 1 > ParentCapacity))
	{
		return;
	}

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

	FScopedTransaction Transaction(NSLOCTEXT("LexUIPrefabEditor", "WrapWidgets", "Wrap Widgets"));
	CommonParent->Modify();

	struct FWidgetWrapState
	{
		ULexWidget* Widget = nullptr;
		int32 SiblingIndex = INDEX_NONE;
		float Width = 0.0f;
		float Height = 0.0f;
	};
	TArray<FWidgetWrapState> WidgetStates;
	WidgetStates.Reserve(Widgets.Num());
	for (ULexWidget* Widget : Widgets)
	{
		WidgetStates.Add({ Widget, Widget->GetSiblingIndex(), Widget->GetWidth(), Widget->GetHeight() });
	}
	WidgetStates.Sort([](const FWidgetWrapState& A, const FWidgetWrapState& B)
	{
		return A.SiblingIndex < B.SiblingIndex;
	});

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

	auto RestoreOriginalHierarchy = [&]()
	{
		Wrapper->TrySetParent(nullptr, false);
		for (const FWidgetWrapState& State : WidgetStates)
		{
			if (IsValid(State.Widget) && State.Widget->GetParent() != CommonParent)
			{
				State.Widget->SetParentFromPrefab(CommonParent, true, State.SiblingIndex);
			}
			if (IsValid(State.Widget))
			{
				State.Widget->SetWidth(State.Width);
				State.Widget->SetHeight(State.Height);
			}
		}
		Wrapper->DestroyWidget();
		Transaction.Cancel();
	};

	// Capacity is evaluated against the final replacement. Temporarily detach the selected
	// children so a full single-child parent can accept the wrapper itself.
	for (int32 Index = WidgetStates.Num() - 1; Index >= 0; --Index)
	{
		FWidgetWrapState& State = WidgetStates[Index];
		State.Widget->Modify();
		if (!State.Widget->TrySetParent(nullptr, true))
		{
			RestoreOriginalHierarchy();
			return;
		}
	}
	if (!Wrapper->TrySetParent(CommonParent, false, MinSiblingIndex))
	{
		RestoreOriginalHierarchy();
		return;
	}
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
	for (const FWidgetWrapState& State : WidgetStates)
	{
		ULexWidget* W = State.Widget;
		// keepWorld preserves the pivot transform but NOT the anchor-driven size: a stretch-anchored
		// child (AnchorMin != AnchorMax) resolves its width/height against the parent, so moving it
		// under the differently-sized wrapper would resize it. Snapshot the rendered extent and
		// restore it after reparenting (a no-op for fixed-anchor children, whose size is parent-independent).
		if (!W->TrySetParent(Wrapper, true))
		{
			RestoreOriginalHierarchy();
			return;
		}
		W->SetWidth(State.Width);
		W->SetHeight(State.Height);
	}

	// Box wraps follow the project layout mode, like ConfigureScrollBox and the prefab factory:
	// UMG-compatible projects get the UMG box family, legacy projects keep the Lex flex box.
	const bool bWrapUMGLayout = ULexUISettings::GetLayoutMode() == ELexUILayoutMode::UMGCompatible;
	switch (WrapType)
	{
	case ELexUIWrapType::HorizontalBox:
		if (bWrapUMGLayout)
		{
			Wrapper->CreateNewLayoutContainer<ULexLayoutContainerHorizontalBox>();
		}
		else if (auto FlexBox = Wrapper->CreateNewLayoutContainer<ULexLayoutContainerFlexBox>())
		{
			FlexBox->SetDirection(ELexLayoutFlexBoxDirectionType::Horizontal);
		}
		break;
	case ELexUIWrapType::VerticalBox:
		if (bWrapUMGLayout)
		{
			Wrapper->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
		}
		else if (auto FlexBox = Wrapper->CreateNewLayoutContainer<ULexLayoutContainerFlexBox>())
		{
			FlexBox->SetDirection(ELexLayoutFlexBoxDirectionType::Vertical);
		}
		break;
	case ELexUIWrapType::Grid:
		Wrapper->CreateNewLayoutContainer<ULexLayoutContainerGrid>();
		break;
	default:
		break;//plain widget container
	}

	if (auto Helper = GetPrefabHelperObject())
	{
		Helper->Modify();
		Helper->SetAnythingDirty();
	}

	SelectWidgets(TSet<ULexWidget*>{ Wrapper }, false);

	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid()) ViewportPtr->GetViewportClient()->Invalidate();
}

bool FLexUIPrefabEditor::CanFindReferencesForSelectedWidget() const
{
	return SelectedWidgets.Num() == 1 && SelectedWidgets[0].IsValid();
}

void FLexUIPrefabEditor::FindReferencesForSelectedWidget()
{
	if (!CanFindReferencesForSelectedWidget())return;
	ULexWidget* Target = SelectedWidgets[0].Get();
	UBlueprint* Blueprint = LexUIPrefabBehaviourUtils::FindBehaviourBlueprint(GetLoadedRootWidget(), PrefabBeingEdited);
	if (!IsValid(Blueprint))
	{
		FNotificationInfo Info(LOCTEXT("FindReferencesNoBehaviour", "This prefab has no companion behaviour blueprint to search."));
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	auto AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	AssetEditorSubsystem->OpenEditorForAsset(Blueprint);
	IAssetEditorInstance* OpenedEditor = AssetEditorSubsystem->FindEditorForAsset(Blueprint, /*bFocusIfOpen*/true);
	if (OpenedEditor == nullptr)return;
	// Quoted, like UMG: an unquoted name matches every node whose text merely contains it.
	const FString SearchTerm = FString::Printf(TEXT("\"%s\""), *LexUIPrefabBehaviourUtils::MakeVariableNameForTarget(Target));
	static_cast<FBlueprintEditor*>(OpenedEditor)->SummonSearchUI(/*bSetFindWithinBlueprint*/true, SearchTerm);
}

void FLexUIPrefabEditor::ReplaceSelectedWidgetLayout(UClass* PanelClass)
{
	if (!IsValid(PanelClass) || !PanelClass->IsChildOf(ULexLayoutContainer::StaticClass()))return;
	const TArray<TWeakObjectPtr<ULexWidget>>& Selection = GetSelectedWidgets();
	if (Selection.Num() != 1)return;
	ULexWidget* Target = Selection[0].Get();
	if (!IsValid(Target))return;
	ULexLayoutContainer* Existing = Target->GetLayoutContainer();
	// "Replace" means replace; offering it on a widget with no panel would be an "add", which is
	// what the palette and the details panel are for.
	if (!IsValid(Existing) || Existing->GetClass() == PanelClass)return;
	if (WidgetBelongsToSubPrefab(Target))
	{
		// The container belongs to the sub prefab asset, so a swap here would be reverted the next
		// time the sub prefab is applied.
		FNotificationInfo Info(LOCTEXT("ReplaceLayoutInsideSubPrefab", "Open the sub prefab to change the panel of a widget it owns."));
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
		Info.ExpireDuration = 6.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	FScopedTransaction Transaction(NSLOCTEXT("LexUIPrefabEditor", "ReplaceWidgetLayout", "Replace Widget Layout"));
	Target->Modify();
	// CreateNewLayoutContainer carries the whole swap: it unregisters the old container, registers
	// the new one, and converts the children's slots to match. Undo is handled by
	// ULexWidget::PostEditUndo, which re-registers whatever container the pointer lands back on.
	if (Target->CreateNewLayoutContainer(PanelClass) == nullptr)
	{
		// The one refusal a designer can hit: single-child panels reject a widget that has several.
		Transaction.Cancel();
		FNotificationInfo Info(FText::Format(
			LOCTEXT("ReplaceLayoutRefused", "{0} cannot hold {1} children."),
			PanelClass->GetDisplayNameText(), FText::AsNumber(Target->GetChildren().Num())));
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
		Info.ExpireDuration = 6.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	if (auto Helper = GetPrefabHelperObject())
	{
		Helper->Modify();
		Helper->SetAnythingDirty();
	}
	if (OutlinerPtr.IsValid())OutlinerPtr->RequestRefresh();
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

void FLexUIPrefabEditor::ValidatePrefabReferences(TArray<FLexUIPrefabCompilerIssue>& OutIssues) const
{
	auto AddIssue = [&OutIssues](ELexUIPrefabCompilerSeverity Severity, FString Message,
		UObject* SourceObject = nullptr, ULexUIPrefabSequence* Animation = nullptr, bool bOpenRawData = false)
	{
		FLexUIPrefabCompilerIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Severity = Severity;
		Issue.Message = MoveTemp(Message);
		Issue.SourceObject = SourceObject;
		Issue.Animation = Animation;
		Issue.bOpenRawData = bOpenRawData;
	};

	ULexUIPrefab* Prefab = PrefabBeingEdited;
	ULexUIPrefabHelperObject* Helper = IsValid(Prefab) ? Prefab->GetPrefabHelperObject() : nullptr;
	ULexWidget* RootWidget = IsValid(Helper) ? Helper->LoadedRootWidget.Get() : nullptr;
	if (!IsValid(Prefab) || !IsValid(Helper) || !IsValid(RootWidget))
	{
		AddIssue(ELexUIPrefabCompilerSeverity::Error, TEXT("The prefab, helper, or loaded root widget is unavailable."));
		return;
	}

	for (int32 Index = 0; Index < Prefab->ReferenceAssetList.Num(); ++Index)
	{
		if (!IsValid(Prefab->ReferenceAssetList[Index]))
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("ReferenceAssetList[%d] is missing."), Index), nullptr, nullptr, true);
		}
	}
	for (int32 Index = 0; Index < Prefab->ReferenceClassList.Num(); ++Index)
	{
		if (!IsValid(Prefab->ReferenceClassList[Index]))
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("ReferenceClassList[%d] is missing."), Index), nullptr, nullptr, true);
		}
	}

	TMap<UObject*, FGuid> FirstGuidByObject;
	for (const TPair<FGuid, TObjectPtr<UObject>>& Pair : Helper->MapGuidToObject)
	{
		if (!Pair.Key.IsValid())
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning, TEXT("Helper GUID map contains an invalid GUID."), Pair.Value.Get(), nullptr, true);
		}
		if (!IsValid(Pair.Value))
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Helper GUID '%s' points to a missing object."), *Pair.Key.ToString()), nullptr, nullptr, true);
			continue;
		}
		if (const FGuid* ExistingGuid = FirstGuidByObject.Find(Pair.Value.Get()))
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Object '%s' is mapped by multiple helper GUIDs (%s and %s)."),
					*Pair.Value->GetName(), *ExistingGuid->ToString(), *Pair.Key.ToString()), Pair.Value.Get(), nullptr, true);
		}
		else
		{
			FirstGuidByObject.Add(Pair.Value.Get(), Pair.Key);
		}
	}

	// Newly added widgets/components receive stable GUIDs during SavePrefab. Missing entries are
	// only suspicious while the helper is otherwise clean; invalid or duplicate existing entries
	// above are always reported.
	const bool bExpectCompleteGuidMap = !Helper->GetAnythingDirty();
	TArray<ULexWidget*> Widgets;
	TSet<const ULexWidget*> VisitedWidgets;
	Widgets.Add(RootWidget);
	for (int32 WidgetIndex = 0; WidgetIndex < Widgets.Num(); ++WidgetIndex)
	{
		ULexWidget* Widget = Widgets[WidgetIndex];
		if (!IsValid(Widget))
		{
			continue;
		}
		if (VisitedWidgets.Contains(Widget))
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Widget hierarchy contains a duplicate or cyclic reference to '%s'."),
					*Widget->GetDisplayName()), Widget, nullptr, true);
			continue;
		}
		VisitedWidgets.Add(Widget);
		for (ULexWidget* Child : Widget->GetChildren())
		{
			if (IsValid(Child))
			{
				if (Child->GetParent() != Widget)
				{
					AddIssue(ELexUIPrefabCompilerSeverity::Warning,
						FString::Printf(TEXT("Widget '%s' is listed under '%s' but points to a different parent."),
							*Child->GetDisplayName(), *Widget->GetDisplayName()), Child, nullptr, true);
				}
				Widgets.Add(Child);
			}
		}
		int32 ValidDirectChildCount = 0;
		for (const ULexWidget* Child : Widget->GetChildren())
		{
			ValidDirectChildCount += IsValid(Child) ? 1 : 0;
		}
		const int32 ChildCapacity = Widget->GetMaxChildrenCapacity();
		if (ChildCapacity != INDEX_NONE && ValidDirectChildCount > ChildCapacity)
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Widget '%s' has %d direct children but its layout/behaviour capacity is %d."),
					*Widget->GetDisplayName(), ValidDirectChildCount, ChildCapacity), Widget);
		}

		int32 ContentWidgetCount = 0;
		for (ULexUIBehaviour* Component : Widget->GetAllComponents())
		{
			ContentWidgetCount += IsValid(Cast<ULexContentWidget>(Component)) ? 1 : 0;
		}
		const ULexLayoutContainer* LayoutContainer = Widget->GetLayoutContainer();
		const bool bRequiresContentWidget = IsValid(LayoutContainer)
			&& (LayoutContainer->IsA<ULexLayoutContainerSizeBox>()
				|| LayoutContainer->IsA<ULexLayoutContainerScaleBox>()
				|| LayoutContainer->IsA<ULexLayoutContainerSafeZone>());
		if (bRequiresContentWidget && ContentWidgetCount == 0)
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Widget '%s' uses a single-child layout but has no ContentWidget behaviour."),
					*Widget->GetDisplayName()), Widget);
		}
		else if (ContentWidgetCount > 1)
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Widget '%s' has %d ContentWidget behaviours; only one is allowed."),
					*Widget->GetDisplayName(), ContentWidgetCount), Widget);
		}
		if (bExpectCompleteGuidMap && !FirstGuidByObject.Contains(Widget))
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Widget '%s' is missing from the helper GUID map."), *Widget->GetDisplayName()), Widget, nullptr, true);
		}
		for (ULexUIBehaviour* Component : Widget->GetAllComponents())
		{
			if (bExpectCompleteGuidMap && IsValid(Component) && !FirstGuidByObject.Contains(Component))
			{
				AddIssue(ELexUIPrefabCompilerSeverity::Warning,
					FString::Printf(TEXT("Component '%s' on widget '%s' is missing from the helper GUID map."),
						*Component->GetName(), *Widget->GetDisplayName()), Component, nullptr, true);
			}
		}
	}
	for (const TPair<FGuid, TObjectPtr<UObject>>& Pair : Helper->MapGuidToObject)
	{
		UObject* Object = Pair.Value.Get();
		if (!IsValid(Object)) continue;
		const ULexWidget* OwningWidget = Cast<ULexWidget>(Object);
		if (!OwningWidget)
		{
			OwningWidget = Object->GetTypedOuter<ULexWidget>();
		}
		if (!IsValid(OwningWidget) || !VisitedWidgets.Contains(OwningWidget))
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Helper GUID '%s' maps '%s', which is outside the prefab root hierarchy."),
					*Pair.Key.ToString(), *Object->GetName()), Object, nullptr, true);
		}
	}

	UClass* BehaviourClass = GetEffectiveBehaviourClass();
	if (BehaviourClass != nullptr)
	{
		if (!BehaviourClass->IsChildOf(ULexUIBehaviour::StaticClass())
			|| BehaviourClass->HasAnyClassFlags(LexUIPrefabEditorLocal::FBehaviourClassFilter::DisallowedFlags))
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("BehaviourClass '%s' is not a concrete usable ULexUIBehaviour class."), *BehaviourClass->GetName()),
				Prefab);
		}
		int32 MatchCount = 0;
		ULexUIBehaviour* Match = nullptr;
		for (ULexUIBehaviour* Component : RootWidget->GetAllComponents())
		{
			if (IsValid(Component) && Component->GetClass() == BehaviourClass)
			{
				++MatchCount;
				Match = Component;
			}
		}
		if (MatchCount == 0)
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Primary Behaviour '%s' is not attached to the prefab root widget."), *BehaviourClass->GetName()), RootWidget);
		}
		else if (MatchCount > 1)
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Primary Behaviour '%s' is ambiguous: %d root components use this class."),
					*BehaviourClass->GetName(), MatchCount), RootWidget);
		}
		else if (bExpectCompleteGuidMap && !FirstGuidByObject.Contains(Match))
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Primary Behaviour '%s' has no helper GUID mapping."), *Match->GetName()), Match, nullptr, true);
		}
	}

	for (const TPair<TObjectPtr<ULexWidget>, FLexUISubPrefabData>& Pair : Helper->SubPrefabMap)
	{
		ULexWidget* SubPrefabRoot = Pair.Key.Get();
		const FLexUISubPrefabData& Data = Pair.Value;
		if (!IsValid(SubPrefabRoot))
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning, TEXT("SubPrefabMap contains a missing root widget."), nullptr, nullptr, true);
		}
		if (!IsValid(Data.PrefabAsset))
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Sub-prefab root '%s' has no valid prefab asset."), *GetNameSafe(SubPrefabRoot)), SubPrefabRoot);
		}
		for (const TPair<FGuid, TObjectPtr<UObject>>& ObjectPair : Data.MapGuidToObject)
		{
			if (!ObjectPair.Key.IsValid() || !IsValid(ObjectPair.Value))
			{
				AddIssue(ELexUIPrefabCompilerSeverity::Warning,
					FString::Printf(TEXT("Sub-prefab '%s' contains an invalid object mapping for GUID '%s'."),
						*GetNameSafe(Data.PrefabAsset), *ObjectPair.Key.ToString()), SubPrefabRoot, nullptr, true);
			}
		}
		for (const FLexUIPrefabOverrideParameterData& Override : Data.ObjectOverrideParameterArray)
		{
			UObject* OverrideObject = Override.Object.Get();
			if (!IsValid(OverrideObject))
			{
				AddIssue(ELexUIPrefabCompilerSeverity::Warning,
					FString::Printf(TEXT("Sub-prefab '%s' contains an override for a missing object."), *GetNameSafe(Data.PrefabAsset)),
					SubPrefabRoot, nullptr, true);
				continue;
			}
			for (FName PropertyName : Override.MemberPropertyNames)
			{
				FProperty* Property = FindFProperty<FProperty>(OverrideObject->GetClass(), PropertyName);
				if (Property == nullptr)
				{
					AddIssue(ELexUIPrefabCompilerSeverity::Warning,
						FString::Printf(TEXT("Override property '%s.%s' no longer exists."),
							*OverrideObject->GetClass()->GetName(), *PropertyName.ToString()), OverrideObject, nullptr, true);
				}
			}
		}
	}

	TArray<FString> IgnoredBindings;
	TArray<FString> BindingProblems;
	LexUIPrefabBehaviourUtils::AutoBindAndValidate(RootWidget, Prefab, IgnoredBindings, BindingProblems, false);
	for (const FString& Problem : BindingProblems)
	{
		AddIssue(ELexUIPrefabCompilerSeverity::Warning,
			FString::Printf(TEXT("Behaviour variable: %s"), *Problem), GetPrimaryBehaviour());
	}

	for (ULexWidget* Widget : Widgets)
	{
		if (!IsValid(Widget))
		{
			continue;
		}
		const TArray<ULexUIBehaviour*>& Components = Widget->GetAllComponents();
		for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex)
		{
			ULexUIBehaviour* Component = Components[ComponentIndex];
			if (!IsValid(Component))
			{
				AddIssue(ELexUIPrefabCompilerSeverity::Warning,
					FString::Printf(TEXT("Widget '%s' has a missing Behaviour component at slot %d."),
						*Widget->GetDisplayName(), ComponentIndex), Widget);
				continue;
			}
			for (TFieldIterator<FStructProperty> It(Component->GetClass()); It; ++It)
			{
				FStructProperty* EventProperty = *It;
				if (EventProperty->Struct != FLexUIEventDelegate::StaticStruct())
				{
					continue;
				}
				const FLexUIEventDelegate* Event = EventProperty->ContainerPtrToValuePtr<FLexUIEventDelegate>(Component);
				TArray<FLexUIEventBindingValidationIssue> EventIssues;
				Event->GetValidationIssues(EventIssues, RootWidget);
				for (const FLexUIEventBindingValidationIssue& EventIssue : EventIssues)
				{
					AddIssue(ELexUIPrefabCompilerSeverity::Warning,
						FString::Printf(TEXT("%s.%s binding %d: %s"), *Component->GetName(), *EventProperty->GetName(),
							EventIssue.BindingIndex + 1, *EventIssue.Message), Component);
				}
			}

			if (ULexUIPrefabSequenceComponent* SequenceComponent = Cast<ULexUIPrefabSequenceComponent>(Component))
			{
				const TArray<ULexUIPrefabSequence*>& Sequences = SequenceComponent->GetSequenceArray();
				for (int32 SequenceIndex = 0; SequenceIndex < Sequences.Num(); ++SequenceIndex)
				{
					ULexUIPrefabSequence* Sequence = Sequences[SequenceIndex];
					if (!IsValid(Sequence))
					{
						AddIssue(ELexUIPrefabCompilerSeverity::Warning,
							FString::Printf(TEXT("Animation slot %d on '%s' is missing."), SequenceIndex, *Component->GetName()), Component);
						continue;
					}
					TArray<FGuid> InvalidBindingIds;
					Sequence->GetInvalidObjectBindingIds(RootWidget, InvalidBindingIds);
					if (Sequence->HasObjectBindingCountMismatch())
					{
						AddIssue(ELexUIPrefabCompilerSeverity::Warning,
							FString::Printf(TEXT("Animation '%s' has mismatched object binding ID and reference arrays."),
								*Sequence->GetDisplayNameString()), Component, Sequence, true);
					}
					for (const FGuid& BindingId : InvalidBindingIds)
					{
						FString BindingName = BindingId.ToString();
						if (UMovieScene* MovieScene = Sequence->GetMovieScene())
						{
							if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(BindingId))
							{
								BindingName = Possessable->GetName();
							}
						}
						AddIssue(ELexUIPrefabCompilerSeverity::Warning,
							FString::Printf(TEXT("Animation '%s' references deleted object binding '%s'."),
								*Sequence->GetDisplayNameString(), *BindingName), Component, Sequence);
					}
				}
			}
		}
	}

	// Measurement never feeds arranged rects back into itself, so an Auto-measured child with no
	// intrinsic size source (no authored rect, no fitter, no visual, no sized content) measures as
	// zero and collapses. That used to be silent and self-perpetuating; report it here instead.
	for (ULexWidget* Widget : Widgets)
	{
		if (!IsValid(Widget) || Widget->GetIgnoreLayout() || !Widget->GetLayoutVisibleInHierarchy())
		{
			continue;
		}
		ULexWidget* Parent = Widget->GetParent();
		ULexPanelLayoutBase* ParentPanel = IsValid(Parent) ? Cast<ULexPanelLayoutBase>(Parent->GetLayoutContainer()) : nullptr;
		if (!IsValid(ParentPanel))
		{
			continue;
		}
		const ULexPanelSlot* Slot = Widget->GetPanelSlot();
		if (IsValid(Slot) && Slot->SizeRule != ELexPanelSizeRule::Auto)
		{
			continue;
		}
		// A widget with neither visual nor children is an intentional spacer; zero is its job.
		const bool bContentBearing = IsValid(Widget->GetVisual()) || Widget->GetChildrenCount() > 0;
		if (!bContentBearing)
		{
			continue;
		}
		const FVector2D Desired = ParentPanel->GetDesiredSize(Widget);
		if (Desired.X <= 0.0 || Desired.Y <= 0.0)
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("'%s' has no intrinsic size source on its Auto-measured %s axis and will collapse to zero under '%s'. Give it an authored size, a SizeBox override, or content with intrinsic size."),
					*Widget->GetDisplayName(),
					Desired.X <= 0.0 && Desired.Y <= 0.0 ? TEXT("X and Y") : (Desired.X <= 0.0 ? TEXT("X") : TEXT("Y")),
					*Parent->GetDisplayName()), Widget);
		}
	}

	// A scroll box only arranges (and scrolls) layout-participating children. Content converted from
	// the UIScrollView component workflow often keeps its old Ignore Layout flag — the scroll box then
	// excludes it entirely and MaxScrollOffset stays 0, which reads as "scrolling is broken".
	for (ULexWidget* Widget : Widgets)
	{
		// Skip hierarchy-hidden scroll boxes: under a deactivated page every child reads as
		// non-participating, which used to fire this warning with a misleading "Ignore Layout"
		// message for perfectly healthy content.
		if (!IsValid(Widget) || !Cast<ULexLayoutContainerScrollBox>(Widget->GetLayoutContainer())
			|| !Widget->GetLayoutVisibleInHierarchy())
		{
			continue;
		}
		int32 Participating = 0;
		for (ULexWidget* Child : Widget->GetChildren())
		{
			if (IsValid(Child) && !Child->GetIgnoreLayout() && Child->GetLayoutVisibleInHierarchy())
			{
				Participating++;
			}
		}
		if (Participating == 0 && Widget->GetChildrenCount() > 0)
		{
			AddIssue(ELexUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Scroll box '%s' has children but none participate in layout (Ignore Layout is set on all of them) — nothing will scroll. Clear Ignore Layout on the content."),
					*Widget->GetDisplayName()), Widget);
		}
	}
}

void FLexUIPrefabEditor::PublishCompilerResults(const FText& PageTitle,
	const TArray<FLexUIPrefabCompilerIssue>& Issues, const FText& Summary, bool bAutoOpenOnProblems)
{
	if (!CompilerResultsListing.IsValid())
	{
		return;
	}
	CompilerResultsListing->NewPage(PageTitle);
	const TWeakPtr<FLexUIPrefabEditor> WeakThis = SharedThis(this);
	bool bHasProblems = false;
	for (const FLexUIPrefabCompilerIssue& Issue : Issues)
	{
		EMessageSeverity::Type Severity = EMessageSeverity::Info;
		if (Issue.Severity == ELexUIPrefabCompilerSeverity::Warning)
		{
			Severity = EMessageSeverity::Warning;
			bHasProblems = true;
		}
		else if (Issue.Severity == ELexUIPrefabCompilerSeverity::Error)
		{
			Severity = EMessageSeverity::Error;
			bHasProblems = true;
		}

		TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(Severity, FText::FromString(Issue.Message));
		TSharedPtr<FActionToken> ActionToken;
		if (Issue.Animation.IsValid())
		{
			const TWeakObjectPtr<ULexUIPrefabSequence> WeakAnimation = Issue.Animation;
			ActionToken = FActionToken::Create(LOCTEXT("OpenAnimationIssueAction", "Open Animation"),
				LOCTEXT("OpenAnimationIssueActionTooltip", "Open and select the animation containing this binding."),
				FOnActionTokenExecuted::CreateLambda([WeakThis, WeakAnimation]()
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakThis.Pin())
					{
						Editor->NavigateToAnimation(WeakAnimation);
					}
				}));
		}
		else if (Issue.bOpenRawData)
		{
			ActionToken = FActionToken::Create(LOCTEXT("OpenRawDataIssueAction", "Open Raw Data"),
				LOCTEXT("OpenRawDataIssueActionTooltip", "Open the prefab reference and GUID data."),
				FOnActionTokenExecuted::CreateLambda([WeakThis]()
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakThis.Pin())
					{
						Editor->OnOpenRawDataViewerPanel();
					}
				}));
		}
		else if (Issue.SourceObject.IsValid())
		{
			const TWeakObjectPtr<UObject> WeakObject = Issue.SourceObject;
			ActionToken = FActionToken::Create(LOCTEXT("SelectCompilerIssueAction", "Select"),
				LOCTEXT("SelectCompilerIssueActionTooltip", "Select the source widget or Behaviour in the prefab editor."),
				FOnActionTokenExecuted::CreateLambda([WeakThis, WeakObject]()
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakThis.Pin())
					{
						Editor->NavigateToCompilerObject(WeakObject);
					}
				}));
		}
		if (ActionToken.IsValid())
		{
			Message->AddToken(ActionToken.ToSharedRef());
			Message->SetMessageLink(ActionToken.ToSharedRef());
		}
		CompilerResultsListing->AddMessage(Message, false);
	}
	CompilerResultsListing->AddMessage(FTokenizedMessage::Create(EMessageSeverity::Info, Summary), false);
	if (bAutoOpenOnProblems && bHasProblems)
	{
		InvokeTab(FLexUIPrefabEditorTabs::CompilerResultsID);
	}
}

void FLexUIPrefabEditor::RunInitialReferenceValidation()
{
	TArray<FLexUIPrefabCompilerIssue> Issues;
	ValidatePrefabReferences(Issues);
	LastApplyWarningCount = 0;
	LastApplyErrorCount = 0;
	for (const FLexUIPrefabCompilerIssue& Issue : Issues)
	{
		LastApplyWarningCount += Issue.Severity == ELexUIPrefabCompilerSeverity::Warning ? 1 : 0;
		LastApplyErrorCount += Issue.Severity == ELexUIPrefabCompilerSeverity::Error ? 1 : 0;
	}
	LastApplyStatus = LastApplyErrorCount > 0
		? ELexUIPrefabApplyStatus::Error
		: (LastApplyWarningCount > 0 ? ELexUIPrefabApplyStatus::Warning : ELexUIPrefabApplyStatus::Unknown);
	const FText Summary = LastApplyErrorCount > 0 || LastApplyWarningCount > 0
		? FText::Format(LOCTEXT("OpenReferenceCheckProblems", "Reference check found {0} error(s) and {1} warning(s)."),
			FText::AsNumber(LastApplyErrorCount), FText::AsNumber(LastApplyWarningCount))
		: LOCTEXT("OpenReferenceCheckClean", "Reference check completed. No issues found.");
	PublishCompilerResults(FText::Format(LOCTEXT("OpenResultsPageTitle", "Open {0}"), FText::FromString(GetNameSafe(PrefabBeingEdited))),
		Issues, Summary, true);
}

void FLexUIPrefabEditor::NavigateToCompilerObject(TWeakObjectPtr<UObject> InObject)
{
	UObject* Object = InObject.Get();
	if (!IsValid(Object))
	{
		return;
	}
	ULexUIBehaviour* Behaviour = Cast<ULexUIBehaviour>(Object);
	ULexWidget* Widget = Cast<ULexWidget>(Object);
	if (Behaviour != nullptr)
	{
		Widget = Behaviour->GetWidget();
	}
	if (Widget == nullptr)
	{
		Widget = Object->GetTypedOuter<ULexWidget>();
	}
	if (!IsValid(Widget))
	{
		return;
	}
	InvokeTab(FLexUIPrefabEditorTabs::DetailsID);
	SelectWidgets(TSet<ULexWidget*>{Widget}, false);
	if (Behaviour != nullptr)
	{
		ULexUISelection* Selection = ULexUISelection::GetInstance(GetWorld());
		Selection->ClearComponentSelection();
		Selection->SelectComponent(Behaviour);
	}
}

void FLexUIPrefabEditor::NavigateToAnimation(TWeakObjectPtr<ULexUIPrefabSequence> InAnimation)
{
	if (!InAnimation.IsValid() || !SequencerPtr.IsValid())
	{
		return;
	}
	InvokeTab(FLexUIPrefabEditorTabs::SequencerID);
	SequencerPtr->SelectAnimation(InAnimation.Get());
}

bool FLexUIPrefabEditor::ApplyPrefabChanges()
{
	ULexUIPrefab* Prefab = GetPrefabBeingEdited();
	ULexUIPrefabHelperObject* Helper = IsValid(Prefab) ? Prefab->GetPrefabHelperObject() : nullptr;
	TArray<FLexUIPrefabCompilerIssue> Issues;
	const FText PageTitle = FText::Format(
		LOCTEXT("ApplyResultsPageTitle", "Apply {0} - {1}"),
		FText::FromString(GetNameSafe(Prefab)),
		FText::AsTime(FDateTime::Now()));
	if (!IsValid(Helper) || !IsValid(Helper->LoadedRootWidget))
	{
		FLexUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.Severity = ELexUIPrefabCompilerSeverity::Error;
		Issue.Message = TEXT("Apply failed because the prefab root data is unavailable.");
		LastApplyStatus = ELexUIPrefabApplyStatus::Error;
		LastApplyWarningCount = 0;
		LastApplyErrorCount = 1;
		bLastApplySerializationSucceeded = false;
		PublishCompilerResults(PageTitle, Issues, LOCTEXT("ApplyMissingRootSummary", "Apply failed: prefab root data is unavailable."), true);
		return false;
	}

	LastApplyStatus = ELexUIPrefabApplyStatus::Unknown;
	LastApplyWarningCount = 0;
	LastApplyErrorCount = 0;
	bLastApplySerializationSucceeded = false;
	FLexUIEditorTools::OnBeforeApplyPrefab.Broadcast(Helper);

	// Old assets used a convention-based BP_<PrefabName> component without storing the class.
	// Persist that already-loaded component as the explicit primary behaviour on first Apply.
	if (Prefab->GetBehaviourClass() == nullptr)
	{
		if (ULexUIBehaviour* LegacyBehaviour = LexUIPrefabBehaviourUtils::FindBehaviourComponent(Helper->LoadedRootWidget, Prefab))
		{
			Prefab->Modify();
			Prefab->SetBehaviourClass(LegacyBehaviour->GetClass());
			FLexUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
			Issue.Severity = ELexUIPrefabCompilerSeverity::Info;
			Issue.Message = FString::Printf(TEXT("Migrated legacy companion behaviour '%s' to BehaviourClass."), *LegacyBehaviour->GetClass()->GetName());
			Issue.SourceObject = LegacyBehaviour;
		}
	}

	if (ULexWidget* RootWidget = GetLoadedRootWidget())
	{
		const int32 RenameCount = FLexUIEditorTools::EnsureUniqueWidgetDisplayNames(RootWidget);
		if (RenameCount > 0)
		{
			FLexUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
			Issue.Severity = ELexUIPrefabCompilerSeverity::Info;
			Issue.Message = FString::Printf(TEXT("Renamed %d duplicate widget name(s) using UMG-style numeric suffixes."), RenameCount);
			if (OutlinerPtr.IsValid())
			{
				OutlinerPtr->RequestRefresh();
			}
		}
	}

	// UMG BindWidget-style pass, run just before the prefab is written. Validation runs below
	// after auto-wiring so each remaining problem is reported exactly once.
	if (ULexWidget* RootWidget = GetLoadedRootWidget())
	{
		TArray<FString> BoundDetails, IgnoredProblems;
		LexUIPrefabBehaviourUtils::AutoBindAndValidate(RootWidget, Prefab, BoundDetails, IgnoredProblems, true);
		if (BoundDetails.Num() > 0)
		{
			Helper->Modify();
			Helper->SetAnythingDirty();
			FLexUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
			Issue.Severity = ELexUIPrefabCompilerSeverity::Info;
			Issue.Message = FString::Printf(TEXT("Auto-bound %d Behaviour variable(s): %s"),
				BoundDetails.Num(), *FString::Join(BoundDetails, TEXT(", ")));
			Issue.SourceObject = GetPrimaryBehaviour();
		}
	}

	for (const FString& Warning : PendingBehaviourWarnings)
	{
		FLexUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.Severity = ELexUIPrefabCompilerSeverity::Warning;
		Issue.Message = Warning;
	}
	PendingBehaviourWarnings.Reset();

	const int32 RemovedStaleGuidMappings = Helper->CleanupObjectsOutsideRootHierarchy();
	if (RemovedStaleGuidMappings > 0)
	{
		FLexUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.Severity = ELexUIPrefabCompilerSeverity::Info;
		Issue.Message = FString::Printf(
			TEXT("Removed %d stale Helper GUID mapping(s) outside the prefab root hierarchy."),
			RemovedStaleGuidMappings);
	}

	ValidatePrefabReferences(Issues);
	const bool bHasStructuralError = Issues.ContainsByPredicate([](const FLexUIPrefabCompilerIssue& Issue)
	{
		return Issue.Severity == ELexUIPrefabCompilerSeverity::Error;
	});
	if (!bHasStructuralError)
	{
		bLastApplySerializationSucceeded = Helper->SavePrefab();
	}
	if (!bLastApplySerializationSucceeded)
	{
		FLexUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.Severity = ELexUIPrefabCompilerSeverity::Error;
		Issue.Message = TEXT("Prefab serialization failed. Current changes were not written to the asset.");
	}

	LastApplyWarningCount = 0;
	LastApplyErrorCount = 0;
	for (const FLexUIPrefabCompilerIssue& Issue : Issues)
	{
		LastApplyWarningCount += Issue.Severity == ELexUIPrefabCompilerSeverity::Warning ? 1 : 0;
		LastApplyErrorCount += Issue.Severity == ELexUIPrefabCompilerSeverity::Error ? 1 : 0;
	}
	LastApplyStatus = LastApplyErrorCount > 0
		? ELexUIPrefabApplyStatus::Error
		: (LastApplyWarningCount > 0 ? ELexUIPrefabApplyStatus::Warning : ELexUIPrefabApplyStatus::Success);

	FText Summary;
	if (LastApplyStatus == ELexUIPrefabApplyStatus::Error)
	{
		Summary = FText::Format(LOCTEXT("ApplyErrorSummary", "Apply failed with {0} error(s) and {1} warning(s)."),
			FText::AsNumber(LastApplyErrorCount), FText::AsNumber(LastApplyWarningCount));
	}
	else if (LastApplyStatus == ELexUIPrefabApplyStatus::Warning)
	{
		Summary = FText::Format(LOCTEXT("ApplyWarningSummary", "Applied with {0} warning(s)."), FText::AsNumber(LastApplyWarningCount));
	}
	else
	{
		Summary = LOCTEXT("ApplySuccessSummary", "Apply succeeded. No reference issues found.");
	}
	PublishCompilerResults(PageTitle, Issues, Summary, true);

	FNotificationInfo Info(Summary);
	Info.ExpireDuration = LastApplyErrorCount > 0 ? 7.0f : 4.0f;
	if (LastApplyStatus == ELexUIPrefabApplyStatus::Error)
	{
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.ErrorWithColor"));
	}
	else if (LastApplyStatus == ELexUIPrefabApplyStatus::Warning)
	{
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
	}
	FSlateNotificationManager::Get().AddNotification(Info);
	return LastApplyStatus != ELexUIPrefabApplyStatus::Error;
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

bool FLexUIPrefabEditor::GetShowLayoutDebug() const
{
	return GetDefault<ULexUIEditorSettings>()->bShowLayoutDebugVisualization;
}

bool FLexUIPrefabEditor::GetShowResolutionGuides() const
{
	return GetDefault<ULexUIEditorSettings>()->bShowDesignResolutionGuides;
}

void FLexUIPrefabEditor::ToggleResolutionGuides()
{
	ULexUIEditorSettings* Settings = GetMutableDefault<ULexUIEditorSettings>();
	Settings->bShowDesignResolutionGuides = !Settings->bShowDesignResolutionGuides;
	Settings->SaveConfig();
}

FIntPoint FLexUIPrefabEditor::GetDesignerCanvasSize()
{
	if (ULexWidget* RootAgent = GetRootAgentWidget())
	{
		return FIntPoint(FMath::RoundToInt(RootAgent->GetWidth()), FMath::RoundToInt(RootAgent->GetHeight()));
	}
	return PrefabBeingEdited ? PrefabBeingEdited->PrefabDataForPrefabEditor.CanvasSize : FIntPoint(1920, 1080);
}

FIntPoint FLexUIPrefabEditor::GetDesignerViewportSize()
{
	if (PrefabBeingEdited)
	{
		const FIntPoint Stored = PrefabBeingEdited->PrefabDataForPrefabEditor.DesignViewportSize;
		if (Stored.X > 0 && Stored.Y > 0)
		{
			return Stored;
		}
	}
	// Assets authored before the picker consulted the scale rule stored only CanvasSize, which
	// back then WAS the picked resolution.
	return GetDesignerCanvasSize();
}

bool FLexUIPrefabEditor::CalculateDesignerCanvasFor(FIntPoint InViewportSize, FIntPoint& OutCanvasSize, float& OutScale)
{
	OutCanvasSize = InViewportSize;
	OutScale = 1.0f;
	if (InViewportSize.X <= 0 || InViewportSize.Y <= 0)
	{
		return false;
	}
	// The prefab's OWN root canvas is the one that governs once this prefab is the top-level UI at
	// runtime. The preview agent's canvas outranks it inside the editor only because the agent sits
	// above it, so asking the agent would just report the editor's own scaffolding back.
	ULexWidget* Root = GetLoadedRootWidget();
	ULexCanvas* Canvas = IsValid(Root) ? Root->GetComponent<ULexCanvas>() : nullptr;
	if (!IsValid(Canvas))
	{
		return false;
	}
	FVector2D CanvasSize;
	Canvas->CalculateCanvasSizeAndScale(InViewportSize, CanvasSize, OutScale);
	if (CanvasSize.X <= 0.0f || CanvasSize.Y <= 0.0f)
	{
		OutCanvasSize = InViewportSize;
		OutScale = 1.0f;
		return false;
	}
	OutCanvasSize = FIntPoint(FMath::RoundToInt(CanvasSize.X), FMath::RoundToInt(CanvasSize.Y));
	return true;
}

void FLexUIPrefabEditor::SetDesignerViewportSize(FIntPoint NewViewportSize)
{
	ULexWidget* RootAgent = GetRootAgentWidget();
	if (!IsValid(RootAgent) || !PrefabBeingEdited || NewViewportSize.X <= 0 || NewViewportSize.Y <= 0)
	{
		return;
	}
	// The picked resolution is the VIEWPORT size; the canvas the designer lays out on is whatever
	// the prefab's own scaler rule makes of it. Sizing the agent to the raw device resolution is
	// what made the picker lie for every mode except ConstantPixelSize.
	FIntPoint NewCanvasSize;
	float NewScale = 1.0f;
	CalculateDesignerCanvasFor(NewViewportSize, NewCanvasSize, NewScale);
	// Re-clicking the checked preset (or flipping a square canvas) must not dirty the asset or
	// push an empty undo entry.
	if (NewViewportSize == GetDesignerViewportSize() && NewCanvasSize == GetDesignerCanvasSize())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("SetDesignScreenSize", "Set Design Screen Size"));
	// The preview-scene agent is created without RF_Transactional; without it Modify records
	// nothing and undo would roll back only the stored CanvasSize, not the visible canvas.
	RootAgent->SetFlags(RF_Transactional);
	RootAgent->Modify();
	// SetSizeDelta, matching how the instance scene and thumbnail scene size the root canvas.
	RootAgent->SetSizeDelta(FVector2D(NewCanvasSize.X, NewCanvasSize.Y));
	// Keep the agent canvas's edit-mode viewport in step: it is forced to a fixed size, and if the
	// preview ever runs a screen-space render mode its editor tick would otherwise resize the agent
	// to the stale default and stomp this.
	if (ULexCanvas* AgentCanvas = RootAgent->GetComponent<ULexCanvas>())
	{
		AgentCanvas->Modify();
		AgentCanvas->SizeInEditMode = NewViewportSize;
	}
	PrefabBeingEdited->Modify();
	PrefabBeingEdited->PrefabDataForPrefabEditor.CanvasSize = NewCanvasSize;
	PrefabBeingEdited->PrefabDataForPrefabEditor.DesignViewportSize = NewViewportSize;
	ULexWidget::MarkLayoutForRebuild(RootAgent);
	ULexWidget::RebuildLayoutImmediately(RootAgent);
}

void FLexUIPrefabEditor::ToggleLayoutDebug()
{
	ULexUIEditorSettings* Settings = GetMutableDefault<ULexUIEditorSettings>();
	Settings->bShowLayoutDebugVisualization = !Settings->bShowLayoutDebugVisualization;
	Settings->SaveConfig();
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
		PrefabEditorCommands.OverridesViewer,
		FExecuteAction::CreateSP(this, &FLexUIPrefabEditor::OnOpenOverridesViewerPanel),
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
		auto BehaviourOptionsMenuEntry = FToolMenuEntry::InitComboButton(
			"BehaviourOptions",
			FUIAction(),
			FNewToolMenuDelegate::CreateSP(this, &FLexUIPrefabEditor::GenerateBehaviourOptionsMenu),
			LOCTEXT("BehaviourOptionsTooltip", "Create, select, replace, or remove this prefab's primary Behaviour."));
		BehaviourOptionsMenuEntry.ToolBarData.bSimpleComboBox = true;

		FToolMenuSection& Section = ToolBar->AddSection("LexUIPrefabCommands", TAttribute<FText>(), InsertAfterAssetSection);
		Section.AddEntry(ApplyButtonMenuEntry);
		Section.AddEntry(ApplyOptionsMenuEntry);
		Section.AddEntry(BehaviourButton);
		Section.AddEntry(BehaviourOptionsMenuEntry);
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(FLexUIPrefabEditorCommand::Get().RawDataViewer));
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(FLexUIPrefabEditorCommand::Get().OverridesViewer));
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(FLexUIPrefabEditorCommand::Get().OpenPrefabHelperObject));
	}
}

void FLexUIPrefabEditor::GenerateBehaviourOptionsMenu(UToolMenu* InMenu)
{
	FToolMenuSection& Section = InMenu->AddSection("Behaviour", LOCTEXT("BehaviourMenuSection", "Behaviour"));
	Section.AddMenuEntry(
		"OpenBehaviourPanel",
		LOCTEXT("OpenBehaviourPanel", "Behaviour Panel"),
		LOCTEXT("OpenBehaviourPanelTooltip", "Open the behaviour panel: widget references with quick bind, provided events and functions, and per-widget event handler shortcuts."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Details"),
		FUIAction(FExecuteAction::CreateSP(this, &FLexUIPrefabEditor::OnOpenBehaviourViewerPanel)));
	Section.AddMenuEntry(
		"OpenCurrentBehaviour",
		LOCTEXT("OpenCurrentBehaviour", "Open Current Behaviour"),
		LOCTEXT("OpenCurrentBehaviourTooltip", "Open the current Behaviour using its registered script editor backend."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FLexUIPrefabEditor::CreateOrOpenBehaviourBlueprint),
			FCanExecuteAction::CreateLambda([WeakThis = TWeakPtr<FLexUIPrefabEditor>(SharedThis(this))]()
			{
				return WeakThis.IsValid() && WeakThis.Pin()->GetEffectiveBehaviourClass() != nullptr;
			})));
	Section.AddMenuEntry(
		"CreateBehaviourBlueprint",
		LOCTEXT("CreateBehaviourBlueprint", "Create Blueprint Behaviour"),
		LOCTEXT("CreateBehaviourBlueprintTooltip", "Create BP_<PrefabName>, assign it as the primary Behaviour, and open it."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Blueprints"),
		FUIAction(FExecuteAction::CreateSP(this, &FLexUIPrefabEditor::CreateAndAssignBehaviourBlueprint)));
	Section.AddMenuEntry(
		"SelectBehaviourClass",
		LOCTEXT("SelectBehaviourClass", "Select Behaviour Class..."),
		LOCTEXT("SelectBehaviourClassTooltip", "Pick any concrete ULexUIBehaviour class, including externally generated script classes."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
		FUIAction(FExecuteAction::CreateSP(this, &FLexUIPrefabEditor::PickBehaviourClass)));
	Section.AddMenuEntry(
		"RemoveBehaviour",
		LOCTEXT("RemoveBehaviour", "Remove Behaviour"),
		LOCTEXT("RemoveBehaviourTooltip", "Remove the primary Behaviour component without deleting its script asset."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FLexUIPrefabEditor::RemovePrimaryBehaviour),
			FCanExecuteAction::CreateLambda([WeakThis = TWeakPtr<FLexUIPrefabEditor>(SharedThis(this))]()
			{
				return WeakThis.IsValid() && WeakThis.Pin()->GetEffectiveBehaviourClass() != nullptr;
			})));
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
	switch (LastApplyStatus)
	{
	case ELexUIPrefabApplyStatus::Success:
		return LOCTEXT("ApplyGood_Tooltip", "Prefab is up to date");
	case ELexUIPrefabApplyStatus::Warning:
		return FText::Format(LOCTEXT("ApplyWarnings_Tooltip", "Applied with {0} warning(s)"), FText::AsNumber(LastApplyWarningCount));
	case ELexUIPrefabApplyStatus::Error:
		return FText::Format(LOCTEXT("ApplyErrors_Tooltip", "Apply failed with {0} error(s)"), FText::AsNumber(LastApplyErrorCount));
	default:
		return LOCTEXT("ApplyUnknown_Tooltip", "Prefab has not been applied in this editor session");
	}
}
FSlateIcon FLexUIPrefabEditor::GetApplyButtonStatusImage()const
{
	static const FName CompileStatusBackground("Blueprint.CompileStatus.Background");
	static const FName CompileStatusUnknown("Blueprint.CompileStatus.Overlay.Unknown");
	static const FName CompileStatusGood("Blueprint.CompileStatus.Overlay.Good");
	static const FName CompileStatusWarning("Blueprint.CompileStatus.Overlay.Warning");
	static const FName CompileStatusError("Blueprint.CompileStatus.Overlay.Error");

	FName Overlay = CompileStatusUnknown;
	if (!GetAnythingDirty())
	{
		switch (LastApplyStatus)
		{
		case ELexUIPrefabApplyStatus::Success: Overlay = CompileStatusGood; break;
		case ELexUIPrefabApplyStatus::Warning: Overlay = CompileStatusWarning; break;
		case ELexUIPrefabApplyStatus::Error: Overlay = CompileStatusError; break;
		default: break;
		}
	}
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
		.Label(LOCTEXT("OutlinerTab_Title", "Hierarchy"))
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

TSharedRef<SDockTab> FLexUIPrefabEditor::SpawnTab_PrefabOverridesViewer(const FSpawnTabArgs& Args)
{
	if (PrefabOverridesViewer.IsValid())
	{
		PrefabOverridesViewer->Rebuild();
	}
	return SNew(SDockTab)
		.Label(LOCTEXT("PrefabOverridesTab_Title", "Overrides"))
		[
			PrefabOverridesViewer.ToSharedRef()
		];
}

TSharedRef<SDockTab> FLexUIPrefabEditor::SpawnTab_PrefabBehaviourViewer(const FSpawnTabArgs& Args)
{
	if (PrefabBehaviourViewer.IsValid())
	{
		PrefabBehaviourViewer->Rebuild();
	}
	return SNew(SDockTab)
		.Label(LOCTEXT("PrefabBehaviourTab_Title", "Behaviour"))
		[
			PrefabBehaviourViewer.ToSharedRef()
		];
}

TSharedRef<SDockTab> FLexUIPrefabEditor::SpawnTab_CompilerResults(const FSpawnTabArgs& Args)
{
	check(CompilerResultsListing.IsValid());
	return SNew(SDockTab)
		.Label(LOCTEXT("CompilerResultsTab_Title", "Compiler Results"))
		[
			FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog")
				.CreateLogListingWidget(CompilerResultsListing.ToSharedRef())
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
				if (!CurrentSelectedWidget->CanAcceptAdditionalChildren(PrefabsToLoad.Num()))
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						LOCTEXT("Error_ParentAtCapacity", "The target widget cannot accept the dropped prefab(s)."));
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
