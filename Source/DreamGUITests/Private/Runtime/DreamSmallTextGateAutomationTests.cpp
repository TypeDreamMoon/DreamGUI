// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIManager.h"
#include "Core/Text/DreamTextPainter.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "MeshModifier/DreamMeshModifierTextAnimation.h"
#include "MeshModifier/TextAnimation/DreamMeshModifierTextAnimation_PropertyWithEase.h"
#include "MeshModifier/TextAnimation/DreamMeshModifierTextAnimation_Selector.h"

#include "DreamScopedWorld.h"
#include "DreamTextTestFont.h"
#include "Driver/DreamDriverRig.h"

/*
 * Small text from coverage glyphs, the text component's half: the gate that decides, at each paint, whether a text may
 * draw its small sizes from hinted coverage glyphs placed on the device pixel grid, and where that grid lies.
 *
 * Every test here paints for real -- a label on the driver rig's screen-space canvas, painted by the UI manager's tick --
 * in the made-up font, whose coverage glyphs come from MockCoverageGlyph: a ready-made box for any glyph, ready or still
 * being made as the test says. What is read back is UDreamText::GetSmallTextState, the gate's answer at the last paint,
 * and the painter's report. A frame of the rig is a frame of the game: transform changes flushed, the sharpen sweep, the
 * canvases updated, the fonts' pending work handed over.
 */
namespace DreamSmallTextGateTestLocal
{
	/** A coverage glyph about the size a real one would be at InSize26Dot6, ready or still being made. */
	void MakeMockGlyph(int32 InSize26Dot6, bool bInPending, FDreamUICoverageGlyph& OutGlyph)
	{
		const float Pixels = (float)InSize26Dot6 / 64.0f;
		OutGlyph.BitmapLeft = 0;
		OutGlyph.Width = FMath::CeilToInt(Pixels * 0.6f) + 1;
		OutGlyph.Height = FMath::CeilToInt(Pixels * 0.75f);
		OutGlyph.BitmapTop = OutGlyph.Height;
		OutGlyph.MinUV = FVector2f(0.0f, 0.0f);
		OutGlyph.MaxUV = FVector2f((float)OutGlyph.Width / 512.0f, (float)OutGlyph.Height / 512.0f);
		OutGlyph.SliceIndex = 0;
		OutGlyph.bPending = bInPending;
	}

	/** The made-up font, offering coverage glyphs that come back as *bInPending says when they are asked for. */
	UDreamTextTestFont* MakeCoverageFont(UObject* InOuter, const bool* bInPending)
	{
		UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(InOuter);
		Font->MockCoverageGlyph = [bInPending](int32 InFaceIndex, uint32 InGlyphIndex, int32 InSize26Dot6, EDreamUICoverageGlyphFlags InFlags, FDreamUICoverageGlyph& OutGlyph)
		{
			MakeMockGlyph(InSize26Dot6, bInPending != nullptr && *bInPending, OutGlyph);
			return true;
		};
		return Font;
	}

	const TCHAR* GateName(EDreamTextSmallTextGate InGate)
	{
		switch (InGate)
		{
		case EDreamTextSmallTextGate::NotPainted: return TEXT("NotPainted");
		case EDreamTextSmallTextGate::Coverage: return TEXT("Coverage");
		case EDreamTextSmallTextGate::Off: return TEXT("Off");
		case EDreamTextSmallTextGate::Font: return TEXT("Font");
		case EDreamTextSmallTextGate::OverrideMaterial: return TEXT("OverrideMaterial");
		case EDreamTextSmallTextGate::Material: return TEXT("Material");
		case EDreamTextSmallTextGate::Style: return TEXT("Style");
		case EDreamTextSmallTextGate::Modifier: return TEXT("Modifier");
		case EDreamTextSmallTextGate::Snapping: return TEXT("Snapping");
		case EDreamTextSmallTextGate::NoCanvas: return TEXT("NoCanvas");
		case EDreamTextSmallTextGate::WorldSpace: return TEXT("WorldSpace");
		case EDreamTextSmallTextGate::RenderScale: return TEXT("RenderScale");
		case EDreamTextSmallTextGate::RenderLayer: return TEXT("RenderLayer");
		case EDreamTextSmallTextGate::Transform: return TEXT("Transform");
		case EDreamTextSmallTextGate::Large: return TEXT("Large");
		case EDreamTextSmallTextGate::Settling: return TEXT("Settling");
		}
		return TEXT("?");
	}

