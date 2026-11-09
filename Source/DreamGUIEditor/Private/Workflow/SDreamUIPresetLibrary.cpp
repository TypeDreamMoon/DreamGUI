// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Workflow/SDreamUIPresetLibrary.h"

#include "Designer/DreamWidgetBlueprintEditor.h"
#include "DreamGUIEditorModule.h"
#include "Widget/SDreamGradientPreview.h"
#include "Workflow/DreamUILayerStyleClipboard.h"
#include "Workflow/DreamUIStylePresets.h"

#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGradientAsset.h"
#include "Core/DreamLayerStylePreset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SDreamUIPresetLibrary"

namespace DreamUIPresetLibraryPanelLocal
{
	/** A name, asked for in a small modal window. Unset when it was cancelled. */
	TOptional<FString> PromptForName(const FText& InTitle, const FString& InDefault)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return TOptional<FString>();
		}
		const TSharedRef<FString> Name = MakeShared<FString>(InDefault);
		const TSharedRef<bool> bAccepted = MakeShared<bool>(false);
		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(InTitle)
			.SizingRule(ESizingRule::Autosized)
			.SupportsMaximize(false)
			.SupportsMinimize(false);
		const TWeakPtr<SWindow> WeakWindow = Window;
		auto Close = [WeakWindow, bAccepted](bool bInAccept)
		{
			*bAccepted = bInAccept;
			if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
			{
				Pinned->RequestDestroyWindow();
			}
		};
		Window->SetContent(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(12.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SEditableTextBox)
					.MinDesiredWidth(260.0f)
					.Text(FText::FromString(InDefault))
					.OnTextChanged_Lambda([Name](const FText& InText) { *Name = InText.ToString(); })
					.OnTextCommitted_Lambda([Name, Close](const FText& InText, ETextCommit::Type InCommit)
					{
						*Name = InText.ToString();
						if (InCommit == ETextCommit::OnEnter)
						{
							Close(true);
						}
					})
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SUniformGridPanel).SlotPadding(FMargin(4.0f, 0.0f))
					+ SUniformGridPanel::Slot(0, 0)
					[
						SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT("OK", "OK"))
						.OnClicked_Lambda([Close]() { Close(true); return FReply::Handled(); })
					]
					+ SUniformGridPanel::Slot(1, 0)
					[
						SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT("Cancel", "Cancel"))
						.OnClicked_Lambda([Close]() { Close(false); return FReply::Handled(); })
					]
				]
			]);
		FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());
		return *bAccepted && !Name->TrimStartAndEnd().IsEmpty() ? TOptional<FString>(Name->TrimStartAndEnd()) : TOptional<FString>();
	}

	FText KindLabel(EDreamUIPresetKind InKind)
	{
		switch (InKind)
		{
		case EDreamUIPresetKind::Gradient: return LOCTEXT("Gradients", "Gradients");
		case EDreamUIPresetKind::Style: return LOCTEXT("Styles", "Styles");
		case EDreamUIPresetKind::Effect: return LOCTEXT("Effects", "Effects");
		}
		return FText::GetEmpty();
	}

	void Notify(const FText& InMessage)
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InMessage.ToString());
	}
}

