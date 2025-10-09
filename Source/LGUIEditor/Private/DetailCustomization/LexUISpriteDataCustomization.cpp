// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexUISpriteDataCustomization.h"
#include "DesktopPlatformModule.h"
#include "Core/LexUISettings.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Core/LexUIDynamicSpriteAtlasData.h"
#include "Core/LexUIStaticSpriteAtlasData.h"
#include "Core/LexUIManager.h"
#include "Sound/SoundCue.h"
#include "Core/LexUISpriteData.h"

#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "LGUIEditorTools.h"

#define LOCTEXT_NAMESPACE "LGUISpriteDataCustomization"

TSharedRef<IDetailCustomization> FLexUISpriteDataCustomization::MakeInstance()
{
	return MakeShareable(new FLexUISpriteDataCustomization);
}

void FLexUISpriteDataCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<ULexUISpriteData>(targetObjects[0].Get());
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo));
	IDetailCategoryBuilder& lguiCategory = DetailBuilder.EditCategory("LGUI");
	lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteTexture));
	lguiCategory.AddCustomRow(LOCTEXT("ReloadTexture_Row", "ReloadTexture"))
		.ValueContent()
		[
			SNew(SButton)
			.Text(LOCTEXT("ReloadTexture_Button", "ReloadTexture"))
			.HAlign(EHorizontalAlignment::HAlign_Center)
			.OnClicked_Lambda([this]{
				TargetScriptPtr->ReloadTexture();
				TargetScriptPtr->MarkPackageDirty();
				ULexUIManagerWorldSubsystem::RefreshAllUI();
				return FReply::Handled();
			})
		];
	lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.Width));
	lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.Height));
	lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.MinUV));
	lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.MaxUV));
	lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.BorderMinUV));
	lguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.BorderMaxUV));
	IDetailCategoryBuilder& atlasPackingCategory = DetailBuilder.EditCategory("AtlasPacking");
	auto PackingAtlasProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, packingAtlas));
	atlasPackingCategory.AddProperty(PackingAtlasProperty);
	PackingAtlasProperty->SetOnPropertyValuePreChange(FSimpleDelegate::CreateLambda([this] {
		if (IsValid(TargetScriptPtr->GetPackingAtlas()))
		{
			TargetScriptPtr->GetPackingAtlas()->RemoveSpriteData(TargetScriptPtr.Get());
		}
		}));
	PackingAtlasProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([this, &DetailBuilder] {
		if (IsValid(TargetScriptPtr->GetPackingAtlas()))
		{
			TargetScriptPtr->GetPackingAtlas()->AddSpriteData(TargetScriptPtr.Get());
		}
		CheckSprite();
		TargetScriptPtr->InitSpriteData();
		DetailBuilder.ForceRefreshDetails();
		}));
	auto PackingTagProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, PackingTag));
	DetailBuilder.HideProperty(PackingTagProperty);
	
	RefreshNameList(nullptr);
	if (ULexUIDynamicSpriteAtlasManager::Instance != nullptr)
	{
		ULexUIDynamicSpriteAtlasManager::Instance->OnAtlasMapChanged.AddSP(this, &FLexUISpriteDataCustomization::RefreshNameList, &DetailBuilder);
	}
	atlasPackingCategory.AddCustomRow(LOCTEXT("PackingTag", "Packing Tag"))
	.NameContent()
	[
		PackingTagProperty->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SBox)
		.IsEnabled_Lambda([this] {
			return !IsValid(TargetScriptPtr->GetPackingAtlas());
			})
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.MinDesiredWidth(120)
				.Padding(FMargin(0, 2, 0, 2))
				[
					//PackingTagProperty->CreatePropertyValueWidget()
					SNew(SComboButton)
					.HasDownArrow(true)
					.ButtonContent()
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						[
							SNew(SEditableText)
							.OnTextCommitted(this, &FLexUISpriteDataCustomization::OnPackingTagTextCommited, PackingTagProperty, &DetailBuilder)
							.Text(this, &FLexUISpriteDataCustomization::GetPackingTagText, PackingTagProperty)
						]
					]
					.MenuContent()
					[
						SNew(SVerticalBox)
						+SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SListView<TSharedPtr<FName>>)
							.ListItemsSource(&NameList)
							.OnGenerateRow(this, &FLexUISpriteDataCustomization::GenerateComboItem)
							.OnSelectionChanged(this, &FLexUISpriteDataCustomization::HandleRequiredParamComboChanged, PackingTagProperty, &DetailBuilder)
						]
					]
				]
			]
			+SHorizontalBox::Slot()
			.Padding(FMargin(2))
			.AutoWidth()
			[
				SNew(SButton)
				.VAlign(EVerticalAlignment::VAlign_Center)
				.Text(LOCTEXT("OpenAtals", "Open Atals Viewer"))
				.OnClicked_Lambda([]() {
				LGUIEditorTools::OpenAtlasViewer_Impl();
				return FReply::Handled();
					})
			]
		]
	]
	;
	atlasPackingCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, bUseEdgePixelPadding));

	//if change PackingTag, clear all sprites and repack
	auto packingTagHangle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, PackingTag));
	packingTagHangle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([] {ULexUISpriteData::MarkAllSpritesNeedToReinitialize(); }));
	//if change SpriteTexture, clear all sprites and repack
	auto spriteTextureHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteTexture));
	spriteTextureHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&DetailBuilder] {ULexUISpriteData::MarkAllSpritesNeedToReinitialize(); DetailBuilder.ForceRefreshDetails(); }));
	UObject* spriteTextureObject = nullptr;
	spriteTextureHandle->GetValue(spriteTextureObject);
	if (spriteTextureObject)
	{
		int32 atlasPadding = ULexUISettings::GetAtlasTexturePadding(TargetScriptPtr->PackingTag);
		if (TargetScriptPtr->SpriteTexture->GetSurfaceWidth() + atlasPadding * 2 > WARNING_ATLAS_SIZE || TargetScriptPtr->SpriteTexture->GetSurfaceHeight() + atlasPadding * 2 > WARNING_ATLAS_SIZE)
		{
			UE_LOG(LGUIEditor, Error, TEXT("Target texture width or height is too large! Consider use UITexture to render this texture."));
			FNotificationInfo Info(LOCTEXT("TextureSizeError", "Target texture width or height is too large! Consider use UITexture to render this texture."));
			Info.Image = FAppStyle::GetBrush(TEXT("LevelEditor.RecompileGameCode"));
			Info.FadeInDuration = 0.1f;
			Info.FadeOutDuration = 0.5f;
			Info.ExpireDuration = 5.0f;
			Info.bUseThrobber = false;
			Info.bUseSuccessFailIcons = true;
			Info.bUseLargeFont = true;
			Info.bFireAndForget = false;
			Info.bAllowThrottleWhenFrameRateIsLow = false;
			auto NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
			NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
			NotificationItem->ExpireAndFadeout();

			auto CompileFailSound = LoadObject<USoundBase>(NULL, TEXT("/Engine/EditorSounds/Notifications/CompileFailed_Cue.CompileFailed_Cue"));
			GEditor->PlayEditorSound(CompileFailSound);
			spriteTextureObject = nullptr;
			spriteTextureHandle->SetValue(spriteTextureObject);
			TargetScriptPtr->bIsInitialized = false;
		}
	}

	if (spriteTextureObject)
	{
		IDetailCategoryBuilder& paddingCategory = DetailBuilder.EditCategory("Padding");
		paddingCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.Padding.Left));
		paddingCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.Padding.Right));
		paddingCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.Padding.Top));
		paddingCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.Padding.Bottom));

		IDetailCategoryBuilder& borderEditorCategory = DetailBuilder.EditCategory("BorderEditor");
		spriteSlateBrush = TSharedPtr<FSlateBrush>(new FSlateBrush);
		spriteSlateBrush->SetResourceObject(TargetScriptPtr->SpriteTexture);
		borderEditorCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.Border.Left));
		borderEditorCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.Border.Right));
		borderEditorCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.Border.Top));
		borderEditorCategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexUISpriteData, SpriteInfo.Border.Bottom));
		borderEditorCategory.AddCustomRow(LOCTEXT("BorderEditor", "BorderEditor"))
		.WholeRowContent()
		[
			SNew(SBorder)
			[
				SAssignNew(ImageBox, SBox)
				//.MinDesiredHeight(this, &FLGUISpriteDataCustomization::GetMinDesiredHeight, &DetailBuilder)
				.HeightOverride(this, &FLexUISpriteDataCustomization::GetMinDesiredHeight, &DetailBuilder)
				//.MinDesiredHeight(4096)
				//.MinDesiredWidth(4096)
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					.AutoHeight()
					.VAlign(EVerticalAlignment::VAlign_Center)
					.HAlign(EHorizontalAlignment::HAlign_Center)
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(EHorizontalAlignment::HAlign_Center)
						.VAlign(EVerticalAlignment::VAlign_Center)
						[
							SNew(SOverlay)
							//image background
							+SOverlay::Slot()
							[
								SNew(SImage)
								.Image(FAppStyle::GetBrush("Checkerboard"))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.15f, 0.15f, 0.15f)))
							]
							//image display
							+SOverlay::Slot()
							[
								SNew(SBox)
								.WidthOverride(this, &FLexUISpriteDataCustomization::GetImageWidth)
								.HeightOverride(this, &FLexUISpriteDataCustomization::GetImageHeight)
								[
									SNew(SImage)
									.Image(spriteSlateBrush.Get())
								]
							]
							//left splitter
							+SOverlay::Slot()
							[
								SNew(SHorizontalBox)
								+SHorizontalBox::Slot()
								.HAlign(EHorizontalAlignment::HAlign_Left)
								[
									SNew(SBox)
									.WidthOverride(this, &FLexUISpriteDataCustomization::GetBorderLeftSize)
									[
										SNew(SBox)
										.HAlign(EHorizontalAlignment::HAlign_Right)
										.WidthOverride(1)
										[
											SNew(SImage)
											.Image(FAppStyle::GetBrush("PropertyEditor.VerticalDottedLine"))
										]
									]
								]
							]
							//right splitter
							+SOverlay::Slot()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.HAlign(EHorizontalAlignment::HAlign_Right)
								[
									SNew(SBox)
									.WidthOverride(this, &FLexUISpriteDataCustomization::GetBorderRightSize)
									[
										SNew(SBox)
										.HAlign(EHorizontalAlignment::HAlign_Left)
										.WidthOverride(1)
										[
											SNew(SImage)
											.Image(FAppStyle::GetBrush("PropertyEditor.VerticalDottedLine"))
										]
									]
								]
							]
							//top splitter
							+SOverlay::Slot()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.VAlign(EVerticalAlignment::VAlign_Top)
								[
									SNew(SBox)
									.HeightOverride(this, &FLexUISpriteDataCustomization::GetBorderTopSize)
									[
										SNew(SBox)
										.VAlign(EVerticalAlignment::VAlign_Bottom)
										.HeightOverride(1)
										[
											SNew(SImage)
											.Image(FAppStyle::GetBrush("PropertyEditor.HorizontalDottedLine"))
										]
									]
								]
							]
							//bottom splitter
							+SOverlay::Slot()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.VAlign(EVerticalAlignment::VAlign_Bottom)
								[
									SNew(SBox)
									.HeightOverride(this, &FLexUISpriteDataCustomization::GetBorderBottomSize)
									[
										SNew(SBox)
										.VAlign(EVerticalAlignment::VAlign_Top)
										.HeightOverride(1)
										[
											SNew(SImage)
											.Image(FAppStyle::GetBrush("PropertyEditor.HorizontalDottedLine"))
										]
									]
								]
							]
						]
					]
				]
			]
		];
	}
}

