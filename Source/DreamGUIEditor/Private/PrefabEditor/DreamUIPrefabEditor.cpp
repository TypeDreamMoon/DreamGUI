// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIPrefabEditor.h"
#include "DreamGUIEditorModule.h"
#include "DreamUIPrefabEditorViewport.h"
#include "DreamUIPrefabEditorDetails.h"
#include "DreamUIPrefabRawDataViewer.h"
#include "DreamUIPrefabOverridesViewer.h"
#include "DreamUIPrefabBehaviourViewer.h"
#include "EditorModeManager.h"
#include "GameFramework/Actor.h"
#include "AssetSelection.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Misc/FeedbackContext.h"
#include "DreamUIPrefabEditorCommand.h"
#include "DreamUIEditorTools.h"
#include "DreamUIControlRegistry.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "DreamWidgetEditorHierarchyView.h"
#include "SDreamUIPrefabPalette.h"
#include "DreamUIPrefabBehaviourUtils.h"
#include "DreamUIBehaviourEditorBackend.h"
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
#include "Core/DreamUIManager.h"
#include "Core/DreamUISettings.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamUIPrefabEditorViewportClient.h"
#include "EngineDefines.h"//MIN_ORTHOZOOM
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Interaction/DreamContentWidget.h"
#include "Framework/Commands/GenericCommands.h"
#include "PrefabSystem/DreamUIPrefabInstanceScene.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "PrefabAnimation/DreamUIPrefabSequenceEditor.h"
#include "ScopedTransaction.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "SourceCodeNavigation.h"
#include "Event/DreamUIEventDelegate.h"
#include "Utils/DreamUIUtils.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequenceComponent.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequence.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "Logging/TokenizedMessage.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"

#define LOCTEXT_NAMESPACE "DreamUIPrefabEditor"

const FName PrefabEditorAppName = FName(TEXT("DreamUIPrefabEditorApp"));

TArray<FDreamUIPrefabEditor*> FDreamUIPrefabEditor::PrefabEditorInstanceCollection;

struct FDreamUIPrefabEditorTabs
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

const FName FDreamUIPrefabEditorTabs::DetailsID(TEXT("Details"));
const FName FDreamUIPrefabEditorTabs::ViewportID(TEXT("Viewport"));
const FName FDreamUIPrefabEditorTabs::OutlinerID(TEXT("Outliner"));
const FName FDreamUIPrefabEditorTabs::PaletteID(TEXT("Palette"));
const FName FDreamUIPrefabEditorTabs::SequencerID(TEXT("Sequencer"));
const FName FDreamUIPrefabEditorTabs::PrefabRawDataViewerID(TEXT("PrefabRawDataViewer"));
const FName FDreamUIPrefabEditorTabs::PrefabOverridesViewerID(TEXT("PrefabOverridesViewer"));
const FName FDreamUIPrefabEditorTabs::PrefabBehaviourViewerID(TEXT("PrefabBehaviourViewer"));
const FName FDreamUIPrefabEditorTabs::CompilerResultsID(TEXT("CompilerResults"));

namespace DreamUIPrefabEditorLocal
{
	class FBehaviourClassFilter final : public IClassViewerFilter
	{
	public:
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass,
			TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return InClass != nullptr
				&& InClass->IsChildOf(UDreamUIBehaviour::StaticClass())
				&& !InClass->HasAnyClassFlags(DisallowedFlags);
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions,
			const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData,
			TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return InUnloadedClassData->IsChildOf(UDreamUIBehaviour::StaticClass())
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

