// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/DreamSlatePanelDriver.h"
#include "Driver/Designer/Tests/DreamDesignerDuiFixture.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"

#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamDesignerEffectHandles.h"
#include "Designer/DreamDesignerGradientHandles.h"
#include "Designer/DreamDesignerHandles.h"
#include "Designer/DreamGradientHandleMath.h"
#include "DreamUICaptureLibrary.h"
#include "Extensions/Effects/DreamGlass.h"
#include "Extensions/Effects/DreamPointerLight.h"
#include "RenderingThread.h"
#include "RHI/DreamPixelProbe.h"
#include "Slate/SceneViewport.h"
#include "Workflow/DreamUILayerStyleDialog.h"

/*
 * The effects' viewport handles on a class whose hierarchy is a .dui: Tab steps through the kinds a selection has and then
 * none; a clip's side, a clip's corner and a mask's feather; a glass's corner and its light; a layer style's shadow on the
 * global light, which turns the global light, and a rect block's own shadow; a pointer light's place and a style's global
 * light. Every drag is checked in the preview or the asset, in the file the button writes, and as one undo step; Esc puts
 * one back with no step left.
 *
 * Where a handle stands is asked of its provider (the handle the viewport would draw), and put on screen through the
 * widget's transform; every press, move, key and release goes in through the viewport's own entry points.
 */
namespace DreamDesignerEffectHandleTestLocal
{
	using namespace DreamTests;
	namespace Mask = DreamDesignerEffectHandles::MaskIds;
	namespace Glass = DreamDesignerEffectHandles::GlassIds;
	namespace Shadow = DreamDesignerEffectHandles::ShadowIds;
	namespace Light = DreamDesignerEffectHandles::LightIds;

	/** The .dui of one InType named Card, 300 by 160, with InLines on it, in a canvas panel. */
	TArray<FString> MakeFile(const TCHAR* InType, TConstArrayView<const TCHAR*> InLines)
	{
		TArray<FString> Lines = {
			TEXT("Widget Root {"),
			TEXT("    + CanvasPanel"),
			FString::Printf(TEXT("    %s Card {"), InType),
			TEXT("        AnchorData.SizeDelta = (300, 160)"),
		};
		for (const TCHAR* Line : InLines)
		{
			Lines.Add(FString(TEXT("        ")) + Line);
		}
		Lines.Add(TEXT("    }"));
		Lines.Add(TEXT("}"));
		return Lines;
	}

	/** The bound providers' names: a tagged property's handles go by Kind.Property. */
	FName MaskHandles() { return FDreamDesignerHandles::MakeBoundName(DreamDesignerEffectHandles::MaskKindName, TEXT("ClipMask")); }
	FName GlassHandles() { return FDreamDesignerHandles::MakeBoundName(DreamDesignerEffectHandles::GlassKindName, TEXT("CornerRadius")); }
	FName StyleShadowHandles() { return FDreamDesignerHandles::MakeBoundName(DreamDesignerEffectHandles::ShadowKindName, TEXT("Style")); }
	FName BlockShadowHandles() { return FDreamDesignerHandles::MakeBoundName(DreamDesignerEffectHandles::ShadowKindName, TEXT("OuterShadowAngle")); }
	FName PointerLightHandles() { return FDreamDesignerHandles::MakeBoundName(DreamDesignerEffectHandles::LightKindName, TEXT("LightPosition")); }
	FName GlobalLightHandles() { return FDreamDesignerHandles::MakeBoundName(DreamDesignerEffectHandles::LightKindName, TEXT("GlobalLightAngle")); }

	/** The active kind of handles as it was, put back when the test ends: the next test starts where every test does. */
	struct FScopedActiveHandles
	{
		FName Before = FDreamDesignerHandles::GetActive();
		explicit FScopedActiveHandles(FName InActive) { FDreamDesignerHandles::SetActive(InActive); }
		~FScopedActiveHandles() { FDreamDesignerHandles::SetActive(Before); }
	};

	/** Screen pixels per unit of InWidget's local space, as the designer draws it now. */
	double MeasurePixelsPerUnit(const FDreamDesignerDriver& InDriver, const UDreamWidget* InWidget)
	{
		TArray<FVector2D> Pixels;
		const FTransform& Transform = InWidget->GetWorldTransform();
		if (!InDriver.WorldToPixels(TArray<FVector>({ Transform.TransformPosition(FVector::ZeroVector), Transform.TransformPosition(FVector(0.0, 100.0, 0.0)) }), Pixels))
		{
			return 1.0;
		}
		return FMath::Max(FVector2D::Distance(Pixels[0], Pixels[1]) / 100.0, UE_KINDA_SMALL_NUMBER);
	}

	bool LocalToPixel(const FDreamDesignerDriver& InDriver, const UDreamWidget* InWidget, const FVector2D& InLocal, FVector2D& OutPixel)
	{
		TArray<FVector2D> Pixels;
		if (!InDriver.WorldToPixels(TArray<FVector>({ InWidget->GetWorldTransform().TransformPosition(FVector(0.0, InLocal.X, InLocal.Y)) }), Pixels))
		{
			return false;
		}
		OutPixel = Pixels[0];
		return true;
	}

