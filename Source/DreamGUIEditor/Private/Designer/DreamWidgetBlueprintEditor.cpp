// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetDesignerModes.h"
#include "Designer/DreamWidgetDesignerTabs.h"
#include "BlueprintEditorTabs.h"
#include "DreamGUIEditorModule.h"
#include "DreamWidgetBlueprint.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Text/DreamUITextWriteBack.h"
#include "Core/DreamTextUserWidget.h"
#include "Text/DreamUIPaths.h"

#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "HAL/PlatformProcess.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Core/DreamWidgetTree.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Preview/DreamWidgetDesignerScene.h"
#include "SDreamWidgetDesignerViewport.h"
#include "SDreamWidgetDesignerDetails.h"
#include "EditorModeManager.h"
#include "GameFramework/Actor.h"
#include "AssetSelection.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Misc/FeedbackContext.h"
#include "DreamWidgetDesignerCommands.h"
#include "DreamUIEditorTools.h"
#include "DreamUIControlRegistry.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "DreamWidgetEditorHierarchyView.h"
#include "SDreamWidgetPalette.h"
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
#include "K2Node_Variable.h"
#include "UMGStyle.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUISettings.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamWidgetDesignerViewportClient.h"
#include "EngineDefines.h"//MIN_ORTHOZOOM
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Interaction/DreamContentWidget.h"
#include "Framework/Commands/GenericCommands.h"
#include "Preview/DreamWidgetDesignerScene.h"
#include "Animation/SDreamWidgetAnimationEditor.h"
#include "ScopedTransaction.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "SourceCodeNavigation.h"
#include "Event/DreamUIEventDelegate.h"
#include "Utils/DreamUIUtils.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Animation/DreamWidgetAnimation.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "Logging/TokenizedMessage.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"

#define LOCTEXT_NAMESPACE "DreamWidgetDesigner"

const FName DesignerAppName = FName(TEXT("DreamWidgetBlueprintEditorApp"));

TArray<FDreamWidgetBlueprintEditor*> FDreamWidgetBlueprintEditor::DesignerInstances;

struct FDreamWidgetBlueprintEditorTabs
{
	// Tab identifiers
	static const FName DetailsID;
	static const FName ViewportID;
	static const FName OutlinerID;
	static const FName PaletteID;
	static const FName SequencerID;
};

const FName FDreamWidgetBlueprintEditorTabs::DetailsID(TEXT("Details"));
const FName FDreamWidgetBlueprintEditorTabs::ViewportID(TEXT("Viewport"));
const FName FDreamWidgetBlueprintEditorTabs::OutlinerID(TEXT("Outliner"));
const FName FDreamWidgetBlueprintEditorTabs::PaletteID(TEXT("Palette"));
const FName FDreamWidgetBlueprintEditorTabs::SequencerID(TEXT("Sequencer"));

namespace DreamWidgetDesignerLocal
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

}

FName GetDesignerWorldName()
{
	static uint32 NameSuffix = 0;
	return FName(*FString::Printf(TEXT("DesignerWorld_%d"), NameSuffix++));
}
FDreamWidgetBlueprintEditor::FDreamWidgetBlueprintEditor()
{
	DesignerInstances.Add(this);
}
FDreamWidgetBlueprintEditor::~FDreamWidgetBlueprintEditor()
{
	DesignerInstances.Remove(this);

	// A backstop only. OnClose does this while the world is still alive, which is the case that
	// matters; reaching here with a preview still held means the editor was never closed.
	ShutdownPreview();
}

bool FDreamWidgetBlueprintEditor::WorldIsDesigner(UWorld* InWorld)
{
	for (auto Instance : DesignerInstances)
	{
		if (Instance->GetWorld() == InWorld)
		{
			return true;
		}
	}
	return false;
}

TWeakPtr<FDreamWidgetBlueprintEditor> FDreamWidgetBlueprintEditor::GetEditorByWorld(UWorld* InWorld)
{
	for (auto Instance : DesignerInstances)
	{
		if (Instance->GetWorld() == InWorld)
		{
			return SharedThis(Instance);
		}
	}
	return nullptr;
}

bool FDreamWidgetBlueprintEditor::WidgetIsRootAgent(UDreamWidget* InWidget)
{
	for (auto Instance : DesignerInstances)
	{
		if (InWidget == Instance->GetPreviewScene()->GetRootAgent())
		{
			return true;
		}
	}
	return false;
}

void FDreamWidgetBlueprintEditor::IterateAllDesigners(const TFunction<void(FDreamWidgetBlueprintEditor*)>& InFunction)
{
	for (auto Instance : DesignerInstances)
	{
		InFunction(Instance);
	}
}

FBox FDreamWidgetBlueprintEditor::GetWidgetWorldBox(const UDreamWidget* InWidget)
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

bool FDreamWidgetBlueprintEditor::AccumulateWidgetsBounds(const TArray<UDreamWidget*>& InWidgets, FBoxSphereBounds& OutResult)
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

FBoxSphereBounds FDreamWidgetBlueprintEditor::MakeCanvasFramingBounds(FIntPoint InCanvasSize)
{
	// UI lives on the YZ plane at X = 0, centred on the canvas, which is where the design canvas
	// sits even when every widget in the prefab is inactive.
	const FVector Extent(0.0, FMath::Max(1, InCanvasSize.X) * 0.5, FMath::Max(1, InCanvasSize.Y) * 0.5);
	return FBoxSphereBounds(FBox(-Extent, Extent));
}

bool FDreamWidgetBlueprintEditor::GetSelectedObjectsBounds(FBoxSphereBounds& OutResult)
{
	TArray<UDreamWidget*> Widgets;
	Widgets.Reserve(SelectedWidgets.Num());
	for (auto& Widget : SelectedWidgets)
	{
		Widgets.Add(Widget.Get());
	}
	return AccumulateWidgetsBounds(Widgets, OutResult);
}

bool FDreamWidgetBlueprintEditor::GetAllObjectsBounds(FBoxSphereBounds& OutResult)
{
	TArray<UDreamWidget*> Widgets;
	if (UDreamWidget* Root = const_cast<FDreamWidgetBlueprintEditor*>(this)->GetPreviewRootWidget())
	{
		UDreamWidget::CollectChildrenWidgets(Root, Widgets, true);
	}
	return AccumulateWidgetsBounds(Widgets, OutResult);
}

FBoxSphereBounds FDreamWidgetBlueprintEditor::GetAllObjectsBounds()
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

