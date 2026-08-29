// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIPrefabEditorViewportToolbar.h"
#include "DreamUIDesignScreenSizes.h"
#include "DreamUIPrefabEditorViewport.h"
#include "DreamWidgetBlueprintEditor.h"
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

#define LOCTEXT_NAMESPACE "SDreamUIPrefabEditorViewportToolbar"

namespace DreamUI_Private
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

namespace DreamUI_Private
{
	/** The screen-size picker: presets, size rule, a typed custom size, and the canvas tools. */
	static TSharedRef<SWidget> MakeScreenSizeMenu(TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor)
	{
		FMenuBuilder MenuBuilder(true, nullptr);
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("ScreenSizeSection", "Screen Size"));
		for (const FDreamUIDesignScreenSize& ScreenSize : GetDreamUIDesignScreenSizes())
		{
			const FIntPoint Size = ScreenSize.Size;
			// Show what each device resolution actually becomes, so the rule is visible
			// at the point of choosing rather than only after the canvas jumps.
			FText Label = FText::FromString(ScreenSize.Label);
			if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())
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
					if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())
					{
						Editor->SetDesignerSizeRule(EDreamUIDesignerSizeRule::Custom);
						Editor->SetDesignerViewportSize(Size);
					}
				}), FCanExecuteAction(), FIsActionChecked::CreateLambda([WeakEditor, Size]()
				{
					if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())return Editor->GetDesignerViewportSize() == Size;
					return false;
				})), NAME_None, EUserInterfaceActionType::RadioButton);
		}
		MenuBuilder.EndSection();
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("ScreenSizeRuleSection", "Size Rule"));
		MenuBuilder.AddMenuEntry(LOCTEXT("SizeRuleCustom", "Custom"),
			LOCTEXT("SizeRuleCustomTip", "The canvas is the resolution picked above, or typed below."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakEditor]()
			{
				if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())Editor->SetDesignerSizeRule(EDreamUIDesignerSizeRule::Custom);
			}), FCanExecuteAction(), FIsActionChecked::CreateLambda([WeakEditor]()
			{
				if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())return Editor->GetDesignerSizeRule() == EDreamUIDesignerSizeRule::Custom;
				return false;
			})), NAME_None, EUserInterfaceActionType::RadioButton);
		MenuBuilder.AddMenuEntry(LOCTEXT("SizeRuleFillScreen", "Fill Screen"),
			LOCTEXT("SizeRuleFillScreenTip", "The canvas is the viewport, one design unit per pixel, and follows it as the window resizes. Picking it zooms to 1:1 so that is true straight away. The resolution you chose stays on the asset and comes back with Custom."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakEditor]()
			{
				if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())Editor->SetDesignerSizeRule(EDreamUIDesignerSizeRule::FillScreen);
			}), FCanExecuteAction(), FIsActionChecked::CreateLambda([WeakEditor]()
			{
				if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())return Editor->GetDesignerSizeRule() == EDreamUIDesignerSizeRule::FillScreen;
				return false;
			})), NAME_None, EUserInterfaceActionType::RadioButton);
		MenuBuilder.AddMenuEntry(LOCTEXT("SizeRuleDesired", "Desired"),
			LOCTEXT("SizeRuleDesiredTip", "Size the canvas to what the root widget's UMG-compatible panel measures, so a tooltip-sized prefab can be authored at its own size. Applied once, when chosen. A root with no such panel measures nothing, and a canvas sized by a scaler rule is not this menu's to set."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakEditor]()
			{
				if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())Editor->SetDesignerSizeRule(EDreamUIDesignerSizeRule::Desired);
			}), FCanExecuteAction(), FIsActionChecked::CreateLambda([WeakEditor]()
			{
				if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())return Editor->GetDesignerSizeRule() == EDreamUIDesignerSizeRule::Desired;
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
						TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
						if (!Editor.IsValid())return TOptional<int32>();
						const FIntPoint Size = Editor->GetDesignerViewportSize();
						return bHorizontal ? Size.X : Size.Y;
					})
					.OnValueCommitted_Lambda([WeakEditor, bHorizontal](int32 NewValue, ETextCommit::Type)
					{
						TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
						if (!Editor.IsValid() || NewValue <= 0)return;
						FIntPoint Size = Editor->GetDesignerViewportSize();
						if (bHorizontal)Size.X = NewValue;
						else Size.Y = NewValue;
						Editor->SetDesignerSizeRule(EDreamUIDesignerSizeRule::Custom);
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
				if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())
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
				if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())Editor->ToggleResolutionGuides();
			}), FCanExecuteAction::CreateLambda([WeakEditor]()
			{
				TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
				return Editor.IsValid() && Editor->GetShowDesignerChrome();
			}), FIsActionChecked::CreateLambda([WeakEditor]()
			{
				if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())return Editor->GetShowResolutionGuides();
				return false;
			})), NAME_None, EUserInterfaceActionType::ToggleButton);
		MenuBuilder.EndSection();
		return MenuBuilder.MakeWidget();
	}
}