	static const TCHAR* SaveOnApplySection = TEXT("DreamUIPrefabEditor.Settings");
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
FDreamUIPrefabEditor::FDreamUIPrefabEditor()
{
	PrefabEditorInstanceCollection.Add(this);
}
FDreamUIPrefabEditor::~FDreamUIPrefabEditor()
{
	PrefabEditorInstanceCollection.Remove(this);

	UWorld* EditorWorld = nullptr;
	if (IsValid(PrefabBeingEdited))
	{
		if (FDreamUIPrefabInstanceScene* PreviewScene = PrefabBeingEdited->GetPrefabInstanceScene())
		{
			EditorWorld = PreviewScene->GetWorld();
		}
	}
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(EditorWorld))
	{
		Manager->OnDreamUIWidgetOutlinerChanged.RemoveAll(this);
		Manager->EventOnOutlineChanged.RemoveAll(this);
		Manager->bShouldTickInEditor = false;
	}
	if (UDreamUISelection* Selection = UDreamUISelection::GetInstance(EditorWorld))
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

FDreamUIPrefabEditor* FDreamUIPrefabEditor::GetEditorForPrefabIfValid(UDreamUIPrefab* InPrefab)
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

bool FDreamUIPrefabEditor::WorldIsPrefabEditor(UWorld* InWorld)
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

TWeakPtr<FDreamUIPrefabEditor> FDreamUIPrefabEditor::GetEditorByWorld(UWorld* InWorld)
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

bool FDreamUIPrefabEditor::WidgetIsRootAgent(UDreamWidget* InWidget)
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

void FDreamUIPrefabEditor::IterateAllPrefabEditor(const TFunction<void(FDreamUIPrefabEditor*)>& InFunction)
{
	for (auto Instance : PrefabEditorInstanceCollection)
	{
		InFunction(Instance);
	}
}

bool FDreamUIPrefabEditor::RefreshOnSubPrefabDirty(UDreamUIPrefab* InSubPrefab)
{
	return GetPrefabHelperObject()->RefreshOnSubPrefabDirty(InSubPrefab);
}

FBox FDreamUIPrefabEditor::GetWidgetWorldBox(const UDreamWidget* InWidget)
{
	FBox LocalBounds;
	if (auto Visual = InWidget->GetVisual())
	{
		FVector Min, Max;
		Visual->GetGeometryBounds3DInLocalSpace(Min, Max);
		LocalBounds = FBox(Min, Max);
	}
	else
	{
		// A layout-only panel draws nothing, so its rect is the only extent it has.
		auto Min2D = InWidget->GetLocalSpaceLeftBottomPoint();
		auto Max2D = InWidget->GetLocalSpaceRightTopPoint();
		LocalBounds = FBox(FVector(0, Min2D.X, Min2D.Y), FVector(0, Max2D.X, Max2D.Y));
	}
	return LocalBounds.TransformBy(InWidget->GetWorldTransform());
}

bool FDreamUIPrefabEditor::AccumulateWidgetsBounds(const TArray<UDreamWidget*>& InWidgets, FBoxSphereBounds& OutResult)
{
	// Zeroed rather than left alone: callers that ignore the return value must still get a box the
	// camera can be pointed at instead of whatever was on the stack.
	OutResult = FBoxSphereBounds(EForceInit::ForceInitToZero);
	bool bAnyBounds = false;
	for (const UDreamWidget* Widget : InWidgets)
	{
		if (!IsValid(Widget) || !Widget->GetWidgetActiveInHierarchy())
		{
			continue;
		}
		const FBoxSphereBounds Box = FBoxSphereBounds(GetWidgetWorldBox(Widget));
		OutResult = bAnyBounds ? OutResult + Box : Box;
		bAnyBounds = true;
	}
	return bAnyBounds;
}

FBoxSphereBounds FDreamUIPrefabEditor::MakeCanvasFramingBounds(FIntPoint InCanvasSize)
{
	// UI lives on the YZ plane at X = 0, centred on the canvas, which is where the design canvas
	// sits even when every widget in the prefab is inactive.
	const FVector Extent(0.0, FMath::Max(1, InCanvasSize.X) * 0.5, FMath::Max(1, InCanvasSize.Y) * 0.5);
	return FBoxSphereBounds(FBox(-Extent, Extent));
}

bool FDreamUIPrefabEditor::GetSelectedObjectsBounds(FBoxSphereBounds& OutResult)
{
	TArray<UDreamWidget*> Widgets;
	Widgets.Reserve(SelectedWidgets.Num());
	for (auto& Widget : SelectedWidgets)
	{
		Widgets.Add(Widget.Get());
	}
	return AccumulateWidgetsBounds(Widgets, OutResult);
}

bool FDreamUIPrefabEditor::GetAllObjectsBounds(FBoxSphereBounds& OutResult)
{
	TArray<UDreamWidget*> Widgets;
	Widgets.Reserve(GetPrefabHelperObject()->MapGuidToObject.Num());
	for (auto& KeyValue : GetPrefabHelperObject()->MapGuidToObject)
	{
		if (auto Widget = Cast<UDreamWidget>(KeyValue.Value))
		{
			Widgets.Add(Widget);
		}
	}
	return AccumulateWidgetsBounds(Widgets, OutResult);
}

FBoxSphereBounds FDreamUIPrefabEditor::GetAllObjectsBounds()
{
	FBoxSphereBounds Bounds = FBoxSphereBounds(EForceInit::ForceInitToZero);
	if (!GetAllObjectsBounds(Bounds))
	{
		// GetInitialViewSetting feeds Origin to the orbit point even when the saved view wins, so
		// "nothing is active" has to produce a real place, not an unanswered question.
		Bounds = MakeCanvasFramingBounds(GetDesignerCanvasSize());
	}
	return Bounds;
}

bool FDreamUIPrefabEditor::WidgetBelongsToSubPrefab(UDreamWidget* InWidget)
{
	return GetPrefabHelperObject()->IsWidgetBelongsToSubPrefab(InWidget);
}

bool FDreamUIPrefabEditor::WidgetIsSubPrefabRoot(UDreamWidget* InSubPrefabRootWidget)
{
	return GetPrefabHelperObject()->SubPrefabMap.Contains(InSubPrefabRootWidget);
}

FDreamUISubPrefabData FDreamUIPrefabEditor::GetSubPrefabDataForActor(UDreamWidget* InSubPrefabWidget)
{
	return GetPrefabHelperObject()->GetSubPrefabData(InSubPrefabWidget);
}

void FDreamUIPrefabEditor::OpenSubPrefab(UDreamWidget* InSubPrefabWidget)
{
	if (auto SubPrefabAsset = GetPrefabHelperObject()->GetSubPrefabAsset(InSubPrefabWidget))
	{
		auto PrefabEditor = FDreamUIPrefabEditor::GetEditorForPrefabIfValid(SubPrefabAsset);
		if (!PrefabEditor)
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			AssetEditorSubsystem->OpenEditorForAsset(SubPrefabAsset);
		}
	}
}
void FDreamUIPrefabEditor::SelectSubPrefab(UDreamWidget* InSubPrefabWidget)
{
	if (auto SubPrefabAsset = GetPrefabHelperObject()->GetSubPrefabAsset(InSubPrefabWidget))
	{
		TArray<UObject*> ObjectsToSync;
		ObjectsToSync.Add(SubPrefabAsset);
		GEditor->SyncBrowserToObjects(ObjectsToSync);
	}
}

bool FDreamUIPrefabEditor::GetAnythingDirty()const 
{ 
	return GetPrefabHelperObject()->GetAnythingDirty();
}

namespace
{
	void SyncWidgetRegisterStateAfterTransaction(UDreamWidget* RootAgent, UWorld* EditorWorld)
	{
		if (!IsValid(RootAgent) || !IsValid(EditorWorld))
		{
			return;
		}

		RootAgent->EnsureChildrenAfterTransaction();
		TArray<UDreamWidget*> ReachableWidgets;
		UDreamWidget::CollectChildrenWidgets(RootAgent, ReachableWidgets);

		TSet<UDreamWidget*> AllWidgets;
		ForEachObjectOfClass(UDreamWidget::StaticClass(), [&](UObject* Object)
		{
			if (auto Widget = Cast<UDreamWidget>(Object))
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

void FDreamUIPrefabEditor::SyncSelection()
{
	// Every widget SelectWidgets hands to the selection broadcasts back into here, so without this
	// the set being built is copied back over itself once per widget, mid-loop and half finished.
	if (!bIsSelecting)
	{
		SelectedWidgets = UDreamUISelection::GetInstance(GetWorld())->GetSelectedWidgets();
		OnSelectionChanged.Broadcast();
	}
	// The tree rebuild is not part of that copy and must not be guarded with it: an operation that
	// creates a widget and then selects it needs the row to exist, and this is the refresh that
	// used to make it appear.
	RefreshOutliner();
}

void FDreamUIPrefabEditor::BuildTabDescriptors()
{
	TabDescriptors.Reset();
	auto Content = [](TSharedPtr<SWidget> Widget) { return [Widget]() { return Widget.ToSharedRef(); }; };

	TabDescriptors.Add({ FDreamUIPrefabEditorTabs::ViewportID, LOCTEXT("ViewportTab", "Viewport"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"), Content(ViewportPtr) });
	TabDescriptors.Add({ FDreamUIPrefabEditorTabs::DetailsID, LOCTEXT("DetailsTabLabel", "Details"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"), Content(DetailsPtr) });
	TabDescriptors.Add({ FDreamUIPrefabEditorTabs::OutlinerID, LOCTEXT("OutlinerTabLabel", "Hierarchy"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"), Content(OutlinerPtr) });
	TabDescriptors.Add({ FDreamUIPrefabEditorTabs::PaletteID, LOCTEXT("PaletteTabLabel", "Palette"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Kismet.Tabs.Palette"), Content(PalettePtr) });
	FTabDescriptor SequencerTab{ FDreamUIPrefabEditorTabs::SequencerID, LOCTEXT("SequencerTabLabel", "Animations"),
		FSlateIcon(FUMGStyle::GetStyleSetName(), "Animations.TabIcon"), Content(SequencerPtr) };
	// Closing the panel must also leave animation mode; the sequencer would otherwise keep driving
	// the viewport (and auto-keying) with nothing visible to say so.
	SequencerTab.OnClosed = [this]() { if (SequencerPtr.IsValid()) { SequencerPtr->ClearAnimationSelection(); } };
	TabDescriptors.Add(MoveTemp(SequencerTab));
	TabDescriptors.Add({ FDreamUIPrefabEditorTabs::CompilerResultsID, LOCTEXT("CompilerResultsTabLabel", "Compiler Results"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Message"),
		[this]()
		{
			check(CompilerResultsListing.IsValid());
			return FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog").CreateLogListingWidget(CompilerResultsListing.ToSharedRef());
		} });
	TabDescriptors.Add({ FDreamUIPrefabEditorTabs::PrefabBehaviourViewerID, LOCTEXT("PrefabBehaviourViewerTabLabel", "Behaviour"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Event"), Content(PrefabBehaviourViewer),
		[this]() { if (PrefabBehaviourViewer.IsValid()) { PrefabBehaviourViewer->Rebuild(); } } });
	TabDescriptors.Add({ FDreamUIPrefabEditorTabs::PrefabOverridesViewerID, LOCTEXT("PrefabOverridesViewerTabLabel", "Overrides"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Adjust"), Content(PrefabOverridesViewer),
		[this]() { if (PrefabOverridesViewer.IsValid()) { PrefabOverridesViewer->Rebuild(); } } });
	FTabDescriptor RawData{ FDreamUIPrefabEditorTabs::PrefabRawDataViewerID, LOCTEXT("PrefabRawDataViewerTabLabel", "Raw Data"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Advanced"), Content(PrefabRawDataViewer) };
	RawData.bListedInWindowMenu = false;
	TabDescriptors.Add(MoveTemp(RawData));
	// The sequencer invokes this tab by its engine-wide id and then fills it with the curve editor;
	// spawning it empty here just gives it a home in this window.
	FTabDescriptor CurveEditor{ FName("SequencerGraphEditor"), NSLOCTEXT("Sequencer", "SequencerMainGraphEditorTitle", "Sequencer Curves"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCurveEditor.TabIcon"), []() { return SNullWidget::NullWidget; } };
	CurveEditor.bListedInWindowMenu = false;
	TabDescriptors.Add(MoveTemp(CurveEditor));
}

const FDreamUIPrefabEditor::FTabDescriptor* FDreamUIPrefabEditor::FindTabDescriptor(FName TabId) const
{
	return TabDescriptors.FindByPredicate([TabId](const FTabDescriptor& Desc) { return Desc.Id == TabId; });
}

TSharedRef<SDockTab> FDreamUIPrefabEditor::SpawnTabFromDescriptor(const FSpawnTabArgs& Args, FName TabId)
{
	const FTabDescriptor* Desc = FindTabDescriptor(TabId);
	checkf(Desc, TEXT("No tab descriptor for %s"), *TabId.ToString());
	if (Desc->OnSpawn)
	{
		Desc->OnSpawn();
	}
	TSharedRef<SDockTab> NewTab = SNew(SDockTab)
		.Label(Desc->Label)
		[
			Desc->MakeContent()
		];
	if (Desc->OnClosed)
	{
		NewTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda(
			[OnClosed = Desc->OnClosed](TSharedRef<SDockTab>) { OnClosed(); }));
	}
	return NewTab;
}

void FDreamUIPrefabEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_DreamUIPrefabEditor", "DreamUIPrefab Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	if (TabDescriptors.Num() == 0)
	{
		BuildTabDescriptors();
	}
	for (const FTabDescriptor& Desc : TabDescriptors)
	{
		InTabManager->RegisterTabSpawner(Desc.Id, FOnSpawnTab::CreateSP(this, &FDreamUIPrefabEditor::SpawnTabFromDescriptor, Desc.Id))
			.SetDisplayName(Desc.Label)
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(Desc.Icon)
			.SetMenuType(Desc.bListedInWindowMenu ? ETabSpawnerMenuType::Enabled : ETabSpawnerMenuType::Hidden);
	}
}
void FDreamUIPrefabEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	for (const FTabDescriptor& Desc : TabDescriptors)
	{
		InTabManager->UnregisterTabSpawner(Desc.Id);
	}
}

TSharedRef<FTabManager::FLayout> FDreamUIPrefabEditor::CreateDefaultLayout()
{
	// Bump the version whenever the tab set or the arrangement changes: the tab manager restores a
	// user's saved layout by this name, so an unchanged name means an edit here never reaches anyone
	// who has opened the editor before.
	//
	//   +-----------+--------------------------------+-----------+
	//   | Palette   |                                |           |
	//   +-----------+           Viewport             |  Details  |
	//   | Hierarchy |                                |           |
	//   |           +--------------------------------+-----------+
	//   |           | Animations | Compiler Results | Behaviour | Overrides | Raw Data   (all closed)
	//   +-----------+--------------------------------------------+
	constexpr float LeftColumnWidth = 0.15f;
	constexpr float ViewportWidth = 0.75f;
	constexpr float BottomDrawerHeight = 0.3f;
	return FTabManager::NewLayout("Standalone_DreamUIPrefabEditor_Layout_v5")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(LeftColumnWidth)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5f)
					->AddTab(FDreamUIPrefabEditorTabs::PaletteID, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5f)
					->AddTab(FDreamUIPrefabEditorTabs::OutlinerID, ETabState::OpenedTab)
				)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(1.0f - LeftColumnWidth)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Horizontal)
					->SetSizeCoefficient(1.0f - BottomDrawerHeight)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(ViewportWidth)
						->SetHideTabWell(true)
						->AddTab(FDreamUIPrefabEditorTabs::ViewportID, ETabState::OpenedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(1.0f - ViewportWidth)
						->AddTab(FDreamUIPrefabEditorTabs::DetailsID, ETabState::OpenedTab)
					)
				)
				->Split
				(
					// Every secondary panel has a home here, so InvokeTab lands it in a known place
					// instead of wherever the tab manager guesses.
					FTabManager::NewStack()
					->SetSizeCoefficient(BottomDrawerHeight)
					->SetForegroundTab(FDreamUIPrefabEditorTabs::SequencerID)
					->AddTab(FDreamUIPrefabEditorTabs::SequencerID, ETabState::ClosedTab)
					->AddTab(FDreamUIPrefabEditorTabs::CompilerResultsID, ETabState::ClosedTab)
					->AddTab(FDreamUIPrefabEditorTabs::PrefabBehaviourViewerID, ETabState::ClosedTab)
					->AddTab(FDreamUIPrefabEditorTabs::PrefabOverridesViewerID, ETabState::ClosedTab)
					->AddTab(FDreamUIPrefabEditorTabs::PrefabRawDataViewerID, ETabState::ClosedTab)
					->AddTab(FName("SequencerGraphEditor"), ETabState::ClosedTab)
				)
			)
		);
}

void FDreamUIPrefabEditor::PostUndo(bool bSuccess)
{
	HandlePostTransaction(bSuccess);
}
void FDreamUIPrefabEditor::PostRedo(bool bSuccess)
{
	HandlePostTransaction(bSuccess);
}

void FDreamUIPrefabEditor::HandlePostTransaction(bool bSuccess)
{
	if (!bSuccess || !IsValid(PrefabBeingEdited))
	{
		return;
	}

	FDreamUIPrefabInstanceScene* PreviewScene = PrefabBeingEdited->GetPrefabInstanceScene();
	if (!PreviewScene)
	{
		return;
	}
	UWorld* EditorWorld = PreviewScene->GetWorld();
	UDreamWidget* RootAgent = PreviewScene->GetRootAgent();
	if (!IsValid(EditorWorld) || !IsValid(RootAgent))
	{
		return;
	}

	SyncWidgetRegisterStateAfterTransaction(RootAgent, EditorWorld);
	ApplyDesignerState();
	UDreamUIManagerWorldSubsystem::RefreshAllUI(EditorWorld);
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(EditorWorld))
	{
		Manager->MarkDreamUIWidgetOutlinerChanged();
	}
	if (UDreamUISelection* Selection = UDreamUISelection::GetInstance(EditorWorld))
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

void FDreamUIPrefabEditor::InitPrefabEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost >& InitToolkitHost, UDreamUIPrefab* InPrefab)
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
		.CreateLogListing(*FString::Printf(TEXT("DreamUIPrefabCompiler_%p"), this), LogOptions);

	FDreamUIPrefabEditorCommand::Register();

	PrefabBeingEdited->EnsureInstanceObjects();

	TSharedPtr<FDreamUIPrefabEditor> PrefabEditorPtr = SharedThis(this);

	ViewportPtr = SNew(SDreamUIPrefabEditorViewport, PrefabEditorPtr, PrefabBeingEdited->PrefabDataForPrefabEditor.ViewMode);
	
	DetailsPtr = SNew(SDreamUIPrefabEditorDetails, GetWorld());

	PrefabRawDataViewer = SNew(SDreamUIPrefabRawDataViewer, PrefabEditorPtr, PrefabBeingEdited);

	PrefabOverridesViewer = SNew(SDreamUIPrefabOverridesViewer, PrefabEditorPtr, PrefabBeingEdited);

	PrefabBehaviourViewer = SNew(SDreamUIPrefabBehaviourViewer, PrefabEditorPtr, PrefabBeingEdited);
	
	UDreamUIManagerWorldSubsystem::GetInstance(GetWorld())->OnDreamUIWidgetOutlinerChanged.AddSPLambda(this, [=, this]()
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
	UDreamUIManagerWorldSubsystem::GetInstance(GetWorld())->bShouldTickInEditor = true;
	UDreamUISelection::GetInstance(GetWorld())->OnSelectionChanged.AddSPLambda(this, [=, this]
	{
		SyncSelection();
	});
	
	OutlinerPtr = SNew(SDreamWidgetEditorHierarchyView, GetWorld());
	PalettePtr = SNew(SDreamUIPrefabPalette, SharedThis(this));
	if (UDreamWidget* RootWidget = GetLoadedRootWidget())
	{
		const int32 RenameCount = FDreamUIEditorTools::EnsureUniqueWidgetDisplayNames(RootWidget);
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

	SequencerPtr = SNew(SDreamUIPrefabSequenceEditor);
	// The sequencer's side panels (the curve editor) must dock into this window, not the level editor.
	SequencerPtr->SetToolkitHost(GetToolkitHost());
	
	BindCommands();
	ExtendToolbar();
	// InitAssetEditor below builds the menus, so a project's extenders have to be registered first.
	AddMenuExtender(FDreamGUIEditorModule::Get().GetMenuExtensibilityManager()->GetAllExtenders(
		GetToolkitCommands(), TArray<UObject*>{ GetPrefabBeingEdited() }));
	AddToolbarExtender(FDreamGUIEditorModule::Get().GetToolBarExtensibilityManager()->GetAllExtenders(
		GetToolkitCommands(), TArray<UObject*>{ GetPrefabBeingEdited() }));

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = CreateDefaultLayout();

	InitAssetEditor(Mode, InitToolkitHost, PrefabEditorAppName, StandaloneDefaultLayout, true, true, PrefabBeingEdited);
	if (!bRegisteredForUndo && GEditor)
	{
		GEditor->RegisterForUndo(this);
		bRegisteredForUndo = true;
	}

	// After opening a prefab, broadcast event to DreamUIPrefabSequencerEditor
	FDreamUIEditorTools::OnEditingPrefabChanged.Broadcast(GetLoadedRootWidget());
	RunInitialReferenceValidation();
}

void FDreamUIPrefabEditor::GetInitialViewSetting(FVector& OutLocation, FRotator& OutRotation, FVector& OutOrbitLocation, ELevelViewportType& OutViewType)
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

UDreamWidget* FDreamUIPrefabEditor::GetRootAgentWidget()
{
	return GetPreviewScene()->GetRootAgent();
}

UDreamWidget* FDreamUIPrefabEditor::GetLoadedRootWidget()
{
	return GetPrefabHelperObject()->LoadedRootWidget;
}

FName FDreamUIPrefabEditor::GetSequencerTabID()
{
	return FDreamUIPrefabEditorTabs::SequencerID;
}

UDreamUIPrefabSequence* FDreamUIPrefabEditor::GetAnimationBeingEdited()const
{
	return SequencerPtr.IsValid() ? SequencerPtr->GetPrefabSequence() : nullptr;
}

void FDreamUIPrefabEditor::SaveAsset_Execute()
{
	ApplyPrefabChanges();
	if (bLastApplySerializationSucceeded)
	{
		SaveAppliedPrefabToDisk();
	}
}

void FDreamUIPrefabEditor::SaveAppliedPrefabToDisk()
{
	SaveEditorState();
	FAssetEditorToolkit::SaveAsset_Execute();
	FDreamUIEditorTools::RefreshLoadedPrefab();
	FDreamUIEditorTools::RefreshOnSubPrefabChange(GetPrefabHelperObject()->PrefabAsset);
}

void FDreamUIPrefabEditor::OnOpenOverridesViewerPanel()
{
	this->InvokeTab(FDreamUIPrefabEditorTabs::PrefabOverridesViewerID);
}

void FDreamUIPrefabEditor::OnOpenBehaviourViewerPanel()
{
	this->InvokeTab(FDreamUIPrefabEditorTabs::PrefabBehaviourViewerID);
}

void FDreamUIPrefabEditor::OnOpenRawDataViewerPanel()
{
	this->InvokeTab(FDreamUIPrefabEditorTabs::PrefabRawDataViewerID);
}
void FDreamUIPrefabEditor::OnOpenPrefabHelperObjectDetailsPanel()
{
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	AssetEditorSubsystem->OpenEditorForAsset(GetPrefabHelperObject());
}

UBlueprint* FDreamUIPrefabEditor::GetOrCreateBehaviourBlueprint()
{
	UDreamWidget* RootWidget = GetLoadedRootWidget();
	if (RootWidget == nullptr || PrefabBeingEdited == nullptr)return nullptr;

	UBlueprint* Blueprint = nullptr;
	if (UDreamUIBehaviour* PrimaryBehaviour = GetPrimaryBehaviour())
	{
		Blueprint = Cast<UBlueprint>(PrimaryBehaviour->GetClass()->ClassGeneratedBy);
		if (Blueprint == nullptr)
		{
			return nullptr;
		}
	}
	if (Blueprint == nullptr)
	{
		Blueprint = DreamUIPrefabBehaviourUtils::CreateBehaviourBlueprint(PrefabBeingEdited, RootWidget);
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

void FDreamUIPrefabEditor::CreateOrOpenBehaviourBlueprint()
{
	UClass* BehaviourClass = GetEffectiveBehaviourClass();
	if (BehaviourClass == nullptr)
	{
		CreateAndAssignBehaviourBlueprint();
		return;
	}
	if (TSharedPtr<IDreamUIBehaviourEditorBackend> Backend = GetBehaviourEditorBackend())
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

void FDreamUIPrefabEditor::CreateAndAssignBehaviourBlueprint()
{
	UDreamWidget* RootWidget = GetLoadedRootWidget();
	if (!IsValid(RootWidget) || !IsValid(PrefabBeingEdited))return;

	if (UBlueprint* ExistingBlueprint = Cast<UBlueprint>(GetEffectiveBehaviourClass() ? GetEffectiveBehaviourClass()->ClassGeneratedBy : nullptr))
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ExistingBlueprint);
		return;
	}

	UBlueprint* Blueprint = DreamUIPrefabBehaviourUtils::CreateBehaviourBlueprint(PrefabBeingEdited, RootWidget);
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

UClass* FDreamUIPrefabEditor::GetEffectiveBehaviourClass() const
{
	if (!IsValid(PrefabBeingEdited))return nullptr;
	if (UClass* ExplicitClass = PrefabBeingEdited->GetBehaviourClass())
	{
		return ExplicitClass;
	}
	if (UDreamWidget* RootWidget = const_cast<FDreamUIPrefabEditor*>(this)->GetLoadedRootWidget())
	{
		if (UDreamUIBehaviour* LegacyCompanion = DreamUIPrefabBehaviourUtils::FindBehaviourComponent(RootWidget, PrefabBeingEdited))
		{
			return LegacyCompanion->GetClass();
		}
	}
	return nullptr;
}

UDreamUIBehaviour* FDreamUIPrefabEditor::GetPrimaryBehaviour() const
{
	UDreamWidget* RootWidget = const_cast<FDreamUIPrefabEditor*>(this)->GetLoadedRootWidget();
	UClass* BehaviourClass = GetEffectiveBehaviourClass();
	if (!IsValid(RootWidget) || !IsValid(BehaviourClass))return nullptr;

	UDreamUIBehaviour* Match = nullptr;
	for (UDreamUIBehaviour* Component : RootWidget->GetAllComponents())
	{
		if (IsValid(Component) && Component->GetClass() == BehaviourClass)
		{
			if (Match != nullptr)return nullptr;
			Match = Component;
		}
	}
	return Match;
}

TSharedPtr<IDreamUIBehaviourEditorBackend> FDreamUIPrefabEditor::GetBehaviourEditorBackend() const
{
	return FDreamUIBehaviourEditorBackendRegistry::Get().FindBackend(GetEffectiveBehaviourClass());
}

bool FDreamUIPrefabEditor::CanAuthorBehaviour() const
{
	UClass* BehaviourClass = GetEffectiveBehaviourClass();
	if (BehaviourClass == nullptr)return true;
	TSharedPtr<IDreamUIBehaviourEditorBackend> Backend = GetBehaviourEditorBackend();
	return Backend.IsValid()
		&& (Backend->CanPromoteToVariable(BehaviourClass) || Backend->CanAddEventHandler(BehaviourClass));
}

void FDreamUIPrefabEditor::PickBehaviourClass()
{
	FClassViewerInitializationOptions Options;
	Options.DisplayMode = EClassViewerDisplayMode::TreeView;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.bShowNoneOption = true;
	Options.bShowUnloadedBlueprints = true;
	Options.bEnableClassDynamicLoading = true;
	Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::Dynamic;
	Options.InitiallySelectedClass = GetEffectiveBehaviourClass();
	Options.ClassFilters.Add(MakeShared<DreamUIPrefabEditorLocal::FBehaviourClassFilter>());

	UClass* ChosenClass = GetEffectiveBehaviourClass();
	if (!SClassPickerDialog::PickClass(LOCTEXT("PickPrefabBehaviourClass", "Pick Root Behaviour for DreamUI Prefab"),
		Options, ChosenClass, UDreamUIBehaviour::StaticClass()))
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

bool FDreamUIPrefabEditor::AssignBehaviourClass(UClass* InClass)
{
	UDreamWidget* RootWidget = GetLoadedRootWidget();
	UDreamUIPrefabHelperObject* Helper = GetPrefabHelperObject();
	if (!IsValid(InClass) || !InClass->IsChildOf(UDreamUIBehaviour::StaticClass())
		|| InClass->HasAnyClassFlags(DreamUIPrefabEditorLocal::FBehaviourClassFilter::DisallowedFlags)
		|| !IsValid(RootWidget) || !IsValid(Helper))
	{
		return false;
	}

	TArray<UDreamUIBehaviour*> MatchingComponents;
	for (UDreamUIBehaviour* Component : RootWidget->GetAllComponents())
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

	UDreamUIBehaviour* OldBehaviour = GetPrimaryBehaviour();
	UDreamUIBehaviour* NewBehaviour = MatchingComponents.IsEmpty() ? nullptr : MatchingComponents[0];
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
	FDreamUIUtils::NotifyPropertyChanged(RootWidget, UDreamWidget::GetPropertyName_Components());
	SelectWidgets(TSet<UDreamWidget*>{RootWidget}, false);
	return true;
}

bool FDreamUIPrefabEditor::ReplacePrimaryBehaviour(UDreamUIBehaviour* InOldBehaviour, UDreamUIBehaviour* InNewBehaviour, bool bNewBehaviourWasCreated)
{
	UDreamWidget* RootWidget = GetLoadedRootWidget();
	UDreamUIPrefabHelperObject* Helper = GetPrefabHelperObject();
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

	TArray<UDreamWidget*> WidgetStack;
	WidgetStack.Add(RootWidget);
	while (!WidgetStack.IsEmpty())
	{
		UDreamWidget* Widget = WidgetStack.Pop();
		for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
		{
			if (!IsValid(Component))continue;
			for (TFieldIterator<FStructProperty> It(Component->GetClass()); It; ++It)
			{
				FStructProperty* StructProperty = *It;
				if (StructProperty->Struct == FDreamUIEventDelegate::StaticStruct())
				{
					FDreamUIEventDelegate* Event = StructProperty->ContainerPtrToValuePtr<FDreamUIEventDelegate>(Component);
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
		TArray<UDreamWidget*> RefreshStack;
		RefreshStack.Add(RootWidget);
		while (!RefreshStack.IsEmpty())
		{
			UDreamWidget* Widget = RefreshStack.Pop();
			for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
			{
				if (!IsValid(Component))continue;
				for (TFieldIterator<FStructProperty> It(Component->GetClass()); It; ++It)
				{
					FStructProperty* StructProperty = *It;
					if (StructProperty->Struct == FDreamUIEventDelegate::StaticStruct())
					{
						StructProperty->ContainerPtrToValuePtr<FDreamUIEventDelegate>(Component)->ReplaceBindingTarget(InNewBehaviour, InNewBehaviour);
					}
				}
			}
			RefreshStack.Append(Widget->GetChildren());
		}
	}
	if (OldIndex != INDEX_NONE)RootWidget->MoveComponentToIndex(InNewBehaviour, OldIndex);
	return true;
}

void FDreamUIPrefabEditor::RemovePrimaryBehaviour()
{
	UDreamUIBehaviour* OldBehaviour = GetPrimaryBehaviour();
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
		UDreamWidget* RootWidget = GetLoadedRootWidget();
		UDreamUIPrefabHelperObject* Helper = GetPrefabHelperObject();
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
		FDreamUIUtils::NotifyPropertyChanged(RootWidget, UDreamWidget::GetPropertyName_Components());
		Helper->SetAnythingDirty();
	}
	PrefabBeingEdited->SetBehaviourClass(nullptr);
}

void FDreamUIPrefabEditor::PromoteToBehaviourVariable(UObject* InTarget)
{
	if (InTarget == nullptr)return;
	// The refusal belongs here rather than on the panel: the behaviour viewer's button is one of two
	// ways in, and the hierarchy's "Promote to Behaviour Variable" context entry reaches this
	// function without passing any sub-prefab check. This prefab serializes references only to
	// widgets it owns, so a variable promoted onto a borrowed one comes back null after a save --
	// which is also why AutoBindAndValidate refuses these targets on its own pass.
	if (UDreamWidget* TargetWidget = Cast<UDreamWidget>(InTarget); TargetWidget != nullptr && WidgetBelongsToSubPrefab(TargetWidget))
	{
		FNotificationInfo Info(LOCTEXT("PromoteInsideSubPrefab", "Open the sub prefab to promote a widget it owns; a variable bound here would be empty after a save."));
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
		Info.ExpireDuration = 7.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}
	if (GetEffectiveBehaviourClass() == nullptr && GetOrCreateBehaviourBlueprint() == nullptr)return;
	UDreamUIBehaviour* PrimaryBehaviour = GetPrimaryBehaviour();
	TSharedPtr<IDreamUIBehaviourEditorBackend> Backend = GetBehaviourEditorBackend();
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

bool FDreamUIPrefabEditor::CanAddEventHandler(EDreamUIBehaviourHandlerType InHandlerType) const
{
	UClass* BehaviourClass = GetEffectiveBehaviourClass();
	if (BehaviourClass == nullptr)
	{
		return true;
	}
	TSharedPtr<IDreamUIBehaviourEditorBackend> Backend = GetBehaviourEditorBackend();
	return Backend.IsValid() && Backend->CanAddEventHandler(BehaviourClass, InHandlerType);
}

void FDreamUIPrefabEditor::AddEventHandler(const DreamUIPrefabBehaviourUtils::FDiscoveredEvent& InEvent,
	EDreamUIBehaviourHandlerType InHandlerType)
{
	if (!IsValid(InEvent.Component) || InEvent.EventProperty == nullptr)
	{
		return;
	}
	FDreamUIEventDelegate* LiveEvent = InEvent.EventProperty->ContainerPtrToValuePtr<FDreamUIEventDelegate>(InEvent.Component);
	if (LiveEvent->IsBound())
	{
		FNotificationInfo Info(LOCTEXT("EventHandlerAlreadyBound", "This event already has a binding."));
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}
	if (GetEffectiveBehaviourClass() == nullptr && GetOrCreateBehaviourBlueprint() == nullptr)return;
	UDreamUIBehaviour* PrimaryBehaviour = GetPrimaryBehaviour();
	TSharedPtr<IDreamUIBehaviourEditorBackend> Backend = GetBehaviourEditorBackend();
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
			if (InHandlerType == EDreamUIBehaviourHandlerType::Function)
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
	// A widget's axis-aligned bounds in ITS PARENT's frame. DreamGUI's UI plane is YZ
	// (RelativeLocation.Y = horizontal, .Z = vertical, .X = the plane normal), confirmed by
	// UDreamWidget::CalculateAnchorFromTransform. Local-space edges are pushed through the
	// widget's local transform so per-widget scale/rotation is accounted for (AABB via min/max).
	struct FParentSpaceRect
	{
		double Left = 0, Right = 0, Bottom = 0, Top = 0;
		double CenterH() const { return (Left + Right) * 0.5; }
		double CenterV() const { return (Bottom + Top) * 0.5; }
	};
	FParentSpaceRect GetParentSpaceRect(UDreamWidget* W)
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
	bool GatherSharedParentSelection(const TArray<TWeakObjectPtr<UDreamWidget>>& InSelection, int32 InMinCount, bool bRefuseLayoutParent, TArray<UDreamWidget*>& OutWidgets, UDreamWidget*& OutParent)
	{
		OutWidgets.Reset();
		OutParent = nullptr;
		UDreamWidget* CommonParent = nullptr;
		bool bParentSet = false;
		for (const TWeakObjectPtr<UDreamWidget>& Weak : InSelection)
		{
			UDreamWidget* W = Weak.Get();
			if (W == nullptr) continue;
			UDreamWidget* Parent = W->GetParent();
			if (Parent == nullptr) continue;//root / detached: has no sibling frame
			if (!bParentSet) { CommonParent = Parent; bParentSet = true; }
			else if (Parent != CommonParent)
			{
				FNotificationInfo Info(NSLOCTEXT("DreamUIPrefabEditor", "SharedParentRequired", "This action needs the selected widgets to share a parent."));
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
			FNotificationInfo Info(NSLOCTEXT("DreamUIPrefabEditor", "AlignLayoutParent", "The shared parent has a layout container that positions its children -- align/distribute would be overridden by the layout."));
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

void FDreamUIPrefabEditor::AlignSelectedWidgets(EDreamUIWidgetAlignType AlignType)
{
	TArray<UDreamWidget*> Widgets;
	UDreamWidget* CommonParent = nullptr;
	if (!GatherSharedParentSelection(GetSelectedWidgets(), 2, /*bRefuseLayoutParent*/true, Widgets, CommonParent)) return;

	// Selection bound in the shared parent's frame.
	TArray<FParentSpaceRect> Rects;
	Rects.Reserve(Widgets.Num());
	double GroupLeft = TNumericLimits<double>::Max(), GroupRight = TNumericLimits<double>::Lowest();
	double GroupBottom = TNumericLimits<double>::Max(), GroupTop = TNumericLimits<double>::Lowest();
	for (UDreamWidget* W : Widgets)
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

	const FScopedTransaction Transaction(NSLOCTEXT("DreamUIPrefabEditor", "AlignWidgets", "Align Widgets"));
	for (int32 i = 0; i < Widgets.Num(); i++)
	{
		UDreamWidget* W = Widgets[i];
		const FParentSpaceRect& R = Rects[i];
		//parent-frame delta applies straight to anchoredPosition (X=horizontal, Y=vertical), same as the arrow-key nudge
		FVector2D AnchoredPos = W->GetAnchoredPosition();
		switch (AlignType)
		{
		case EDreamUIWidgetAlignType::LeftEdge:         AnchoredPos.X += GroupLeft - R.Left; break;
		case EDreamUIWidgetAlignType::RightEdge:        AnchoredPos.X += GroupRight - R.Right; break;
		case EDreamUIWidgetAlignType::HorizontalCenter: AnchoredPos.X += GroupCenterH - R.CenterH(); break;
		case EDreamUIWidgetAlignType::TopEdge:          AnchoredPos.Y += GroupTop - R.Top; break;
		case EDreamUIWidgetAlignType::BottomEdge:       AnchoredPos.Y += GroupBottom - R.Bottom; break;
		case EDreamUIWidgetAlignType::VerticalCenter:   AnchoredPos.Y += GroupCenterV - R.CenterV(); break;
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

void FDreamUIPrefabEditor::DistributeSelectedWidgets(bool bHorizontal)
{
	TArray<UDreamWidget*> Widgets;
	UDreamWidget* CommonParent = nullptr;
	if (!GatherSharedParentSelection(GetSelectedWidgets(), 3, /*bRefuseLayoutParent*/true, Widgets, CommonParent)) return;

	// Pair each widget with its parent-frame rect and sort along the distribute axis by low edge.
	struct FEntry { UDreamWidget* Widget; FParentSpaceRect Rect; };
	TArray<FEntry> Entries;
	Entries.Reserve(Widgets.Num());
	for (UDreamWidget* W : Widgets) { Entries.Add({ W, GetParentSpaceRect(W) }); }
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

	const FScopedTransaction Transaction(NSLOCTEXT("DreamUIPrefabEditor", "DistributeWidgets", "Distribute Widgets"));
	double Cursor = HighEdge(Entries[0].Rect);//trailing edge of the fixed first widget
	for (int32 i = 1; i < Entries.Num() - 1; i++)
	{
		const double TargetLow = Cursor + Gap;
		const double Delta = TargetLow - LowEdge(Entries[i].Rect);
		UDreamWidget* W = Entries[i].Widget;
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

void FDreamUIPrefabEditor::CollectLayoutPanelDescriptors(const UClass* InExcludeClass, TArray<const FDreamUIControlDescriptor*>& OutDescriptors)
{
	OutDescriptors.Reset();
	for (const FDreamUIControlDescriptor& Descriptor : FDreamUIControlRegistry::Get().GetDescriptors())
	{
		UClass* PanelClass = Descriptor.LayoutContainerClass.Get();
		if (PanelClass == nullptr || PanelClass == InExcludeClass)
		{
			continue;
		}
		if (Descriptor.VisualClass.IsValid() || Descriptor.BehaviourClass.IsValid())
		{
			continue;//a control that happens to use a panel, not a panel
		}
		OutDescriptors.Add(&Descriptor);
	}
	OutDescriptors.Sort([](const FDreamUIControlDescriptor& A, const FDreamUIControlDescriptor& B)
	{
		return A.DisplayName.CompareTo(B.DisplayName) < 0;
	});
}

void FDreamUIPrefabEditor::WrapSelectedWidgets(UClass* InLayoutContainerClass)
{
	if (InLayoutContainerClass != nullptr && !InLayoutContainerClass->IsChildOf(UDreamLayoutContainer::StaticClass()))return;
	TArray<UDreamWidget*> Widgets;
	UDreamWidget* CommonParent = nullptr;
	// wrapping is fine under a layout parent (the wrapper just becomes a layout child), so don't refuse it
	if (!GatherSharedParentSelection(GetSelectedWidgets(), 1, /*bRefuseLayoutParent*/false, Widgets, CommonParent)) return;
	if (CommonParent == nullptr) return;

	TSet<const UDreamWidget*> SelectedSet;
	for (UDreamWidget* Widget : Widgets)
	{
		if (IsValid(Widget)) SelectedSet.Add(Widget);
	}
	int32 ValidChildCount = 0;
	int32 ReplacedChildCount = 0;
	for (const UDreamWidget* Child : CommonParent->GetChildren())
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
	for (UDreamWidget* W : Widgets)
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
		const TArray<UDreamWidget*>& Siblings = CommonParent->GetChildren();
		for (UDreamWidget* W : Widgets)
		{
			const int32 Idx = Siblings.IndexOfByKey(W);
			if (Idx != INDEX_NONE) MinSiblingIndex = FMath::Min(MinSiblingIndex, Idx);
		}
	}
	if (MinSiblingIndex == TNumericLimits<int32>::Max()) MinSiblingIndex = -1;

	FScopedTransaction Transaction(NSLOCTEXT("DreamUIPrefabEditor", "WrapWidgets", "Wrap Widgets"));
	CommonParent->Modify();

	struct FWidgetWrapState
	{
		UDreamWidget* Widget = nullptr;
		int32 SiblingIndex = INDEX_NONE;
		float Width = 0.0f;
		float Height = 0.0f;
	};
	TArray<FWidgetWrapState> WidgetStates;
	WidgetStates.Reserve(Widgets.Num());
	for (UDreamWidget* Widget : Widgets)
	{
		WidgetStates.Add({ Widget, Widget->GetSiblingIndex(), Widget->GetWidth(), Widget->GetHeight() });
	}
	WidgetStates.Sort([](const FWidgetWrapState& A, const FWidgetWrapState& B)
	{
		return A.SiblingIndex < B.SiblingIndex;
	});

	// New container, inserted where the selection was, sized to enclose it. Named after the palette's
	// own label for the panel, so the wrapper reads as the thing that was picked rather than as the
	// C++ class that happens to implement it.
	FString WrapperName = TEXT("Widget");
	if (InLayoutContainerClass != nullptr)
	{
		WrapperName = InLayoutContainerClass->GetName();
		TArray<const FDreamUIControlDescriptor*> Panels;
		CollectLayoutPanelDescriptors(nullptr, Panels);
		for (const FDreamUIControlDescriptor* Descriptor : Panels)
		{
			if (Descriptor->LayoutContainerClass.Get() == InLayoutContainerClass)
			{
				WrapperName = Descriptor->DisplayName.ToString();
				break;
			}
		}
	}
	UDreamWidget* Wrapper = NewObject<UDreamWidget>(CommonParent->GetOuter(), UDreamWidget::StaticClass(), NAME_None, RF_Public | RF_Transactional);
	Wrapper->Modify();//enroll the new widget in the transaction so undo/redo restores it (and re-registers it), like DeleteWidgets
	Wrapper->SetDisplayName(FDreamUIEditorTools::MakeUniqueWidgetDisplayName(CommonParent, WrapperName));
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
		UDreamWidget* W = State.Widget;
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

	// Null is the plain-widget choice, which is a wrapper with no panel rather than a default panel.
	if (InLayoutContainerClass != nullptr && Wrapper->CreateNewLayoutContainer(InLayoutContainerClass) == nullptr)
	{
		// The one refusal a designer can hit: single-child panels reject a selection of several.
		RestoreOriginalHierarchy();
		FNotificationInfo Info(FText::Format(
			LOCTEXT("WrapLayoutRefused", "{0} cannot hold {1} children."),
			InLayoutContainerClass->GetDisplayNameText(), FText::AsNumber(WidgetStates.Num())));
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

	SelectWidgets(TSet<UDreamWidget*>{ Wrapper }, false);

	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid()) ViewportPtr->GetViewportClient()->Invalidate();
}

bool FDreamUIPrefabEditor::CanFindReferencesForSelectedWidget() const
{
	return SelectedWidgets.Num() == 1 && SelectedWidgets[0].IsValid();
}

void FDreamUIPrefabEditor::FindReferencesForSelectedWidget()
{
	if (!CanFindReferencesForSelectedWidget())return;
	UDreamWidget* Target = SelectedWidgets[0].Get();
	UBlueprint* Blueprint = DreamUIPrefabBehaviourUtils::FindBehaviourBlueprint(GetLoadedRootWidget(), PrefabBeingEdited);
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
	const FString SearchTerm = FString::Printf(TEXT("\"%s\""), *DreamUIPrefabBehaviourUtils::MakeVariableNameForTarget(Target));
	static_cast<FBlueprintEditor*>(OpenedEditor)->SummonSearchUI(/*bSetFindWithinBlueprint*/true, SearchTerm);
}

void FDreamUIPrefabEditor::ReplaceSelectedWidgetLayout(UClass* PanelClass)
{
	if (!IsValid(PanelClass) || !PanelClass->IsChildOf(UDreamLayoutContainer::StaticClass()))return;
	const TArray<TWeakObjectPtr<UDreamWidget>>& Selection = GetSelectedWidgets();
	if (Selection.Num() != 1)return;
	UDreamWidget* Target = Selection[0].Get();
	if (!IsValid(Target))return;
	UDreamLayoutContainer* Existing = Target->GetLayoutContainer();
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

	FScopedTransaction Transaction(NSLOCTEXT("DreamUIPrefabEditor", "ReplaceWidgetLayout", "Replace Widget Layout"));
	Target->Modify();
	// CreateNewLayoutContainer carries the whole swap: it unregisters the old container, registers
	// the new one, and converts the children's slots to match. Undo is handled by
	// UDreamWidget::PostEditUndo, which re-registers whatever container the pointer lands back on.
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

void FDreamUIPrefabEditor::TogglePreviewRenderMode()
{
	FDreamUIPrefabInstanceScene* PreviewScene = GetPreviewScene();
	UDreamWidget* RootAgent = PreviewScene ? PreviewScene->GetRootAgent() : nullptr;
	UDreamCanvas* RootCanvas = IsValid(RootAgent) ? RootAgent->GetComponent<UDreamCanvas>() : nullptr;
	if (!IsValid(RootCanvas))
	{
		return;
	}
	// Between the two modes the designer actually lives in. Screen space is the projection play
	// uses -- the only one where a declared Perspective can show itself; world space is the editor
	// camera's, better for orbiting geometry. RenderTarget and the UE-renderer world space are
	// deliberate authoring choices, not preview states, so the toggle does not cycle through them.
	const bool bScreenSpace = RootCanvas->GetRenderMode() == EDreamRenderMode::ScreenSpaceOverlay;
	RootCanvas->SetRenderMode(bScreenSpace ? EDreamRenderMode::WorldSpace_DreamUI : EDreamRenderMode::ScreenSpaceOverlay);
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid())
	{
		ViewportPtr->GetViewportClient()->Invalidate();
	}
}

void FDreamUIPrefabEditor::FrameViewportFromCanvasEye()
{
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid())
	{
		StaticCastSharedPtr<FDreamUIPrefabEditorViewportClient>(ViewportPtr->GetViewportClient())->FrameFromCanvasEye();
	}
}

bool FDreamUIPrefabEditor::CanFrameViewportFromCanvasEye()const
{
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid())
	{
		return StaticCastSharedPtr<FDreamUIPrefabEditorViewportClient>(ViewportPtr->GetViewportClient())->CanFrameFromCanvasEye();
	}
	return false;
}

bool FDreamUIPrefabEditor::IsPreviewingScreenSpace()const
{
	FDreamUIPrefabInstanceScene* PreviewScene = const_cast<FDreamUIPrefabEditor*>(this)->GetPreviewScene();
	UDreamWidget* RootAgent = PreviewScene ? PreviewScene->GetRootAgent() : nullptr;
	UDreamCanvas* RootCanvas = IsValid(RootAgent) ? RootAgent->GetComponent<UDreamCanvas>() : nullptr;
	return IsValid(RootCanvas) && RootCanvas->GetRenderMode() == EDreamRenderMode::ScreenSpaceOverlay;
}

void FDreamUIPrefabEditor::SaveEditorState()
{
	//save view location and rotation
	auto ViewTransform = ViewportPtr->GetViewportClient()->GetViewTransform();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewLocation = ViewTransform.GetLocation();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewRotation = ViewTransform.GetRotation();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewOrbitLocation = ViewTransform.GetLookAt();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewportType = ViewportPtr->GetViewportClient()->GetViewportType();
	auto RootAgentWidget = GetPreviewScene()->GetRootAgent();
	PrefabBeingEdited->PrefabDataForPrefabEditor.CanvasSize = FIntPoint(RootAgentWidget->GetWidth(), RootAgentWidget->GetHeight());
	PrefabBeingEdited->CanvasSize = PrefabBeingEdited->PrefabDataForPrefabEditor.CanvasSize;
	auto RootCanvas = RootAgentWidget->GetComponent<UDreamCanvas>();
	PrefabBeingEdited->PrefabDataForPrefabEditor.CanvasRenderMode = (uint8)RootCanvas->GetRenderMode();
	PrefabBeingEdited->PrefabDataForPrefabEditor.ViewMode = ViewportPtr->GetViewportClient()->GetViewMode();

	TSet<TWeakObjectPtr<UDreamWidget>> ExpandWidgetSet;
	OutlinerPtr->GetExpandWidgets(ExpandWidgetSet);
	TSet<FGuid> UnexpandWidgetGuidArray;
	for (auto& KeyValue : GetPrefabHelperObject()->MapGuidToObject)
	{
		if (auto Widget = Cast<UDreamWidget>(KeyValue.Value))
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

void FDreamUIPrefabEditor::ValidatePrefabReferences(TArray<FDreamUIPrefabCompilerIssue>& OutIssues) const
{
	auto AddIssue = [&OutIssues](EDreamUIPrefabCompilerSeverity Severity, FString Message,
		UObject* SourceObject = nullptr, UDreamUIPrefabSequence* Animation = nullptr, bool bOpenRawData = false)
	{
		FDreamUIPrefabCompilerIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Severity = Severity;
		Issue.Message = MoveTemp(Message);
		Issue.SourceObject = SourceObject;
		Issue.Animation = Animation;
		Issue.bOpenRawData = bOpenRawData;
	};

	UDreamUIPrefab* Prefab = PrefabBeingEdited;
	UDreamUIPrefabHelperObject* Helper = IsValid(Prefab) ? Prefab->GetPrefabHelperObject() : nullptr;
	UDreamWidget* RootWidget = IsValid(Helper) ? Helper->LoadedRootWidget.Get() : nullptr;
	if (!IsValid(Prefab) || !IsValid(Helper) || !IsValid(RootWidget))
	{
		AddIssue(EDreamUIPrefabCompilerSeverity::Error, TEXT("The prefab, helper, or loaded root widget is unavailable."));
		return;
	}

	for (int32 Index = 0; Index < Prefab->ReferenceAssetList.Num(); ++Index)
	{
		if (!IsValid(Prefab->ReferenceAssetList[Index]))
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("ReferenceAssetList[%d] is missing."), Index), nullptr, nullptr, true);
		}
	}
	for (int32 Index = 0; Index < Prefab->ReferenceClassList.Num(); ++Index)
	{
		if (!IsValid(Prefab->ReferenceClassList[Index]))
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("ReferenceClassList[%d] is missing."), Index), nullptr, nullptr, true);
		}
	}

	TMap<UObject*, FGuid> FirstGuidByObject;
	for (const TPair<FGuid, TObjectPtr<UObject>>& Pair : Helper->MapGuidToObject)
	{
		if (!Pair.Key.IsValid())
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning, TEXT("Helper GUID map contains an invalid GUID."), Pair.Value.Get(), nullptr, true);
		}
		if (!IsValid(Pair.Value))
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Helper GUID '%s' points to a missing object."), *Pair.Key.ToString()), nullptr, nullptr, true);
			continue;
		}
		if (const FGuid* ExistingGuid = FirstGuidByObject.Find(Pair.Value.Get()))
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
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
	TArray<UDreamWidget*> Widgets;
	TSet<const UDreamWidget*> VisitedWidgets;
	Widgets.Add(RootWidget);
	for (int32 WidgetIndex = 0; WidgetIndex < Widgets.Num(); ++WidgetIndex)
	{
		UDreamWidget* Widget = Widgets[WidgetIndex];
		if (!IsValid(Widget))
		{
			continue;
		}
		if (VisitedWidgets.Contains(Widget))
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Widget hierarchy contains a duplicate or cyclic reference to '%s'."),
					*Widget->GetDisplayName()), Widget, nullptr, true);
			continue;
		}
		VisitedWidgets.Add(Widget);
		for (UDreamWidget* Child : Widget->GetChildren())
		{
			if (IsValid(Child))
			{
				if (Child->GetParent() != Widget)
				{
					AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
						FString::Printf(TEXT("Widget '%s' is listed under '%s' but points to a different parent."),
							*Child->GetDisplayName(), *Widget->GetDisplayName()), Child, nullptr, true);
				}
				Widgets.Add(Child);
			}
		}
		int32 ValidDirectChildCount = 0;
		for (const UDreamWidget* Child : Widget->GetChildren())
		{
			ValidDirectChildCount += IsValid(Child) ? 1 : 0;
		}
		const int32 ChildCapacity = Widget->GetMaxChildrenCapacity();
		if (ChildCapacity != INDEX_NONE && ValidDirectChildCount > ChildCapacity)
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Widget '%s' has %d direct children but its layout/behaviour capacity is %d."),
					*Widget->GetDisplayName(), ValidDirectChildCount, ChildCapacity), Widget);
		}

		int32 ContentWidgetCount = 0;
		for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
		{
			ContentWidgetCount += IsValid(Cast<UDreamContentWidget>(Component)) ? 1 : 0;
		}
		if (const UDreamLayoutContainer* LayoutContainer = Widget->GetLayoutContainer(); IsValid(LayoutContainer))
		{
			TArray<TSubclassOf<UDreamUIBehaviour>> RequiredClasses;
			LayoutContainer->GetRequiredBehaviourClasses(RequiredClasses);
			for (const TSubclassOf<UDreamUIBehaviour>& RequiredClass : RequiredClasses)
			{
				if (IsValid(*RequiredClass) && !IsValid(Widget->GetComponent(RequiredClass)))
				{
					AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
						FString::Printf(TEXT("Widget '%s' uses %s but has no %s behaviour. Re-select the panel type to add it."),
							*Widget->GetDisplayName(), *LayoutContainer->GetClass()->GetName(), *(*RequiredClass)->GetName()), Widget);
				}
			}
		}
		if (ContentWidgetCount > 1)
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Widget '%s' has %d ContentWidget behaviours; only one is allowed."),
					*Widget->GetDisplayName(), ContentWidgetCount), Widget);
		}
		if (bExpectCompleteGuidMap && !FirstGuidByObject.Contains(Widget))
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Widget '%s' is missing from the helper GUID map."), *Widget->GetDisplayName()), Widget, nullptr, true);
		}
		for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
		{
			if (bExpectCompleteGuidMap && IsValid(Component) && !FirstGuidByObject.Contains(Component))
			{
				AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
					FString::Printf(TEXT("Component '%s' on widget '%s' is missing from the helper GUID map."),
						*Component->GetName(), *Widget->GetDisplayName()), Component, nullptr, true);
			}
		}
	}
	for (const TPair<FGuid, TObjectPtr<UObject>>& Pair : Helper->MapGuidToObject)
	{
		UObject* Object = Pair.Value.Get();
		if (!IsValid(Object)) continue;
		const UDreamWidget* OwningWidget = Cast<UDreamWidget>(Object);
		if (!OwningWidget)
		{
			OwningWidget = Object->GetTypedOuter<UDreamWidget>();
		}
		if (!IsValid(OwningWidget) || !VisitedWidgets.Contains(OwningWidget))
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Helper GUID '%s' maps '%s', which is outside the prefab root hierarchy."),
					*Pair.Key.ToString(), *Object->GetName()), Object, nullptr, true);
		}
	}

	UClass* BehaviourClass = GetEffectiveBehaviourClass();
	if (BehaviourClass != nullptr)
	{
		if (!BehaviourClass->IsChildOf(UDreamUIBehaviour::StaticClass())
			|| BehaviourClass->HasAnyClassFlags(DreamUIPrefabEditorLocal::FBehaviourClassFilter::DisallowedFlags))
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("BehaviourClass '%s' is not a concrete usable UDreamUIBehaviour class."), *BehaviourClass->GetName()),
				Prefab);
		}
		int32 MatchCount = 0;
		UDreamUIBehaviour* Match = nullptr;
		for (UDreamUIBehaviour* Component : RootWidget->GetAllComponents())
		{
			if (IsValid(Component) && Component->GetClass() == BehaviourClass)
			{
				++MatchCount;
				Match = Component;
			}
		}
		if (MatchCount == 0)
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Primary Behaviour '%s' is not attached to the prefab root widget."), *BehaviourClass->GetName()), RootWidget);
		}
		else if (MatchCount > 1)
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Primary Behaviour '%s' is ambiguous: %d root components use this class."),
					*BehaviourClass->GetName(), MatchCount), RootWidget);
		}
		else if (bExpectCompleteGuidMap && !FirstGuidByObject.Contains(Match))
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Primary Behaviour '%s' has no helper GUID mapping."), *Match->GetName()), Match, nullptr, true);
		}
	}

	for (const TPair<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData>& Pair : Helper->SubPrefabMap)
	{
		UDreamWidget* SubPrefabRoot = Pair.Key.Get();
		const FDreamUISubPrefabData& Data = Pair.Value;
		if (!IsValid(SubPrefabRoot))
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning, TEXT("SubPrefabMap contains a missing root widget."), nullptr, nullptr, true);
		}
		if (!IsValid(Data.PrefabAsset))
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Sub-prefab root '%s' has no valid prefab asset."), *GetNameSafe(SubPrefabRoot)), SubPrefabRoot);
		}
		for (const TPair<FGuid, TObjectPtr<UObject>>& ObjectPair : Data.MapGuidToObject)
		{
			if (!ObjectPair.Key.IsValid() || !IsValid(ObjectPair.Value))
			{
				AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
					FString::Printf(TEXT("Sub-prefab '%s' contains an invalid object mapping for GUID '%s'."),
						*GetNameSafe(Data.PrefabAsset), *ObjectPair.Key.ToString()), SubPrefabRoot, nullptr, true);
			}
		}
		for (const FDreamUIPrefabOverrideParameterData& Override : Data.ObjectOverrideParameterArray)
		{
			UObject* OverrideObject = Override.Object.Get();
			if (!IsValid(OverrideObject))
			{
				AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
					FString::Printf(TEXT("Sub-prefab '%s' contains an override for a missing object."), *GetNameSafe(Data.PrefabAsset)),
					SubPrefabRoot, nullptr, true);
				continue;
			}
			for (FName PropertyName : Override.MemberPropertyNames)
			{
				FProperty* Property = FindFProperty<FProperty>(OverrideObject->GetClass(), PropertyName);
				if (Property == nullptr)
				{
					AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
						FString::Printf(TEXT("Override property '%s.%s' no longer exists."),
							*OverrideObject->GetClass()->GetName(), *PropertyName.ToString()), OverrideObject, nullptr, true);
				}
			}
		}
	}

	TArray<FString> IgnoredBindings;
	TArray<FString> BindingProblems;
	DreamUIPrefabBehaviourUtils::AutoBindAndValidate(RootWidget, Prefab, IgnoredBindings, BindingProblems, false);
	for (const FString& Problem : BindingProblems)
	{
		AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
			FString::Printf(TEXT("Behaviour variable: %s"), *Problem), GetPrimaryBehaviour());
	}

	for (UDreamWidget* Widget : Widgets)
	{
		if (!IsValid(Widget))
		{
			continue;
		}
		const TArray<UDreamUIBehaviour*>& Components = Widget->GetAllComponents();
		for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex)
		{
			UDreamUIBehaviour* Component = Components[ComponentIndex];
			if (!IsValid(Component))
			{
				AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
					FString::Printf(TEXT("Widget '%s' has a missing Behaviour component at slot %d."),
						*Widget->GetDisplayName(), ComponentIndex), Widget);
				continue;
			}
			for (TFieldIterator<FStructProperty> It(Component->GetClass()); It; ++It)
			{
				FStructProperty* EventProperty = *It;
				if (EventProperty->Struct != FDreamUIEventDelegate::StaticStruct())
				{
					continue;
				}
				const FDreamUIEventDelegate* Event = EventProperty->ContainerPtrToValuePtr<FDreamUIEventDelegate>(Component);
				TArray<FDreamUIEventBindingValidationIssue> EventIssues;
				Event->GetValidationIssues(EventIssues, RootWidget);
				for (const FDreamUIEventBindingValidationIssue& EventIssue : EventIssues)
				{
					AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
						FString::Printf(TEXT("%s.%s binding %d: %s"), *Component->GetName(), *EventProperty->GetName(),
							EventIssue.BindingIndex + 1, *EventIssue.Message), Component);
				}
			}

			if (UDreamUIPrefabSequenceComponent* SequenceComponent = Cast<UDreamUIPrefabSequenceComponent>(Component))
			{
				const TArray<UDreamUIPrefabSequence*>& Sequences = SequenceComponent->GetSequenceArray();
				for (int32 SequenceIndex = 0; SequenceIndex < Sequences.Num(); ++SequenceIndex)
				{
					UDreamUIPrefabSequence* Sequence = Sequences[SequenceIndex];
					if (!IsValid(Sequence))
					{
						AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
							FString::Printf(TEXT("Animation slot %d on '%s' is missing."), SequenceIndex, *Component->GetName()), Component);
						continue;
					}
					TArray<FGuid> InvalidBindingIds;
					Sequence->GetInvalidObjectBindingIds(RootWidget, InvalidBindingIds);
					if (Sequence->HasObjectBindingCountMismatch())
					{
						AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
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
						AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
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
	for (UDreamWidget* Widget : Widgets)
	{
		if (!IsValid(Widget) || Widget->GetIgnoreLayout() || !Widget->GetLayoutVisibleInHierarchy())
		{
			continue;
		}
		UDreamWidget* Parent = Widget->GetParent();
		UDreamPanelLayoutBase* ParentPanel = IsValid(Parent) ? Cast<UDreamPanelLayoutBase>(Parent->GetLayoutContainer()) : nullptr;
		if (!IsValid(ParentPanel))
		{
			continue;
		}
		const UDreamPanelSlot* Slot = Widget->GetPanelSlot();
		if (IsValid(Slot) && Slot->SizeRule != EDreamPanelSizeRule::Auto)
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
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("'%s' has no intrinsic size source on its Auto-measured %s axis and will collapse to zero under '%s'. Give it an authored size, a SizeBox override, or content with intrinsic size."),
					*Widget->GetDisplayName(),
					Desired.X <= 0.0 && Desired.Y <= 0.0 ? TEXT("X and Y") : (Desired.X <= 0.0 ? TEXT("X") : TEXT("Y")),
					*Parent->GetDisplayName()), Widget);
		}
	}

	// A scroll box only arranges (and scrolls) layout-participating children. Content converted from
	// the UIScrollView component workflow often keeps its old Ignore Layout flag — the scroll box then
	// excludes it entirely and MaxScrollOffset stays 0, which reads as "scrolling is broken".
	for (UDreamWidget* Widget : Widgets)
	{
		// Skip hierarchy-hidden scroll boxes: under a deactivated page every child reads as
		// non-participating, which used to fire this warning with a misleading "Ignore Layout"
		// message for perfectly healthy content.
		if (!IsValid(Widget) || !Cast<UDreamLayoutContainerScrollBox>(Widget->GetLayoutContainer())
			|| !Widget->GetLayoutVisibleInHierarchy())
		{
			continue;
		}
		int32 Participating = 0;
		for (UDreamWidget* Child : Widget->GetChildren())
		{
			if (IsValid(Child) && !Child->GetIgnoreLayout() && Child->GetLayoutVisibleInHierarchy())
			{
				Participating++;
			}
		}
		if (Participating == 0 && Widget->GetChildrenCount() > 0)
		{
			AddIssue(EDreamUIPrefabCompilerSeverity::Warning,
				FString::Printf(TEXT("Scroll box '%s' has children but none participate in layout (Ignore Layout is set on all of them) — nothing will scroll. Clear Ignore Layout on the content."),
					*Widget->GetDisplayName()), Widget);
		}
	}
}

