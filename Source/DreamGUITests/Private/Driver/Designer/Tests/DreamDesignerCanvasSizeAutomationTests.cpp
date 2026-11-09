// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"

#include "Designer/DreamWidgetBlueprintEditor.h"//and UDreamUIDesignerSettings
#include "Engine/UserInterfaceSettings.h"

/*
 * The design canvas a screen size and the DPI preview make, held across the engine frames after them. The designer sizes
 * the canvas when a size is picked or the preview is switched; the preview's own root canvas then runs its editor tick,
 * and whatever that makes of the canvas is what the author is left looking at. A check made in the same frame as the
 * pick sees only the first.
 */
namespace DreamDesignerCanvasSizeTestLocal
{
	using namespace DreamTests;

	/** More than enough frames for every root canvas of the preview world to have run its editor tick. */
	constexpr int32 SettleFrames = 4;

	/** InDevice over the project's DPI curve at InDevice, warning on InTest when the curve leaves it as it is. */
	FIntPoint DeviceOverCurve(FAutomationTestBase& InTest, FIntPoint InDevice)
	{
		const float Curve = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(InDevice);
		if (FMath::IsNearlyEqual(Curve, 1.0f, 0.01f))
		{
			InTest.AddWarning(FString::Printf(TEXT("This project's DPI curve gives %s a scale of %.3f, so the preview has nothing to shrink."),
				*InDevice.ToString(), Curve));
		}
		return FDreamWidgetBlueprintEditor::ApplyDPIScaleToViewportSize(InDevice, Curve);
	}

	void ExpectCanvas(FAutomationTestBase& InTest, FDreamWidgetBlueprintEditor& InToolkit, FIntPoint InExpected, const FString& InWhen)
	{
		const FIntPoint Canvas = InToolkit.GetDesignerCanvasSize();
		InTest.TestTrue(FString::Printf(TEXT("%s: the design canvas is %s, expected %s"), *InWhen, *Canvas.ToString(), *InExpected.ToString()),
			Canvas == InExpected);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDPIPreviewShrinksAPickedSizeTest,
	"DreamGUI.Designer.Driver.WithTheDPIPreviewOnAScreenSizePickedIsShrunkByTheCurveAtThatSizeAndStaysSo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The DPI preview on, and two device sizes picked one after the other the way the toolbar's screen-size menu picks them:
 * each time the design canvas is that device over the project's DPI curve AT THAT DEVICE, as picked and still after the
 * frames that follow. The scale used to be the one for the size picked before, which the asset had not been told it was
 * leaving yet.
 */
bool FDreamDesignerDPIPreviewShrinksAPickedSizeTest::RunTest(const FString&)
{
	using namespace DreamDesignerCanvasSizeTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerDPIPreviewPickedSize"));
	State->Preferences = MakeShared<FScopedDesignerPreferences>();
	State->Preferences->SetPreviewDPIScale(true);
	EnqueueDesignerFrames(State, 2);
	for (const FIntPoint Device : { FIntPoint(3840, 2160), FIntPoint(2560, 1440) })
	{
		const FIntPoint Expected = DeviceOverCurve(*this, Device);
		EnqueueDesignerAction(State, [this, State, Device, Expected]()
		{
			FDreamWidgetBlueprintEditor* Toolkit = State->Driver->Toolkit();
			Toolkit->SetDesignerSizeRule(EDreamUIDesignerSizeRule::Custom);
			Toolkit->SetDesignerViewportSize(Device);
			ExpectCanvas(*this, *Toolkit, Expected, FString::Printf(TEXT("%s picked"), *Device.ToString()));
		});
		EnqueueDesignerFrames(State, SettleFrames);
		EnqueueDesignerAction(State, [this, State, Device, Expected]()
		{
			FDreamWidgetBlueprintEditor* Toolkit = State->Driver->Toolkit();
			ExpectCanvas(*this, *Toolkit, Expected, FString::Printf(TEXT("%s picked, %d frames on"), *Device.ToString(), SettleFrames));
			TestEqual(TEXT("...and the picker names the device"), Toolkit->GetDesignerViewportSize(), Device);
		});
	}
	EnqueueDesignerTeardown(State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDPIPreviewRightAfterAPickTest,
	"DreamGUI.Designer.Driver.TheDPIPreviewSwitchedOnInTheFrameASizeIsPickedStillShrinksTheCanvasOnceItTicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A 3840 by 2160 device picked with the DPI preview off, and the preview switched on in that same frame -- what the
 * toolbar's toggle does besides saving the preference, done here without the save: the canvas is the device over the
 * curve, and still is once the preview's root canvas has ticked. That tick used to size the canvas back to the raw
 * device: it went by the edit-mode size the pick had just changed, which named the device rather than the canvas.
 * Switched off again, the canvas is the device.
 */
bool FDreamDesignerDPIPreviewRightAfterAPickTest::RunTest(const FString&)
{
	using namespace DreamDesignerCanvasSizeTestLocal;
	const FIntPoint Device(3840, 2160);
	const FIntPoint Expected = DeviceOverCurve(*this, Device);
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerDPIPreviewAfterPick"));
	State->Preferences = MakeShared<FScopedDesignerPreferences>();
	State->Preferences->SetPreviewDPIScale(false);
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, Device, Expected]()
	{
		FDreamWidgetBlueprintEditor* Toolkit = State->Driver->Toolkit();
		Toolkit->SetDesignerSizeRule(EDreamUIDesignerSizeRule::Custom);
		Toolkit->SetDesignerViewportSize(Device);
		ExpectCanvas(*this, *Toolkit, Device, TEXT("Picked with the preview off"));
		GetMutableDefault<UDreamUIDesignerSettings>()->bPreviewDPIScale = true;
		Toolkit->ApplyDesignerViewportSize(Toolkit->GetDesignerViewportSize(), /*bRecordOnAsset*/false);
		ExpectCanvas(*this, *Toolkit, Expected, TEXT("The preview switched on in the same frame"));
	});
	EnqueueDesignerFrames(State, SettleFrames);
	EnqueueDesignerAction(State, [this, State, Device, Expected]()
	{
		FDreamWidgetBlueprintEditor* Toolkit = State->Driver->Toolkit();
		ExpectCanvas(*this, *Toolkit, Expected, FString::Printf(TEXT("%d frames on"), SettleFrames));
		GetMutableDefault<UDreamUIDesignerSettings>()->bPreviewDPIScale = false;
		Toolkit->ApplyDesignerViewportSize(Toolkit->GetDesignerViewportSize(), /*bRecordOnAsset*/false);
	});
	EnqueueDesignerFrames(State, SettleFrames);
	EnqueueDesignerAction(State, [this, State, Device]()
	{
		ExpectCanvas(*this, *State->Driver->Toolkit(), Device, FString::Printf(TEXT("Switched off, %d frames on"), SettleFrames));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

#endif
