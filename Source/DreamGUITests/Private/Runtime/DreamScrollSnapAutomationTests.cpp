// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamScrollBox.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "HAL/IConsoleManager.h"

#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverTypes.h"

/*
 * SCROLLING IN WHOLE DEVICE PIXELS, BEHIND DreamGUI.Scroll.SnapToDevicePixels.
 *
 * Small text drawn from coverage glyphs keeps its pixels through a move of whole device pixels and has to be painted
 * again for any other, so a smooth scroll repainted every label it moved on every frame. With the console variable on,
 * a scroll view on a 2D canvas puts its content on the nearest whole device pixel along the axis it scrolls, while
 * its offset -- what the box reads back, what its bar shows, what a fling integrates -- stays the one asked for.
 * Off, the default, the content goes exactly where the offset puts it, as it always did.
 *
 * The canvas is scaled two-thirds (a 1920x1080 reference on a 1280x720 screen), so a whole unit is not a whole
 * pixel and an offset like 37.3 lands between pixels.
 */
namespace DreamScrollSnapTestLocal
{
	const TCHAR* const SnapVariableName = TEXT("DreamGUI.Scroll.SnapToDevicePixels");

	/** The console variable at InValue for as long as this lives, and put back after. */
	struct FScopedSnapToDevicePixels
	{
		IConsoleVariable* Variable = nullptr;
		int32 Previous = 0;

		explicit FScopedSnapToDevicePixels(int32 InValue)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(SnapVariableName);
			if (Variable != nullptr)
			{
				Previous = Variable->GetInt();
				Variable->Set(InValue, ECVF_SetByCode);
			}
		}

		~FScopedSnapToDevicePixels()
		{
			if (Variable != nullptr)
			{
				Variable->Set(Previous, ECVF_SetByCode);
			}
		}
	};

	/**
	 * Where InWidget's origin is drawn on the rig's 1280x720 screen, in device pixels from its top-left corner, down
	 * positive. Read off the root canvas's widget, which spans the screen, rather than through the projection the
	 * scroll view snaps with: two roads to the same pixel.
	 */
	FVector2D DevicePixelOf(FDreamDriverRig& InRig, const UDreamWidget* InWidget)
	{
		const UDreamWidget* Root = InRig.Root();
		const FVector Local = Root->GetWorldTransform().InverseTransformPosition(InWidget->GetWorldLocation());
		const double PixelsPerUnitX = 1280.0 / Root->GetWidth();
		const double PixelsPerUnitY = 720.0 / Root->GetHeight();
		return FVector2D((Local.Y - Root->GetLocalSpaceLeft()) * PixelsPerUnitX, (Root->GetLocalSpaceTop() - Local.Z) * PixelsPerUnitY);
	}

	bool IsWholePixel(double InPixels)
	{
		return FMath::Abs(InPixels - FMath::RoundToDouble(InPixels)) < 1.0e-3;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollSnapToDevicePixelsTest,
	"DreamGUI.ScrollBox.SnappingToDevicePixelsLandsTheContentOnWholePixelsAndOffLeavesItWhereTheOffsetSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollSnapToDevicePixelsTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollSnapTestLocal;
	if (!TestNotNull(TEXT("The console variable exists"), IConsoleManager::Get().FindConsoleVariable(SnapVariableName)))
	{
		return false;
	}
	FDreamRigOptions Options;
	Options.ViewportSize = FIntPoint(1280, 720);
	Options.CanvasScaleMode = EDreamCanvasScaleMode::ScaleWithScreenSize;
	Options.ReferenceResolution = FVector2D(1920.0, 1080.0);
	Options.MatchFromWidthToHeight = 1.0f;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	Rig.BindTest(this);
	UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(400.0, 300.0));
	if (!TestTrue(TEXT("The rig and the box came up"), Rig.IsUsable() && Box != nullptr && Box->GetContentNode() != nullptr))
	{
		return false;
	}
	for (int32 RowIndex = 0; RowIndex < 20; ++RowIndex)
	{
		Rig.MakeWidget(FString::Printf(TEXT("Row%02d"), RowIndex), Box->GetContentNode(), FVector2D(380.0, 60.0));
	}
	Box->RefreshContentExtent();
	Rig.PumpFrames(2);
	if (!TestEqual(TEXT("The canvas is scaled two-thirds"), Rig.RootCanvas()->GetCanvasScale(), 2.0f / 3.0f, 1.0e-3f))
	{
		return false;
	}
	const double PixelsPerUnit = 1280.0 / Rig.Root()->GetWidth();
	const UDreamWidget* Content = Box->GetContentNode();

	double StartPixel = 0.0;
	{
		FScopedSnapToDevicePixels Off(0);
		Box->SetScrollOffset(0.0f);
		Rig.PumpFrames(1);
		StartPixel = DevicePixelOf(Rig, Content).Y;
		Box->SetScrollOffset(37.3f);
		Rig.PumpFrames(1);
		TestEqual(TEXT("Off, the box reads back the offset"), Box->GetScrollOffset(), 37.3f, 1.0e-3f);
		TestEqual(TEXT("...and the content moved exactly that far, a fraction of a pixel included"),
			StartPixel - DevicePixelOf(Rig, Content).Y, 37.3 * PixelsPerUnit, 1.0e-3);
	}

	{
		FScopedSnapToDevicePixels On(1);
		for (const float Offset : { 37.3f, 81.7f, 12.05f })
		{
			Box->SetScrollOffset(Offset);
			Rig.PumpFrames(1);
			const double Pixel = DevicePixelOf(Rig, Content).Y;
			TestTrue(FString::Printf(TEXT("On, offset %.2f puts the content on a whole pixel"), Offset), IsWholePixel(Pixel));
			TestTrue(FString::Printf(TEXT("...the nearest one to where the offset says (%.2f)"), Offset),
				FMath::Abs(Pixel - (StartPixel - Offset * PixelsPerUnit)) <= 0.5 + 1.0e-3);
			TestEqual(FString::Printf(TEXT("...while the box reads back the offset asked for (%.2f)"), Offset), Box->GetScrollOffset(), Offset, 1.0e-3f);
		}
	}

	{
		// Back off: the next move goes exactly where its offset says again.
		FScopedSnapToDevicePixels Off(0);
		Box->SetScrollOffset(37.3f);
		Rig.PumpFrames(1);
		TestEqual(TEXT("Turned off again, the content goes exactly where the offset says"),
			StartPixel - DevicePixelOf(Rig, Content).Y, 37.3 * PixelsPerUnit, 1.0e-3);
	}
	return true;
}

#endif
