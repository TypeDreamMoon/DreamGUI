// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DataFactory/DreamGradientAssetFactory.h"
#include "Core/DreamGradientAsset.h"

UDreamGradientAssetFactory::UDreamGradientAssetFactory()
{
	SupportedClass = UDreamGradientAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UDreamGradientAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UDreamGradientAsset* NewAsset = NewObject<UDreamGradientAsset>(InParent, Class, Name, Flags | RF_Transactional);
	FDreamGradient Gradient;
	if (InitialGradient.IsSet())
	{
		Gradient = InitialGradient.GetValue();
	}
	else
	{
		// Something to see and to edit: an empty gradient paints nothing, and its editor would start blank.
		Gradient.Stops.Add(FDreamGradientStop(0.0f, FColor::White));
		Gradient.Stops.Add(FDreamGradientStop(1.0f, FColor::Black));
	}
	NewAsset->SetGradient(Gradient);
	return NewAsset;
}
