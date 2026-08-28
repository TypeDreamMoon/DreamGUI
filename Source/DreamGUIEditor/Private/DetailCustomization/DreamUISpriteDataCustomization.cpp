// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamUISpriteDataCustomization.h"
#include "DreamDetailsMultiSelect.h"
#include "Core/DreamUISettings.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Core/DreamUIDynamicSpriteAtlasData.h"
#include "Core/DreamUIStaticSpriteAtlasData.h"
#include "Core/DreamUIManager.h"
#include "Sound/SoundCue.h"
#include "Core/DreamUISpriteData.h"

#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"

#define LOCTEXT_NAMESPACE "DreamGUISpriteDataCustomization"

TSharedRef<IDetailCustomization> FDreamUISpriteDataCustomization::MakeInstance()
{
	return MakeShareable(new FDreamUISpriteDataCustomization);
}

void FDreamUISpriteDataCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<UDreamUISpriteData>(targetObjects[0].Get());
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo));
	IDetailCategoryBuilder& dreamguiCategory = DetailBuilder.EditCategory("DreamGUI");
	dreamguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteTexture));
	dreamguiCategory.AddCustomRow(LOCTEXT("ReloadTexture_Row", "ReloadTexture"))
		.ValueContent()
		[
			SNew(SButton)
			.Text(LOCTEXT("ReloadTexture_Button", "ReloadTexture"))
			.HAlign(EHorizontalAlignment::HAlign_Center)
			.OnClicked_Lambda([this]{
				TargetScriptPtr->ReloadTexture();
				TargetScriptPtr->MarkPackageDirty();
				UDreamUIManagerWorldSubsystem::RefreshAllUI();
				return FReply::Handled();
			})
		];
	dreamguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.Width));
	dreamguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.Height));
	dreamguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.MinUV));
	dreamguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.MaxUV));
	dreamguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.BorderMinUV));
	dreamguiCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.BorderMaxUV));

	TArray<FName> PropertiesNeedToHide;
	IDetailCategoryBuilder& AtlasPackingCategory = DetailBuilder.EditCategory("AtlasPacking");
	auto PackingType_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, PackingType));
	const auto PackingType = (EDreamUISpritePackingType)DreamDetailsMultiSelect::ValueOr<uint8>(PackingType_PH, 0);
	PackingType_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&DetailBuilder]()
	{
		DetailBuilder.ForceRefreshDetails();
	}));
	AtlasPackingCategory.AddProperty(PackingType_PH);

	switch (PackingType)
	{
	case EDreamUISpritePackingType::Static:
		break;
	case EDreamUISpritePackingType::Dynamic:
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, PackingAtlas));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, AtlasTextureIndex));
		break;
	}
	auto PackingAtlas_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, PackingAtlas));
	PackingAtlas_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([this, &DetailBuilder] {
		DetailBuilder.ForceRefreshDetails();
		}));
	RefreshNameList(nullptr);
	if (UDreamUIDynamicSpriteAtlasManager::Instance != nullptr)
	{
		UDreamUIDynamicSpriteAtlasManager::Instance->OnAtlasMapChanged.AddSP(this, &FDreamUISpriteDataCustomization::RefreshNameList, &DetailBuilder);
	}
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, PackingTag));
	auto PackingTag_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, PackingTag));
	if (PackingType == EDreamUISpritePackingType::Dynamic)
	{
		AtlasPackingCategory.AddCustomRow(LOCTEXT("PackingTag", "Packing Tag"))
		.NameContent()
		[
			PackingTag_PH->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SBox)
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
								.OnTextCommitted(this, &FDreamUISpriteDataCustomization::OnPackingTagTextCommited, PackingTag_PH, &DetailBuilder)
								.Text(this, &FDreamUISpriteDataCustomization::GetPackingTagText, PackingTag_PH)
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
								.OnGenerateRow(this, &FDreamUISpriteDataCustomization::GenerateComboItem)
								.OnSelectionChanged(this, &FDreamUISpriteDataCustomization::HandleRequiredParamComboChanged, PackingTag_PH, &DetailBuilder)
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
					.Text(LOCTEXT("OpenAtlas", "Open Atlas Viewer"))
					.OnClicked_Lambda([]() {
						FGlobalTabmanager::Get()->TryInvokeTab(FDreamGUIEditorModule::DreamUIDynamicSpriteAtlasViewerTabName);
						return FReply::Handled();
					})
				]
			]
		]
		;
	}
	
	//if change PackingTag, clear all sprites and repack
	PackingTag_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([] {UDreamUISpriteData::MarkAllSpritesNeedToReinitialize(); }));
	//if change SpriteTexture, clear all sprites and repack
	auto spriteTextureHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteTexture));
	spriteTextureHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&DetailBuilder] {UDreamUISpriteData::MarkAllSpritesNeedToReinitialize(); DetailBuilder.ForceRefreshDetails(); }));
	UObject* spriteTextureObject = nullptr;
	spriteTextureHandle->GetValue(spriteTextureObject);
	if (spriteTextureObject)
	{
		int32 atlasPadding = UDreamUISettings::GetAtlasTexturePadding(TargetScriptPtr->PackingTag);
		if (TargetScriptPtr->SpriteTexture->GetSurfaceWidth() + atlasPadding * 2 > WARNING_ATLAS_SIZE || TargetScriptPtr->SpriteTexture->GetSurfaceHeight() + atlasPadding * 2 > WARNING_ATLAS_SIZE)
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("Target texture width or height is too large! Consider use UITexture to render this texture."));
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
		paddingCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.Padding.Left));
		paddingCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.Padding.Right));
		paddingCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.Padding.Top));
		paddingCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.Padding.Bottom));

		IDetailCategoryBuilder& borderEditorCategory = DetailBuilder.EditCategory("Border");
		spriteSlateBrush = TSharedPtr<FSlateBrush>(new FSlateBrush);
		spriteSlateBrush->SetResourceObject(TargetScriptPtr->SpriteTexture);
		auto BorderLeftPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.Border.Left));
		auto BorderRightPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.Border.Right));
		auto BorderTopPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.Border.Top));
		auto BorderBottomPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.Border.Bottom));
		borderEditorCategory.AddProperty(BorderLeftPropertyHandle);
		borderEditorCategory.AddProperty(BorderRightPropertyHandle);
		borderEditorCategory.AddProperty(BorderTopPropertyHandle);
		borderEditorCategory.AddProperty(BorderBottomPropertyHandle);
		auto BorderDirtyPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamUISpriteData, SpriteInfo.bIsBorderDirty));
		borderEditorCategory.AddCustomRow(LOCTEXT("BorderEditorTitleRow", "BorderEditor"))
		.NameContent()
		[
			SNew(SBox)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BorderEditorTitle", "Border Editor"))
			]
		]
		.ValueContent()
		[
			SNew(SBox)
			.VAlign(VAlign_Center)
			.ToolTipText(LOCTEXT("ApplyTooltip", "Click this button after change these values, or it will not take effect."))
			[
				SNew(SButton)
				.Text(LOCTEXT("Apply", "Apply"))
				.IsEnabled(this, &FDreamUISpriteDataCustomization::IsApplyButtonEnabled, BorderDirtyPropertyHandle.ToSharedPtr())
				.OnClicked_Lambda([=, this]()
				{
					TargetScriptPtr->SpriteInfo.bIsBorderDirty = false;

					TargetScriptPtr->ReloadTexture();
					TargetScriptPtr->InitSpriteData();
					TargetScriptPtr->MarkPackageDirty();
					UDreamUIManagerWorldSubsystem::RefreshAllUI();
					return FReply::Handled();
				})
			]
		]
		;
		borderEditorCategory.AddCustomRow(LOCTEXT("BorderEditor", "BorderEditor"))
		.WholeRowContent()
		[
			SNew(SBorder)
			[
				SAssignNew(ImageBox, SBox)
				//.MinDesiredHeight(this, &FDreamGUISpriteDataCustomization::GetMinDesiredHeight, &DetailBuilder)
				.HeightOverride(this, &FDreamUISpriteDataCustomization::GetMinDesiredHeight, &DetailBuilder)
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
								.WidthOverride(this, &FDreamUISpriteDataCustomization::GetImageWidth)
								.HeightOverride(this, &FDreamUISpriteDataCustomization::GetImageHeight)
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
									.WidthOverride(this, &FDreamUISpriteDataCustomization::GetBorderLeftSize)
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
									.WidthOverride(this, &FDreamUISpriteDataCustomization::GetBorderRightSize)
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
									.HeightOverride(this, &FDreamUISpriteDataCustomization::GetBorderTopSize)
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
									.HeightOverride(this, &FDreamUISpriteDataCustomization::GetBorderBottomSize)
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

void FDreamUISpriteDataCustomization::CheckSprite()
{
	//check invalid RenderSprite in atlas
	if (IsValid(TargetScriptPtr->PackingAtlas))
	{
		TargetScriptPtr->PackingAtlas->CleanupInvalidSpriteData();
	}
	if (auto DynamicSpriteAtlasData = UDreamUIDynamicSpriteAtlasManager::Find(TargetScriptPtr->PackingTag))
	{
		DynamicSpriteAtlasData->CheckSprite();
	}
}

void FDreamUISpriteDataCustomization::RefreshNameList(IDetailLayoutBuilder* DetailBuilder)
{
	NameList.Reset();
	NameList.Add(MakeShareable(new FName(NAME_None)));
	if (UDreamUIDynamicSpriteAtlasManager::Instance != nullptr)
	{
		auto& AtlasMap = UDreamUIDynamicSpriteAtlasManager::Instance->GetAtlasMap();
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

void FDreamUISpriteDataCustomization::OnPackingTagTextCommited(const FText& InText, ETextCommit::Type CommitType, TSharedRef<IPropertyHandle> InProperty, IDetailLayoutBuilder* DetailBuilder)
{
	FName packingTag = FName(InText.ToString());
	InProperty->SetValue(packingTag);
	TargetScriptPtr->ReloadTexture();
	TargetScriptPtr->InitSpriteData();
	DetailBuilder->ForceRefreshDetails();

	CheckSprite();
}

TSharedRef<ITableRow> FDreamUISpriteDataCustomization::GenerateComboItem(TSharedPtr<FName> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FName>>, OwnerTable)
		[
			SNew(STextBlock).Text(FText::FromName(*InItem))
		];
}

void FDreamUISpriteDataCustomization::HandleRequiredParamComboChanged(TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo, TSharedRef<IPropertyHandle> InProperty, IDetailLayoutBuilder* DetailBuilder)
{
	InProperty->SetValue(*Item.Get());
	TargetScriptPtr->ReloadTexture();
	TargetScriptPtr->InitSpriteData();
	DetailBuilder->ForceRefreshDetails();

	CheckSprite();
}

FText FDreamUISpriteDataCustomization::GetPackingTagText(TSharedRef<IPropertyHandle> InProperty)const
{
	FName packingTag;
	InProperty->GetValue(packingTag);
	return FText::FromName(packingTag);
}

FOptionalSize FDreamUISpriteDataCustomization::GetImageWidth()const
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
FOptionalSize FDreamUISpriteDataCustomization::GetImageHeight()const
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
FOptionalSize FDreamUISpriteDataCustomization::GetMinDesiredHeight(IDetailLayoutBuilder* DetailBuilder)const
{
	return DetailBuilder->GetDetailsViewSharedPtr()->GetCachedGeometry().GetLocalSize().Y - 400;
}
FOptionalSize FDreamUISpriteDataCustomization::GetBorderLeftSize()const
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
FOptionalSize FDreamUISpriteDataCustomization::GetBorderRightSize()const
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
FOptionalSize FDreamUISpriteDataCustomization::GetBorderTopSize()const
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
FOptionalSize FDreamUISpriteDataCustomization::GetBorderBottomSize()const
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

bool FDreamUISpriteDataCustomization::IsApplyButtonEnabled(TSharedPtr<IPropertyHandle> BorderDirtyPropertyHandle) const
{
	if (TargetScriptPtr.IsValid())
	{
		return TargetScriptPtr->SpriteInfo.bIsBorderDirty;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
