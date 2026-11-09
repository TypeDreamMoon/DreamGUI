// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DetailCustomization/PropertyType/DreamCurveCustomizations.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DreamTweener.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamCurveCustomizations"

TArray<FDreamCurvePoint> DreamCurveCustomizations::PointsOf(const FDreamStyleContour& InContour)
{
	TArray<FDreamCurvePoint> Out;
	if (InContour.Shape == EDreamStyleContourShape::Custom && InContour.Points.Num() >= 2)
	{
		for (const FDreamStyleContourPoint& Point : InContour.Points)
		{
			Out.Emplace(Point.Input, Point.Output, Point.bCorner);
		}
		return Out;
	}
	// A named shape, or a custom one with too few points to be a curve: sampled finely enough to keep its bumps.
	const int32 Count = InContour.Shape == EDreamStyleContourShape::Linear || InContour.Shape == EDreamStyleContourShape::Custom ? 2 : 9;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float X = static_cast<float>(Index) / (Count - 1);
		Out.Emplace(X, InContour.Evaluate(X));
	}
	return Out;
}

FDreamStyleContour DreamCurveCustomizations::ContourOf(const TArray<FDreamCurvePoint>& InPoints)
{
	FDreamStyleContour Out;
	Out.Shape = EDreamStyleContourShape::Custom;
	for (const FDreamCurvePoint& Point : InPoints)
	{
		Out.Points.Emplace(Point.X, Point.Y, Point.bCorner);
	}
	return Out;
}

float DreamCurveCustomizations::EvaluateEase(EDreamTweenEase InEase, float InT)
{
	const FDreamTweenFunction Function = UDreamTweener::GetEaseFunction(InEase);
	// Penner's order: change, start, time, duration.
	return Function.IsBound() ? Function.Execute(1.0f, 0.0f, InT, 1.0f) : InT;
}

TSharedRef<IPropertyTypeCustomization> FDreamContourCustomization::MakeInstance()
{
	return MakeShareable(new FDreamContourCustomization);
}

const FDreamStyleContour& FDreamContourCustomization::Current() const
{
	if (CachedFrame != GFrameCounter)
	{
		CachedFrame = GFrameCounter;
		Cached = FDreamStyleContour();
		TArray<void*> Raw;
		if (Handle.IsValid() && Handle->IsValidHandle())
		{
			Handle->AccessRawData(Raw);
		}
		if (Raw.Num() > 0 && Raw[0] != nullptr)
		{
			Cached = *static_cast<const FDreamStyleContour*>(Raw[0]);
		}
	}
	return Cached;
}

void FDreamContourCustomization::Commit(const TSharedRef<IPropertyHandle>& InHandle, const FDreamStyleContour& InContour, bool bInteractive)
{
	FString Text;
	FDreamStyleContour::StaticStruct()->ExportText(Text, &InContour, nullptr, nullptr, PPF_None, nullptr);
	InHandle->SetValueFromFormattedString(Text, bInteractive ? EPropertyValueSetFlags::InteractiveChange : EPropertyValueSetFlags::DefaultFlags);
}

void FDreamContourCustomization::BeginEdit()
{
	if (!bEditing && GEditor != nullptr)
	{
		bEditing = true;
		GEditor->BeginTransaction(LOCTEXT("EditContour", "Edit Contour"));
	}
}

void FDreamContourCustomization::EndEdit()
{
	if (bEditing && GEditor != nullptr)
	{
		GEditor->EndTransaction();
	}
	bEditing = false;
}

void FDreamContourCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Handle = PropertyHandle;
	const TWeakPtr<FDreamContourCustomization> Weak = StaticCastSharedRef<FDreamContourCustomization>(AsShared());
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(160.0f)
		[
			SNew(SComboButton)
			.IsEnabled_Lambda([Weak]() { const TSharedPtr<FDreamContourCustomization> Self = Weak.Pin(); return Self.IsValid() && Self->Handle.IsValid() && Self->Handle->IsEditable(); })
			.OnGetMenuContent_Lambda([Weak]() { const TSharedPtr<FDreamContourCustomization> Self = Weak.Pin(); return Self.IsValid() ? Self->MakePopup() : SNullWidget::NullWidget; })
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 1.0f, 6.0f, 1.0f)
				[
					SNew(SDreamCurveEditor)
					.DesiredSize(FVector2D(36.0, 24.0))
					.bShowGrid(false)
					.Evaluate([Weak](float X) { const TSharedPtr<FDreamContourCustomization> Self = Weak.Pin(); return Self.IsValid() ? Self->Current().Evaluate(X) : X; })
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([Weak]()
					{
						const TSharedPtr<FDreamContourCustomization> Self = Weak.Pin();
						return Self.IsValid() ? StaticEnum<EDreamStyleContourShape>()->GetDisplayNameTextByValue(static_cast<int64>(Self->Current().Shape)) : FText::GetEmpty();
					})
				]
			]
		];
}

TSharedRef<SWidget> FDreamContourCustomization::MakePopup()
{
	const TWeakPtr<FDreamContourCustomization> Weak = StaticCastSharedRef<FDreamContourCustomization>(AsShared());
	TSharedRef<SUniformGridPanel> Presets = SNew(SUniformGridPanel).SlotPadding(FMargin(2.0f));
	const UEnum* Enum = StaticEnum<EDreamStyleContourShape>();
	int32 Slot = 0;
	for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
	{
		const EDreamStyleContourShape Shape = static_cast<EDreamStyleContourShape>(Enum->GetValueByIndex(Index));
		if (Shape == EDreamStyleContourShape::Custom)
		{
			continue;
		}
		FDreamStyleContour Preset;
		Preset.Shape = Shape;
		Presets->AddSlot(Slot % 6, Slot / 6)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(2.0f))
			.ToolTipText(Enum->GetDisplayNameTextByIndex(Index))
			.OnClicked_Lambda([Weak, Preset]()
			{
				if (const TSharedPtr<FDreamContourCustomization> Self = Weak.Pin(); Self.IsValid() && Self->Handle.IsValid() && Self->Handle->IsValidHandle())
				{
					Commit(Self->Handle.ToSharedRef(), Preset, false);
				}
				return FReply::Handled();
			})
			[
				SNew(SDreamCurveEditor)
				.DesiredSize(FVector2D(34.0, 34.0))
				.bShowGrid(false)
				.Evaluate([Preset](float X) { return Preset.Evaluate(X); })
			]
		];
		++Slot;
	}
	return SNew(SBox)
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				Presets
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 8.0f, 0.0f, 4.0f)
			[
				SNew(SDreamCurveEditor)
				.DesiredSize(FVector2D(220.0, 220.0))
				.bEditable(true)
				.Evaluate([Weak](float X) { const TSharedPtr<FDreamContourCustomization> Self = Weak.Pin(); return Self.IsValid() ? Self->Current().Evaluate(X) : X; })
				.Points_Lambda([Weak]()
				{
					const TSharedPtr<FDreamContourCustomization> Self = Weak.Pin();
					return Self.IsValid() ? DreamCurveCustomizations::PointsOf(Self->Current()) : TArray<FDreamCurvePoint>();
				})
				.OnBeginEdit_Lambda([Weak]() { if (const TSharedPtr<FDreamContourCustomization> Self = Weak.Pin()) { Self->BeginEdit(); } })
				.OnPointsChanged_Lambda([Weak](const TArray<FDreamCurvePoint>& InPoints, bool bInteractive)
				{
					if (const TSharedPtr<FDreamContourCustomization> Self = Weak.Pin(); Self.IsValid() && Self->Handle.IsValid() && Self->Handle->IsValidHandle())
					{
						Commit(Self->Handle.ToSharedRef(), DreamCurveCustomizations::ContourOf(InPoints), bInteractive);
						Self->CachedFrame = MAX_uint64;
					}
				})
				.OnEndEdit_Lambda([Weak]() { if (const TSharedPtr<FDreamContourCustomization> Self = Weak.Pin()) { Self->EndEdit(); } })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("ContourHint", "Click to add a point, drag one off to remove it, Alt+click for a corner."))
			]
		];
}