bool FDreamWidgetBlueprintEditor::GetAnythingDirty()const
{
	// The Blueprint's own dirty flag. There is no second copy to compare against any more: an edit
	// lands on the asset, which is what "dirty" was always trying to approximate.
	return IsValid(BlueprintBeingEdited) && BlueprintBeingEdited->GetOutermost()->IsDirty();
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

void FDreamWidgetBlueprintEditor::SyncSelection()
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

FName FDreamWidgetBlueprintEditor::GetDefaultModeName()
{
	return FDreamWidgetBlueprintApplicationModes::DesignerMode;
}

void FDreamWidgetBlueprintEditor::RegisterApplicationModes(const TArray<UBlueprint*>& InBlueprints,
	bool bShouldOpenInDefaultsMode, bool bNewlyCreated)
{
	// Deliberately NOT calling the base: FBlueprintEditor's own modes are the standalone Blueprint
	// window's, and one of them would open a DreamUI hierarchy with no design surface at all.
	if (InBlueprints.Num() != 1)
	{
		return;
	}
	TSharedPtr<FDreamWidgetBlueprintEditor> ThisPtr(SharedThis(this));
	AddApplicationMode(FDreamWidgetBlueprintApplicationModes::DesignerMode,
		MakeShared<FDreamWidgetDesignerApplicationMode>(ThisPtr));
	AddApplicationMode(FDreamWidgetBlueprintApplicationModes::GraphMode,
		MakeShared<FDreamWidgetGraphApplicationMode>(ThisPtr));
	SetCurrentMode(GetDefaultModeName());
}

void FDreamWidgetBlueprintEditor::PostUndo(bool bSuccess)
{
	FBlueprintEditor::PostUndo(bSuccess);
	HandlePostTransaction(bSuccess);
}
void FDreamWidgetBlueprintEditor::PostRedo(bool bSuccess)
{
	FBlueprintEditor::PostRedo(bSuccess);
	HandlePostTransaction(bSuccess);
}

void FDreamWidgetBlueprintEditor::HandlePostTransaction(bool bSuccess)
{
	if (!bSuccess || !IsValid(BlueprintBeingEdited))
	{
		return;
	}

	FDreamWidgetDesignerScene* DesignerScene = GetPreviewScene();
	if (!DesignerScene)
	{
		return;
	}
	UWorld* EditorWorld = DesignerScene->GetWorld();
	UDreamWidget* RootAgent = DesignerScene->GetRootAgent();
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

void FDreamWidgetBlueprintEditor::InitDesigner(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost >& InitToolkitHost, UDreamWidgetBlueprint* InBlueprint)
{
	BlueprintBeingEdited = InBlueprint;

	FDreamWidgetDesignerCommands::Register();

	// The tree, and its root, have to exist before anything asks what is being designed -- a fresh
	// asset would otherwise open onto nothing and every panel would have to guard against it.
	BlueprintBeingEdited->GetOrCreateWidgetTree();

	// The preview world, the design canvas and the live instance. Built before the panels, because
	// every one of them asks for the world in its constructor.
	PreviewHost = MakeShared<FDreamWidgetPreviewHost>();
	PreviewHost->Initialize(BlueprintBeingEdited);

	// For a text-authored asset, hook the designer's edits up to the file they came from. Without
	// this the host still marks itself dirty and still broadcasts on every flush -- it just does it
	// to nobody, and the panel looks like it is editing the .dui while nothing reaches the disk.
	{
		const FString AuthoredPath = DreamUITextAuthoring::GetAuthoredSourcePath(BlueprintBeingEdited);
		if (!AuthoredPath.IsEmpty())
		{
			const FString AbsolutePath = UDreamTextUserWidget::ResolveDuiFilePath(AuthoredPath);
			FString WriteBackError;
			TextWriteBack = FDreamUITextWriteBack::Create(AbsolutePath, PreviewHost, WriteBackError);
			if (!TextWriteBack.IsValid())
			{
				// Loud, and not fatal: the designer is still worth opening on a file that cannot be
				// read, and refusing to open it would leave the author with no way to look at the
				// asset at all.
				UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Designer edits will not reach '%s': %s"),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *AbsolutePath, *WriteBackError);
			}
		}
	}

	TSharedPtr<FDreamWidgetBlueprintEditor> DesignerPtr = SharedThis(this);

	ViewportPtr = SNew(SDreamWidgetDesignerViewport, DesignerPtr, BlueprintBeingEdited->DesignerData.ViewMode);
	
	DetailsPtr = SNew(SDreamWidgetDesignerDetails, GetWorld());

	
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
	PalettePtr = SNew(SDreamWidgetPalette, SharedThis(this));
	if (UDreamWidget* RootWidget = GetPreviewRootWidget())
	{
		const int32 RenameCount = FDreamUIEditorTools::EnsureUniqueWidgetDisplayNames(RootWidget);
		if (RenameCount > 0)
		{
			FNotificationInfo Info(FText::Format(
				LOCTEXT("UniqueWidgetNamesOnOpen", "Renamed {0} duplicate widget name(s) using UMG-style numeric suffixes. Save to keep the migration."),
				FText::AsNumber(RenameCount)));
			Info.ExpireDuration = 6.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}
	ApplyDesignerState();

	SequencerPtr = SNew(SDreamWidgetAnimationEditor);
	
	BindCommands();
	// InitBlueprintEditor below builds the menus, so a project's extenders have to be registered first.
	AddMenuExtender(FDreamGUIEditorModule::Get().GetMenuExtensibilityManager()->GetAllExtenders(
		GetToolkitCommands(), TArray<UObject*>{ GetWidgetBlueprint() }));
	AddToolbarExtender(FDreamGUIEditorModule::Get().GetToolBarExtensibilityManager()->GetAllExtenders(
		GetToolkitCommands(), TArray<UObject*>{ GetWidgetBlueprint() }));

	// The modes own the layouts, so this hands over to FBlueprintEditor and RegisterApplicationModes
	// decides what the window looks like.
	InitBlueprintEditor(Mode, InitToolkitHost, TArray<UBlueprint*>{ BlueprintBeingEdited }, /*bShouldOpenInDefaultsMode*/false);
	// Only now does this toolkit have a host; the sequencer's side panels (the curve editor) must
	// dock into this window rather than the level editor's.
	if (SequencerPtr.IsValid())
	{
		SequencerPtr->SetToolkitHost(GetToolkitHost());
	}
	// FBlueprintEditor registers for undo itself.

	// After opening, broadcast event to DreamWidgetAnimationSequencerEditor
	// The AUTHORING root: animations are asset data, and the panel edits them in place.
	FDreamUIEditorTools::OnEditingWidgetChanged.Broadcast(GetAnimationHostWidget());
}

void FDreamWidgetBlueprintEditor::GetInitialViewSetting(FVector& OutLocation, FRotator& OutRotation, FVector& OutOrbitLocation, ELevelViewportType& OutViewType)
{
	auto& DesignerViewData = BlueprintBeingEdited->DesignerData;
	auto SceneBounds = this->GetAllObjectsBounds();
	if (DesignerViewData.ViewLocation == FVector::ZeroVector && DesignerViewData.ViewRotation == FRotator::ZeroRotator)
	{
		OutLocation = FVector(-SceneBounds.SphereRadius * 1.2f, SceneBounds.Origin.Y, SceneBounds.Origin.Z);
		OutRotation = FRotator::ZeroRotator;
	}
	else
	{
		OutLocation = DesignerViewData.ViewLocation;
		OutRotation = DesignerViewData.ViewRotation;
	}
	if (DesignerViewData.ViewOrbitLocation == FVector::ZeroVector)
	{
		OutOrbitLocation = SceneBounds.Origin;
	}
	else
	{
		OutOrbitLocation = DesignerViewData.ViewOrbitLocation;
	}
	OutViewType = (ELevelViewportType)DesignerViewData.ViewportType;
}

UDreamWidget* FDreamWidgetBlueprintEditor::GetRootAgentWidget()
{
	return PreviewHost.IsValid() ? PreviewHost->GetRootAgent() : nullptr;
}

void FDreamWidgetBlueprintEditor::OnClose()
{
	// Base first: it deactivates the mode and closes the tabs, and those panels are still reading the
	// world the preview owns.
	FBlueprintEditor::OnClose();
	ShutdownPreview();
}

void FDreamWidgetBlueprintEditor::ShutdownPreview()
{
	if (!PreviewHost.IsValid())
	{
		return;
	}
	UWorld* EditorWorld = PreviewHost->GetWorld();
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
	PreviewHost->Shutdown();
	PreviewHost.Reset();
}

UDreamWidget* FDreamWidgetBlueprintEditor::GetPreviewRootWidget()
{
	return PreviewHost.IsValid() ? PreviewHost->GetPreviewRoot() : nullptr;
}

UDreamWidget* FDreamWidgetBlueprintEditor::GetAnimationHostWidget() const
{
	if (!IsValid(BlueprintBeingEdited) || !IsValid(BlueprintBeingEdited->WidgetTree))
	{
		return nullptr;
	}
	return BlueprintBeingEdited->WidgetTree->RootWidget.Get();
}

UDreamWidget* FDreamWidgetBlueprintEditor::FindPreviewForAnimationContext(UDreamWidget* InAuthoredWidget)
{
	if (!IsValid(InAuthoredWidget))
	{
		return nullptr;
	}
	// The designer that owns this AUTHORED widget -- found by the Blueprint it belongs to, since an
	// authoring widget lives in no world and the usual world lookup finds nothing.
	UDreamWidgetTree* Tree = InAuthoredWidget->GetTypedOuter<UDreamWidgetTree>();
	UDreamWidgetBlueprint* Blueprint = Tree != nullptr ? Tree->GetTypedOuter<UDreamWidgetBlueprint>() : nullptr;
	if (Blueprint == nullptr)
	{
		return nullptr;
	}
	for (FDreamWidgetBlueprintEditor* Designer : DesignerInstances)
	{
		if (Designer != nullptr && Designer->GetWidgetBlueprint() == Blueprint && Designer->PreviewHost.IsValid())
		{
			return Designer->PreviewHost->FindPreviewForTemplate(InAuthoredWidget);
		}
	}
	return nullptr;
}

UDreamWidget* FDreamWidgetBlueprintEditor::GetTemplateWidget(const UDreamWidget* InPreviewWidget) const
{
	return PreviewHost.IsValid() ? PreviewHost->FindTemplateForPreview(InPreviewWidget) : nullptr;
}

FName FDreamWidgetBlueprintEditor::GetSequencerTabID()
{
	return FDreamWidgetBlueprintEditorTabs::SequencerID;
}

UDreamWidgetAnimation* FDreamWidgetBlueprintEditor::GetAnimationBeingEdited()const
{
	return SequencerPtr.IsValid() ? SequencerPtr->GetAnimation() : nullptr;
}

void FDreamWidgetBlueprintEditor::SaveAsset_Execute()
{
	// Compile FIRST. The class instances are built from is a duplicate of the authoring tree taken
	// at compile time, so saving without compiling writes a hierarchy that no instance has yet --
	// the exact shape of the designer's "forgot to press Apply", which this is here to end.
	SaveEditorState();
	if (IsValid(BlueprintBeingEdited))
	{
		FKismetEditorUtilities::CompileBlueprint(BlueprintBeingEdited);
	}
	FBlueprintEditor::SaveAsset_Execute();
}

void FDreamWidgetBlueprintEditor::Tick(float DeltaTime)
{
	FBlueprintEditor::Tick(DeltaTime);
	// A structural edit only marks the preview stale; this is where it is paid for, once, however
	// many edits went into the gesture.
	if (PreviewHost.IsValid())
	{
		PreviewHost->RebuildPreviewIfInvalidated();
	}
	// Fill Screen has no resize event to hang off -- the viewport is an FViewport, not a Slate
	// widget this toolkit hears from -- so it is asked here. ApplyDesignerViewportSize returns
	// immediately when the size already matches, which is every tick but the ones after a resize.
	if (DesignerSizeRule == EDreamUIDesignerSizeRule::FillScreen)
	{
		ApplyFillScreenSize();
	}
}

void FDreamWidgetBlueprintEditor::ApplyFillScreenSize()
{
	const FIntPoint PixelSize = GetDesignerViewportPixelSize();
	if (PixelSize.X > 0 && PixelSize.Y > 0)
	{
		ApplyDesignerViewportSize(PixelSize, /*bRecordOnAsset*/false);
	}
}

void FDreamWidgetBlueprintEditor::RefreshDesignersFor(UDreamWidgetBlueprint* InBlueprint)
{
	IterateAllDesigners([InBlueprint](FDreamWidgetBlueprintEditor* Editor)
	{
		if (Editor->BlueprintBeingEdited == InBlueprint && Editor->PreviewHost.IsValid())
		{
			Editor->PreviewHost->InvalidatePreview();
		}
	});
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
				FNotificationInfo Info(NSLOCTEXT("DreamWidgetDesigner", "SharedParentRequired", "This action needs the selected widgets to share a parent."));
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
			FNotificationInfo Info(NSLOCTEXT("DreamWidgetDesigner", "AlignLayoutParent", "The shared parent has a layout container that positions its children -- align/distribute would be overridden by the layout."));
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

void FDreamWidgetBlueprintEditor::AlignSelectedWidgets(EDreamUIWidgetAlignType AlignType)
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

	const FScopedTransaction Transaction(NSLOCTEXT("DreamWidgetDesigner", "AlignWidgets", "Align Widgets"));
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

	CommitSelectedWidgetGeometryToTemplate();
	//menu action (not a drag): repaint now so it shows even when the preview realtime is off
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid()) ViewportPtr->GetViewportClient()->Invalidate();
}

void FDreamWidgetBlueprintEditor::DistributeSelectedWidgets(bool bHorizontal)
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

	const FScopedTransaction Transaction(NSLOCTEXT("DreamWidgetDesigner", "DistributeWidgets", "Distribute Widgets"));
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

	CommitSelectedWidgetGeometryToTemplate();
	//menu action (not a drag): repaint now so it shows even when the preview realtime is off
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid()) ViewportPtr->GetViewportClient()->Invalidate();
}

void FDreamWidgetBlueprintEditor::CollectLayoutPanelDescriptors(const UClass* InExcludeClass, TArray<const FDreamUIControlDescriptor*>& OutDescriptors)
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

void FDreamWidgetBlueprintEditor::WrapSelectedWidgets(UClass* InLayoutContainerClass)
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

	FScopedTransaction Transaction(NSLOCTEXT("DreamWidgetDesigner", "WrapWidgets", "Wrap Widgets"));
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
				State.Widget->SetParentIgnoringCapacity(CommonParent, true, State.SiblingIndex);
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

	// Everything above happened on the PREVIEW, which is where the geometry could be worked out.
	// This is the half that reaches the asset.
	{
		TArray<UDreamWidget*> WrappedPreviews;
		WrappedPreviews.Reserve(WidgetStates.Num());
		for (const FWidgetWrapState& State : WidgetStates)
		{
			WrappedPreviews.Add(State.Widget);
		}
		if (!WrapTemplatesFrom(Wrapper, WrappedPreviews, InLayoutContainerClass))
		{
			RestoreOriginalHierarchy();
			FNotificationInfo Info(LOCTEXT("WrapNotMirrored",
				"Wrap With could not be applied to the asset, so it was rolled back."));
			Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
			Info.ExpireDuration = 6.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
			return;
		}
	}

	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid()) ViewportPtr->GetViewportClient()->Invalidate();
}

bool FDreamWidgetBlueprintEditor::WrapTemplatesFrom(UDreamWidget* InPreviewWrapper, TConstArrayView<UDreamWidget*> InPreviewChildren,
	UClass* InLayoutContainerClass)
{
	if (!IsValid(BlueprintBeingEdited) || !IsValid(InPreviewWrapper) || !IsValid(InPreviewWrapper->GetParent()))
	{
		return false;
	}
	UDreamWidget* ParentTemplate = GetTemplateWidget(InPreviewWrapper->GetParent());
	if (ParentTemplate == nullptr)
	{
		return false;
	}
	const int32 SiblingIndex = InPreviewWrapper->GetParent()->GetChildIndex(InPreviewWrapper);
	UDreamWidget* WrapperTemplate = DreamWidgetTreeEditing::CreateWidget(BlueprintBeingEdited,
		UDreamWidget::StaticClass(), ParentTemplate, SiblingIndex, InPreviewWrapper->GetDisplayName());
	if (WrapperTemplate == nullptr)
	{
		return false;
	}
	// By value, not by the name lookup the other mirrors use: this wrapper is the one widget in the
	// gesture that the preview invented, so there is no name to look it up by.
	WrapperTemplate->SetAnchorData(InPreviewWrapper->GetAnchorData());

	for (UDreamWidget* PreviewChild : InPreviewChildren)
	{
		if (UDreamWidget* ChildTemplate = GetTemplateWidget(PreviewChild))
		{
			DreamWidgetTreeEditing::ReparentWidget(BlueprintBeingEdited, ChildTemplate, WrapperTemplate,
				InPreviewWrapper->GetChildIndex(PreviewChild));
		}
	}
	// After the children, so a single-child panel is asked to accept what it will actually hold.
	if (InLayoutContainerClass != nullptr)
	{
		WrapperTemplate->CreateNewLayoutContainer(InLayoutContainerClass);
	}
	CommitWidgetGeometryToTemplate(InPreviewChildren);

	TArray<UDreamWidget*> Previews;
	RepublishPreviewAndSelect({ WrapperTemplate }, Previews);
	if (Previews.Num() > 0)
	{
		SelectWidgets(TSet<UDreamWidget*>{ Previews[0] }, false);
	}
	return true;
}

