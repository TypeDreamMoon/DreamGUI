// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FLexVisualBatchMeshCustomization : public IDetailCustomization
{
public:
	FLexVisualBatchMeshCustomization();
	~FLexVisualBatchMeshCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class ULexVisualBatchMesh> TargetScriptPtr;
};
