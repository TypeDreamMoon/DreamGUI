// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexCanvasCustomization.h"
#include "LGUIEditorUtils.h"
#include "Core/Components/LexCanvas.h"
#include "Core/LGUIManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Engine/TextureRenderTarget2D.h"

#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"

#define LOCTEXT_NAMESPACE "LGUICanvasCustomization"
FLexCanvasCustomization::FLexCanvasCustomization()
{
}

FLexCanvasCustomization::~FLexCanvasCustomization()
{
}

TSharedRef<IDetailCustomization> FLexCanvasCustomization::MakeInstance()
{
	return MakeShareable(new FLexCanvasCustomization);
}
void FLexCanvasCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<ULexCanvas>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[FLGUICanvasCustomization]Get TargetScript is null"));
		return;
	}

	LGUIEditorUtils::ShowError_MultiComponentNotAllowed(&DetailBuilder, TargetScriptArray[0].Get());

	auto renderModeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderMode));
	renderModeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexCanvasCustomization::ForceRefresh, &DetailBuilder));

	if (TargetScriptArray[0]->GetActualRenderMode() == ELexRenderMode::ScreenSpaceOverlay)
	{
		if (auto world = TargetScriptArray[0]->GetWorld())
		{
			if (auto LGUIManager = ULGUIManagerWorldSubsystem::GetInstance(world))
			{
				auto& CanvasArray = LGUIManager->GetCanvasArray(ELexRenderMode::ScreenSpaceOverlay);
				int ScreenSpaceRootCanvasCount = 0;
				for (auto item : CanvasArray)
				{
					if (item.IsValid())
					{
						if (item->IsRootCanvas())
						{
							ScreenSpaceRootCanvasCount++;
						}
					}
				}
				if (ScreenSpaceRootCanvasCount > 1)
				{
					auto errMsg = FText::Format(LOCTEXT("MultipleScreenSpaceLGUICanvasError", "[{0}].{1} Detect multiple LGUICanvas renderred with ScreenSpaceOverlay mode, this is not allowed! There should be only one ScreenSpace UI in a world!")
					, FText::FromString(ANSI_TO_TCHAR(__FUNCTION__)), __LINE__);
					LGUIEditorUtils::ShowError(&DetailBuilder, errMsg);
				}
			}
		}
	}

	//if (TargetScriptArray[0]->GetWorld())
	//{
	//	if (!TargetScriptArray[0]->GetWorld()->IsGameWorld())
	//	{
	//		TargetScriptArray[0]->MarkCanvasUpdate();
	//	}
	//}
	
	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("LGUI");
	TArray<FName> needToHidePropertyNames;

	if (TargetScriptArray[0]->GetWorld() != nullptr)
	{
		category.AddCustomRow(LOCTEXT("DrawcallInfo", "DrawcallInfo"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DrawcallCountLabel", "DrawcallCount"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FLinearColor(FColor::Green))
		]
		.ValueContent()
		[
			SNew(STextBlock)
			.Text(this, &FLexCanvasCustomization::GetDrawcallInfo)
			.ToolTipText(this, &FLexCanvasCustomization::GetDrawcallInfoTooltip)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FLinearColor(FColor::Green))
		]
		;
	}

	auto OverrideSortingHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexCanvas, bOverrideSorting));
	bool bOverrideSorting;
	OverrideSortingHandle->GetValue(bOverrideSorting);
	category.AddProperty(OverrideSortingHandle);
	OverrideSortingHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexCanvasCustomization::ForceRefresh, &DetailBuilder));

	if (bOverrideSorting)
	{
		category.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexCanvas, SortOrder)));
		//sortOrder info
		{
			category.AddCustomRow(LOCTEXT("SortOrderInfo", "SortOrderInfo"))
			.WholeRowContent()
			.MinDesiredWidth(500)
			[
				SNew(SBox)
				.HeightOverride(20)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(this, &FLexCanvasCustomization::GetSortOrderInfo, TargetScriptArray[0])
					.AutoWrapText(true)
				]
			]
			;
		}
	}
	else
	{
		needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, SortOrder));
	}

	if (TargetScriptArray[0]->IsRootCanvas())
	{
		switch (TargetScriptArray[0]->RenderMode)
		{
		case ELexRenderMode::ScreenSpaceOverlay:
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTarget));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetUpdateMode));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetSizeMode));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetResolutionScale));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, BlendDepth));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, DepthFade));
			break;
		case ELexRenderMode::WorldSpace:
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTarget));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetUpdateMode));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetSizeMode));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetResolutionScale));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, BlendDepth));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, DepthFade));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, bEnableDepthTest));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, bPreviewWithLGUIRenderer));
			break;
		case ELexRenderMode::WorldSpace_LGUI:
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTarget));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetUpdateMode));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetSizeMode));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetResolutionScale));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, bEnableDepthTest));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, bPreviewWithLGUIRenderer));
			break;
		case ELexRenderMode::RenderTarget:
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, BlendDepth));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, DepthFade));
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, bPreviewWithLGUIRenderer));
			break;
		}
	}
	else
	{
		needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderMode));
		needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTarget));
		needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetUpdateMode));
		needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetSizeMode));
		needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetResolutionScale));
		needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, bEnableDepthTest));
		needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, bPreviewWithLGUIRenderer));

		auto overrideParametersHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexCanvas, OverrideParameters));
		overrideParametersHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexCanvasCustomization::ForceRefresh, &DetailBuilder));
		if (!TargetScriptArray[0]->GetOverrideDefaultMaterial())
		{
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, DefaultMaterial));
		}
		if (!TargetScriptArray[0]->GetOverrideDynamicPixelsPerUnit())
		{
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, DynamicPixelsPerUnit));
		}
		if (!TargetScriptArray[0]->GetOverrideRequireNormalAndTangent())
		{
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, bRequireNormalAndTangent));
		}

		if (!TargetScriptArray[0]->GetOverrideBlendDepth())
		{
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, BlendDepth));
		}
		if (!TargetScriptArray[0]->GetOverrideDepthFade())
		{
			needToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(ULexCanvas, DepthFade));
		}
	}

	if (!needToHidePropertyNames.Contains(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderMode)))
	{
		category.AddProperty(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderMode));
	}
	if (!needToHidePropertyNames.Contains(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTarget)))
	{
		IDetailGroup& RenderTargetGroup = category.AddGroup(FName(TEXT("RenderTarget")), LOCTEXT("RenderTarget", "RenderTarget"));
		RenderTargetGroup.HeaderProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTarget)));
		RenderTargetGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetUpdateMode)));
		RenderTargetGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetSizeMode)));
		RenderTargetGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderTargetResolutionScale)));
	}

	for (auto item : needToHidePropertyNames)
	{
		DetailBuilder.HideProperty(item);
	}
}

