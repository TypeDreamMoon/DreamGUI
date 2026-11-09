// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"

#define LOCTEXT_NAMESPACE "DreamGUIEditorUtils"
class DREAMGUIEDITOR_API FDreamUIEditorUtils
{
#define ErrorInfoCategory TEXT("Error")
public:
	template<class T>
	static void ShowError_MultiComponentNotAllowed(IDetailLayoutBuilder* DetailBuilder, T* Component)
	{
		auto ErrorMessage = FText::Format(LOCTEXT("MultipleComponentInOneActorNotAllowed", "Multiple {0} component in one actor is not allowed!"), FText::FromName(T::StaticClass()->GetFName()));
		ShowError_MultiComponentNotAllowed(DetailBuilder, Component, ErrorMessage);
	}
	template<class T>
	static void ShowError_MultiComponentNotAllowed(IDetailLayoutBuilder* DetailBuilder, T* Component, const FText& ErrorMessage)
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamUIBehaviour>::Value, "'T' template parameter to ShowWarning_MultiComponentNotAllowed must be derived from UActorComponent");
		if (auto Widget = Component->GetWidget())
		{
			auto Components = Widget->GetComponents(Component->GetClass());
			if (Components.Num() > 1)
			{
				IDetailCategoryBuilder& dreamguiCategory = DetailBuilder->EditCategory(ErrorInfoCategory, FText::GetEmpty(), ECategoryPriority::Variable);
				dreamguiCategory.AddCustomRow(LOCTEXT("MultiComponentNotAllowed_Tips", "MultiComponentNotAllowed_Tips"))
					.WholeRowContent()
					[
						SNew(STextBlock)
						.Text(ErrorMessage)
						.ColorAndOpacity(FLinearColor(FColor::Red))
						.AutoWrapText(true)
					]
				;
			}
		}
	}
	/** An error row when Component's widget has no RequiredClass: a component of that class, or a visual of it. */
	static void ShowError_RequireComponent(IDetailLayoutBuilder* DetailBuilder, UDreamUIBehaviour* Component, const UClass* RequiredClass)
	{
		if (RequiredClass == nullptr)
		{
			return;
		}
		if (auto Widget = Component->GetWidget())
		{
			// What the sequence players need is a visual, which a TSubclassOf<UDreamUIBehaviour> made from it held as null.
			const bool bHasIt = RequiredClass->IsChildOf(UDreamVisual::StaticClass())
				? Widget->GetVisual() != nullptr && Widget->GetVisual()->IsA(RequiredClass)
				: RequiredClass->IsChildOf(UDreamUIBehaviour::StaticClass()) && Widget->GetComponents(const_cast<UClass*>(RequiredClass)).Num() > 0;
			if (!bHasIt)
			{
				IDetailCategoryBuilder& Category = DetailBuilder->EditCategory(ErrorInfoCategory, FText::GetEmpty(), ECategoryPriority::Variable);
				Category.AddCustomRow(LOCTEXT("RequireComponentRow", "RequireComponent"))
					.WholeRowContent()
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("RequireComponentTip", "This component require {0} component on widget!"), FText::FromName(RequiredClass->GetFName())))
						.ColorAndOpacity(FLinearColor(FColor::Red))
						.AutoWrapText(true)
					]
				;
			}
		}
	}
	static void ShowError(IDetailLayoutBuilder* LayoutBuilder, const FText& ErrorMessage)
	{
		IDetailCategoryBuilder& category = LayoutBuilder->EditCategory(ErrorInfoCategory, FText::GetEmpty(), ECategoryPriority::Variable);
		category.AddCustomRow(LOCTEXT("ErrorInfoTextRow", "ErrorInfoText"))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(ErrorMessage)
			.ColorAndOpacity(FLinearColor(FColor::Red))
			.AutoWrapText(true)
		]
		;
	}
	static void ShowWarning(IDetailLayoutBuilder* LayoutBuilder, const FText& ErrorMessage)
	{
		IDetailCategoryBuilder& category = LayoutBuilder->EditCategory(ErrorInfoCategory, FText::GetEmpty(), ECategoryPriority::Variable);
		category.AddCustomRow(LOCTEXT("WarningInfoTextRow", "WarningInfoText"))
			.WholeRowContent()
			[
				SNew(STextBlock)
				.Text(ErrorMessage)
				.ColorAndOpacity(FLinearColor(FColor::Yellow))
				.AutoWrapText(true)
			]
		;
	}
	static void ShowError(IDetailCategoryBuilder* CategoryBuilder, const FText& ErrorMessage)
	{
		CategoryBuilder->AddCustomRow(LOCTEXT("ErrorInfoTextRow", "ErrorInfoText"))
			.WholeRowContent()
			[
				SNew(STextBlock)
				.Text(ErrorMessage)
				.ColorAndOpacity(FLinearColor(FColor::Red))
				.AutoWrapText(true)
			]
		;
	}
	static IDetailPropertyRow& CreateSubDetail(IDetailCategoryBuilder* category, IDetailLayoutBuilder* DetailBuilder, TSharedRef<IPropertyHandle> handle)
	{
		DetailBuilder->HideProperty(handle);
		IDetailPropertyRow& prop = category->AddProperty(handle);
		auto name = handle->GetPropertyDisplayName().ToString();
		name = "    " + name;
		prop.DisplayName(FText::FromString(name));
		return prop;
	}
	static bool IsEnabledOnProperty(TSharedRef<IPropertyHandle> PropertyHandle)
	{
		return PropertyHandle->IsEditable();
	}
	static void DrawThumbnailIcon(const FString& TextureFullPath, int32 X, int32 Y, uint32 Width, uint32 Height, FCanvas* Canvas);
	/** The icon at TextureFullPath, loaded once per editor session and held by UDreamGUIEditorSubsystem. */
	static UTexture2D* LoadTexture(const FString& TextureFullPath);
};
#undef LOCTEXT_NAMESPACE