void FDreamWidgetBlueprintEditor::ReplaceSelectedWidgetLayout(UClass* PanelClass)
{
	// A NINTH structural entry, and one that hides behind a viewport toolbar rather than behind any
	// of the create/delete/move family: swapping a widget's layout container is rewriting its
	// `+ VerticalBox` line, and it writes straight onto the template without touching
	// DreamWidgetTreeEditing. A notification as well as the log, because this one is only ever
	// reached from a toolbar and a toolbar click that silently does nothing is the failure W5 spent
	// its time removing.
	if (DreamUITextAuthoring::RefuseStructuralEdit(BlueprintBeingEdited, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
		FString::Printf(TEXT("replace the panel with a '%s'"), *GetNameSafe(PanelClass))))
	{
		FNotificationInfo Info(DreamUITextAuthoring::DescribeStructuralRefusal(
			BlueprintBeingEdited, TEXT("replace this widget's panel")));
		Info.Image = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
		Info.ExpireDuration = 6.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}
	if (!IsValid(PanelClass) || !PanelClass->IsChildOf(UDreamLayoutContainer::StaticClass()))return;
	const TArray<TWeakObjectPtr<UDreamWidget>>& Selection = GetSelectedWidgets();
	if (Selection.Num() != 1)return;
	UDreamWidget* Target = Selection[0].Get();
	if (!IsValid(Target))return;
	UDreamLayoutContainer* Existing = Target->GetLayoutContainer();
	// "Replace" means replace; offering it on a widget with no panel would be an "add", which is
	// what the palette and the details panel are for.
	if (!IsValid(Existing) || Existing->GetClass() == PanelClass)return;
	// The swap has to land on the TEMPLATE. Done on the preview it would look right until the next
	// rebuild threw it away, which is the single easiest mistake to make in this editor now.
	UDreamWidget* TargetTemplate = GetTemplateWidget(Target);
	if (!IsValid(TargetTemplate))return;

	FScopedTransaction Transaction(NSLOCTEXT("DreamWidgetDesigner", "ReplaceWidgetLayout", "Replace Widget Layout"));
	TargetTemplate->Modify();
	Target = TargetTemplate;
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

	CommitSelectedWidgetGeometryToTemplate();
	if (OutlinerPtr.IsValid())OutlinerPtr->RequestRefresh();
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid()) ViewportPtr->GetViewportClient()->Invalidate();
}

void FDreamWidgetBlueprintEditor::TogglePreviewRenderMode()
{
	FDreamWidgetDesignerScene* DesignerScene = GetPreviewScene();
	UDreamWidget* RootAgent = DesignerScene ? DesignerScene->GetRootAgent() : nullptr;
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

void FDreamWidgetBlueprintEditor::FrameViewportFromCanvasEye()
{
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid())
	{
		StaticCastSharedPtr<FDreamWidgetDesignerViewportClient>(ViewportPtr->GetViewportClient())->FrameFromCanvasEye();
	}
}

bool FDreamWidgetBlueprintEditor::CanFrameViewportFromCanvasEye()const
{
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid())
	{
		return StaticCastSharedPtr<FDreamWidgetDesignerViewportClient>(ViewportPtr->GetViewportClient())->CanFrameFromCanvasEye();
	}
	return false;
}

bool FDreamWidgetBlueprintEditor::IsPreviewingScreenSpace()const
{
	FDreamWidgetDesignerScene* DesignerScene = const_cast<FDreamWidgetBlueprintEditor*>(this)->GetPreviewScene();
	UDreamWidget* RootAgent = DesignerScene ? DesignerScene->GetRootAgent() : nullptr;
	UDreamCanvas* RootCanvas = IsValid(RootAgent) ? RootAgent->GetComponent<UDreamCanvas>() : nullptr;
	return IsValid(RootCanvas) && RootCanvas->GetRenderMode() == EDreamRenderMode::ScreenSpaceOverlay;
}

void FDreamWidgetBlueprintEditor::SaveEditorState()
{
	//save view location and rotation
	auto ViewTransform = ViewportPtr->GetViewportClient()->GetViewTransform();
	if (!IsValid(BlueprintBeingEdited) || !ViewportPtr.IsValid())
	{
		return;
	}
	FDreamWidgetDesignerData& DesignerData = BlueprintBeingEdited->DesignerData;
	DesignerData.ViewLocation = ViewTransform.GetLocation();
	DesignerData.ViewRotation = ViewTransform.GetRotation();
	DesignerData.ViewOrbitLocation = ViewTransform.GetLookAt();
	DesignerData.ViewportType = ViewportPtr->GetViewportClient()->GetViewportType();
	DesignerData.ViewMode = ViewportPtr->GetViewportClient()->GetViewMode();
	if (UDreamWidget* RootAgentWidget = GetRootAgentWidget())
	{
		DesignerData.CanvasSize = FIntPoint(RootAgentWidget->GetWidth(), RootAgentWidget->GetHeight());
		if (UDreamCanvas* RootCanvas = RootAgentWidget->GetComponent<UDreamCanvas>())
		{
			DesignerData.CanvasRenderMode = (uint8)RootCanvas->GetRenderMode();
		}
	}

	// Collapsed rows are recorded by TEMPLATE name: the preview widget the row is showing is
	// rebuilt constantly, and its object is a different one every time, but the name it shares
	// with its template is not.
	TSet<TWeakObjectPtr<UDreamWidget>> ExpandWidgetSet;
	if (OutlinerPtr.IsValid())
	{
		OutlinerPtr->GetExpandWidgets(ExpandWidgetSet);
	}
	TSet<FName> Unexpanded;
	if (UDreamWidget* Root = GetPreviewRootWidget())
	{
		// To the nested boundary only. Object names repeat across assets, so recording the innards of
		// a nested Button here would collapse whichever host widget happens to share one of them.
		TArray<UDreamWidget*> AllWidgets;
		CollectDreamWidgetsToNestedBoundary(Root, AllWidgets);
		for (UDreamWidget* Widget : AllWidgets)
		{
			if (IsValid(Widget) && !ExpandWidgetSet.Contains(Widget))
			{
				Unexpanded.Add(Widget->GetFName());
			}
		}
	}
	DesignerData.UnexpandedWidgets = Unexpanded;
}

void FDreamWidgetBlueprintEditor::FocusAnimationByDisplayName(const FString& InDisplayName)
{
	InvokeTab(FDreamWidgetBlueprintEditorTabs::SequencerID);
	if (!SequencerPtr.IsValid() || InDisplayName.IsEmpty())
	{
		return;
	}
	if (UDreamWidgetAnimationComponent* Host = SequencerPtr->GetSequenceComponent())
	{
		if (UDreamWidgetAnimation* Sequence = Host->GetSequenceByDisplayName(InDisplayName))
		{
			SequencerPtr->SelectAnimation(Sequence);
		}
	}
}

void FDreamWidgetBlueprintEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(BlueprintBeingEdited);
}

void FDreamWidgetBlueprintEditor::SelectWidgets(const TSet<UDreamWidget*>& Widgets, bool bAppendOrToggle, bool bNotifyGEditor)
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

void FDreamWidgetBlueprintEditor::ApplyDesignerState()
{
	if (!IsValid(BlueprintBeingEdited))return;
	UDreamWidget* Root = GetPreviewRootWidget();
	if (!IsValid(Root))return;
	const TSet<FName>& HiddenSet = BlueprintBeingEdited->DesignerData.HiddenWidgets;
	TArray<UDreamWidget*> AllWidgets;
	CollectDreamWidgetsToNestedBoundary(Root, AllWidgets);
	for (UDreamWidget* Widget : AllWidgets)
	{
		if (IsValid(Widget))
		{
			Widget->SetHiddenInDesigner(HiddenSet.Contains(Widget->GetFName()));
		}
	}
}

bool FDreamWidgetBlueprintEditor::IsWidgetHiddenInDesigner(const UDreamWidget* Widget) const
{
	return Widget && Widget->GetHiddenInDesigner();
}

void FDreamWidgetBlueprintEditor::SetWidgetHiddenInDesigner(UDreamWidget* Widget, bool bHidden)
{
	if (!Widget || !IsValid(BlueprintBeingEdited) || IsWidgetHiddenInDesigner(Widget) == bHidden)return;
	const FScopedTransaction Transaction(LOCTEXT("ToggleDesignerVisibility", "Toggle Designer Visibility"));
	BlueprintBeingEdited->Modify();
	const FName Key = Widget->GetFName();
	if (bHidden)BlueprintBeingEdited->DesignerData.HiddenWidgets.Add(Key);
	else BlueprintBeingEdited->DesignerData.HiddenWidgets.Remove(Key);
	Widget->SetHiddenInDesigner(bHidden);
	BlueprintBeingEdited->MarkPackageDirty();
	if (OutlinerPtr.IsValid())OutlinerPtr->RequestRefresh();
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid())ViewportPtr->GetViewportClient()->Invalidate();
}

bool FDreamWidgetBlueprintEditor::IsWidgetLockedInDesigner(const UDreamWidget* Widget) const
{
	if (!IsValid(BlueprintBeingEdited) || Widget == nullptr)return false;
	return BlueprintBeingEdited->DesignerData.LockedWidgets.Contains(Widget->GetFName());
}

void FDreamWidgetBlueprintEditor::SetWidgetLockedInDesigner(UDreamWidget* Widget, bool bLocked, bool bRecursive)
{
	if (!Widget || !IsValid(BlueprintBeingEdited))return;
	TArray<UDreamWidget*> Widgets{ Widget };
	if (bRecursive)
	{
		TArray<UDreamWidget*> Descendants;
		CollectDreamWidgetsToNestedBoundary(Widget, Descendants, /*bIncludeRoot*/false);
		Widgets.Append(Descendants);
	}
	const FScopedTransaction Transaction(LOCTEXT("ToggleDesignerLock", "Toggle Designer Lock"));
	BlueprintBeingEdited->Modify();
	for (UDreamWidget* Item : Widgets)
	{
		if (!IsValid(Item))continue;
		if (bLocked)BlueprintBeingEdited->DesignerData.LockedWidgets.Add(Item->GetFName());
		else BlueprintBeingEdited->DesignerData.LockedWidgets.Remove(Item->GetFName());
	}
	BlueprintBeingEdited->MarkPackageDirty();
	OnSelectionChanged.Broadcast();
	if (OutlinerPtr.IsValid())OutlinerPtr->RequestRefresh();
	if (ViewportPtr.IsValid() && ViewportPtr->GetViewportClient().IsValid())ViewportPtr->GetViewportClient()->Invalidate();
}

bool FDreamWidgetBlueprintEditor::IsWidgetLockedForInteraction(const UDreamWidget* Widget) const
{
	return GetRespectDesignerLocks() && IsWidgetLockedInDesigner(Widget);
}

bool FDreamWidgetBlueprintEditor::GetRespectDesignerLocks() const
{
	return GetDefault<UDreamUIDesignerSettings>()->bRespectDesignerLocks;
}

void FDreamWidgetBlueprintEditor::ToggleRespectDesignerLocks()
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->bRespectDesignerLocks = !Settings->bRespectDesignerLocks;
	Settings->SaveConfig();
	// What is selectable just changed for every open designer, and the padlock column reads
	// the same switch, so both surfaces have to be told rather than waiting for the next click.
	IterateAllDesigners([](FDreamWidgetBlueprintEditor* Editor)
	{
		Editor->RefreshOutliner();
		if (Editor->ViewportPtr.IsValid() && Editor->ViewportPtr->GetViewportClient().IsValid())Editor->ViewportPtr->GetViewportClient()->Invalidate();
	});
}

