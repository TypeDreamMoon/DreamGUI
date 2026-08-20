// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "LexUIPrefabEditorViewportToolbar.h"
#include "LexUIDesignScreenSizes.h"
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
#include "Widgets/Input/SNumericEntryBox.h"
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
			.Padding(4, 0, 0, 0)
			[
				SNew(SComboButton)
				.HasDownArrow(true)
				.ToolTipText(LOCTEXT("DesignerZoomTooltip", "How much of a screen pixel one design unit covers, and how to reset it."))
				.OnGetMenuContent_Lambda([WeakEditor]() -> TSharedRef<SWidget>
				{
					FMenuBuilder MenuBuilder(true, nullptr);
					MenuBuilder.AddMenuEntry(LOCTEXT("ZoomToFit", "Zoom to Fit"),
						LOCTEXT("ZoomToFitTip", "Frame the whole design canvas. F frames the selection instead."), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([WeakEditor]()
						{
							if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())Editor->ZoomDesignerToFit();
						})));
					MenuBuilder.AddMenuEntry(LOCTEXT("ZoomActualSize", "Zoom 1:1"),
						LOCTEXT("ZoomActualSizeTip", "One design unit per screen pixel: the size the UI will really be."), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([WeakEditor]()
						{
							if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())Editor->ZoomDesignerToActualSize();
						})));
					return MenuBuilder.MakeWidget();
				})
				.ButtonContent()
				[
					SNew(STextBlock).Text_Lambda([WeakEditor]()
					{
						TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin();
						if (!Editor.IsValid())return FText::GetEmpty();
						const float PixelsPerUnit = Editor->GetDesignerPixelsPerUnit();
						// A perspective view has a different scale at every depth, so there is no one
						// number to print; naming a wrong one is worse than naming none.
						if (PixelsPerUnit <= 0.0f)return LOCTEXT("ZoomReadout3D", "Zoom 3D");
						return FText::FromString(FString::Printf(TEXT("Zoom %.0f%%"), PixelsPerUnit * 100.0f));
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SComboButton)
				.HasDownArrow(true)
				.ToolTipText_Lambda([WeakEditor]()
				{
					TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin();
					if (!Editor.IsValid())return FText::GetEmpty();
					const FIntPoint Viewport = Editor->GetDesignerViewportSize();
					FIntPoint CanvasSize;
					float Scale = 1.0f;
					if (!Editor->CalculateDesignerCanvasFor(Viewport, CanvasSize, Scale))
					{
						return LOCTEXT("ScreenSizeTooltip_NoCanvas", "Design screen size: the device resolution to preview.\n\nThis prefab's root carries no LexCanvas, so nothing scales it and the design canvas simply equals the device resolution. Add a canvas to the root to preview a scale rule.");
					}
					return FText::FromString(FString::Printf(
						TEXT("Design screen size: the device resolution to preview.\n\nDevice %d x %d, and this prefab's canvas rule turns that into a %d x %d design canvas at %.3f scale.\nThe rule (Scale Mode / Reference Resolution / Screen Match Mode) lives on the LexCanvas of the prefab's root widget."),
						Viewport.X, Viewport.Y, CanvasSize.X, CanvasSize.Y, Scale));
				})
				.OnGetMenuContent_Lambda([WeakEditor]() -> TSharedRef<SWidget>
				{
					FMenuBuilder MenuBuilder(true, nullptr);
					MenuBuilder.BeginSection(NAME_None, LOCTEXT("ScreenSizeSection", "Screen Size"));
					for (const FLexUIDesignScreenSize& ScreenSize : GetLexUIDesignScreenSizes())
					{
						const FIntPoint Size = ScreenSize.Size;
						// Show what each device resolution actually becomes, so the rule is visible
						// at the point of choosing rather than only after the canvas jumps.
						FText Label = FText::FromString(ScreenSize.Label);
						if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())
						{
							FIntPoint CanvasSize;
							float Scale = 1.0f;
							if (Editor->CalculateDesignerCanvasFor(Size, CanvasSize, Scale) && CanvasSize != Size)
							{
								Label = FText::FromString(FString::Printf(TEXT("%s  ->  canvas %d x %d"),
									ScreenSize.Label, CanvasSize.X, CanvasSize.Y));
							}
						}
						MenuBuilder.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([WeakEditor, Size]()
							{
								if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())
								{
									Editor->SetDesignerSizeRule(ELexUIDesignerSizeRule::Custom);
									Editor->SetDesignerViewportSize(Size);
								}
							}), FCanExecuteAction(), FIsActionChecked::CreateLambda([WeakEditor, Size]()
							{
								if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())return Editor->GetDesignerViewportSize() == Size;
								return false;
							})), NAME_None, EUserInterfaceActionType::RadioButton);
					}
					MenuBuilder.EndSection();
					MenuBuilder.BeginSection(NAME_None, LOCTEXT("ScreenSizeRuleSection", "Size Rule"));
					MenuBuilder.AddMenuEntry(LOCTEXT("SizeRuleCustom", "Custom"),
						LOCTEXT("SizeRuleCustomTip", "The canvas is the resolution picked above, or typed below."), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([WeakEditor]()
						{
							if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())Editor->SetDesignerSizeRule(ELexUIDesignerSizeRule::Custom);
						}), FCanExecuteAction(), FIsActionChecked::CreateLambda([WeakEditor]()
						{
							if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())return Editor->GetDesignerSizeRule() == ELexUIDesignerSizeRule::Custom;
							return false;
						})), NAME_None, EUserInterfaceActionType::RadioButton);
					MenuBuilder.AddMenuEntry(LOCTEXT("SizeRuleDesired", "Desired"),
						LOCTEXT("SizeRuleDesiredTip", "Size the canvas to what the root widget's UMG-compatible panel measures, so a tooltip-sized prefab can be authored at its own size. Applied once, when chosen. A root with no such panel measures nothing, and a canvas sized by a scaler rule is not this menu's to set."), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([WeakEditor]()
						{
							if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())Editor->SetDesignerSizeRule(ELexUIDesignerSizeRule::Desired);
						}), FCanExecuteAction(), FIsActionChecked::CreateLambda([WeakEditor]()
						{
							if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())return Editor->GetDesignerSizeRule() == ELexUIDesignerSizeRule::Desired;
							return false;
						})), NAME_None, EUserInterfaceActionType::RadioButton);
					MenuBuilder.EndSection();
					MenuBuilder.BeginSection(NAME_None, LOCTEXT("ScreenSizeCustomSection", "Custom Size"));
					{
						auto MakeAxisEntry = [WeakEditor](bool bHorizontal) -> TSharedRef<SWidget>
						{
							return SNew(SBox).WidthOverride(84.0f)
							[
								SNew(SNumericEntryBox<int32>)
								.MinValue(1)
								.MinDesiredValueWidth(60.0f)
								.Value_Lambda([WeakEditor, bHorizontal]() -> TOptional<int32>
								{
									TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin();
									if (!Editor.IsValid())return TOptional<int32>();
									const FIntPoint Size = Editor->GetDesignerViewportSize();
									return bHorizontal ? Size.X : Size.Y;
								})
								.OnValueCommitted_Lambda([WeakEditor, bHorizontal](int32 NewValue, ETextCommit::Type)
								{
									TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin();
									if (!Editor.IsValid() || NewValue <= 0)return;
									FIntPoint Size = Editor->GetDesignerViewportSize();
									if (bHorizontal)Size.X = NewValue;
									else Size.Y = NewValue;
									Editor->SetDesignerSizeRule(ELexUIDesignerSizeRule::Custom);
									Editor->SetDesignerViewportSize(Size);
								})
							];
						};
						MenuBuilder.AddWidget(MakeAxisEntry(true), LOCTEXT("CustomSizeWidth", "Width"));
						MenuBuilder.AddWidget(MakeAxisEntry(false), LOCTEXT("CustomSizeHeight", "Height"));
					}
					MenuBuilder.EndSection();
					MenuBuilder.BeginSection(NAME_None, LOCTEXT("ScreenSizeToolsSection", "Tools"));
					MenuBuilder.AddMenuEntry(LOCTEXT("FlipOrientation", "Flip Orientation"),
						LOCTEXT("FlipOrientationTip", "Swap the previewed device resolution's width and height."), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([WeakEditor]()
						{
							if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())
							{
								const FIntPoint Size = Editor->GetDesignerViewportSize();
								Editor->SetDesignerViewportSize(FIntPoint(Size.Y, Size.X));
							}
						})));
					MenuBuilder.AddMenuEntry(LOCTEXT("ShowResolutionGuides", "Show Resolution Guides"),
						LOCTEXT("ShowResolutionGuidesTip", "Overlay common device resolutions on the design canvas, like UMG's designer surface. Needs the designer overlay switched on."), FSlateIcon(),
						// Greyed rather than dead: the overlay switch draws these guides or nothing does,
						// so with it off this entry could only report a state the viewport contradicts.
						FUIAction(FExecuteAction::CreateLambda([WeakEditor]()
						{
							if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())Editor->ToggleResolutionGuides();
						}), FCanExecuteAction::CreateLambda([WeakEditor]()
						{
							TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin();
							return Editor.IsValid() && Editor->GetShowDesignerChrome();
						}), FIsActionChecked::CreateLambda([WeakEditor]()
						{
							if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())return Editor->GetShowResolutionGuides();
							return false;
						})), NAME_None, EUserInterfaceActionType::ToggleButton);
					MenuBuilder.EndSection();
					return MenuBuilder.MakeWidget();
				})
				.ButtonContent()
				[
					SNew(STextBlock).Text_Lambda([WeakEditor]()
					{
						TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin();
						if (!Editor.IsValid())return FText::GetEmpty();
						const FIntPoint Viewport = Editor->GetDesignerViewportSize();
						FIntPoint CanvasSize;
						float Scale = 1.0f;
						// Naming only one number would hide exactly the discrepancy this picker exists
						// to expose, so show the device resolution and the canvas it really produces.
						if (Editor->CalculateDesignerCanvasFor(Viewport, CanvasSize, Scale) && CanvasSize != Viewport)
						{
							return FText::FromString(FString::Printf(TEXT("%d x %d  ->  %d x %d"),
								Viewport.X, Viewport.Y, CanvasSize.X, CanvasSize.Y));
						}
						return FText::FromString(FString::Printf(TEXT("%d x %d"), Viewport.X, Viewport.Y));
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
				.ToolTipText(LOCTEXT("LayoutDebugTooltip", "Show layout measurement, arrangement, slot, ownership, and clipping diagnostics for the selected widget. Needs the designer overlay switched on."))
				// The overlay switch draws this readout or nothing does, so with it off the checkbox
				// would sit Checked over a viewport showing none of it.
				.IsEnabled_Lambda([WeakEditor]()
				{
					TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin();
					return Editor.IsValid() && Editor->GetShowDesignerChrome();
				})
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
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText(LOCTEXT("RespectLocksTooltip", "Honour the designer locks. Switch it off to select and drag a locked widget without unlocking it; the locks themselves are left as they are."))
				.IsChecked_Lambda([WeakEditor]()
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())return Editor->GetRespectDesignerLocks() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					return ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakEditor](ECheckBoxState)
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())Editor->ToggleRespectDesignerLocks();
				})
				[
					SNew(SBox).WidthOverride(22).HeightOverride(22).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.Lock"))
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText(LOCTEXT("DesignerChromeTooltip", "Draw everything the editor puts over the prefab: the canvas boundary, selection outlines, handles, guides, layout diagnostics and readouts. Switch it off to see the prefab on its own; the gestures all still work."))
				.IsChecked_Lambda([WeakEditor]()
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())return Editor->GetShowDesignerChrome() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					return ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakEditor](ECheckBoxState)
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())Editor->ToggleShowDesignerChrome();
				})
				[
					SNew(SBox).WidthOverride(22).HeightOverride(22).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.Visibility"))
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
