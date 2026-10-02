// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamUIFontData_FreeTypeRenderCustomization.h"
#include "DreamDetailsMultiSelect.h"
#include "Misc/FileHelper.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Widget/DreamUIFileBrowser.h"

#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Styling/SlateTypes.h"
#include "Core/DreamUITextData.h"
#include "Core/Text/DreamFontFaceResolver.h"
#include "Internationalization/BreakIterator.h"
#include "PropertyType/DreamUIFontFallbackCustomization.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamGUIFreeTypeRenderFontDataCustomization"

// The checkbox below is the one row of this panel that is written from a lambda instead of by a
// property row, so the write lives here where a test can reach it: writing the UPROPERTY directly
// leaves the package clean, and the toggle is then gone the next time the asset is loaded.
// DreamSmallCustomizationAutomationTests.cpp declares both of these again - this customization has no
// shared header to hang them on.
namespace DreamUIFontDataCustomization
{
	DREAMGUIEDITOR_API ECheckBoxState GetUseRelativeFilePathState(TSharedPtr<IPropertyHandle> InHandle)
	{
		bool bValue = false;
		// A mixed selection has no single answer, and the checkbox has a state for exactly that.
		if (!InHandle.IsValid() || InHandle->GetValue(bValue) != FPropertyAccess::Success)return ECheckBoxState::Undetermined;
		return bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	DREAMGUIEDITOR_API bool SetUseRelativeFilePath(TSharedPtr<IPropertyHandle> InHandle, bool bInUseRelativeFilePath)
	{
		if (!InHandle.IsValid())return false;
		return InHandle->SetValue(bInUseRelativeFilePath) == FPropertyAccess::Success;
	}
}

namespace DreamUIFontResolveSampleLocal
{
	/** A pasted paragraph would make a row per character; past this many the row says how many it left out. */
	constexpr int32 MaxResolveSampleRows = 48;

	/** A cluster's code points in order, surrogate pairs joined. */
	void DecodeCluster(const FString& InClusterText, TArray<uint32>& OutCluster)
	{
		OutCluster.Reset();
		const int ClusterLength = InClusterText.Len();
		for (int CharIndex = 0; CharIndex < ClusterLength;)
		{
			int CodeUnits = 1;
			OutCluster.Add(FDreamUIText_CodePoint::DecodeCodePointAt(InClusterText, ClusterLength, CharIndex, CodeUnits));
			CharIndex += CodeUnits;
		}
	}