bool FDreamWidgetBlueprintEditor::GetShowDesignerChrome() const
{
	return GetDefault<UDreamUIDesignerSettings>()->bShowDesignerChrome;
}

void FDreamWidgetBlueprintEditor::ToggleShowDesignerChrome()
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->bShowDesignerChrome = !Settings->bShowDesignerChrome;
	Settings->SaveConfig();
	IterateAllDesigners([](FDreamWidgetBlueprintEditor* Editor)
	{
		if (Editor->ViewportPtr.IsValid() && Editor->ViewportPtr->GetViewportClient().IsValid())Editor->ViewportPtr->GetViewportClient()->Invalidate();
	});
}

void FDreamWidgetBlueprintEditor::RefreshOutliner()
{
	if (OutlinerPtr.IsValid())OutlinerPtr->RequestRefresh();
}

bool FDreamWidgetBlueprintEditor::IsDesignerGridSnapEnabled() const
{
	return GetDefault<UDreamUIDesignerSettings>()->bGridSnapEnabled;
}

void FDreamWidgetBlueprintEditor::ToggleDesignerGridSnap()
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->bGridSnapEnabled = !Settings->bGridSnapEnabled;
	Settings->SaveConfig();
}

float FDreamWidgetBlueprintEditor::GetDesignerGridSize() const
{
	return FMath::Max(1.0f, GetDefault<UDreamUIDesignerSettings>()->GridSize);
}

void FDreamWidgetBlueprintEditor::SetDesignerGridSize(float GridSize)
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->GridSize = FMath::Max(1.0f, GridSize);
	Settings->SaveConfig();
}

bool FDreamWidgetBlueprintEditor::GetShowDesignerGuides() const
{
	return GetDefault<UDreamUIDesignerSettings>()->bShowDesignerGuides;
}

void FDreamWidgetBlueprintEditor::ToggleDesignerGuides()
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->bShowDesignerGuides = !Settings->bShowDesignerGuides;
	Settings->SaveConfig();
}

bool FDreamWidgetBlueprintEditor::GetShowLayoutDebug() const
{
	// The chrome switch takes the whole overlay down, this diagnostic included, and the toolbar reads
	// the same answer the drawing does -- a checkbox reporting Checked over a viewport that is
	// showing none of it is the toggle lying about the state of the screen.
	return GetShowDesignerChrome() && GetDefault<UDreamUIDesignerSettings>()->bShowLayoutDebug;
}

bool FDreamWidgetBlueprintEditor::GetShowResolutionGuides() const
{
	return GetShowDesignerChrome() && GetDefault<UDreamUIDesignerSettings>()->bShowResolutionGuides;
}

bool FDreamWidgetBlueprintEditor::GetShowDesignerRulers() const
{
	return GetShowDesignerChrome() && GetDefault<UDreamUIDesignerSettings>()->bShowDesignerRulers;
}

void FDreamWidgetBlueprintEditor::ToggleDesignerRulers()
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->bShowDesignerRulers = !Settings->bShowDesignerRulers;
	Settings->SaveConfig();
}

void FDreamWidgetBlueprintEditor::ToggleResolutionGuides()
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->bShowResolutionGuides = !Settings->bShowResolutionGuides;
	Settings->SaveConfig();
}

FIntPoint FDreamWidgetBlueprintEditor::GetDesignerCanvasSize()
{
	if (UDreamWidget* RootAgent = GetRootAgentWidget())
	{
		return FIntPoint(FMath::RoundToInt(RootAgent->GetWidth()), FMath::RoundToInt(RootAgent->GetHeight()));
	}
	return IsValid(BlueprintBeingEdited) ? BlueprintBeingEdited->DesignerData.CanvasSize : FIntPoint(1920, 1080);
}

FIntPoint FDreamWidgetBlueprintEditor::GetDesignerViewportSize()
{
	// Under Fill Screen the asset still holds the resolution the author picked, which is the right
	// thing to keep and the wrong thing to report: the picker would name a resolution the canvas is
	// not. The agent canvas's edit-mode size is what was actually applied.
	if (DesignerSizeRule == EDreamUIDesignerSizeRule::FillScreen)
	{
		if (UDreamWidget* RootAgent = GetRootAgentWidget())
		{
			if (UDreamCanvas* AgentCanvas = RootAgent->GetComponent<UDreamCanvas>();
				IsValid(AgentCanvas) && AgentCanvas->SizeInEditMode.X > 0 && AgentCanvas->SizeInEditMode.Y > 0)
			{
				return AgentCanvas->SizeInEditMode;
			}
		}
	}
	if (BlueprintBeingEdited)
	{
		const FIntPoint Stored = BlueprintBeingEdited->DesignerData.DesignViewportSize;
		if (Stored.X > 0 && Stored.Y > 0)
		{
			return Stored;
		}
	}
	// Assets authored before the picker consulted the scale rule stored only CanvasSize, which
	// back then WAS the picked resolution.
	return GetDesignerCanvasSize();
}

bool FDreamWidgetBlueprintEditor::CalculateDesignerCanvasFor(FIntPoint InViewportSize, FIntPoint& OutCanvasSize, float& OutScale)
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
	UDreamWidget* Root = GetPreviewRootWidget();
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

void FDreamWidgetBlueprintEditor::SetDesignerViewportSize(FIntPoint NewViewportSize)
{
	ApplyDesignerViewportSize(NewViewportSize, /*bRecordOnAsset*/true);
}

void FDreamWidgetBlueprintEditor::ApplyDesignerViewportSize(FIntPoint NewViewportSize, bool bRecordOnAsset)
{
	UDreamWidget* RootAgent = GetRootAgentWidget();
	if (!IsValid(RootAgent) || !BlueprintBeingEdited || NewViewportSize.X <= 0 || NewViewportSize.Y <= 0)
	{
		return;
	}
	// The picked resolution is the VIEWPORT size; the canvas the designer lays out on is whatever
	// the prefab's own scaler rule makes of it. Sizing the agent to the raw device resolution is
	// what made the picker lie for every mode except ConstantPixelSize.
	FIntPoint NewCanvasSize;
	float NewScale = 1.0f;
	CalculateDesignerCanvasFor(NewViewportSize, NewCanvasSize, NewScale);
	UDreamCanvas* AgentCanvas = RootAgent->GetComponent<UDreamCanvas>();
	// Re-clicking the checked preset (or flipping a square canvas) must not dirty the asset or
	// push an empty undo entry. Fill Screen asks the same question of the APPLIED state instead:
	// the asset still holds the author's chosen resolution, so comparing against it would answer
	// "no" every tick and resize the canvas forever.
	const bool bAlreadyApplied = bRecordOnAsset
		? (NewViewportSize == GetDesignerViewportSize() && NewCanvasSize == GetDesignerCanvasSize())
		: (IsValid(AgentCanvas) && AgentCanvas->SizeInEditMode == NewViewportSize && NewCanvasSize == GetDesignerCanvasSize());
	if (bAlreadyApplied)
	{
		return;
	}
	// No transaction on the view-only path: undo restores the asset, and there is nothing here of
	// the asset's to restore. An entry per window resize would also bury whatever the author was
	// actually doing.
	TOptional<FScopedTransaction> Transaction;
	if (bRecordOnAsset)
	{
		Transaction.Emplace(LOCTEXT("SetDesignScreenSize", "Set Design Screen Size"));
		// The preview-scene agent is created without RF_Transactional; without it Modify records
		// nothing and undo would roll back only the stored CanvasSize, not the visible canvas.
		RootAgent->SetFlags(RF_Transactional);
		RootAgent->Modify();
	}
	// SetSizeDelta, matching how the instance scene and thumbnail scene size the root canvas.
	RootAgent->SetSizeDelta(FVector2D(NewCanvasSize.X, NewCanvasSize.Y));
	// Keep the agent canvas's edit-mode viewport in step: it is forced to a fixed size, and if the
	// preview ever runs a screen-space render mode its editor tick would otherwise resize the agent
	// to the stale default and stomp this.
	if (IsValid(AgentCanvas))
	{
		if (bRecordOnAsset)
		{
			AgentCanvas->Modify();
		}
		AgentCanvas->SizeInEditMode = NewViewportSize;
	}
	if (bRecordOnAsset)
	{
		BlueprintBeingEdited->Modify();
		BlueprintBeingEdited->DesignerData.CanvasSize = NewCanvasSize;
		BlueprintBeingEdited->DesignerData.DesignViewportSize = NewViewportSize;
	}
	UDreamWidget::MarkLayoutForRebuild(RootAgent);
	UDreamWidget::RebuildLayoutImmediately(RootAgent);
}

FIntPoint FDreamWidgetBlueprintEditor::GetDesignerViewportPixelSize() const
{
	TSharedPtr<FEditorViewportClient> Client = ViewportPtr.IsValid() ? ViewportPtr->GetViewportClient() : nullptr;
	FViewport* Viewport = Client.IsValid() ? Client->Viewport : nullptr;
	return Viewport != nullptr ? FIntPoint(Viewport->GetSizeXY()) : FIntPoint::ZeroValue;
}

void FDreamWidgetBlueprintEditor::ToggleLayoutDebug()
{
	UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
	Settings->bShowLayoutDebug = !Settings->bShowLayoutDebug;
	Settings->SaveConfig();
}

float FDreamWidgetBlueprintEditor::SnapDesignerValue(float Value) const
{
	if (!IsDesignerGridSnapEnabled())return Value;
	return FMath::GridSnap(Value, GetDesignerGridSize());
}

FBox FDreamWidgetBlueprintEditor::GetDesignerFramingBox()
{
	// The agent carries the design canvas rect, which is the page the author is laying out on; the
	// prefab root only covers the content that happens to exist on it.
	if (UDreamWidget* RootAgent = GetRootAgentWidget(); IsValid(RootAgent))
	{
		return GetWidgetWorldBox(RootAgent);
	}
	if (UDreamWidget* Root = GetPreviewRootWidget(); IsValid(Root))
	{
		return GetWidgetWorldBox(Root);
	}
	return MakeCanvasFramingBounds(GetDesignerCanvasSize()).GetBox();
}

void FDreamWidgetBlueprintEditor::ZoomDesignerToFit()
{
	if (!ViewportPtr.IsValid() || !ViewportPtr->GetViewportClient().IsValid())return;
	ViewportPtr->GetViewportClient()->FocusViewportOnBox(GetDesignerFramingBox());
}

void FDreamWidgetBlueprintEditor::ZoomDesignerToActualSize()
{
	TSharedPtr<FEditorViewportClient> Client = ViewportPtr.IsValid() ? ViewportPtr->GetViewportClient() : nullptr;
	if (!Client.IsValid() || !Client->IsOrtho() || Client->Viewport == nullptr)return;
	Client->SetOrthoZoom(DesignerOrthoZoomFor(Client->GetOrthoZoom(), Client->GetOrthoUnitsPerPixel(Client->Viewport), 1.0f));
	Client->Invalidate();
}

float FDreamWidgetBlueprintEditor::GetDesignerPixelsPerUnit() const
{
	TSharedPtr<FEditorViewportClient> Client = ViewportPtr.IsValid() ? ViewportPtr->GetViewportClient() : nullptr;
	// A perspective view has a different scale at every depth, so there is no one number to report.
	if (!Client.IsValid() || !Client->IsOrtho() || Client->Viewport == nullptr)return 0.0f;
	const float UnitsPerPixel = Client->GetOrthoUnitsPerPixel(Client->Viewport);
	return UnitsPerPixel > UE_SMALL_NUMBER ? 1.0f / UnitsPerPixel : 0.0f;
}