void SDreamUIPresetLibrary::Construct(const FArguments& InArgs, TSharedPtr<FDreamWidgetBlueprintEditor> InDesigner)
{
	using namespace DreamUIPresetLibraryPanelLocal;
	Designer = InDesigner;

	TSharedRef<SSegmentedControl<EDreamUIPresetKind>> Shelves = SNew(SSegmentedControl<EDreamUIPresetKind>)
		.Value_Lambda([this]() { return Kind; })
		.OnValueChanged(this, &SDreamUIPresetLibrary::SetKind);
	for (const EDreamUIPresetKind Shelf : { EDreamUIPresetKind::Gradient, EDreamUIPresetKind::Style, EDreamUIPresetKind::Effect })
	{
		Shelves->AddSlot(Shelf).Text(KindLabel(Shelf));
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
		[
			Shelves
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 0.0f, 4.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SAssignNew(SearchBox, SSearchBox)
				.OnTextChanged(this, &SDreamUIPresetLibrary::SetSearchText)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SComboButton)
				.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
				.ToolTipText(LOCTEXT("SaveSelectionTooltip", "Keep the selected widget's gradient, layer style or effect settings as a new preset on this shelf."))
				.IsEnabled(this, &SDreamUIPresetLibrary::HasSelection)
				.OnGetMenuContent(this, &SDreamUIPresetLibrary::MakeSaveMenu)
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.Plus")).ColorAndOpacity(FSlateColor::UseForeground())
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("SaveSelection", "Save Selection"))
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("RefreshTooltip", "Read the shelf again."))
				.OnClicked_Lambda([this]() { Refresh(); return FReply::Handled(); })
				[
					SNew(SImage).Image(FAppStyle::GetBrush("Icons.Refresh")).ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4.0f, 0.0f)
		[
			SAssignNew(List, SListView<TSharedPtr<FDreamUIPreset>>)
			.ListItemsSource(&Visible)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SDreamUIPresetLibrary::MakeRow)
			.OnMouseButtonDoubleClick(this, &SDreamUIPresetLibrary::OnDoubleClick)
			.OnContextMenuOpening(this, &SDreamUIPresetLibrary::MakeContextMenu)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text_Lambda([this]()
				{
					return LastError.IsEmpty()
						? LOCTEXT("Hint", "Double-click or drag a preset onto a widget. Right-click for more.")
						: LastError;
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Apply", "Apply to Selection"))
				.IsEnabled_Lambda([this]() { return HasSelection() && List.IsValid() && List->GetNumItemsSelected() == 1; })
				.OnClicked(this, &SDreamUIPresetLibrary::OnApplyClicked)
			]
		]
	];

	ChangedHandle = DreamUIPresetLibrary::OnChanged().AddSP(this, &SDreamUIPresetLibrary::Refresh);
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetAddedHandle = AssetRegistry.OnAssetAdded().AddSP(this, &SDreamUIPresetLibrary::OnAssetsChanged);
	AssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddSP(this, &SDreamUIPresetLibrary::OnAssetsChanged);
	Refresh();
}

