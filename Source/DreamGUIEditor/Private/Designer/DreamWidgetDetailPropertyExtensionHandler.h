// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "IDetailPropertyExtensionHandler.h"

class FDreamWidgetDetailPropertyExtensionHandler : public IDetailPropertyExtensionHandler
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

	virtual bool IsPropertyExtendable(const UClass* InObjectClass, const IPropertyHandle& PropertyHandle)const override;
	virtual void ExtendWidgetRow(FDetailWidgetRow& InWidgetRow, const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass, TSharedPtr<IPropertyHandle> PropertyHandle) override;
private:
	TWeakObjectPtr<UWorld> World;
};
