// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamWidgetDesignerModes.h"
#include "Designer/DreamWidgetDesignerTabs.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/SDreamWidgetDesignerViewport.h"
#include "Designer/SDreamWidgetDesignerDetails.h"
#include "Designer/DreamWidgetEditorHierarchyView.h"
#include "Designer/SDreamWidgetPalette.h"
#include "Animation/SDreamWidgetAnimationEditor.h"
#include "DreamWidgetBlueprint.h"

#include "BlueprintEditorTabs.h"
#include "SBlueprintEditorToolbar.h"
#include "BlueprintEditorModes.h"
#include "WorkflowOrientedApp/SModeWidget.h"
#include "Widgets/Layout/SSpacer.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ToolMenus.h"
#include "UMGStyle.h"
#include "Styling/AppStyle.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "DreamWidgetDesignerModes"

const FName FDreamWidgetBlueprintApplicationModes::DesignerMode(TEXT("DesignerName"));
const FName FDreamWidgetBlueprintApplicationModes::GraphMode(TEXT("GraphName"));

FText FDreamWidgetBlueprintApplicationModes::GetLocalizedMode(FName InMode)
{
	if (InMode == DesignerMode)
	{
		return LOCTEXT("DesignerMode", "Designer");
	}
	if (InMode == GraphMode)
	{
		return LOCTEXT("GraphMode", "Graph");
	}
	return FText::GetEmpty();
}

void FDreamWidgetBlueprintApplicationModes::AddModeSwitcher(TSharedPtr<FDreamWidgetBlueprintEditor> InEditor,
	TSharedPtr<FExtender> InExtender)
{
	if (!InEditor.IsValid() || !InExtender.IsValid())
	{
		return;
	}
	// WEAK, and this matters more than it looks. The extender is held by the mode, the mode by the
	// editor -- so a lambda capturing the editor by shared pointer closes a cycle and the toolkit is
	// never destroyed. A leaked toolkit stays subscribed to OnBlueprintUnloaded, whose engine handler
	// walks GetEditingObjects() unguarded, so deleting the asset asserted and took the editor down.
	TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor = InEditor;
	InExtender->AddToolBarExtension("Asset", EExtensionHook::After, InEditor->GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateLambda([WeakEditor](FToolBarBuilder&)
		{
			TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
			if (!Editor.IsValid())
			{
				return;
			}
			TAttribute<FName> GetActiveMode(Editor.ToSharedRef(), &FBlueprintEditor::GetCurrentMode);
			FOnModeChangeRequested SetActiveMode = FOnModeChangeRequested::CreateSP(
				Editor.ToSharedRef(), &FBlueprintEditor::SetCurrentMode);

			Editor->AddToolbarWidget(SNew(SSpacer).Size(FVector2D(4.0f, 1.0f)));
			Editor->AddToolbarWidget(
				SNew(SModeWidget, GetLocalizedMode(DesignerMode), DesignerMode)
				.OnGetActiveMode(GetActiveMode)
				.OnSetActiveMode(SetActiveMode)
				.IconImage(FAppStyle::GetBrush("UMGEditor.SwitchToDesigner")));
			Editor->AddToolbarWidget(SNew(SSpacer).Size(FVector2D(10.0f, 1.0f)));
			Editor->AddToolbarWidget(
				SNew(SModeWidget, GetLocalizedMode(GraphMode), GraphMode)
				.OnGetActiveMode(GetActiveMode)
				.OnSetActiveMode(SetActiveMode)
				.IconImage(FAppStyle::GetBrush("FullBlueprintEditor.SwitchToScriptingMode")));
			Editor->AddToolbarWidget(SNew(SSpacer).Size(FVector2D(10.0f, 1.0f)));
		}));
}

FDreamWidgetBlueprintApplicationMode::FDreamWidgetBlueprintApplicationMode(
	TSharedPtr<FDreamWidgetBlueprintEditor> InEditor, FName InModeName)
	: FBlueprintEditorApplicationMode(StaticCastSharedPtr<FBlueprintEditor>(InEditor), InModeName,
		FDreamWidgetBlueprintApplicationModes::GetLocalizedMode, /*bRegisterViewport*/false, /*bRegisterDefaultsTab*/false)
	, MyDesignerEditor(InEditor)
{
}