float FDreamWidgetBlueprintEditor::DesignerOrthoZoomFor(float InCurrentOrthoZoom, float InCurrentUnitsPerPixel, float InDesiredPixelsPerUnit)
{
	if (InCurrentUnitsPerPixel <= UE_SMALL_NUMBER || InDesiredPixelsPerUnit <= UE_SMALL_NUMBER)return InCurrentOrthoZoom;
	// Ortho zoom is proportional to units-per-pixel, so the new zoom is the old one scaled by the
	// ratio between them -- and more pixels per unit is a SMALLER zoom, not a larger one.
	const float DesiredUnitsPerPixel = 1.0f / InDesiredPixelsPerUnit;
	return FMath::Clamp(InCurrentOrthoZoom * (DesiredUnitsPerPixel / InCurrentUnitsPerPixel),
		(float)MIN_ORTHOZOOM, (float)MAX_ORTHOZOOM);
}

void FDreamWidgetBlueprintEditor::SetDesignerSizeRule(EDreamUIDesignerSizeRule InRule)
{
	if (InRule == EDreamUIDesignerSizeRule::FillScreen)
	{
		// Read BEFORE the rule changes: once it is Fill Screen this getter answers with the fill size.
		if (DesignerSizeRule != EDreamUIDesignerSizeRule::FillScreen)
		{
			DesignerSizeBeforeFillScreen = GetDesignerViewportSize();
		}
		DesignerSizeRule = InRule;
		ApplyFillScreenSize();
		// Framed and then 1:1, in that order and both needed. 1:1 alone makes the canvas the
		// viewport's size in units while the camera looks at some other part of the world, which
		// fills nothing anybody can see; framing alone fits it at whatever zoom that takes.
		ZoomDesignerToFit();
		ZoomDesignerToActualSize();
		return;
	}
	if (InRule != EDreamUIDesignerSizeRule::Desired)
	{
		const bool bLeavingFillScreen = DesignerSizeRule == EDreamUIDesignerSizeRule::FillScreen;
		DesignerSizeRule = InRule;
		if (bLeavingFillScreen && DesignerSizeBeforeFillScreen.X > 0 && DesignerSizeBeforeFillScreen.Y > 0)
		{
			// Applied, not recorded: this is putting back what was on screen before, and if the asset
			// had a resolution of its own then this IS it.
			ApplyDesignerViewportSize(DesignerSizeBeforeFillScreen, /*bRecordOnAsset*/false);
			DesignerSizeBeforeFillScreen = FIntPoint::ZeroValue;
		}
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

bool FDreamWidgetBlueprintEditor::GetDesignerDesiredSize(FVector2D& OutSize)
{
	OutSize = FVector2D::ZeroVector;
	UDreamWidget* Root = GetPreviewRootWidget();
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

FIntPoint FDreamWidgetBlueprintEditor::DesignerViewportSizeFromDesired(const FVector2D& InDesiredSize, FIntPoint InFallback)
{
	// Zero or non-finite means the measurement failed, not that the author wants a collapsed canvas.
	auto AxisOr = [](double InDesired, int32 InFallbackAxis)
	{
		return (FMath::IsFinite(InDesired) && InDesired >= 1.0) ? FMath::RoundToInt32(InDesired) : InFallbackAxis;
	};
	return FIntPoint(FMath::Max(1, AxisOr(InDesiredSize.X, InFallback.X)), FMath::Max(1, AxisOr(InDesiredSize.Y, InFallback.Y)));
}

TSharedPtr<SWidget> FDreamWidgetBlueprintEditor::BuildWidgetContextMenu()
{
	return OutlinerPtr.IsValid() ? OutlinerPtr->BuildContextMenu() : nullptr;
}

FDreamWidgetDesignerScene* FDreamWidgetBlueprintEditor::GetPreviewScene()
{ 
	return PreviewHost.IsValid() ? PreviewHost->GetScene() : nullptr;
}

UWorld* FDreamWidgetBlueprintEditor::GetWorld()
{
	return PreviewHost.IsValid() ? PreviewHost->GetWorld() : nullptr;
}

void FDreamWidgetBlueprintEditor::BindCommands()
{
	const FDreamWidgetDesignerCommands& DesignerCommands = FDreamWidgetDesignerCommands::Get();
	ToolkitCommands->MapAction(
		DesignerCommands.ToggleScreenSpacePreview,
		FExecuteAction::CreateSP(this, &FDreamWidgetBlueprintEditor::TogglePreviewRenderMode),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FDreamWidgetBlueprintEditor::IsPreviewingScreenSpace)
	);
	ToolkitCommands->MapAction(
		DesignerCommands.FrameFromCanvasEye,
		FExecuteAction::CreateSP(this, &FDreamWidgetBlueprintEditor::FrameViewportFromCanvasEye),
		FCanExecuteAction::CreateSP(this, &FDreamWidgetBlueprintEditor::CanFrameViewportFromCanvasEye)
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
void FDreamWidgetBlueprintEditor::ExtendDesignerToolbar(UToolMenu* ToolBar)
{
	if (ToolBar == nullptr)
	{
		return;
	}
	const FDreamWidgetDesignerCommands& Commands = FDreamWidgetDesignerCommands::Get();
	const FName AppStyle = FAppStyle::GetAppStyleSetName();

	// Compiling and saving are the stock asset-editor buttons; what is left here is the design
	// surface's own view state. Apply is gone with the prefab, and so is the behaviour host: the
	// Blueprint's own toolbar owns compiling, and its graph owns the logic.
	FToolMenuSection& ViewSection = ToolBar->AddSection("DreamWidgetDesignerView", TAttribute<FText>(), FToolMenuInsert("Asset", EToolMenuInsertType::After));
	{
		ViewSection.AddEntry(FToolMenuEntry::InitToolBarButton(Commands.ToggleScreenSpacePreview
			, TAttribute<FText>(), TAttribute<FText>()
			, FSlateIcon(AppStyle, "LevelEditor.Tabs.Viewports")));
		ViewSection.AddEntry(FToolMenuEntry::InitToolBarButton(Commands.FrameFromCanvasEye
			, TAttribute<FText>(), TAttribute<FText>()
			, FSlateIcon(AppStyle, "EditorViewport.ToggleRealTime")));
	}

	// The .dui, on the toolbar, because it is the one property of a text-backed class that has to be
	// findable.
	//
	// It IS reachable without this -- Graph mode, Class Defaults, the Details panel -- and that is
	// exactly the problem: the file a screen is authored in should not take a mode switch and three
	// clicks to name, and a class that has one should say which one where the author is looking at
	// the hierarchy it produced. The empty state is what made this necessary: a widget blueprint
	// parented to UDreamTextUserWidget opens on a blank designer with no visible reason and nothing
	// to click.
	TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor = SharedThis(this);
	FToolMenuSection& SourceSection = ToolBar->AddSection("DreamWidgetTextSource", TAttribute<FText>(),
		FToolMenuInsert("DreamWidgetDesignerView", EToolMenuInsertType::After));
	{
		FToolUIAction SourceAction;
		// Hidden rather than disabled for a hand-authored class: a greyed control implies "not now",
		// and this one is "never, and nothing you do here will change it" -- the parent class decides,
		// and a parent class is not changed from a toolbar.
		SourceAction.IsActionVisibleDelegate = FToolMenuIsActionButtonVisible::CreateLambda(
			[WeakEditor](const FToolMenuContext&)
			{
				const TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
				return Editor.IsValid()
					&& DreamUITextAuthoring::CanAuthorFromText(Editor->GetWidgetBlueprint());
			});

		SourceSection.AddEntry(FToolMenuEntry::InitComboButton(
			"DreamWidgetTextSourceCombo",
			FToolUIActionChoice(SourceAction),
			FNewToolMenuChoice(FNewToolMenuDelegate::CreateSP(this, &FDreamWidgetBlueprintEditor::FillTextSourceMenu)),
			TAttribute<FText>::CreateLambda([WeakEditor]
			{
				const TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
				const FString FileName = Editor.IsValid()
					? DreamUITextAuthoring::GetAuthoredSourceFileName(Editor->GetWidgetBlueprint()) : FString();
				return FileName.IsEmpty()
					? LOCTEXT("NoTextSource", "No Source File")
					: FText::FromString(FileName);
			}),
			TAttribute<FText>::CreateLambda([WeakEditor]
			{
				const TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
				const FString Path = Editor.IsValid()
					? DreamUITextAuthoring::GetAuthoredSourcePath(Editor->GetWidgetBlueprint()) : FString();
				// The RESOLVED path in the tooltip, not the stored one. The stored one is short and
				// portable and says nothing about where the file actually is, which is exactly the
				// question asked by anyone hovering this after a compile could not find it.
				return Path.IsEmpty()
					? LOCTEXT("NoTextSourceTooltip",
						"This class builds its hierarchy from a .dui, and does not name one yet. Pick a file to author it in.")
					: FText::FromString(UDreamTextUserWidget::ResolveDuiFilePath(Path));
			}),
			FSlateIcon(AppStyle, "Icons.Documentation")));
	}
}

void FDreamWidgetBlueprintEditor::FillTextSourceMenu(UToolMenu* InMenu)
{
	if (InMenu == nullptr)
	{
		return;
	}
	UDreamWidgetBlueprint* Blueprint = GetWidgetBlueprint();
	const FString AuthoredPath = DreamUITextAuthoring::GetAuthoredSourcePath(Blueprint);
	const FString ResolvedPath = UDreamTextUserWidget::ResolveDuiFilePath(AuthoredPath);
	const bool bFileExists = !ResolvedPath.IsEmpty() && FPaths::FileExists(ResolvedPath);
	const FName AppStyle = FAppStyle::GetAppStyleSetName();

	FToolMenuSection& Section = InMenu->AddSection("DreamWidgetTextSourceActions",
		LOCTEXT("TextSourceSection", "DreamUI Source"));

	// First, and only when there is nothing yet. Picking a file assumes a file exists, and until this
	// existed the way to get one was to leave the editor, create an empty file by hand, and know the
	// syntax well enough to type a hierarchy into it before anything would compile. That is a hard
	// stop at the very first step of the very first screen anyone authors.
	if (AuthoredPath.IsEmpty())
	{
		Section.AddMenuEntry("CreateTextSource",
			LOCTEXT("CreateTextSource", "Create Source File..."),
			LOCTEXT("CreateTextSourceTooltip",
				"Write a new .dui with a starter hierarchy, point this class at it, and compile."),
			FSlateIcon(AppStyle, "Icons.Plus"),
			FUIAction(FExecuteAction::CreateSP(this, &FDreamWidgetBlueprintEditor::CreateTextSourceFile)));
	}

	Section.AddMenuEntry("PickTextSource",
		LOCTEXT("PickTextSource", "Set Source File..."),
		LOCTEXT("PickTextSourceTooltip", "Choose the .dui this class builds its hierarchy from, then recompile."),
		FSlateIcon(AppStyle, "Icons.FolderOpen"),
		FUIAction(FExecuteAction::CreateSP(this, &FDreamWidgetBlueprintEditor::PickTextSourceFile)));

	Section.AddMenuEntry("OpenTextSource",
		LOCTEXT("OpenTextSource", "Open in Default Editor"),
		LOCTEXT("OpenTextSourceTooltip", "Open the .dui in whatever application the system opens .dui files with."),
		FSlateIcon(AppStyle, "Icons.Edit"),
		FUIAction(
			FExecuteAction::CreateLambda([ResolvedPath]
			{
				FPlatformProcess::LaunchFileInDefaultExternalApplication(*ResolvedPath, nullptr, ELaunchVerb::Open);
			}),
			FCanExecuteAction::CreateLambda([bFileExists] { return bFileExists; })));

	Section.AddMenuEntry("ShowTextSource",
		LOCTEXT("ShowTextSource", "Show in Explorer"),
		LOCTEXT("ShowTextSourceTooltip", "Select the .dui in the file browser."),
		FSlateIcon(AppStyle, "SystemWideCommands.FindInContentBrowser"),
		FUIAction(
			FExecuteAction::CreateLambda([ResolvedPath]
			{
				FPlatformProcess::ExploreFolder(*ResolvedPath);
			}),
			FCanExecuteAction::CreateLambda([bFileExists] { return bFileExists; })));

	// Said out loud rather than left to be inferred from an empty designer. A path that resolves to
	// nothing is the one state where every other entry here is disabled and the reason is invisible.
	if (!AuthoredPath.IsEmpty() && !bFileExists)
	{
		Section.AddMenuEntry("MissingTextSource",
			FText::Format(LOCTEXT("MissingTextSource", "Not found: {0}"), FText::FromString(ResolvedPath)),
			FText::FromString(ResolvedPath),
			FSlateIcon(AppStyle, "Icons.Warning"),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
	}
}

void FDreamWidgetBlueprintEditor::PickTextSourceFile()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform == nullptr)
	{
		return;
	}
	UDreamWidgetBlueprint* Blueprint = GetWidgetBlueprint();
	if (!DreamUITextAuthoring::CanAuthorFromText(Blueprint))
	{
		return;
	}

	// Opened at the file it already names, else at the first source root, else at the project. The
	// last fallback is the one that matters: with no DUI directory anywhere yet, the project root is
	// where the author is about to make one.
	const FString AuthoredPath = DreamUITextAuthoring::GetAuthoredSourcePath(Blueprint);
	FString DefaultDirectory;
	if (!AuthoredPath.IsEmpty())
	{
		DefaultDirectory = FPaths::GetPath(UDreamTextUserWidget::ResolveDuiFilePath(AuthoredPath));
	}
	if (!FPaths::DirectoryExists(DefaultDirectory))
	{
		const TArray<FDreamUISourceRoot> Roots = DreamUIPaths::GetSourceRoots();
		DefaultDirectory = Roots.Num() > 0
			? Roots[0].Directory
			: FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	TArray<FString> Chosen;
	const bool bPicked = DesktopPlatform->OpenFileDialog(ParentWindowHandle,
		LOCTEXT("PickTextSourceTitle", "Pick a DreamUI source file").ToString(),
		DefaultDirectory, FString(), TEXT("DreamUI source (*.dui)|*.dui"),
		EFileDialogFlags::None, Chosen);
	if (!bPicked || Chosen.Num() == 0)
	{
		return;
	}
	DreamUITextAuthoring::SetAuthoredSourcePath(Blueprint, Chosen[0]);
}

FString FDreamWidgetBlueprintEditor::GetDefaultTextSourceDirectory() const
{
	const TArray<FDreamUISourceRoot> Roots = DreamUIPaths::GetSourceRoots();
	if (Roots.Num() > 0)
	{
		return Roots[0].Directory;
	}
	// No root exists yet, which is the first-time case and the one that has to work. Offering the
	// project root instead would have the author create their first .dui one directory above where
	// anything looks for it.
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), DreamUIPaths::SourceDirectoryName));
}

void FDreamWidgetBlueprintEditor::CreateTextSourceFile()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	UDreamWidgetBlueprint* Blueprint = GetWidgetBlueprint();
	if (DesktopPlatform == nullptr || !DreamUITextAuthoring::CanAuthorFromText(Blueprint))
	{
		return;
	}

	const FString DefaultDirectory = GetDefaultTextSourceDirectory();
	// Created before the dialog, not after: a save dialog pointed at a directory that does not exist
	// silently opens somewhere else, and the author ends up with their first source outside every
	// root without being told.
	IFileManager::Get().MakeDirectory(*DefaultDirectory, /*Tree*/true);

	const FString SuggestedName = FString::Printf(TEXT("%s%s"),
		*Blueprint->GetName(), DreamUIPaths::SourceExtension);

	TArray<FString> Chosen;
	const bool bPicked = DesktopPlatform->SaveFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		LOCTEXT("CreateTextSourceTitle", "Create a DreamUI source file").ToString(),
		DefaultDirectory, SuggestedName, TEXT("DreamUI source (*.dui)|*.dui"),
		EFileDialogFlags::None, Chosen);
	if (!bPicked || Chosen.Num() == 0)
	{
		return;
	}

	FString FilePath = FPaths::ConvertRelativePathToFull(Chosen[0]);
	if (!FPaths::GetExtension(FilePath, /*bIncludeDot*/true)
		.Equals(DreamUIPaths::SourceExtension, ESearchCase::IgnoreCase))
	{
		FilePath += DreamUIPaths::SourceExtension;
	}

	// Never over an existing file. This command exists for the empty state; a "create" that can
	// silently replace somebody's screen is not worth the one click it saves.
	if (FPaths::FileExists(FilePath))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("TextSourceExists", "'{0}' already exists. Use Set Source File... to point at it."),
			FText::FromString(FilePath)));
		return;
	}

	// A starter that RENDERS. An empty file compiles to an empty hierarchy, which looks exactly like
	// a broken pipeline the first time anyone sees it; a root plus one centred label is the smallest
	// thing that answers "did this work" by appearing.
	//
	// The class line is written because it is what makes localization keys survive renaming the file,
	// and the anchor block because the root is the one place in a .dui where anchors are still the
	// right tool -- everything below it lays out with containers and alignment.
	const FString Starter = FString::Printf(
		TEXT("// %s\n")
		TEXT("class %s\n")
		TEXT("\n")
		TEXT("Widget Root {\n")
		TEXT("    AnchorData.AnchorMin = (0, 0)\n")
		TEXT("    AnchorData.AnchorMax = (1, 1)\n")
		TEXT("    AnchorData.SizeDelta = (0, 0)\n")
		TEXT("\n")
		TEXT("    + Overlay {}\n")
		TEXT("\n")
		TEXT("    Text Title {\n")
		TEXT("        Text     = \"%s\"\n")
		TEXT("        FontSize = 24\n")
		TEXT("        @slot HorizontalAlignment = Center\n")
		TEXT("        @slot VerticalAlignment   = Center\n")
		TEXT("    }\n")
		TEXT("}\n"),
		*FPaths::GetCleanFilename(FilePath),
		*Blueprint->GetPathName(),
		*Blueprint->GetName());

	if (!FFileHelper::SaveStringToFile(Starter, *FilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("TextSourceNotWritten", "Could not write '{0}'."), FText::FromString(FilePath)));
		return;
	}

	DreamUITextAuthoring::SetAuthoredSourcePath(Blueprint, FilePath);
}

