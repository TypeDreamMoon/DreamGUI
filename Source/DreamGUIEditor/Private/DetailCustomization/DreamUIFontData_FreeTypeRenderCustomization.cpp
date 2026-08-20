// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamUIFontData_FreeTypeRenderCustomization.h"
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

#define LOCTEXT_NAMESPACE "DreamGUIFreeTypeRenderFontDataCustomization"

// The checkbox below is the one row of this panel that is written from a lambda instead of by a
// property row, so the write lives here where a test can reach it: writing the UPROPERTY directly
// leaves the package clean, and the toggle is then gone the next time the asset is loaded.
// DreamSmallCustomizationAutomationTests.cpp declares both of these again - this customization has no
// shared header to hang them on.
namespace DreamUIFontDataCustomization
{
	ECheckBoxState GetUseRelativeFilePathState(TSharedPtr<IPropertyHandle> InHandle)
	{
		bool bValue = false;
		// A mixed selection has no single answer, and the checkbox has a state for exactly that.
		if (!InHandle.IsValid() || InHandle->GetValue(bValue) != FPropertyAccess::Success)return ECheckBoxState::Undetermined;
		return bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	bool SetUseRelativeFilePath(TSharedPtr<IPropertyHandle> InHandle, bool bInUseRelativeFilePath)
	{
		if (!InHandle.IsValid())return false;
		return InHandle->SetValue(bInUseRelativeFilePath) == FPropertyAccess::Success;
	}
}

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
	fontTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateRaw(this, &FDreamUIFontData_FreeTypeRenderCustomization::ForceRefresh, &DetailBuilder));
	uint8 fontTypeUint8;
	fontTypeHandle->GetValue(fontTypeUint8);
	auto fontType = (EDreamUIDynamicFontDataType)fontTypeUint8;

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
#undef LOCTEXT_NAMESPACE