FReply FLexCanvasCustomization::OnClickFixClipTextureSetting(TSharedRef<IPropertyHandle> ClipTextureHandle)
{
	UObject* ClipTextureObject = nullptr;
	ClipTextureHandle->GetValue(ClipTextureObject);
	if (IsValid(ClipTextureObject))
	{
		auto clipTexture = Cast<UTexture2D>(ClipTextureObject);
		if (clipTexture->CompressionSettings != TextureCompressionSettings::TC_EditorIcon
			|| clipTexture->MipGenSettings != TextureMipGenSettings::TMGS_NoMipmaps
			)
		{
			clipTexture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
			clipTexture->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
			clipTexture->UpdateResource();
			clipTexture->Modify();
		}
	}

	return FReply::Handled();
}
bool FLexCanvasCustomization::IsFixClipTextureEnabled(TSharedRef<IPropertyHandle> ClipTextureHandle)const
{
	UObject* ClipTextureObject = nullptr;
	ClipTextureHandle->GetValue(ClipTextureObject);
	if (IsValid(ClipTextureObject))
	{
		auto clipTexture = Cast<UTexture2D>(ClipTextureObject);
		if (clipTexture->CompressionSettings != TextureCompressionSettings::TC_EditorIcon
			|| clipTexture->MipGenSettings != TextureMipGenSettings::TMGS_NoMipmaps
			)
		{
			return true;
		}
	}
	return false;
}

void FLexCanvasCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (DetailBuilder)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
FText FLexCanvasCustomization::GetSortOrderInfo(TWeakObjectPtr<ULexCanvas> TargetScript)const
{
	if (TargetScript.IsValid())
	{
		if (auto world = TargetScript->GetWorld())
		{
			if (auto LGUIManager = ULGUIManagerWorldSubsystem::GetInstance(world))
			{
				FText spaceText;
				if (TargetScript->IsRenderToScreenSpace())
				{
					spaceText = LOCTEXT("ScreenSpaceOverlay", "ScreenSpaceOverlay");
				}
				else if (TargetScript->IsRenderToWorldSpace())
				{
					if (TargetScript->IsRenderByLGUIRendererOrUERenderer())
					{
						spaceText = LOCTEXT("World Space - LGUI Renderer", "World Space - LGUI Renderer");
					}
					else
					{
						spaceText = LOCTEXT("World Space - UE Renderer", "World Space - UE Renderer");
					}
				}
				else if (TargetScript->IsRenderToRenderTarget())
				{
					if (IsValid(TargetScript->RenderTarget))
					{
						spaceText = FText::Format(LOCTEXT("RenderTarget({0})", "RenderTarget({0})"), FText::FromString(TargetScript->RenderTarget->GetName()));
					}
					else
					{
						spaceText = LOCTEXT("RenderTarget(NotValid)", "RenderTarget(NotValid)");
					}
				}

				auto renderMode = TargetScript->GetActualRenderMode();
				auto& itemList = LGUIManager->GetCanvasArray(renderMode);
				int sortOrderCount = 0;
				for (auto item : itemList)
				{
					if (!item.IsValid())continue;
					if (item == TargetScript)continue;

					if (item->GetSortOrder() == TargetScript->GetSortOrder())
						sortOrderCount++;
				}
				auto depthInfo = FText::Format(LOCTEXT("CanvasSortOrderTip", "All LGUICanvas of {0} with same SortOrder count: {1}\n"), spaceText, sortOrderCount);
				return depthInfo;
			}
		}
	}
	return FText::GetEmpty();
}