bool FDreamWidgetBlueprintEditor::IsFilteredActor(const AActor* Actor)
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

void FDreamWidgetBlueprintEditor::OnOutlinerActorDoubleClick(AActor* Actor)
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

FName FDreamWidgetBlueprintEditor::GetToolkitFName() const
{
	return FName("DreamWidgetBlueprintEditor");
}
FText FDreamWidgetBlueprintEditor::GetBaseToolkitName() const
{
	return LOCTEXT("DreamWidgetDesignerAppLabel", "DreamUI Designer");
}
FText FDreamWidgetBlueprintEditor::GetToolkitName() const
{
	return FText::FromString(BlueprintBeingEdited->GetName());
}
FText FDreamWidgetBlueprintEditor::GetToolkitToolTipText() const
{
	return FAssetEditorToolkit::GetToolTipTextForObject(BlueprintBeingEdited);
}
FLinearColor FDreamWidgetBlueprintEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}
FString FDreamWidgetBlueprintEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("DreamUIDesigner");
}
FString FDreamWidgetBlueprintEditor::GetDocumentationLink() const
{
	return TEXT("");
}
void FDreamWidgetBlueprintEditor::OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit)
{

}
void FDreamWidgetBlueprintEditor::OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit)
{

}

FReply FDreamWidgetBlueprintEditor::TryHandleAssetDragDropOperation(const FDragDropEvent& DragDropEvent, UDreamWidget* InParentWidget)
{
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (!Operation.IsValid() || !Operation->IsOfType<FAssetDragDropOp>())
	{
		return FReply::Unhandled();
	}
	if (!IsValid(BlueprintBeingEdited))
	{
		return FReply::Unhandled();
	}

	// What a drop means changed with the model. It used to load a prefab's contents into this one
	// as a sub-prefab instance, carrying a GUID map and an override list. A hierarchy is a class
	// now, so dropping one places an instance of it -- the same thing the palette does, and the
	// reason the whole sub-prefab apparatus is gone.
	const TArray<FAssetData> DroppedAssetData = AssetUtil::ExtractAssetDataFromDrag(Operation);
	TArray<UClass*> ClassesToPlace;
	for (int32 Index = 0; Index < DroppedAssetData.Num(); ++Index)
	{
		const FAssetData& AssetData = DroppedAssetData[Index];
		if (!AssetData.IsAssetLoaded())
		{
			GWarn->StatusUpdate(Index, DroppedAssetData.Num(),
				FText::Format(LOCTEXT("LoadingAsset", "Loading Asset {0}"), FText::FromName(AssetData.AssetName)));
		}
		UDreamWidgetBlueprint* DroppedBlueprint = Cast<UDreamWidgetBlueprint>(AssetData.GetAsset());
		if (DroppedBlueprint == nullptr)
		{
			continue;
		}
		if (DroppedBlueprint == BlueprintBeingEdited)
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("Error_SelfAsChild", "A hierarchy cannot contain itself."));
			return FReply::Unhandled();
		}
		// A class that derives from this one would contain it, which is the same cycle a step out.
		if (BlueprintBeingEdited->GeneratedClass != nullptr && DroppedBlueprint->GeneratedClass != nullptr
			&& DroppedBlueprint->GeneratedClass->IsChildOf(BlueprintBeingEdited->GeneratedClass))
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("Error_CyclicNesting", "That hierarchy derives from this one, so placing it here would nest this hierarchy inside itself."));
			return FReply::Unhandled();
		}
		if (DroppedBlueprint->GeneratedClass != nullptr)
		{
			ClassesToPlace.Add(DroppedBlueprint->GeneratedClass);
		}
	}
	if (ClassesToPlace.IsEmpty())
	{
		return FReply::Unhandled();
	}

	// Resolve the drop target to its template: that is where the new widget has to be created.
	UDreamWidget* PreviewParent = InParentWidget != nullptr ? InParentWidget
		: (SelectedWidgets.Num() > 0 ? SelectedWidgets[0].Get() : GetPreviewRootWidget());
	UDreamWidget* ParentTemplate = GetTemplateWidget(PreviewParent);
	if (ParentTemplate == nullptr)
	{
		// The design canvas, or something else in the preview world that is not part of the
		// hierarchy: the authored root is the only sensible home.
		ParentTemplate = BlueprintBeingEdited->WidgetTree != nullptr ? BlueprintBeingEdited->WidgetTree->RootWidget.Get() : nullptr;
	}
	if (ParentTemplate == nullptr)
	{
		return FReply::Unhandled();
	}
	if (!ParentTemplate->CanAcceptAdditionalChildren(ClassesToPlace.Num()))
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("Error_ParentAtCapacity", "The target widget cannot accept the dropped hierarchy."));
		return FReply::Unhandled();
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateFromAssetDrop_Transaction", "DreamUI Create from asset drop"));
	int32 CreatedCount = 0;
	for (UClass* ClassToPlace : ClassesToPlace)
	{
		if (DreamWidgetTreeEditing::CreateWidget(BlueprintBeingEdited, ClassToPlace, ParentTemplate, -1,
			ClassToPlace->GetName()) != nullptr)
		{
			CreatedCount++;
		}
	}
	if (CreatedCount == 0)
	{
		return FReply::Unhandled();
	}
	// The preview does not have the new widgets yet, so there is nothing to select until it is
	// rebuilt; the outliner refresh rides along on that.
	if (PreviewHost.IsValid())
	{
		PreviewHost->InvalidatePreview();
	}
	return FReply::Handled();
}