	/** That the text's last paint was decided by InExpected, saying what it was decided by when it was not. */
	bool ExpectGate(FAutomationTestBase& InTest, const UDreamText* InText, EDreamTextSmallTextGate InExpected, const TCHAR* InWhat)
	{
		const EDreamTextSmallTextGate Actual = InText->GetSmallTextState().Gate;
		return InTest.TestTrue(FString::Printf(TEXT("%s: %s (it was %s)"), InWhat, GateName(InExpected), GateName(Actual)), Actual == InExpected);
	}

	/**
	 * A label on the rig's screen-space canvas: a card the tests move, turn and scale, a 300x40 widget on it, and on that a
	 * 14-unit text in the made-up font, which offers coverage glyphs -- ready ones unless bPendingGlyphs says otherwise.
	 */
	struct FLabelStage
	{
		FDreamDriverRig& Rig;
		UDreamTextTestFont* Font = nullptr;
		UDreamWidget* Card = nullptr;
		UDreamWidget* Label = nullptr;
		UDreamText* Text = nullptr;
		bool bPendingGlyphs = false;

		explicit FLabelStage(FDreamDriverRig& InRig) : Rig(InRig) {}

		bool Build(FAutomationTestBase& InTest)
		{
			if (!InTest.TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
			{
				return false;
			}
			Font = MakeCoverageFont(Rig.GetWorld(), &bPendingGlyphs);
			Card = Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(400.0, 100.0));
			Label = Card != nullptr ? Rig.MakeWidget(TEXT("Label"), Card, FVector2D(300.0, 40.0)) : nullptr;
			Text = Label != nullptr ? Label->CreateNewVisual<UDreamText>() : nullptr;
			if (!InTest.TestNotNull(TEXT("The label has its text"), Text))
			{
				return false;
			}
			Text->SetFont(Font);
			Text->SetFontSize(14.0f);
			Text->SetText(FText::FromString(TEXT("Small print")));
			// The first frame lays the label out and paints it; the second leaves it as a quiet frame would find it.
			Rig.PumpFrames(2);
			return true;
		}

		void Frames(int32 InCount) { Rig.PumpFrames(InCount); }
		const FDreamTextSmallTextState& State() const { return Text->GetSmallTextState(); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSmallTextGateTextConditionsTest,
	"DreamGUI.Text.SmallText.ASmallScreenTextDrawsFromCoverageUntilSomethingAboutTheTextRulesItOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The gate's conditions on the text itself. A 14-unit label on a screen canvas one pixel to the unit draws from coverage
 * at its first paint. Then, one at a time and each taken back again: the text's own switch, a font that offers no coverage
 * glyphs, an override material, an outline, face softness and face dilation, a TextAnimation that lifts the glyphs (where
 * one that only fades them changes nothing), and a size past the limit -- each sends the text to the field, saying why.
 */
bool FDreamSmallTextGateTextConditionsTest::RunTest(const FString& Parameters)
{
	using namespace DreamSmallTextGateTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	FLabelStage Stage(Rig);
	if (!Stage.Build(*this))
	{
		return false;
	}
	UDreamText* Text = Stage.Text;
	if (!ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("A 14-unit label on a screen canvas draws from coverage at its first paint")))
	{
		return false;
	}
	TestEqual(TEXT("...one device pixel to a unit"), Stage.State().DeviceScale, 1.0f, 1.0e-4f);
	TestEqual(TEXT("...its glyphs rasterized at that scale"), Stage.State().RasterScale, 1.0f, 1.0e-4f);
	TestFalse(TEXT("...without waiting for anything"), Stage.State().bWaitingToSharpen);
	TestTrue(TEXT("...and the painter drew items from coverage"), Text->GetSmallTextReport().CoverageItems > 0);

	Text->SetSmallTextRaster(EDreamTextSmallTextRaster::Off);
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Off, TEXT("SmallTextRaster Off keeps it on the field"));
	TestEqual(TEXT("...and nothing is drawn from coverage"), Text->GetSmallTextReport().CoverageItems, 0);
	Text->SetSmallTextRaster(EDreamTextSmallTextRaster::Auto);
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("Auto again"));

