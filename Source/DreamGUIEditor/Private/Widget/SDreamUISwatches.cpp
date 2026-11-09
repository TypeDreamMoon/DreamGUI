// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Widget/SDreamUISwatches.h"

#include "ISettingsModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDreamUISwatches"

namespace DreamUISwatchesLocal
{
	const FName SwatchesName = GET_MEMBER_NAME_CHECKED(UDreamGUISettings, ProjectSwatches);

	FString HexOf(const FColor& InColor)
	{
		return InColor.A == 255
			? FString::Printf(TEXT("#%02X%02X%02X"), InColor.R, InColor.G, InColor.B)
			: FString::Printf(TEXT("#%02X%02X%02X%02X"), InColor.R, InColor.G, InColor.B, InColor.A);
	}

	void Save()
	{
		UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
		// The whole class: an array is no property UpdateSinglePropertyInConfigFile can write.
		Settings->TryUpdateDefaultConfigFile();
		FDreamUIProjectPalette::OnChanged().Broadcast();
	}
}

const TArray<FDreamUISwatch>& FDreamUIProjectPalette::Get()
{
	return GetDefault<UDreamGUISettings>()->ProjectSwatches;
}

void FDreamUIProjectPalette::Add(const FColor& InSRGB, const FString& InName)
{
	UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
	if (Settings->ProjectSwatches.ContainsByPredicate([&InSRGB](const FDreamUISwatch& InSwatch) { return InSwatch.Color == InSRGB; }))
	{
		return;
	}
	Settings->ProjectSwatches.Add(FDreamUISwatch(InName.IsEmpty() ? DreamUISwatchesLocal::HexOf(InSRGB) : InName, InSRGB));
	DreamUISwatchesLocal::Save();
}

void FDreamUIProjectPalette::Remove(int32 InIndex)
{
	UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
	if (Settings->ProjectSwatches.IsValidIndex(InIndex))
	{
		Settings->ProjectSwatches.RemoveAt(InIndex);
		DreamUISwatchesLocal::Save();
	}
}

FSimpleMulticastDelegate& FDreamUIProjectPalette::OnChanged()
{
	static FSimpleMulticastDelegate Changed;
	static bool bForwarding = false;
	if (!bForwarding)
	{
		bForwarding = true;
		// An edit of the palette in the Project Settings window is a change too.
		GetMutableDefault<UDreamGUISettings>()->OnSettingChanged().AddLambda([](UObject*, FPropertyChangedEvent& InEvent)
		{
			if (InEvent.GetMemberPropertyName() == DreamUISwatchesLocal::SwatchesName)
			{
				Changed.Broadcast();
			}
		});
	}
	return Changed;
}