UDreamWidgetBlueprint* FDreamWidgetBlueprintApplicationMode::GetWidgetBlueprint() const
{
	TSharedPtr<FDreamWidgetBlueprintEditor> Editor = MyDesignerEditor.Pin();
	return Editor.IsValid() ? Editor->GetWidgetBlueprint() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Designer

FDreamWidgetDesignerApplicationMode::FDreamWidgetDesignerApplicationMode(TSharedPtr<FDreamWidgetBlueprintEditor> InEditor)
	: FDreamWidgetBlueprintApplicationMode(InEditor, FDreamWidgetBlueprintApplicationModes::DesignerMode)
{
	WorkspaceMenuCategory = FWorkspaceItem::NewGroup(LOCTEXT("WorkspaceMenu_DreamWidgetDesigner", "DreamUI Designer"));

	// One row per panel. This is the FTabDescriptor table the toolkit used to carry, moved to where
	// a mode-based editor expects it; the property worth keeping is that adding a panel is still one
	// entry here plus one slot in the layout below.
	auto AddPanel = [this, InEditor](FName InTabID, const FText& InLabel, const FSlateIcon& InIcon,
		FDreamWidgetDesignerTabSummoner::FMakeContent InMakeContent)
		-> TSharedRef<FDreamWidgetDesignerTabSummoner>
	{
		TSharedRef<FDreamWidgetDesignerTabSummoner> Summoner = MakeShared<FDreamWidgetDesignerTabSummoner>(
			InEditor, InTabID, InLabel, InIcon, MoveTemp(InMakeContent));
		TabFactories.RegisterFactory(Summoner);
		return Summoner;
	};

	const FName AppStyle = FAppStyle::GetAppStyleSetName();

	AddPanel(FDreamWidgetDesignerTabs::ViewportID, LOCTEXT("ViewportTab", "Viewport"),
		FSlateIcon(AppStyle, "LevelEditor.Tabs.Viewports"),
		[](FDreamWidgetBlueprintEditor& Editor) -> TSharedRef<SWidget>
		{
			TSharedPtr<SWidget> Widget = Editor.GetViewportWidget();
			return Widget.IsValid() ? Widget.ToSharedRef() : SNullWidget::NullWidget;
		});
	AddPanel(FDreamWidgetDesignerTabs::HierarchyID, LOCTEXT("HierarchyTab", "Hierarchy"),
		FSlateIcon(AppStyle, "LevelEditor.Tabs.Outliner"),
		[](FDreamWidgetBlueprintEditor& Editor) -> TSharedRef<SWidget>
		{
			TSharedPtr<SWidget> Widget = Editor.GetHierarchyWidget();
			return Widget.IsValid() ? Widget.ToSharedRef() : SNullWidget::NullWidget;
		});
	AddPanel(FDreamWidgetDesignerTabs::PaletteID, LOCTEXT("PaletteTab", "Palette"),
		FSlateIcon(AppStyle, "Kismet.Tabs.Palette"),
		[](FDreamWidgetBlueprintEditor& Editor) -> TSharedRef<SWidget>
		{
			TSharedPtr<SWidget> Widget = Editor.GetPaletteWidget();
			return Widget.IsValid() ? Widget.ToSharedRef() : SNullWidget::NullWidget;
		});
	AddPanel(FDreamWidgetDesignerTabs::DetailsID, LOCTEXT("DetailsTab", "Details"),
		FSlateIcon(AppStyle, "LevelEditor.Tabs.Details"),
		[](FDreamWidgetBlueprintEditor& Editor) -> TSharedRef<SWidget>
		{
			TSharedPtr<SWidget> Widget = Editor.GetDesignerDetailsWidget();
			return Widget.IsValid() ? Widget.ToSharedRef() : SNullWidget::NullWidget;
		});
	{
		TSharedRef<FDreamWidgetDesignerTabSummoner> Animations = AddPanel(
			FDreamWidgetDesignerTabs::AnimationsID, LOCTEXT("AnimationsTab", "Animations"),
			FSlateIcon(FUMGStyle::GetStyleSetName(), "Animations.TabIcon"),
			[](FDreamWidgetBlueprintEditor& Editor) -> TSharedRef<SWidget>
			{
				TSharedPtr<SWidget> Widget = Editor.GetSequencerEditor();
				return Widget.IsValid() ? Widget.ToSharedRef() : SNullWidget::NullWidget;
			});
		// Closing the panel must also leave animation mode; the sequencer would otherwise keep
		// driving the viewport (and auto-keying) with nothing visible to say so.
		Animations->OnClosedCallback = [](FDreamWidgetBlueprintEditor& Editor)
		{
			if (TSharedPtr<SDreamWidgetAnimationEditor> Sequencer = Editor.GetSequencerEditor())
			{
				Sequencer->ClearAnimationSelection();
			}
		};
	}
	// Spawned empty on purpose: the sequencer invokes this id itself and fills it with the curve
	// editor. Without a home here it would dock into whatever window the tab manager guesses.
	AddPanel(FDreamWidgetDesignerTabs::SequencerCurvesID,
		NSLOCTEXT("Sequencer", "SequencerMainGraphEditorTitle", "Sequencer Curves"),
		FSlateIcon(AppStyle, "GenericCurveEditor.TabIcon"),
		[](FDreamWidgetBlueprintEditor&) -> TSharedRef<SWidget> { return SNullWidget::NullWidget; });

	//   +-----------+--------------------------------+-----------+
	//   | Palette   |                                |           |
	//   +-----------+           Viewport             |  Details  |
	//   | Hierarchy |                                |           |
	//   |           +--------------------------------+-----------+
	//   |           | Animations | Sequencer Curves   (both closed)
	//   +-----------+--------------------------------------------+
	constexpr float LeftColumnWidth = 0.15f;
	constexpr float ViewportWidth = 0.75f;
	constexpr float BottomDrawerHeight = 0.3f;
	TabLayout = FTabManager::NewLayout("DreamWidgetBlueprintEditor_Designer_Layout_v1")
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
					->AddTab(FDreamWidgetDesignerTabs::PaletteID, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5f)
					->AddTab(FDreamWidgetDesignerTabs::HierarchyID, ETabState::OpenedTab)
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
						->SetHideTabWell(true)
						->SetSizeCoefficient(ViewportWidth)
						->AddTab(FDreamWidgetDesignerTabs::ViewportID, ETabState::OpenedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(1.0f - ViewportWidth)
						->AddTab(FDreamWidgetDesignerTabs::DetailsID, ETabState::OpenedTab)
					)
				)
				->Split
				(
					// Every secondary panel has a home here, so InvokeTab lands it in a known place
					// instead of wherever the tab manager guesses.
					FTabManager::NewStack()
					->SetSizeCoefficient(BottomDrawerHeight)
					->SetForegroundTab(FDreamWidgetDesignerTabs::AnimationsID)
					->AddTab(FDreamWidgetDesignerTabs::AnimationsID, ETabState::ClosedTab)
					->AddTab(FDreamWidgetDesignerTabs::SequencerCurvesID, ETabState::ClosedTab)
					// NOT the compiler results: this mode registers only its own panels (see
					// RegisterTabFactories), and FCompilerResultsSummoner lives in Kismet private
					// headers a plugin cannot reach -- so naming that tab here asked the tab manager
					// for a spawner nobody had. It said so on every open: "Cannot spawn tab because no
					// spawner is registered for Document", then a tab called "Unknown" failing to
					// appear in this layout. Compile results reach the author through the toolbar
					// badge and the Message Log.
				)
			)
		);

	// The mode switcher plus the stock compile button: in the designer, "compile" is what carries an
	// authoring edit onto the class, so it belongs here and not only in the graph.
	if (InEditor.IsValid())
	{
		ToolbarExtender = MakeShared<FExtender>();
		FDreamWidgetBlueprintApplicationModes::AddModeSwitcher(InEditor, ToolbarExtender);
		InEditor->RegisterModeToolbarIfUnregistered(GetModeName());
		FName OutParentToolbarName;
		const FName ToolbarName = InEditor->GetToolMenuToolbarNameForMode(GetModeName(), OutParentToolbarName);
		if (UToolMenu* Toolbar = UToolMenus::Get()->FindMenu(ToolbarName))
		{
			InEditor->GetToolbarBuilder()->AddCompileToolbar(Toolbar);
			InEditor->ExtendDesignerToolbar(Toolbar);
		}
	}
}

