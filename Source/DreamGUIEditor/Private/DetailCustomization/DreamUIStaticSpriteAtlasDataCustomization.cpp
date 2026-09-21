// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamUIStaticSpriteAtlasDataCustomization.h"
#include "Core/DreamUIStaticSpriteAtlasData.h"
#include "Utils/DreamUIUtils.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"

#define LOCTEXT_NAMESPACE "DreamGUIStaticSpriteAtlasDataCustomization"

TSharedRef<IDetailCustomization> FDreamUIStaticSpriteAtlasDataCustomization::MakeInstance()
{
	return MakeShareable(new FDreamUIStaticSpriteAtlasDataCustomization);
}

void FDreamUIStaticSpriteAtlasDataCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<UDreamUIStaticSpriteAtlasData>(targetObjects[0].Get());
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	IDetailCategoryBuilder& DreamGUICategory = DetailBuilder.EditCategory("DreamGUI");
	auto spriteArrayHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUIStaticSpriteAtlasData, SpriteDataArray));
	spriteArrayHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&DetailBuilder] {
		DetailBuilder.ForceRefreshDetails();
		}));
	DreamGUICategory.AddProperty(spriteArrayHandle);

	//check spriteData's packingAtlas
	if (TargetScriptPtr->CheckInvalidSpriteData())
	{
		auto ErrMsg = LOCTEXT("CheckSpriteDataError", "Some spriteData in spriteArray is not valid! Click \"Cleanup\" button to clear invalid spriteData.");
		UE_LOG(DreamGUIEditor, Error, TEXT("%s"), *ErrMsg.ToString());
		FDreamUIUtils::EditorNotification(ErrMsg, false, 10.0f);
		DreamGUICategory.AddCustomRow(LOCTEXT("Error_Row", "Error"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(ErrMsg)
				.ColorAndOpacity(FLinearColor(FColor::Red))
				.AutoWrapText(true)
			]
			.ValueContent()
			[
				SNew(SButton)
				.Text(LOCTEXT("CleanupButtonText", "Cleanup"))
				.HAlign(EHorizontalAlignment::HAlign_Center)
				.OnClicked_Lambda([this, &DetailBuilder] {
					// The button outlives the asset it was built for: the row is still clickable after a
					// reimport or a GC has taken the atlas out from under the panel.
					if (!TargetScriptPtr.IsValid())return FReply::Handled();
					TargetScriptPtr->CleanupInvalidSpriteData();
					DetailBuilder.ForceRefreshDetails();
					return FReply::Handled();
					})
			]
		;
	}

	DreamGUICategory.AddCustomRow(LOCTEXT("PackAtlasButtonRow", "Pack Atlas"))
		.ValueContent()
		[
			SNew(SButton)
			.Text(LOCTEXT("PackAtlasButton", "Pack Atlas"))
			.HAlign(EHorizontalAlignment::HAlign_Center)
			.OnClicked_Lambda([this] {
				if (!TargetScriptPtr.IsValid())return FReply::Handled();
				TargetScriptPtr->MarkNotInitialized();
				TargetScriptPtr->MarkAtlasPackDirty();
				TargetScriptPtr->InitCheck();
				return FReply::Handled();
			})
		];

	auto TextureMipDataHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUIStaticSpriteAtlasData, TexturePixelData));
	auto TextureMipDataBufferSize = TargetScriptPtr->TexturePixelData.Num();
	auto TextureMipDataBufferSize_kb = (double)TextureMipDataBufferSize / 1024;
	auto TextureMipDataBufferSize_mb = TextureMipDataBufferSize_kb / 1024;
	FString DisplyTextureMipDataBufferSize;
	if (TextureMipDataBufferSize_mb >= 1)
	{
		DisplyTextureMipDataBufferSize = FString::Printf(TEXT("%d.%d mb")
			, FMath::FloorToInt(TextureMipDataBufferSize_mb)
			, FMath::RoundToInt(FMath::Fractional(TextureMipDataBufferSize_mb) * 100)
		);
	}
	else if (TextureMipDataBufferSize_kb >= 1)
	{
		DisplyTextureMipDataBufferSize = FString::Printf(TEXT("%d.%d kb")
			, FMath::FloorToInt(TextureMipDataBufferSize_kb)
			, FMath::RoundToInt(FMath::Fractional(TextureMipDataBufferSize_kb) * 100)
		);
	}
	else
	{
		DisplyTextureMipDataBufferSize = FString::Printf(TEXT("%d"), TextureMipDataBufferSize);
	}
	DreamGUICategory.AddCustomRow(LOCTEXT("PackedDataSize_Row", "Packed Data Size"), true)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PackedDataSize_Name", "Packed Data Size"))
			.Font(DetailBuilder.GetDetailFont())
			.ToolTipText(LOCTEXT("PackedDataSize_Tooltip", "Store texture mip data, so we can recreate atlas texture with this data."))
		]
		.ValueContent()
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("%s"), *DisplyTextureMipDataBufferSize)))
			.Font(DetailBuilder.GetDetailFont())
		];
}

#undef LOCTEXT_NAMESPACE