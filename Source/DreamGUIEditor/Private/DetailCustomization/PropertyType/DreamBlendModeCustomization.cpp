// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DetailCustomization/PropertyType/DreamBlendModeCustomization.h"

#include "Core/DreamUIBlendMath.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamBlendModeCustomization"

namespace DreamBlendModeThumbnailsLocal
{
	constexpr int32 Width = 48;
	constexpr int32 Height = 32;

	struct FPicture
	{
		TStrongObjectPtr<UTexture2D> Texture;
		TUniquePtr<FSlateBrush> Brush;
	};

	TArray<FPicture>& Pictures()
	{
		static TArray<FPicture> Made;
		return Made;
	}

	FVector3f Hue(float InHue)
	{
		const FLinearColor Colour = FLinearColor(InHue * 360.0f, 0.7f, 0.95f).HSVToLinearRGB();
		return FVector3f(Colour.R, Colour.G, Colour.B);
	}
}

FIntPoint FDreamBlendModeThumbnails::GetSize()
{
	return FIntPoint(DreamBlendModeThumbnailsLocal::Width, DreamBlendModeThumbnailsLocal::Height);
}

void FDreamBlendModeThumbnails::Render(EDreamUIBlendMode InMode, TArray<FColor>& OutPixels)
{
	using namespace DreamBlendModeThumbnailsLocal;
	OutPixels.SetNumUninitialized(Width * Height);
	const FVector2f Centre(Width * 0.5f, Height * 0.5f);
	const float Radius = Height * 0.4f;
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const float U = (X + 0.5f) / Width;
			const float V = (Y + 0.5f) / Height;
			// What is behind: hues across the top two thirds, black to white across the bottom third.
			const FVector3f Backdrop = V < 0.66f ? Hue(U) : FVector3f(U, U, U);
			// The element: a disc, warm at the top and cool at the bottom, antialiased at its edge.
			const float Distance = FVector2f::Distance(FVector2f(X + 0.5f, Y + 0.5f), Centre);
			float Coverage = FMath::Clamp(Radius - Distance + 0.5f, 0.0f, 1.0f);
			const FVector3f Element = FMath::Lerp(FVector3f(1.0f, 0.55f, 0.15f), FVector3f(0.35f, 0.25f, 0.95f), V);
			EDreamUIBlendMode Mode = InMode;
			if (InMode == EDreamUIBlendMode::Dissolve)
			{
				// Dissolve keeps a pixel or drops it by the element's alpha: at a partial alpha, so that it shows.
				Coverage = FDreamUIBlendMath::DissolveAlpha(Coverage * 0.55f, X, Y);
				Mode = EDreamUIBlendMode::Alpha;
			}
			const FVector4f Out = FDreamUIBlendMath::ReferenceComposite(Mode, Backdrop, 1.0f, Element, Coverage);
			const float Alpha = FMath::Max(Out.W, KINDA_SMALL_NUMBER);
			OutPixels[Y * Width + X] = FColor(
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Out.X / Alpha * 255.0f), 0, 255)),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Out.Y / Alpha * 255.0f), 0, 255)),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Out.Z / Alpha * 255.0f), 0, 255)),
				255);
		}
	}
}

const FSlateBrush* FDreamBlendModeThumbnails::Get(EDreamUIBlendMode InMode)
{
	using namespace DreamBlendModeThumbnailsLocal;
	TArray<FPicture>& Made = Pictures();
	if (Made.Num() == 0)
	{
		Made.SetNum(DreamUIBlend::NumModes);
		for (int32 Index = 0; Index < DreamUIBlend::NumModes; ++Index)
		{
			TArray<FColor> Pixels;
			Render(static_cast<EDreamUIBlendMode>(Index), Pixels);
			UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
			if (Texture == nullptr)
			{
				continue;
			}
			Texture->SRGB = true;
			Texture->Filter = TF_Nearest;
			void* Data = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
			Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
			Texture->UpdateResource();
			Made[Index].Texture.Reset(Texture);
			Made[Index].Brush = MakeUnique<FSlateBrush>();
			Made[Index].Brush->SetResourceObject(Texture);
			Made[Index].Brush->ImageSize = FVector2D(Width, Height);
			Made[Index].Brush->DrawAs = ESlateBrushDrawType::Image;
		}
	}
	const int32 Index = static_cast<int32>(InMode);
	return Made.IsValidIndex(Index) && Made[Index].Brush.IsValid() ? Made[Index].Brush.Get() : FAppStyle::GetNoBrush();
}

void FDreamBlendModeThumbnails::Shutdown()
{
	DreamBlendModeThumbnailsLocal::Pictures().Empty();
}

FDreamPropertyPreview::~FDreamPropertyPreview()
{
	End();
}