void FDreamWidgetDesignerApplicationMode::PreDeactivateMode()
{
	// Base deliberately not called -- see the header. UMG's designer mode leaves the same call
	// commented out for the same reason.
	FApplicationMode::PreDeactivateMode();
}

void FDreamWidgetDesignerApplicationMode::RegisterTabFactories(TSharedPtr<FTabManager> InTabManager)
{
	TSharedPtr<FBlueprintEditor> Editor = GetOwningBlueprintEditor();
	Editor->RegisterToolbarTab(InTabManager.ToSharedRef());
	// Only this mode's own panels, matching UMG's designer mode. The Blueprint editor's tabs -- the
	// graph document, my-blueprint, find results -- belong to the Graph mode; registering them here
	// would put a second, differently-populated copy of each in a window that has no room for them.
	Editor->PushTabFactories(TabFactories);
}

//////////////////////////////////////////////////////////////////////////
// Graph

FDreamWidgetGraphApplicationMode::FDreamWidgetGraphApplicationMode(TSharedPtr<FDreamWidgetBlueprintEditor> InEditor)
	: FDreamWidgetBlueprintApplicationMode(InEditor, FDreamWidgetBlueprintApplicationModes::GraphMode)
{
	TabLayout = FTabManager::NewLayout("DreamWidgetBlueprintEditor_Graph_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.15f)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.5f)
						->AddTab(FBlueprintEditorTabs::MyBlueprintID, ETabState::OpenedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.5f)
						->AddTab(FBlueprintEditorTabs::DetailsID, ETabState::OpenedTab)
					)
				)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.70f)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.80f)
						->AddTab("Document", ETabState::ClosedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.20f)
						->AddTab(FBlueprintEditorTabs::CompilerResultsID, ETabState::ClosedTab)
						->AddTab(FBlueprintEditorTabs::FindResultsID, ETabState::ClosedTab)
					)
				)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.15f)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(FBlueprintEditorTabs::PaletteID, ETabState::ClosedTab)
					)
				)
			)
		);

	if (InEditor.IsValid())
	{
		ToolbarExtender = MakeShared<FExtender>();
		FDreamWidgetBlueprintApplicationModes::AddModeSwitcher(InEditor, ToolbarExtender);
		InEditor->RegisterModeToolbarIfUnregistered(GetModeName());
		FName OutParentToolbarName;
		const FName ToolbarName = InEditor->GetToolMenuToolbarNameForMode(GetModeName(), OutParentToolbarName);
		if (UToolMenu* Toolbar = UToolMenus::Get()->FindMenu(ToolbarName))
		{
			InEditor->GetToolbarBuilder()->AddCompileToolbar(Toolbar);
			InEditor->GetToolbarBuilder()->AddScriptingToolbar(Toolbar);
			InEditor->GetToolbarBuilder()->AddBlueprintGlobalOptionsToolbar(Toolbar);
			InEditor->GetToolbarBuilder()->AddDebuggingToolbar(Toolbar);
		}
	}
}

void FDreamWidgetGraphApplicationMode::RegisterTabFactories(TSharedPtr<FTabManager> InTabManager)
{
	TSharedPtr<FBlueprintEditor> Editor = GetOwningBlueprintEditor();
	Editor->RegisterToolbarTab(InTabManager.ToSharedRef());
	Editor->PushTabFactories(CoreTabFactories);
	Editor->PushTabFactories(BlueprintEditorTabFactories);
	Editor->PushTabFactories(TabFactories);
}

#undef LOCTEXT_NAMESPACE