FText FLexCanvasCustomization::GetDrawcallInfo()const
{
	auto LGUIManager = ULGUIManagerWorldSubsystem::GetInstance(TargetScriptArray[0]->GetWorld());
	if (TargetScriptArray.Num() > 0 && TargetScriptArray[0].IsValid() && LGUIManager)
	{
		auto& allCanvas = LGUIManager->GetCanvasArray(TargetScriptArray[0]->GetRenderMode());
		int allDrawcallCount = 0;
		for (auto& canvasItem : allCanvas)
		{
			if (TargetScriptArray[0]->GetActualRenderMode() == ELexRenderMode::RenderTarget)
			{
				if (TargetScriptArray[0]->RenderTarget == canvasItem->RenderTarget && IsValid(canvasItem->RenderTarget))
				{
					allDrawcallCount += canvasItem->GetDrawCallCount();
				}
			}
			else
			{
				allDrawcallCount += canvasItem->GetDrawCallCount();
			}
		}
		return FText::FromString(FString::Printf(TEXT("%d/%d"), TargetScriptArray[0]->GetDrawCallCount(), allDrawcallCount));
	}
	return FText::FromString(FString::Printf(TEXT("0/0")));
}
FText FLexCanvasCustomization::GetDrawcallInfoTooltip()const
{
	FString spaceText;
	switch (TargetScriptArray[0]->GetActualRenderMode())
	{
	case ELexRenderMode::ScreenSpaceOverlay:
		spaceText = TEXT("ScreenSpaceOverlay");
		break;
	case ELexRenderMode::WorldSpace:
		spaceText = TEXT("WorldSpace UE Renderer");
		break;
	case ELexRenderMode::WorldSpace_LGUI:
		spaceText = TEXT("WorldSpace LGUI Renderer");
		break;
	case ELexRenderMode::RenderTarget:
		if (IsValid(TargetScriptArray[0]->RenderTarget))
		{
			spaceText = FString::Printf(TEXT("RenderTarget(%s)"), *(TargetScriptArray[0]->RenderTarget->GetName()));
		}
		else
		{
			spaceText = FString::Printf(TEXT("RenderTarget(NotValid)"));
		}
		break;
	}

	if (auto LGUIManager = ULGUIManagerWorldSubsystem::GetInstance(TargetScriptArray[0]->GetWorld()))
	{
		auto& allCanvas = LGUIManager->GetCanvasArray(TargetScriptArray[0]->GetActualRenderMode());
		int allDrawcallCount = 0;
		for (auto& canvasItem : allCanvas)
		{
			if (TargetScriptArray[0]->GetActualRenderMode() == ELexRenderMode::RenderTarget)
			{
				if (TargetScriptArray[0]->RenderTarget == canvasItem->RenderTarget && IsValid(canvasItem->RenderTarget))
				{
					allDrawcallCount += canvasItem->GetDrawCallCount();
				}
			}
			else
			{
				allDrawcallCount += canvasItem->GetDrawCallCount();
			}
		}
		auto tooltipStr = FText::Format(LOCTEXT("DrawcallInfoTooltip", "This canvas's drawcall count:{0}, all canvas of {1} drawcall count:{2}")
			, TargetScriptArray[0]->GetDrawCallCount(), FText::FromString(spaceText), allDrawcallCount);
		return tooltipStr;
	}
	return FText::GetEmpty();
}
void FLexCanvasCustomization::OnCopySortOrder()
{
	if (TargetScriptArray.Num() > 0)
	{
		if (TargetScriptArray[0].IsValid())
		{
			FPlatformApplicationMisc::ClipboardCopy(*FString::Printf(TEXT("%d"), TargetScriptArray[0]->GetSortOrder()));
		}
	}
}
void FLexCanvasCustomization::OnPasteSortOrder(TSharedRef<IPropertyHandle> PropertyHandle)
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	if (PastedText.IsNumeric())
	{
		int value = FCString::Atoi(*PastedText);
		PropertyHandle->SetValue(value);
	}
}
#undef LOCTEXT_NAMESPACE