// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "IDetailPropertyExtensionHandler.h"

class FLexUIPrefabEditor;

class FLexWidgetDetailPropertyExtensionHandler : public IDetailPropertyExtensionHandler
{
public:
	FLexWidgetDetailPropertyExtensionHandler(TWeakPtr<FLexUIPrefabEditor> InPrefabEditor);

	virtual bool IsPropertyExtendable(const UClass* InObjectClass, const IPropertyHandle& PropertyHandle)const override;
	virtual void ExtendWidgetRow(FDetailWidgetRow& InWidgetRow, const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass, TSharedPtr<IPropertyHandle> PropertyHandle) override;
private:
	TWeakPtr<FLexUIPrefabEditor> PrefabEditorPtr;
	TSharedPtr<SComboButton> PickerButton;
};