void FDreamUIPrefabEditor::PublishCompilerResults(const FText& PageTitle,
	const TArray<FDreamUIPrefabCompilerIssue>& Issues, const FText& Summary, bool bAutoOpenOnProblems)
{
	if (!CompilerResultsListing.IsValid())
	{
		return;
	}
	CompilerResultsListing->NewPage(PageTitle);
	const TWeakPtr<FDreamUIPrefabEditor> WeakThis = SharedThis(this);
	bool bHasProblems = false;
	for (const FDreamUIPrefabCompilerIssue& Issue : Issues)
	{
		EMessageSeverity::Type Severity = EMessageSeverity::Info;
		if (Issue.Severity == EDreamUIPrefabCompilerSeverity::Warning)
		{
			Severity = EMessageSeverity::Warning;
			bHasProblems = true;
		}
		else if (Issue.Severity == EDreamUIPrefabCompilerSeverity::Error)
		{
			Severity = EMessageSeverity::Error;
			bHasProblems = true;
		}

		TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(Severity, FText::FromString(Issue.Message));
		TSharedPtr<FActionToken> ActionToken;
		if (Issue.Animation.IsValid())
		{
			const TWeakObjectPtr<UDreamUIPrefabSequence> WeakAnimation = Issue.Animation;
			ActionToken = FActionToken::Create(LOCTEXT("OpenAnimationIssueAction", "Open Animation"),
				LOCTEXT("OpenAnimationIssueActionTooltip", "Open and select the animation containing this binding."),
				FOnActionTokenExecuted::CreateLambda([WeakThis, WeakAnimation]()
				{
					if (TSharedPtr<FDreamUIPrefabEditor> Editor = WeakThis.Pin())
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
					if (TSharedPtr<FDreamUIPrefabEditor> Editor = WeakThis.Pin())
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
					if (TSharedPtr<FDreamUIPrefabEditor> Editor = WeakThis.Pin())
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
		InvokeTab(FDreamUIPrefabEditorTabs::CompilerResultsID);
	}
}

void FDreamUIPrefabEditor::RunInitialReferenceValidation()
{
	TArray<FDreamUIPrefabCompilerIssue> Issues;
	ValidatePrefabReferences(Issues);
	LastApplyWarningCount = 0;
	LastApplyErrorCount = 0;
	for (const FDreamUIPrefabCompilerIssue& Issue : Issues)
	{
		LastApplyWarningCount += Issue.Severity == EDreamUIPrefabCompilerSeverity::Warning ? 1 : 0;
		LastApplyErrorCount += Issue.Severity == EDreamUIPrefabCompilerSeverity::Error ? 1 : 0;
	}
	LastApplyStatus = LastApplyErrorCount > 0
		? EDreamUIPrefabApplyStatus::Error
		: (LastApplyWarningCount > 0 ? EDreamUIPrefabApplyStatus::Warning : EDreamUIPrefabApplyStatus::Unknown);
	const FText Summary = LastApplyErrorCount > 0 || LastApplyWarningCount > 0
		? FText::Format(LOCTEXT("OpenReferenceCheckProblems", "Reference check found {0} error(s) and {1} warning(s)."),
			FText::AsNumber(LastApplyErrorCount), FText::AsNumber(LastApplyWarningCount))
		: LOCTEXT("OpenReferenceCheckClean", "Reference check completed. No issues found.");
	PublishCompilerResults(FText::Format(LOCTEXT("OpenResultsPageTitle", "Open {0}"), FText::FromString(GetNameSafe(PrefabBeingEdited))),
		Issues, Summary, true);
}

void FDreamUIPrefabEditor::NavigateToCompilerObject(TWeakObjectPtr<UObject> InObject)
{
	UObject* Object = InObject.Get();
	if (!IsValid(Object))
	{
		return;
	}
	UDreamUIBehaviour* Behaviour = Cast<UDreamUIBehaviour>(Object);
	UDreamWidget* Widget = Cast<UDreamWidget>(Object);
	if (Behaviour != nullptr)
	{
		Widget = Behaviour->GetWidget();
	}
	if (Widget == nullptr)
	{
		Widget = Object->GetTypedOuter<UDreamWidget>();
	}
	if (!IsValid(Widget))
	{
		return;
	}
	InvokeTab(FDreamUIPrefabEditorTabs::DetailsID);
	SelectWidgets(TSet<UDreamWidget*>{Widget}, false);
	if (Behaviour != nullptr)
	{
		UDreamUISelection* Selection = UDreamUISelection::GetInstance(GetWorld());
		Selection->ClearComponentSelection();
		Selection->SelectComponent(Behaviour);
	}
}

void FDreamUIPrefabEditor::NavigateToAnimation(TWeakObjectPtr<UDreamUIPrefabSequence> InAnimation)
{
	if (!InAnimation.IsValid() || !SequencerPtr.IsValid())
	{
		return;
	}
	InvokeTab(FDreamUIPrefabEditorTabs::SequencerID);
	SequencerPtr->SelectAnimation(InAnimation.Get());
}

void FDreamUIPrefabEditor::FocusAnimationByDisplayName(const FString& InDisplayName)
{
	InvokeTab(FDreamUIPrefabEditorTabs::SequencerID);
	if (!SequencerPtr.IsValid() || InDisplayName.IsEmpty())
	{
		return;
	}
	if (UDreamUIPrefabSequenceComponent* Host = SequencerPtr->GetSequenceComponent())
	{
		if (UDreamUIPrefabSequence* Sequence = Host->GetSequenceByDisplayName(InDisplayName))
		{
			SequencerPtr->SelectAnimation(Sequence);
		}
	}
}

bool FDreamUIPrefabEditor::ApplyPrefabChanges()
{
	UDreamUIPrefab* Prefab = GetPrefabBeingEdited();
	UDreamUIPrefabHelperObject* Helper = IsValid(Prefab) ? Prefab->GetPrefabHelperObject() : nullptr;
	TArray<FDreamUIPrefabCompilerIssue> Issues;
	const FText PageTitle = FText::Format(
		LOCTEXT("ApplyResultsPageTitle", "Apply {0} - {1}"),
		FText::FromString(GetNameSafe(Prefab)),
		FText::AsTime(FDateTime::Now()));
	if (!IsValid(Helper) || !IsValid(Helper->LoadedRootWidget))
	{
		FDreamUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.Severity = EDreamUIPrefabCompilerSeverity::Error;
		Issue.Message = TEXT("Apply failed because the prefab root data is unavailable.");
		LastApplyStatus = EDreamUIPrefabApplyStatus::Error;
		LastApplyWarningCount = 0;
		LastApplyErrorCount = 1;
		bLastApplySerializationSucceeded = false;
		PublishCompilerResults(PageTitle, Issues, LOCTEXT("ApplyMissingRootSummary", "Apply failed: prefab root data is unavailable."), true);
		return false;
	}

	LastApplyStatus = EDreamUIPrefabApplyStatus::Unknown;
	LastApplyWarningCount = 0;
	LastApplyErrorCount = 0;
	bLastApplySerializationSucceeded = false;
	FDreamUIEditorTools::OnBeforeApplyPrefab.Broadcast(Helper);

	// Old assets used a convention-based BP_<PrefabName> component without storing the class.
	// Persist that already-loaded component as the explicit primary behaviour on first Apply.
	if (Prefab->GetBehaviourClass() == nullptr)
	{
		if (UDreamUIBehaviour* LegacyBehaviour = DreamUIPrefabBehaviourUtils::FindBehaviourComponent(Helper->LoadedRootWidget, Prefab))
		{
			Prefab->Modify();
			Prefab->SetBehaviourClass(LegacyBehaviour->GetClass());
			FDreamUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
			Issue.Severity = EDreamUIPrefabCompilerSeverity::Info;
			Issue.Message = FString::Printf(TEXT("Migrated legacy companion behaviour '%s' to BehaviourClass."), *LegacyBehaviour->GetClass()->GetName());
			Issue.SourceObject = LegacyBehaviour;
		}
	}

	if (UDreamWidget* RootWidget = GetLoadedRootWidget())
	{
		const int32 RenameCount = FDreamUIEditorTools::EnsureUniqueWidgetDisplayNames(RootWidget);
		if (RenameCount > 0)
		{
			FDreamUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
			Issue.Severity = EDreamUIPrefabCompilerSeverity::Info;
			Issue.Message = FString::Printf(TEXT("Renamed %d duplicate widget name(s) using UMG-style numeric suffixes."), RenameCount);
			if (OutlinerPtr.IsValid())
			{
				OutlinerPtr->RequestRefresh();
			}
		}
	}

	// UMG BindWidget-style pass, run just before the prefab is written. Validation runs below
	// after auto-wiring so each remaining problem is reported exactly once.
	if (UDreamWidget* RootWidget = GetLoadedRootWidget())
	{
		TArray<FString> BoundDetails, IgnoredProblems;
		DreamUIPrefabBehaviourUtils::AutoBindAndValidate(RootWidget, Prefab, BoundDetails, IgnoredProblems, true);
		if (BoundDetails.Num() > 0)
		{
			Helper->Modify();
			Helper->SetAnythingDirty();
			FDreamUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
			Issue.Severity = EDreamUIPrefabCompilerSeverity::Info;
			Issue.Message = FString::Printf(TEXT("Auto-bound %d Behaviour variable(s): %s"),
				BoundDetails.Num(), *FString::Join(BoundDetails, TEXT(", ")));
			Issue.SourceObject = GetPrimaryBehaviour();
		}
	}

	for (const FString& Warning : PendingBehaviourWarnings)
	{
		FDreamUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.Severity = EDreamUIPrefabCompilerSeverity::Warning;
		Issue.Message = Warning;
	}
	PendingBehaviourWarnings.Reset();

	const int32 RemovedStaleGuidMappings = Helper->CleanupObjectsOutsideRootHierarchy();
	if (RemovedStaleGuidMappings > 0)
	{
		FDreamUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.Severity = EDreamUIPrefabCompilerSeverity::Info;
		Issue.Message = FString::Printf(
			TEXT("Removed %d stale Helper GUID mapping(s) outside the prefab root hierarchy."),
			RemovedStaleGuidMappings);
	}

	ValidatePrefabReferences(Issues);
	const bool bHasStructuralError = Issues.ContainsByPredicate([](const FDreamUIPrefabCompilerIssue& Issue)
	{
		return Issue.Severity == EDreamUIPrefabCompilerSeverity::Error;
	});
	if (!bHasStructuralError)
	{
		// The camera and the expanded rows are written into the asset, so they have to be recorded
		// before it is serialized: saving from the Content Browser never runs the in-editor save
		// path, and Save-on-Apply defaults to Never.
		SaveEditorState();
		bLastApplySerializationSucceeded = Helper->SavePrefab();
	}
	if (!bLastApplySerializationSucceeded)
	{
		FDreamUIPrefabCompilerIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.Severity = EDreamUIPrefabCompilerSeverity::Error;
		Issue.Message = TEXT("Prefab serialization failed. Current changes were not written to the asset.");
	}

	LastApplyWarningCount = 0;
	LastApplyErrorCount = 0;
	for (const FDreamUIPrefabCompilerIssue& Issue : Issues)
	{
		LastApplyWarningCount += Issue.Severity == EDreamUIPrefabCompilerSeverity::Warning ? 1 : 0;
		LastApplyErrorCount += Issue.Severity == EDreamUIPrefabCompilerSeverity::Error ? 1 : 0;
	}
	LastApplyStatus = LastApplyErrorCount > 0
		? EDreamUIPrefabApplyStatus::Error
		: (LastApplyWarningCount > 0 ? EDreamUIPrefabApplyStatus::Warning : EDreamUIPrefabApplyStatus::Success);

	FText Summary;
	if (LastApplyStatus == EDreamUIPrefabApplyStatus::Error)
	{
		Summary = FText::Format(LOCTEXT("ApplyErrorSummary", "Apply failed with {0} error(s) and {1} warning(s)."),
			FText::AsNumber(LastApplyErrorCount), FText::AsNumber(LastApplyWarningCount));
	}
	else if (LastApplyStatus == EDreamUIPrefabApplyStatus::Warning)
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
	if (LastApplyStatus == EDreamUIPrefabApplyStatus::Error)
	{
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.ErrorWithColor"));
	}
	else if (LastApplyStatus == EDreamUIPrefabApplyStatus::Warning)
	{
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
	}
	FSlateNotificationManager::Get().AddNotification(Info);
	return LastApplyStatus != EDreamUIPrefabApplyStatus::Error;
}

void FDreamUIPrefabEditor::OnApply()
{
	const bool bApplySucceeded = ApplyPrefabChanges();
	const int32 SaveMode = DreamUIPrefabEditorLocal::GetSaveOnApplyMode();
	const bool bShouldSave = SaveMode == DreamUIPrefabEditorLocal::Always
		|| (SaveMode == DreamUIPrefabEditorLocal::SuccessOnly && bApplySucceeded);
	if (bLastApplySerializationSucceeded && bShouldSave)
	{
		SaveAppliedPrefabToDisk();
	}
}

void FDreamUIPrefabEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PrefabBeingEdited);
}

void FDreamUIPrefabEditor::SelectWidgets(const TSet<UDreamWidget*>& Widgets, bool bAppendOrToggle, bool bNotifyGEditor)
{
	if (bIsSelecting)return;
	bIsSelecting = true;
	
	TSet<UDreamWidget*> TempSelection;
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
			UDreamUISelection::GetInstance(GetWorld())->SelectNone();
		}
	}

	for ( const auto& Widget : TempSelection )
	{
		const bool bToggleOff = bAppendOrToggle && SelectedWidgets.Contains(Widget);
		if (bToggleOff)
		{
			SelectedWidgets.Remove(Widget);
		}
		else
		{
			SelectedWidgets.Add(Widget);
		}
		if (bNotifyGEditor)
		{
			if (bToggleOff)
			{
				UDreamUISelection::GetInstance(GetWorld())->DeselectWidget(Widget);
			}
			else
			{
				UDreamUISelection::GetInstance(GetWorld())->SelectWidget(Widget);
			}
		}
	}
	
	OnSelectionChanged.Broadcast();
	bIsSelecting = false;
}