void FDreamPropertyPreview::Write(const TSharedRef<IPropertyHandle>& InHandle, TFunctionRef<FString(int32)> InTextFor)
{
	FProperty* Property = InHandle->GetProperty();
	TArray<UObject*> Outers;
	InHandle->GetOuterObjects(Outers);
	TArray<void*> Raw;
	InHandle->AccessRawData(Raw);
	if (Property == nullptr)
	{
		return;
	}
	if (Outers.Num() == 0)
	{
		// A struct shown on its own (a structure details view, a dialog's working copy) has no object to tell: the view is
		// told instead, as an interactive change, which is what its OnPropertyValueChanged listeners redraw on.
		for (int32 Index = 0; Index < Raw.Num(); ++Index)
		{
			if (Raw[Index] != nullptr)
			{
				Property->ImportText_Direct(*InTextFor(Index), Raw[Index], nullptr, PPF_None);
			}
		}
		InHandle->NotifyPostChange(EPropertyChangeType::Interactive);
		return;
	}
	if (Raw.Num() != Outers.Num())
	{
		return;
	}
	for (int32 Index = 0; Index < Raw.Num(); ++Index)
	{
		if (Raw[Index] == nullptr || !IsValid(Outers[Index]))
		{
			continue;
		}
		Property->ImportText_Direct(*InTextFor(Index), Raw[Index], Outers[Index], PPF_None);
		// Interactive, as a slider's drag is: the object redraws, and what listens for committed edits does not record it.
		FPropertyChangedEvent Event(Property, EPropertyChangeType::Interactive);
		Outers[Index]->PostEditChangeProperty(Event);
	}
}

void FDreamPropertyPreview::Begin(const TSharedRef<IPropertyHandle>& InHandle, const FString& InValueText)
{
	if (!IsActive())
	{
		FProperty* Property = InHandle->GetProperty();
		TArray<void*> Raw;
		InHandle->AccessRawData(Raw);
		if (Property == nullptr || Raw.Num() == 0)
		{
			return;
		}
		Originals.Reset();
		for (void* Value : Raw)
		{
			FString Text;
			if (Value != nullptr)
			{
				Property->ExportText_Direct(Text, Value, Value, nullptr, PPF_None);
			}
			Originals.Add(Text);
		}
		Handle = InHandle;
	}
	Write(InHandle, [&InValueText](int32) { return InValueText; });
}

void FDreamPropertyPreview::End()
{
	if (!IsActive())
	{
		return;
	}
	const TArray<FString> Restore = MoveTemp(Originals);
	Originals.Reset();
	// A panel that was rebuilt meanwhile took its handle with it, and the objects it pointed at with it.
	if (const TSharedPtr<IPropertyHandle> Pinned = Handle.Pin(); Pinned.IsValid() && Pinned->IsValidHandle())
	{
		Write(Pinned.ToSharedRef(), [&Restore](int32 InIndex) { return Restore.IsValidIndex(InIndex) ? Restore[InIndex] : FString(); });
	}
	Handle.Reset();
}

TSharedRef<IPropertyTypeCustomization> FDreamBlendModeCustomization::MakeInstance()
{
	return MakeShareable(new FDreamBlendModeCustomization);
}

const TArray<TArray<EDreamUIBlendMode>>& FDreamBlendModeCustomization::GetGroups()
{
	using M = EDreamUIBlendMode;
	static const TArray<TArray<M>> Groups = {
		{ M::Alpha, M::Dissolve },
		{ M::Darken, M::Multiply, M::ColorBurn, M::LinearBurn, M::DarkerColor },
		{ M::Lighten, M::Screen, M::ColorDodge, M::Additive, M::LighterColor },
		{ M::Overlay, M::SoftLight, M::HardLight, M::VividLight, M::LinearLight, M::PinLight, M::HardMix },
		{ M::Difference, M::Exclusion, M::Subtract, M::Divide },
		{ M::Hue, M::Saturation, M::Color, M::Luminosity },
	};
	return Groups;
}

FText FDreamBlendModeCustomization::GetModeName(EDreamUIBlendMode InMode)
{
	// The two modes the enum names after what the hardware does, by the names an image editor gives them.
	switch (InMode)
	{
	case EDreamUIBlendMode::Alpha: return LOCTEXT("Normal", "Normal");
	case EDreamUIBlendMode::Additive: return LOCTEXT("LinearDodge", "Linear Dodge (Add)");
	default: return StaticEnum<EDreamUIBlendMode>()->GetDisplayNameTextByValue(static_cast<int64>(InMode));
	}
}

TOptional<EDreamUIBlendMode> FDreamBlendModeCustomization::GetCurrent() const
{
	uint8 Value = 0;
	if (Handle.IsValid() && Handle->IsValidHandle() && Handle->GetValue(Value) == FPropertyAccess::Success)
	{
		return static_cast<EDreamUIBlendMode>(Value);
	}
	return {};
}

