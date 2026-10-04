// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "AssetTypeActions/AssetTypeActions_DreamGradientAsset.h"

#include "Core/DreamGradientAsset.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DreamGradientAsset"

FAssetTypeActions_DreamGradientAsset::FAssetTypeActions_DreamGradientAsset(EAssetTypeCategories::Type InAssetCategory)
	: AssetCategory(InAssetCategory)
{
}

bool FAssetTypeActions_DreamGradientAsset::CanFilter()
{
	return true;
}

void FAssetTypeActions_DreamGradientAsset::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);

	TArray<TWeakObjectPtr<UDreamGradientAsset>> Gradients;
	for (UObject* Object : InObjects)
	{
		if (UDreamGradientAsset* Gradient = Cast<UDreamGradientAsset>(Object))
		{
			Gradients.Add(Gradient);
		}
	}
	// The spelling a .dui, a project preset and a rich text's <gradient=...> all take; one line per asset when several are selected.
	MenuBuilder.AddMenuEntry(
		LOCTEXT("CopyAsCss", "Copy as CSS"),
		LOCTEXT("CopyAsCss_ToolTip", "Copy the gradient as CSS, the way a .dui file, a project gradient preset or a rich text's <gradient=...> writes it."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
		FUIAction(
			FExecuteAction::CreateLambda([Gradients]()
			{
				TArray<FString> Lines;
				for (const TWeakObjectPtr<UDreamGradientAsset>& Gradient : Gradients)
				{
					if (const UDreamGradientAsset* Asset = Gradient.Get())
					{
						Lines.Add(Asset->GetGradient().ToCss());
					}
				}
				FPlatformApplicationMisc::ClipboardCopy(*FString::Join(Lines, LINE_TERMINATOR));
			}),
			FCanExecuteAction::CreateLambda([Gradients]() { return Gradients.Num() > 0; })));
}

uint32 FAssetTypeActions_DreamGradientAsset::GetCategories()
{
	return AssetCategory;
}

FText FAssetTypeActions_DreamGradientAsset::GetName() const
{
	return LOCTEXT("Name", "Dream Gradient");
}

UClass* FAssetTypeActions_DreamGradientAsset::GetSupportedClass() const
{
	return UDreamGradientAsset::StaticClass();
}

FColor FAssetTypeActions_DreamGradientAsset::GetTypeColor() const
{
	return FColor(232, 182, 74);
}

bool FAssetTypeActions_DreamGradientAsset::HasActions(const TArray<UObject*>& InObjects) const
{
	return true;
}

#undef LOCTEXT_NAMESPACE
