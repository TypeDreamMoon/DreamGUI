// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "SDreamWidgetDesignerViewportToolbar.h"
#include "DreamUIDesignScreenSizes.h"
#include "SDreamWidgetDesignerViewport.h"
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
#include "Internationalization/TextLocalizationManager.h"//the cultures the project ships, for the preview menu
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "ViewportToolbar/UnrealEdViewportToolbarContext.h"

#define LOCTEXT_NAMESPACE "SDreamWidgetDesignerViewportToolbar"

namespace DreamUI_Private
{
	/** What the zoom box will accept, in percent. 10% shows a 4K canvas whole; 1600% shows one pixel. */
	static constexpr float DreamUIDesignerMinZoomPercent = 10.0f;
	static constexpr float DreamUIDesignerMaxZoomPercent = 1600.0f;

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
	/**
	 * The cultures the project actually ships, plus "Source" for the text as authored.
	 *
	 * Asked of the localization manager rather than of FInternationalization's whole culture list:
	 * the latter is every culture the ICU data knows about, several hundred of them, and previewing
	 * a culture with no translations in it shows the source text while claiming otherwise.
	 */
	static TSharedRef<SWidget> MakePreviewCultureMenu(TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor)
	{
		FMenuBuilder MenuBuilder(true, nullptr);
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("PreviewCultureSection", "Preview Language"));
		auto AddCulture = [&MenuBuilder, WeakEditor](const FText& InLabel, const FString& InCultureName)
		{
			MenuBuilder.AddMenuEntry(InLabel, FText::GetEmpty(), FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([WeakEditor, InCultureName]()
					{
						if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())
						{
							Editor->SetPreviewCulture(InCultureName);
						}
					}),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([WeakEditor, InCultureName]()
					{
						TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
						return Editor.IsValid() && Editor->GetPreviewCulture() == InCultureName;
					})),
				NAME_None, EUserInterfaceActionType::RadioButton);
		};
		AddCulture(LOCTEXT("PreviewCultureSource", "Source (no preview)"), FString());

		TArray<FString> CultureNames = FTextLocalizationManager::Get().GetLocalizedCultureNames(ELocalizationLoadFlags::Game);
		CultureNames.Sort();
		for (const FString& CultureName : CultureNames)
		{
			if (CultureName.IsEmpty())
			{
				continue;
			}
			// The culture's own display name where ICU knows one, so the list reads as languages
			// rather than as codes; the code itself stays in the tooltip-free label after it.
			FText Label = FText::FromString(CultureName);
			if (const FCulturePtr Culture = FInternationalization::Get().GetCulture(CultureName))
			{
				Label = FText::FromString(FString::Printf(TEXT("%s  (%s)"),
					*Culture->GetDisplayName(), *CultureName));
			}
			AddCulture(Label, CultureName);
		}
		if (CultureNames.Num() == 0)
		{
			// Said out loud: an empty menu is indistinguishable from a broken one, and "this project
			// has no translations yet" is the answer.
			MenuBuilder.AddWidget(
				SNew(SBox).Padding(FMargin(12.0f, 4.0f))
				[
					SNew(STextBlock).Text(LOCTEXT("PreviewCultureNoneAvailable", "This project ships no game translations."))
				],
				FText::GetEmpty());
		}
		MenuBuilder.EndSection();
		return MenuBuilder.MakeWidget();
	}

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
	/** The designer behind the viewport a tool-menu section is being built for, or null. */
	static TWeakPtr<FDreamWidgetBlueprintEditor> GetDesignerFromSection(const FToolMenuSection& InSection)
	{
		if (UUnrealEdViewportToolbarContext* const Context = InSection.FindContext<UUnrealEdViewportToolbarContext>())
		{
			if (TSharedPtr<SEditorViewport> Viewport = Context->Viewport.Pin())
			{
				return StaticCastSharedPtr<SDreamWidgetDesignerViewport>(Viewport)->GetDesigner();
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
	 * Everything is built per viewport from the menu context, so two open designers each
	 * drive their own designer rather than whichever one registered the menu first.
	 */
	static void AddDesignerSections(UToolMenu* InMenu)
	{
		const FName AppStyle = FAppStyle::GetAppStyleSetName();

		FToolMenuSection& SnapSection = InMenu->AddSection("DreamUISnapping");
		SnapSection.Alignment = EToolMenuSectionAlign::First;
		SnapSection.AddDynamicEntry("DesignerSnapping", FNewToolMenuSectionDelegate::CreateLambda([AppStyle](FToolMenuSection& InSection)
		{
			const TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor = GetDesignerFromSection(InSection);
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
			const TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor = GetDesignerFromSection(InSection);
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
					// A number to type and a slider to drag. "Fit" and "1:1" are the two zooms an
					// author can name; every other one -- 200% to look at a 12-pixel icon, 50% to see
					// a tall screen whole -- had no way in at all.
					MenuBuilder.BeginSection(NAME_None, LOCTEXT("ZoomLevelSection", "Zoom Level"));
					MenuBuilder.AddWidget(
						SNew(SBox)
						.Padding(FMargin(8.0f, 2.0f))
						.MinDesiredWidth(140.0f)
						[
							SNew(SNumericEntryBox<float>)
							.AllowSpin(true)
							.MinValue(DreamUIDesignerMinZoomPercent)
							.MaxValue(DreamUIDesignerMaxZoomPercent)
							.MinSliderValue(DreamUIDesignerMinZoomPercent)
							.MaxSliderValue(DreamUIDesignerMaxZoomPercent)
							.Delta(5.0f)
							.Value_Lambda([WeakEditor]() -> TOptional<float>
							{
								TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
								const float PixelsPerUnit = Editor.IsValid() ? Editor->GetDesignerPixelsPerUnit() : 0.0f;
								// Unset, not zero: the 3D camera has a different scale at every depth,
								// and a box reading "0%" is a number that is simply wrong.
								if (PixelsPerUnit <= 0.0f) { return TOptional<float>(); }
								return PixelsPerUnit * 100.0f;
							})
							.OnValueChanged_Lambda([WeakEditor](float InPercent)
							{
								if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())
								{
									Editor->SetDesignerPixelsPerUnit(InPercent / 100.0f);
								}
							})
							.OnValueCommitted_Lambda([WeakEditor](float InPercent, ETextCommit::Type)
							{
								if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())
								{
									Editor->SetDesignerPixelsPerUnit(InPercent / 100.0f);
								}
							})
						],
						LOCTEXT("ZoomPercentLabel", "Zoom %"));
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
			const TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor = GetDesignerFromSection(InSection);
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
			const TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor = GetDesignerFromSection(InSection);
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
			InSection.AddEntry(MakeDesignerToggle("SafeZone", WeakEditor, FText::GetEmpty(),
				LOCTEXT("SafeZoneTooltip", "Draw the platform's title-safe area on the design canvas. Its own switch, so seeing it no longer means turning the device-resolution overlay on as well. A desktop that declares no safe area draws nothing; r.DebugSafeZone.TitleRatio is what makes one appear."),
				FSlateIcon(AppStyle, "Icons.Alert"),
				&FDreamWidgetBlueprintEditor::GetShowSafeZone, &FDreamWidgetBlueprintEditor::ToggleShowSafeZone,
				&FDreamWidgetBlueprintEditor::GetShowDesignerChrome));
			InSection.AddEntry(MakeDesignerToggle("DesignerChrome", WeakEditor, FText::GetEmpty(),
				LOCTEXT("DesignerChromeTooltip", "Draw everything the editor puts over the prefab: the canvas boundary, selection outlines, handles, guides, layout diagnostics and readouts. Switch it off to see the prefab on its own; the gestures all still work."),
				FSlateIcon(AppStyle, "Icons.Visibility"),
				&FDreamWidgetBlueprintEditor::GetShowDesignerChrome, &FDreamWidgetBlueprintEditor::ToggleShowDesignerChrome));
		}));

		// What the screen will really be shown AS, rather than what is on it: the application scale
		// the project's DPI curve applies, and the language the text resolves in. Both are things a
		// layout can be wrong about while looking perfect in the designer.
		FToolMenuSection& PreviewSection = InMenu->AddSection("DreamUIPreview");
		PreviewSection.Alignment = EToolMenuSectionAlign::First;
		PreviewSection.AddDynamicEntry("DesignerPreview", FNewToolMenuSectionDelegate::CreateLambda([AppStyle](FToolMenuSection& InSection)
		{
			const TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor = GetDesignerFromSection(InSection);
			InSection.AddEntry(MakeDesignerToggle("PreviewDPI", WeakEditor, FText::GetEmpty(),
				LOCTEXT("PreviewDPITooltip", "Shrink the design canvas by the project's DPI curve, the way UMG's designer does. The curve is the project's own (Project Settings > User Interface), so this previews the application scale Slate will really apply at the chosen device resolution -- and it runs before the DreamCanvas scale rule, which is the order the runtime uses."),
				FSlateIcon(AppStyle, "Icons.Adjust"),
				&FDreamWidgetBlueprintEditor::GetPreviewDPIScale, &FDreamWidgetBlueprintEditor::TogglePreviewDPIScale));

			const TAttribute<FText> CultureLabel = TAttribute<FText>::CreateLambda([WeakEditor]()
			{
				TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin();
				const FString Culture = Editor.IsValid() ? Editor->GetPreviewCulture() : FString();
				return Culture.IsEmpty() ? LOCTEXT("PreviewCultureNone", "Source") : FText::FromString(Culture);
			});
			InSection.AddEntry(FToolMenuEntry::InitComboButton("PreviewCulture", FUIAction(),
				FNewToolMenuChoice(FOnGetContent::CreateStatic(&MakePreviewCultureMenu, WeakEditor)),
				CultureLabel,
				LOCTEXT("PreviewCultureTooltip", "Resolve the preview's text in another culture, the way UMG's Localization Preview does. Game text only -- the editor stays in your own language -- and it is switched off again when this designer closes."),
				FSlateIcon(AppStyle, "Icons.Localization")));
		}));

		// Align / Distribute, on the surface the widgets are on. They were only ever reachable by
		// right-clicking a row in the hierarchy panel.
		FToolMenuSection& ArrangeSection = InMenu->AddSection("DreamUIArrange");
		ArrangeSection.Alignment = EToolMenuSectionAlign::First;
		ArrangeSection.AddDynamicEntry("DesignerArrange", FNewToolMenuSectionDelegate::CreateLambda([AppStyle](FToolMenuSection& InSection)
		{
			const TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor = GetDesignerFromSection(InSection);
			InSection.AddEntry(FToolMenuEntry::InitComboButton("Arrange",
				FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([WeakEditor]()
				{
					return FDreamWidgetBlueprintEditor::HasAlignDistributeEntries(WeakEditor);
				})),
				FNewToolMenuChoice(FNewMenuDelegate::CreateLambda([WeakEditor](FMenuBuilder& InMenuBuilder)
				{
					FDreamWidgetBlueprintEditor::FillAlignDistributeMenu(InMenuBuilder, WeakEditor);
				})),
				LOCTEXT("ArrangeLabel", "Arrange"),
				LOCTEXT("ArrangeTooltip", "Align or distribute the selected sibling widgets. Align needs two selected, Distribute three."),
				FSlateIcon(AppStyle, "Icons.Layout")));
		}));
	}
}