TSharedRef<IPropertyTypeCustomization> FDreamEaseCustomization::MakeInstance()
{
	return MakeShareable(new FDreamEaseCustomization);
}

TOptional<uint8> FDreamEaseCustomization::GetCurrent() const
{
	uint8 Value = 0;
	if (Handle.IsValid() && Handle->IsValidHandle() && Handle->GetValue(Value) == FPropertyAccess::Success)
	{
		return Value;
	}
	return {};
}

void FDreamEaseCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Handle = PropertyHandle;
	const TWeakPtr<FDreamEaseCustomization> Weak = StaticCastSharedRef<FDreamEaseCustomization>(AsShared());
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(160.0f)
		[
			SNew(SComboButton)
			.IsEnabled_Lambda([Weak]() { const TSharedPtr<FDreamEaseCustomization> Self = Weak.Pin(); return Self.IsValid() && Self->Handle.IsValid() && Self->Handle->IsEditable(); })
			.OnGetMenuContent_Lambda([Weak]() { const TSharedPtr<FDreamEaseCustomization> Self = Weak.Pin(); return Self.IsValid() ? Self->MakeMenu() : SNullWidget::NullWidget; })
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 1.0f, 6.0f, 1.0f)
				[
					SNew(SDreamCurveEditor)
					.DesiredSize(FVector2D(30.0, 20.0))
					.bShowGrid(false)
					.Evaluate([Weak](float T)
					{
						const TSharedPtr<FDreamEaseCustomization> Self = Weak.Pin();
						const TOptional<uint8> Current = Self.IsValid() ? Self->GetCurrent() : TOptional<uint8>();
						return Current.IsSet() ? DreamCurveCustomizations::EvaluateEase(static_cast<EDreamTweenEase>(Current.GetValue()), T) : T;
					})
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([Weak]()
					{
						const TSharedPtr<FDreamEaseCustomization> Self = Weak.Pin();
						const TOptional<uint8> Current = Self.IsValid() ? Self->GetCurrent() : TOptional<uint8>();
						return Current.IsSet() ? StaticEnum<EDreamTweenEase>()->GetDisplayNameTextByValue(Current.GetValue()) : NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values");
					})
				]
			]
		];
}

TSharedRef<SWidget> FDreamEaseCustomization::MakeMenu()
{
	const TWeakPtr<FDreamEaseCustomization> Weak = StaticCastSharedRef<FDreamEaseCustomization>(AsShared());
	const UEnum* Enum = StaticEnum<EDreamTweenEase>();
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(2.0f));
	int32 Slot = 0;
	for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
	{
		if (Enum->HasMetaData(TEXT("Hidden"), Index))
		{
			continue;
		}
		const EDreamTweenEase Ease = static_cast<EDreamTweenEase>(Enum->GetValueByIndex(Index));
		Grid->AddSlot(Slot % 4, Slot / 4)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(4.0f, 2.0f))
			.OnClicked_Lambda([Weak, Enum, Index]()
			{
				if (const TSharedPtr<FDreamEaseCustomization> Self = Weak.Pin(); Self.IsValid() && Self->Handle.IsValid() && Self->Handle->IsValidHandle())
				{
					Self->Handle->SetValueFromFormattedString(Enum->GetNameStringByIndex(Index));
				}
				FSlateApplication::Get().DismissAllMenus();
				return FReply::Handled();
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(SDreamCurveEditor)
					.DesiredSize(FVector2D(48.0, 36.0))
					.bShowGrid(false)
					.Evaluate([Ease](float T) { return DreamCurveCustomizations::EvaluateEase(Ease, T); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(Enum->GetDisplayNameTextByIndex(Index))
				]
			]
		];
		++Slot;
	}
	return SNew(SBox)
		.MaxDesiredHeight(560.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			.Padding(6.0f)
			[
				Grid
			]
		];
}

#undef LOCTEXT_NAMESPACE
