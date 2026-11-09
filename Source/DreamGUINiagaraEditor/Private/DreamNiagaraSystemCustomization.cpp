// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamNiagaraSystemCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DreamNiagaraMaterialConverter.h"
#include "DreamNiagaraSystem.h"
#include "Engine/Texture.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamNiagaraSystemCustomization"

TSharedRef<IDetailCustomization> FDreamNiagaraSystemCustomization::MakeInstance()
{
	return MakeShared<FDreamNiagaraSystemCustomization>();
}

FText FDreamNiagaraSystemCustomization::DescribeRenderer(const FDreamNiagaraRendererInfo& InInfo)
{
	const FString Where = FString::Printf(TEXT("%s / %d"), *InInfo.EmitterName.ToString(), InInfo.RendererIndex);
	if (!InInfo.IsDrawn())
	{
		return FText::Format(LOCTEXT("NotDrawn", "{0}  {1}: not drawn."), FText::FromString(Where), FText::FromString(InInfo.RendererType));
	}
	const FText Kind = InInfo.Kind == EDreamNiagaraRendererKind::Sprite ? LOCTEXT("Sprite", "sprites") : LOCTEXT("Ribbon", "ribbons");
	const FString Texture = InInfo.InferredTexture.IsValid() ? InInfo.InferredTexture->GetName() : TEXT("a soft default (the material samples no texture)");
	const FString Blend = StaticEnum<EDreamUIBlendMode>()->GetDisplayNameTextByValue(static_cast<int64>(InInfo.InferredBlendMode)).ToString();
	return FText::Format(LOCTEXT("Drawn", "{0}  drawn as {1}{2}, built-in: {3}, {4}"), FText::FromString(Where), Kind,
		InInfo.bLocalSpace ? LOCTEXT("Local", " (local space)") : FText::GetEmpty(), FText::FromString(Texture), FText::FromString(Blend));
}

void FDreamNiagaraSystemCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	Visuals.Reset();
	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		if (UDreamNiagaraSystem* Visual = Cast<UDreamNiagaraSystem>(Object.Get()))
		{
			Visuals.Add(Visual);
		}
	}
	if (Visuals.Num() == 0)
	{
		return;
	}
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("DreamGUI-Niagara"));
	// What each renderer is drawn as, or why it is not: of the first element, when several are selected.
	UDreamNiagaraSystem* First = Visuals[0].Get();
	TSharedRef<SVerticalBox> Report = SNew(SVerticalBox);
	const TArray<FDreamNiagaraRendererInfo>& Renderers = First->GetRendererInfo();
	if (Renderers.Num() == 0)
	{
		Report->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont()).Text(LOCTEXT("NoRenderers", "No system, or a system with no enabled renderer."))
		];
	}
	for (const FDreamNiagaraRendererInfo& Info : Renderers)
	{
		Report->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(Info.IsDrawn() ? FSlateColor::UseForeground() : FSlateColor(FLinearColor(1.0f, 0.55f, 0.25f)))
			.Text(DescribeRenderer(Info))
		];
		for (const FText& Note : Info.Notes)
		{
			Report->AddSlot().AutoHeight().Padding(16.0f, 0.0f, 0.0f, 1.0f)
			[
				SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont()).AutoWrapText(true).Text(Note)
			];
		}
	}
	Category.AddCustomRow(LOCTEXT("Renderers", "Renderers"))
		.NameContent()
		[
			SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont()).Text(LOCTEXT("RenderersLabel", "Renderers"))
			.ToolTipText(LOCTEXT("RenderersTip", "What DreamGUI draws of the system: CPU emitters' sprite and ribbon renderers, one draw call each."))
		]
		.ValueContent()
		.MinDesiredWidth(360.0f)
		[
			Report
		];
	Category.AddCustomRow(LOCTEXT("Convert", "Convert Particle Materials"))
		.ValueContent()
		[
			SNew(SButton)
			.Text(LOCTEXT("ConvertButton", "Convert Particle Materials"))
			.ToolTipText(LOCTEXT("ConvertTip", "Copy each drawn renderer's particle material as a DreamGUI UI material (beside it, _DreamUI; engine and plugin materials into /Game/DreamUI/ConvertedParticleMaterials), save it, put it in the material map and shade with Material."))
			.OnClicked(this, &FDreamNiagaraSystemCustomization::ConvertMaterials)
		];
}

FReply FDreamNiagaraSystemCustomization::ConvertMaterials()
{
	FScopedTransaction Transaction(LOCTEXT("ConvertTransaction", "Convert Particle Materials"));
	TArray<FString> Lines;
	for (const TWeakObjectPtr<UDreamNiagaraSystem>& Weak : Visuals)
	{
		UDreamNiagaraSystem* Visual = Weak.Get();
		if (Visual == nullptr)
		{
			continue;
		}
		for (const FDreamNiagaraMaterialConversion& Conversion : FDreamNiagaraMaterialConverter::ConvertForVisual(Visual, /*bInSave*/ true))
		{
			const FString Source = Conversion.Source != nullptr ? Conversion.Source->GetName() : TEXT("?");
			if (Conversion.Result == nullptr)
			{
				Lines.Add(FString::Printf(TEXT("%s: %s"), *Source, *Conversion.Error));
				continue;
			}
			Lines.Add(FString::Printf(TEXT("%s -> %s"), *Source, *Conversion.Result->GetPathName()));
			for (const FString& Unconverted : Conversion.Unconverted)
			{
				Lines.Add(FString::Printf(TEXT("    not converted: %s"), *Unconverted));
			}
		}
	}
	FNotificationInfo Info(FText::FromString(Lines.Num() > 0 ? FString::Join(Lines, TEXT("\n")) : TEXT("No particle material to convert.")));
	Info.ExpireDuration = 8.0f;
	Info.bUseLargeFont = false;
	FSlateNotificationManager::Get().AddNotification(Info);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