FGuid FDreamUIPrefabEditor::FindWidgetGuid(const UDreamWidget* Widget) const
{
	if (!Widget || !PrefabBeingEdited || !PrefabBeingEdited->GetPrefabHelperObject())return FGuid();
	for (const auto& Pair : PrefabBeingEdited->GetPrefabHelperObject()->MapGuidToObject)
	{
		if (Pair.Value == Widget)return Pair.Key;
	}
	return FGuid();
}

FGuid FDreamUIPrefabEditor::FindOrAddWidgetGuid(UDreamWidget* Widget)
{
	if (!Widget)return FGuid();
	if (const FGuid Existing = FindWidgetGuid(Widget); Existing.IsValid())return Existing;
	if (UDreamUIPrefabHelperObject* Helper = GetPrefabHelperObject())
	{
		Helper->Modify();
		const FGuid NewGuid = FGuid::NewGuid();
		Helper->MapGuidToObject.Add(NewGuid, Widget);
		return NewGuid;
	}
	return FGuid();
}

void FDreamUIPrefabEditor::ApplyDesignerState()
{
	if (!PrefabBeingEdited || !GetPrefabHelperObject())return;
	const TSet<FGuid>& HiddenSet = PrefabBeingEdited->PrefabDataForPrefabEditor.HiddenWidgetSet;
	for (const auto& Pair : GetPrefabHelperObject()->MapGuidToObject)
	{
		if (UDreamWidget* Widget = Cast<UDreamWidget>(Pair.Value))
		{
			Widget->SetHiddenInDesigner(HiddenSet.Contains(Pair.Key));
		}
	}
}