	FString FormatCodepoints(TConstArrayView<uint32> InCluster)
	{
		TArray<FString> Codepoints;
		for (const uint32 Codepoint : InCluster)
		{
			Codepoints.Add(FString::Printf(TEXT("U+%04X"), Codepoint));
		}
		return FString::Join(Codepoints, TEXT(" "));
	}
}

FString FDreamUIFontData_FreeTypeRenderCustomization::ResolveSampleText;
FString FDreamUIFontData_FreeTypeRenderCustomization::ResolveSampleCulture;

TSharedRef<IDetailCustomization> FDreamUIFontData_FreeTypeRenderCustomization::MakeInstance()
{
	return MakeShareable(new FDreamUIFontData_FreeTypeRenderCustomization);
}

void FDreamUIFontData_FreeTypeRenderCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = targetObjects.Num() > 0 ? Cast<UDreamUIFontData_FreeTypeRender>(targetObjects[0].Get()) : nullptr;
	if (!TargetScriptPtr.IsValid())
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	auto fontTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, FontType));
	//SP, not Raw: the handle outlives this customization, and the refresh it asks for is what destroys it
	fontTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIFontData_FreeTypeRenderCustomization::ForceRefresh, &DetailBuilder));
	const auto fontType = (EDreamUIDynamicFontDataType)DreamDetailsMultiSelect::ValueOr<uint8>(fontTypeHandle, 0);

	IDetailCategoryBuilder& dreamguiCategory = DetailBuilder.EditCategory("DreamGUI");
	dreamguiCategory.AddCustomRow(LOCTEXT("ReloadFont", "ReloadFont"))
	.WholeRowContent()
	[
		SNew(SButton)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.OnClicked(this, &FDreamUIFontData_FreeTypeRenderCustomization::OnReloadButtonClicked, &DetailBuilder)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ReloadFont", "ReloadFont"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	]
	;
	dreamguiCategory.AddProperty(fontTypeHandle);
	TArray<FName> propertiesNeedToHide;
	if (fontType == EDreamUIDynamicFontDataType::EngineFont)
	{
		propertiesNeedToHide.Add(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, FontFilePath));
		propertiesNeedToHide.Add(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, bUseRelativeFilePath));
		propertiesNeedToHide.Add(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, bUseExternalFileOrEmbedInToUAsset));
		propertiesNeedToHide.Add(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, FontFace));

		dreamguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, EngineFont));
	}
	else
	{
		propertiesNeedToHide.Add(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, FontFilePath));
		propertiesNeedToHide.Add(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, bUseRelativeFilePath));
		propertiesNeedToHide.Add(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, bUseExternalFileOrEmbedInToUAsset));
		propertiesNeedToHide.Add(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, EngineFont));
		propertiesNeedToHide.Add(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, FontFace));

		auto fontFilePathHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, FontFilePath));
		auto useRelativeFilePathHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, bUseRelativeFilePath));
		// The layout builder is rebuilt by the refresh the checkbox itself asks for, so the checkbox
		// must not hold on to it; the utilities outlive the layout and are how a row asks for a refresh.
		TWeakPtr<IPropertyUtilities> propertyUtilities = DetailBuilder.GetPropertyUtilities();
		TWeakObjectPtr<UDreamUIFontData_FreeTypeRender> weakTarget = TargetScriptPtr;
		dreamguiCategory.AddCustomRow(LOCTEXT("FontSourceFileCategory","FontSourceFile"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FontSourceFile", "Font Source File"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(600)
		[	
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.MaxWidth(500)
			[
				SNew(SDreamUIFileBrowser)
				.FolderPath(this, &FDreamUIFontData_FreeTypeRenderCustomization::OnGetFontFilePath)
				.DialogTitle(TEXT("Browse for a font data file"))
				.DefaultFileName("font.ttf")
				.Filter(TEXT("Font file(*.ttf,*.ttc,*.otf)|*.ttf;*.ttc;*.otf|Any font file|*.*"))
				.OnFilePathChanged(this, &FDreamUIFontData_FreeTypeRenderCustomization::OnPathTextChanged, fontFilePathHandle)
				.OnFilePathCommitted(this, &FDreamUIFontData_FreeTypeRenderCustomization::OnPathTextCommitted, fontFilePathHandle, &DetailBuilder)
			]
			+SHorizontalBox::Slot()
			.MaxWidth(100)
			.Padding(FMargin(5, 0, 0, 0))
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([useRelativeFilePathHandle]() {return DreamUIFontDataCustomization::GetUseRelativeFilePathState(useRelativeFilePathHandle); })
				.OnCheckStateChanged_Lambda([useRelativeFilePathHandle, weakTarget, propertyUtilities](ECheckBoxState State)
					{
						if (!DreamUIFontDataCustomization::SetUseRelativeFilePath(useRelativeFilePathHandle, State == ECheckBoxState::Checked))return;
						if (auto Target = weakTarget.Get())
						{
							Target->ReloadFont();
						}
						if (auto Utilities = propertyUtilities.Pin())
						{
							Utilities->ForceRefresh();
						}
					})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("UseRelativePath","Relative To \"ProjectDir\""))
					.ToolTipText(LOCTEXT("Tooltip", "Font file use relative path(relative to ProjectDir) or absolute path. After build your game, remember to copy your font file to target path, unless \"UseExternalFileOrEmbedInToUAsset\" is false"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]
		]
		;
		TargetScriptPtr->InitFreeType();
		if (TargetScriptPtr->bAlreadyInitialized == false)
		{
			dreamguiCategory.AddCustomRow(LOCTEXT("ErrorTip", "ErrorTip"))
			.WholeRowContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InitializeFontFail", "Initialize font fail, check outputlog for detail"))
				.ColorAndOpacity(FSlateColor(FLinearColor::Yellow))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			;
		}
		dreamguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, bUseExternalFileOrEmbedInToUAsset));
	}

	//faces
	FontFaceOptions.Empty();
	for (auto Face : TargetScriptPtr->SubFaces)
	{
		FontFaceOptions.Add(MakeShareable(new FString(Face)));
	}
	auto fontFaceHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, FontFace));
	dreamguiCategory.AddCustomRow(LOCTEXT("FontFace", "FontFace"))
		.NameContent()
		[
			fontFaceHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SComboButton)
			.HasDownArrow(true)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				[
					SNew(STextBlock)
					.Font(DetailBuilder.GetDetailFont())
					.Text(this, &FDreamUIFontData_FreeTypeRenderCustomization::FontFaceOptions_GetCurrentFace)
				]
			]
			.MenuContent()
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SListView<TSharedPtr<FString>>)
					.ListItemsSource(&FontFaceOptions)
					.OnGenerateRow(this, &FDreamUIFontData_FreeTypeRenderCustomization::FontFaceOptions_GenerateComboItem, &DetailBuilder)
					.OnSelectionChanged(this, &FDreamUIFontData_FreeTypeRenderCustomization::FontFaceOptions_OnComboChanged, fontFaceHandle, &DetailBuilder)
				]
			]
		]
		;

	// The fallbacks right under the face, with what a sample resolves to through them. FallbackFontArray, the list old
	// assets load into, has no row: it has no edit flag, and loading moves it into Fallbacks and empties it.
	auto fallbacksHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, Fallbacks));
	auto preferColorEmojiHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUIFontData_FreeTypeRender, bPreferColorEmoji));
	dreamguiCategory.AddProperty(fallbacksHandle);
	dreamguiCategory.AddProperty(preferColorEmojiHandle);
	// The font has rebuilt its face table in PostEditChangeProperty by the time these fire, so the sample sees the edit.
	const FSimpleDelegate refreshResolveSample = FSimpleDelegate::CreateSP(this, &FDreamUIFontData_FreeTypeRenderCustomization::RefreshResolveSample);
	fallbacksHandle->SetOnPropertyValueChanged(refreshResolveSample);
	fallbacksHandle->SetOnChildPropertyValueChanged(refreshResolveSample);
	preferColorEmojiHandle->SetOnPropertyValueChanged(refreshResolveSample);
	AddResolveSampleRow(dreamguiCategory);

	for (auto& propertyName : propertiesNeedToHide)
	{
		DetailBuilder.HideProperty(propertyName);
	}
}