	UDreamTextTestFont* PlainFont = NewObject<UDreamTextTestFont>(Rig.GetWorld());
	Text->SetFont(PlainFont);
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Font, TEXT("A font that offers no coverage glyphs"));
	Text->SetFont(Stage.Font);
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("The coverage font again"));

	UMaterialInterface* Material = UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->DefaultUIMaterial, TEXT("DefaultUIMaterial"));
	if (TestNotNull(TEXT("A material to override the text's with"), Material))
	{
		Text->SetOverrideMaterial(Material);
		Stage.Frames(1);
		ExpectGate(*this, Text, EDreamTextSmallTextGate::OverrideMaterial, TEXT("An override material"));
		Text->SetOverrideMaterial(nullptr);
		Stage.Frames(1);
		ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("The override material taken off"));
	}

	FDreamTextStyle Outlined;
	Outlined.OutlineColor = FColor::Black;
	Outlined.OutlineWidth = 0.1f;
	FDreamTextStyle Softened;
	Softened.FaceSoftness = 0.05f;
	FDreamTextStyle Dilated;
	Dilated.FaceDilate = 0.05f;
	struct FStyled
	{
		const TCHAR* What;
		FDreamTextStyle Style;
	};
	const FStyled Styles[] = {
		{ TEXT("An outline"), Outlined },
		{ TEXT("Face softness"), Softened },
		{ TEXT("Face dilation"), Dilated },
	};
	for (const FStyled& Styled : Styles)
	{
		Text->SetTextStyle(Styled.Style);
		Stage.Frames(1);
		ExpectGate(*this, Text, EDreamTextSmallTextGate::Style, Styled.What);
		Text->SetTextStyle(FDreamTextStyle());
		Stage.Frames(1);
		ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, *FString::Printf(TEXT("%s taken off"), Styled.What));
	}

	UDreamMeshModifierTextAnimation* Animation = Stage.Label->AddComponent<UDreamMeshModifierTextAnimation>();
	if (TestNotNull(TEXT("The label took a TextAnimation"), Animation))
	{
		Animation->SetSelector(NewObject<UDreamMeshModifierTextAnimation_RangeSelector>(Animation));
		UDreamMeshModifierTextAnimation_PositionProperty* Lift = NewObject<UDreamMeshModifierTextAnimation_PositionProperty>(Animation);
		Lift->SetPosition(FVector(0.0, 0.0, 4.0));
		Animation->SetProperties({ Lift });
		Stage.Frames(1);
		ExpectGate(*this, Text, EDreamTextSmallTextGate::Modifier, TEXT("A TextAnimation that lifts the glyphs"));
		UDreamMeshModifierTextAnimation_AlphaProperty* Fade = NewObject<UDreamMeshModifierTextAnimation_AlphaProperty>(Animation);
		Fade->SetAlpha(0.25f);
		Animation->SetProperties({ Fade });
		Stage.Frames(1);
		ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("One that only fades them, as a typewriter does"));
		Animation->SetProperties({ Fade, Lift });
		Stage.Frames(1);
		ExpectGate(*this, Text, EDreamTextSmallTextGate::Modifier, TEXT("A fade and a lift together"));
		Animation->SetEnable(false);
		Stage.Frames(1);
		ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("The TextAnimation switched off"));
	}

	Text->SetFontSize(48.0f);
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Large, TEXT("48 units, past the font's 20 px"));
	Text->SetFontSize(14.0f);
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("14 again"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSmallTextGatePlacementTest,
	"DreamGUI.Text.SmallText.OnlyAFlatUnrolledUnmirroredEvenlyScaledTextOutsideARenderLayerWithSnappingAllowedDrawsFromCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The gate's conditions on where the text is drawn. The label's card is rolled, scaled unevenly, mirrored and turned out of
 * the canvas's plane, the label's and the card's pixel snapping disabled, and the card made a render layer -- each sends
 * the text to the field, and the move or the setting taken back brings it back to coverage at once, its scale being the
 * one it settled at before. Pixel snapping is read up the chain: Disabled above it rules coverage out unless the label
 * itself says SnapToPixel, and Inherit all the way to the root does not. A screen-space canvas drawn below the screen's
 * resolution rules it out too.
 */
bool FDreamSmallTextGatePlacementTest::RunTest(const FString& Parameters)
{
	using namespace DreamSmallTextGateTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	FLabelStage Stage(Rig);
	if (!Stage.Build(*this))
	{
		return false;
	}
	UDreamText* Text = Stage.Text;
	UDreamWidget* Card = Stage.Card;
	if (!ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("Flat on the canvas")))
	{
		return false;
	}

	struct FMove
	{
		const TCHAR* What;
		FRotator Rotation;
		FVector Scale;
	};
	const FMove Moves[] = {
		{ TEXT("A card rolled 5 degrees"), FRotator(0.0, 0.0, 5.0), FVector::OneVector },
		{ TEXT("A card turned 20 degrees out of the canvas's plane"), FRotator(0.0, 20.0, 0.0), FVector::OneVector },
		{ TEXT("A card scaled 1 across and 1.2 up"), FRotator::ZeroRotator, FVector(1.0, 1.0, 1.2) },
		{ TEXT("A card mirrored across"), FRotator::ZeroRotator, FVector(1.0, -1.0, 1.0) },
	};
	for (const FMove& Move : Moves)
	{
		Card->SetRelativeRotationEuler(Move.Rotation);
		Card->SetRelativeScale(Move.Scale);
		Stage.Frames(1);
		ExpectGate(*this, Text, EDreamTextSmallTextGate::Transform, Move.What);
		Card->SetRelativeRotationEuler(FRotator::ZeroRotator);
		Card->SetRelativeScale(FVector::OneVector);
		Stage.Frames(1);
		ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, *FString::Printf(TEXT("%s, put back"), Move.What));
	}

	Stage.Label->SetPixelSnapping(EWidgetPixelSnapping::Disabled);
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Snapping, TEXT("The label's pixel snapping Disabled"));
	Stage.Label->SetPixelSnapping(EWidgetPixelSnapping::Inherit);
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("Inherit, up to a root that says nothing"));
	Card->SetPixelSnapping(EWidgetPixelSnapping::Disabled);
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Snapping, TEXT("The card's Disabled, inherited by the label"));
	Stage.Label->SetPixelSnapping(EWidgetPixelSnapping::SnapToPixel);
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("The label's own SnapToPixel under it"));
	Stage.Label->SetPixelSnapping(EWidgetPixelSnapping::Inherit);
	Card->SetPixelSnapping(EWidgetPixelSnapping::Inherit);
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("Both back to Inherit"));

	Card->SetRenderLayerMode(EDreamWidgetRenderLayer::Always);
	Stage.Frames(2);
	if (TestTrue(TEXT("The card, set to Always, is a render layer"), Card->IsRenderLayer()))
	{
		ExpectGate(*this, Text, EDreamTextSmallTextGate::RenderLayer, TEXT("A text in a render layer, placed on the GPU"));
	}
	Card->SetRenderLayerMode(EDreamWidgetRenderLayer::Never);
	Stage.Frames(2);
	TestFalse(TEXT("Set to Never, the card is a layer no longer"), Card->IsRenderLayer());
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("Out of the layer again"));

	// A screen-space UI drawn at a fraction of the screen's resolution has no pixel grid to place on: the renderer scales
	// it up, or not, on its own thread. The render scale repaints nothing by itself, so the text is asked to.
	UDreamCanvas* Canvas = Rig.RootCanvas();
	auto Repaint = [Text, &Stage]()
	{
		Text->SetSmallTextRaster(EDreamTextSmallTextRaster::Off);
		Text->SetSmallTextRaster(EDreamTextSmallTextRaster::Auto);
		Stage.Frames(1);
	};
	Canvas->SetScreenSpaceRenderScale(0.5f);
	Repaint();
	ExpectGate(*this, Text, EDreamTextSmallTextGate::RenderScale, TEXT("A screen-space canvas drawn at half the screen's resolution"));
	Canvas->SetScreenSpaceRenderScale(1.0f);
	Repaint();
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("...and at its full resolution again"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSmallTextGateCanvasesTest,
	"DreamGUI.Text.SmallText.WorldSpaceAndEditorCanvasesKeepTheFieldAndARenderTargetBlendsInLinearSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Which canvases have a device pixel grid. A text on a world-space canvas, either renderer, stays on the field; on a
 * render-target canvas it draws from coverage and tells the shader its target blends in linear space, its device scale
 * the target's pixels to the canvas's unit -- two when the target is made at twice the canvas's size; and a screen-space
 * canvas in an editor world, which the editor draws in the level through its own camera, counts as world space.
 */
bool FDreamSmallTextGateCanvasesTest::RunTest(const FString& Parameters)
{
	using namespace DreamSmallTextGateTestLocal;
	auto BuildLabel = [this](UWorld* InWorld, EDreamRenderMode InMode, UDreamWidget*& OutRoot, UDreamCanvas*& OutCanvas) -> UDreamText*
	{
		OutRoot = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Transient);
		OutRoot->SetWidth(800.0f);
		OutRoot->SetHeight(400.0f);
		OutRoot->OnRegister();
		OutCanvas = OutRoot->AddComponent<UDreamCanvas>();
		if (!TestNotNull(TEXT("A canvas on the root"), OutCanvas))
		{
			return nullptr;
		}
		OutCanvas->SetRenderMode(InMode);
		UDreamWidget* Child = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Transient);
		Child->SetWidth(300.0f);
		Child->SetHeight(40.0f);
		Child->OnRegister();
		if (!TestTrue(TEXT("The label is under the root"), Child->TrySetParent(OutRoot, false)))
		{
			return nullptr;
		}
		UDreamText* Text = Child->CreateNewVisual<UDreamText>();
		if (!TestNotNull(TEXT("The label has its text"), Text))
		{
			return nullptr;
		}
		Text->SetFont(MakeCoverageFont(InWorld, nullptr));
		// Small enough to stay under the limit at whatever scale a canvas comes up at.
		Text->SetFontSize(8.0f);
		Text->SetText(FText::FromString(TEXT("Small print")));
		return Text;
	};

	{
		DreamTests::FScopedGameWorld TestWorld;
		UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
		if (!TestNotNull(TEXT("A game world with a UI manager"), Manager))
		{
			return false;
		}
		UDreamWidget* Root = nullptr;
		UDreamCanvas* Canvas = nullptr;
		UDreamText* Text = BuildLabel(TestWorld.World, EDreamRenderMode::WorldSpace, Root, Canvas);
		if (Text == nullptr)
		{
			if (Root != nullptr)
			{
				Root->DestroyWidget();
			}
			return false;
		}
		Manager->TickDreamUI(1.0f / 30.0f);
		Manager->TickDreamUI(1.0f / 30.0f);
		ExpectGate(*this, Text, EDreamTextSmallTextGate::WorldSpace, TEXT("A world-space canvas drawn by the engine's renderer"));
		Canvas->SetRenderMode(EDreamRenderMode::WorldSpace_DreamUI);
		Manager->TickDreamUI(1.0f / 30.0f);
		Manager->TickDreamUI(1.0f / 30.0f);
		ExpectGate(*this, Text, EDreamTextSmallTextGate::WorldSpace, TEXT("...and one drawn by DreamGUI's"));
		Canvas->SetRenderMode(EDreamRenderMode::RenderTarget);
		Manager->TickDreamUI(1.0f / 30.0f);
		Manager->TickDreamUI(1.0f / 30.0f);
		if (ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("A render-target canvas")))
		{
			TestTrue(TEXT("...whose target blends in linear space"), Text->GetSmallTextState().bLinearTarget);
			TestEqual(TEXT("...one pixel of its target to the canvas's unit"), Text->GetSmallTextState().DeviceScale, 1.0f, 1.0e-4f);
		}
		Root->DestroyWidget();
	}

	// The device pixels are the target's, whatever the canvas scale says: a target made at twice the canvas's size (its
	// resolution scale, set before the label's first paint, which counts as settled) has two of them to the canvas's unit.
	{
		DreamTests::FScopedGameWorld TestWorld;
		UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
		if (!TestNotNull(TEXT("Another game world with a UI manager"), Manager))
		{
			return false;
		}
		UDreamWidget* Root = nullptr;
		UDreamCanvas* Canvas = nullptr;
		UDreamText* Text = BuildLabel(TestWorld.World, EDreamRenderMode::RenderTarget, Root, Canvas);
		if (Text != nullptr)
		{
			Canvas->SetRenderTargetResolutionScale(2.0f);
			Manager->TickDreamUI(1.0f / 30.0f);
			Manager->TickDreamUI(1.0f / 30.0f);
			if (ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("A render-target canvas drawn at twice its size")))
			{
				TestEqual(TEXT("...two pixels of its target to the canvas's unit"), Text->GetSmallTextState().DeviceScale, 2.0f, 1.0e-4f);
			}
		}
		if (Root != nullptr)
		{
			Root->DestroyWidget();
		}
	}

	{
		DreamTests::FScopedGameWorld EditorWorld(EWorldType::Editor);
		UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(EditorWorld.World);
		if (!TestNotNull(TEXT("An editor world with a UI manager"), Manager))
		{
			return false;
		}
		UDreamWidget* Root = nullptr;
		UDreamCanvas* Canvas = nullptr;
		UDreamText* Text = BuildLabel(EditorWorld.World, EDreamRenderMode::ScreenSpaceOverlay, Root, Canvas);
		if (Text != nullptr)
		{
			Manager->TickDreamUI(1.0f / 30.0f);
			Manager->TickDreamUI(1.0f / 30.0f);
			ExpectGate(*this, Text, EDreamTextSmallTextGate::WorldSpace, TEXT("A screen-space canvas in an editor world, drawn in the level"));
		}
		if (Root != nullptr)
		{
			Root->DestroyWidget();
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSmallTextGateDeviceGridTest,
	"DreamGUI.Text.SmallText.TheDeviceScaleIsTheCanvasScaleTimesTheTextsScaleAndTheGridStartsAtTheCanvasCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Where the device pixel grid lies. On a canvas scaled 1.25 with the screen, a label on a card scaled 1.5 draws at
 * S = 1.875 device pixels to its unit. Its grid, u = S x + SnapOrigin.X and v = S y + SnapOrigin.Y on the label's own
 * coordinates, puts the canvas's top-left corner at (0, 0) and its bottom-right corner at the viewport's (1280, -720):
 * whole numbers are the boundaries of the pixels the canvas renders to.
 */
bool FDreamSmallTextGateDeviceGridTest::RunTest(const FString& Parameters)
{
	using namespace DreamSmallTextGateTestLocal;
	FDreamRigOptions Options;
	Options.ViewportSize = FIntPoint(1280, 720);
	Options.CanvasScaleMode = EDreamCanvasScaleMode::ScaleWithScreenSize;
	Options.ReferenceResolution = FVector2D(1024.0, 576.0);
	Options.MatchFromWidthToHeight = 1.0f;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	Rig.BindTest(this);
	FLabelStage Stage(Rig);
	if (!Stage.Build(*this))
	{
		return false;
	}
	if (!TestEqual(TEXT("The canvas is scaled 1.25"), Rig.RootCanvas()->GetCanvasScale(), 1.25f, 1.0e-3f))
	{
		return false;
	}
	// 15 device pixels at the scale below: under the font's 20.
	Stage.Text->SetFontSize(8.0f);
	Stage.Card->SetRelativeScale(FVector(1.5));
	// Off-centre, so the origin has to come from where the label really is.
	Stage.Card->SetRelativeLocation(Stage.Card->GetRelativeLocation() + FVector(0.0, 37.3, -21.9));
	Stage.Frames(4);
	UDreamText* Text = Stage.Text;
	if (!ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("The scaled label, once its scale has settled")))
	{
		return false;
	}
	const FDreamTextSmallTextState& State = Stage.State();
	TestEqual(TEXT("S is the canvas scale times the card's"), State.DeviceScale, 1.875f, 1.0e-4f);
	TestEqual(TEXT("...and the raster scale is S"), State.RasterScale, 1.875f, 1.0e-4f);
	TestFalse(TEXT("A screen canvas blends as the screen does, not linearly"), State.bLinearTarget);

	const UDreamWidget* Root = Rig.Root();
	auto ToDevice = [&State, Root, &Stage](float InCanvasX, float InCanvasY)
	{
		const FVector World = Root->GetWorldTransform().TransformPosition(FVector(0.0, InCanvasX, InCanvasY));
		const FVector Local = Stage.Label->GetWorldTransform().InverseTransformPosition(World);
		return FVector2D(State.DeviceScale * Local.Y + State.SnapOrigin.X, State.DeviceScale * Local.Z + State.SnapOrigin.Y);
	};
	const FVector2D TopLeft = ToDevice(Root->GetLocalSpaceLeft(), Root->GetLocalSpaceTop());
	const FVector2D BottomRight = ToDevice(Root->GetLocalSpaceRight(), Root->GetLocalSpaceBottom());
	TestEqual(TEXT("The canvas's left edge is u = 0"), TopLeft.X, 0.0, 0.01);
	TestEqual(TEXT("...its top edge v = 0"), TopLeft.Y, 0.0, 0.01);
	TestEqual(TEXT("...its right edge u = 1280"), BottomRight.X, 1280.0, 0.01);
	TestEqual(TEXT("...and its bottom edge v = -720"), BottomRight.Y, -720.0, 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSmallTextGateSettlingTest,
	"DreamGUI.Text.SmallText.ASmallScaleChangeKeepsTheRasterAndALargerOneWaitsThreeFramesForTheScaleToSettle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The debounce, the raster-scale hysteresis and the sharpen sweep, frame by frame. Scaled by half a percent, the label keeps
 * drawing from coverage with its glyphs as they were rasterized; scaled by 3 percent it goes to the field and into its
 * world's sharpen set, whose sweep binds to the manager's tick then, and three frames later -- the scale having held -- it
 * is repainted from coverage at the new scale and the sweep unbinds. A zoom of a step a frame keeps it on the field until
 * the zoom stops, and three frames more. No layout runs for any of it: the scale is no layout input.
 */
bool FDreamSmallTextGateSettlingTest::RunTest(const FString& Parameters)
{
	using namespace DreamSmallTextGateTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	FLabelStage Stage(Rig);
	if (!Stage.Build(*this))
	{
		return false;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig's world has its UI manager"), Manager))
	{
		return false;
	}
	UDreamText* Text = Stage.Text;
	if (!ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("Unscaled")))
	{
		return false;
	}
	FSimpleMulticastDelegate& BeforeCanvasesUpdate = Manager->GetOnBeforeRootCanvasesUpdate();
	TestFalse(TEXT("Nothing waits, so no sweep is bound"), BeforeCanvasesUpdate.IsBound());
	const int32 Laid = Text->GetCacheTextGeometryData().GetLayoutRunCount();

	Stage.Card->SetRelativeScale(FVector(1.005));
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("Scaled by half a percent"));
	TestEqual(TEXT("...its quads drawn for the new scale"), Stage.State().DeviceScale, 1.005f, 1.0e-4f);
	TestEqual(TEXT("...from the raster it had"), Stage.State().RasterScale, 1.0f, 1.0e-5f);
	TestFalse(TEXT("...without waiting"), BeforeCanvasesUpdate.IsBound());

	Stage.Card->SetRelativeScale(FVector(1.03));
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Settling, TEXT("Scaled by 3 percent"));
	TestTrue(TEXT("...it waits in its world's sharpen set"), Stage.State().bWaitingToSharpen);
	TestTrue(TEXT("...whose sweep is bound now"), BeforeCanvasesUpdate.IsBound());
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Settling, TEXT("One frame of the scale holding"));
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Settling, TEXT("Two frames of it"));
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("Three frames of it: sharpened"));
	TestEqual(TEXT("...rasterized at the scale it settled at"), Stage.State().RasterScale, 1.03f, 1.0e-4f);
	TestFalse(TEXT("...out of the sharpen set"), Stage.State().bWaitingToSharpen);
	TestFalse(TEXT("...and with nothing waiting, the sweep unbound"), BeforeCanvasesUpdate.IsBound());

	const double ZoomSteps[] = { 1.1, 1.2, 1.3 };
	for (const double Step : ZoomSteps)
	{
		Stage.Card->SetRelativeScale(FVector(Step));
		Stage.Frames(1);
		ExpectGate(*this, Text, EDreamTextSmallTextGate::Settling, *FString::Printf(TEXT("A zoom, at %.1f"), Step));
	}
	Stage.Frames(2);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Settling, TEXT("Two frames after the zoom stopped"));
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("Three"));
	TestEqual(TEXT("...rasterized at the zoom's last scale"), Stage.State().RasterScale, 1.3f, 1.0e-4f);
	TestFalse(TEXT("...and the sweep unbound again"), BeforeCanvasesUpdate.IsBound());

	TestEqual(TEXT("None of it laid the text out"), Text->GetCacheTextGeometryData().GetLayoutRunCount(), Laid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSmallTextGateMovesTest,
	"DreamGUI.Text.SmallText.AMoveByWholeDevicePixelsKeepsTheCoverageQuadsAndAnyOtherMoveRepaintsThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Coverage quads sit on the device grid of the paint that made them. Moved 3 pixels across and 2 down, as a scroll moves
 * it, the label needs no repaint; moved half a pixel, or a quarter, or scaled, it does, and once repainted its grid has
 * moved with it. A label drawn from the field never asks to be repainted for a move.
 */
bool FDreamSmallTextGateMovesTest::RunTest(const FString& Parameters)
{
	using namespace DreamSmallTextGateTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	FLabelStage Stage(Rig);
	if (!Stage.Build(*this))
	{
		return false;
	}
	UDreamText* Text = Stage.Text;
	UDreamWidget* Label = Stage.Label;
	if (!ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("Before any move")))
	{
		return false;
	}
	const FVector Home = Label->GetRelativeLocation();

	Label->SetRelativeLocation(Home + FVector(0.0, 3.0, -2.0));
	TestFalse(TEXT("3 pixels across and 2 down keep the quads on the grid"), Text->GetRepaintsOnTransformChange());
	Label->SetRelativeLocation(Home + FVector(0.0, 3.5, -2.0));
	TestTrue(TEXT("Half a pixel across takes them off it"), Text->GetRepaintsOnTransformChange());
	Label->SetRelativeLocation(Home + FVector(0.0, 0.0, -0.25));
	TestTrue(TEXT("...and so does a quarter pixel down"), Text->GetRepaintsOnTransformChange());
	Label->SetRelativeLocation(Home);
	Label->SetRelativeScale(FVector(1.002));
	TestTrue(TEXT("...and a scale, however small"), Text->GetRepaintsOnTransformChange());
	Label->SetRelativeScale(FVector::OneVector);
	TestFalse(TEXT("Back where it was painted, nothing is off the grid"), Text->GetRepaintsOnTransformChange());

	const FVector2f PaintedOrigin = Stage.State().SnapOrigin;
	Label->SetRelativeLocation(Home + FVector(0.0, 0.5, 0.0));
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("Moved half a pixel and painted"));
	const float GridMove = Stage.State().SnapOrigin.X - PaintedOrigin.X;
	TestEqual(TEXT("...its grid moved half a pixel with it"), GridMove - FMath::FloorToFloat(GridMove), 0.5f, 1.0e-3f);
	TestFalse(TEXT("...and the move it was painted for is no move now"), Text->GetRepaintsOnTransformChange());

	Text->SetSmallTextRaster(EDreamTextSmallTextRaster::Off);
	Stage.Frames(1);
	Label->SetRelativeLocation(Home + FVector(0.0, 0.25, 0.0));
	TestFalse(TEXT("A label drawn from the field is never repainted for a move"), Text->GetRepaintsOnTransformChange());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSmallTextGateRepaintOnlyTest,
	"DreamGUI.Text.SmallText.TheRasterSwitchAndCoverageGlyphsLandingRepaintTheTextWithoutLayingItOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Coverage glyphs only stand in for quads, so nothing about them lays a text out. A label whose coverage glyphs are still
 * being made draws those items from the field and says so in its report; nothing repaints it until the font says glyphs
 * landed (OnCoverageGlyphsChanged), and then it is repainted from coverage. SmallTextRaster switched off and on again
 * repaints it each time -- field quads, then coverage quads -- and none of this runs a layout.
 */