	/** Where handle InId of provider InProvider is on screen for the preview widget, as the viewport would draw it. */
	bool FindHandle(const FDreamDesignerDriver& InDriver, UDreamWidget* InPreview, FName InProvider, int32 InId, FVector2D& OutPixel)
	{
		const TSharedPtr<IDreamDesignerHandleProvider> Provider = FDreamDesignerHandles::FindProviderFor(InPreview, InProvider);
		if (!Provider.IsValid() || !::IsValid(InPreview))
		{
			return false;
		}
		FDreamDesignerHandleSet Set;
		Set.PixelsPerUnit = MeasurePixelsPerUnit(InDriver, InPreview);
		Provider->CollectHandles(InPreview, Set);
		const FDreamDesignerHandle* Handle = Set.Handles.FindByPredicate([InId](const FDreamDesignerHandle& InHandle) { return InHandle.Id == InId; });
		return Handle != nullptr && LocalToPixel(InDriver, InPreview, Handle->Local, OutPixel);
	}

	FIntPoint RoundPixel(const FVector2D& InPixel)
	{
		return FIntPoint(FMath::RoundToInt32(InPixel.X), FMath::RoundToInt32(InPixel.Y));
	}

	/** A hand's drag: down at InFrom, InSteps moves to InTo with a frame after each; the release is the caller's. */
	void PressAndCarry(FDreamDesignerDriver& InDriver, FIntPoint InFrom, FIntPoint InTo, int32 InSteps = 5)
	{
		InDriver.MoveTo(InFrom);
		InDriver.PumpFrame();
		InDriver.Press(EKeys::LeftMouseButton);
		InDriver.PumpFrame();
		for (int32 Step = 1; Step <= InSteps; ++Step)
		{
			InDriver.MoveTo(InFrom + (InTo - InFrom) * Step / InSteps);
			InDriver.PumpFrame();
		}
	}

	void LetGo(FDreamDesignerDriver& InDriver)
	{
		InDriver.Release(EKeys::LeftMouseButton);
		InDriver.PumpFrame();
		InDriver.PumpFrame();
	}

	/** The text-authored designer, one to one, with the file's Card selected; its template and preview. */
	bool Prepare(FAutomationTestBase& InTest, FScopedDuiDesigner& InScoped, UDreamWidget*& OutTemplate, UDreamWidget*& OutPreview)
	{
		if (!InScoped.IsReady())
		{
			InTest.AddError(FString::Printf(TEXT("No text-authored designer to drive: %s."), *InScoped.Failure));
			return false;
		}
		FDreamDesignerDriver& Driver = *InScoped.Driver;
		FBox2D WorkArea(ForceInit);
		if (!PrepareDesignerOneToOne(InTest, Driver, WorkArea))
		{
			return false;
		}
		OutTemplate = InScoped.FindTemplate(TEXT("Card"));
		if (!InTest.TestNotNull(TEXT("The file's Card is in the asset"), OutTemplate))
		{
			return false;
		}
		SelectOnlyInDesigner(Driver, OutTemplate);
		Driver.PumpFrame();
		OutPreview = Driver.PreviewFor(OutTemplate);
		return InTest.TestNotNull(TEXT("and in the preview"), OutPreview);
	}

	/**
	 * The Card's template and preview found again. An undo of a text-authored class writes the file back and builds the
	 * asset's hierarchy from it, so the widgets held from before are no longer the asset's.
	 */
	bool Refind(FAutomationTestBase& InTest, FScopedDuiDesigner& InScoped, UDreamWidget*& OutTemplate, UDreamWidget*& OutPreview)
	{
		OutTemplate = InScoped.FindTemplate(TEXT("Card"));
		if (!InTest.TestNotNull(TEXT("After the undo the Card is in the asset"), OutTemplate))
		{
			return false;
		}
		SelectOnlyInDesigner(*InScoped.Driver, OutTemplate);
		InScoped.Driver->PumpFrame();
		OutPreview = InScoped.Driver->PreviewFor(OutTemplate);
		return InTest.TestNotNull(TEXT("...and in the preview"), OutPreview);
	}

	/**
	 * Drags handle InId of InProvider by InTravel screen pixels and lets go; then checks that the file changed and that one
	 * undo takes it back. InCheck looks at the template between the release and the undo.
	 */
	bool DragAndUndo(FAutomationTestBase& InTest, FScopedDuiDesigner& InScoped, UDreamWidget* InPreview, FName InProvider, int32 InId, const FVector2D& InTravel,
		TFunctionRef<void(const FString& InFileAfter)> InCheck)
	{
		FDreamDesignerDriver& Driver = *InScoped.Driver;
		const FString Before = InScoped.ReadFile();
		FVector2D At;
		if (!InTest.TestTrue(*FString::Printf(TEXT("Handle %d of %s is on screen"), InId, *InProvider.ToString()), FindHandle(Driver, InPreview, InProvider, InId, At)))
		{
			return false;
		}
		PressAndCarry(Driver, RoundPixel(At), RoundPixel(At + InTravel));
		LetGo(Driver);
		const FString After = InScoped.ReadFile();
		if (!InTest.TestTrue(TEXT("The release writes the file"), After != Before))
		{
			InTest.AddInfo(FString::Printf(TEXT("The file reads:\n%s"), *After));
			return false;
		}
		InCheck(After);
		InTest.TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
		Driver.PumpFrame();
		InTest.TestEqualSensitive(TEXT("One undo takes the file back"), InScoped.ReadFile(), Before);
		return true;
	}