FText FDreamUIFontData_FreeTypeRenderCustomization::FontFaceOptions_GetCurrentFace()const
{
	// Slate keeps asking after the asset is gone: this attribute outlives the object it reads.
	if (!TargetScriptPtr.IsValid())return LOCTEXT("NoFontFace", "(No Valid Face)");
	if (TargetScriptPtr->SubFaces.Num() == 0 || TargetScriptPtr->FontFace >= TargetScriptPtr->SubFaces.Num())
	{
		return LOCTEXT("NoFontFace", "(No Valid Face)");
	}
	return FText::FromString(TargetScriptPtr->SubFaces[TargetScriptPtr->FontFace]);
}

TSharedRef<ITableRow> FDreamUIFontData_FreeTypeRenderCustomization::FontFaceOptions_GenerateComboItem(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable, IDetailLayoutBuilder* DetailBuilder)
{
	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		[
			SNew(SBox)
			.Padding(FMargin(8, 2))
			[
				SNew(STextBlock)
				.Font(DetailBuilder->GetDetailFont())
				.Text(FText::FromString(*InItem))
			]
		];
}

void FDreamUIFontData_FreeTypeRenderCustomization::FontFaceOptions_OnComboChanged(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> InProperty, IDetailLayoutBuilder* DetailBuilder)
{
	int FoundIndex = FontFaceOptions.IndexOfByKey(Item);
	if (FoundIndex != INDEX_NONE)
	{
		InProperty->SetValue(FoundIndex);
		DetailBuilder->ForceRefreshDetails();
	}
}

FText FDreamUIFontData_FreeTypeRenderCustomization::OnGetFontFilePath()const
{
	auto& fileManager = IFileManager::Get();
	// Same as the face name above: the browser row can outlive the asset it was built for.
	if (!TargetScriptPtr.IsValid())return FText::FromString(fileManager.GetFilenameOnDisk(*FPaths::ProjectDir()));
	return FText::FromString(TargetScriptPtr->FontFilePath.IsEmpty() ? fileManager.GetFilenameOnDisk(*FPaths::ProjectDir()) : TargetScriptPtr->FontFilePath);
}

