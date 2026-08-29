// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "PrefabEditor/DreamWidgetBlueprintEditor.h"
#include "PrefabEditor/DreamUIDesignerRuler.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"

/*
 * How the designer LOOKS at the asset, as opposed to what it does to it.
 *
 * The distinction is the whole of what is worth testing here. A view rule that quietly writes to the
 * asset dirties it on every window resize and leaves a number in the diff describing somebody's
 * monitor; a ruler whose step is chosen by rounding to nearest draws its labels on top of each other
 * at exactly the zooms nobody tried.
 */

namespace DreamDesignerViewTestLocal
{
	struct FScopedDesigner
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamWidgetBlueprintEditor* Designer = nullptr;

		explicit FScopedDesigner(const TCHAR* InName)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr)
			{
				return;
			}
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			Tree->RootWidget->SetDisplayName(TEXT("Root"));
			Tree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerCanvasPanel::StaticClass());
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
			Designer = static_cast<FDreamWidgetBlueprintEditor*>(
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Blueprint, false));
		}

		~FScopedDesigner()
		{
			if (GEditor != nullptr && Blueprint != nullptr)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Blueprint);
				FSlateApplication::Get().Tick();
			}
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerFillScreenIsAViewRuleTest,
	"DreamGUI.Designer.FillScreenSizesTheCanvasWithoutRewritingTheAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerFillScreenIsAViewRuleTest::RunTest(const FString&)
{
	using namespace DreamDesignerViewTestLocal;

	FScopedDesigner Scoped(TEXT("DesignerFillScreen"));
	if (!TestNotNull(TEXT("The designer opened"), Scoped.Designer))
	{
		return false;
	}

	// A resolution the author picked. This one belongs on the asset.
	Scoped.Designer->SetDesignerViewportSize(FIntPoint(1280, 720));
	TestEqual(TEXT("The chosen resolution is recorded"),
		Scoped.Blueprint->DesignerData.DesignViewportSize, FIntPoint(1280, 720));
	TestEqual(TEXT("And the canvas is sized to it"), Scoped.Designer->GetDesignerCanvasSize(), FIntPoint(1280, 720));

	// What Fill Screen does every time the window changes size. The canvas has to move; the asset
	// must not, or a diff would record the size of whoever last had the window open.
	Scoped.Designer->ApplyDesignerViewportSize(FIntPoint(800, 600), /*bRecordOnAsset*/false);
	TestEqual(TEXT("The canvas followed"), Scoped.Designer->GetDesignerCanvasSize(), FIntPoint(800, 600));
	TestEqual(TEXT("The recorded resolution did not"),
		Scoped.Blueprint->DesignerData.DesignViewportSize, FIntPoint(1280, 720));
	TestEqual(TEXT("Nor did the recorded canvas size"),
		Scoped.Blueprint->DesignerData.CanvasSize, FIntPoint(1280, 720));

	// While the rule is driving, the picker has to name the canvas that is there rather than the one
	// the asset remembers -- a radio button on 1280x720 over an 800x600 canvas is a lie.
	Scoped.Designer->SetDesignerSizeRule(EDreamUIDesignerSizeRule::FillScreen);
	TestEqual(TEXT("The picker reports what is applied"), Scoped.Designer->GetDesignerViewportSize(), FIntPoint(800, 600));

	// And leaving the rule brings back the resolution the asset held all along.
	Scoped.Designer->SetDesignerSizeRule(EDreamUIDesignerSizeRule::Custom);
	TestEqual(TEXT("Custom restores the chosen resolution"),
		Scoped.Designer->GetDesignerViewportSize(), FIntPoint(1280, 720));
	TestEqual(TEXT("And the canvas with it"), Scoped.Designer->GetDesignerCanvasSize(), FIntPoint(1280, 720));

	// The case that actually bit, found by clicking Custom in a real designer and watching nothing
	// happen: an asset that never recorded a resolution, which every converted asset is. The getter
	// then answers with the canvas's CURRENT size, so asking it after the rule has flipped hands back
	// the fill size and "restore" restores it to itself.
	Scoped.Blueprint->DesignerData.DesignViewportSize = FIntPoint::ZeroValue;
	Scoped.Designer->ApplyDesignerViewportSize(FIntPoint(1024, 768), /*bRecordOnAsset*/false);
	TestEqual(TEXT("A canvas with nothing recorded behind it"),
		Scoped.Designer->GetDesignerCanvasSize(), FIntPoint(1024, 768));
	Scoped.Designer->SetDesignerSizeRule(EDreamUIDesignerSizeRule::FillScreen);
	// Standing in for the viewport, which headless has no size to offer.
	Scoped.Designer->ApplyDesignerViewportSize(FIntPoint(640, 480), /*bRecordOnAsset*/false);
	Scoped.Designer->SetDesignerSizeRule(EDreamUIDesignerSizeRule::Custom);
	TestEqual(TEXT("Leaving restores what was on screen before, recorded or not"),
		Scoped.Designer->GetDesignerCanvasSize(), FIntPoint(1024, 768));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRulerStepTest,
	"DreamGUI.Designer.RulerStepsNeverCrowdTheirLabels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerRulerStepTest::RunTest(const FString&)
{
	constexpr float MinPixels = 72.0f;

	// The claim, over four decades of zoom: the step is never SMALLER than the minimum spacing.
	// Rounding to nearest passes at most of these and fails in between, which is why the sweep is
	// dense rather than a handful of round numbers.
	int32 TooTight = 0;
	int32 NotOneTwoFive = 0;
	for (int32 Index = 1; Index <= 4000; ++Index)
	{
		const float PixelsPerUnit = (float)Index * 0.01f;
		const float Step = ChooseDreamUIRulerStep(PixelsPerUnit, MinPixels);
		if (!(Step > 0.0f))
		{
			continue;
		}
		if (Step * PixelsPerUnit < MinPixels - UE_KINDA_SMALL_NUMBER)
		{
			++TooTight;
		}
		// 1, 2 or 5 times a power of ten -- anything else is a ruler labelled in units nobody counts in.
		const float Decade = FMath::Pow(10.0f, FMath::FloorToFloat(FMath::LogX(10.0f, Step)));
		const float Mantissa = Step / Decade;
		const bool bNice = FMath::IsNearlyEqual(Mantissa, 1.0f, 0.001f)
			|| FMath::IsNearlyEqual(Mantissa, 2.0f, 0.001f)
			|| FMath::IsNearlyEqual(Mantissa, 5.0f, 0.001f);
		if (!bNice)
		{
			++NotOneTwoFive;
		}
	}
	TestEqual(TEXT("No zoom produces a step tighter than the label spacing"), TooTight, 0);
	TestEqual(TEXT("Every step is 1, 2 or 5 times a power of ten"), NotOneTwoFive, 0);

	// The mantissa guard: at exactly one pixel per unit the answer is the minimum spacing itself,
	// not twice it. Floor(log10(72)) landing a hair low is what would double it.
	TestEqual(TEXT("A whole-number zoom is not rounded up a step"), ChooseDreamUIRulerStep(1.0f, 100.0f), 100.0f);
	TestEqual(TEXT("Nothing to draw at zero scale"), ChooseDreamUIRulerStep(0.0f, MinPixels), 0.0f);
	TestEqual(TEXT("Nor at zero spacing"), ChooseDreamUIRulerStep(4.0f, 0.0f), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRulerTicksTest,
	"DreamGUI.Designer.RulerTicksCoverTheVisibleRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerRulerTicksTest::RunTest(const FString&)
{
	TArray<FDreamUIRulerTick> Ticks;

	// A ruler running left to right at two pixels per unit, majors every 100.
	auto UnitToPixel = [](float Unit) { return 40.0f + Unit * 2.0f; };
	BuildDreamUIRulerTicks(-250.0f, 250.0f, 100.0f, 5, UnitToPixel, Ticks);
	if (!TestTrue(TEXT("It produced ticks"), Ticks.Num() > 0))
	{
		return false;
	}
	int32 Majors = 0;
	int32 OutOfRange = 0;
	int32 Mispositioned = 0;
	for (const FDreamUIRulerTick& Tick : Ticks)
	{
		if (Tick.bMajor)
		{
			++Majors;
			// A major names a multiple of the step. This is the one that quietly breaks when the
			// major test is written against the unit value in floats instead of the tick index.
			if (!FMath::IsNearlyZero(FMath::Fmod(Tick.Unit, 100.0f), 0.01f))
			{
				++Mispositioned;
			}
		}
		if (Tick.Unit < -250.0f - UE_KINDA_SMALL_NUMBER || Tick.Unit > 250.0f + UE_KINDA_SMALL_NUMBER)
		{
			++OutOfRange;
		}
		if (!FMath::IsNearlyEqual(Tick.Pixel, UnitToPixel(Tick.Unit), 0.01f))
		{
			++Mispositioned;
		}
	}
	TestEqual(TEXT("Nothing outside the range asked for"), OutOfRange, 0);
	TestEqual(TEXT("Every tick sits where its unit maps to"), Mispositioned, 0);
	// -200, -100, 0, 100, 200.
	TestEqual(TEXT("A major every hundred units"), Majors, 5);
	TestEqual(TEXT("And four minors between each"), Ticks.Num(), 25);

	// A camera zoomed to nothing asks for a ruler with millions of ticks. Refusing beats allocating.
	BuildDreamUIRulerTicks(-1.0e6f, 1.0e6f, 1.0f, 5, UnitToPixel, Ticks);
	TestEqual(TEXT("An absurd range draws no ruler rather than a million ticks"), Ticks.Num(), 0);

	// Degenerate inputs draw nothing rather than dividing by zero.
	BuildDreamUIRulerTicks(0.0f, 100.0f, 0.0f, 5, UnitToPixel, Ticks);
	TestEqual(TEXT("No step, no ticks"), Ticks.Num(), 0);
	BuildDreamUIRulerTicks(100.0f, 100.0f, 10.0f, 5, UnitToPixel, Ticks);
	TestEqual(TEXT("No range, no ticks"), Ticks.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPerspectivePreviewDefaultTest,
	"DreamGUI.Perspective.Widget.NewWidgetBlueprintsPreviewThroughTheCanvasCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectivePreviewDefaultTest::RunTest(const FString& Parameters)
{
	// The designer previews through whatever DesignerData.CanvasRenderMode says. A world-space preview
	// is projected by the editor camera, where a declared Perspective is correctly inert -- so if the
	// default were world space, the feature would look broken in the first place anyone tries it,
	// which is exactly how it was reported.
	FDreamWidgetDesignerData Defaults;
	TestEqual(TEXT("A fresh Widget Blueprint previews through the canvas camera"),
		Defaults.CanvasRenderMode, (uint8)EDreamRenderMode::ScreenSpaceOverlay);
	return true;
}


#endif