	/** The widget's layer style: asked of its components, since a template's are never registered (UDreamLayerStyle::Find wants that). */
	UDreamLayerStyle* StyleOf(const UDreamWidget* InWidget)
	{
		return ::IsValid(InWidget) ? InWidget->GetComponent<UDreamLayerStyle>() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerHandleTabTest,
	"DreamGUI.Designer.Driver.TabStepsThroughTheSelectionsKindsOfHandlesThenNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerHandleTabTest::RunTest(const FString&)
{
	using namespace DreamDesignerEffectHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedActiveHandles Active(NAME_None);
	FScopedDuiDesigner Scoped(TEXT("DesignerHandleTab"), MakeFile(TEXT("RectBlock"), {
		TEXT("bEnableBodyGradient = true"),
		TEXT("BodyGradient = \"linear-gradient(90deg, #FF0000, #0000FF)\""),
		TEXT("bEnableOuterShadow = true"),
		TEXT("OuterShadowDistance = 10"),
		TEXT("Clipping = ClipToBounds") }));
	UDreamWidget* Template = nullptr;
	UDreamWidget* Preview = nullptr;
	if (!Prepare(*this, Scoped, Template, Preview))
	{
		return false;
	}
	FDreamDesignerDriver& Driver = *Scoped.Driver;
	const FName Body = DreamDesignerGradientHandles::GetProviderName(EDreamRectBlockGradientSlot::Body);
	TArray<FName> Steps;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		Driver.KeyDown(EKeys::Tab);
		Driver.PumpFrame();
		Driver.KeyUp(EKeys::Tab);
		Driver.PumpFrame();
		Steps.Add(FDreamDesignerHandles::GetActive());
	}
	TestTrue(*FString::Printf(TEXT("From the body gradient Tab steps to the rect block's shadows, the mask, none and the gradient again: %s"), *FString::JoinBy(Steps, TEXT(", "), [](FName InName) { return InName.ToString(); })),
		Steps == TArray<FName>({ BlockShadowHandles(), MaskHandles(), FDreamDesignerHandles::Off, Body }));

	// Through Slate, as a hand's Tab goes: the viewport takes it while it has the keyboard...
	FString WhyNot;
	FDreamDesignerHandles::SetActive(NAME_None);
	if (TestTrue(TEXT("The viewport takes the keyboard"), Driver.FocusForKeyboard(WhyNot)))
	{
		TestTrue(*FString::Printf(TEXT("Tab is sent to it (%s)"), *WhyNot), Driver.PressShortcut(EKeys::Tab, EDreamDriverModifierKeys::None, WhyNot));
		Driver.PumpFrame();
		TestEqual(TEXT("...and steps the handles"), FDreamDesignerHandles::GetActive(), BlockShadowHandles());
	}
	// ...and not while a text box elsewhere has it: there Tab is Slate's, to move the focus on.
	const TSharedPtr<SWidget> TextBox = DreamSlatePanel::FindInAnyWindow([](const TSharedRef<SWidget>& InWidget)
	{
		return InWidget->GetType() == FName(TEXT("SEditableText")) && InWidget->GetVisibility().IsVisible() && InWidget->SupportsKeyboardFocus();
	});
	if (TestTrue(TEXT("There is a text box outside the viewport to type in"), TextBox.IsValid())
		&& TestTrue(TEXT("...and it takes the keyboard"), DreamSlatePanel::FocusAsKeyboardUser(TextBox.ToSharedRef(), WhyNot)))
	{
		const FName Before = FDreamDesignerHandles::GetActive();
		DreamSlatePanel::PressKey(EKeys::Tab, FModifierKeysState(), WhyNot);
		Driver.PumpFrame();
		TestEqual(TEXT("A Tab there leaves the handles as they were"), FDreamDesignerHandles::GetActive(), Before);
		TestFalse(TEXT("...and does not take the keyboard to the viewport"), Driver.HasKeyboardFocus());
		AddInfo(FString::Printf(TEXT("The Tab left the keyboard on %s"), *DreamSlatePanel::Describe(DreamSlatePanel::GetKeyboardFocus())));
	}

	// With none shown, a press where the gradient's end was is the selection box's: the gradient stays as it was.
	FDreamDesignerHandles::SetActive(FDreamDesignerHandles::Off);
	FVector2D End;
	if (TestTrue(TEXT("The gradient's end is on screen"), FindHandle(Driver, Preview, Body, static_cast<int32>(EDreamGradientPoint::End), End)))
	{
		const FDreamGradient Before = CastChecked<UDreamRectBlock>(Template->GetVisual())->GetBodyGradient();
		PressAndCarry(Driver, RoundPixel(End), RoundPixel(End + FVector2D(0.0, 40.0)));
		LetGo(Driver);
		TestTrue(TEXT("With the handles off, the gradient is not dragged"), CastChecked<UDreamRectBlock>(Template->GetVisual())->GetBodyGradient() == Before);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerMaskSideTest,
	"DreamGUI.Designer.Driver.DraggingAClipsSideMovesItsMarginWritesTheDuiAndIsOneUndoStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerMaskSideTest::RunTest(const FString&)
{
	using namespace DreamDesignerEffectHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedActiveHandles Active(MaskHandles());
	FScopedDuiDesigner Scoped(TEXT("DesignerMaskSide"), MakeFile(TEXT("RectBlock"), {
		TEXT("Clipping = ClipToBounds"),
		TEXT("ClipMask.Shape = Ellipse") }));
	UDreamWidget* Template = nullptr;
	UDreamWidget* Preview = nullptr;
	if (!Prepare(*this, Scoped, Template, Preview))
	{
		return false;
	}
	const double PixelsPerUnit = MeasurePixelsPerUnit(*Scoped.Driver, Preview);
	DragAndUndo(*this, Scoped, Preview, MaskHandles(), Mask::FirstSide + 2, FVector2D(40.0, 0.0), [&](const FString& InFile)
	{
		const FMargin Margin = Template->GetClippingMargin();
		TestTrue(*FString::Printf(TEXT("The right side moved out 40 pixels: %.2f units"), Margin.Right), FMath::IsNearlyEqual(Margin.Right, 40.0 / PixelsPerUnit, 1.5));
		TestTrue(TEXT("...and no other"), Margin.Left == 0.0f && Margin.Top == 0.0f && Margin.Bottom == 0.0f);
		TestTrue(TEXT("The file says the margin"), InFile.Contains(TEXT("ClippingMargin")));
	});
	if (Refind(*this, Scoped, Template, Preview))
	{
		TestTrue(TEXT("The asset's margin is back"), Template->GetClippingMargin() == FMargin(0.0f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerMaskCornerFeatherTest,
	"DreamGUI.Designer.Driver.AClipsCornersRoundTogetherAndEscPutsAFeatherDragBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerMaskCornerFeatherTest::RunTest(const FString&)
{
	using namespace DreamDesignerEffectHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedActiveHandles Active(MaskHandles());
	FScopedDuiDesigner Scoped(TEXT("DesignerMaskCorner"), MakeFile(TEXT("RectBlock"), { TEXT("Clipping = ClipToBounds") }));
	UDreamWidget* Template = nullptr;
	UDreamWidget* Preview = nullptr;
	if (!Prepare(*this, Scoped, Template, Preview))
	{
		return false;
	}
	FDreamDesignerDriver& Driver = *Scoped.Driver;
	const double PixelsPerUnit = MeasurePixelsPerUnit(Driver, Preview);
	// The right-top corner's handle carried in along its diagonal, 20 pixels each way: a radius of 20 units at one to one.
	DragAndUndo(*this, Scoped, Preview, MaskHandles(), Mask::FirstCorner + 1, FVector2D(-20.0, 20.0), [&](const FString& InFile)
	{
		const FVector4f Radii = Template->GetClippingCornerRadius();
		TestTrue(*FString::Printf(TEXT("The corners rounded together: (%.2f, %.2f, %.2f, %.2f)"), Radii.X, Radii.Y, Radii.Z, Radii.W),
			FMath::IsNearlyEqual(Radii.Y, 20.0 / PixelsPerUnit, 1.5) && Radii.X == Radii.Y && Radii.Z == Radii.Y && Radii.W == Radii.Y);
		TestTrue(TEXT("The file says the corners"), InFile.Contains(TEXT("ClippingCornerRadius")));
	});

	// A feather dragged in, then Esc: back as it was, and no step left.
	if (!Refind(*this, Scoped, Template, Preview))
	{
		return false;
	}
	const FString Before = Scoped.ReadFile();
	const FDreamClipMask MaskBefore = Template->GetClipMask();
	FVector2D Feather;
	if (!TestTrue(TEXT("The left side's feather is on screen"), FindHandle(Driver, Preview, MaskHandles(), Mask::FirstFeather, Feather)))
	{
		return false;
	}
	const FUndoMark Mark = FUndoMark::Now();
	PressAndCarry(Driver, RoundPixel(Feather), RoundPixel(Feather + FVector2D(30.0, 0.0)));
	TestTrue(TEXT("While dragged, the preview's mask feathers"), Driver.PreviewFor(Template)->GetClipMask().Feather.Left > 0.0f);
	Driver.KeyDown(EKeys::Escape);
	Driver.PumpFrame();
	Driver.KeyUp(EKeys::Escape);
	LetGo(Driver);
	TestTrue(TEXT("Esc puts the preview's mask back"), Driver.PreviewFor(Template)->GetClipMask() == MaskBefore);
	TestTrue(TEXT("...the asset's is as it was"), Template->GetClipMask() == MaskBefore);
	TestEqualSensitive(TEXT("...the file too"), Scoped.ReadFile(), Before);
	TestTrue(TEXT("...and no undo step is left"), FUndoMark::Now() == Mark);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerGlassHandleTest,
	"DreamGUI.Designer.Driver.AGlasssCornersRoundTogetherAndItsLightTurnsToWhereItIsDragged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerGlassHandleTest::RunTest(const FString&)
{
	using namespace DreamDesignerEffectHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedActiveHandles Active(GlassHandles());
	FScopedDuiDesigner Scoped(TEXT("DesignerGlass"), MakeFile(TEXT("Glass"), {}));
	UDreamWidget* Template = nullptr;
	UDreamWidget* Preview = nullptr;
	if (!Prepare(*this, Scoped, Template, Preview))
	{
		return false;
	}
	if (!TestNotNull(TEXT("The Card is glass"), Cast<UDreamGlass>(Template->GetVisual())))
	{
		return false;
	}
	const double PixelsPerUnit = MeasurePixelsPerUnit(*Scoped.Driver, Preview);
	const FDreamUIGlassParams Before = CastChecked<UDreamGlass>(Template->GetVisual())->MakeParams();
	// The left-top corner (its default 24 units) carried in 10 pixels each way.
	DragAndUndo(*this, Scoped, Preview, GlassHandles(), Glass::FirstCorner + 2, FVector2D(10.0, 10.0), [&](const FString& InFile)
	{
		const FVector4f Radii = CastChecked<UDreamGlass>(Template->GetVisual())->MakeParams().CornerRadii;
		TestTrue(*FString::Printf(TEXT("All four corners rounded more, together: (%.2f, %.2f, %.2f, %.2f)"), Radii.X, Radii.Y, Radii.Z, Radii.W),
			FMath::IsNearlyEqual(Radii.Z, Before.CornerRadii.Z + 10.0 / PixelsPerUnit, 1.5) && Radii.X == Radii.Z && Radii.Y == Radii.Z && Radii.W == Radii.Z);
		TestTrue(TEXT("The file says the corners"), InFile.Contains(TEXT("CornerRadius")));
	});

	// The light dragged straight to the right of the centre: a light from 90 degrees (0 up, clockwise).
	if (!Refind(*this, Scoped, Template, Preview))
	{
		return false;
	}
	FVector2D Light;
	FVector2D Right;
	if (TestTrue(TEXT("The light's handle is on screen"), FindHandle(*Scoped.Driver, Preview, GlassHandles(), Glass::LightAngle, Light))
		&& TestTrue(TEXT("...and the place to take it"), LocalToPixel(*Scoped.Driver, Preview, Preview->GetLocalSpaceCenter() + FVector2D(60.0, 0.0), Right)))
	{
		DragAndUndo(*this, Scoped, Preview, GlassHandles(), Glass::LightAngle, Right - Light, [&](const FString& InFile)
		{
			const float Angle = CastChecked<UDreamGlass>(Template->GetVisual())->MakeParams().LightAngle;
			TestTrue(*FString::Printf(TEXT("The light comes from the right: %.2f degrees"), Angle), FMath::IsNearlyEqual(Angle, 90.0f, 2.0f));
			TestTrue(TEXT("The file says the light's angle"), InFile.Contains(TEXT("LightAngle")));
		});
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerStyleShadowTest,
	"DreamGUI.Designer.Driver.DraggingAShadowOnTheGlobalLightTurnsTheGlobalLightAndSetsItsDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerStyleShadowTest::RunTest(const FString&)
{
	using namespace DreamDesignerEffectHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedActiveHandles Active(StyleShadowHandles());
	FScopedDuiDesigner Scoped(TEXT("DesignerStyleShadow"), MakeFile(TEXT("RectBlock"), {
		TEXT("+ DreamLayerStyle { Style.DropShadows = [\"(Distance=12)\"]  Style.InnerShadows = [\"(Distance=6)\"] }") }));
	UDreamWidget* Template = nullptr;
	UDreamWidget* Preview = nullptr;
	if (!Prepare(*this, Scoped, Template, Preview))
	{
		return false;
	}
	const UDreamLayerStyle* Style = StyleOf(Template);
	if (!TestNotNull(TEXT("The Card has its layer style"), Style) || !TestEqual(TEXT("...a drop shadow on the global light"), Style->GetStyle().DropShadows.Num(), 1))
	{
		return false;
	}
	const double PixelsPerUnit = MeasurePixelsPerUnit(*Scoped.Driver, Preview);
	FVector2D Handle;
	FVector2D Below;
	if (!TestTrue(TEXT("The drop shadow's handle is on screen"), FindHandle(*Scoped.Driver, Preview, StyleShadowHandles(), Shadow::FirstDropShadow, Handle))
		|| !TestTrue(TEXT("...and the place to take it"), LocalToPixel(*Scoped.Driver, Preview, Preview->GetLocalSpaceCenter() + FVector2D(0.0, -30.0 / PixelsPerUnit), Below)))
	{
		return false;
	}
	// Straight down, 30 pixels from the centre: a light from above, 30 units away at one to one.
	DragAndUndo(*this, Scoped, Preview, StyleShadowHandles(), Shadow::FirstDropShadow, Below - Handle, [&](const FString& InFile)
	{
		const FDreamLayerEffects& After = StyleOf(Template)->GetStyle();
		TestTrue(*FString::Printf(TEXT("The global light comes from above: %.2f degrees"), After.GlobalLightAngle), FMath::IsNearlyEqual(After.GlobalLightAngle, 90.0f, 2.0f));
		TestTrue(*FString::Printf(TEXT("The shadow is 30 units away: %.2f"), After.DropShadows[0].Distance), FMath::IsNearlyEqual(After.DropShadows[0].Distance, 30.0f / static_cast<float>(PixelsPerUnit), 1.5f));
		TestEqual(TEXT("Its own angle is not what moved"), After.DropShadows[0].Angle, 120.0f);
		TestEqual(TEXT("The inner shadow on the global light keeps its distance"), After.InnerShadows[0].Distance, 6.0f);
		TestTrue(TEXT("The file says the global light"), InFile.Contains(TEXT("GlobalLightAngle")));
	});
	if (Refind(*this, Scoped, Template, Preview) && TestNotNull(TEXT("The Card's layer style is there again"), StyleOf(Template)))
	{
		TestEqual(TEXT("The global light is back"), StyleOf(Template)->GetStyle().GlobalLightAngle, 120.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerBlockShadowTest,
	"DreamGUI.Designer.Driver.DraggingARectBlocksShadowSetsItsAngleAndDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerBlockShadowTest::RunTest(const FString&)
{
	using namespace DreamDesignerEffectHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedActiveHandles Active(BlockShadowHandles());
	FScopedDuiDesigner Scoped(TEXT("DesignerBlockShadow"), MakeFile(TEXT("RectBlock"), {
		TEXT("bEnableOuterShadow = true"),
		TEXT("OuterShadowAngle = 45"),
		TEXT("OuterShadowDistance = 10") }));
	UDreamWidget* Template = nullptr;
	UDreamWidget* Preview = nullptr;
	if (!Prepare(*this, Scoped, Template, Preview))
	{
		return false;
	}
	const double PixelsPerUnit = MeasurePixelsPerUnit(*Scoped.Driver, Preview);
	FVector2D Handle;
	FVector2D Right;
	if (!TestTrue(TEXT("The shadow's handle is on screen"), FindHandle(*Scoped.Driver, Preview, BlockShadowHandles(), Shadow::RectBlockOuterShadow, Handle))
		|| !TestTrue(TEXT("...and the place to take it"), LocalToPixel(*Scoped.Driver, Preview, Preview->GetLocalSpaceCenter() + FVector2D(25.0 / PixelsPerUnit, 0.0), Right)))
	{
		return false;
	}
	// To the right of the centre: a light from the left.
	DragAndUndo(*this, Scoped, Preview, BlockShadowHandles(), Shadow::RectBlockOuterShadow, Right - Handle, [&](const FString& InFile)
	{
		const UDreamRectBlock* Block = CastChecked<UDreamRectBlock>(Template->GetVisual());
		TestTrue(*FString::Printf(TEXT("The light comes from the left: %.2f degrees"), Block->GetOuterShadowAngle()), FMath::IsNearlyEqual(Block->GetOuterShadowAngle(), 180.0f, 2.0f));
		TestTrue(*FString::Printf(TEXT("The shadow is 25 units away: %.2f"), Block->GetOuterShadowDistance()), FMath::IsNearlyEqual(Block->GetOuterShadowDistance(), 25.0f / static_cast<float>(PixelsPerUnit), 1.5f));
		TestTrue(TEXT("The file says the angle"), InFile.Contains(TEXT("OuterShadowAngle = 1")));
	});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerLightHandleTest,
	"DreamGUI.Designer.Driver.APointerLightsPlaceAndAStylesGlobalLightAreDraggedWhereTheyAre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerLightHandleTest::RunTest(const FString&)
{
	using namespace DreamDesignerEffectHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedActiveHandles Active(PointerLightHandles());
	FScopedDuiDesigner Scoped(TEXT("DesignerLights"), MakeFile(TEXT("RectBlock"), {
		TEXT("+ DreamPointerLight { bFollowPointer = false }"),
		TEXT("+ DreamLayerStyle { Style.DropShadows = [\"(Distance=12)\"] }") }));
	UDreamWidget* Template = nullptr;
	UDreamWidget* Preview = nullptr;
	if (!Prepare(*this, Scoped, Template, Preview))
	{
		return false;
	}
	const UDreamPointerLight* PointerLight = Template->GetComponent<UDreamPointerLight>();
	if (!TestNotNull(TEXT("The Card has its pointer light"), PointerLight))
	{
		return false;
	}
	const double PixelsPerUnit = MeasurePixelsPerUnit(*Scoped.Driver, Preview);
	// The light's place carried 40 pixels right and 20 down: up is +Y in the widget's space.
	DragAndUndo(*this, Scoped, Preview, PointerLightHandles(), Light::PointerLightPlace, FVector2D(40.0, 20.0), [&](const FString& InFile)
	{
		const FVector2D Place = Template->GetComponent<UDreamPointerLight>()->GetLightPosition();
		TestTrue(*FString::Printf(TEXT("The light stands right and below: (%.2f, %.2f)"), Place.X, Place.Y),
			FMath::IsNearlyEqual(Place.X, 40.0 / PixelsPerUnit, 1.5) && FMath::IsNearlyEqual(Place.Y, -20.0 / PixelsPerUnit, 1.5));
		TestTrue(TEXT("The file says its place"), InFile.Contains(TEXT("LightPosition")));
	});

	// The global light taken to the disc's left rim: a light from the left, at no altitude. It is a kind of its own.
	FDreamDesignerHandles::SetActive(GlobalLightHandles());
	if (!Refind(*this, Scoped, Template, Preview))
	{
		return false;
	}
	FVector2D Global;
	FVector2D Rim;
	if (TestTrue(TEXT("The global light's handle is on screen"), FindHandle(*Scoped.Driver, Preview, GlobalLightHandles(), Light::GlobalLight, Global))
		&& TestTrue(TEXT("...and the place to take it"), LocalToPixel(*Scoped.Driver, Preview, Preview->GetLocalSpaceCenter() + FVector2D(-70.0 / PixelsPerUnit, 0.0), Rim)))
	{
		DragAndUndo(*this, Scoped, Preview, GlobalLightHandles(), Light::GlobalLight, Rim - Global, [&](const FString& InFile)
		{
			const FDreamLayerEffects& After = StyleOf(Template)->GetStyle();
			TestTrue(*FString::Printf(TEXT("The global light comes from the left: %.2f degrees"), After.GlobalLightAngle), FMath::IsNearlyEqual(FMath::Abs(After.GlobalLightAngle), 180.0f, 2.0f));
			TestTrue(*FString::Printf(TEXT("...at the rim, no altitude: %.2f"), After.GlobalLightAltitude), After.GlobalLightAltitude < 1.0f);
			TestTrue(TEXT("The file says the global light"), InFile.Contains(TEXT("GlobalLightAngle")) && InFile.Contains(TEXT("GlobalLightAltitude")));
		});
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerEffectHandlePixelTest,
	"DreamGUI.Designer.RHI.EachKindOfEffectHandlesIsDrawnOverItsSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * The designer's viewport drawn by a real RHI with each of four widgets selected in turn and its kind of handles shown -- a
 * star mask, a glass, a layer style's shadows, a pointer light and a style's global light -- then with the handles off:
 * where one of its handles stands the two pictures differ. The pictures are kept in Saved/DreamGUITests/Captures
 * (Designer_Handles_*.png).
 */
bool FDreamDesignerEffectHandlePixelTest::RunTest(const FString&)
{
	using namespace DreamDesignerEffectHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedActiveHandles Active(NAME_None);
	FScopedDuiDesigner Scoped(TEXT("DesignerEffectHandlePixels"), {
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    RectBlock MaskCard {"),
		TEXT("        AnchorData.SizeDelta = (200, 130)"),
		TEXT("        AnchorData.AnchoredPosition = (-220, 100)"),
		TEXT("        Color = #4A90D9FF"),
		TEXT("        Clipping = ClipToBounds"),
		TEXT("        ClipMask.Shape = Star"),
		TEXT("        ClipMask.Sides = 5"),
		TEXT("        ClipMask.Feather = (8, 8, 8, 8)"),
		TEXT("    }"),
		TEXT("    Glass GlassPane {"),
		TEXT("        AnchorData.SizeDelta = (200, 130)"),
		TEXT("        AnchorData.AnchoredPosition = (220, 100)"),
		TEXT("    }"),
		TEXT("    RectBlock ShadowCard {"),
		TEXT("        AnchorData.SizeDelta = (200, 130)"),
		TEXT("        AnchorData.AnchoredPosition = (-220, -110)"),
		TEXT("        Color = #E8E2D6FF"),
		TEXT("        + DreamLayerStyle {"),
		TEXT("            Style.DropShadows = [\"(Distance=18,Size=12)\"]"),
		TEXT("            Style.InnerShadows = [\"(bUseGlobalLight=False,Angle=-60,Distance=8)\"]"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("    RectBlock LightCard {"),
		TEXT("        AnchorData.SizeDelta = (200, 130)"),
		TEXT("        AnchorData.AnchoredPosition = (220, -110)"),
		TEXT("        Color = #6B5B95FF"),
		TEXT("        + DreamPointerLight {"),
		TEXT("            bFollowPointer = false"),
		TEXT("            LightPosition = (40, 20)"),
		TEXT("            Radius = 90"),
		TEXT("        }"),
		TEXT("        + DreamLayerStyle {"),
		TEXT("            Style.BevelEmboss.bEnabled = true"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (!Scoped.IsReady())
	{
		AddError(FString::Printf(TEXT("No text-authored designer to drive: %s."), *Scoped.Failure));
		return false;
	}
	FDreamDesignerDriver& Driver = *Scoped.Driver;
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	auto Draw = [&Driver](TArray<FColor>& OutPixels, FIntPoint& OutSize)
	{
		for (int32 Frame = 0; Frame < 6; ++Frame)
		{
			Driver.PumpFrame();
			Driver.DrawFrame();
		}
		FlushRenderingCommands();
		const TSharedPtr<FSceneViewport> Viewport = Driver.SceneViewport();
		return Viewport.IsValid() && UDreamUICaptureLibrary::ReadViewportPixels(Viewport.Get(), OutPixels, OutSize) && OutPixels.Num() == OutSize.X * OutSize.Y;
	};
	struct FCase
	{
		const TCHAR* Widget;
		FName Provider;
		int32 HandleId;
		const TCHAR* Capture;
	};
	const FCase Cases[] = {
		{ TEXT("MaskCard"), MaskHandles(), Mask::InnerRatio, TEXT("Designer_Handles_Mask") },
		{ TEXT("GlassPane"), GlassHandles(), Glass::LightAngle, TEXT("Designer_Handles_Glass") },
		{ TEXT("ShadowCard"), StyleShadowHandles(), Shadow::FirstDropShadow, TEXT("Designer_Handles_Shadow") },
		{ TEXT("LightCard"), GlobalLightHandles(), Light::GlobalLight, TEXT("Designer_Handles_Light") },
	};
	// Away from every widget, so that no hover outline is drawn over one.
	Driver.MoveTo(FIntPoint(4, 4));
	for (const FCase& Case : Cases)
	{
		UDreamWidget* Template = Scoped.FindTemplate(Case.Widget);
		if (!TestNotNull(*FString::Printf(TEXT("%s is in the asset"), Case.Widget), Template))
		{
			continue;
		}
		SelectOnlyInDesigner(Driver, Template);
		Driver.PumpFrame();
		UDreamWidget* Preview = Driver.PreviewFor(Template);
		FVector2D At;
		if (!TestTrue(*FString::Printf(TEXT("%s's handle is on screen"), Case.Widget), FindHandle(Driver, Preview, Case.Provider, Case.HandleId, At)))
		{
			continue;
		}
		FDreamDesignerHandles::SetActive(Case.Provider);
		TArray<FColor> Shown;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!TestTrue(TEXT("The viewport reads back with the handles shown"), Draw(Shown, Size)))
		{
			continue;
		}
		FDreamPixelProbe::SaveCapture(Shown, Size, Case.Capture);
		FDreamDesignerHandles::SetActive(FDreamDesignerHandles::Off);
		TArray<FColor> Hidden;
		FIntPoint HiddenSize = FIntPoint::ZeroValue;
		if (!TestTrue(TEXT("...and with them off"), Draw(Hidden, HiddenSize)) || !TestEqual(TEXT("of one size"), HiddenSize, Size))
		{
			continue;
		}
		const FIntPoint Pixel(FMath::Clamp(FMath::RoundToInt32(At.X), 0, Size.X - 1), FMath::Clamp(FMath::RoundToInt32(At.Y), 0, Size.Y - 1));
		const FColor& With = Shown[Pixel.Y * Size.X + Pixel.X];
		const FColor& Without = Hidden[Pixel.Y * Size.X + Pixel.X];
		TestTrue(*FString::Printf(TEXT("%s: the handle is drawn at %s: %s shown, %s off"), Case.Widget, *Pixel.ToString(), *With.ToString(), *Without.ToString()),
			FMath::Max3(FMath::Abs(With.R - Without.R), FMath::Abs(With.G - Without.G), FMath::Abs(With.B - Without.B)) > 40);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerShadowWhileStyleDialogTest,
	"DreamGUI.Designer.Driver.AShadowDraggedWhileTheLayerStyleDialogEditsTheWidgetMovesTheDialogsStyleAndWritesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The Layer Style dialog's session editing the Card (the dialog without its window), and the Card's drop shadow on the
 * global light dragged straight down: the session's global light comes from above and it has a change to write, while the
 * template and the file are as they were. Committed, the session writes the light into the file, and one undo takes it
 * back. A try that is not kept, as Esc ends a drag, puts the session's style back.
 */
bool FDreamDesignerShadowWhileStyleDialogTest::RunTest(const FString&)
{
	using namespace DreamDesignerEffectHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedActiveHandles Active(StyleShadowHandles());
	FScopedDuiDesigner Scoped(TEXT("DesignerShadowStyleDialog"), MakeFile(TEXT("RectBlock"), {
		TEXT("+ DreamLayerStyle { Style.DropShadows = [\"(Distance=12)\"] }") }));
	UDreamWidget* Template = nullptr;
	UDreamWidget* Preview = nullptr;
	if (!Prepare(*this, Scoped, Template, Preview))
	{
		return false;
	}
	const TSharedPtr<FDreamUILayerStyleSession> Session = FDreamUILayerStyleSession::Begin(*Scoped.Driver->Toolkit(), { Preview });
	if (!TestTrue(TEXT("A Layer Style session edits the Card"), Session.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("...and is the one found editing it"), FDreamUILayerStyleSession::FindEditing(Preview) == Session);
	Scoped.Driver->PumpFrame();
	const FString Before = Scoped.ReadFile();

	// A try not kept puts the working copy back.
	FDreamLayerEffects Tried = Session->GetStyle();
	Tried.GlobalLightAngle = 10.0f;
	Session->TryStyle(Tried);
	TestEqual(TEXT("A tried style is the working copy"), Session->GetStyle().GlobalLightAngle, 10.0f);
	Session->EndTry(/*bInKeep*/false);
	TestEqual(TEXT("...and not kept, it is put back"), Session->GetStyle().GlobalLightAngle, 120.0f);
	TestFalse(TEXT("...and is no change"), Session->IsDirty());

	const double PixelsPerUnit = MeasurePixelsPerUnit(*Scoped.Driver, Preview);
	FVector2D Handle;
	FVector2D Below;
	if (!TestTrue(TEXT("The drop shadow's handle is on screen"), FindHandle(*Scoped.Driver, Preview, StyleShadowHandles(), Shadow::FirstDropShadow, Handle))
		|| !TestTrue(TEXT("...and the place to take it"), LocalToPixel(*Scoped.Driver, Preview, Preview->GetLocalSpaceCenter() + FVector2D(0.0, -30.0 / PixelsPerUnit), Below)))
	{
		Session->Cancel();
		return false;
	}
	PressAndCarry(*Scoped.Driver, RoundPixel(Handle), RoundPixel(Below));
	LetGo(*Scoped.Driver);
	TestTrue(*FString::Printf(TEXT("The session's global light comes from above: %.2f degrees"), Session->GetStyle().GlobalLightAngle),
		FMath::IsNearlyEqual(Session->GetStyle().GlobalLightAngle, 90.0f, 2.0f));
	TestTrue(TEXT("...a change the session has to write"), Session->IsDirty());
	TestEqualSensitive(TEXT("The viewport wrote nothing to the file"), Scoped.ReadFile(), Before);
	TestTrue(TEXT("...nor to the template"), StyleOf(Template) != nullptr && StyleOf(Template)->GetStyle().GlobalLightAngle == 120.0f);

	TestEqual(TEXT("Committed, the session writes the Card"), Session->Commit(), 1);
	TestTrue(TEXT("...the global light into the file"), Scoped.ReadFile().Contains(TEXT("GlobalLightAngle")));
	TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
	Scoped.Driver->PumpFrame();
	TestEqualSensitive(TEXT("One undo takes the file back"), Scoped.ReadFile(), Before);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