FText FDreamUIFontData_FreeTypeRenderCustomization::GetCurrentValue() const
{
	auto faceName = TargetScriptPtr->SubFaces[TargetScriptPtr->FontFace];
	return FText::FromString(faceName);
}
void FDreamUIFontData_FreeTypeRenderCustomization::OnFontFaceComboSelectionChanged(TSharedPtr<FString> InSelectedItem, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> fontFaceHandle)
{
	int selectedIndex = 0;
	for (int i = 0; i < TargetScriptPtr->SubFaces.Num(); i++)
	{
		if (TargetScriptPtr->SubFaces[i] == *InSelectedItem)
		{
			selectedIndex = i;
		}
	}
	fontFaceHandle->SetValue(selectedIndex);
}

void FDreamUIFontData_FreeTypeRenderCustomization::OnPathTextChanged(const FString& InString, TSharedRef<IPropertyHandle> InPathProperty)
{
	InPathProperty->SetValue(InString);
}
void FDreamUIFontData_FreeTypeRenderCustomization::OnPathTextCommitted(const FString& InString, TSharedRef<IPropertyHandle> InPathProperty, IDetailLayoutBuilder* DetailBuilderPtr)
{
	// The row survives the asset being deleted out from under the open panel.
	if (!TargetScriptPtr.IsValid())return;
	FString pathString = InString;
	if (TargetScriptPtr->bUseRelativeFilePath)
	{
		if (pathString.StartsWith(FPaths::ProjectDir()))//is relative path
		{
			pathString.RemoveFromStart(FPaths::ProjectDir(), ESearchCase::CaseSensitive);
		}
	}
	InPathProperty->SetValue(pathString);
	TargetScriptPtr->ReloadFont();
	DetailBuilderPtr->ForceRefreshDetails();
}
FReply FDreamUIFontData_FreeTypeRenderCustomization::OnReloadButtonClicked(IDetailLayoutBuilder* DetailBuilderPtr)
{
	if (!TargetScriptPtr.IsValid())return FReply::Handled();
	TargetScriptPtr->ReloadFont();
	DetailBuilderPtr->ForceRefreshDetails();
	return FReply::Handled();
}
void FDreamUIFontData_FreeTypeRenderCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilderPtr)
{
	if (DetailBuilderPtr)
	{
		DetailBuilderPtr->ForceRefreshDetails();
	}
}

void FDreamUIFontData_FreeTypeRenderCustomization::AddResolveSampleRow(IDetailCategoryBuilder& Category)
{
	const FText gameLanguageText = LOCTEXT("ResolveSampleGameLanguage", "Game language");
	// The culture menu can outlive this customization: a refresh builds a new one while the old row is still open.
	const TWeakPtr<FDreamUIFontData_FreeTypeRenderCustomization> weakThis = SharedThis(this);
	Category.AddCustomRow(LOCTEXT("ResolveSampleRow", "Resolve Sample"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("ResolveSample", "Resolve Sample"))
		.ToolTipText(LOCTEXT("ResolveSample_Tooltip", "Type some text to see which of this font's faces draws each of its characters, in the language picked beside it. It is the choice a text makes: from the fallbacks' ranges, cultures and preference, and whether a character asks for its emoji form."))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(300.0f)
	.MaxDesiredWidth(800.0f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(FText::FromString(ResolveSampleText))
				.HintText(LOCTEXT("ResolveSample_Hint", "Sample text"))
				.OnTextChanged(this, &FDreamUIFontData_FreeTypeRenderCustomization::OnResolveSampleTextChanged)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				DreamUICulturePicker::MakeComboButton(
					TAttribute<FText>::CreateLambda([gameLanguageText]() { return DreamUICulturePicker::GetCultureDisplayText(ResolveSampleCulture, gameLanguageText); }),
					LOCTEXT("ResolveSampleCulture_Tooltip", "The sample's language, as a text's Language: the fallbacks meant for it are tried first."),
					gameLanguageText,
					[weakThis](const FString& InCultureName)
					{
						if (const TSharedPtr<FDreamUIFontData_FreeTypeRenderCustomization> pinnedThis = weakThis.Pin())
						{
							pinnedThis->OnResolveSampleCulturePicked(InCultureName);
						}
					},
					[]() { return ResolveSampleCulture; })
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SAssignNew(ResolveResultsBox, SVerticalBox)
		]
	];
	RefreshResolveSample();
}

