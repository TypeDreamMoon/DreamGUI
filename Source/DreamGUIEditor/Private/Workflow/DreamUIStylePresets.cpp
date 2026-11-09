// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Workflow/DreamUIStylePresets.h"

#include "Core/DreamGUISettings.h"
#include "DreamGUIEditorModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IContentBrowserSingleton.h"
#include "Misc/PackageName.h"
#include "Styling/AppStyle.h"
#include "UObject/Package.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamUIStylePresets"

namespace DreamUIStylePresetsLocal
{
	bool Fail(FText* OutError, const FText& InReason)
	{
		if (OutError != nullptr)
		{
			*OutError = InReason;
		}
		return false;
	}

	/** Where the asset goes: InArgs' folder, or the one the Save Asset dialog was given. False when it was cancelled. */
	bool ResolveDestination(const FDreamUIStylePresetSaveArgs& InArgs, FString& OutPackagePath, FString& OutName)
	{
		OutName = InArgs.Name.IsEmpty() ? FString(TEXT("LS_NewStyle")) : InArgs.Name;
		if (!InArgs.PackagePath.IsEmpty())
		{
			OutPackagePath = InArgs.PackagePath;
			return true;
		}
		FSaveAssetDialogConfig Config;
		Config.DialogTitleOverride = LOCTEXT("SaveDialogTitle", "Save Style Preset");
		Config.DefaultPath = TEXT("/Game");
		Config.DefaultAssetName = OutName;
		Config.AssetClassNames.Add(UDreamLayerStylePreset::StaticClass()->GetClassPathName());
		Config.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::Disallow;
		const FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		const FString ObjectPath = ContentBrowser.Get().CreateModalSaveAssetDialog(Config);
		if (ObjectPath.IsEmpty())
		{
			return false;
		}
		const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
		OutPackagePath = FPackageName::GetLongPackagePath(PackageName);
		OutName = FPackageName::GetShortName(PackageName);
		return true;
	}
}

FSimpleMulticastDelegate& DreamUIStylePresets::OnPresetsChanged()
{
	static FSimpleMulticastDelegate Changed;
	return Changed;
}

UDreamLayerStylePreset* DreamUIStylePresets::CreatePresetAsset(const FDreamUIStylePresetSaveArgs& InArgs, FText* OutError)
{
	using namespace DreamUIStylePresetsLocal;
	FString PackagePath;
	FString Name;
	if (!ResolveDestination(InArgs, PackagePath, Name))
	{
		Fail(OutError, LOCTEXT("Cancelled", "No preset was made: the Save dialog was cancelled."));
		return nullptr;
	}
	if (!FPackageName::IsValidLongPackageName(PackagePath / Name, /*bIncludeReadOnlyRoots*/true))
	{
		Fail(OutError, FText::Format(LOCTEXT("BadPath", "'{0}' is not a content path a preset can be made in."), FText::FromString(PackagePath / Name)));
		return nullptr;
	}
	FString PackageName;
	FString AssetName;
	const FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	AssetTools.Get().CreateUniqueAssetName(PackagePath / Name, FString(), PackageName, AssetName);

	UPackage* Package = CreatePackage(*PackageName);
	if (Package == nullptr)
	{
		Fail(OutError, FText::Format(LOCTEXT("NoPackage", "The package '{0}' could not be made."), FText::FromString(PackageName)));
		return nullptr;
	}
	UDreamLayerStylePreset* Preset = NewObject<UDreamLayerStylePreset>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	Preset->SetStyle(InArgs.Style);
	Preset->SetBlending(InArgs.Blending.IsSet(), InArgs.Blending.Get(FDreamLayerStyleBlending()));
	FAssetRegistryModule::AssetCreated(Preset);
	Package->MarkPackageDirty();
	if (GEditor != nullptr && !GIsAutomationTesting)
	{
		TArray<UObject*> Made = { Preset };
		GEditor->SyncBrowserToObjects(Made);
	}
	OnPresetsChanged().Broadcast();
	return Preset;
}

bool DreamUIStylePresets::SaveTextPreset(const FDreamUIStylePresetSaveArgs& InArgs, FText* OutError)
{
	using namespace DreamUIStylePresetsLocal;
	const FString Name = InArgs.Name.TrimStartAndEnd();
	if (Name.IsEmpty())
	{
		return Fail(OutError, LOCTEXT("NoName", "A text preset needs a name."));
	}
	const FDreamUILayerStyleSnippet Snippet = FDreamUILayerStyleSnippet::FromStyle(InArgs.Style, nullptr,
		InArgs.Blending.IsSet() ? TOptional<EDreamUIBlendMode>(InArgs.Blending->BlendMode) : TOptional<EDreamUIBlendMode>(),
		InArgs.Blending.IsSet() ? InArgs.Blending->Opacity : 1.0f);
	if (Snippet.IsEmpty())
	{
		return Fail(OutError, LOCTEXT("NoText", "The style could not be written as text."));
	}
	UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
	Settings->StylePresets.Add(FName(*Name), Snippet.GetText());
	// The whole class: a map is no property UpdateSinglePropertyInConfigFile can write.
	Settings->TryUpdateDefaultConfigFile();
	OnPresetsChanged().Broadcast();
	return true;
}