void FLexUISpriteDataCustomization::CheckSprite()
{
	//check invalid RenderSprite in atlas
	if (IsValid(TargetScriptPtr->packingAtlas))
	{
		TargetScriptPtr->packingAtlas->CheckSprite();
	}
	if (auto spriteAtlasData = ULexUIDynamicSpriteAtlasManager::Find(TargetScriptPtr->PackingTag))
	{
		spriteAtlasData->CheckSprite(TargetScriptPtr->PackingTag);
	}
}

void FLexUISpriteDataCustomization::RefreshNameList(IDetailLayoutBuilder* DetailBuilder)
{
	NameList.Reset();
	NameList.Add(MakeShareable(new FName(NAME_None)));
	if (ULexUIDynamicSpriteAtlasManager::Instance != nullptr)
	{
		auto& AtlasMap = ULexUIDynamicSpriteAtlasManager::Instance->GetAtlasMap();
		for (auto KeyValue : AtlasMap)
		{
			NameList.Add(TSharedPtr<FName>(new FName(KeyValue.Key)));
		}
	}
	if (DetailBuilder != nullptr)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}

void FLexUISpriteDataCustomization::OnPackingTagTextCommited(const FText& InText, ETextCommit::Type CommitType, TSharedRef<IPropertyHandle> InProperty, IDetailLayoutBuilder* DetailBuilder)
{
	FName packingTag = FName(InText.ToString());
	InProperty->SetValue(packingTag);
	TargetScriptPtr->ReloadTexture();
	TargetScriptPtr->InitSpriteData();
	DetailBuilder->ForceRefreshDetails();

	CheckSprite();
}