void FDreamUIProjectPalette::OpenInProjectSettings()
{
	if (ISettingsModule* Settings = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings")))
	{
		const UDreamGUISettings* Defaults = GetDefault<UDreamGUISettings>();
		Settings->ShowViewer(Defaults->GetContainerName(), Defaults->GetCategoryName(), Defaults->GetSectionName());
	}
}

// ---------------------------------------------------------------------------------------------------------------------

void SDreamUISwatchGrid::Construct(const FArguments& InArgs)
{
	OnSwatchClickedDelegate = InArgs._OnSwatchClicked;
	CurrentColor = InArgs._CurrentColor;
	SwatchSize = InArgs._SwatchSize;
	ChildSlot
	[
		SAssignNew(Box, SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(2.0f, 2.0f))
	];
	Rebuild();
	PaletteChangedHandle = FDreamUIProjectPalette::OnChanged().AddSP(this, &SDreamUISwatchGrid::Rebuild);
}

SDreamUISwatchGrid::~SDreamUISwatchGrid()
{
	FDreamUIProjectPalette::OnChanged().Remove(PaletteChangedHandle);
}

void SDreamUISwatchGrid::ClickSwatch(int32 InIndex)
{
	const TArray<FDreamUISwatch>& Swatches = FDreamUIProjectPalette::Get();
	if (Swatches.IsValidIndex(InIndex))
	{
		OnSwatchClickedDelegate.ExecuteIfBound(FLinearColor::FromSRGBColor(Swatches[InIndex].Color));
	}
}

void SDreamUISwatchGrid::AddCurrentColor()
{
	if (CurrentColor.IsBound() || CurrentColor.IsSet())
	{
		FDreamUIProjectPalette::Add(CurrentColor.Get().ToFColorSRGB());
	}
}

void SDreamUISwatchGrid::Rebuild()
{
	if (!Box.IsValid())
	{
		return;
	}
	Box->ClearChildren();
	const TArray<FDreamUISwatch>& Swatches = FDreamUIProjectPalette::Get();
	const TWeakPtr<SDreamUISwatchGrid> WeakThis = SharedThis(this);
	for (int32 Index = 0; Index < Swatches.Num(); ++Index)
	{
		const FDreamUISwatch& Swatch = Swatches[Index];
		const FText Tooltip = FText::Format(LOCTEXT("SwatchTooltip", "{0}  {1}\nClick to use it; right-click to take it out of the project's palette."),
			FText::FromString(Swatch.Name), FText::FromString(DreamUISwatchesLocal::HexOf(Swatch.Color)));
		Box->AddSlot()
		[
			SNew(SBox)
			.WidthOverride(SwatchSize)
			.HeightOverride(SwatchSize)
			.ToolTipText(Tooltip)
			[
				SNew(SColorBlock)
				.Color(FLinearColor::FromSRGBColor(Swatch.Color))
				.ShowBackgroundForAlpha(true)
				.CornerRadius(FVector4(2.0f, 2.0f, 2.0f, 2.0f))
				.OnMouseButtonDown_Lambda([WeakThis, Index](const FGeometry&, const FPointerEvent& InEvent)
				{
					const TSharedPtr<SDreamUISwatchGrid> Grid = WeakThis.Pin();
					if (!Grid.IsValid())
					{
						return FReply::Unhandled();
					}
					if (InEvent.GetEffectingButton() == EKeys::RightMouseButton)
					{
						FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/true, nullptr);
						Menu.AddMenuEntry(LOCTEXT("RemoveSwatch", "Remove from Project Palette"), FText::GetEmpty(), FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([Index]() { FDreamUIProjectPalette::Remove(Index); })));
						FSlateApplication::Get().PushMenu(Grid.ToSharedRef(), FWidgetPath(), Menu.MakeWidget(),
							InEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
						return FReply::Handled();
					}
					Grid->ClickSwatch(Index);
					return FReply::Handled();
				})
			]
		];
	}
	if (CurrentColor.IsBound() || CurrentColor.IsSet())
	{
		Box->AddSlot()
		[
			SNew(SBox)
			.WidthOverride(SwatchSize)
			.HeightOverride(SwatchSize)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(0.0f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ToolTipText(LOCTEXT("AddSwatchTooltip", "Add this colour to the project's palette."))
				.OnClicked_Lambda([WeakThis]()
				{
					if (const TSharedPtr<SDreamUISwatchGrid> Grid = WeakThis.Pin())
					{
						Grid->AddCurrentColor();
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Plus"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		];
	}
	if (Swatches.IsEmpty() && !(CurrentColor.IsBound() || CurrentColor.IsSet()))
	{
		Box->AddSlot()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoSwatches", "The project's palette is empty."))
			.Font(FAppStyle::GetFontStyle("SmallFont"))
		];
	}
}

// ---------------------------------------------------------------------------------------------------------------------

void SDreamUIColorQuickPick::Construct(const FArguments& InArgs)
{
	Color = InArgs._Color;
	OnColorCommittedDelegate = InArgs._OnColorCommitted;
	const FOnDreamUIColorCommitted OnPreview = InArgs._OnColorPreview;
	const FSimpleDelegate OnCancelled = InArgs._OnPreviewCancelled;

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	if (InArgs._bShowEyedropper)
	{
		Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SAssignNew(Eyedropper, SDreamUIEyedropperButton)
			.OnPreview_Lambda([OnPreview](const FDreamUIPickedColor& InColor) { OnPreview.ExecuteIfBound(InColor.Linear); })
			.OnCancelled(OnCancelled)
			.OnPicked_Lambda([this](const FDreamUIPickedColor& InColor) { OnColorCommittedDelegate.ExecuteIfBound(InColor.Linear); })
		];
	}
	Row->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.HasDownArrow(false)
		.ContentPadding(FMargin(1.0f))
		.ToolTipText(LOCTEXT("PaletteTooltip", "The project's palette: pick one of its colours, or add this one to it."))
		.OnGetMenuContent(this, &SDreamUIColorQuickPick::MakePaletteMenu)
		.ButtonContent()
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("ColorPicker.Mode"))
			.ColorAndOpacity(FSlateColor::UseForeground())
		]
	];
	ChildSlot
	[
		Row
	];
}

TSharedRef<SWidget> SDreamUIColorQuickPick::MakePaletteMenu()
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/true, nullptr);
	Menu.BeginSection("Swatches", LOCTEXT("ProjectPalette", "Project Palette"));
	Menu.AddWidget(
		SNew(SBox)
		.MaxDesiredWidth(200.0f)
		.Padding(FMargin(4.0f))
		[
			SNew(SDreamUISwatchGrid)
			.CurrentColor(Color)
			.OnSwatchClicked_Lambda([this](const FLinearColor& InColor)
			{
				OnColorCommittedDelegate.ExecuteIfBound(InColor);
				FSlateApplication::Get().DismissAllMenus();
			})
		],
		FText::GetEmpty(), /*bNoIndent*/true);
	Menu.EndSection();
	Menu.AddMenuSeparator();
	Menu.AddMenuEntry(LOCTEXT("AddToPalette", "Add This Colour to the Palette"), FText::GetEmpty(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Plus")),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { FDreamUIProjectPalette::Add(Color.Get().ToFColorSRGB()); }),
			FCanExecuteAction::CreateLambda([this]() { return Color.IsBound() || Color.IsSet(); })));
	Menu.AddMenuEntry(LOCTEXT("EditPalette", "Edit Project Palette..."),
		LOCTEXT("EditPaletteTooltip", "The palette is a project setting: Project Settings > Plugins > Dream GUI > Editor."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Settings")),
		FUIAction(FExecuteAction::CreateStatic(&FDreamUIProjectPalette::OpenInProjectSettings)));
	return Menu.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
