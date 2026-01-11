// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FLexImageBrushStructCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader( TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils ) override;
	virtual void CustomizeChildren( TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils ) override;
private:
	EVisibility GetMarginPropertyVisibility() const;
	EVisibility GetUVRegionPropertyVisibility() const;
	EVisibility GetPixelsPerUnitMultiplierPropertyVisibility() const;

	/** Callback for determining image size reset button visibility */
	bool IsImageSizeResetToDefaultVisible(TSharedPtr<IPropertyHandle> PropertyHandle) const;

	/** Callback for clicking the image size reset button */
	void OnImageSizeResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle) const;

	/** Gets the current default image size based on the current texture resource */
	FVector2f GetDefaultImageSize() const;

	/** Slate Brush DrawAs property */
	TSharedPtr<IPropertyHandle> DrawAsProperty;

	/** Slate Brush Image Size property */
	TSharedPtr<IPropertyHandle> ImageSizeProperty;

	/** Slate Brush Resource Object property */
	TSharedPtr<IPropertyHandle> ResourceObjectProperty;

	/** Slate Brush Image Type property */
	TSharedPtr<IPropertyHandle> ImageTypeProperty;

	/** Error text to display if the resource object is not valid*/
	TSharedPtr<SErrorText> ResourceErrorText;
};
