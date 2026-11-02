// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamUIStaticSpriteAtlasDataCustomization.h"
#include "Core/DreamUIStaticSpriteAtlasData.h"
#include "Utils/DreamUIUtils.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"

#define LOCTEXT_NAMESPACE "DreamGUIStaticSpriteAtlasDataCustomization"

TSharedRef<IDetailCustomization> FDreamUIStaticSpriteAtlasDataCustomization::MakeInstance()
{
	return MakeShareable(new FDreamUIStaticSpriteAtlasDataCustomization);
}

void FDreamUIStaticSpriteAtlasDataCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	// An empty list is a real state -- the panel rebuilds while a selection is being cleared -- and
	// indexing [0] there reads off the end of an empty array.
	TargetScriptPtr = targetObjects.Num() > 0 ? Cast<UDreamUIStaticSpriteAtlasData>(targetObjects[0].Get()) : nullptr;
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	// The layout builder is thrown away by the very refresh these delegates and the button ask for, so
	// none of them may hold it; the utilities outlive the layout and are how a row asks for a refresh.
	const TWeakPtr<IPropertyUtilities> WeakUtilities = DetailBuilder.GetPropertyUtilities();
	IDetailCategoryBuilder& DreamGUICategory = DetailBuilder.EditCategory("DreamGUI");
	auto spriteArrayHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUIStaticSpriteAtlasData, SpriteDataArray));
	spriteArrayHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakUtilities] {
		// Requested, not forced: this runs inside the property's own value-changed broadcast.
		if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
		{
			Utilities->RequestForceRefresh();
		}
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
				.OnClicked_Lambda([WeakTarget = TargetScriptPtr, WeakUtilities] {
					// The button outlives the asset it was built for: the row is still clickable after a
					// reimport or a GC has taken the atlas out from under the panel. It outlives this
					// customization too, so it holds the target itself and not `this`.
					UDreamUIStaticSpriteAtlasData* Target = WeakTarget.Get();
					if (Target == nullptr)return FReply::Handled();
					Target->CleanupInvalidSpriteData();
					if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
					{
						Utilities->RequestForceRefresh();
					}
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
			.OnClicked_Lambda([WeakTarget = TargetScriptPtr] {
				UDreamUIStaticSpriteAtlasData* Target = WeakTarget.Get();
				if (Target == nullptr)return FReply::Handled();
				Target->MarkNotInitialized();
				Target->MarkAtlasPackDirty();
				Target->InitCheck();
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