///////////////////////////////////////////////////////////
// SDreamWidgetDesignerViewportToolbar

ICommonEditorViewportToolbarInfoProvider& SDreamWidgetDesignerViewportToolbar::GetInfoProvider() const
{
	// A reference cannot say "gone", so the only guard available is one that names the failure: the
	// provider is the viewport widget, and a toolbar that outlives it would otherwise dereference
	// null somewhere far from here. The base class has the same hole and the same signature.
	const TSharedPtr<ICommonEditorViewportToolbarInfoProvider> InfoProvider = InfoProviderWeakPtr.Pin();
	checkf(InfoProvider.IsValid(), TEXT("The designer viewport toolbar outlived the viewport it belongs to."));
	return *InfoProvider.Get();
}

FName SDreamWidgetDesignerViewportToolbar::GetViewportToolbarMenuName()
{
	return TEXT("DreamWidgetDesigner.ViewportToolbar");
}

void SDreamWidgetDesignerViewportToolbar::Construct(const FArguments& InArgs, TSharedPtr<class ICommonEditorViewportToolbarInfoProvider> InInfoProvider)
{
	InfoProviderWeakPtr = InInfoProvider;

	// The base class SCommonEditorViewportToolbarBase::Construct() registers and populates the
	// globally-shared "UnrealEd.ViewportToolbar" tool menu, which adds the full set of buttons
	// (Transforms, Snapping, Camera, View Modes, Show, Performance/Scalability, Profile, Settings).
	// Since that menu is shared across editors we cannot trim it without affecting everyone, so
	// here we build a dedicated toolbar: the designer's own controls on the left, and only the
	// Camera and View Modes buttons on the right.
	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();

	const FName DreamUIViewportToolbarName = GetViewportToolbarMenuName();
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

#undef LOCTEXT_NAMESPACE