bool FDreamUIPrefabEditor::IsWidgetHiddenInDesigner(const UDreamWidget* Widget) const
{
	return Widget && Widget->GetHiddenInDesigner();
}

void FDreamUIPrefabEditor::SetWidgetHiddenInDesigner(UDreamWidget* Widget, bool bHidden)
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

bool FDreamUIPrefabEditor::IsWidgetLockedInDesigner(const UDreamWidget* Widget) const
{
	if (!PrefabBeingEdited)return false;
	const FGuid Guid = FindWidgetGuid(Widget);
	return Guid.IsValid() && PrefabBeingEdited->PrefabDataForPrefabEditor.LockedWidgetSet.Contains(Guid);
}

void FDreamUIPrefabEditor::SetWidgetLockedInDesigner(UDreamWidget* Widget, bool bLocked, bool bRecursive)
{
	if (!Widget || !PrefabBeingEdited)return;
	TArray<UDreamWidget*> Widgets{ Widget };
	if (bRecursive)
	{
		TArray<UDreamWidget*> Descendants;
		UDreamWidget::CollectChildrenWidgets(Widget, Descendants);
		Widgets.Append(Descendants);
	}
	const FScopedTransaction Transaction(LOCTEXT("ToggleDesignerLock", "Toggle Designer Lock"));
	PrefabBeingEdited->Modify();
	for (UDreamWidget* Item : Widgets)
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

bool FDreamUIPrefabEditor::IsWidgetLockedForInteraction(const UDreamWidget* Widget) const
{
	return GetRespectDesignerLocks() && IsWidgetLockedInDesigner(Widget);
}

bool FDreamUIPrefabEditor::GetRespectDesignerLocks() const
{
	return GetDefault<UDreamUIDesignerSettings>()->bRespectDesignerLocks;
}

void FDreamUIPrefabEditor::ToggleRespectDesignerLocks()
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->bRespectDesignerLocks = !Settings->bRespectDesignerLocks;
	Settings->SaveConfig();
	// What is selectable just changed for every open prefab editor, and the padlock column reads
	// the same switch, so both surfaces have to be told rather than waiting for the next click.
	IterateAllPrefabEditor([](FDreamUIPrefabEditor* Editor)
	{
		Editor->RefreshOutliner();
		if (Editor->ViewportPtr.IsValid() && Editor->ViewportPtr->GetViewportClient().IsValid())Editor->ViewportPtr->GetViewportClient()->Invalidate();
	});
}

