// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamLayerStyleTypes.h"
#include "DetailCustomization/DreamAngleGlobalLight.h"
#include "DetailCustomization/PropertyType/DreamAngleCustomization.h"
#include "DetailCustomization/PropertyType/DreamBlendModeCustomization.h"
#include "DetailCustomization/PropertyType/DreamCurveCustomizations.h"
#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"
#include "DreamTweener.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Engine/World.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIValueFormat.h"
#include "UObject/StructOnScope.h"
#include "Widget/SDreamAngleWheel.h"
#include "Widget/SDreamCurveEditor.h"

/*
 * The editing controls: the angle dial and the global light it can turn, the curves of contours and eases, and the blend
 * mode menu's pictures and hover preview. What can be held to without a pointer is: the dial's arithmetic, which angle it
 * edits, the curve edits a pointer makes, the contour's .dui spelling both ways, every mode's picture, and that a preview
 * writes nothing anyone keeps.
 */
namespace DreamEditorControlsTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	TSharedPtr<IPropertyRowGenerator> RowsFor(UObject* InObject)
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		TSharedPtr<IPropertyRowGenerator> Generator = PropertyEditor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
		Generator->SetObjects({ InObject });
		return Generator;
	}

	/** The handle, depth first, whose property path ends with InPathTail ("DropShadows[0].Angle" ends "Angle" under "DropShadows"). */
	TSharedPtr<IPropertyHandle> FindHandle(const TSharedRef<IDetailTreeNode>& InNode, const FString& InPathTail)
	{
		if (TSharedPtr<IPropertyHandle> Handle = InNode->CreatePropertyHandle(); Handle.IsValid() && Handle->IsValidHandle())
		{
			if (Handle->GeneratePathToProperty().EndsWith(InPathTail))
			{
				return Handle;
			}
		}
		TArray<TSharedRef<IDetailTreeNode>> Children;
		InNode->GetChildren(Children, /*bInIgnoreVisibility*/true);
		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			if (TSharedPtr<IPropertyHandle> Found = FindHandle(Child, InPathTail))
			{
				return Found;
			}
		}
		return nullptr;
	}

	TSharedPtr<IPropertyHandle> FindHandle(const TSharedPtr<IPropertyRowGenerator>& InRows, const FString& InPathTail)
	{
		for (const TSharedRef<IDetailTreeNode>& Root : InRows->GetRootTreeNodes())
		{
			if (TSharedPtr<IPropertyHandle> Found = FindHandle(Root, InPathTail))
			{
				return Found;
			}
		}
		return nullptr;
	}

	FDreamUIValue Value(EDreamUIValueKind InKind, const FString& InRaw)
	{
		FDreamUIValue Out;
		Out.Kind = InKind;
		Out.Raw = InRaw;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAngleWheelArithmeticTest,
	"DreamGUI.Editor.Controls.TheAngleDialReadsAndDrawsEachConventionTheWayItRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAngleWheelArithmeticTest::RunTest(const FString& Parameters)
{
	struct FCase { EDreamAngleConvention Convention; FVector2D Offset; float Degrees; const TCHAR* What; };
	for (const FCase& Case : {
		FCase{ EDreamAngleConvention::RightCounterClockwise, FVector2D(0.0, -10.0), 90.0f, TEXT("counter-clockwise from three o'clock: straight up is 90") },
		FCase{ EDreamAngleConvention::RightCounterClockwise, FVector2D(-10.0, 0.0), 180.0f, TEXT("...and nine o'clock 180") },
		FCase{ EDreamAngleConvention::RightClockwise, FVector2D(0.0, 10.0), 90.0f, TEXT("clockwise from three o'clock: straight down is 90") },
		FCase{ EDreamAngleConvention::TopClockwise, FVector2D(10.0, 0.0), 90.0f, TEXT("a clock's: three o'clock is 90") },
		FCase{ EDreamAngleConvention::TopClockwise, FVector2D(0.0, -10.0), 0.0f, TEXT("...and twelve 0") } })
	{
		const float Read = DreamAngleWheel::Wrap(DreamAngleWheel::AngleOf(Case.Offset, Case.Convention), false);
		TestEqual(Case.What, Read, DreamAngleWheel::Wrap(Case.Degrees, false), 0.01f);
		const FVector2D Drawn = DreamAngleWheel::DirectionOf(Case.Degrees, Case.Convention);
		TestTrue(FString::Printf(TEXT("%s, drawn the same way (%s)"), Case.What, *Drawn.ToString()), Drawn.Equals(Case.Offset.GetSafeNormal(), 0.001));
	}
	TestEqual(TEXT("Signed angles wrap into (-180, 180]"), DreamAngleWheel::Wrap(270.0f, true), -90.0f, 0.001f);
	TestEqual(TEXT("...with a half turn kept as 180, as a light's angle is"), DreamAngleWheel::Wrap(-180.0f, true), 180.0f, 0.001f);
	TestEqual(TEXT("Unsigned angles wrap into [0, 360)"), DreamAngleWheel::Wrap(-45.0f, false), 315.0f, 0.001f);
	TestEqual(TEXT("\"TopCW\" names a clock's convention"), static_cast<int32>(DreamAngleWheel::ParseConvention(TEXT("TopCW"))), static_cast<int32>(EDreamAngleConvention::TopClockwise));
	TestEqual(TEXT("No value names an image editor's"), static_cast<int32>(DreamAngleWheel::ParseConvention(FString())), static_cast<int32>(EDreamAngleConvention::RightCounterClockwise));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAngleWheelGlobalLightTest,
	"DreamGUI.Editor.Controls.AnEffectUsingTheGlobalLightTurnsTheStylesGlobalAngle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A layer style with a drop shadow: its angle is a dial (it carries the metadata), and while the shadow uses the global
 * light the angle the dial edits is the style's global light angle, found up the shadow's parents.
 */
bool FDreamAngleWheelGlobalLightTest::RunTest(const FString& Parameters)
{
	using namespace DreamEditorControlsTestLocal;
	FScopedTestWorld TestWorld;
	UDreamWidget* Widget = NewObject<UDreamWidget>(TestWorld.World);
	Widget->CreateNewVisual<UDreamRectBlock>();
	UDreamLayerStyle* Style = Widget->AddComponent<UDreamLayerStyle>();
	FDreamLayerEffects Effects;
	Effects.DropShadows.AddDefaulted();
	Effects.GlobalLightAngle = 33.0f;
	Style->SetStyle(Effects);
	const TSharedPtr<IPropertyRowGenerator> Rows = RowsFor(Style);
	const TSharedPtr<IPropertyHandle> Angle = FindHandle(Rows, TEXT("DropShadows[0].Angle"));
	if (!TestTrue(TEXT("The drop shadow's angle has a row"), Angle.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("...that the dial customizes"), FDreamAngleWheelIdentifier().IsPropertyTypeCustomized(*Angle));
	const TSharedPtr<IPropertyHandle> Global = FDreamAngleCustomization::FindGlobalAngle(Angle.ToSharedRef());
	float GlobalValue = 0.0f;
	TestTrue(TEXT("The style's global light angle is found up the shadow's parents"), Global.IsValid() && Global->GetValue(GlobalValue) == FPropertyAccess::Success && FMath::IsNearlyEqual(GlobalValue, 33.0f));
	TestTrue(TEXT("A new shadow uses the global light"), FDreamAngleCustomization::UsesGlobalLight(Angle.ToSharedRef()));
	// An angle with no global light to share is a dial and nothing more.
	const TSharedPtr<IPropertyHandle> Satin = FindHandle(Rows, TEXT("Satin.Angle"));
	TestTrue(TEXT("The satin's angle is a dial with no global light"), Satin.IsValid() && !FDreamAngleCustomization::FindGlobalAngle(Satin.ToSharedRef()).IsValid());

	// A shadow shown on its own, as a dialog shows its working copy, apart from the style: the walk up finds no global light,
	// and the resolver the dialog adds while it is open does.
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	const TSharedPtr<FStructOnScope> AloneShadow = MakeShared<FStructOnScope>(FDreamStyleDropShadow::StaticStruct());
	const TSharedRef<IPropertyRowGenerator> AloneRows = PropertyEditor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
	AloneRows->SetStructure(AloneShadow);
	const TSharedPtr<IPropertyHandle> AloneAngle = FindHandle(TSharedPtr<IPropertyRowGenerator>(AloneRows), TEXT("Angle"));
	const TSharedPtr<IPropertyHandle> StyleGlobal = FindHandle(Rows, TEXT("GlobalLightAngle"));
	if (TestTrue(TEXT("A shadow shown on its own has an angle row, and the style a global light row"), AloneAngle.IsValid() && StyleGlobal.IsValid()))
	{
		TestTrue(TEXT("...the shadow on its own uses the global light, read beside its angle"), FDreamAngleCustomization::UsesGlobalLight(AloneAngle.ToSharedRef()));
		TestFalse(TEXT("...and finds none up its handles"), DreamAngleGlobalLight::Find(AloneAngle.ToSharedRef()).IsValid());
		const int32 Resolver = DreamAngleGlobalLight::AddResolver([AloneAngle, StyleGlobal](const TSharedRef<IPropertyHandle>& InAngle)
		{
			return InAngle == AloneAngle.ToSharedRef() ? StyleGlobal : TSharedPtr<IPropertyHandle>();
		});
		TestTrue(TEXT("...finds it through the resolver its view adds"), DreamAngleGlobalLight::Find(AloneAngle.ToSharedRef()) == StyleGlobal);
		DreamAngleGlobalLight::RemoveResolver(Resolver);
		TestFalse(TEXT("...and not once the resolver is removed"), DreamAngleGlobalLight::Find(AloneAngle.ToSharedRef()).IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCurveEditsTest,
	"DreamGUI.Editor.Controls.TheCurveEditorAddsMovesRemovesAndCornersPointsAndKeepsItsEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCurveEditsTest::RunTest(const FString& Parameters)
{
	const TArray<FDreamCurvePoint> Line = { FDreamCurvePoint(0.0f, 0.0f), FDreamCurvePoint(1.0f, 1.0f) };
	int32 Added = INDEX_NONE;
	TArray<FDreamCurvePoint> Points = SDreamCurveEditor::AddPoint(Line, FVector2f(0.5f, 0.9f), Added);
	TestEqual(TEXT("A point added in the middle goes between the ends"), Added, 1);
	TestEqual(TEXT("...and there are three"), Points.Num(), 3);
	Points = SDreamCurveEditor::MovePoint(Points, 1, FVector2f(1.5f, 0.2f));
	TestTrue(TEXT("A point moved past its neighbour stops short of it"), Points[1].X < 1.0f && Points[1].X > 0.5f);
	Points = SDreamCurveEditor::MovePoint(Points, 0, FVector2f(0.4f, 0.3f));
	TestEqual(TEXT("The first point stays at 0 across"), Points[0].X, 0.0f);
	TestEqual(TEXT("...and moves up and down"), Points[0].Y, 0.3f, 0.0001f);
	Points = SDreamCurveEditor::ToggleCorner(Points, 1);
	TestTrue(TEXT("A point is made a corner"), Points[1].bCorner);
	TestEqual(TEXT("An end is never removed"), SDreamCurveEditor::RemovePoint(Points, 0).Num(), 3);
	TestEqual(TEXT("A middle point is"), SDreamCurveEditor::RemovePoint(Points, 1).Num(), 2);

	// A named contour's handles are samples of it, and the custom curve they make is the same curve at the samples.
	FDreamStyleContour Gaussian;
	Gaussian.Shape = EDreamStyleContourShape::Gaussian;
	const TArray<FDreamCurvePoint> Samples = DreamCurveCustomizations::PointsOf(Gaussian);
	const FDreamStyleContour Custom = DreamCurveCustomizations::ContourOf(Samples);
	TestEqual(TEXT("Editing a named shape starts from a custom curve"), static_cast<int32>(Custom.Shape), static_cast<int32>(EDreamStyleContourShape::Custom));
	for (const FDreamCurvePoint& Sample : Samples)
	{
		TestEqual(*FString::Printf(TEXT("...through the shape's own value at %.3f"), Sample.X), Custom.Evaluate(Sample.X), Gaussian.Evaluate(Sample.X), 0.001f);
	}
	TestEqual(TEXT("A linear ease at the middle is the middle"), DreamCurveCustomizations::EvaluateEase(EDreamTweenEase::Linear, 0.5f), 0.5f, 0.0001f);
	TestEqual(TEXT("A quadratic ease in at the middle is a quarter"), DreamCurveCustomizations::EvaluateEase(EDreamTweenEase::InQuad, 0.5f), 0.25f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamContourSpellingTest,
	"DreamGUI.Text.AContourIsANamedShapesWordOrItsCustomPointsAndReadsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A contour's .dui spelling: a named shape as its word, a custom curve as custom(x y, x y corner, ...) in a string; read
 * back field for field through the value format, which is how a .dui line becomes the property and the property a line.
 */
bool FDreamContourSpellingTest::RunTest(const FString& Parameters)
{
	using namespace DreamEditorControlsTestLocal;
	const FProperty* Property = FindFProperty<FProperty>(FDreamStyleBevelEmboss::StaticStruct(), TEXT("Contour"));
	if (!TestNotNull(TEXT("A bevel has a contour"), Property) || !TestTrue(TEXT("...with a short form"), DreamUIValueFormat::HasShortForm(Property)))
	{
		return false;
	}
	FDreamStyleContour Named;
	Named.Shape = EDreamStyleContourShape::RingDouble;
	FString Text;
	TestTrue(TEXT("A named contour prints"), DreamUIValueFormat::Print(Property, &Named, Text));
	TestEqual(TEXT("...as its word"), Text, FString(TEXT("RingDouble")));
	FDreamStyleContour Back;
	TestTrue(TEXT("The word reads back"), DreamUIValueFormat::Parse(Property, Value(EDreamUIValueKind::Identifier, TEXT("RingDouble")), &Back));
	TestTrue(TEXT("...as the same contour"), Back == Named);
	TestTrue(TEXT("A name in another case reads too"), DreamUIValueFormat::Parse(Property, Value(EDreamUIValueKind::Identifier, TEXT("gaussian")), &Back) && Back.Shape == EDreamStyleContourShape::Gaussian);

	FDreamStyleContour Custom;
	Custom.Shape = EDreamStyleContourShape::Custom;
	Custom.Points = { FDreamStyleContourPoint(0.0f, 0.0f), FDreamStyleContourPoint(0.5f, 1.0f, true), FDreamStyleContourPoint(1.0f, 0.25f) };
	TestTrue(TEXT("A custom contour prints"), DreamUIValueFormat::Print(Property, &Custom, Text));
	TestEqual(TEXT("...as its points in a string"), Text, FString(TEXT("\"custom(0 0, 0.5 1 corner, 1 0.25)\"")));
	FDreamStyleContour CustomBack;
	TestTrue(TEXT("The points read back"), DreamUIValueFormat::Parse(Property, Value(EDreamUIValueKind::String, TEXT("custom(0 0, 0.5 1 corner, 1 0.25)")), &CustomBack));
	TestTrue(TEXT("...as the same contour, corner and all"), CustomBack == Custom);

	FDreamStyleContour Untouched = Named;
	TestFalse(TEXT("Text that is neither is refused"), DreamUIValueFormat::Parse(Property, Value(EDreamUIValueKind::String, TEXT("custom(0 zero)")), &Untouched));
	TestTrue(TEXT("...and leaves the contour as it was"), Untouched == Named);
	TestFalse(TEXT("Custom is not a name to write"), FDreamStyleContour::ParseText(TEXT("Custom"), Untouched));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBlendModePicturesTest,
	"DreamGUI.Editor.Controls.EveryBlendModeHasAPictureOfWhatItDoesAndTheMenuListsEachOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBlendModePicturesTest::RunTest(const FString& Parameters)
{
	TSet<int32> Listed;
	for (const TArray<EDreamUIBlendMode>& Group : FDreamBlendModeCustomization::GetGroups())
	{
		for (const EDreamUIBlendMode Mode : Group)
		{
			TestFalse(*FString::Printf(TEXT("%s is listed once"), *FDreamBlendModeCustomization::GetModeName(Mode).ToString()), Listed.Contains(static_cast<int32>(Mode)));
			Listed.Add(static_cast<int32>(Mode));
		}
	}
	TestEqual(TEXT("The menu lists all the modes"), Listed.Num(), DreamUIBlend::NumModes);

	TArray<FColor> Normal;
	FDreamBlendModeThumbnails::Render(EDreamUIBlendMode::Alpha, Normal);
	const FIntPoint Size = FDreamBlendModeThumbnails::GetSize();
	TestEqual(TEXT("A picture is its size"), Normal.Num(), Size.X * Size.Y);
	TSet<uint32> Distinct;
	for (int32 Index = 0; Index < DreamUIBlend::NumModes; ++Index)
	{
		TArray<FColor> Pixels;
		FDreamBlendModeThumbnails::Render(static_cast<EDreamUIBlendMode>(Index), Pixels);
		Distinct.Add(FCrc::MemCrc32(Pixels.GetData(), Pixels.Num() * sizeof(FColor)));
		// Away from the disc the backdrop shows as it is, in every mode.
		TestEqual(*FString::Printf(TEXT("Mode %d leaves the corner as it is"), Index), Pixels[0], Normal[0]);
	}
	TestTrue(*FString::Printf(TEXT("The pictures differ (%d distinct of %d)"), Distinct.Num(), DreamUIBlend::NumModes), Distinct.Num() >= DreamUIBlend::NumModes - 1);
	// In the middle of the disc, normal is the element and multiply darker than either.
	const FColor NormalCentre = Normal[(Size.Y / 2) * Size.X + Size.X / 2];
	TArray<FColor> Multiply;
	FDreamBlendModeThumbnails::Render(EDreamUIBlendMode::Multiply, Multiply);
	const FColor MultiplyCentre = Multiply[(Size.Y / 2) * Size.X + Size.X / 2];
	TestTrue(TEXT("Multiply is darker than normal at the disc's centre"), MultiplyCentre.R + MultiplyCentre.G + MultiplyCentre.B < NormalCentre.R + NormalCentre.G + NormalCentre.B);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBlendModePreviewTest,
	"DreamGUI.Designer.Controls.HoveringABlendModeShowsItOnThePreviewAndKeepsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A rect block in a designer: a preview of a blend mode on its preview visual -- what hovering a mode in the menu does --
 * changes the preview, not the asset, and records no transaction; ending it puts the preview back; a second mode hovered
 * before the first ended still ends on the original.
 */
bool FDreamBlendModePreviewTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests;
	using namespace DreamEditorControlsTestLocal;
	FScopedDesignerSession Session(TEXT("BlendModePreview"), /*bGiveRootAPanel*/true);
	if (!TestTrue(FString::Printf(TEXT("The designer came up (%s)"), *Session.GetFailure()), Session.IsReady()) || !TestNotNull(TEXT("The editor has an undo buffer"), GEditor->Trans.Get()))
	{
		return false;
	}
	// Placed from the palette, as an author places one: the template and its preview both get the rect block.
	FDreamDesignerDriver& Driver = Session.GetDriver();
	UDreamWidget* Template = DropOntoRootAndFindTemplate(Driver, UDreamRectBlock::StaticClass(), Driver.ViewportCentrePixel());
	if (!TestNotNull(TEXT("A rect block was placed from the palette"), Template))
	{
		return false;
	}
	Driver.PumpFrame();
	UDreamWidget* Preview = Driver.PreviewFor(Template);
	UDreamRectBlock* PreviewBlock = Preview ? Cast<UDreamRectBlock>(Preview->GetVisual()) : nullptr;
	UDreamRectBlock* TemplateBlock = Cast<UDreamRectBlock>(Template->GetVisual());
	if (!TestNotNull(TEXT("The preview has its rect block"), PreviewBlock) || !TestNotNull(TEXT("...and the template its own"), TemplateBlock))
	{
		return false;
	}
	// The blend mode row the Layer category shows for the widget: what the menu is on.
	const TSharedPtr<IPropertyRowGenerator> Rows = RowsFor(Preview);
	const TSharedPtr<IPropertyHandle> Blend = FindHandle(Rows, TEXT("BlendMode"));
	if (!TestTrue(TEXT("The blend mode has a handle"), Blend.IsValid()))
	{
		return false;
	}
	const int32 UndoBefore = GEditor->Trans->GetQueueLength();
	FDreamPropertyPreview PreviewMode;
	PreviewMode.Begin(Blend.ToSharedRef(), TEXT("Multiply"));
	TestEqual(TEXT("Hovering Multiply shows it on the preview"), static_cast<int32>(PreviewBlock->GetBlendMode()), static_cast<int32>(EDreamUIBlendMode::Multiply));
	PreviewMode.Begin(Blend.ToSharedRef(), TEXT("Screen"));
	TestEqual(TEXT("Moving on to Screen shows that"), static_cast<int32>(PreviewBlock->GetBlendMode()), static_cast<int32>(EDreamUIBlendMode::Screen));
	TestEqual(TEXT("The asset is untouched"), static_cast<int32>(TemplateBlock->GetBlendMode()), static_cast<int32>(EDreamUIBlendMode::Alpha));
	PreviewMode.End();
	TestEqual(TEXT("Leaving the menu puts the preview back"), static_cast<int32>(PreviewBlock->GetBlendMode()), static_cast<int32>(EDreamUIBlendMode::Alpha));
	TestEqual(TEXT("Nothing was recorded for undo"), GEditor->Trans->GetQueueLength(), UndoBefore);
	return true;
}

#endif
