// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUITextData.h"
#include "DreamUICaptureLibrary.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Utils/DreamUIUtils.h"

#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "RHI/DreamPixelProbe.h"

/*
 * The picture a player sees: a play session's viewport, read back with the scene under it and the screen-space UI
 * over it, the way DreamUI.Capture writes it. Where the pixel tests look at a canvas's own target, this is the path a
 * game's HUD takes -- drawn into the view family's target after the scene -- and the picture it leaves is written
 * to Saved/DreamGUITests/Captures for a person to look at.
 *
 * On a real RHI only: under -nullrhi a viewport has no pixels to read.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieViewportPictureTest,
	"DreamGUI.Pie.RHI.AScreenSpacePanelIsInThePictureOfThePlaySessionsViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamDriverPieViewportPictureTest::RunTest(const FString& Parameters)
{
	static const FColor Magenta = FColor(255, 0, 255, 255);
	TSharedRef<TWeakObjectPtr<UDreamWidget>> Made = MakeShared<TWeakObjectPtr<UDreamWidget>>();

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Made](FDreamDriverPieRig& InRig)
	{
		UDreamWidget* Block = InRig.MakeWidgetWithVisual(UDreamTexture::StaticClass(), TEXT("MagentaBlock"), nullptr, FVector2D(240.0, 160.0));
		if (UDreamTexture* Visual = Block != nullptr ? Cast<UDreamTexture>(Block->GetVisual()) : nullptr)
		{
			Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
			Visual->SetColor(Magenta);
		}
		*Made = Block;
		UDreamWidget* Label = InRig.MakeWidgetWithVisual(UDreamText::StaticClass(), TEXT("Label"), nullptr, FVector2D(400.0, 48.0), FVector2D(0.0, -130.0));
		if (UDreamText* Text = Label != nullptr ? Cast<UDreamText>(Label->GetVisual()) : nullptr)
		{
			Text->SetText(FText::FromString(TEXT("DreamGUI in a play session")));
			Text->SetFontSize(28.0f);
			Text->SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
			Text->SetColor(FColor::White);
		}
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	// The canvas batches on a worker and the scene render trails the tick; a handful of frames covers both.
	Steps.WaitFrames(6);
	Steps.Then([this, Made](FDreamDriverContext& InContext)
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		FViewport* Viewport = UDreamUICaptureLibrary::FindViewportOf(InContext.World);
		if (!TestTrue(TEXT("The play session's viewport reads back"), UDreamUICaptureLibrary::ReadViewportPixels(Viewport, Pixels, Size)))
		{
			return;
		}
		const FString File = FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("Pie_ScreenSpacePanel"));
		AddInfo(FString::Printf(TEXT("The play session's viewport, %dx%d, is in %s."), Size.X, Size.Y, File.IsEmpty() ? TEXT("(not written)") : *File));
		const TOptional<FVector2D> Centre = FDreamDriverProjection::WidgetCentrePixel(Made->Get());
		if (TestTrue(TEXT("The panel has a pixel on the viewport"), Centre.IsSet()))
		{
			const FIntPoint Pixel(FMath::FloorToInt32(Centre.GetValue().X), FMath::FloorToInt32(Centre.GetValue().Y));
			FDreamPixelProbe::ExpectColorAt(*this, Pixels, Size, Pixel, Magenta, 8, TEXT("the centre of the magenta panel, in the viewport's picture"));
		}
		TestTrue(TEXT("...and the picture is not the panel alone: the scene is under it"),
			FDreamPixelProbe::CountColor(Pixels, Size, FIntRect(0, 0, Size.X, Size.Y), Magenta, 8) < Size.X * Size.Y);
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
