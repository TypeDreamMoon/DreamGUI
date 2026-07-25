// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabEditorViewportToolbar.h"
#include "LexUIPrefabEditorViewport.h"
#include "LexUIPrefabEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorViewportCommands.h"
#include "EditorViewportClient.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "ViewportToolbar/UnrealEdViewportToolbarContext.h"

#define LOCTEXT_NAMESPACE "SLexUIPrefabEditorViewportToolbar"

namespace LexUI_Private
{
	// Builds a custom Camera submenu that exposes only two viewport modes:
	//   "3D" -> Perspective, "2D" -> Back (ortho back).
	// The button label reflects the active mode (3D / 2D) instead of the engine's
	// Perspective/Top/.../Back naming, and Top/Bottom/Left/Right/Front are dropped.
	static FToolMenuEntry MakeCameraSubmenuEntry()
	{
		return FToolMenuEntry::InitDynamicEntry(
			"DynamicCameraOptions",
			FNewToolMenuSectionDelegate::CreateLambda(
				[](FToolMenuSection& InDynamicSection) -> void
				{
					TWeakPtr<SEditorViewport> WeakViewport;
					if (UUnrealEdViewportToolbarContext* const EditorViewportContext =
							InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>())
					{
						WeakViewport = EditorViewportContext->Viewport;
					}

					// Button label: show "3D" while in Perspective, "2D" otherwise.
					const TAttribute<FText> Label = TAttribute<FText>::CreateLambda(
						[WeakViewport]()
						{
							if (TSharedPtr<SEditorViewport> Viewport = WeakViewport.Pin())
							{
								const bool bIsPerspective =
									Viewport->GetViewportClient()->ViewportType == LVT_Perspective;
								return bIsPerspective
									? LOCTEXT("CameraButton_3D", "3D")
									: LOCTEXT("CameraButton_2D", "2D");
							}
							return LOCTEXT("CameraSubmenuLabel", "Camera");
						}
					);

					FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
						"Camera",
						Label,
						LOCTEXT("CameraSubmenuTooltip", "Camera options"),
						FNewToolMenuDelegate::CreateLambda(
							[](UToolMenu* Submenu) -> void
							{
								const FEditorViewportCommands& ViewportCommands = FEditorViewportCommands::Get();
								FToolMenuSection& Section = Submenu->AddSection("ViewportMode");

								// 3D (Perspective) - relabel the command to "3D"
								{
									FToolMenuEntry& Mode3D = Section.AddMenuEntry(
										ViewportCommands.Perspective,
										LOCTEXT("ViewportMode_3D", "3D"),
										FText::GetEmpty(),
										FSlateIcon()
									);
									Mode3D.UserInterfaceActionType = EUserInterfaceActionType::RadioButton;
								}

								// 2D (Back / ortho back) - relabel the command to "2D"
								{
									FToolMenuEntry& Mode2D = Section.AddMenuEntry(
										ViewportCommands.Back,
										LOCTEXT("ViewportMode_2D", "2D"),
										FText::GetEmpty(),
										FSlateIcon()
									);
									Mode2D.UserInterfaceActionType = EUserInterfaceActionType::RadioButton;
								}
							}
						),
						false,
						FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.CameraComponent")
					);
					Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
				}
			)
		);
	}
}

///////////////////////////////////////////////////////////
// SLexUIPrefabEditorViewportToolbar

ICommonEditorViewportToolbarInfoProvider& SLexUIPrefabEditorViewportToolbar::GetInfoProvider() const
{
	return *InfoProviderWeakPtr.Pin().Get();
}