bool FDreamUIPrefabEditor::GetShowDesignerChrome() const
{
	return GetDefault<UDreamUIDesignerSettings>()->bShowDesignerChrome;
}

void FDreamUIPrefabEditor::ToggleShowDesignerChrome()
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->bShowDesignerChrome = !Settings->bShowDesignerChrome;
	Settings->SaveConfig();
	IterateAllPrefabEditor([](FDreamUIPrefabEditor* Editor)
	{
		if (Editor->ViewportPtr.IsValid() && Editor->ViewportPtr->GetViewportClient().IsValid())Editor->ViewportPtr->GetViewportClient()->Invalidate();
	});
}

void FDreamUIPrefabEditor::RefreshOutliner()
{
	if (OutlinerPtr.IsValid())OutlinerPtr->RequestRefresh();
}

bool FDreamUIPrefabEditor::IsDesignerGridSnapEnabled() const
{
	return GetDefault<UDreamUIDesignerSettings>()->bGridSnapEnabled;
}

void FDreamUIPrefabEditor::ToggleDesignerGridSnap()
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->bGridSnapEnabled = !Settings->bGridSnapEnabled;
	Settings->SaveConfig();
}

float FDreamUIPrefabEditor::GetDesignerGridSize() const
{
	return FMath::Max(1.0f, GetDefault<UDreamUIDesignerSettings>()->GridSize);
}

void FDreamUIPrefabEditor::SetDesignerGridSize(float GridSize)
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->GridSize = FMath::Max(1.0f, GridSize);
	Settings->SaveConfig();
}

bool FDreamUIPrefabEditor::GetShowDesignerGuides() const
{
	return GetDefault<UDreamUIDesignerSettings>()->bShowDesignerGuides;
}

void FDreamUIPrefabEditor::ToggleDesignerGuides()
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->bShowDesignerGuides = !Settings->bShowDesignerGuides;
	Settings->SaveConfig();
}

bool FDreamUIPrefabEditor::GetShowLayoutDebug() const
{
	// The chrome switch takes the whole overlay down, this diagnostic included, and the toolbar reads
	// the same answer the drawing does -- a checkbox reporting Checked over a viewport that is
	// showing none of it is the toggle lying about the state of the screen.
	return GetShowDesignerChrome() && GetDefault<UDreamUIDesignerSettings>()->bShowLayoutDebug;
}

bool FDreamUIPrefabEditor::GetShowResolutionGuides() const
{
	return GetShowDesignerChrome() && GetDefault<UDreamUIDesignerSettings>()->bShowResolutionGuides;
}

void FDreamUIPrefabEditor::ToggleResolutionGuides()
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->bShowResolutionGuides = !Settings->bShowResolutionGuides;
	Settings->SaveConfig();
}