SDreamUIPresetLibrary::~SDreamUIPresetLibrary()
{
	DreamUIPresetLibrary::OnChanged().Remove(ChangedHandle);
	if (FAssetRegistryModule* AssetRegistry = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
	{
		AssetRegistry->Get().OnAssetAdded().Remove(AssetAddedHandle);
		AssetRegistry->Get().OnAssetRemoved().Remove(AssetRemovedHandle);
	}
}

void SDreamUIPresetLibrary::OnAssetsChanged(const FAssetData& InAsset)
{
	const UClass* Class = InAsset.GetClass();
	if (Class != nullptr && (Class->IsChildOf<UDreamGradientAsset>() || Class->IsChildOf<UDreamLayerStylePreset>()))
	{
		Refresh();
	}
}

void SDreamUIPresetLibrary::SetKind(EDreamUIPresetKind InKind)
{
	Kind = InKind;
	LastError = FText::GetEmpty();
	Refresh();
}

void SDreamUIPresetLibrary::SetSearchText(const FText& InText)
{
	SearchText = InText.ToString();
	Filter();
}

void SDreamUIPresetLibrary::Refresh()
{
	TArray<FDreamUIPreset> Presets;
	DreamUIPresetLibrary::Collect(Kind, Presets);
	All.Reset();
	for (FDreamUIPreset& Preset : Presets)
	{
		All.Add(MakeShared<FDreamUIPreset>(MoveTemp(Preset)));
	}
	Filter();
}

void SDreamUIPresetLibrary::Filter()
{
	Visible.Reset();
	for (const TSharedPtr<FDreamUIPreset>& Preset : All)
	{
		if (SearchText.IsEmpty() || Preset->Name.ToString().Contains(SearchText))
		{
			Visible.Add(Preset);
		}
	}
	if (List.IsValid())
	{
		List->RequestListRefresh();
	}
}

bool SDreamUIPresetLibrary::HasSelection() const
{
	const TSharedPtr<FDreamWidgetBlueprintEditor> Pinned = Designer.Pin();
	if (!Pinned.IsValid())
	{
		return false;
	}
	for (const TWeakObjectPtr<UDreamWidget>& Widget : Pinned->GetSelectedWidgets())
	{
		if (Widget.IsValid())
		{
			return true;
		}
	}
	return false;
}

int32 SDreamUIPresetLibrary::ApplyToSelection(const FDreamUIPreset& InPreset, bool bInFollowAsset)
{
	const TSharedPtr<FDreamWidgetBlueprintEditor> Pinned = Designer.Pin();
	if (!Pinned.IsValid())
	{
		return 0;
	}
	TArray<UDreamWidget*> Targets;
	for (const TWeakObjectPtr<UDreamWidget>& Widget : Pinned->GetSelectedWidgets())
	{
		if (Widget.IsValid())
		{
			Targets.Add(Widget.Get());
		}
	}
	if (Targets.IsEmpty())
	{
		LastError = LOCTEXT("NothingSelected", "Select the widgets to apply the preset to.");
		return 0;
	}
	FText Error;
	const int32 Applied = DreamUIPresetLibrary::Apply(*Pinned, Targets, InPreset, bInFollowAsset, &Error);
	LastError = Applied > 0 ? FText::GetEmpty() : Error;
	return Applied;
}

void SDreamUIPresetLibrary::OnDoubleClick(TSharedPtr<FDreamUIPreset> InPreset)
{
	if (InPreset.IsValid())
	{
		ApplyToSelection(*InPreset);
	}
}

FReply SDreamUIPresetLibrary::OnApplyClicked()
{
	const TArray<TSharedPtr<FDreamUIPreset>> Selected = List.IsValid() ? List->GetSelectedItems() : TArray<TSharedPtr<FDreamUIPreset>>();
	if (Selected.Num() == 1)
	{
		ApplyToSelection(*Selected[0]);
	}
	return FReply::Handled();
}

TSharedRef<ITableRow> SDreamUIPresetLibrary::MakeRow(TSharedPtr<FDreamUIPreset> InPreset, const TSharedRef<STableViewBase>& InOwner)
{
	TSharedRef<SWidget> Preview = SNullWidget::NullWidget;
	switch (InPreset->Kind)
	{
	case EDreamUIPresetKind::Gradient:
	{
		FDreamGradient Gradient;
		InPreset->GetGradient(Gradient);
		Preview = SNew(SDreamGradientPreview).Gradient(Gradient).DesiredSize(FVector2D(48.0, 14.0));
		break;
	}
	case EDreamUIPresetKind::Style:
		Preview = SNew(SImage).Image(FAppStyle::GetBrush("Icons.Adjust")).ColorAndOpacity(FSlateColor::UseForeground());
		break;
	case EDreamUIPresetKind::Effect:
	{
		UClass* Class = nullptr;
		bool bIsComponent = false;
		InPreset->GetEffectClass(Class, bIsComponent);
		Preview = SNew(SImage).Image(FSlateIconFinder::FindIconBrushForClass(Class != nullptr ? Class : UObject::StaticClass()));
		break;
	}
	}
	const TSharedPtr<FDreamUIPreset> Captured = InPreset;
	const FText Source = InPreset->Source == EDreamUIPresetSource::Asset ? LOCTEXT("SourceAsset", "Asset") : LOCTEXT("SourceProject", "Project");
	const FText Tooltip = InPreset->Source == EDreamUIPresetSource::Asset
		? FText::FromString(InPreset->Asset.ToString())
		: FText::FromString(InPreset->Text);
	return SNew(STableRow<TSharedPtr<FDreamUIPreset>>, InOwner)
		.ToolTipText(Tooltip)
		.OnDragDetected_Lambda([Captured](const FGeometry&, const FPointerEvent& InEvent)
		{
			return InEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && Captured.IsValid()
				? FReply::Handled().BeginDragDrop(FDreamUIPresetDragDropOp::New(*Captured))
				: FReply::Unhandled();
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 2.0f, 6.0f, 2.0f)
			[
				SNew(SBox).MinDesiredWidth(48.0f).HAlign(HAlign_Center)
				[
					Preview
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromName(InPreset->Name))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(Source)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
}

TSharedPtr<SWidget> SDreamUIPresetLibrary::MakeContextMenu()
{
	using namespace DreamUIPresetLibraryPanelLocal;
	const TArray<TSharedPtr<FDreamUIPreset>> Selected = List.IsValid() ? List->GetSelectedItems() : TArray<TSharedPtr<FDreamUIPreset>>();
	if (Selected.Num() != 1)
	{
		return nullptr;
	}
	const TSharedPtr<FDreamUIPreset> Preset = Selected[0];
	const TWeakPtr<SDreamUIPresetLibrary> WeakThis = SharedThis(this);
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/true, nullptr);
	Menu.BeginSection("Apply", LOCTEXT("ApplySection", "Apply"));
	{
		Menu.AddMenuEntry(LOCTEXT("ApplyToSelection", "Apply to Selection"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakThis, Preset]() { if (const auto This = WeakThis.Pin()) This->ApplyToSelection(*Preset); }),
				FCanExecuteAction::CreateLambda([WeakThis]() { const auto This = WeakThis.Pin(); return This.IsValid() && This->HasSelection(); })));
		if (Preset->Source == EDreamUIPresetSource::Asset)
		{
			Menu.AddMenuEntry(LOCTEXT("ApplyCopy", "Apply as a Copy"),
				LOCTEXT("ApplyCopyTooltip", "Write the asset's values onto the widgets instead of following the asset, so later edits of the asset leave them as they are."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([WeakThis, Preset]() { if (const auto This = WeakThis.Pin()) This->ApplyToSelection(*Preset, /*bInFollowAsset*/false); }),
					FCanExecuteAction::CreateLambda([WeakThis]() { const auto This = WeakThis.Pin(); return This.IsValid() && This->HasSelection(); })));
		}
	}
	Menu.EndSection();
	Menu.BeginSection("Keep", LOCTEXT("KeepSection", "Save As"));
	{
		if (Preset->Source == EDreamUIPresetSource::ProjectSettings && Preset->Kind != EDreamUIPresetKind::Effect)
		{
			Menu.AddMenuEntry(LOCTEXT("SaveAsAsset", "Save as an Asset..."),
				LOCTEXT("SaveAsAssetTooltip", "Make an asset of this preset, which widgets can follow."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Save")),
				FUIAction(FExecuteAction::CreateLambda([WeakThis, Preset]()
				{
					FText Error;
					if (DreamUIPresetLibrary::SaveAsAsset(*Preset, FString(), &Error) == nullptr && !Error.IsEmpty())
					{
						if (const auto This = WeakThis.Pin()) This->LastError = Error;
					}
				})));
		}
		if (Preset->Source == EDreamUIPresetSource::Asset)
		{
			Menu.AddMenuEntry(LOCTEXT("SaveAsText", "Save in the Project Settings..."),
				LOCTEXT("SaveAsTextTooltip", "Keep a copy of this preset as text in the project settings."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Save")),
				FUIAction(FExecuteAction::CreateLambda([WeakThis, Preset]()
				{
					const TOptional<FString> Name = PromptForName(LOCTEXT("TextPresetName", "Text Preset Name"), Preset->Name.ToString());
					FText Error;
					if (Name.IsSet() && !DreamUIPresetLibrary::SaveAsText(*Preset, FName(*Name.GetValue()), &Error))
					{
						if (const auto This = WeakThis.Pin()) This->LastError = Error;
					}
				})));
		}
	}
	Menu.EndSection();
	Menu.BeginSection("Manage", LOCTEXT("ManageSection", "Preset"));
	{
		if (Preset->Source == EDreamUIPresetSource::Asset)
		{
			Menu.AddMenuEntry(LOCTEXT("Browse", "Browse to Asset"), FText::GetEmpty(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.BrowseContent")),
				FUIAction(FExecuteAction::CreateLambda([Preset]()
				{
					if (UObject* Asset = Preset->Asset.TryLoad(); Asset != nullptr && GEditor != nullptr)
					{
						TArray<UObject*> Assets = { Asset };
						GEditor->SyncBrowserToObjects(Assets);
					}
				})));
		}
		else
		{
			Menu.AddMenuEntry(LOCTEXT("Remove", "Remove from the Project Settings"), FText::GetEmpty(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Delete")),
				FUIAction(FExecuteAction::CreateLambda([Preset]() { DreamUIPresetLibrary::RemoveTextPreset(Preset->Kind, Preset->Name); })));
		}
	}
	Menu.EndSection();
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SDreamUIPresetLibrary::MakeSaveMenu()
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/true, nullptr);
	const TWeakPtr<SDreamUIPresetLibrary> WeakThis = SharedThis(this);
	Menu.AddMenuEntry(LOCTEXT("SaveSelectionText", "In the Project Settings..."),
		LOCTEXT("SaveSelectionTextTooltip", "A text preset, kept in the project settings."), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakThis]() { if (const auto This = WeakThis.Pin()) This->SaveSelection(/*bInAsAsset*/false); })));
	if (Kind != EDreamUIPresetKind::Effect)
	{
		Menu.AddMenuEntry(LOCTEXT("SaveSelectionAsset", "As an Asset..."),
			LOCTEXT("SaveSelectionAssetTooltip", "A preset asset, which widgets can follow."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakThis]() { if (const auto This = WeakThis.Pin()) This->SaveSelection(/*bInAsAsset*/true); })));
	}
	return Menu.MakeWidget();
}