void FDreamBlendModeCustomization::Choose(EDreamUIBlendMode InMode)
{
	// Put back what the hover showed first, so that the edit is recorded from the value it really had.
	Preview.End();
	if (Handle.IsValid() && Handle->IsValidHandle())
	{
		Handle->SetValueFromFormattedString(StaticEnum<EDreamUIBlendMode>()->GetNameStringByValue(static_cast<int64>(InMode)));
	}
	FSlateApplication::Get().DismissAllMenus();
}

void FDreamBlendModeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Handle = PropertyHandle;
	const TWeakPtr<FDreamBlendModeCustomization> Weak = StaticCastSharedRef<FDreamBlendModeCustomization>(AsShared());
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(180.0f)
		[
			SNew(SComboButton)
			.IsEnabled_Lambda([Weak]()
			{
				const TSharedPtr<FDreamBlendModeCustomization> Self = Weak.Pin();
				return Self.IsValid() && Self->Handle.IsValid() && Self->Handle->IsEditable();
			})
			.OnGetMenuContent_Lambda([Weak]()
			{
				const TSharedPtr<FDreamBlendModeCustomization> Self = Weak.Pin();
				return Self.IsValid() ? Self->MakeMenu() : SNullWidget::NullWidget;
			})
			.OnMenuOpenChanged_Lambda([Weak](bool bOpen)
			{
				if (const TSharedPtr<FDreamBlendModeCustomization> Self = Weak.Pin(); Self.IsValid() && !bOpen)
				{
					Self->Preview.End();
				}
			})
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(24.0f)
					.HeightOverride(16.0f)
					[
						SNew(SImage)
						.Image_Lambda([Weak]()
						{
							const TSharedPtr<FDreamBlendModeCustomization> Self = Weak.Pin();
							const TOptional<EDreamUIBlendMode> Current = Self.IsValid() ? Self->GetCurrent() : TOptional<EDreamUIBlendMode>();
							return Current.IsSet() ? FDreamBlendModeThumbnails::Get(Current.GetValue()) : FAppStyle::GetNoBrush();
						})
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([Weak]()
					{
						const TSharedPtr<FDreamBlendModeCustomization> Self = Weak.Pin();
						const TOptional<EDreamUIBlendMode> Current = Self.IsValid() ? Self->GetCurrent() : TOptional<EDreamUIBlendMode>();
						return Current.IsSet() ? GetModeName(Current.GetValue()) : NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values");
					})
				]
			]
		];
}

TSharedRef<SWidget> FDreamBlendModeCustomization::MakeMenu()
{
	const TWeakPtr<FDreamBlendModeCustomization> Weak = StaticCastSharedRef<FDreamBlendModeCustomization>(AsShared());
	const TOptional<EDreamUIBlendMode> Current = GetCurrent();
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	const TArray<TArray<EDreamUIBlendMode>>& Groups = GetGroups();
	for (int32 Group = 0; Group < Groups.Num(); ++Group)
	{
		if (Group > 0)
		{
			Menu->AddSlot().AutoHeight().Padding(4.0f, 2.0f)[ SNew(SSeparator) ];
		}
		for (const EDreamUIBlendMode Mode : Groups[Group])
		{
			const bool bCurrent = Current.IsSet() && Current.GetValue() == Mode;
			Menu->AddSlot()
			.AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "Menu.Button")
				.ContentPadding(FMargin(6.0f, 2.0f))
				.ToolTipText(LOCTEXT("ModeTip", "Hover to see the widget drawn in this mode; click to use it."))
				.OnHovered_Lambda([Weak, Mode]()
				{
					if (const TSharedPtr<FDreamBlendModeCustomization> Self = Weak.Pin(); Self.IsValid() && Self->Handle.IsValid() && Self->Handle->IsValidHandle())
					{
						Self->Preview.Begin(Self->Handle.ToSharedRef(), StaticEnum<EDreamUIBlendMode>()->GetNameStringByValue(static_cast<int64>(Mode)));
					}
				})
				.OnUnhovered_Lambda([Weak]()
				{
					if (const TSharedPtr<FDreamBlendModeCustomization> Self = Weak.Pin())
					{
						Self->Preview.End();
					}
				})
				.OnClicked_Lambda([Weak, Mode]()
				{
					if (const TSharedPtr<FDreamBlendModeCustomization> Self = Weak.Pin())
					{
						Self->Choose(Mode);
					}
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SImage).Image(FDreamBlendModeThumbnails::Get(Mode))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f, 12.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(GetModeName(Mode))
						.Font(bCurrent ? IDetailLayoutBuilder::GetDetailFontBold() : IDetailLayoutBuilder::GetDetailFont())
					]
				]
			];
		}
	}
	return Menu;
}

#undef LOCTEXT_NAMESPACE
