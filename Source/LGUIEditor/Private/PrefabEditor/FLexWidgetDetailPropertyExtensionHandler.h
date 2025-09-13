// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "IDetailPropertyExtensionHandler.h"

class FLGUIPrefabEditor;

class FLexWidgetDetailPropertyExtensionHandler : public IDetailPropertyExtensionHandler
{
public:
	FLexWidgetDetailPropertyExtensionHandler(TWeakPtr<FLGUIPrefabEditor> InPrefabEditor);

	virtual bool IsPropertyExtendable(const UClass* InObjectClass, const IPropertyHandle& PropertyHandle)const override;
	virtual void ExtendWidgetRow(FDetailWidgetRow& InWidgetRow, const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass, TSharedPtr<IPropertyHandle> PropertyHandle) override;
private:
	TWeakPtr<FLGUIPrefabEditor> PrefabEditorPtr;
};