namespace DreamUI_Private
{
	/** The prefab editor behind the viewport a tool-menu section is being built for, or null. */
	static TWeakPtr<FDreamWidgetBlueprintEditor> GetPrefabEditorFromSection(const FToolMenuSection& InSection)
	{
		if (UUnrealEdViewportToolbarContext* const Context = InSection.FindContext<UUnrealEdViewportToolbarContext>())
		{
			if (TSharedPtr<SEditorViewport> Viewport = Context->Viewport.Pin())
			{
				return StaticCastSharedPtr<SDreamUIPrefabEditorViewport>(Viewport)->GetPrefabEditor();
			}
		}
		return nullptr;
	}

	/** A checked/unchecked toggle bound to one designer flag. */
	static FToolMenuEntry MakeDesignerToggle(const FName Name, const TWeakPtr<FDreamWidgetBlueprintEditor>& WeakEditor,
		const FText& Label, const FText& ToolTip, const FSlateIcon& Icon,
		bool (FDreamWidgetBlueprintEditor::*Getter)() const, void (FDreamWidgetBlueprintEditor::*Toggle)(),
		bool (FDreamWidgetBlueprintEditor::*EnabledWhen)() const = nullptr)
	{
		FUIAction Action(
			FExecuteAction::CreateLambda([WeakEditor, Toggle]()
			{
				if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin()) { ((*Editor).*Toggle)(); }
			}),
			FCanExecuteAction::CreateLambda([WeakEditor, EnabledWhen]()
			{
				TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
				return Editor.IsValid() && (EnabledWhen == nullptr || ((*Editor).*EnabledWhen)());
			}),
			FIsActionChecked::CreateLambda([WeakEditor, Getter]()
			{
				TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
				return Editor.IsValid() && ((*Editor).*Getter)();
			}));
		return FToolMenuEntry::InitToolBarButton(Name, FToolUIActionChoice(Action), Label, ToolTip, Icon, EUserInterfaceActionType::ToggleButton);
	}

	/**
	 * The designer's own controls, in the same tool-menu style as the camera and view-mode buttons
	 * on the right. Each group is its own section so the toolbar draws a separator between them:
	 *   [ Snap | 10 ]  [ 75% ]  [ 1000 x 500 ]  [ guides  diagnostics  locks  overlay ]
	 * Everything is built per viewport from the menu context, so two open prefab editors each
	 * drive their own designer rather than whichever one registered the menu first.
	 */
	static void AddDesignerSections(UToolMenu* InMenu)
	{
		const FName AppStyle = FAppStyle::GetAppStyleSetName();

		FToolMenuSection& SnapSection = InMenu->AddSection("DreamUISnapping");
		SnapSection.Alignment = EToolMenuSectionAlign::First;
		SnapSection.AddDynamicEntry("DesignerSnapping", FNewToolMenuSectionDelegate::CreateLambda([AppStyle](FToolMenuSection& InSection)
		{
			const TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor = GetPrefabEditorFromSection(InSection);
			InSection.AddEntry(MakeDesignerToggle("GridSnap", WeakEditor,
				LOCTEXT("DesignerSnapLabel", "Snap"),
				LOCTEXT("DesignerSnapTooltip", "Snap 2D designer movement and resize operations to the grid size next to this button."),
				FSlateIcon(AppStyle, "Icons.Snap"),
				&FDreamWidgetBlueprintEditor::IsDesignerGridSnapEnabled, &FDreamWidgetBlueprintEditor::ToggleDesignerGridSnap));

			const TAttribute<FText> GridLabel = TAttribute<FText>::CreateLambda([WeakEditor]()
			{
				if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin()) { return FText::AsNumber(Editor->GetDesignerGridSize()); }
				return FText::GetEmpty();
			});
			FToolMenuEntry GridEntry = FToolMenuEntry::InitComboButton("GridSize", FUIAction(),
				FNewToolMenuChoice(FOnGetContent::CreateLambda([WeakEditor]() -> TSharedRef<SWidget>
				{
					FMenuBuilder MenuBuilder(true, nullptr);
					MenuBuilder.BeginSection(NAME_None, LOCTEXT("GridSizeSection", "Grid Size"));
					for (float GridSize : { 1.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f })
					{
						MenuBuilder.AddMenuEntry(FText::AsNumber(GridSize), FText::GetEmpty(), FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([WeakEditor, GridSize]()
							{
								if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin()) { Editor->SetDesignerGridSize(GridSize); }
							}), FCanExecuteAction(), FIsActionChecked::CreateLambda([WeakEditor, GridSize]()
							{
								if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin()) { return FMath::IsNearlyEqual(Editor->GetDesignerGridSize(), GridSize); }
								return false;
							})), NAME_None, EUserInterfaceActionType::RadioButton);
					}
					MenuBuilder.EndSection();
					return MenuBuilder.MakeWidget();
				})),
				GridLabel,
				LOCTEXT("DesignerGridSizeTooltip", "2D designer grid size, in design units."),
				FSlateIcon(AppStyle, "EditorViewport.LocationGridSnap"));
			InSection.AddEntry(GridEntry);
		}));

		FToolMenuSection& ZoomSection = InMenu->AddSection("DreamUIZoom");
		ZoomSection.Alignment = EToolMenuSectionAlign::First;
		ZoomSection.AddDynamicEntry("DesignerZoom", FNewToolMenuSectionDelegate::CreateLambda([AppStyle](FToolMenuSection& InSection)
		{
			const TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor = GetPrefabEditorFromSection(InSection);
			const TAttribute<FText> ZoomLabel = TAttribute<FText>::CreateLambda([WeakEditor]()
			{
				TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
				if (!Editor.IsValid()) { return FText::GetEmpty(); }
				const float PixelsPerUnit = Editor->GetDesignerPixelsPerUnit();
				// A perspective view has a different scale at every depth, so there is no one
				// number to print; naming a wrong one is worse than naming none.
				if (PixelsPerUnit <= 0.0f) { return LOCTEXT("ZoomReadout3D", "Zoom"); }
				return FText::FromString(FString::Printf(TEXT("%.0f%%"), PixelsPerUnit * 100.0f));
			});
			InSection.AddEntry(FToolMenuEntry::InitComboButton("Zoom", FUIAction(),
				FNewToolMenuChoice(FOnGetContent::CreateLambda([WeakEditor]() -> TSharedRef<SWidget>
				{
					FMenuBuilder MenuBuilder(true, nullptr);
					MenuBuilder.BeginSection(NAME_None, LOCTEXT("ZoomSection", "Zoom"));
					MenuBuilder.AddMenuEntry(LOCTEXT("ZoomToFit", "Zoom to Fit"),
						LOCTEXT("ZoomToFitTip", "Frame the whole design canvas. F frames the selection instead."), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([WeakEditor]()
						{
							if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin()) { Editor->ZoomDesignerToFit(); }
						})));
					MenuBuilder.AddMenuEntry(LOCTEXT("ZoomActualSize", "Zoom 1:1"),
						LOCTEXT("ZoomActualSizeTip", "One design unit per screen pixel: the size the UI will really be."), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([WeakEditor]()
						{
							if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin()) { Editor->ZoomDesignerToActualSize(); }
						})));
					MenuBuilder.EndSection();
					return MenuBuilder.MakeWidget();
				})),
				ZoomLabel,
				LOCTEXT("DesignerZoomTooltip", "How much of a screen pixel one design unit covers, and how to reset it."),
				FSlateIcon(AppStyle, "Icons.Search")));
		}));

		FToolMenuSection& SizeSection = InMenu->AddSection("DreamUIScreenSize");
		SizeSection.Alignment = EToolMenuSectionAlign::First;
		SizeSection.AddDynamicEntry("DesignerScreenSize", FNewToolMenuSectionDelegate::CreateLambda([AppStyle](FToolMenuSection& InSection)
		{
			const TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor = GetPrefabEditorFromSection(InSection);
			const TAttribute<FText> SizeLabel = TAttribute<FText>::CreateLambda([WeakEditor]()
			{
				TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
				if (!Editor.IsValid()) { return FText::GetEmpty(); }
				const FIntPoint Viewport = Editor->GetDesignerViewportSize();
				FIntPoint CanvasSize;
				float Scale = 1.0f;
				// Naming only one number would hide exactly the discrepancy this picker exists
				// to expose, so show the device resolution and the canvas it really produces.
				if (Editor->CalculateDesignerCanvasFor(Viewport, CanvasSize, Scale) && CanvasSize != Viewport)
				{
					return FText::FromString(FString::Printf(TEXT("%d x %d  ->  %d x %d"), Viewport.X, Viewport.Y, CanvasSize.X, CanvasSize.Y));
				}
				return FText::FromString(FString::Printf(TEXT("%d x %d"), Viewport.X, Viewport.Y));
			});
			const TAttribute<FText> SizeToolTip = TAttribute<FText>::CreateLambda([WeakEditor]()
			{
				TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
				if (!Editor.IsValid()) { return FText::GetEmpty(); }
				const FIntPoint Viewport = Editor->GetDesignerViewportSize();
				FIntPoint CanvasSize;
				float Scale = 1.0f;
				if (!Editor->CalculateDesignerCanvasFor(Viewport, CanvasSize, Scale))
				{
					return LOCTEXT("ScreenSizeTooltip_NoCanvas", "Design screen size: the device resolution to preview.\n\nThis prefab's root carries no DreamCanvas, so nothing scales it and the design canvas simply equals the device resolution. Add a canvas to the root to preview a scale rule.");
				}
				return FText::FromString(FString::Printf(
					TEXT("Design screen size: the device resolution to preview.\n\nDevice %d x %d, and this prefab's canvas rule turns that into a %d x %d design canvas at %.3f scale.\nThe rule (Scale Mode / Reference Resolution / Screen Match Mode) lives on the DreamCanvas of the prefab's root widget."),
					Viewport.X, Viewport.Y, CanvasSize.X, CanvasSize.Y, Scale));
			});
			InSection.AddEntry(FToolMenuEntry::InitComboButton("ScreenSize", FUIAction(),
				FNewToolMenuChoice(FOnGetContent::CreateStatic(&MakeScreenSizeMenu, WeakEditor)),
				SizeLabel, SizeToolTip, FSlateIcon(AppStyle, "Icons.Layout")));
		}));

		FToolMenuSection& OverlaySection = InMenu->AddSection("DreamUIOverlays");
		OverlaySection.Alignment = EToolMenuSectionAlign::First;
		OverlaySection.AddDynamicEntry("DesignerOverlays", FNewToolMenuSectionDelegate::CreateLambda([AppStyle](FToolMenuSection& InSection)
		{
			const TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor = GetPrefabEditorFromSection(InSection);
			InSection.AddEntry(MakeDesignerToggle("Guides", WeakEditor, FText::GetEmpty(),
				LOCTEXT("DesignerGuidesTooltip", "Show 2D designer snapping guides while manipulating widgets."),
				FSlateIcon(AppStyle, "ViewportToolbar.SetShowGrid"),
				&FDreamWidgetBlueprintEditor::GetShowDesignerGuides, &FDreamWidgetBlueprintEditor::ToggleDesignerGuides));
			InSection.AddEntry(MakeDesignerToggle("Rulers", WeakEditor, FText::GetEmpty(),
				LOCTEXT("DesignerRulersTooltip", "Rulers along the top and left of the viewport, in the design canvas's own units, with the cursor marked on each. Needs the designer overlay switched on."),
				FSlateIcon(AppStyle, "Icons.Adjust"),
				&FDreamWidgetBlueprintEditor::GetShowDesignerRulers, &FDreamWidgetBlueprintEditor::ToggleDesignerRulers,
				&FDreamWidgetBlueprintEditor::GetShowDesignerChrome));
			InSection.AddEntry(MakeDesignerToggle("LayoutDebug", WeakEditor, FText::GetEmpty(),
				LOCTEXT("LayoutDebugTooltip", "Show layout measurement, arrangement, slot, ownership, and clipping diagnostics for the selected widget. Needs the designer overlay switched on."),
				FSlateIcon(AppStyle, "Icons.Info"),
				&FDreamWidgetBlueprintEditor::GetShowLayoutDebug, &FDreamWidgetBlueprintEditor::ToggleLayoutDebug,
				// The overlay switch draws this readout or nothing does, so with it off the button
				// would sit checked over a viewport showing none of it.
				&FDreamWidgetBlueprintEditor::GetShowDesignerChrome));
			InSection.AddEntry(MakeDesignerToggle("RespectLocks", WeakEditor, FText::GetEmpty(),
				LOCTEXT("RespectLocksTooltip", "Honour the designer locks. Switch it off to select and drag a locked widget without unlocking it; the locks themselves are left as they are."),
				FSlateIcon(AppStyle, "Icons.Lock"),
				&FDreamWidgetBlueprintEditor::GetRespectDesignerLocks, &FDreamWidgetBlueprintEditor::ToggleRespectDesignerLocks));
			InSection.AddEntry(MakeDesignerToggle("DesignerChrome", WeakEditor, FText::GetEmpty(),
				LOCTEXT("DesignerChromeTooltip", "Draw everything the editor puts over the prefab: the canvas boundary, selection outlines, handles, guides, layout diagnostics and readouts. Switch it off to see the prefab on its own; the gestures all still work."),
				FSlateIcon(AppStyle, "Icons.Visibility"),
				&FDreamWidgetBlueprintEditor::GetShowDesignerChrome, &FDreamWidgetBlueprintEditor::ToggleShowDesignerChrome));
		}));
	}
}