void FDreamUIFontData_FreeTypeRenderCustomization::RefreshResolveSample()
{
	using namespace DreamUIFontResolveSampleLocal;
	if (!ResolveResultsBox.IsValid())return;
	ResolveResultsBox->ClearChildren();
	UDreamUIFontData_FreeTypeRender* font = TargetScriptPtr.Get();
	if (font == nullptr || ResolveSampleText.IsEmpty())return;

	// What a text in this language asks, cluster by cluster, the way the layout segments it.
	const FDreamTextLanguage language = FDreamTextLanguage::Make(ResolveSampleCulture);
	const TSharedRef<IBreakIterator> graphemes = FBreakIterator::CreateCharacterBoundaryIterator();
	graphemes->SetString(ResolveSampleText);
	TArray<uint32> cluster;
	int32 clusterCount = 0;
	for (int32 clusterStart = graphemes->ResetToBeginning(), clusterEnd = graphemes->MoveToNext(); clusterEnd != INDEX_NONE; clusterStart = clusterEnd, clusterEnd = graphemes->MoveToNext())
	{
		const FString clusterText = ResolveSampleText.Mid(clusterStart, clusterEnd - clusterStart);
		DecodeCluster(clusterText, cluster);
		// Tabs and line breaks are not drawn from a face.
		if (cluster.Num() == 0 || cluster[0] < 0x20 || cluster[0] == 0x7F)continue;
		if (clusterCount++ >= MaxResolveSampleRows)continue;

		FDreamFontFaceQuery query;
		query.Cluster = cluster;
		query.Cultures = language.PrioritizedCultureNames;
		query.Presentation = FDreamFontFaceResolver::GetPresentation(cluster);
		const FDreamFontFaceChoice choice = FDreamFontFaceResolver::Resolve(font, query);

		FText faceText;
		if (!choice.bCoversBase)
		{
			faceText = LOCTEXT("ResolveSampleNoFace", "no face has it: face 0's missing-glyph box");
		}
		else
		{
			// Face 0 is this font, 1..N its fallbacks; the owner is the asset whose own face it is.
			const UDreamUIFontData_FreeTypeRender* owner = font->GetFaceOwner(choice.FaceIndex);
			const FText ownerName = owner != nullptr ? FText::FromString(owner->GetName()) : LOCTEXT("ResolveSampleNoFont", "(no font)");
			const FText kindText = choice.bColor ? LOCTEXT("ResolveSampleColour", "colour") : LOCTEXT("ResolveSampleMonochrome", "monochrome");
			faceText = choice.bCoversCluster
				? FText::Format(LOCTEXT("ResolveSampleFace", "face {0} · {1} · {2}"), FText::AsNumber(choice.FaceIndex), ownerName, kindText)
				: FText::Format(LOCTEXT("ResolveSampleFaceBaseOnly", "face {0} · {1} · {2} · its first character only"), FText::AsNumber(choice.FaceIndex), ownerName, kindText);
		}
		const FText presentationText = query.Presentation == EDreamTextPresentation::Emoji
			? LOCTEXT("ResolveSampleEmojiForm", "Asks for its emoji form: the colour faces are tried first while the font prefers colour emoji.")
			: LOCTEXT("ResolveSampleTextForm", "Asks for its text form: the monochrome faces are tried first.");

		ResolveResultsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			.ToolTipText(presentationText)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.MinDesiredWidth(28.0f)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(FText::FromString(clusterText))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::FromString(FormatCodepoints(cluster)))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(choice.bCoversBase ? FSlateColor::UseForeground() : FStyleColors::Warning)
				.Text(faceText)
			]
		];
	}
	if (clusterCount > MaxResolveSampleRows)
	{
		ResolveResultsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(FText::Format(LOCTEXT("ResolveSampleMore", "...and {0} more"), FText::AsNumber(clusterCount - MaxResolveSampleRows)))
		];
	}
}

void FDreamUIFontData_FreeTypeRenderCustomization::OnResolveSampleTextChanged(const FText& InText)
{
	ResolveSampleText = InText.ToString();
	RefreshResolveSample();
}

void FDreamUIFontData_FreeTypeRenderCustomization::OnResolveSampleCulturePicked(const FString& InCultureName)
{
	ResolveSampleCulture = InCultureName;
	RefreshResolveSample();
}
#undef LOCTEXT_NAMESPACE