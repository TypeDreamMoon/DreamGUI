// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamCanvasCustomization.h"
#include "DreamUIEditorUtils.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Engine/TextureRenderTarget2D.h"

#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"
#include "Widgets/Input/SSlider.h"

#define LOCTEXT_NAMESPACE "DreamCanvasCustomization"
FDreamCanvasCustomization::FDreamCanvasCustomization()
{
}

FDreamCanvasCustomization::~FDreamCanvasCustomization()
{
}

TSharedRef<IDetailCustomization> FDreamCanvasCustomization::MakeInstance()
{
	return MakeShareable(new FDreamCanvasCustomization);
}
void FDreamCanvasCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<UDreamCanvas>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	FDreamUIEditorUtils::ShowError_MultiComponentNotAllowed(&DetailBuilder, TargetScriptArray[0].Get());

	auto RenderModeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderMode));
	RenderModeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamCanvasCustomization::ForceRefresh, &DetailBuilder));
	
	if (TargetScriptArray[0]->GetActualRenderMode() == EDreamRenderMode::ScreenSpaceOverlay)
	{
		if (auto World = TargetScriptArray[0]->GetWorld())
		{
			if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(World))
			{
				auto CanvasArray = DreamUIManager->GetCanvasArrayByRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
				TArray<UDreamCanvas*> RootCanvasArray;
				for (auto& Canvas : CanvasArray)
				{
					if (Canvas && Canvas->IsRootCanvas())
					{
						RootCanvasArray.Add(Canvas);
					}
				}
				int ScreenSpaceRootCanvasCount = RootCanvasArray.Num();
				if (ScreenSpaceRootCanvasCount > 1)
				{
					auto errMsg = FText::Format(LOCTEXT("MultipleScreenSpaceDreamCanvasError", "[{0}].{1} Detect multiple DreamCanvas rendered with ScreenSpaceOverlay mode, this is not allowed! There should be only one ScreenSpace UI in a world!")
					, FText::FromString(ANSI_TO_TCHAR(__FUNCTION__)), __LINE__);
					FDreamUIEditorUtils::ShowError(&DetailBuilder, errMsg);
				}
			}
		}
	}
	
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("DreamGUI");
	TArray<FName> NeedToHidePropertyNames;

	if (TargetScriptArray[0]->GetWorld() != nullptr)
	{
		Category.AddCustomRow(LOCTEXT("DrawCallInfo", "DrawCallInfo"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DrawCallCountLabel", "DrawCallCount"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FLinearColor(FColor::Green))
		]
		.ValueContent()
		[
			SNew(STextBlock)
			.Text(this, &FDreamCanvasCustomization::GetDrawcallInfo)
			.ToolTipText(this, &FDreamCanvasCustomization::GetDrawcallInfoTooltip)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FLinearColor(FColor::Green))
		]
		;
	}

	auto OverrideSortingHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, bOverrideSorting));
	bool bOverrideSorting = false;
	OverrideSortingHandle->GetValue(bOverrideSorting);
	OverrideSortingHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamCanvasCustomization::ForceRefresh, &DetailBuilder));

	if (bOverrideSorting)
	{
		auto& Group = Category.AddGroup(TEXT("OverrideSortingGroup"), OverrideSortingHandle->GetPropertyDisplayName());
		Group.HeaderProperty(OverrideSortingHandle);
		Group.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, SortOrder)));
	}
	else
	{
		NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, SortOrder));
	}
	
	auto ForceRenderToTarget_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, bForceRenderToTarget));
	ForceRenderToTarget_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamCanvasCustomization::ForceRefresh, &DetailBuilder));

	if (TargetScriptArray[0]->IsRootCanvas()
		|| TargetScriptArray[0]->GetWorld() == nullptr//maybe in blueprint editor, then world is null
		)
	{
		if (TargetScriptArray[0]->GetParentCanvas() == nullptr)
		{
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, bForceRenderToTarget));
		}
		switch (TargetScriptArray[0]->RenderMode)
		{
		case EDreamRenderMode::ScreenSpaceOverlay:
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTarget));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetClearColor));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetUpdateMode));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetSizeMode));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetResolutionScale));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, BlendDepth));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, DepthFade));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, TraceChannel));
			break;
		case EDreamRenderMode::WorldSpace:
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTarget));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetClearColor));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetUpdateMode));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetSizeMode));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetResolutionScale));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, BlendDepth));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, DepthFade));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, bEnableDepthTest));
			break;
		case EDreamRenderMode::WorldSpace_DreamUI:
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTarget));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetClearColor));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetUpdateMode));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetSizeMode));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetResolutionScale));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, bEnableDepthTest));
			break;
		case EDreamRenderMode::RenderTarget:
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, BlendDepth));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, DepthFade));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, TraceChannel));
			break;
		}
	}
	else
	{
		NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderMode));
		NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTarget));
		NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetClearColor));
		NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetUpdateMode));
		NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetSizeMode));
		NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetResolutionScale));
		NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, bEnableDepthTest));
		NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, TraceChannel));

		auto overrideParametersHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, OverrideParameters));
		overrideParametersHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamCanvasCustomization::ForceRefresh, &DetailBuilder));
		if (!TargetScriptArray[0]->GetOverrideDefaultMaterial())
		{
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, DefaultMaterial));
		}
		if (!TargetScriptArray[0]->GetOverrideRequireNormalAndTangent())
		{
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, bRequireNormalAndTangent));
		}

		if (!TargetScriptArray[0]->GetOverrideBlendDepth())
		{
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, BlendDepth));
		}
		if (!TargetScriptArray[0]->GetOverrideDepthFade())
		{
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, DepthFade));
		}
	}

	if (!NeedToHidePropertyNames.Contains(GET_MEMBER_NAME_CHECKED(UDreamCanvas, bForceRenderToTarget)))
	{
		Category.AddProperty(ForceRenderToTarget_PH);
	}
	if (!NeedToHidePropertyNames.Contains(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderMode)))
	{
		Category.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderMode));
	}
	if (!NeedToHidePropertyNames.Contains(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTarget)))
	{
		IDetailGroup& RenderTargetGroup = Category.AddGroup(FName(TEXT("RenderTarget")), LOCTEXT("RenderTarget", "RenderTarget"));
		RenderTargetGroup.HeaderProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTarget)));
		RenderTargetGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetUpdateMode)));
		RenderTargetGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetSizeMode)));
		RenderTargetGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetResolutionScale)));
		RenderTargetGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderTargetClearColor)));
	}

	auto& CanvasScalerCategory = DetailBuilder.EditCategory("DreamGUI-CanvasScaler");
	//add all property
	NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ProjectionType));
	NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, FieldOfView));
	NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, NearClipPlane));
	NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, FarClipPlane));

	NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ScaleMode));
	NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ReferenceResolution));
	NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ScreenMatchMode));
	NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, MatchFromWidthToHeight));
	NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, CustomScale));
	NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, bFixedSizeInEditMode));
	NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, SizeInEditMode));

	auto CreateSlider = [this, &CanvasScalerCategory](const FText& FilterString, TSharedPtr<IPropertyHandle> Property) {
	CanvasScalerCategory.AddCustomRow(FilterString)
	.PropertyHandleList({ Property })
	.NameContent()
	[
		SNew(SBox)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Match", "Match"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	]
	.ValueContent()
	.MinDesiredWidth(500)
	[
		SAssignNew(ValueBox, SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(this, &FDreamCanvasCustomization::GetValueWidth)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				[
					SNew(SSlider)
					.Value_Lambda([=]{
						float value = 0.0;
						Property->GetValue(value);
						return value;
						})
					.OnValueChanged_Lambda([=](float value){
						Property->SetValue(value);
						})
				]
				+SVerticalBox::Slot()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Width", "Width"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
					+ SHorizontalBox::Slot()
					.HAlign(EHorizontalAlignment::HAlign_Right)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Height", "Height"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				]
			]
		]
		+SHorizontalBox::Slot()
		.HAlign(EHorizontalAlignment::HAlign_Right)
		[
			SNew(SBox)
			.MinDesiredWidth(50)
			[
				Property->CreatePropertyValueWidget()
			]
		]
	]
	;
	};

	EDreamRenderMode ActualRenderMode;
	if (TargetScriptArray[0]->GetWorld() == nullptr)
	{
		ActualRenderMode = TargetScriptArray[0]->GetRenderMode();
	}
	else
	{
		ActualRenderMode = TargetScriptArray[0]->GetActualRenderMode();
	}
	
	if (ActualRenderMode == EDreamRenderMode::WorldSpace || ActualRenderMode == EDreamRenderMode::WorldSpace_DreamUI)
	{
		CanvasScalerCategory.AddCustomRow(LOCTEXT("WorldSpaceUIInfo", "WorldSpaceUIInfo"))
			.WholeRowContent()
			.MinDesiredWidth(500)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("NothingHereForWorldSpaceUI", "Nothing here for WorldSpaceUI"))
				.AutoWrapText(true)
			];
	}
	else if (
		ActualRenderMode == EDreamRenderMode::ScreenSpaceOverlay
		|| ActualRenderMode == EDreamRenderMode::RenderTarget
		)
	{
		CanvasScalerCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ScaleMode));

		DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ScaleMode))
			->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&] { DetailBuilder.ForceRefreshDetails(); }));
		if (TargetScriptArray[0]->ScaleMode == EDreamCanvasScaleMode::ScaleWithScreenSize)
		{
			CanvasScalerCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ReferenceResolution));
			CanvasScalerCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ScreenMatchMode));
			DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ScreenMatchMode))
				->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&] { DetailBuilder.ForceRefreshDetails(); }));
			switch (TargetScriptArray[0]->ScreenMatchMode)
			{
			case EDreamCanvasScreenMatchMode::Expand:
			case EDreamCanvasScreenMatchMode::Shrink:
			{
				
			}
			break;
			case EDreamCanvasScreenMatchMode::MatchWidthOrHeight:
			{
				auto matchProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, MatchFromWidthToHeight));
				CreateSlider(LOCTEXT("MatchSlider", "MatchSlider"), matchProperty);
			}
			break;
			}
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, CustomScale));
		}
		else if (TargetScriptArray[0]->ScaleMode == EDreamCanvasScaleMode::ConstantPixelSize)
		{
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ReferenceResolution));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ScreenMatchMode));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, MatchFromWidthToHeight));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, CustomScale));
		}
		else
		{
			CanvasScalerCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, CustomScale));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ReferenceResolution));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ScreenMatchMode));
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, MatchFromWidthToHeight));
		}

		auto projectionTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ProjectionType));
		projectionTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&] { DetailBuilder.ForceRefreshDetails(); }));
		if (TargetScriptArray[0]->ProjectionType == ECameraProjectionMode::Orthographic)
		{
			NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamCanvas, FieldOfView));
		}

		CanvasScalerCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, ProjectionType));
		CanvasScalerCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, FieldOfView));
		CanvasScalerCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, NearClipPlane));
		CanvasScalerCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, FarClipPlane));

		if (ActualRenderMode == EDreamRenderMode::ScreenSpaceOverlay)
		{
			CanvasScalerCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, bFixedSizeInEditMode));
			CanvasScalerCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamCanvas, SizeInEditMode));
		}
	}

	for (auto item : NeedToHidePropertyNames)
	{
		DetailBuilder.HideProperty(item);
	}
}