void FDreamWidgetBlueprintEditor::CommitWidgetGeometryToTemplate(TConstArrayView<UDreamWidget*> InPreviewWidgets)
{
	if (!PreviewHost.IsValid())
	{
		return;
	}
	// The authored geometry: the anchor block (anchors, position, size, pivot) plus the transform
	// the rotate and scale handles write. Everything else a gesture can touch goes through the
	// details panel, which mirrors on its own.
	static const FName GeometryProperties[] =
	{
		UDreamWidget::GetPropertyName_AnchorData(),
		UDreamWidget::GetPropertyName_RelativeLocation(),
		UDreamWidget::GetPropertyName_RelativeRotation(),
		UDreamWidget::GetPropertyName_RelativeScale(),
	};
	for (UDreamWidget* PreviewWidget : InPreviewWidgets)
	{
		if (IsValid(PreviewWidget))
		{
			PreviewHost->CopyPreviewValuesToTemplate(PreviewWidget, GeometryProperties);
		}
	}
	// Modified, not rebuilt, and NOT routed back through the selection: a drag calls this on
	// every mouse move, so a rebuild here would pull the widget out from under the handle
	// moving it -- and CommitSelectedWidgetGeometryToTemplate calls THIS, which is a loop.
	MarkDesignChanged();
}

void FDreamWidgetBlueprintEditor::CommitSelectedWidgetGeometryToTemplate()
{
	TArray<UDreamWidget*> Widgets;
	Widgets.Reserve(SelectedWidgets.Num());
	for (const TWeakObjectPtr<UDreamWidget>& Widget : SelectedWidgets)
	{
		if (Widget.IsValid())
		{
			Widgets.Add(Widget.Get());
		}
	}
	CommitWidgetGeometryToTemplate(Widgets);
}

//////////////////////////////////////////////////////////////////////////
// Structural editing, routed here from FDreamUIEditorTools. See the header.

namespace DreamWidgetDesignerClipboard
{
	/**
	 * Copied template subtrees, kept alive in a tree of their own.
	 *
	 * Copies rather than references on purpose: the source can be deleted, its Blueprint closed, or
	 * the whole hierarchy reshaped between the copy and the paste, and a clipboard that pointed at
	 * the originals would then paste whatever they had become. Shared across designer windows, which
	 * is what makes copy-here paste-there work.
	 *
	 * Held by a strong pointer because nothing else references it -- a plain static would be collected.
	 */
	static TStrongObjectPtr<UDreamWidgetTree> Tree;

	UDreamWidgetTree& Get()
	{
		if (!Tree.IsValid())
		{
			Tree.Reset(NewObject<UDreamWidgetTree>(GetTransientPackage(), NAME_None, RF_Transient));
		}
		return *Tree.Get();
	}

	/** The copied roots, in the order they were copied. */
	static TArray<TWeakObjectPtr<UDreamWidget>> Roots;
}

void FDreamWidgetBlueprintEditor::MigrateDetailsChangeToTemplate(TConstArrayView<UObject*> InEditedObjects, FEditPropertyChain& InChain, bool bIsModify)
{
	if (!PreviewHost.IsValid())
	{
		return;
	}
	// The objects the PANEL is showing, not the widget selection: a component's properties are edited
	// on the component, and the widget it hangs off has no such property to write.
	bool bMigrated = false;
	for (UObject* Edited : InEditedObjects)
	{
		bMigrated |= PreviewHost->MigratePropertyToTemplate(Edited, InChain, bIsModify);
	}
	if (bMigrated && !bIsModify)
	{
		// The preview already shows the value; what changed is that the asset now carries it too.
		MarkDesignChanged();
		// A committed details edit is a natural flush point: the interactive ticks were already
		// dropped upstream (SDreamWidgetDesignerDetails::NotifyPostChange refuses
		// EPropertyChangeType::Interactive), so reaching here at all means the author let go.
		PreviewHost->FlushTemplateChanges();
	}
}

FDreamWidgetBlueprintEditor* FDreamWidgetBlueprintEditor::FindDesignerForWidget(const UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return nullptr;
	}
	// A preview widget lives in a designer's world; a widget in a level does not. That is the whole
	// distinction, and it is already the one GetEditorByWorld draws.
	return GetEditorByWorld(InWidget->GetWorld()).Pin().Get();
}

void FDreamWidgetBlueprintEditor::RepublishPreviewAndSelect(TConstArrayView<UDreamWidget*> InTemplates, TArray<UDreamWidget*>& OutPreviews)
{
	OutPreviews.Reset();
	if (!PreviewHost.IsValid())
	{
		return;
	}
	// Now, not on the next tick: the caller is about to select what it just made, and a selection of
	// widgets that do not exist yet is a selection of nothing.
	//
	// This invalidates every preview pointer anyone was holding, including ones handed out by an
	// earlier call in the same gesture. That is the contract FDreamWidgetReference exists to state:
	// resolve a preview when you need it, never keep one across a structural edit.
	PreviewHost->RebuildPreview();
	for (const UDreamWidget* Template : InTemplates)
	{
		if (UDreamWidget* Preview = PreviewHost->FindPreviewForTemplate(Template))
		{
			OutPreviews.Add(Preview);
		}
	}
	RefreshOutliner();
}

UDreamWidget* FDreamWidgetBlueprintEditor::DesignerCreateWidget(UDreamWidget* InPreviewParent, TSubclassOf<UDreamWidget> InWidgetClass,
	const FString& InDesiredName, TFunction<void(UDreamWidget*)> InConfigureTemplate)
{
	if (!IsValid(BlueprintBeingEdited))
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Cannot create a widget: this designer has no Blueprint."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	// Every other structural command in this editor opens one; this one did not, so dragging a
	// control out of the palette landed a widget that Ctrl+Z could not take back -- the undo stack
	// simply had no entry for it, and the next Ctrl+Z reached past it into the user's earlier work.
	// Here rather than at the call sites: the palette, the hierarchy drop and the tools menu all
	// arrive through this function, and three of them remembering separately is how one forgets.
	const FScopedTransaction Transaction(LOCTEXT("CreateWidget_Transaction", "DreamUI Create Widget"));
	UDreamWidget* ParentTemplate = GetTemplateWidget(InPreviewParent);
	if (ParentTemplate == nullptr)
	{
		// The design canvas, or the user widget itself: the authored root is the only sensible home.
		ParentTemplate = IsValid(BlueprintBeingEdited->WidgetTree) ? BlueprintBeingEdited->WidgetTree->RootWidget.Get() : nullptr;
	}
	UDreamWidget* Template = DreamWidgetTreeEditing::CreateWidget(BlueprintBeingEdited, InWidgetClass, ParentTemplate, -1, InDesiredName);
	if (Template == nullptr)
	{
		// CreateWidget said why. Nothing was written, so the transaction records nothing either.
		return nullptr;
	}
	// Configuration lands on the TEMPLATE -- the visual, the components, whatever the caller adds.
	// Done on the preview it would be built and thrown away in the same gesture.
	if (InConfigureTemplate)
	{
		InConfigureTemplate(Template);
	}
	TArray<UDreamWidget*> Previews;
	RepublishPreviewAndSelect({ Template }, Previews);
	return Previews.Num() > 0 ? Previews[0] : nullptr;
}

bool FDreamWidgetBlueprintEditor::DesignerDeleteWidgets(TConstArrayView<UDreamWidget*> InPreviewWidgets)
{
	if (!IsValid(BlueprintBeingEdited))
	{
		return false;
	}
	bool bDeleted = false;
	for (UDreamWidget* PreviewWidget : InPreviewWidgets)
	{
		if (UDreamWidget* Template = GetTemplateWidget(PreviewWidget))
		{
			bDeleted |= DreamWidgetTreeEditing::DeleteWidget(BlueprintBeingEdited, Template);
		}
	}
	if (bDeleted)
	{
		if (UDreamUISelection* Selection = UDreamUISelection::GetInstance(GetWorld()))
		{
			// The preview objects the selection is holding are about to stop existing.
			Selection->SelectNone();
		}
		TArray<UDreamWidget*> Unused;
		RepublishPreviewAndSelect({}, Unused);
	}
	return bDeleted;
}

UDreamUIBehaviour* FDreamWidgetBlueprintEditor::DesignerAddComponents(UDreamWidget* InPreviewWidget, TConstArrayView<UClass*> InComponentClasses)
{
	return DesignerAddComponentBy(InPreviewWidget, [InComponentClasses](UDreamWidget* InTemplate) -> UDreamUIBehaviour*
	{
		UDreamUIBehaviour* Last = nullptr;
		for (UClass* ComponentClass : InComponentClasses)
		{
			if (UDreamUIBehaviour* Added = InTemplate->AddComponent(ComponentClass))
			{
				Last = Added;
			}
		}
		return Last;
	});
}

UDreamUIBehaviour* FDreamWidgetBlueprintEditor::DesignerAddComponentBy(UDreamWidget* InPreviewWidget,
	TFunctionRef<UDreamUIBehaviour*(UDreamWidget*)> InAddToTemplate)
{
	// A seventh and eighth entry the design note did not list, and they are structural for the same
	// reason the six are: a behaviour is a `+ Class { }` line in the .dui, so adding or removing one
	// here is adding or removing a line the next compile will put back exactly as the file has it.
	// They do not come through DreamWidgetTreeEditing at all -- they call NotifyStructureChanged
	// themselves -- which is why gating the five (or the six) does not reach them.
	if (DreamUITextAuthoring::RefuseStructuralEdit(BlueprintBeingEdited, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
		TEXT("add a behaviour")))
	{
		return nullptr;
	}
	UDreamWidget* Template = GetTemplateWidget(InPreviewWidget);
	if (Template == nullptr || !IsValid(BlueprintBeingEdited))
	{
		return nullptr;
	}
	BlueprintBeingEdited->Modify();
	Template->SetFlags(RF_Transactional);
	Template->Modify();

	UDreamUIBehaviour* AddedOnTemplate = InAddToTemplate(Template);
	if (!IsValid(AddedOnTemplate))
	{
		return nullptr;
	}
	AddedOnTemplate->SetFlags(RF_Transactional);
	const int32 Index = Template->GetAllComponents().Find(AddedOnTemplate);
	if (Index == INDEX_NONE)
	{
		return nullptr;
	}
	// Structurally: a behaviour can bring a required panel with it, which changes the hierarchy.
	DreamWidgetTreeEditing::NotifyStructureChanged(BlueprintBeingEdited);

	TArray<UDreamWidget*> Previews;
	RepublishPreviewAndSelect({ Template }, Previews);
	if (Previews.Num() > 0)
	{
		// By position, because an instanced sub-object has no name the two halves share.
		const TArray<UDreamUIBehaviour*>& Components = Previews[0]->GetAllComponents();
		if (Components.IsValidIndex(Index))
		{
			return Components[Index];
		}
	}
	return nullptr;
}

bool FDreamWidgetBlueprintEditor::DesignerRemoveComponent(UDreamWidget* InPreviewWidget, UDreamUIBehaviour* InPreviewComponent)
{
	// The other half of the pair above.
	if (DreamUITextAuthoring::RefuseStructuralEdit(BlueprintBeingEdited, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
		FString::Printf(TEXT("remove the behaviour '%s'"), *GetNameSafe(InPreviewComponent ? InPreviewComponent->GetClass() : nullptr))))
	{
		return false;
	}
	UDreamWidget* Template = GetTemplateWidget(InPreviewWidget);
	if (Template == nullptr || !IsValid(InPreviewComponent) || !IsValid(BlueprintBeingEdited))
	{
		return false;
	}
	const int32 Index = InPreviewWidget->GetAllComponents().Find(InPreviewComponent);
	if (!Template->GetAllComponents().IsValidIndex(Index))
	{
		return false;
	}
	BlueprintBeingEdited->Modify();
	Template->SetFlags(RF_Transactional);
	Template->Modify();
	Template->RemoveComponent(Template->GetAllComponents()[Index]);
	DreamWidgetTreeEditing::NotifyStructureChanged(BlueprintBeingEdited);

	TArray<UDreamWidget*> Previews;
	RepublishPreviewAndSelect({ Template }, Previews);
	return true;
}