FIntPoint FDreamUIPrefabEditor::GetDesignerCanvasSize()
{
	if (UDreamWidget* RootAgent = GetRootAgentWidget())
	{
		return FIntPoint(FMath::RoundToInt(RootAgent->GetWidth()), FMath::RoundToInt(RootAgent->GetHeight()));
	}
	return PrefabBeingEdited ? PrefabBeingEdited->CanvasSize : FIntPoint(1920, 1080);
}

FIntPoint FDreamUIPrefabEditor::GetDesignerViewportSize()
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

bool FDreamUIPrefabEditor::CalculateDesignerCanvasFor(FIntPoint InViewportSize, FIntPoint& OutCanvasSize, float& OutScale)
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
	UDreamWidget* Root = GetLoadedRootWidget();
	UDreamCanvas* Canvas = IsValid(Root) ? Root->GetComponent<UDreamCanvas>() : nullptr;
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

void FDreamUIPrefabEditor::SetDesignerViewportSize(FIntPoint NewViewportSize)
{
	UDreamWidget* RootAgent = GetRootAgentWidget();
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
	if (UDreamCanvas* AgentCanvas = RootAgent->GetComponent<UDreamCanvas>())
	{
		AgentCanvas->Modify();
		AgentCanvas->SizeInEditMode = NewViewportSize;
	}
	PrefabBeingEdited->Modify();
	PrefabBeingEdited->PrefabDataForPrefabEditor.CanvasSize = NewCanvasSize;
	PrefabBeingEdited->PrefabDataForPrefabEditor.DesignViewportSize = NewViewportSize;
	PrefabBeingEdited->CanvasSize = NewCanvasSize;
	UDreamWidget::MarkLayoutForRebuild(RootAgent);
	UDreamWidget::RebuildLayoutImmediately(RootAgent);
}

void FDreamUIPrefabEditor::ToggleLayoutDebug()
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->bShowLayoutDebug = !Settings->bShowLayoutDebug;
	Settings->SaveConfig();
}

float FDreamUIPrefabEditor::SnapDesignerValue(float Value) const
{
	if (!IsDesignerGridSnapEnabled())return Value;
	return FMath::GridSnap(Value, GetDesignerGridSize());
}

FBox FDreamUIPrefabEditor::GetDesignerFramingBox()
{
	// The agent carries the design canvas rect, which is the page the author is laying out on; the
	// prefab root only covers the content that happens to exist on it.
	if (UDreamWidget* RootAgent = GetRootAgentWidget(); IsValid(RootAgent))
	{
		return GetWidgetWorldBox(RootAgent);
	}
	if (UDreamWidget* Root = GetLoadedRootWidget(); IsValid(Root))
	{
		return GetWidgetWorldBox(Root);
	}
	return MakeCanvasFramingBounds(GetDesignerCanvasSize()).GetBox();
}

void FDreamUIPrefabEditor::ZoomDesignerToFit()
{
	if (!ViewportPtr.IsValid() || !ViewportPtr->GetViewportClient().IsValid())return;
	ViewportPtr->GetViewportClient()->FocusViewportOnBox(GetDesignerFramingBox());
}

void FDreamUIPrefabEditor::ZoomDesignerToActualSize()
{
	TSharedPtr<FEditorViewportClient> Client = ViewportPtr.IsValid() ? ViewportPtr->GetViewportClient() : nullptr;
	if (!Client.IsValid() || !Client->IsOrtho() || Client->Viewport == nullptr)return;
	Client->SetOrthoZoom(DesignerOrthoZoomFor(Client->GetOrthoZoom(), Client->GetOrthoUnitsPerPixel(Client->Viewport), 1.0f));
	Client->Invalidate();
}

float FDreamUIPrefabEditor::GetDesignerPixelsPerUnit() const
{
	TSharedPtr<FEditorViewportClient> Client = ViewportPtr.IsValid() ? ViewportPtr->GetViewportClient() : nullptr;
	// A perspective view has a different scale at every depth, so there is no one number to report.
	if (!Client.IsValid() || !Client->IsOrtho() || Client->Viewport == nullptr)return 0.0f;
	const float UnitsPerPixel = Client->GetOrthoUnitsPerPixel(Client->Viewport);
	return UnitsPerPixel > UE_SMALL_NUMBER ? 1.0f / UnitsPerPixel : 0.0f;
}

float FDreamUIPrefabEditor::DesignerOrthoZoomFor(float InCurrentOrthoZoom, float InCurrentUnitsPerPixel, float InDesiredPixelsPerUnit)
{
	if (InCurrentUnitsPerPixel <= UE_SMALL_NUMBER || InDesiredPixelsPerUnit <= UE_SMALL_NUMBER)return InCurrentOrthoZoom;
	// Ortho zoom is proportional to units-per-pixel, so the new zoom is the old one scaled by the
	// ratio between them -- and more pixels per unit is a SMALLER zoom, not a larger one.
	const float DesiredUnitsPerPixel = 1.0f / InDesiredPixelsPerUnit;
	return FMath::Clamp(InCurrentOrthoZoom * (DesiredUnitsPerPixel / InCurrentUnitsPerPixel),
		(float)MIN_ORTHOZOOM, (float)MAX_ORTHOZOOM);
}

void FDreamUIPrefabEditor::SetDesignerSizeRule(EDreamUIDesignerSizeRule InRule)
{
	if (InRule != EDreamUIDesignerSizeRule::Desired)
	{
		DesignerSizeRule = InRule;
		return;
	}
	auto Refuse = [](const FText& InMessage)
	{
		FNotificationInfo Info(InMessage);
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
		Info.ExpireDuration = 6.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	};
	FVector2D DesiredSize;
	if (!GetDesignerDesiredSize(DesiredSize))
	{
		Refuse(LOCTEXT("SizeRuleDesiredNoMeasurement", "Nothing in this prefab measures itself. Give the root widget a UMG-compatible panel and the canvas can be sized to what it contains, or choose Custom and type the root's current size."));
		return;
	}
	// The measurement is a CANVAS size, and SetDesignerViewportSize takes a DEVICE resolution that it
	// then runs the prefab's own scaler rule over. Under anything but Constant Pixel Size the canvas
	// is pinned by the reference resolution whatever device is named, so no resolution reaches the
	// measured canvas -- and handing the measurement over regardless divides the canvas by the scale
	// once per click. Ask the rule what the proposal would really produce, and refuse if it is not it.
	const FIntPoint Proposed = DesignerViewportSizeFromDesired(DesiredSize, GetDesignerViewportSize());
	FIntPoint ProposedCanvasSize;
	float ProposedScale = 1.0f;
	if (CalculateDesignerCanvasFor(Proposed, ProposedCanvasSize, ProposedScale) && ProposedCanvasSize != Proposed)
	{
		Refuse(FText::Format(
			LOCTEXT("SizeRuleDesiredScaled", "This prefab's canvas rule turns a {0} x {1} device into a {2} x {3} canvas, so the canvas cannot be sized to the content. Set the root DreamCanvas to Constant Pixel Size first."),
			Proposed.X, Proposed.Y, ProposedCanvasSize.X, ProposedCanvasSize.Y));
		return;
	}
	// Recorded only once it has been carried out. A refusal leaves the canvas exactly where it was,
	// and a radio button sitting on a rule nothing enforced says the canvas is something it is not.
	DesignerSizeRule = InRule;
	SetDesignerViewportSize(Proposed);
}

bool FDreamUIPrefabEditor::GetDesignerDesiredSize(FVector2D& OutSize)
{
	OutSize = FVector2D::ZeroVector;
	UDreamWidget* Root = GetLoadedRootWidget();
	if (!IsValid(Root))return false;
	// Only a UMG-compatible panel measures anything. A legacy DreamGUI container reports the widget's own
	// rect back as its desired size, and a root with no container has nothing but that rect either,
	// so both would answer "the size it already is" and have that read as a measurement.
	UDreamPanelLayoutBase* Panel = Cast<UDreamPanelLayoutBase>(Root->GetLayoutContainer());
	if (!IsValid(Panel))return false;
	const FVector2f Preferred = Panel->GetLayoutPreferredSize();
	if (!(Preferred.X > 0.0f && Preferred.Y > 0.0f))return false;
	OutSize = FVector2D(Preferred);
	return true;
}

FIntPoint FDreamUIPrefabEditor::DesignerViewportSizeFromDesired(const FVector2D& InDesiredSize, FIntPoint InFallback)
{
	// Zero or non-finite means the measurement failed, not that the author wants a collapsed canvas.
	auto AxisOr = [](double InDesired, int32 InFallbackAxis)
	{
		return (FMath::IsFinite(InDesired) && InDesired >= 1.0) ? FMath::RoundToInt32(InDesired) : InFallbackAxis;
	};
	return FIntPoint(FMath::Max(1, AxisOr(InDesiredSize.X, InFallback.X)), FMath::Max(1, AxisOr(InDesiredSize.Y, InFallback.Y)));
}

TSharedPtr<SWidget> FDreamUIPrefabEditor::BuildWidgetContextMenu()
{
	return OutlinerPtr.IsValid() ? OutlinerPtr->BuildContextMenu() : nullptr;
}

FDreamUIPrefabInstanceScene* FDreamUIPrefabEditor::GetPreviewScene()
{ 
	return PrefabBeingEdited->GetPrefabInstanceScene();
}

UWorld* FDreamUIPrefabEditor::GetWorld()
{
	return PrefabBeingEdited->GetPrefabInstanceScene()->GetWorld();
}

void FDreamUIPrefabEditor::BindCommands()
{
	const FDreamUIPrefabEditorCommand& PrefabEditorCommands = FDreamUIPrefabEditorCommand::Get();
	ToolkitCommands->MapAction(
		PrefabEditorCommands.Apply,
		FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::OnApply),
		FCanExecuteAction(),
		FIsActionChecked()
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.SaveOnApply_Never,
		FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::SetSaveOnApplyMode, static_cast<int32>(DreamUIPrefabEditorLocal::Never)),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FDreamUIPrefabEditor::IsSaveOnApplyMode, static_cast<int32>(DreamUIPrefabEditorLocal::Never))
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.SaveOnApply_SuccessOnly,
		FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::SetSaveOnApplyMode, static_cast<int32>(DreamUIPrefabEditorLocal::SuccessOnly)),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FDreamUIPrefabEditor::IsSaveOnApplyMode, static_cast<int32>(DreamUIPrefabEditorLocal::SuccessOnly))
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.SaveOnApply_Always,
		FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::SetSaveOnApplyMode, static_cast<int32>(DreamUIPrefabEditorLocal::Always)),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FDreamUIPrefabEditor::IsSaveOnApplyMode, static_cast<int32>(DreamUIPrefabEditorLocal::Always))
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.RawDataViewer,
		FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::OnOpenRawDataViewerPanel),
		FCanExecuteAction(),
		FIsActionChecked()
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.OverridesViewer,
		FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::OnOpenOverridesViewerPanel),
		FCanExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::HasAnySubPrefab),
		FIsActionChecked()
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.BehaviourViewer,
		FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::OnOpenBehaviourViewerPanel),
		FCanExecuteAction(),
		FIsActionChecked()
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.OpenPrefabHelperObject,
		FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::OnOpenPrefabHelperObjectDetailsPanel),
		FCanExecuteAction(),
		FIsActionChecked()
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.OpenBehaviourBlueprint,
		FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::CreateOrOpenBehaviourBlueprint),
		FCanExecuteAction(),
		FIsActionChecked()
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.ToggleScreenSpacePreview,
		FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::TogglePreviewRenderMode),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FDreamUIPrefabEditor::IsPreviewingScreenSpace)
	);
	ToolkitCommands->MapAction(
		PrefabEditorCommands.FrameFromCanvasEye,
		FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::FrameViewportFromCanvasEye),
		FCanExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::CanFrameViewportFromCanvasEye)
	);

	TFunction<UDreamWidget*()> GetSelectedWidget = [this]()
	{
		if (this->GetSelectedWidgets().Num() == 1)
		{
			auto Actor = this->GetSelectedWidgets()[0];
			if (Actor.IsValid())
			{
				return Actor.Get();
			}
		}
		return (UDreamWidget*)nullptr;
	};
	TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetArray = [this]()
	{
		TArray<UDreamWidget*> TempSelectedActors;
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
		FExecuteAction::CreateStatic(&FDreamUIEditorTools::CopyWidgets, GetSelectedWidgetArray),
		FCanExecuteAction::CreateStatic(&FDreamUIEditorTools::CanCopyWidget, GetSelectedWidgetArray),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Cut,
		FExecuteAction::CreateSPLambda(this, [=, this]()
		{
			FDreamUIEditorTools::CutWidgets(GetSelectedWidgetArray);
			OutlinerPtr->RequestRefresh();
		}),
		FCanExecuteAction::CreateStatic(&FDreamUIEditorTools::CanCutWidget, GetSelectedWidgetArray),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Paste,
		FExecuteAction::CreateSPLambda(this, [=, this]()
		{
			FDreamUIEditorTools::PasteWidgets(GetSelectedWidgetArray);
			OutlinerPtr->RequestRefresh();
		}),
		FCanExecuteAction::CreateStatic(&FDreamUIEditorTools::CanPasteWidget, GetSelectedWidget),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSPLambda(this, [=, this]()
		{
			FDreamUIEditorTools::DuplicateWidgets(GetSelectedWidgetArray);
			OutlinerPtr->RequestRefresh();
		}),
		FCanExecuteAction::CreateStatic(&FDreamUIEditorTools::CanDuplicateWidget, GetSelectedWidgetArray),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSPLambda(this, [=, this]()
		{
			FDreamUIEditorTools::DeleteWidgets(GetSelectedWidgetArray, FDreamUIEditorTools::EDeleteWidgetWarningType::WarnAndAskUser);
			OutlinerPtr->RequestRefresh();
		}),
		FCanExecuteAction::CreateStatic(&FDreamUIEditorTools::CanDeleteWidget, GetSelectedWidgetArray),
		FGetActionCheckState(),
		FIsActionButtonVisible()
	);
}
void FDreamUIPrefabEditor::ExtendToolbar()
{
	const FName MenuName = GetToolMenuToolbarName();
	if (!UToolMenus::Get()->IsMenuRegistered(MenuName))
	{
		UToolMenus::Get()->RegisterMenu(MenuName, "AssetEditor.DefaultToolBar", EMultiBoxType::ToolBar);
	}

	UToolMenu* ToolBar = UToolMenus::Get()->FindMenu(MenuName);
	const FDreamUIPrefabEditorCommand& Commands = FDreamUIPrefabEditorCommand::Get();
	const FName AppStyle = FAppStyle::GetAppStyleSetName();

	// Sections, left to right: Apply | Behaviour | Panels | View | Debug. Each one is a named section
	// so a project extender can insert relative to it, and the chain anchors on "Asset" so anything
	// the engine injects after Asset (the screenshot tool, for one) lands after ours.
	FToolMenuSection& ApplySection = ToolBar->AddSection("DreamUIPrefabApply", TAttribute<FText>(), FToolMenuInsert("Asset", EToolMenuInsertType::After));
	{
		auto ApplyButtonMenuEntry = FToolMenuEntry::InitToolBarButton(Commands.Apply
			, LOCTEXT("Apply", "Apply")
			, TAttribute<FText>(this, &FDreamUIPrefabEditor::GetApplyButtonStatusTooltip)
			, TAttribute<FSlateIcon>(this, &FDreamUIPrefabEditor::GetApplyButtonStatusImage));
		ApplyButtonMenuEntry.StyleNameOverride = "CalloutToolbar";
		auto ApplyOptionsMenuEntry = FToolMenuEntry::InitComboButton(
			"ApplyOptions",
			FUIAction(),
			FNewToolMenuDelegate::CreateSP(this, &FDreamUIPrefabEditor::GenerateApplyOptionsMenu),
			LOCTEXT("ApplyOptionsTooltip", "Options to customize how DreamUI prefabs are applied"));
		ApplyOptionsMenuEntry.StyleNameOverride = "CalloutToolbar";
		ApplyOptionsMenuEntry.ToolBarData.bSimpleComboBox = true;
		ApplySection.AddEntry(ApplyButtonMenuEntry);
		ApplySection.AddEntry(ApplyOptionsMenuEntry);
	}

	// The script host. "Behaviour BP" opens the blueprint; the combo manages which class is the
	// primary behaviour. The Behaviour *panel* is a dock tab and lives in the Panels section below --
	// one word used for two things was the source of most confusion with the old flat toolbar.
	FToolMenuSection& BehaviourSection = ToolBar->AddSection("DreamUIPrefabBehaviour", TAttribute<FText>(), FToolMenuInsert("DreamUIPrefabApply", EToolMenuInsertType::After));
	{
		BehaviourSection.AddEntry(FToolMenuEntry::InitToolBarButton(Commands.OpenBehaviourBlueprint
			, TAttribute<FText>(), TAttribute<FText>()
			, FSlateIcon(AppStyle, "Icons.Blueprints")));
		auto BehaviourOptionsMenuEntry = FToolMenuEntry::InitComboButton(
			"BehaviourOptions",
			FUIAction(),
			FNewToolMenuDelegate::CreateSP(this, &FDreamUIPrefabEditor::GenerateBehaviourOptionsMenu),
			LOCTEXT("BehaviourOptionsTooltip", "Create, select, replace, or remove this prefab's primary Behaviour."));
		BehaviourOptionsMenuEntry.ToolBarData.bSimpleComboBox = true;
		BehaviourSection.AddEntry(BehaviourOptionsMenuEntry);
	}

	FToolMenuSection& PanelsSection = ToolBar->AddSection("DreamUIPrefabPanels", TAttribute<FText>(), FToolMenuInsert("DreamUIPrefabBehaviour", EToolMenuInsertType::After));
	{
		PanelsSection.AddEntry(FToolMenuEntry::InitToolBarButton(Commands.BehaviourViewer
			, TAttribute<FText>(), TAttribute<FText>()
			, FSlateIcon(AppStyle, "Icons.Event")));
		PanelsSection.AddEntry(FToolMenuEntry::InitToolBarButton(Commands.OverridesViewer
			, TAttribute<FText>(), TAttribute<FText>()
			, FSlateIcon(AppStyle, "Icons.Adjust")));
	}

	FToolMenuSection& ViewSection = ToolBar->AddSection("DreamUIPrefabView", TAttribute<FText>(), FToolMenuInsert("DreamUIPrefabPanels", EToolMenuInsertType::After));
	{
		ViewSection.AddEntry(FToolMenuEntry::InitToolBarButton(Commands.ToggleScreenSpacePreview
			, TAttribute<FText>(), TAttribute<FText>()
			, FSlateIcon(AppStyle, "LevelEditor.Tabs.Viewports")));
		ViewSection.AddEntry(FToolMenuEntry::InitToolBarButton(Commands.FrameFromCanvasEye
			, TAttribute<FText>(), TAttribute<FText>()
			, FSlateIcon(AppStyle, "EditorViewport.ToggleRealTime")));
	}

	FToolMenuSection& DebugSection = ToolBar->AddSection("DreamUIPrefabDebug", TAttribute<FText>(), FToolMenuInsert("DreamUIPrefabView", EToolMenuInsertType::After));
	{
		DebugSection.AddEntry(FToolMenuEntry::InitComboButton(
			"Debug",
			FUIAction(),
			FNewToolMenuDelegate::CreateSP(this, &FDreamUIPrefabEditor::GenerateDebugMenu),
			LOCTEXT("DebugMenu", "Debug"),
			LOCTEXT("DebugMenuTooltip", "Inspection panels for the prefab's stored data."),
			FSlateIcon(AppStyle, "Icons.Advanced")));
	}
}

