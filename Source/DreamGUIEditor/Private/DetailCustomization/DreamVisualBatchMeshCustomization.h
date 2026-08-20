// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FDreamVisualBatchMeshCustomization : public IDetailCustomization
{
public:
	FDreamVisualBatchMeshCustomization();
	~FDreamVisualBatchMeshCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class UDreamVisualBatchMesh> TargetScriptPtr;
};