void SLexUIPrefabEditorViewportToolbar::Construct(const FArguments& InArgs, TSharedPtr<class ICommonEditorViewportToolbarInfoProvider> InInfoProvider)
{
	InfoProviderWeakPtr = InInfoProvider;

	// The base class SCommonEditorViewportToolbarBase::Construct() registers and populates the
	// globally-shared "UnrealEd.ViewportToolbar" tool menu, which adds the full set of buttons
	// (Transforms, Snapping, Camera, View Modes, Show, Performance/Scalability, Profile, Settings).
	// Since that menu is shared across editors we cannot trim it without affecting everyone, so
	// here we build a dedicated toolbar that only exposes the Camera and View Modes buttons.
	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();
	TSharedRef<SLexUIPrefabEditorViewport> LexViewport = StaticCastSharedRef<SLexUIPrefabEditorViewport>(ViewportRef);
	TWeakPtr<FLexUIPrefabEditor> WeakEditor = LexViewport->GetPrefabEditor();

	static const FName LexUIViewportToolbarName = TEXT("LexUIPrefabEditor.ViewportToolbar");
	if (!UToolMenus::Get()->IsMenuRegistered(LexUIViewportToolbarName))
	{
		UToolMenu* const ViewportToolbarMenu = UToolMenus::Get()->RegisterMenu(
			LexUIViewportToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar
		);
		ViewportToolbarMenu->StyleName = TEXT("ViewportToolbar");

		FToolMenuSection& RightSection = ViewportToolbarMenu->AddSection("Right");
		RightSection.Alignment = EToolMenuSectionAlign::Last;
		{
			// Camera menu (custom: only 3D / 2D modes)
			RightSection.AddEntry(LexUI_Private::MakeCameraSubmenuEntry());

			// View Modes menu
			RightSection.AddEntry(UE::UnrealEd::CreateViewModesSubmenu());
		}
	}

	FToolMenuContext ViewportToolbarContext;
	{
		ViewportToolbarContext.AppendCommandList(ViewportRef->GetCommandList());

		UUnrealEdViewportToolbarContext* const ContextObject = UE::UnrealEd::CreateViewportToolbarDefaultContext(ViewportRef);
		ViewportToolbarContext.AddObject(ContextObject);
	}

	TSharedRef<SWidget> ToolMenuWidget = UToolMenus::Get()->GenerateWidget(LexUIViewportToolbarName, ViewportToolbarContext);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("EditorViewportToolBar.Background")))
		.Cursor(EMouseCursor::Default)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText(LOCTEXT("DesignerSnapTooltip", "Snap 2D designer movement and resize operations to the selected grid size."))
				.IsChecked_Lambda([WeakEditor]()
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())return Editor->IsDesignerGridSnapEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					return ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakEditor](ECheckBoxState)
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())Editor->ToggleDesignerGridSnap();
				})
				[
					SNew(SBox).WidthOverride(22).HeightOverride(22).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.Snap"))
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.HasDownArrow(true)
				.ToolTipText(LOCTEXT("DesignerGridSizeTooltip", "2D designer grid size."))
				.OnGetMenuContent_Lambda([WeakEditor]() -> TSharedRef<SWidget>
				{
					FMenuBuilder MenuBuilder(true, nullptr);
					for (float GridSize : { 1.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f })
					{
						MenuBuilder.AddMenuEntry(FText::AsNumber(GridSize), FText::GetEmpty(), FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([WeakEditor, GridSize]()
							{
								if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())Editor->SetDesignerGridSize(GridSize);
							}), FCanExecuteAction(), FIsActionChecked::CreateLambda([WeakEditor, GridSize]()
							{
								if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())return FMath::IsNearlyEqual(Editor->GetDesignerGridSize(), GridSize);
								return false;
							})), NAME_None, EUserInterfaceActionType::RadioButton);
					}
					return MenuBuilder.MakeWidget();
				})
				.ButtonContent()
				[
					SNew(STextBlock).Text_Lambda([WeakEditor]()
					{
						if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())return FText::AsNumber(Editor->GetDesignerGridSize());
						return FText::GetEmpty();
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText(LOCTEXT("DesignerGuidesTooltip", "Show 2D designer snapping guides while manipulating widgets."))
				.IsChecked_Lambda([WeakEditor]()
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())return Editor->GetShowDesignerGuides() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					return ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakEditor](ECheckBoxState)
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())Editor->ToggleDesignerGuides();
				})
				[
					SNew(SBox).WidthOverride(22).HeightOverride(22).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("ViewportToolbar.SetShowGrid"))
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText(LOCTEXT("LayoutDebugTooltip", "Show layout measurement, arrangement, slot, ownership, and clipping diagnostics for the selected widget."))
				.IsChecked_Lambda([WeakEditor]()
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())return Editor->GetShowLayoutDebug() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					return ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakEditor](ECheckBoxState)
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())Editor->ToggleLayoutDebug();
				})
				[
					SNew(SBox).WidthOverride(22).HeightOverride(22).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.Info"))
					]
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				ToolMenuWidget
			]
		]
	];

	// Finish the SViewportToolBar base initialization (open-menu state, etc.)
	SViewportToolBar::Construct(SViewportToolBar::FArguments());
}

TSharedRef<SWidget> SLexUIPrefabEditorViewportToolbar::GenerateShowMenu() const
{
	GetInfoProvider().OnFloatingButtonClicked();

	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();

	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder ShowMenuBuilder(bInShouldCloseWindowAfterMenuSelection, ViewportRef->GetCommandList());

	return ShowMenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
