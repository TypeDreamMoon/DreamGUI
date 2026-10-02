// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Engine/World.h"
#include "MeshModifier/DreamMeshModifierMirror.h"
#include "DreamScopedWorld.h"

/*
 * The two UMG panels that had no counterpart here.
 *
 * Border earns a class for exactly one reason: its HorizontalAlignment and VerticalAlignment belong to
 * the BORDER and say where it puts its content, while everywhere else in this plugin alignment belongs
 * to the slot and says where the child sits in what it was given. Everything else about a border --
 * padding, a coloured background -- was already expressible as an overlay with a rect block, and an
 * author coming from UMG had nowhere to put the one property that was not.
 *
 * MenuAnchor is here for its placement arithmetic, which is the half of UMenuAnchor that has a right
 * answer. The popup's lifetime is not: UIDropdown and DreamUIModal each already own a version of that.
 */

namespace DreamBorderMenuAnchorTestLocal
{
	using DreamTests::FScopedGameWorld;

	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBorderAlignsItsContentItselfTest,
	"DreamGUI.Border.TheBorderSaysWhereItsContentSitsRatherThanTheContentSlotSayingIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBorderAlignsItsContentItselfTest::RunTest(const FString& Parameters)
{
	using namespace DreamBorderMenuAnchorTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* BorderWidget = MakeWidget(TestWorld.World, nullptr, TEXT("Border"), 300.0f, 200.0f);
	UDreamWidget* Content = MakeWidget(TestWorld.World, BorderWidget, TEXT("Content"), 100.0f, 50.0f);
	UDreamLayoutContainerBorder* Border = BorderWidget->CreateNewLayoutContainer<UDreamLayoutContainerBorder>();
	if (!TestNotNull(TEXT("Border created"), Border))
	{
		return false;
	}

	// Fill on both axes to start with: the content takes the whole padded box, as an overlay would.
	UDreamWidget::MarkLayoutForRebuild(BorderWidget);
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("Filling, the content takes the whole box"),
		FMath::IsNearlyEqual(Content->GetWidth(), 300.0f, 0.01f)
		&& FMath::IsNearlyEqual(Content->GetHeight(), 200.0f, 0.01f));

	// Now the property that has no other home. The content's own slot is still Fill; the BORDER says
	// Center, and the border wins -- which is the whole of UBorder::HorizontalAlignment.
	Border->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
	Border->SetVerticalAlignment(EDreamPanelVerticalAlignment::Top);
	if (UDreamPanelSlot* Slot = Content->GetPanelSlot(); TestNotNull(TEXT("Content has a slot"), Slot))
	{
		TestTrue(TEXT("...and the slot itself still says Fill"),
			Slot->HorizontalAlignment == EDreamPanelHorizontalAlignment::Fill);
	}
	UDreamWidget::MarkLayoutForRebuild(BorderWidget);
	Manager->TickDreamUI(0.016f);

	TestTrue(TEXT("Centred, the content shrinks to what it wants"),
		FMath::IsNearlyEqual(Content->GetWidth(), 100.0f, 0.01f)
		&& FMath::IsNearlyEqual(Content->GetHeight(), 50.0f, 0.01f));
	// Horizontally centred in 300 with a 100-wide child: equal gaps, so the child's centre is the
	// border's centre and its anchored position is zero on that axis.
	TestTrue(TEXT("...centred horizontally"),
		FMath::IsNearlyEqual(Content->GetAnchoredPosition().X, 0.0, 0.01));
	// Top-aligned in 200 with a 50-tall child: 75 above centre.
	TestTrue(TEXT("...and pinned to the top vertically"),
		FMath::IsNearlyEqual(Content->GetAnchoredPosition().Y, 75.0, 0.01));

	BorderWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBorderPaddingAndDesiredSizeScaleTest,
	"DreamGUI.Border.PaddingInsetsTheContentAndDesiredSizeScaleOnlyChangesWhatTheBorderReports",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBorderPaddingAndDesiredSizeScaleTest::RunTest(const FString& Parameters)
{
	using namespace DreamBorderMenuAnchorTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* BorderWidget = MakeWidget(TestWorld.World, nullptr, TEXT("Border"), 300.0f, 200.0f);
	MakeWidget(TestWorld.World, BorderWidget, TEXT("Content"), 100.0f, 50.0f);
	UDreamLayoutContainerBorder* Border = BorderWidget->CreateNewLayoutContainer<UDreamLayoutContainerBorder>();
	if (!TestNotNull(TEXT("Border created"), Border))
	{
		return false;
	}
	Border->SetPadding(FMargin(10.0f, 4.0f, 10.0f, 4.0f));

	// Content 100x50 plus 20 and 8 of padding.
	const FVector2f Natural = Border->GetLayoutPreferredSize();
	TestTrue(TEXT("The border reports its content plus its padding"),
		FMath::IsNearlyEqual(Natural.X, 120.0f, 0.01f) && FMath::IsNearlyEqual(Natural.Y, 58.0f, 0.01f));

	// UBorder::DesiredSizeScale is a statement to the PARENT about how much room to hand over. It does
	// not scale anything the border draws or arranges, which is what separates it from a scale box.
	Border->SetDesiredSizeScale(FVector2D(2.0, 0.5));
	const FVector2f Scaled = Border->GetLayoutPreferredSize();
	TestTrue(TEXT("DesiredSizeScale scales the reported size"),
		FMath::IsNearlyEqual(Scaled.X, 240.0f, 0.01f) && FMath::IsNearlyEqual(Scaled.Y, 29.0f, 0.01f));

	BorderWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBorderBrushColorReachesTheVisualTest,
	"DreamGUI.Border.SettingTheBrushColourColoursTheWidgetVisualThatDrawsTheBackground",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBorderBrushColorReachesTheVisualTest::RunTest(const FString& Parameters)
{
	using namespace DreamBorderMenuAnchorTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* BorderWidget = MakeWidget(TestWorld.World, nullptr, TEXT("Border"), 300.0f, 200.0f);
	UDreamRectBlock* Background = BorderWidget->CreateNewVisual<UDreamRectBlock>();
	if (!TestNotNull(TEXT("Background visual created"), Background))
	{
		return false;
	}
	UDreamLayoutContainerBorder* Border = BorderWidget->CreateNewLayoutContainer<UDreamLayoutContainerBorder>();
	if (!TestNotNull(TEXT("Border created"), Border))
	{
		return false;
	}

	// There is no second brush: a DreamGUI widget's art is its visual, and BrushColor tints that visual so
	// UBorder::SetBrushColor has a counterpart instead of a missing feature. It tints, as SBorder multiplies its brush
	// by BorderBackgroundColor: the colour the visual was authored with is kept, and what is drawn is the product.
	Border->SetBrushColor(FLinearColor(1.0f, 0.0f, 0.0f, 1.0f));
	const FColor Applied = Background->GetFinalColor();
	TestEqual(TEXT("Red reaches the visual"), static_cast<int32>(Applied.R), 255);
	TestEqual(TEXT("...and nothing else does"), static_cast<int32>(Applied.G), 0);
	TestEqual(TEXT("...on either channel"), static_cast<int32>(Applied.B), 0);
	TestTrue(TEXT("The visual's own colour is left as it was authored"), Background->GetColor() == FColor::White);

	// Multiplied in linear space, so a mid-grey visual under a red brush colour draws the same mid red.
	Background->SetColor(FColor(128, 128, 128, 255));
	const FColor OverGrey = Background->GetFinalColor();
	TestTrue(TEXT("A grey visual is drawn red at the grey's level"), FMath::Abs(static_cast<int32>(OverGrey.R) - 128) <= 1);
	TestEqual(TEXT("...with the other channels taken out"), static_cast<int32>(OverGrey.G), 0);

	BorderWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBorderContentColourTest,
	"DreamGUI.Border.ContentColourTintsTheContentAndNotTheBackground",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * UBorder's ContentColorAndOpacity is SCompoundWidget's ColorAndOpacity: blended into what the border's children
 * paint with and never into the border's own brush. The panel had no such property. It now tints every visual below
 * the border -- the content's and what the content holds -- multiplied in linear space, and leaves the background
 * alone.
 */
bool FDreamBorderContentColourTest::RunTest(const FString& Parameters)
{
	using namespace DreamBorderMenuAnchorTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* BorderWidget = MakeWidget(TestWorld.World, nullptr, TEXT("Border"), 300.0f, 200.0f);
	UDreamWidget* Content = MakeWidget(TestWorld.World, BorderWidget, TEXT("Content"), 100.0f, 50.0f);
	UDreamWidget* Inner = MakeWidget(TestWorld.World, Content, TEXT("Inner"), 20.0f, 20.0f);
	UDreamRectBlock* Background = BorderWidget->CreateNewVisual<UDreamRectBlock>();
	UDreamRectBlock* ContentArt = Content->CreateNewVisual<UDreamRectBlock>();
	UDreamRectBlock* InnerArt = Inner->CreateNewVisual<UDreamRectBlock>();
	UDreamLayoutContainerBorder* Border = BorderWidget->CreateNewLayoutContainer<UDreamLayoutContainerBorder>();
	if (!TestNotNull(TEXT("Background visual created"), Background)
		|| !TestNotNull(TEXT("Content visual created"), ContentArt)
		|| !TestNotNull(TEXT("Inner visual created"), InnerArt)
		|| !TestNotNull(TEXT("Border created"), Border))
	{
		BorderWidget->DestroyWidget();
		return false;
	}
	TestTrue(TEXT("White by default, which tints nothing"), Border->ContentColorAndOpacity == FLinearColor::White
		&& ContentArt->GetFinalColor() == FColor::White);

	Border->SetContentColorAndOpacity(FLinearColor(1.0f, 0.0f, 0.0f, 0.5f));
	const FColor ContentColour = ContentArt->GetFinalColor();
	TestEqual(TEXT("The content is drawn red"), static_cast<int32>(ContentColour.R), 255);
	TestTrue(TEXT("...with green and blue taken out"), ContentColour.G == 0 && ContentColour.B == 0);
	TestTrue(TEXT("...at half its opacity"), FMath::Abs(static_cast<int32>(ContentColour.A) - 128) <= 1);
	TestTrue(TEXT("What the content holds is tinted the same way"), InnerArt->GetFinalColor() == ContentColour);
	TestTrue(TEXT("The background is not tinted"), Background->GetFinalColor() == FColor::White);
	TestTrue(TEXT("...and no visual's own colour was written"), ContentArt->GetColor() == FColor::White);

	Border->SetContentColorAndOpacity(FLinearColor::White);
	TestTrue(TEXT("Set back to white, the content draws as authored"), ContentArt->GetFinalColor() == FColor::White);

	BorderWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBorderDisabledEffectTest,
	"DreamGUI.Border.ADisabledBorderDrawsItsBackgroundAtFortyFivePercentAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * UBorder's bShowEffectWhenDisabled: a disabled SBorder draws its brush with the disabled effect, which Slate's default
 * shader applies as the alpha times 0.45. The panel drew a disabled border as an enabled one. It now takes the
 * background's alpha to 45 percent while the widget is disabled -- by its own switch or an ancestor's -- and leaves
 * the content alone, as SBorder's effect reaches its brush only. The look follows the switch the moment it flips,
 * with no layout pass in between.
 */
bool FDreamBorderDisabledEffectTest::RunTest(const FString& Parameters)
{
	using namespace DreamBorderMenuAnchorTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 300.0f);
	UDreamWidget* BorderWidget = MakeWidget(TestWorld.World, Root, TEXT("Border"), 300.0f, 200.0f);
	UDreamWidget* Content = MakeWidget(TestWorld.World, BorderWidget, TEXT("Content"), 100.0f, 50.0f);
	UDreamRectBlock* Background = BorderWidget->CreateNewVisual<UDreamRectBlock>();
	UDreamRectBlock* ContentArt = Content->CreateNewVisual<UDreamRectBlock>();
	UDreamLayoutContainerBorder* Border = BorderWidget->CreateNewLayoutContainer<UDreamLayoutContainerBorder>();
	if (!TestNotNull(TEXT("Background visual created"), Background)
		|| !TestNotNull(TEXT("Content visual created"), ContentArt)
		|| !TestNotNull(TEXT("Border created"), Border))
	{
		Root->DestroyWidget();
		return false;
	}
	auto IsAtDisabledAlpha = [](const UDreamVisual* InVisual)
	{
		// 0.45 of 255 is 114.75; one either way covers the rounding of the encode.
		return FMath::Abs(static_cast<int32>(InVisual->GetFinalColor().A) - 115) <= 1;
	};
	TestTrue(TEXT("The effect is on by default, as UMG's is"), Border->bShowEffectWhenDisabled);
	TestEqual(TEXT("Enabled, the background draws at full alpha"), static_cast<int32>(Background->GetFinalColor().A), 255);

	BorderWidget->SetIsEnabled(false);
	TestTrue(TEXT("Disabled, the background draws at 45 percent"), IsAtDisabledAlpha(Background));
	TestEqual(TEXT("...in its own colour"), static_cast<int32>(Background->GetFinalColor().R), 255);
	TestEqual(TEXT("...and the content at full alpha"), static_cast<int32>(ContentArt->GetFinalColor().A), 255);

	BorderWidget->SetIsEnabled(true);
	TestEqual(TEXT("Enabled again, the background is back at full alpha"), static_cast<int32>(Background->GetFinalColor().A), 255);

	Root->SetIsEnabled(false);
	TestTrue(TEXT("A disabled ancestor disables the look as well"), IsAtDisabledAlpha(Background));
	Border->SetShowEffectWhenDisabled(false);
	TestEqual(TEXT("With the effect off a disabled border draws as an enabled one"), static_cast<int32>(Background->GetFinalColor().A), 255);
	Border->SetShowEffectWhenDisabled(true);
	TestTrue(TEXT("...and with it back on, at 45 percent again"), IsAtDisabledAlpha(Background));
	Root->SetIsEnabled(true);
	TestEqual(TEXT("Enabling the ancestor gives the full alpha back"), static_cast<int32>(Background->GetFinalColor().A), 255);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBorderFlipForRightToLeftTest,
	"DreamGUI.Border.FlippingForRightToLeftMirrorsTheBackgroundButNotTheContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * UBorder's bFlipForRightToLeftFlowDirection: under a right-to-left flow SBorder draws its brush through a scale of
 * (-1, 1) about the brush's centre, so the background is mirrored and nothing it holds is. The panel had no flip. It
 * now puts a mirror modifier on its own widget -- the one whose visual is the background -- switched on while the flag
 * is on and the flow resolves right to left, and on nothing below it. The reflection itself is checked on a quad built
 * by hand: across the centre line, with every triangle rewound so it still faces the way it was built to.
 */
bool FDreamBorderFlipForRightToLeftTest::RunTest(const FString& Parameters)
{
	using namespace DreamBorderMenuAnchorTestLocal;
	{
		// A quad across [-150, -50], reflected about a centre line at 0, spans [50, 150].
		FDreamUIGeometry Quad;
		Quad.OriginVertices.Add(FDreamUIOriginVertexData(FVector3f(0.0f, -150.0f, -20.0f)));
		Quad.OriginVertices.Add(FDreamUIOriginVertexData(FVector3f(0.0f, -50.0f, -20.0f)));
		Quad.OriginVertices.Add(FDreamUIOriginVertexData(FVector3f(0.0f, -150.0f, 20.0f)));
		Quad.OriginVertices.Add(FDreamUIOriginVertexData(FVector3f(0.0f, -50.0f, 20.0f)));
		Quad.Triangles = { 0, 3, 2, 0, 1, 3 };
		UDreamMeshModifierMirror::MirrorGeometry(Quad, FVector2f::ZeroVector, true, false);
		TestEqual(TEXT("The left edge lands where the right edge's mirror image is"), Quad.OriginVertices[0].Position.Y, 150.0f);
		TestEqual(TEXT("...and the right edge where the left edge's is"), Quad.OriginVertices[1].Position.Y, 50.0f);
		TestEqual(TEXT("Up and down are left alone"), Quad.OriginVertices[2].Position.Z, 20.0f);
		TestEqual(TEXT("The texture runs the other way along the reflected axis"), Quad.OriginVertices[0].Tangent.Y, -1.0f);
		TestTrue(TEXT("Every triangle is rewound, so it still faces the way it was built to"),
			Quad.Triangles == TArray<FDreamUIMeshIndex>({ 0, 2, 3, 0, 3, 1 }));
		// Through both axes about another centre: a half turn, which turns no triangle over.
		UDreamMeshModifierMirror::MirrorGeometry(Quad, FVector2f(100.0f, 0.0f), true, true);
		TestEqual(TEXT("About a line at 100, a corner at 150 lands at 50"), Quad.OriginVertices[0].Position.Y, 50.0f);
		TestEqual(TEXT("...and reflected through the other axis as well, the bottom edge becomes the top"),
			Quad.OriginVertices[0].Position.Z, 20.0f);
		TestTrue(TEXT("A half turn leaves the winding as it was"), Quad.Triangles == TArray<FDreamUIMeshIndex>({ 0, 2, 3, 0, 3, 1 }));
	}

	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* BorderWidget = MakeWidget(TestWorld.World, nullptr, TEXT("Border"), 300.0f, 200.0f);
	UDreamWidget* Content = MakeWidget(TestWorld.World, BorderWidget, TEXT("Content"), 100.0f, 50.0f);
	UDreamRectBlock* Background = BorderWidget->CreateNewVisual<UDreamRectBlock>();
	UDreamLayoutContainerBorder* Border = BorderWidget->CreateNewLayoutContainer<UDreamLayoutContainerBorder>();
	if (!TestNotNull(TEXT("Background visual created"), Background) || !TestNotNull(TEXT("Border created"), Border))
	{
		BorderWidget->DestroyWidget();
		return false;
	}
	auto Relayout = [Manager, BorderWidget]()
	{
		UDreamWidget::MarkLayoutForRebuild(BorderWidget);
		Manager->TickDreamUI(0.016f);
	};
	TestNull(TEXT("No mirror until the flag asks for one"), BorderWidget->GetComponent<UDreamMeshModifierMirror>());

	Border->SetFlipForRightToLeftFlowDirection(true);
	UDreamMeshModifierMirror* Mirror = BorderWidget->GetComponent<UDreamMeshModifierMirror>();
	if (!TestNotNull(TEXT("The flag puts a mirror on the border's own widget"), Mirror))
	{
		BorderWidget->DestroyWidget();
		return false;
	}
	TestFalse(TEXT("...switched off while the flow runs left to right"), Mirror->GetEnable());

	BorderWidget->SetFlowDirectionPreference(EDreamFlowDirectionPreference::RightToLeft);
	Relayout();
	TestTrue(TEXT("Right to left, the background is mirrored"), Mirror->GetEnable());
	TestTrue(TEXT("...left to right only"), Mirror->GetMirrorHorizontally() && !Mirror->GetMirrorVertically());
	TestNull(TEXT("...and nothing the border holds is"), Content->GetComponent<UDreamMeshModifierMirror>());

	Border->SetFlipForRightToLeftFlowDirection(false);
	TestFalse(TEXT("With the flag off nothing is mirrored, whatever the flow"), Mirror->GetEnable());
	Border->SetFlipForRightToLeftFlowDirection(true);
	TestTrue(TEXT("Back on, the same mirror flips the background again"), Mirror->GetEnable()
		&& BorderWidget->GetComponent<UDreamMeshModifierMirror>() == Mirror);

	BorderWidget->SetFlowDirectionPreference(EDreamFlowDirectionPreference::LeftToRight);
	Relayout();
	TestFalse(TEXT("Left to right again, the background is drawn as authored"), Mirror->GetEnable());

	BorderWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorPlacementArithmeticTest,
	"DreamGUI.MenuAnchor.EveryPlacementPutsTheMenuWhereUMGWouldPutIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMenuAnchorPlacementArithmeticTest::RunTest(const FString& Parameters)
{
	using Anchor = UDreamLayoutContainerMenuAnchor;
	const FVector2D AnchorPos(100.0, 50.0);
	const FVector2D AnchorSize(80.0, 20.0);
	const FVector2D MenuSize(120.0, 60.0);

	TestTrue(TEXT("BelowAnchor hangs under the left edge"),
		Anchor::CalculateMenuPosition(EDreamMenuPlacement::BelowAnchor, AnchorPos, AnchorSize, MenuSize)
			.Equals(FVector2D(100.0, 70.0), 0.01));
	TestTrue(TEXT("CenteredBelowAnchor centres on the anchor"),
		Anchor::CalculateMenuPosition(EDreamMenuPlacement::CenteredBelowAnchor, AnchorPos, AnchorSize, MenuSize)
			.Equals(FVector2D(80.0, 70.0), 0.01));
	TestTrue(TEXT("BelowRightAnchor lines the right edges up"),
		Anchor::CalculateMenuPosition(EDreamMenuPlacement::BelowRightAnchor, AnchorPos, AnchorSize, MenuSize)
			.Equals(FVector2D(60.0, 70.0), 0.01));
	TestTrue(TEXT("AboveAnchor sits on top of it"),
		Anchor::CalculateMenuPosition(EDreamMenuPlacement::AboveAnchor, AnchorPos, AnchorSize, MenuSize)
			.Equals(FVector2D(100.0, -10.0), 0.01));
	TestTrue(TEXT("MenuRight starts where the anchor ends"),
		Anchor::CalculateMenuPosition(EDreamMenuPlacement::MenuRight, AnchorPos, AnchorSize, MenuSize)
			.Equals(FVector2D(180.0, 50.0), 0.01));
	TestTrue(TEXT("MenuLeft ends where the anchor starts"),
		Anchor::CalculateMenuPosition(EDreamMenuPlacement::MenuLeft, AnchorPos, AnchorSize, MenuSize)
			.Equals(FVector2D(-20.0, 50.0), 0.01));
	TestTrue(TEXT("Center covers the anchor on both axes"),
		Anchor::CalculateMenuPosition(EDreamMenuPlacement::Center, AnchorPos, AnchorSize, MenuSize)
			.Equals(FVector2D(80.0, 30.0), 0.01));

	TestTrue(TEXT("Only the combo-box placements take the anchor's width"),
		Anchor::PlacementMatchesAnchorWidth(EDreamMenuPlacement::ComboBox)
		&& Anchor::PlacementMatchesAnchorWidth(EDreamMenuPlacement::ComboBoxRight)
		&& !Anchor::PlacementMatchesAnchorWidth(EDreamMenuPlacement::BelowAnchor));

	// bFitInWindow shifts and never resizes, and a menu larger than the window keeps its top-left edge:
	// a menu pushed off the top has no way back, one overhanging the bottom can still be scrolled to.
	TestTrue(TEXT("A menu overhanging the right is pulled back in"),
		Anchor::FitMenuInWindow(FVector2D(950.0, 10.0), FVector2D(120.0, 60.0), FVector2D(1000.0, 600.0))
			.Equals(FVector2D(880.0, 10.0), 0.01));
	TestTrue(TEXT("A menu above the top is pushed down to it"),
		Anchor::FitMenuInWindow(FVector2D(10.0, -40.0), FVector2D(120.0, 60.0), FVector2D(1000.0, 600.0))
			.Equals(FVector2D(10.0, 0.0), 0.01));
	TestTrue(TEXT("A menu taller than the window keeps its top edge"),
		Anchor::FitMenuInWindow(FVector2D(10.0, 100.0), FVector2D(120.0, 900.0), FVector2D(1000.0, 600.0))
			.Equals(FVector2D(10.0, 0.0), 0.01));

	// The same fit, asked from the panel's own space, which is where the arrange pass stands. An 80-wide
	// anchor sitting 900 into a 1000-wide window, with a 200-wide menu hung below its left edge.
	const FVector2D Window(1000.0, 600.0);
	const FVector2D WideMenu(200.0, 60.0);
	TestTrue(TEXT("Left-to-right, the overhang is taken back out of the panel-space position"),
		Anchor::FitMenuInWindowFromPanelSpace(FVector2D(0.0, 30.0), WideMenu, FVector2D(900.0, 0.0), 80.0f, Window, false)
			.Equals(FVector2D(-100.0, 30.0), 0.01));
	// Mirrored, that same menu is committed reflected: it hangs off the anchor's RIGHT edge leftwards,
	// 780..980 in the window, which already fits -- so the answer is the position it came in with. A fit
	// that ignored the reflection would have shifted it by the hundred it does not need.
	TestTrue(TEXT("Right-to-left, a menu whose mirrored rect already fits is left alone"),
		Anchor::FitMenuInWindowFromPanelSpace(FVector2D(0.0, 30.0), WideMenu, FVector2D(900.0, 0.0), 80.0f, Window, true)
			.Equals(FVector2D(0.0, 30.0), 0.01));
	// And near the LEFT edge it is the mirrored rect that overhangs (-70..130), so that is what gets
	// pulled in: committing -70 reflects to -50 in the panel, which is the window's own left edge.
	TestTrue(TEXT("Right-to-left, it is the mirrored rect that is kept inside the window"),
		Anchor::FitMenuInWindowFromPanelSpace(FVector2D(0.0, 30.0), WideMenu, FVector2D(50.0, 0.0), 80.0f, Window, true)
			.Equals(FVector2D(-70.0, 30.0), 0.01));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorPlacesAndCollapsesItsMenuTest,
	"DreamGUI.MenuAnchor.AClosedMenuIsCollapsedAndAnOpenComboBoxMenuTakesTheAnchorWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMenuAnchorPlacesAndCollapsesItsMenuTest::RunTest(const FString& Parameters)
{
	using namespace DreamBorderMenuAnchorTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 1000.0f, 600.0f);
	UDreamWidget* AnchorWidget = MakeWidget(TestWorld.World, Root, TEXT("Anchor"), 200.0f, 40.0f);
	UDreamWidget* Face = MakeWidget(TestWorld.World, AnchorWidget, TEXT("Face"), 200.0f, 40.0f);
	UDreamWidget* Menu = MakeWidget(TestWorld.World, AnchorWidget, TEXT("Menu"), 90.0f, 150.0f);
	UDreamLayoutContainerMenuAnchor* MenuAnchor = AnchorWidget->CreateNewLayoutContainer<UDreamLayoutContainerMenuAnchor>();
	if (!TestNotNull(TEXT("Menu anchor created"), MenuAnchor))
	{
		return false;
	}

	TestTrue(TEXT("The first child is the anchor"), MenuAnchor->GetAnchorContent() == Face);
	TestTrue(TEXT("The second is the menu"), MenuAnchor->GetMenuContent() == Menu);

	UDreamWidget::MarkLayoutForRebuild(AnchorWidget);
	Manager->TickDreamUI(0.016f);

	// Closed: the menu is collapsed, so it costs no layout and takes no hit test -- which is what
	// "the menu is not open" has to mean for a panel that keeps the menu in its own hierarchy.
	TestFalse(TEXT("A closed menu is collapsed"), Menu->GetLayoutVisibleInHierarchy());
	TestTrue(TEXT("The anchor content still fills the anchor"),
		FMath::IsNearlyEqual(Face->GetWidth(), 200.0f, 0.01f));
	// And the anchor's own measured size is the anchor content's, never the menu's.
	TestTrue(TEXT("The menu does not grow the button it hangs off"),
		FMath::IsNearlyEqual(MenuAnchor->GetLayoutPreferredSize().Y, 40.0f, 0.01f));

	MenuAnchor->SetIsOpen(true);
	Manager->TickDreamUI(0.016f);

	TestTrue(TEXT("An open menu is laid out"), Menu->GetLayoutVisibleInHierarchy());
	// ComboBox is the default placement: below the anchor, forced to the anchor's width, so the
	// 90-wide menu comes out 200 wide.
	TestTrue(TEXT("A combo-box menu takes the anchor's width"),
		FMath::IsNearlyEqual(Menu->GetWidth(), 200.0f, 0.01f));
	TestTrue(TEXT("...and keeps its own height"),
		FMath::IsNearlyEqual(Menu->GetHeight(), 150.0f, 0.01f));

	// MenuLeft is the placement that moves it furthest, and proves the placement is actually consulted.
	const FVector2D ComboPosition = Menu->GetAnchoredPosition();
	MenuAnchor->SetPlacement(EDreamMenuPlacement::MenuRight);
	Manager->TickDreamUI(0.016f);
	TestFalse(TEXT("Changing the placement moves the menu"),
		Menu->GetAnchoredPosition().Equals(ComboPosition, 0.01));

	MenuAnchor->SetIsOpen(false);
	Manager->TickDreamUI(0.016f);
	TestFalse(TEXT("Closing it collapses it again"), Menu->GetLayoutVisibleInHierarchy());

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorBuildsItsMenuFromItsClassTest,
	"DreamGUI.MenuAnchor.AMenuBuiltFromItsClassIsMadeOnOpenAndReleasedOnClose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * UMenuAnchor's MenuClass: with no menu authored under it, the anchor makes one from the class every time it opens and
 * lets it go when it closes, so a menu showing live data is built fresh each time. The panel could only show a menu
 * authored as its second child. A menu authored there still wins and is only hidden on close, as it always was. The
 * application menu stack stays off unless asked for, so a menu still opens in place by default.
 */
bool FDreamMenuAnchorBuildsItsMenuFromItsClassTest::RunTest(const FString& Parameters)
{
	using namespace DreamBorderMenuAnchorTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 1000.0f, 600.0f);
	UDreamWidget* AnchorWidget = MakeWidget(TestWorld.World, Root, TEXT("Anchor"), 200.0f, 40.0f);
	UDreamWidget* Face = MakeWidget(TestWorld.World, AnchorWidget, TEXT("Face"), 200.0f, 40.0f);
	UDreamLayoutContainerMenuAnchor* MenuAnchor = AnchorWidget->CreateNewLayoutContainer<UDreamLayoutContainerMenuAnchor>();
	if (!TestNotNull(TEXT("Menu anchor created"), MenuAnchor))
	{
		Root->DestroyWidget();
		return false;
	}
	TestFalse(TEXT("The application menu stack is off by default"), MenuAnchor->bUseApplicationMenuStack);
	MenuAnchor->SetMenuClass(UDreamUserWidget::StaticClass());
	TestNull(TEXT("Nothing is made before the menu opens"), MenuAnchor->GetMenuContent());

	MenuAnchor->SetIsOpen(true);
	UDreamWidget* FirstMenu = MenuAnchor->GetMenuContent();
	if (!TestNotNull(TEXT("Opening makes the menu from its class"), FirstMenu))
	{
		Root->DestroyWidget();
		return false;
	}
	TestTrue(TEXT("...an instance of that class"), FirstMenu->IsA<UDreamUserWidget>());
	TestTrue(TEXT("...held by the anchor as its menu"), FirstMenu->GetParent() == AnchorWidget);
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("...and laid out as an open menu is"), FirstMenu->GetLayoutVisibleInHierarchy());

	const TWeakObjectPtr<UDreamWidget> FirstMenuWeak(FirstMenu);
	MenuAnchor->SetIsOpen(false);
	TestNull(TEXT("Closing lets the menu go"), MenuAnchor->GetMenuContent());
	TestFalse(TEXT("...destroyed rather than kept hidden"), FirstMenuWeak.IsValid());
	TestTrue(TEXT("The anchor content is still there"), IsValid(Face) && MenuAnchor->GetAnchorContent() == Face);

	MenuAnchor->SetIsOpen(true);
	UDreamWidget* SecondMenu = MenuAnchor->GetMenuContent();
	TestTrue(TEXT("Opening again makes a new one"), IsValid(SecondMenu) && SecondMenu != FirstMenu);
	MenuAnchor->SetIsOpen(false);

	// An authored menu is the anchor's own: shown in place of a built one, and only hidden on close.
	UDreamWidget* Authored = MakeWidget(TestWorld.World, AnchorWidget, TEXT("Authored"), 90.0f, 150.0f);
	MenuAnchor->SetIsOpen(true);
	TestTrue(TEXT("A menu authored under the anchor wins over its class"), MenuAnchor->GetMenuContent() == Authored);
	MenuAnchor->SetIsOpen(false);
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("...and closing hides it without destroying it"), IsValid(Authored) && !Authored->GetLayoutVisibleInHierarchy());

	Root->DestroyWidget();
	return true;
}

#endif
