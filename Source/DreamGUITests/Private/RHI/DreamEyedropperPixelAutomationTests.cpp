// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Widget/SDreamUIEyedropper.h"
#include "Workflow/DreamUILayerStyleClipboard.h"
#include "Workflow/DreamUILayerStyleSnippet.h"

#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"

#include "Slate/SceneViewport.h"

/*
 * The eyedropper reads the designer viewport's own picture: the pixel at a widget's centre, as drawn after its layer style.
 * A white rect block is dropped, given a red colour overlay by pasting a layer style onto it, and drawn; the eyedropper's
 * pixel at its centre is red -- the overlay's colour, not the block's own -- and the pixel over the empty canvas is not.
 */
namespace DreamEyedropperPixelTestLocal
{
	using namespace DreamTests;
	constexpr double DrawnSeconds = 60.0;

	bool IsRed(const FColor& InColor)
	{
		return InColor.R > 200 && InColor.G < 60 && InColor.B < 60;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEyedropperDesignerPixelTest,
	"DreamGUI.Designer.RHI.TheEyedropperReadsAWidgetsPixelAfterItsLayerStyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FDreamEyedropperDesignerPixelTest::RunTest(const FString&)
{
	using namespace DreamEyedropperPixelTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerEyedropperPixel"));
	EnqueueDesignerFrames(State, 3, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		const FIntPoint Size = Driver.ViewportPixelSize();
		UDreamWidget* Root = DesignerTemplateRoot(State->Asset.Blueprint);
		const TArray<UDreamWidget*> Existing = LiveChildrenOf(Root);
		if (!TestTrue(TEXT("A rect block's drop is taken"), Driver.DropBasicFromPalette(UDreamRectBlock::StaticClass(), FIntPoint(Size.X / 2, Size.Y / 2))))
		{
			State->bAlive = false;
			return;
		}
		for (UDreamWidget* Child : LiveChildrenOf(Root))
		{
			if (!Existing.Contains(Child))
			{
				State->Made.Add(TEXT("Block"), Child);
			}
		}
		UDreamWidget* Block = State->Get(TEXT("Block"));
		if (!TestNotNull(TEXT("The block arrived under the root"), Block))
		{
			State->bAlive = false;
			return;
		}
		FDreamUILayerStyleSnippet Red;
		if (!TestTrue(TEXT("A red colour overlay reads as a layer style"), FDreamUILayerStyleSnippet::Parse(
			TEXT("+ DreamLayerStyle { Style.ColorOverlays = [\"(Color=(R=255,G=0,B=0,A=255))\"] }"), Red)))
		{
			State->bAlive = false;
			return;
		}
		TestEqual(TEXT("And is pasted onto the block"), DreamUILayerStyleClipboard::Apply(*Driver.Toolkit(), { Driver.PreviewFor(Block) }, Red,
			EDreamUIStyleParts::All, FText::FromString(TEXT("Paste Layer Style"))), 1);
		SelectNothingInDesigner(Driver);
		Driver.MoveTo(FIntPoint(Size.X - 6, Size.Y - 6));
	});
	EnqueueDesignerUntil(State, this, [State]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		const TOptional<FIntPoint> Centre = Driver.WidgetPixel(Driver.PreviewFor(State->Get(TEXT("Block"))));
		FDreamUIPickedColor Picked;
		return Centre.IsSet() && FDreamUIEyedropper::SampleViewport(Driver.SceneViewport().Get(), Centre.GetValue(), Picked) && IsRed(Picked.SRGB);
	}, DrawnSeconds, TEXT("the styled block drawn red in the designer viewport"), /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		Driver.DrawFrame();
		const TOptional<FIntPoint> Centre = Driver.WidgetPixel(Driver.PreviewFor(State->Get(TEXT("Block"))));
		if (!TestTrue(TEXT("The block is on screen"), Centre.IsSet()))
		{
			return;
		}
		FDreamUIPickedColor Picked;
		TestTrue(TEXT("The eyedropper reads the viewport at the block's centre"),
			FDreamUIEyedropper::SampleViewport(Driver.SceneViewport().Get(), Centre.GetValue(), Picked));
		TestEqual(TEXT("From the designer viewport"), Picked.Source, EDreamUIPickedFrom::DesignerViewport);
		TestTrue(FString::Printf(TEXT("And reads the overlay's red, not the block's white: %s"), *Picked.SRGB.ToString()), IsRed(Picked.SRGB));
		TestTrue(TEXT("In linear light too"), Picked.Linear.Equals(FLinearColor::FromSRGBColor(Picked.SRGB), 1e-4f));

		FDreamUIPickedColor Canvas;
		TestTrue(TEXT("A corner of the viewport reads too"), FDreamUIEyedropper::SampleViewport(Driver.SceneViewport().Get(), FIntPoint(4, 4), Canvas));
		TestFalse(TEXT("And is not the block's red"), IsRed(Canvas.SRGB));
		FDreamUIPickedColor Outside;
		TestFalse(TEXT("A pixel off the viewport is no pixel"), FDreamUIEyedropper::SampleViewport(Driver.SceneViewport().Get(), FIntPoint(-1, 0), Outside));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

#endif
