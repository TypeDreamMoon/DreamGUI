// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamRingMenu.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamRingSectorRaycast.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"
#include "Tests/DreamControlTestScope.h"
#include "Tests/DreamRingMenuTestTypes.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The ring menu, aimed at the four things that fail SILENTLY in a control assembled out of maths.
 *
 * A slice whose start is right and whose sweep is wrong is a wheel that looks fine until somebody
 * changes a weight. A wedge drawn in one convention and hit-tested in another looks perfect and
 * picks the wrong item -- and there ARE two conventions here, the shader's clockwise-from-three and
 * the family's clockwise-from-twelve, which is exactly why the quarter turn gets a test of its own.
 * A hit sector that copied the DRAWN sweep would blink the highlight off in every gap. And a pool
 * that rebuilt itself on every style edit would be correct and unusable in the designer.
 *
 * Everything runs headless: no world, no registration, no layout pass. So the assertions read data
 * the control OWNS -- the rect's block parameters, the raycast object's radii, the selectable's
 * resting colour -- and never a colour a transition would have had to deliver (the tween manager
 * returns null without a world) or a size a layout pass would have had to resolve.
 */
namespace DreamRingMenuTestLocal
{
	static FDreamRingMenuItem MakeItem(const TCHAR* InLabel, float InWeight = 1.0f, bool bInEnabled = true)
	{
		FDreamRingMenuItem Item;
		Item.Label = FText::AsCultureInvariant(InLabel);
		Item.Tag = FName(InLabel);
		Item.Weight = InWeight;
		Item.bEnabled = bInEnabled;
		return Item;
	}

	/** A four-item wheel, authored before initialization the way a host class would leave it. */
	static UDreamRingMenu* MakeWheel(const TArray<FDreamRingMenuItem>& InItems)
	{
		UDreamRingMenu* Menu = NewObject<UDreamRingMenu>(GetTransientPackage());
		Menu->StyleSource = EDreamUIStyleSource::Inline;
		Menu->Items = InItems;
		Menu->Initialize();
		return Menu;
	}

	static UDreamRectBlock* WedgeRect(const UDreamRingMenu* InMenu, int32 InIndex)
	{
		UDreamWidget* Wedge = InMenu->GetWedgeWidget(InIndex);
		return Wedge != nullptr ? Cast<UDreamRectBlock>(Wedge->GetVisual()) : nullptr;
	}