FReply FDreamCanvasCustomization::OnClickFixClipTextureSetting(TSharedRef<IPropertyHandle> ClipTextureHandle)
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
bool FDreamCanvasCustomization::IsFixClipTextureEnabled(TSharedRef<IPropertyHandle> ClipTextureHandle)const
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

void FDreamCanvasCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (DetailBuilder)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}

FText FDreamCanvasCustomization::GetDrawcallInfo()const
{
	auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(TargetScriptArray[0]->GetWorld());
	if (TargetScriptArray.Num() > 0 && TargetScriptArray[0].IsValid() && DreamUIManager)
	{
		auto CanvasArray = DreamUIManager->GetCanvasArrayByRenderMode(TargetScriptArray[0]->GetRenderMode());
		int AllDrawcallCount = 0;
		for (auto& CanvasItem : CanvasArray)
		{
			if (TargetScriptArray[0]->GetActualRenderMode() == EDreamRenderMode::RenderTarget)
			{
				if (TargetScriptArray[0]->RenderTarget == CanvasItem->RenderTarget && IsValid(CanvasItem->RenderTarget))
				{
					AllDrawcallCount += CanvasItem->GetDrawCallCount();
				}
			}
			else
			{
				AllDrawcallCount += CanvasItem->GetDrawCallCount();
			}
		}
		return FText::FromString(FString::Printf(TEXT("%d/%d"), TargetScriptArray[0]->GetDrawCallCount(), AllDrawcallCount));
	}
	return FText::FromString(FString::Printf(TEXT("0/0")));
}
FText FDreamCanvasCustomization::GetDrawcallInfoTooltip()const
{
	FString spaceText;
	switch (TargetScriptArray[0]->GetActualRenderMode())
	{
	case EDreamRenderMode::ScreenSpaceOverlay:
		spaceText = TEXT("ScreenSpaceOverlay");
		break;
	case EDreamRenderMode::WorldSpace:
		spaceText = TEXT("WorldSpace UE Renderer");
		break;
	case EDreamRenderMode::WorldSpace_DreamUI:
		spaceText = TEXT("WorldSpace DreamGUI Renderer");
		break;
	case EDreamRenderMode::RenderTarget:
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

	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(TargetScriptArray[0]->GetWorld()))
	{
		auto CanvasArray = DreamUIManager->GetCanvasArrayByRenderMode(TargetScriptArray[0]->GetRenderMode());
		int AllDrawcallCount = 0;
		for (auto& CanvasItem : CanvasArray)
		{
			if (TargetScriptArray[0]->GetActualRenderMode() == EDreamRenderMode::RenderTarget)
			{
				if (TargetScriptArray[0]->RenderTarget == CanvasItem->RenderTarget && IsValid(CanvasItem->RenderTarget))
				{
					AllDrawcallCount += CanvasItem->GetDrawCallCount();
				}
			}
			else
			{
				AllDrawcallCount += CanvasItem->GetDrawCallCount();
			}
		}
		return FText::Format(LOCTEXT("DrawcallInfoTooltip", "This canvas's drawcall count:{0}, all canvas of {1} drawcall count:{2}")
			, TargetScriptArray[0]->GetDrawCallCount(), FText::FromString(spaceText), AllDrawcallCount);
	}
	return FText::GetEmpty();
}
void FDreamCanvasCustomization::OnCopySortOrder()
{
	if (TargetScriptArray.Num() > 0)
	{
		if (TargetScriptArray[0].IsValid())
		{
			FPlatformApplicationMisc::ClipboardCopy(*FString::Printf(TEXT("%d"), TargetScriptArray[0]->GetSortOrder()));
		}
	}
}
void FDreamCanvasCustomization::OnPasteSortOrder(TSharedRef<IPropertyHandle> PropertyHandle)
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	if (PastedText.IsNumeric())
	{
		int value = FCString::Atoi(*PastedText);
		PropertyHandle->SetValue(value);
	}
}
FOptionalSize FDreamCanvasCustomization::GetValueWidth()const
{
	return ValueBox->GetCachedGeometry().GetLocalSize().X - 60;
}
#undef LOCTEXT_NAMESPACE