// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Widget/SDreamUIEyedropper.h"

#include "Designer/SDreamWidgetDesignerViewport.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "RenderingThread.h"
#include "Slate/SceneViewport.h"
#include "Styling/AppStyle.h"
#include "UnrealClient.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "SDreamUIEyedropper"

namespace DreamUIEyedropperLocal
{
	const FName DesignerViewportType(TEXT("SDreamWidgetDesignerViewport"));
	const FName ViewportType(TEXT("SViewport"));

	FDreamUIPickedColor FromSRGB(const FColor& InSRGB, EDreamUIPickedFrom InSource)
	{
		FDreamUIPickedColor Color;
		Color.SRGB = FColor(InSRGB.R, InSRGB.G, InSRGB.B, 255);
		Color.Linear = FLinearColor::FromSRGBColor(Color.SRGB);
		Color.Source = InSource;
		return Color;
	}
}

bool FDreamUIEyedropper::FindDesignerViewportPixel(const FVector2D& InScreenPosition, FViewport*& OutViewport, FIntPoint& OutPixel)
{
	using namespace DreamUIEyedropperLocal;
	OutViewport = nullptr;
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}
	FSlateApplication& Slate = FSlateApplication::Get();
	const FWidgetPath Path = Slate.LocateWindowUnderMouse(InScreenPosition, Slate.GetInteractiveTopLevelWindows());
	if (!Path.IsValid())
	{
		return false;
	}
	// The designer viewport, then the viewport widget inside it the scene is drawn into: a point over the toolbar or a
	// ruler is over the designer viewport but not over its picture.
	int32 DesignerIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
	{
		if (Path.Widgets[Index].Widget->GetType() == DesignerViewportType)
		{
			DesignerIndex = Index;
		}
	}
	if (DesignerIndex == INDEX_NONE)
	{
		return false;
	}
	for (int32 Index = DesignerIndex + 1; Index < Path.Widgets.Num(); ++Index)
	{
		const FArrangedWidget& Arranged = Path.Widgets[Index];
		if (Arranged.Widget->GetType() != ViewportType)
		{
			continue;
		}
		const TSharedRef<SDreamWidgetDesignerViewport> Designer =
			StaticCastSharedRef<SDreamWidgetDesignerViewport>(Path.Widgets[DesignerIndex].Widget);
		const TSharedPtr<FSceneViewport> SceneViewport = Designer->GetSceneViewport();
		if (!SceneViewport.IsValid())
		{
			return false;
		}
		const FVector2D LocalSize = Arranged.Geometry.GetLocalSize();
		const FIntPoint ViewportSize = SceneViewport->GetSizeXY();
		if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0 || ViewportSize.X <= 0 || ViewportSize.Y <= 0)
		{
			return false;
		}
		const FVector2D Local = Arranged.Geometry.AbsoluteToLocal(InScreenPosition);
		OutPixel = FIntPoint(
			FMath::Clamp(FMath::FloorToInt32(Local.X / LocalSize.X * ViewportSize.X), 0, ViewportSize.X - 1),
			FMath::Clamp(FMath::FloorToInt32(Local.Y / LocalSize.Y * ViewportSize.Y), 0, ViewportSize.Y - 1));
		OutViewport = SceneViewport.Get();
		return true;
	}
	return false;
}

bool FDreamUIEyedropper::SampleViewport(FViewport* InViewport, FIntPoint InPixel, FDreamUIPickedColor& OutColor)
{
	if (InViewport == nullptr)
	{
		return false;
	}
	const FIntPoint Size = InViewport->GetSizeXY();
	if (InPixel.X < 0 || InPixel.Y < 0 || InPixel.X >= Size.X || InPixel.Y >= Size.Y)
	{
		return false;
	}
	FlushRenderingCommands();
	// A viewport with no target of its own between frames (one drawn straight to its window) has nothing to read here.
	if (!InViewport->GetRenderTargetTexture().IsValid())
	{
		return false;
	}
	TArray<FColor> Pixels;
	if (!InViewport->ReadPixels(Pixels, FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX), FIntRect(InPixel, InPixel + FIntPoint(1, 1)))
		|| Pixels.IsEmpty())
	{
		return false;
	}
	OutColor = DreamUIEyedropperLocal::FromSRGB(Pixels[0], EDreamUIPickedFrom::DesignerViewport);
	return true;
}

