// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Text/DreamTextPaint.h"
#include "DataFactory/DreamUIAssetFactory.h"
#include "DreamGradientAssetFactory.generated.h"

/** Makes Dream Gradient assets (UDreamGradientAsset): from the Add menu, and from a gradient row's "Save as Gradient Asset". */
UCLASS()
class UDreamGradientAssetFactory : public UDreamUIAssetFactory
{
	GENERATED_BODY()
public:
	UDreamGradientAssetFactory();

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface

	/** What the new asset holds; unset, white to black from top to bottom. */
	TOptional<FDreamGradient> InitialGradient;
};