FDreamUILayerStyleSnippet DreamUIStylePresets::MakeSnippet(const UDreamLayerStylePreset* InPreset, bool bInFollow)
{
	if (!IsValid(InPreset))
	{
		return FDreamUILayerStyleSnippet();
	}
	const bool bBlending = InPreset->IncludesBlending();
	return FDreamUILayerStyleSnippet::FromStyle(bInFollow ? FDreamLayerEffects() : InPreset->GetStyle(), bInFollow ? InPreset : nullptr,
		bBlending ? TOptional<EDreamUIBlendMode>(InPreset->GetBlending().BlendMode) : TOptional<EDreamUIBlendMode>(),
		InPreset->GetBlending().Opacity);
}

bool DreamUIStylePresets::PromptNewStyle(const FDreamUIStylePresetSaveArgs& InDefaults, TSharedPtr<SWindow> InParent)
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}
	struct FChoice
	{
		FString Name;
		bool bBlending = false;
		bool bAsAsset = true;
		bool bAccepted = false;
	};
	const TSharedRef<FChoice> Choice = MakeShared<FChoice>();
	Choice->Name = InDefaults.Name.IsEmpty() ? FString(TEXT("Style")) : InDefaults.Name;
	Choice->bBlending = InDefaults.Blending.IsSet();
	const bool bCanIncludeBlending = InDefaults.Blending.IsSet();

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("NewStyleTitle", "New Style"))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false);
	const TWeakPtr<SWindow> WeakWindow = Window;

	Window->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(12.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("NameLabel", "Name"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SEditableTextBox)
					.MinDesiredWidth(240.0f)
					.Text(FText::FromString(Choice->Name))
					.OnTextChanged_Lambda([Choice](const FText& InText) { Choice->Name = InText.ToString(); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				.IsEnabled(false)
				[
					SNew(STextBlock).Text(LOCTEXT("IncludeEffects", "Include Layer Effects"))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SCheckBox)
				.IsEnabled(bCanIncludeBlending)
				.IsChecked_Lambda([Choice]() { return Choice->bBlending ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([Choice](ECheckBoxState InState) { Choice->bBlending = InState == ECheckBoxState::Checked; })
				.ToolTipText(LOCTEXT("IncludeBlendingTooltip", "The widget's blend mode and opacity go with the style, and applying the preset sets them."))
				[
					SNew(STextBlock).Text(LOCTEXT("IncludeBlending", "Include Layer Blending Options"))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "RadioButton")
				.IsChecked_Lambda([Choice]() { return Choice->bAsAsset ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([Choice](ECheckBoxState) { Choice->bAsAsset = true; })
				.ToolTipText(LOCTEXT("AsAssetTooltip", "A Dream Layer Style Preset asset: a layer style can follow it, and every widget following it changes with it."))
				[
					SNew(STextBlock).Text(LOCTEXT("AsAsset", "Save as an asset..."))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "RadioButton")
				.IsChecked_Lambda([Choice]() { return Choice->bAsAsset ? ECheckBoxState::Unchecked : ECheckBoxState::Checked; })
				.OnCheckStateChanged_Lambda([Choice](ECheckBoxState) { Choice->bAsAsset = false; })
				.ToolTipText(LOCTEXT("AsTextTooltip", "Lines of .dui kept in the project settings (Plugins > Dream GUI > Editor > Style Presets)."))
				[
					SNew(STextBlock).Text(LOCTEXT("AsText", "Save in the project settings"))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(SUniformGridPanel).SlotPadding(FMargin(4.0f, 0.0f))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("OK", "OK"))
					.OnClicked_Lambda([Choice, WeakWindow]()
					{
						Choice->bAccepted = true;
						if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
						{
							Pinned->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.OnClicked_Lambda([WeakWindow]()
					{
						if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
						{
							Pinned->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
			]
		]);

	FSlateApplication::Get().AddModalWindow(Window, InParent);
	if (!Choice->bAccepted)
	{
		return false;
	}
	FDreamUIStylePresetSaveArgs Args = InDefaults;
	Args.Name = Choice->Name;
	if (!Choice->bBlending)
	{
		Args.Blending.Reset();
	}
	FText Error;
	const bool bMade = Choice->bAsAsset ? CreatePresetAsset(Args, &Error) != nullptr : SaveTextPreset(Args, &Error);
	if (!bMade && !Error.IsEmpty())
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Error.ToString());
	}
	return bMade;
}

#undef LOCTEXT_NAMESPACE