void SDreamUIPresetLibrary::SaveSelection(bool bInAsAsset)
{
	using namespace DreamUIPresetLibraryPanelLocal;
	const TSharedPtr<FDreamWidgetBlueprintEditor> Pinned = Designer.Pin();
	if (!Pinned.IsValid() || Pinned->GetSelectedWidgets().IsEmpty() || !Pinned->GetSelectedWidgets()[0].IsValid())
	{
		return;
	}
	UDreamWidget* Preview = Pinned->GetSelectedWidgets()[0].Get();
	const UDreamWidget* Source = Pinned->GetTemplateWidget(Preview);
	Source = Source != nullptr ? Source : Preview;

	FString Text;
	if (!DreamUIPresetLibrary::DescribeWidget(Source, Kind, Text))
	{
		LastError = FText::Format(LOCTEXT("NothingToSave", "'{0}' has no {1} to keep."), FText::FromString(Source->GetDisplayName()),
			Kind == EDreamUIPresetKind::Gradient ? LOCTEXT("AGradient", "gradient") : Kind == EDreamUIPresetKind::Style
				? LOCTEXT("AStyle", "layer style") : LOCTEXT("AnEffect", "effect"));
		return;
	}
	FText Error;
	if (Kind == EDreamUIPresetKind::Style)
	{
		// An image editor's New Style: the window itself asks for the name and where it is kept.
		FDreamUILayerStyleSnippet Snippet;
		FDreamUILayerStyleSnippet::Parse(Text, Snippet);
		FDreamUIStylePresetSaveArgs Args;
		if (const UDreamLayerStyle* Style = Snippet.GetLayerStyle())
		{
			Args.Style = Style->GetStyle();
		}
		EDreamUIBlendMode Mode = EDreamUIBlendMode::Alpha;
		float Opacity = 1.0f;
		if (Snippet.GetBlending(Mode, Opacity))
		{
			FDreamLayerStyleBlending Blending;
			Blending.BlendMode = Mode;
			Blending.Opacity = Opacity;
			Args.Blending = Blending;
		}
		Args.Name = Source->GetDisplayName();
		DreamUIStylePresets::PromptNewStyle(Args, FSlateApplication::Get().FindWidgetWindow(AsShared()));
		return;
	}
	if (bInAsAsset)
	{
		FDreamUIPreset Probe;
		Probe.Kind = Kind;
		Probe.Name = FName(*Source->GetDisplayName());
		Probe.Text = Text;
		if (DreamUIPresetLibrary::SaveAsAsset(Probe, FString(), &Error) == nullptr)
		{
			LastError = Error;
		}
		return;
	}
	const TOptional<FString> Name = PromptForName(LOCTEXT("NewTextPresetName", "New Preset Name"), Source->GetDisplayName());
	if (Name.IsSet() && !DreamUIPresetLibrary::SaveTextPreset(Kind, FName(*Name.GetValue()), Text, &Error))
	{
		LastError = Error;
	}
}

#undef LOCTEXT_NAMESPACE