TArray<UDreamWidget*> FDreamWidgetBlueprintEditor::DesignerDuplicateWidgets(TConstArrayView<UDreamWidget*> InPreviewWidgets)
{
	TArray<UDreamWidget*> Result;
	if (!IsValid(BlueprintBeingEdited))
	{
		return Result;
	}
	TArray<UDreamWidget*> NewTemplates;
	for (UDreamWidget* PreviewWidget : InPreviewWidgets)
	{
		UDreamWidget* Template = GetTemplateWidget(PreviewWidget);
		if (Template == nullptr || !IsValid(Template->GetParent()))
		{
			// The authored root has no parent to be duplicated alongside. Said out loud, because a
			// menu item that quietly does nothing is indistinguishable from one that is broken.
			UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Not duplicating '%s': %s."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetNameSafe(PreviewWidget),
				Template == nullptr ? TEXT("it has no template counterpart") : TEXT("the authored root has no parent"));
			continue;
		}
		if (UDreamWidget* Copy = DreamWidgetTreeEditing::DuplicateWidget(BlueprintBeingEdited, Template, Template->GetParent()))
		{
			NewTemplates.Add(Copy);
		}
	}
	RepublishPreviewAndSelect(NewTemplates, Result);
	return Result;
}

void FDreamWidgetBlueprintEditor::DesignerCopyWidgets(TConstArrayView<UDreamWidget*> InPreviewWidgets)
{
	UDreamWidgetTree& Clipboard = DreamWidgetDesignerClipboard::Get();
	for (const TWeakObjectPtr<UDreamWidget>& Previous : DreamWidgetDesignerClipboard::Roots)
	{
		if (Previous.IsValid())
		{
			Previous->DestroyWidget();
		}
	}
	DreamWidgetDesignerClipboard::Roots.Reset();

	for (UDreamWidget* PreviewWidget : InPreviewWidgets)
	{
		UDreamWidget* Template = GetTemplateWidget(PreviewWidget);
		if (Template == nullptr)
		{
			continue;
		}
		// DuplicateSubtree, not DuplicateObject: widgets are outered flat to their tree, so a
		// widget's children are not its subobjects and duplication would not have copied them --
		// the clipboard copy would have pointed at the ASSET's children and stolen them on the
		// next RestoreParentLinksRecursive. Copying used to empty the thing it copied.
		UDreamWidget* Copy = UDreamWidget::DuplicateSubtree(&Clipboard, Template);
		if (IsValid(Copy))
		{
			DreamWidgetDesignerClipboard::Roots.Add(Copy);
		}
	}
}

bool FDreamWidgetBlueprintEditor::DesignerHasClipboardContent()
{
	for (const TWeakObjectPtr<UDreamWidget>& Root : DreamWidgetDesignerClipboard::Roots)
	{
		if (Root.IsValid())
		{
			return true;
		}
	}
	return false;
}

TArray<UDreamWidget*> FDreamWidgetBlueprintEditor::DesignerPasteWidgets(UDreamWidget* InPreviewParent)
{
	TArray<UDreamWidget*> Result;
	// THE SIXTH STRUCTURAL ENTRY POINT, and the one that is easy to miss: paste is not in
	// DreamWidgetTreeEditing with the other five -- it duplicates the clipboard's subtrees straight
	// onto the tree below -- so a gate installed only there would have left Ctrl+V adding nodes to a
	// hierarchy that is regenerated from a text file. See DreamUITextAuthoringGate.h.
	if (DreamUITextAuthoring::RefuseStructuralEdit(BlueprintBeingEdited, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
		TEXT("paste widgets")))
	{
		return Result;
	}
	if (!IsValid(BlueprintBeingEdited) || !IsValid(BlueprintBeingEdited->WidgetTree))
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Cannot paste: this designer has no authoring tree."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return Result;
	}
	UDreamWidget* ParentTemplate = GetTemplateWidget(InPreviewParent);
	if (ParentTemplate == nullptr)
	{
		ParentTemplate = BlueprintBeingEdited->WidgetTree->RootWidget.Get();
	}
	if (ParentTemplate == nullptr)
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Cannot paste: '%s' has no root to paste into."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *BlueprintBeingEdited->GetName());
		return Result;
	}

	UDreamWidgetTree* Tree = BlueprintBeingEdited->WidgetTree;
	BlueprintBeingEdited->Modify();
	Tree->Modify();
	ParentTemplate->Modify();

	TArray<UDreamWidget*> NewTemplates;
	for (const TWeakObjectPtr<UDreamWidget>& Root : DreamWidgetDesignerClipboard::Roots)
	{
		if (!Root.IsValid())
		{
			continue;
		}
		if (!ParentTemplate->CanAcceptAdditionalChildren(1))
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d '%s' has no room for the rest of the clipboard; %d of %d pasted."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ParentTemplate->GetDisplayName(),
				NewTemplates.Num(), DreamWidgetDesignerClipboard::Roots.Num());
			break;
		}
		// A second copy, so pasting twice does not hand the tree the clipboard's own objects. Same
		// reason as the copy above for why this is DuplicateSubtree and not DuplicateObject.
		UDreamWidget* Copy = UDreamWidget::DuplicateSubtree(Tree, Root.Get());
		if (!IsValid(Copy))
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Copying '%s' out of the clipboard produced nothing."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Root->GetDisplayName());
			continue;
		}
		TArray<UDreamWidget*> Copied;
		DreamWidgetTreeEditing::ForEachWidgetInSubtree(Copy, [&Copied](UDreamWidget* Widget) { Copied.Add(Widget); });
		for (UDreamWidget* Widget : Copied)
		{
			// Flat and freshly named, for the same reason DuplicateWidget does it: names are what the
			// template-to-preview correspondence runs on.
			Widget->Rename(*MakeUniqueObjectName(Tree, Widget->GetClass()).ToString(), Tree, REN_DontCreateRedirectors);
			Widget->SetDisplayName(DreamWidgetTreeEditing::MakeUniqueDisplayName(Tree, Widget->GetDisplayName(), Widget));
		}
		if (Copy->TrySetParent(ParentTemplate, /*bKeepWorldPosition*/false, -1))
		{
			NewTemplates.Add(Copy);
		}
		else
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d '%s' refused the pasted '%s'; it was discarded."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ParentTemplate->GetDisplayName(), *Copy->GetDisplayName());
			Copy->DestroyWidget();
		}
	}
	if (NewTemplates.Num() > 0)
	{
		DreamWidgetTreeEditing::NotifyStructureChanged(BlueprintBeingEdited);
	}
	RepublishPreviewAndSelect(NewTemplates, Result);
	return Result;
}

TArray<FText> FDreamWidgetBlueprintEditor::CollectGraphReferencesToWidgets(TConstArrayView<UDreamWidget*> InPreviewWidgets) const
{
	TArray<FText> Result;
	if (!IsValid(BlueprintBeingEdited))
	{
		return Result;
	}

	// Descendants count: deleting a parent deletes them too, and their variables go with it.
	TSet<FName> DoomedVariables;
	for (UDreamWidget* PreviewWidget : InPreviewWidgets)
	{
		UDreamWidget* Template = GetTemplateWidget(PreviewWidget);
		if (Template == nullptr)
		{
			continue;
		}
		DreamWidgetTreeEditing::ForEachWidgetInSubtree(Template, [&DoomedVariables](UDreamWidget* Member)
		{
			const FName VariableName = UDreamWidgetTree::MakeWidgetVariableName(Member);
			if (!VariableName.IsNone())
			{
				DoomedVariables.Add(VariableName);
			}
		});
	}
	if (DoomedVariables.Num() == 0)
	{
		return Result;
	}

	TArray<UK2Node_Variable*> VariableNodes;
	FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_Variable>(BlueprintBeingEdited, VariableNodes);
	// Several nodes reading one variable in one graph is one thing to report. Deduped on the key
	// rather than on the line, because FText has no operator== to compare finished lines with.
	TSet<FString> Reported;
	for (const UK2Node_Variable* Node : VariableNodes)
	{
		// Self-context only: a variable of the same name on some other object is a different variable,
		// and this delete does nothing to it.
		if (!IsValid(Node) || !Node->VariableReference.IsSelfContext())
		{
			continue;
		}
		const FName MemberName = Node->VariableReference.GetMemberName();
		if (!DoomedVariables.Contains(MemberName))
		{
			continue;
		}
		const UEdGraph* Graph = Node->GetGraph();
		const FString GraphName = Graph != nullptr ? Graph->GetName() : FString();
		bool bAlreadyReported = false;
		Reported.Add(MemberName.ToString() + TEXT("|") + GraphName, &bAlreadyReported);
		if (bAlreadyReported)
		{
			continue;
		}
		Result.Add(FText::Format(
			LOCTEXT("GraphReferenceToDoomedWidget", "{0} - used in {1}"),
			FText::FromName(MemberName),
			Graph != nullptr ? FText::FromString(GraphName) : LOCTEXT("SomeGraph", "a graph")));
	}
	return Result;
}

FString FDreamWidgetBlueprintEditor::DesignerRenameWidget(UDreamWidget* InPreviewWidget, const FString& InNewDisplayName)
{
	UDreamWidget* Template = GetTemplateWidget(InPreviewWidget);
	if (Template == nullptr || !IsValid(BlueprintBeingEdited))
	{
		return FString();
	}
	const FString Applied = DreamWidgetTreeEditing::RenameWidget(BlueprintBeingEdited, Template, InNewDisplayName);
	TArray<UDreamWidget*> Previews;
	RepublishPreviewAndSelect({ Template }, Previews);
	return Applied;
}

bool FDreamWidgetBlueprintEditor::ReparentTemplatesFrom(
TConstArrayView<UDreamWidget*> InPreviewWidgets, UDreamWidget* InPreviewNewParent)
{
	if (!IsValid(BlueprintBeingEdited) || !PreviewHost.IsValid())
	{
		return false;
	}
	UDreamWidget* ParentTemplate = GetTemplateWidget(InPreviewNewParent);
	if (ParentTemplate == nullptr)
	{
		// The design canvas, or anything else in the preview world that is not authored. A widget
		// cannot be parented to it, and pretending otherwise would drop the move on the floor.
		return false;
	}

	bool bMoved = false;
	for (UDreamWidget* PreviewWidget : InPreviewWidgets)
	{
		UDreamWidget* ChildTemplate = GetTemplateWidget(PreviewWidget);
		if (ChildTemplate == nullptr)
		{
			continue;
		}
		// The sibling index the preview ended up at, so the two halves agree on order as well as on
		// parentage -- the drop position is part of what the author expressed.
		const int32 SiblingIndex = IsValid(PreviewWidget->GetParent())
			? PreviewWidget->GetParent()->GetChildIndex(PreviewWidget) : -1;
		bMoved |= DreamWidgetTreeEditing::ReparentWidget(BlueprintBeingEdited, ChildTemplate, ParentTemplate, SiblingIndex);
	}
	if (bMoved)
	{
		// The geometry the preview just resolved against its new parent. Ordered after the reparent
		// so the values land on templates that are already in the right place.
		CommitWidgetGeometryToTemplate(InPreviewWidgets);
	}
	return bMoved;
}

void FDreamWidgetBlueprintEditor::MarkDesignChanged()
{
	if (!IsValid(BlueprintBeingEdited))
	{
		return;
	}
	// Modified, not structurally modified: these operations move and re-slot widgets that already
	// exist, so no member of the class changes. What does change is the archetype instances are
	// built from, and that is what the next compile picks up.
	FBlueprintEditorUtils::MarkBlueprintAsModified(BlueprintBeingEdited);
}



#undef LOCTEXT_NAMESPACE