bool FDreamSmallTextGateRepaintOnlyTest::RunTest(const FString& Parameters)
{
	using namespace DreamSmallTextGateTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	FLabelStage Stage(Rig);
	Stage.bPendingGlyphs = true;
	if (!Stage.Build(*this))
	{
		return false;
	}
	UDreamText* Text = Stage.Text;
	if (!ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("A label whose coverage glyphs are being made")))
	{
		return false;
	}
	TestTrue(TEXT("...reports the items that waited for theirs"), Text->GetSmallTextReport().PendingItems > 0);
	TestEqual(TEXT("...and none drawn from coverage yet"), Text->GetSmallTextReport().CoverageItems, 0);
	const int32 Laid = Text->GetCacheTextGeometryData().GetLayoutRunCount();

	Stage.bPendingGlyphs = false;
	Stage.Frames(1);
	TestTrue(TEXT("Nothing repaints it before the font says its glyphs landed"), Text->GetSmallTextReport().PendingItems > 0);

	Stage.Font->OnCoverageGlyphsChanged.Broadcast();
	Stage.Frames(1);
	TestTrue(TEXT("Once it does, the label is repainted from coverage"), Text->GetSmallTextReport().CoverageItems > 0);
	TestEqual(TEXT("...with nothing waiting"), Text->GetSmallTextReport().PendingItems, 0);
	TestEqual(TEXT("...and without a layout"), Text->GetCacheTextGeometryData().GetLayoutRunCount(), Laid);

	const TArray<FDreamUIOriginVertexData> CoverageQuads = Text->GetGeometry()->OriginVertices;
	Text->SetSmallTextRaster(EDreamTextSmallTextRaster::Off);
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Off, TEXT("SmallTextRaster switched off"));
	const TArray<FDreamUIOriginVertexData>& FieldQuads = Text->GetGeometry()->OriginVertices;
	bool bSameQuads = FieldQuads.Num() == CoverageQuads.Num();
	for (int32 Index = 0; bSameQuads && Index < FieldQuads.Num(); Index++)
	{
		bSameQuads = FieldQuads[Index].Position.Equals(CoverageQuads[Index].Position, 0.001f);
	}
	TestFalse(TEXT("...repaints the label with its field quads"), bSameQuads);
	Text->SetSmallTextRaster(EDreamTextSmallTextRaster::Auto);
	Stage.Frames(1);
	ExpectGate(*this, Text, EDreamTextSmallTextGate::Coverage, TEXT("Switched on again"));
	TestTrue(TEXT("...the label is drawn from coverage again"), Text->GetSmallTextReport().CoverageItems > 0);
	TestEqual(TEXT("Neither switch laid it out"), Text->GetCacheTextGeometryData().GetLayoutRunCount(), Laid);
	return true;
}

#endif