void FDreamUIPrefabEditor::GenerateDebugMenu(UToolMenu* InMenu)
{
	FToolMenuSection& Section = InMenu->AddSection("Debug", LOCTEXT("DebugMenuSection", "Inspect"));
	const FDreamUIPrefabEditorCommand& Commands = FDreamUIPrefabEditorCommand::Get();
	Section.AddMenuEntry(Commands.RawDataViewer, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Advanced"));
	Section.AddMenuEntry(Commands.OpenPrefabHelperObject, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Details"));
}

bool FDreamUIPrefabEditor::HasAnySubPrefab() const
{
	const UDreamUIPrefabHelperObject* Helper = GetPrefabHelperObject();
	return IsValid(Helper) && Helper->SubPrefabMap.Num() > 0;
}

void FDreamUIPrefabEditor::GenerateBehaviourOptionsMenu(UToolMenu* InMenu)
{
	FToolMenuSection& Section = InMenu->AddSection("Behaviour", LOCTEXT("BehaviourMenuSection", "Behaviour"));
	Section.AddMenuEntry(
		"OpenCurrentBehaviour",
		LOCTEXT("OpenCurrentBehaviour", "Open Current Behaviour"),
		LOCTEXT("OpenCurrentBehaviourTooltip", "Open the current Behaviour using its registered script editor backend."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::CreateOrOpenBehaviourBlueprint),
			FCanExecuteAction::CreateLambda([WeakThis = TWeakPtr<FDreamUIPrefabEditor>(SharedThis(this))]()
			{
				return WeakThis.IsValid() && WeakThis.Pin()->GetEffectiveBehaviourClass() != nullptr;
			})));
	Section.AddMenuEntry(
		"CreateBehaviourBlueprint",
		LOCTEXT("CreateBehaviourBlueprint", "Create Blueprint Behaviour"),
		LOCTEXT("CreateBehaviourBlueprintTooltip", "Create BP_<PrefabName>, assign it as the primary Behaviour, and open it."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Blueprints"),
		FUIAction(FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::CreateAndAssignBehaviourBlueprint)));
	Section.AddMenuEntry(
		"SelectBehaviourClass",
		LOCTEXT("SelectBehaviourClass", "Select Behaviour Class..."),
		LOCTEXT("SelectBehaviourClassTooltip", "Pick any concrete UDreamUIBehaviour class, including externally generated script classes."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
		FUIAction(FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::PickBehaviourClass)));
	Section.AddMenuEntry(
		"RemoveBehaviour",
		LOCTEXT("RemoveBehaviour", "Remove Behaviour"),
		LOCTEXT("RemoveBehaviourTooltip", "Remove the primary Behaviour component without deleting its script asset."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FDreamUIPrefabEditor::RemovePrimaryBehaviour),
			FCanExecuteAction::CreateLambda([WeakThis = TWeakPtr<FDreamUIPrefabEditor>(SharedThis(this))]()
			{
				return WeakThis.IsValid() && WeakThis.Pin()->GetEffectiveBehaviourClass() != nullptr;
			})));
}

void FDreamUIPrefabEditor::GenerateApplyOptionsMenu(UToolMenu* InMenu)
{
	FToolMenuSection& Section = InMenu->AddSection("ApplyOptions");
	Section.AddSubMenu(
		"SaveOnApply",
		LOCTEXT("SaveOnApplySubMenu", "Save on Apply"),
		LOCTEXT("SaveOnApplySubMenuTooltip", "Determines when the prefab asset is saved after Apply."),
		FNewToolMenuDelegate::CreateSP(this, &FDreamUIPrefabEditor::GenerateSaveOnApplyMenu));
}

void FDreamUIPrefabEditor::GenerateSaveOnApplyMenu(UToolMenu* InMenu)
{
	FToolMenuSection& Section = InMenu->AddSection("SaveOnApply");
	const FDreamUIPrefabEditorCommand& Commands = FDreamUIPrefabEditorCommand::Get();
	Section.AddMenuEntry(Commands.SaveOnApply_Never);
	Section.AddMenuEntry(Commands.SaveOnApply_SuccessOnly);
	Section.AddMenuEntry(Commands.SaveOnApply_Always);
}

void FDreamUIPrefabEditor::SetSaveOnApplyMode(int32 InMode)
{
	if (!GConfig)
	{
		return;
	}
	const int32 ClampedMode = FMath::Clamp(
		InMode,
		static_cast<int32>(DreamUIPrefabEditorLocal::Never),
		static_cast<int32>(DreamUIPrefabEditorLocal::Always));
	GConfig->SetInt(
		DreamUIPrefabEditorLocal::SaveOnApplySection,
		DreamUIPrefabEditorLocal::SaveOnApplyKey,
		ClampedMode,
		GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

bool FDreamUIPrefabEditor::IsSaveOnApplyMode(int32 InMode)const
{
	return DreamUIPrefabEditorLocal::GetSaveOnApplyMode() == InMode;
}

FText FDreamUIPrefabEditor::GetApplyButtonStatusTooltip()const
{
	if (GetAnythingDirty())
	{
		return LOCTEXT("Apply_Tooltip", "Changes need to be applied");
	}
	switch (LastApplyStatus)
	{
	case EDreamUIPrefabApplyStatus::Success:
		return LOCTEXT("ApplyGood_Tooltip", "Prefab is up to date");
	case EDreamUIPrefabApplyStatus::Warning:
		return FText::Format(LOCTEXT("ApplyWarnings_Tooltip", "Applied with {0} warning(s)"), FText::AsNumber(LastApplyWarningCount));
	case EDreamUIPrefabApplyStatus::Error:
		return FText::Format(LOCTEXT("ApplyErrors_Tooltip", "Apply failed with {0} error(s)"), FText::AsNumber(LastApplyErrorCount));
	default:
		return LOCTEXT("ApplyUnknown_Tooltip", "Prefab has not been applied in this editor session");
	}
}
FSlateIcon FDreamUIPrefabEditor::GetApplyButtonStatusImage()const
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
		case EDreamUIPrefabApplyStatus::Success: Overlay = CompileStatusGood; break;
		case EDreamUIPrefabApplyStatus::Warning: Overlay = CompileStatusWarning; break;
		case EDreamUIPrefabApplyStatus::Error: Overlay = CompileStatusError; break;
		default: break;
		}
	}
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), CompileStatusBackground, NAME_None, Overlay);
}

bool FDreamUIPrefabEditor::IsFilteredActor(const AActor* Actor)
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

void FDreamUIPrefabEditor::OnOutlinerActorDoubleClick(AActor* Actor)
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

FName FDreamUIPrefabEditor::GetToolkitFName() const
{
	return FName("DreamUIPrefabEditor");
}
FText FDreamUIPrefabEditor::GetBaseToolkitName() const
{
	return LOCTEXT("DreamUIPrefabEditorAppLabel", "DreamUI Prefab Editor");
}
FText FDreamUIPrefabEditor::GetToolkitName() const
{
	return FText::FromString(PrefabBeingEdited->GetName());
}
FText FDreamUIPrefabEditor::GetToolkitToolTipText() const
{
	return FAssetEditorToolkit::GetToolTipTextForObject(PrefabBeingEdited);
}
FLinearColor FDreamUIPrefabEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}
FString FDreamUIPrefabEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("DreamUIPrefabEditor");
}
FString FDreamUIPrefabEditor::GetDocumentationLink() const
{
	return TEXT("");
}
void FDreamUIPrefabEditor::OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit)
{

}
void FDreamUIPrefabEditor::OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit)
{

}

FReply FDreamUIPrefabEditor::TryHandleAssetDragDropOperation(const FDragDropEvent& DragDropEvent, UDreamWidget* InParentWidget)
{
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (Operation.IsValid() && Operation->IsOfType<FAssetDragDropOp>())
	{
		TArray< FAssetData > DroppedAssetData = AssetUtil::ExtractAssetDataFromDrag(Operation);
		const int32 NumAssets = DroppedAssetData.Num();

		if (NumAssets > 0)
		{
			TArray<UDreamUIPrefab*> PrefabsToLoad;
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
				if (auto PrefabAsset = Cast<UDreamUIPrefab>(Asset))
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
					if (PrefabAsset->PrefabVersion <= (uint16)EDreamUIPrefabVersion::OldVersion)
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
					auto MsgText = FText::Format(LOCTEXT("Error_RootCannotBeParentNode", "{0} cannot be parent actor of child prefab, please choose another actor."), FText::FromString(FDreamUIPrefabInstanceScene::RootAgentActorName));
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

			GEditor->BeginTransaction(LOCTEXT("CreateFromAssetDrop_Transaction", "DreamUI Create from asset drop"));
			// The drop writes the parent's Children array and both of the helper's maps
			// (SubPrefabMap, MapGuidToObject). None of that was snapshotted, so Ctrl+Z after a
			// Content-Browser prefab drop did nothing at all.
			if (UDreamWidget* DropParent = CurrentSelectedWidget.Get())
			{
				if (UObject* Outer = DropParent->GetOuter())Outer->Modify();
				DropParent->SetFlags(RF_Public | RF_Transactional);
				DropParent->Modify();
			}
			if (UDreamUIPrefabHelperObject* Helper = GetPrefabHelperObject())
			{
				Helper->Modify();
			}
			TArray<UDreamWidget*> CreatedWidgetArray;
			if (PrefabsToLoad.Num() > 0)
			{
				for (auto& PrefabAsset : PrefabsToLoad)
				{
					TMap<FGuid, TObjectPtr<UObject>> SubPrefabMapGuidToObject;
					TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> SubSubPrefabMap;
					auto LoadedSubPrefabRootActor = PrefabAsset->LoadPrefabWithExistingObjects(
						GetPreviewScene()->GetWorld()
						, CurrentSelectedWidget->GetOuter()
						, CurrentSelectedWidget.Get()
						, SubPrefabMapGuidToObject, SubSubPrefabMap
					);

					GetPrefabHelperObject()->MakePrefabAsSubPrefab(PrefabAsset, LoadedSubPrefabRootActor, SubPrefabMapGuidToObject, {});
					FDreamUIEditorTools::EnsureUniqueWidgetDisplayNames(GetLoadedRootWidget());
					CreatedWidgetArray.Add(LoadedSubPrefabRootActor);
				}

				if (OutlinerPtr.IsValid())
				{
					UDreamUIManagerObject::AddOneShotTickFunction([=, this] {
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
				UDreamUISelection::GetInstance(GetWorld())->SelectNone();
				for (auto& Widget : CreatedWidgetArray)
				{
					UDreamUISelection::GetInstance(GetWorld())->SelectWidget(Widget);
				}
			}
			GEditor->EndTransaction();
		}

		return FReply::Handled();
	}
	return FReply::Unhandled();
}



#undef LOCTEXT_NAMESPACE
