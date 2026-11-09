// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "IDetailPropertyExtensionHandler.h"

class DREAMGUIEDITOR_API FDreamWidgetDetailPropertyExtensionHandler : public IDetailPropertyExtensionHandler
{
public:
	FDreamWidgetDetailPropertyExtensionHandler(UWorld* InWorld);

	/**
	 * A property the hierarchy picker can fill: a non-instanced object reference to a widget, one of
	 * its behaviours, or one of its sub-objects. IsPropertyExtendable is asked about every row in the
	 * panel, so it has to answer with the same rule ExtendWidgetRow builds against -- a bare true
	 * makes the property editor allocate an extension slot for rows that will never use one.
	 */
	static bool IsWidgetReferenceProperty(const FProperty* InProperty);

	/** An FLinearColor or FColor row: the eyedropper and the project's palette sit beside it (SDreamUIColorQuickPick). */
	static bool IsColorProperty(const FProperty* InProperty);
	/**
	 * A colour the eyedropper or the palette gave (linear light) written through a colour row's handle, in a transaction of
	 * its own, keeping the row's alpha: an FColor row takes it as sRGB. Through the handle, so the panel takes it to the
	 * authoring tree and the `.dui` as it takes a typed value. False when the row is no colour or cannot be read.
	 */
	static bool WritePickedColor(const TSharedPtr<IPropertyHandle>& InHandle, const FLinearColor& InColor);

	virtual bool IsPropertyExtendable(const UClass* InObjectClass, const IPropertyHandle& PropertyHandle)const override;
	virtual void ExtendWidgetRow(FDetailWidgetRow& InWidgetRow, const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass, TSharedPtr<IPropertyHandle> PropertyHandle) override;
private:
	TWeakObjectPtr<UWorld> World;
};