	static UDreamRingSectorRaycast* WedgeSector(const UDreamRingMenu* InMenu, int32 InIndex)
	{
		UDreamWidget* Wedge = InMenu->GetWedgeWidget(InIndex);
		UDreamVisual* Visual = Wedge != nullptr ? Wedge->GetVisual() : nullptr;
		return Visual != nullptr ? Cast<UDreamRingSectorRaycast>(Visual->GetCustomRaycastObject()) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuSliceTest,
	"DreamGUI.Controls.RingMenu.WeightsDivideTheSweepAndTheGapIsDrawnOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRingMenuSliceTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuTestLocal;

	TDreamTestControl<UDreamRingMenu> Menu(MakeWheel({
		MakeItem(TEXT("A")), MakeItem(TEXT("B")), MakeItem(TEXT("C"), 2.0f), MakeItem(TEXT("D")) }));
	Menu->Style.StartAngle = 0.0f;
	Menu->Style.SweepAngle = 360.0f;
	Menu->Style.ItemGapAngle = 4.0f;
	Menu->ApplyStyle();

	if (!TestNotNull(TEXT("one wedge per item"), Menu->GetWedgeWidget(3)))
	{
		return false;
	}
	// Five weights' worth of ring: 1 + 1 + 2 + 1, so a unit slice is 72 degrees.
	TestEqual(TEXT("the first slice starts at the style's start angle"), Menu->GetItemStartAngle(0), 0.0f);
	TestEqual(TEXT("a unit item takes its share"), Menu->GetItemSweepAngle(0), 72.0f);
	TestEqual(TEXT("a double-weighted item takes twice"), Menu->GetItemSweepAngle(2), 144.0f);
	TestEqual(TEXT("slices follow one another"), Menu->GetItemStartAngle(2), 144.0f);
	TestEqual(TEXT("and the last one closes the ring"), Menu->GetItemStartAngle(3), 288.0f);
	TestEqual(TEXT("the mid angle is the slice's middle"), Menu->GetItemMidAngle(2), 216.0f);

	UDreamRectBlock* Rect = WedgeRect(Menu.Get(), 2);
	UDreamRingSectorRaycast* Sector = WedgeSector(Menu.Get(), 2);
	if (!TestNotNull(TEXT("the wedge is a rect block"), Rect) ||
		!TestNotNull(TEXT("the wedge hit-tests as a sector"), Sector))
	{
		return false;
	}
	// THE QUARTER TURN. The shader measures its wedge clockwise from THREE o'clock (UV Y runs down)
	// and this family speaks clockwise from twelve; the two differ by exactly 90 and nothing else.
	// Half the gap comes off the front of the drawn wedge.
	TestEqual(TEXT("the drawn wedge starts at the slice plus half the gap, less the quarter turn"),
		Rect->GetRadialFillRotation(), 144.0f + 2.0f - 90.0f);
	TestEqual(TEXT("and it is the slice less the whole gap"),
		Rect->GetRadialFillAngle(), 144.0f - 4.0f);

	// The hit sector keeps the FULL slice. Were the gap in it too, dragging across one would exit a
	// wedge and enter nothing, and the highlight would blink off between every pair of items.
	TestEqual(TEXT("the hit sector keeps the whole slice -- start"), Sector->StartAngle, 144.0f);
	TestEqual(TEXT("the hit sector keeps the whole slice -- sweep"), Sector->SweepAngle, 144.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuRingShapeTest,
	"DreamGUI.Controls.RingMenu.TheRingIsTheBorderAndTheFullSweepDropsTheMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRingMenuRingShapeTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuTestLocal;

	TDreamTestControl<UDreamRingMenu> Menu(MakeWheel({ MakeItem(TEXT("Only")) }));
	Menu->Style.OuterRadius = 200.0f;
	Menu->Style.InnerRadius = 80.0f;
	Menu->Style.ItemGapAngle = 0.0f;
	Menu->Style.HighlightGrowth = 0.0f;
	Menu->ApplyStyle();

	UDreamRectBlock* Rect = WedgeRect(Menu.Get(), 0);
	if (!TestNotNull(TEXT("the wedge is a rect block"), Rect))
	{
		return false;
	}
	// A procedural rect has exactly one hole in it -- the space a border does not cover -- so a ring
	// is the border ALONE with the body switched off. A face BRUSH therefore has nothing to draw on,
	// which is the same honest cost UDreamProgressBar's radial shape pays.
	TestFalse(TEXT("the body is off, because the border IS the ring"), Rect->GetEnableBody());
	TestTrue(TEXT("the border is on"), Rect->GetEnableBorder());
	TestEqual(TEXT("a percentage corner radius of one keeps the silhouette round"),
		Rect->GetCornerRadius().X, 1.0f);
	// Percentage border width resolves to Percentage * min(w,h) * 0.5, which against a square of
	// side 2R is Percentage * R -- so the band from 80 to 200 is (200 - 80) / 200.
	TestEqual(TEXT("the band is stated as a fraction of the outer radius"), Rect->GetBorderWidth(), 0.6f);
	TestEqual(TEXT("the wedge is a square of twice the outer radius"),
		static_cast<float>(Menu->GetWedgeWidget(0)->GetWidth()), 400.0f);

	// 360 is the shader's "no mask at all" (its gate is sign(max(0, 360 - angle))), so a lone item
	// on a full wheel is an unbroken ring rather than a ring with a hairline seam in it.
	TestFalse(TEXT("a full sweep switches the mask off entirely"), Rect->GetEnableRadialFill());

	// Half a wheel is a mask again, and it is the only thing that changed.
	Menu->Style.SweepAngle = 180.0f;
	Menu->ApplyStyle();
	TestTrue(TEXT("a partial sweep turns the mask back on"), Rect->GetEnableRadialFill());
	TestEqual(TEXT("and states the half"), Rect->GetRadialFillAngle(), 180.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingSectorRaycastTest,
	"DreamGUI.Controls.RingMenu.TheSectorHitsItsOwnAnnulusAndNothingElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRingSectorRaycastTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuTestLocal;

	// The angle convention, on its own, because everything else in the control is stated in it.
	TestEqual(TEXT("straight up is zero"), UDreamRingSectorRaycast::AngleOfLocalPoint(FVector2D(0.0, 10.0)), 0.0f);
	TestEqual(TEXT("right is a quarter turn clockwise"),
		UDreamRingSectorRaycast::AngleOfLocalPoint(FVector2D(10.0, 0.0)), 90.0f);
	TestEqual(TEXT("down is half"), UDreamRingSectorRaycast::AngleOfLocalPoint(FVector2D(0.0, -10.0)), 180.0f);
	TestEqual(TEXT("left is three quarters, not minus a quarter"),
		UDreamRingSectorRaycast::AngleOfLocalPoint(FVector2D(-10.0, 0.0)), 270.0f);

	// The wrap case is the one a slice straddling twelve o'clock depends on, and the reason the test
	// is a subtraction modulo 360 rather than two comparisons.
	TestTrue(TEXT("a slice across twelve contains 355"), UDreamRingSectorRaycast::IsAngleInSweep(355.0f, 350.0f, 30.0f));
	TestTrue(TEXT("and contains 10"), UDreamRingSectorRaycast::IsAngleInSweep(10.0f, 350.0f, 30.0f));
	TestFalse(TEXT("and stops at its end"), UDreamRingSectorRaycast::IsAngleInSweep(20.0f, 350.0f, 30.0f));
	TestTrue(TEXT("a full sweep contains everything"), UDreamRingSectorRaycast::IsAngleInSweep(123.0f, 40.0f, 360.0f));

	// And the raycast itself, against a wedge the control built. Four items on a full wheel, so item
	// 0 owns 0-90: up is inside it, right is not.
	TDreamTestControl<UDreamRingMenu> Menu(MakeWheel({
		MakeItem(TEXT("A")), MakeItem(TEXT("B")), MakeItem(TEXT("C")), MakeItem(TEXT("D")) }));
	Menu->Style.OuterRadius = 200.0f;
	Menu->Style.InnerRadius = 80.0f;
	Menu->Style.HighlightGrowth = 0.0f;
	Menu->HitArea = EDreamRingHitArea::Ring;
	Menu->ApplyStyle();

	UDreamWidget* Wedge = Menu->GetWedgeWidget(0);
	UDreamVisual* Visual = Wedge != nullptr ? Wedge->GetVisual() : nullptr;
	UDreamRingSectorRaycast* Sector = WedgeSector(Menu.Get(), 0);
	if (!TestNotNull(TEXT("the wedge has a hit shape"), Sector) || !TestNotNull(TEXT("and a visual"), Visual))
	{
		return false;
	}

	// The ray straddles the widget's plane, which is what the pipeline guarantees before it calls in.
	// Local space is X depth, Y right, Z up -- so a point "up and inside the band" is (0, 0, 140).
	auto Trace = [Sector, Visual](double InRight, double InUp)
	{
		FVector Hit, Normal;
		return Sector->Raycast(Visual, FVector(-10.0, InRight, InUp), FVector(10.0, InRight, InUp), Hit, Normal);
	};
	TestTrue(TEXT("inside the band and inside the slice"), Trace(0.0, 140.0));
	TestFalse(TEXT("inside the band but in the NEXT slice"), Trace(140.0, 0.0));
	TestFalse(TEXT("inside the slice but in the hub"), Trace(0.0, 40.0));
	TestFalse(TEXT("inside the slice but past the outer edge"), Trace(0.0, 260.0));

	// Slice reaches PAST the drawn ring -- and it is BOUNDED by default, which is not a detail: an
	// unbounded wedge claims its direction across the whole screen, and the raycast sort (by
	// flattened hierarchy index, descending) then hands it every hit against anything declared
	// earlier. The measured symptom was a gallery in which nothing else could be clicked.
	Menu->HitArea = EDreamRingHitArea::Slice;
	Menu->ApplyStyle();
	TestEqual(TEXT("Slice reaches a stated multiple of the outer radius"), Sector->OuterRadius, 400.0f);
	TestTrue(TEXT("so it reaches past where the ring is drawn"), Trace(0.0, 300.0));
	TestFalse(TEXT("-- and stops, so a neighbour outside it is still clickable"), Trace(0.0, 900.0));
	TestFalse(TEXT("while the dead zone still holds"), Trace(0.0, 40.0));

	// Zero is the weapon wheel, and it takes asking for.
	Menu->SliceHitRadiusScale = 0.0f;
	Menu->ApplyStyle();
	TestEqual(TEXT("an explicit zero states no far edge at all"), Sector->OuterRadius, 0.0f);
	TestTrue(TEXT("and then the slice really does own its direction"), Trace(0.0, 5000.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuHighlightTest,
	"DreamGUI.Controls.RingMenu.TheHighlightGrowsTheHitShapeWithTheWedge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRingMenuHighlightTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuTestLocal;

	TDreamTestControl<UDreamRingMenu> Menu(MakeWheel({
		MakeItem(TEXT("A")), MakeItem(TEXT("B")), MakeItem(TEXT("C")), MakeItem(TEXT("D")) }));
	Menu->Style.OuterRadius = 200.0f;
	Menu->Style.InnerRadius = 80.0f;
	Menu->Style.HighlightGrowth = 20.0f;
	Menu->HitArea = EDreamRingHitArea::Ring;
	Menu->ApplyStyle();

	TStrongObjectPtr<UDreamRingMenuProbe> Probe(NewObject<UDreamRingMenuProbe>(GetTransientPackage()));
	Menu->OnHighlightChanged.AddDynamic(Probe.Get(), &UDreamRingMenuProbe::RecordIndex);

	Menu->SetHighlightedIndex(1);
	TestEqual(TEXT("the highlight moved"), Menu->GetHighlightedIndex(), 1);
	TestEqual(TEXT("and was announced"), Probe->LastIndex, 1);
	TestEqual(TEXT("the highlighted wedge reaches further out"),
		static_cast<float>(Menu->GetWedgeWidget(1)->GetWidth()), 440.0f);
	TestEqual(TEXT("its neighbour did not move"),
		static_cast<float>(Menu->GetWedgeWidget(0)->GetWidth()), 400.0f);

	// The inner edge must NOT move with the outer one, or the highlighted wedge would eat its way
	// into the hub: the band is stated as a fraction of the grown radius, (220 - 80) / 220.
	UDreamRectBlock* Rect = WedgeRect(Menu.Get(), 1);
	if (TestNotNull(TEXT("the wedge is a rect block"), Rect))
	{
		TestEqual(TEXT("the band is restated against the grown radius"),
			Rect->GetBorderWidth(), 140.0f / 220.0f);
	}
	// And the hit shape grows with the drawing. Otherwise the pointer that grew the wedge would find
	// itself outside the wedge's hit area, exit, shrink it, re-enter -- and oscillate for as long as
	// it sat on the old boundary.
	UDreamRingSectorRaycast* Sector = WedgeSector(Menu.Get(), 1);
	if (TestNotNull(TEXT("the wedge hit-tests as a sector"), Sector))
	{
		TestEqual(TEXT("the hit sector follows the drawn edge"), Sector->OuterRadius, 220.0f);
	}

	Menu->SetHighlightedIndex(INDEX_NONE);
	TestEqual(TEXT("clearing it shrinks the wedge back"),
		static_cast<float>(Menu->GetWedgeWidget(1)->GetWidth()), 400.0f);
	TestEqual(TEXT("and announces the clear"), Probe->LastIndex, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("moving to the same index twice announces once"), Probe->IndexCalls, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuSelectionTest,
	"DreamGUI.Controls.RingMenu.SelectionRidesTheRestingColourAndActivationAlwaysSpeaks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRingMenuSelectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuTestLocal;

	TDreamTestControl<UDreamRingMenu> Menu(MakeWheel({
		MakeItem(TEXT("Attack")), MakeItem(TEXT("Heal")), MakeItem(TEXT("Flee")) }));
	Menu->ApplyStyle();

	TStrongObjectPtr<UDreamRingMenuProbe> SelectionProbe(NewObject<UDreamRingMenuProbe>(GetTransientPackage()));
	TStrongObjectPtr<UDreamRingMenuProbe> ActivationProbe(NewObject<UDreamRingMenuProbe>(GetTransientPackage()));
	Menu->OnSelectionChanged.AddDynamic(SelectionProbe.Get(), &UDreamRingMenuProbe::RecordIndex);
	Menu->OnItemActivated.AddDynamic(ActivationProbe.Get(), &UDreamRingMenuProbe::RecordActivation);

	Menu->SetSelectedIndex(1);
	TestEqual(TEXT("the selection moved"), Menu->GetSelectedIndex(), 1);
	TestEqual(TEXT("and was announced"), SelectionProbe->LastIndex, 1);

	// Selection is not a pointer state -- it has to survive the pointer leaving -- so it rides the
	// selectable's NORMAL colour rather than a fifth transition it does not have.
	UDreamWidget* Chosen = Menu->GetWedgeWidget(1);
	if (TestNotNull(TEXT("the selected wedge exists"), Chosen))
	{
		if (UUIButton* Button = Chosen->GetComponent<UUIButton>())
		{
			TestEqual(TEXT("the selected wedge rests on the selected colour"),
				Button->GetNormalColor(), Menu->Style.WedgeSelected);
		}
		// And onto the visual directly, because SetNormalColor only repaints through a transition
		// and the tween manager returns null with no world.
		if (UDreamVisual* Visual = Chosen->GetVisual())
		{
			TestEqual(TEXT("and wears it right now"), Visual->GetColor(), Menu->Style.WedgeSelected);
		}
	}
	if (UDreamWidget* Other = Menu->GetWedgeWidget(0))
	{
		if (UDreamVisual* Visual = Other->GetVisual())
		{
			TestEqual(TEXT("an unselected wedge wears the normal colour"),
				Visual->GetColor(), Menu->Style.WedgeNormal);
		}
	}

	// A menu entry is a command: choosing the same one twice has to be sayable even though the
	// selection did not move.
	Menu->SetHighlightedIndex(1);
	Menu->ActivateHighlighted();
	Menu->ActivateHighlighted();
	TestEqual(TEXT("activation fires every time, selection change or not"), ActivationProbe->ActivationCalls, 2);
	TestEqual(TEXT("and carries the item's tag rather than its index"),
		ActivationProbe->LastTag.ToString(), FString(TEXT("Heal")));

	Menu->bAllowDeselect = true;
	Menu->ActivateHighlighted();
	TestEqual(TEXT("with deselect allowed, choosing the selected item clears it"),
		Menu->GetSelectedIndex(), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuDisabledAndStepTest,
	"DreamGUI.Controls.RingMenu.DisabledItemsAreSkippedAndAPartialRingHasEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRingMenuDisabledAndStepTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuTestLocal;

	TDreamTestControl<UDreamRingMenu> Menu(MakeWheel({
		MakeItem(TEXT("A")), MakeItem(TEXT("B"), 1.0f, false), MakeItem(TEXT("C")), MakeItem(TEXT("D")) }));
	Menu->ApplyStyle();

	// A disabled item is not a place the highlight can be, whichever route asked for it.
	Menu->SetHighlightedIndex(1);
	TestEqual(TEXT("a disabled item cannot be highlighted"),
		Menu->GetHighlightedIndex(), static_cast<int32>(INDEX_NONE));
	if (UDreamWidget* Wedge = Menu->GetWedgeWidget(1))
	{
		TestTrue(TEXT("and its widget says it is not interactable"),
			Wedge->GetInteractable() == EDreamWidgetInteractableType::Disabled);
	}

	Menu->SetHighlightedIndex(0);
	Menu->StepHighlight(1);
	TestEqual(TEXT("stepping walks over the disabled one"), Menu->GetHighlightedIndex(), 2);
	Menu->StepHighlight(-1);
	TestEqual(TEXT("and walks back over it"), Menu->GetHighlightedIndex(), 0);

	// A full wheel has no ends.
	Menu->StepHighlight(-1);
	TestEqual(TEXT("a full wheel wraps"), Menu->GetHighlightedIndex(), 3);

	// A partial one does, and pretending otherwise is how a keyboard user steps off the visible arc.
	Menu->Style.SweepAngle = 180.0f;
	Menu->ApplyStyle();
	Menu->SetHighlightedIndex(3);
	Menu->StepHighlight(1);
	TestEqual(TEXT("a partial ring stops at its last item"), Menu->GetHighlightedIndex(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuDirectionTest,
	"DreamGUI.Controls.RingMenu.ADirectionPicksTheSliceAndARestingStickPicksNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRingMenuDirectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuTestLocal;

	TDreamTestControl<UDreamRingMenu> Menu(MakeWheel({
		MakeItem(TEXT("Up")), MakeItem(TEXT("Right")), MakeItem(TEXT("Down")), MakeItem(TEXT("Left")) }));
	// Rotated back a half slice, so each item is centred on a compass point -- which is how a wheel
	// driven by a stick is actually authored.
	Menu->Style.StartAngle = -45.0f;
	Menu->ApplyStyle();

	Menu->HighlightByDirection(FVector2D(0.0, 1.0));
	TestEqual(TEXT("straight up is the first item"), Menu->GetHighlightedIndex(), 0);
	Menu->HighlightByDirection(FVector2D(1.0, 0.0));
	TestEqual(TEXT("right is the second"), Menu->GetHighlightedIndex(), 1);
	Menu->HighlightByDirection(FVector2D(-1.0, 0.0));
	TestEqual(TEXT("left is the fourth"), Menu->GetHighlightedIndex(), 3);

	// A stick at rest is a vector of nearly nothing pointing nowhere in particular; honouring its
	// MAGNITUDE is what stops it from picking a random item every frame. Against StickDeadZone,
	// which is a 0-to-1 magnitude -- measuring it against the ring's RADIUS would have rejected
	// every unit vector a gamepad ever produced.
	TestEqual(TEXT("the stick's dead zone is a magnitude, not a radius"), Menu->StickDeadZone, 0.25f);
	Menu->HighlightByDirection(FVector2D(0.02, 0.05));
	TestEqual(TEXT("a stick inside the dead zone picks nothing"),
		Menu->GetHighlightedIndex(), static_cast<int32>(INDEX_NONE));
	Menu->StickDeadZone = 0.0f;
	Menu->HighlightByDirection(FVector2D(0.02, 0.05));
	TestEqual(TEXT("and with no dead zone the same nudge is a direction like any other"),
		Menu->GetHighlightedIndex(), 0);

	// A partial ring genuinely does not cover every direction, and saying so is the point.
	Menu->Style.SweepAngle = 90.0f;
	Menu->Style.StartAngle = 0.0f;
	Menu->ApplyStyle();
	TestEqual(TEXT("an angle outside a partial sweep belongs to nobody"),
		Menu->IndexAtAngle(200.0f), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuPoolIdentityTest,
	"DreamGUI.Controls.RingMenu.ARestyleRebindsTheWedgesRatherThanRebuildingThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRingMenuPoolIdentityTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuTestLocal;

	TDreamTestControl<UDreamRingMenu> Menu(MakeWheel({
		MakeItem(TEXT("A")), MakeItem(TEXT("B")), MakeItem(TEXT("C")) }));
	Menu->ApplyStyle();

	UDreamWidget* Before = Menu->GetWedgeWidget(1);
	if (!TestNotNull(TEXT("the wedge exists"), Before))
	{
		return false;
	}
	// Creating or destroying a widget marks the UI outliner dirty, and the designer answers that by
	// force-refreshing the whole details view -- so a control whose ApplyStyle tore its pool down
	// would cost tens of milliseconds on every click in the panel. This is that guarantee, as a test.
	Menu->Style.OuterRadius = 320.0f;
	Menu->ApplyStyle();
	TestTrue(TEXT("a style edit keeps the same widget"), (UObject*)Menu->GetWedgeWidget(1) == (UObject*)Before);
	TestEqual(TEXT("and re-states its geometry"),
		static_cast<float>(Menu->GetWedgeWidget(1)->GetWidth()), 640.0f);

	// A change in the NUMBER of wedges is the one thing a rebind cannot carry.
	Menu->SetItems({ MakeItem(TEXT("A")), MakeItem(TEXT("B")) });
	TestNull(TEXT("the pool follows the item count down"), Menu->GetWedgeWidget(2));
	TestTrue(TEXT("and the survivors are still the same widgets"),
		(UObject*)Menu->GetWedgeWidget(1) == (UObject*)Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuMeasureTest,
	"DreamGUI.Controls.RingMenu.AnAutoConsumerMeasuresTheWholeRing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRingMenuMeasureTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuTestLocal;

	// The gallery's shape exactly, and the order the .dui path builds it in: the control is already
	// parented when its Initialize runs (a duplicated tree carries its parent links), and only
	// AFTERWARDS is the hierarchy registered. That order is the whole point of the test -- the
	// measured symptom was a ring drawing its full 192 while the vertical box above it reserved a
	// slot barely half that, so the next control in the column was painted straight across it.
	TDreamTestControl<UDreamWidget> Panel(NewObject<UDreamWidget>(GetTransientPackage()));
	UDreamPanelLayoutBase* Box = Cast<UDreamPanelLayoutBase>(
		Panel->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass()));
	if (!TestNotNull(TEXT("a consumer's box exists to ask"), Box))
	{
		return false;
	}

	UDreamRingMenu* Raw = NewObject<UDreamRingMenu>(GetTransientPackage());
	Raw->StyleSource = EDreamUIStyleSource::Inline;
	Raw->Style.OuterRadius = 96.0f;
	Raw->Style.InnerRadius = 40.0f;
	Raw->Items = { MakeItem(TEXT("A")), MakeItem(TEXT("B")), MakeItem(TEXT("C")) };
	Raw->SetParentBeforeRegister(Panel.Get());
	TDreamTestControl<UDreamRingMenu> Menu(Raw);
	Menu->Initialize();
	Menu->OnRegister();

	// The control states BOTH axes -- there is no "length comes from whoever placed it" for a circle.
	TestEqual(TEXT("the control sizes itself to the ring"), Menu->GetWidth(), 192.0f);
	TestEqual(TEXT("-- on both axes"), Menu->GetHeight(), 192.0f);

	// And what an Auto slot actually asks. Nothing under the control has an intrinsic size to
	// contribute (a rect block states none), so this answer comes from the authored snapshot alone,
	// which is exactly the thing a control that sizes ITSELF has to keep honest.
	const FVector2D Desired = Box->GetDesiredSize(Menu.Get());
	TestEqual(TEXT("an Auto consumer measures the whole ring -- width"),
		static_cast<float>(Desired.X), 192.0f);
	TestEqual(TEXT("-- and height, not the pre-style default"),
		static_cast<float>(Desired.Y), 192.0f);

	// A style edit has to carry into the measure too, or the first restyle desynchronises the
	// column: this is the path a details-panel edit takes.
	Menu->Style.OuterRadius = 60.0f;
	Menu->ApplyStyle();
	const FVector2D Restyled = Box->GetDesiredSize(Menu.Get());
	TestEqual(TEXT("a restyle moves the measure with it"),
		static_cast<float>(Restyled.Y), 120.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuHubTest,
	"DreamGUI.Controls.RingMenu.TheHubFollowsTheHighlightAndFallsBackTwice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRingMenuHubTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuTestLocal;

	TDreamTestControl<UDreamRingMenu> Menu(MakeWheel({
		MakeItem(TEXT("Attack")), MakeItem(TEXT("Heal")) }));
	Menu->HubText = FText::AsCultureInvariant(TEXT("CHOOSE"));
	Menu->ApplyStyle();

	UDreamText* Caption = Menu->HubLabelNode != nullptr
		? Cast<UDreamText>(Menu->HubLabelNode->GetVisual())
		: nullptr;
	if (!TestNotNull(TEXT("the hub has a caption"), Caption))
	{
		return false;
	}
	TestEqual(TEXT("with nothing chosen it is the authored title"), Caption->GetText().ToString(), FString(TEXT("CHOOSE")));

	// The highlight first, the selection second, the authored caption last: what the pointer is on
	// says more than what was chosen a moment ago, and both say more than a static title.
	Menu->SetSelectedIndexWithoutNotify(0);
	TestEqual(TEXT("a selection displaces the title"), Caption->GetText().ToString(), FString(TEXT("Attack")));
	Menu->SetHighlightedIndex(1);
	TestEqual(TEXT("and a highlight displaces the selection"), Caption->GetText().ToString(), FString(TEXT("Heal")));
	Menu->SetHighlightedIndex(INDEX_NONE);
	TestEqual(TEXT("losing the highlight falls back to the selection"), Caption->GetText().ToString(), FString(TEXT("Attack")));

	Menu->bHubFollowsHighlight = false;
	Menu->SetHighlightedIndex(1);
	TestEqual(TEXT("switched off, the hub is the title and nothing else"),
		Caption->GetText().ToString(), FString(TEXT("CHOOSE")));
	return true;
}

#endif