TSharedRef<ITableRow> FLexUISpriteDataCustomization::GenerateComboItem(TSharedPtr<FName> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FName>>, OwnerTable)
		[
			SNew(STextBlock).Text(FText::FromName(*InItem))
		];
}

void FLexUISpriteDataCustomization::HandleRequiredParamComboChanged(TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> InProperty, IDetailLayoutBuilder* DetailBuilder)
{
	InProperty->SetValue(*Item.Get());
	TargetScriptPtr->ReloadTexture();
	TargetScriptPtr->InitSpriteData();
	DetailBuilder->ForceRefreshDetails();

	CheckSprite();
}

FText FLexUISpriteDataCustomization::GetPackingTagText(TSharedRef<IPropertyHandle> InProperty)const
{
	FName packingTag;
	InProperty->GetValue(packingTag);
	return FText::FromName(packingTag);
}

FOptionalSize FLexUISpriteDataCustomization::GetImageWidth()const
{
	float imageAspect = (float)(TargetScriptPtr->SpriteTexture->GetSurfaceWidth()) / TargetScriptPtr->SpriteTexture->GetSurfaceHeight();
	auto imageBoxSize = ImageBox->GetCachedGeometry().GetLocalSize();
	float imageBoxAspect = (float)(imageBoxSize.X / imageBoxSize.Y);
	if (imageAspect > imageBoxAspect)
	{
		return imageBoxSize.X;
	}
	else
	{
		return imageBoxSize.Y * imageAspect;
	}
}
FOptionalSize FLexUISpriteDataCustomization::GetImageHeight()const
{
	float imageAspect = (float)(TargetScriptPtr->SpriteTexture->GetSurfaceWidth()) / TargetScriptPtr->SpriteTexture->GetSurfaceHeight();
	auto imageBoxSize = ImageBox->GetCachedGeometry().GetLocalSize();
	float imageBoxAspect = (float)(imageBoxSize.X / imageBoxSize.Y);
	if (imageAspect > imageBoxAspect)
	{
		return imageBoxSize.X / imageAspect;
	}
	else
	{
		return imageBoxSize.Y;
	}
}
FOptionalSize FLexUISpriteDataCustomization::GetMinDesiredHeight(IDetailLayoutBuilder* DetailBuilder)const
{
	return DetailBuilder->GetDetailsView()->GetCachedGeometry().GetLocalSize().Y - 400;
}
FOptionalSize FLexUISpriteDataCustomization::GetBorderLeftSize()const
{
	if (TargetScriptPtr.Get() == nullptr)return 0;
	float imageAspect = (float)(TargetScriptPtr->SpriteTexture->GetSurfaceWidth()) / TargetScriptPtr->SpriteTexture->GetSurfaceHeight();
	auto imageBoxSize = ImageBox->GetCachedGeometry().GetLocalSize();
	float imageBoxAspect = (float)(imageBoxSize.X / imageBoxSize.Y);
	if (imageAspect > imageBoxAspect)
	{
		return TargetScriptPtr->SpriteInfo.Border.Left * imageBoxSize.X / TargetScriptPtr->SpriteTexture->GetSurfaceWidth();
	}
	else
	{
		return TargetScriptPtr->SpriteInfo.Border.Left * imageBoxSize.Y / TargetScriptPtr->SpriteTexture->GetSurfaceHeight();
	}
}
FOptionalSize FLexUISpriteDataCustomization::GetBorderRightSize()const
{
	if (TargetScriptPtr.Get() == nullptr)return 0;
	float imageAspect = (float)(TargetScriptPtr->SpriteTexture->GetSurfaceWidth()) / TargetScriptPtr->SpriteTexture->GetSurfaceHeight();
	auto imageBoxSize = ImageBox->GetCachedGeometry().GetLocalSize();
	float imageBoxAspect = (float)(imageBoxSize.X / imageBoxSize.Y);
	if (imageAspect > imageBoxAspect)
	{
		return TargetScriptPtr->SpriteInfo.Border.Right * imageBoxSize.X / TargetScriptPtr->SpriteTexture->GetSurfaceWidth();
	}
	else
	{
		return TargetScriptPtr->SpriteInfo.Border.Right * imageBoxSize.Y / TargetScriptPtr->SpriteTexture->GetSurfaceHeight();
	}
}
FOptionalSize FLexUISpriteDataCustomization::GetBorderTopSize()const
{
	if (TargetScriptPtr.Get() == nullptr)return 0;
	float imageAspect = (float)(TargetScriptPtr->SpriteTexture->GetSurfaceWidth()) / TargetScriptPtr->SpriteTexture->GetSurfaceHeight();
	auto imageBoxSize = ImageBox->GetCachedGeometry().GetLocalSize();
	float imageBoxAspect = (float)(imageBoxSize.X / imageBoxSize.Y);
	if (imageAspect > imageBoxAspect)
	{
		return TargetScriptPtr->SpriteInfo.Border.Top * imageBoxSize.X / TargetScriptPtr->SpriteTexture->GetSurfaceWidth();
	}
	else
	{
		return TargetScriptPtr->SpriteInfo.Border.Top * imageBoxSize.Y / TargetScriptPtr->SpriteTexture->GetSurfaceHeight();
	}
}
FOptionalSize FLexUISpriteDataCustomization::GetBorderBottomSize()const
{
	if (TargetScriptPtr.Get() == nullptr)return 0;
	float imageAspect = (float)(TargetScriptPtr->SpriteTexture->GetSurfaceWidth()) / TargetScriptPtr->SpriteTexture->GetSurfaceHeight();
	auto imageBoxSize = ImageBox->GetCachedGeometry().GetLocalSize();
	float imageBoxAspect = (float)(imageBoxSize.X / imageBoxSize.Y);
	if (imageAspect > imageBoxAspect)
	{
		return TargetScriptPtr->SpriteInfo.Border.Bottom * imageBoxSize.X / TargetScriptPtr->SpriteTexture->GetSurfaceWidth();
	}
	else
	{
		return TargetScriptPtr->SpriteInfo.Border.Bottom * imageBoxSize.Y / TargetScriptPtr->SpriteTexture->GetSurfaceHeight();
	}
}

#undef LOCTEXT_NAMESPACE