///////////////////////////////////////////////////////////
// SDreamUIPrefabEditorViewportToolbar

ICommonEditorViewportToolbarInfoProvider& SDreamUIPrefabEditorViewportToolbar::GetInfoProvider() const
{
	return *InfoProviderWeakPtr.Pin().Get();
}

void SDreamUIPrefabEditorViewportToolbar::Construct(const FArguments& InArgs, TSharedPtr<class ICommonEditorViewportToolbarInfoProvider> InInfoProvider)
{
	InfoProviderWeakPtr = InInfoProvider;

	// The base class SCommonEditorViewportToolbarBase::Construct() registers and populates the
	// globally-shared "UnrealEd.ViewportToolbar" tool menu, which adds the full set of buttons
	// (Transforms, Snapping, Camera, View Modes, Show, Performance/Scalability, Profile, Settings).
	// Since that menu is shared across editors we cannot trim it without affecting everyone, so
	// here we build a dedicated toolbar: the designer's own controls on the left, and only the
	// Camera and View Modes buttons on the right.
	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();

	static const FName DreamUIViewportToolbarName = TEXT("DreamUIPrefabEditor.ViewportToolbar");
	if (!UToolMenus::Get()->IsMenuRegistered(DreamUIViewportToolbarName))
	{
		UToolMenu* const ViewportToolbarMenu = UToolMenus::Get()->RegisterMenu(
			DreamUIViewportToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar
		);
		ViewportToolbarMenu->StyleName = TEXT("ViewportToolbar");

		DreamUI_Private::AddDesignerSections(ViewportToolbarMenu);

		FToolMenuSection& RightSection = ViewportToolbarMenu->AddSection("Right");
		RightSection.Alignment = EToolMenuSectionAlign::Last;
		{
			// Camera menu (custom: only 3D / 2D modes)
			RightSection.AddEntry(DreamUI_Private::MakeCameraSubmenuEntry());

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

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("EditorViewportToolBar.Background")))
		.Cursor(EMouseCursor::Default)
		[
			UToolMenus::Get()->GenerateWidget(DreamUIViewportToolbarName, ViewportToolbarContext)
		]
	];

	// Finish the SViewportToolBar base initialization (open-menu state, etc.)
	SViewportToolBar::Construct(SViewportToolBar::FArguments());
}

TSharedRef<SWidget> SDreamUIPrefabEditorViewportToolbar::GenerateShowMenu() const
{
	GetInfoProvider().OnFloatingButtonClicked();

	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();

	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder ShowMenuBuilder(bInShouldCloseWindowAfterMenuSelection, ViewportRef->GetCommandList());

	return ShowMenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
