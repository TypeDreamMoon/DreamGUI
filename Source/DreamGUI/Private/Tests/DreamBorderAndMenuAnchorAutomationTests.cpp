// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"

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
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

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

	// There is no second brush: a DreamGUI widget's art is its visual, and BrushColor names that
	// visual's colour so UBorder::SetBrushColor has a counterpart instead of a missing feature.
	Border->SetBrushColor(FLinearColor(1.0f, 0.0f, 0.0f, 1.0f));
	const FColor Applied = Background->GetColor();
	TestEqual(TEXT("Red reaches the visual"), static_cast<int32>(Applied.R), 255);
	TestEqual(TEXT("...and nothing else does"), static_cast<int32>(Applied.G), 0);
	TestEqual(TEXT("...on either channel"), static_cast<int32>(Applied.B), 0);

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

#endif