bool FDreamUIEyedropper::Sample(const FVector2D& InScreenPosition, FDreamUIPickedColor& OutColor)
{
	FViewport* Viewport = nullptr;
	FIntPoint Pixel = FIntPoint::ZeroValue;
	if (FindDesignerViewportPixel(InScreenPosition, Viewport, Pixel) && SampleViewport(Viewport, Pixel, OutColor))
	{
		return true;
	}
	// Anywhere else, the screen's pixel. Its channels are the encoded values over 255 (gamma 1 leaves them as they are).
	const FLinearColor Screen = FPlatformApplicationMisc::GetScreenPixelColor(InScreenPosition, 1.0f);
	OutColor = DreamUIEyedropperLocal::FromSRGB(Screen.QuantizeRound(), EDreamUIPickedFrom::Screen);
	return true;
}

void SDreamUIEyedropperButton::Construct(const FArguments& InArgs)
{
	OnBeginDelegate = InArgs._OnBegin;
	OnPreviewDelegate = InArgs._OnPreview;
	OnPickedDelegate = InArgs._OnPicked;
	OnCancelledDelegate = InArgs._OnCancelled;

	const TAttribute<FText> ToolTip = InArgs._ToolTipText.IsSet()
		? InArgs._ToolTipText
		: TAttribute<FText>(LOCTEXT("Tooltip", "Pick a colour: click here, then click a pixel of the designer viewport -- as it is drawn, after its effects -- or anywhere on the screen. Escape or a right click cancels."));

	ChildSlot
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(1.0f))
		.ToolTipText(ToolTip)
		.OnClicked(this, &SDreamUIEyedropperButton::OnButtonClicked)
		[
			SNew(SBox)
			.WidthOverride(16.0f)
			.HeightOverride(16.0f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.EyeDropper"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
				// While picking, the colour under the pointer, over the icon.
				+ SOverlay::Slot()
				[
					SNew(SColorBlock)
					.Color(this, &SDreamUIEyedropperButton::GetSwatchColor)
					.Visibility(this, &SDreamUIEyedropperButton::GetSwatchVisibility)
					.ShowBackgroundForAlpha(false)
				]
			]
		]
	];
}

FReply SDreamUIEyedropperButton::OnButtonClicked()
{
	if (bPicking)
	{
		CancelPicking();
		return FReply::Handled();
	}
	BeginPicking();
	return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::SetDirectly);
}

void SDreamUIEyedropperButton::BeginPicking()
{
	if (bPicking)
	{
		return;
	}
	bPicking = true;
	bHasSample = false;
	OnBeginDelegate.ExecuteIfBound();
}

bool SDreamUIEyedropperButton::PickAt(const FVector2D& InScreenPosition)
{
	if (!bPicking)
	{
		return false;
	}
	FDreamUIPickedColor Color;
	const bool bSampled = FDreamUIEyedropper::Sample(InScreenPosition, Color);
	EndPicking();
	if (!bSampled)
	{
		OnCancelledDelegate.ExecuteIfBound();
		return false;
	}
	LastSample = Color;
	OnPickedDelegate.ExecuteIfBound(Color);
	return true;
}

void SDreamUIEyedropperButton::CancelPicking()
{
	if (!bPicking)
	{
		return;
	}
	EndPicking();
	OnCancelledDelegate.ExecuteIfBound();
}

void SDreamUIEyedropperButton::EndPicking()
{
	bPicking = false;
	bHasSample = false;
	if (HasMouseCapture() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}
}

FReply SDreamUIEyedropperButton::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bPicking)
	{
		return SCompoundWidget::OnMouseMove(MyGeometry, MouseEvent);
	}
	FDreamUIPickedColor Color;
	if (FDreamUIEyedropper::Sample(MouseEvent.GetScreenSpacePosition(), Color))
	{
		const bool bChanged = !bHasSample || Color.SRGB != LastSample.SRGB;
		LastSample = Color;
		bHasSample = true;
		if (bChanged)
		{
			OnPreviewDelegate.ExecuteIfBound(Color);
		}
	}
	return FReply::Handled();
}

FReply SDreamUIEyedropperButton::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bPicking)
	{
		return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
	}
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		PickAt(MouseEvent.GetScreenSpacePosition());
	}
	else
	{
		CancelPicking();
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SDreamUIEyedropperButton::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (bPicking && InKeyEvent.GetKey() == EKeys::Escape)
	{
		CancelPicking();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SDreamUIEyedropperButton::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// Another window took the pointer (an alt-tab, a modal): the pick is abandoned, not made.
	if (bPicking)
	{
		bPicking = false;
		bHasSample = false;
		OnCancelledDelegate.ExecuteIfBound();
	}
}

FLinearColor SDreamUIEyedropperButton::GetSwatchColor() const
{
	return LastSample.Linear;
}

EVisibility SDreamUIEyedropperButton::GetSwatchVisibility() const
{
	return bPicking && bHasSample ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
