// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"
#include "DreamListControlsTestTypes.h"

#include "Controls/DreamBorder.h"
#include "Controls/DreamEditableText.h"
#include "Controls/DreamMenuAnchor.h"
#include "Controls/DreamNativeWidgetHost.h"
#include "Controls/DreamRichTextBlock.h"
#include "Controls/DreamThrobber.h"
#include "Controls/DreamTileView.h"
#include "Controls/DreamTreeView.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Demo/DreamUIShowcase.h"
#include "Extensions/DreamUMGWidget.h"
#include "Interaction/UITextInput.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The seven controls that closed the gap against UMG's palette, plus the tree's hierarchical source.
 *
 * Every one of them is assembled out of machinery this plugin already had -- the tile view is the
 * list with more than one column, the throbber is the widget tick, the menu anchor is the popup layer
 * plus the dropdown's blocker, the rich text block is UDreamText::bRichText, the host is
 * UDreamUMGWidget, the borderless fields are UDreamTextInput with its box switched off -- so what is
 * worth asserting is not that the machinery works (it has its own tests) but that the ASSEMBLY is
 * right: that the control reaches the piece, in the shape the control's header claims.
 *
 * Headless, like the rest of the control suite: no world, no registration, no layout pass and no
 * tween manager. Three consequences shape what is asserted below. A control with no world instances
 * no user widget, so the menu anchor's MenuClass road and the host's hosted widget are both "the node
 * exists and the class reached the bridge" rather than "the instance is there". Nothing ticks, so the
 * throbber's animation is driven one frame by hand. And the popup layer is a world subsystem that
 * hands back null, so an opened menu stays a child of its anchor -- which is exactly the state the
 * fallback paths are written for and worth pinning on its own.
 */
namespace DreamParityControlsTestLocal
{
	/** A control with a known rect and its OWN style, not yet built. The list suite's Author, shared. */
	template<class T>
	T* Author(float InWidth = 320.0f, float InHeight = 200.0f)
	{
		T* Control = NewObject<T>(GetTransientPackage());
		Control->StyleSource = EDreamUIStyleSource::Inline;
		Control->SetWidth(InWidth);
		Control->SetHeight(InHeight);
		return Control;
	}

	template<class T>
	T* Make(float InWidth = 320.0f, float InHeight = 200.0f)
	{
		T* Control = Author<T>(InWidth, InHeight);
		Control->Initialize();
		return Control;
	}

	TArray<FText> Labels(int32 InCount)
	{
		TArray<FText> Result;
		for (int32 Index = 0; Index < InCount; ++Index)
		{
			Result.Add(FText::AsCultureInvariant(FString::Printf(TEXT("Item %d"), Index)));
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTileViewGridTest,
	"DreamGUI.Controls.TileView.TilesFillTheWidthBeforeTheyStartANewLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTileViewGridTest::RunTest(const FString& Parameters)
{
	using namespace DreamParityControlsTestLocal;

	// A three-hundred wide viewport, hundred-wide tiles with no gap: three across, and the fourth
	// item starts the second line. Numbers chosen so "three columns" is arithmetic rather than a
	// measurement that happens to come out right.
	TDreamTestControl<UDreamTileView> Tiles(Author<UDreamTileView>(300.0f, 200.0f));
	Tiles->Style.TileWidth = 100.0f;
	Tiles->Style.TileSpacing = 0.0f;
	Tiles->Style.List.RowHeight = 50.0f;
	Tiles->Style.List.RowSpacing = 0.0f;
	Tiles->bShowScrollBar = false;
	Tiles->Items = Labels(7);
	Tiles->Initialize();

	if (!TestEqual(TEXT("seven items, seven tiles"), Tiles->GetRowCount(), 7))
	{
		return false;
	}
	TestEqual(TEXT("three tiles fit across a three-hundred wide viewport"), Tiles->GetColumnCount(), 3);
	// Seven items in threes is three lines, the last one short. The scroll range follows the LINES,
	// which is the whole difference between a tile view and a list.
	TestEqual(TEXT("and seven tiles make three lines"), Tiles->GetRowLineCount(), 3);
	TestEqual(TEXT("so the scrolled column is three rows tall, not seven"),
		Tiles->ColumnNode->GetHeight(), 3.0f * 50.0f);

	// The placement, item by item. A tile's column is its index modulo the count and its line is the
	// division, both taken from the DISPLAY index -- so tile 3 is directly under tile 0.
	auto TilePos = [&Tiles](int32 InIndex) -> FVector2D
	{
		UDreamWidget* Row = Tiles->RowNodes.IsValidIndex(InIndex) ? Tiles->RowNodes[InIndex].Get() : nullptr;
		return IsValid(Row) ? Row->GetAnchoredPosition() : FVector2D(-1.0, -1.0);
	};
	TestEqual(TEXT("the first tile sits at the column's left edge"), static_cast<float>(TilePos(0).X), 0.0f);
	TestEqual(TEXT("-- and its top"), static_cast<float>(TilePos(0).Y), 0.0f);
	TestEqual(TEXT("the second is one tile pitch across"), static_cast<float>(TilePos(1).X), 100.0f);
	TestEqual(TEXT("-- on the same line"), static_cast<float>(TilePos(1).Y), 0.0f);
	TestEqual(TEXT("the fourth is back at the left"), static_cast<float>(TilePos(3).X), 0.0f);
	TestEqual(TEXT("-- one row down"), static_cast<float>(TilePos(3).Y), -50.0f);

	// A tile has a WIDTH of its own, where a list's row is as wide as the column. That is the one
	// property the tile style adds and the reason the base's placement had to become a hook.
	if (UDreamWidget* FirstTile = Tiles->RowNodes[0].Get())
	{
		TestEqual(TEXT("a tile is the style's width"), FirstTile->GetWidth(), 100.0f);
		TestEqual(TEXT("and the list style's height"), FirstTile->GetHeight(), 50.0f);
	}

	// A narrower viewport re-flows. One column is the floor, never zero: a viewport narrower than a
	// tile shows one and clips it, where zero columns would divide by nothing and draw nothing.
	Tiles->SetWidth(120.0f);
	Tiles->HandleDimensionsChanged(false, true, false);
	TestEqual(TEXT("a narrower viewport fits one tile across"), Tiles->GetColumnCount(), 1);
	TestEqual(TEXT("so seven tiles are seven lines"), Tiles->GetRowLineCount(), 7);
	TestEqual(TEXT("and the scroll range grew with them"),
		Tiles->ColumnNode->GetHeight(), 7.0f * 50.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamThrobberTest,
	"DreamGUI.Controls.Throbber.ThePiecesArePlacedByShapeAndFadeOnTheirOwnClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamThrobberTest::RunTest(const FString& Parameters)
{
	using namespace DreamParityControlsTestLocal;

	TDreamTestControl<UDreamThrobber> Throbber(Author<UDreamThrobber>(64.0f, 16.0f));
	Throbber->Style.NumberOfPieces = 4;
	Throbber->Style.PieceSize = FVector2D(10.0, 10.0);
	Throbber->Style.PieceSpacing = 2.0f;
	Throbber->Style.Period = 1.0f;
	Throbber->Style.MinOpacity = 0.0f;
	Throbber->Initialize();

	if (!TestEqual(TEXT("the style's piece count is the number of pieces"), Throbber->PieceNodes.Num(), 4))
	{
		return false;
	}
	// A row, centred: four pieces at a pitch of twelve put the first one eighteen units left of the
	// middle and the last one eighteen right, which is the spelling that needs no odd/even case.
	TestEqual(TEXT("the first piece sits left of centre"),
		static_cast<float>(Throbber->PieceNodes[0]->GetAnchoredPosition().X), -18.0f);
	TestEqual(TEXT("and the last one the same distance right"),
		static_cast<float>(Throbber->PieceNodes[3]->GetAnchoredPosition().X), 18.0f);
	TestEqual(TEXT("every piece is on the same line"),
		static_cast<float>(Throbber->PieceNodes[2]->GetAnchoredPosition().Y), 0.0f);
	// The control states BOTH of its own dimensions, because neither comes from whoever placed it.
	TestEqual(TEXT("the control is as wide as its pieces and their gaps"), Throbber->GetWidth(), 46.0f);

	// Phase zero: piece 0 is at the top of the wave and the piece half a turn away is at the bottom.
	// The floor is zero here so "dimmest" is a number a test can name exactly.
	TestEqual(TEXT("the first piece starts bright"), Throbber->PieceNodes[0]->GetRenderOpacity(), 1.0f);
	TestEqual(TEXT("and the one opposite it starts dark"),
		Throbber->PieceNodes[2]->GetRenderOpacity(), 0.0f);

	// Half a period by hand, because nothing ticks a headless widget. The wave has travelled half a
	// turn, so the two swap.
	Throbber->NativeOnTick(0.5f);
	TestEqual(TEXT("half a cycle later the first piece is dark"),
		Throbber->PieceNodes[0]->GetRenderOpacity(), 0.0f);
	TestEqual(TEXT("and the opposite one is bright"),
		Throbber->PieceNodes[2]->GetRenderOpacity(), 1.0f);
	TestTrue(TEXT("the phase says so too"), FMath::IsNearlyEqual(Throbber->GetPhase(), 0.5f, 0.001f));

	// A full period from there wraps rather than accumulating -- a clock that only grows loses its
	// fraction after a few hours, and a throbber is exactly the widget somebody leaves up.
	Throbber->NativeOnTick(1.0f);
	TestTrue(TEXT("a full cycle later the phase is where it was"),
		FMath::IsNearlyEqual(Throbber->GetPhase(), 0.5f, 0.001f));

	// Stopped, the pieces stay where they are: a throbber that snapped back to phase zero would read
	// as a glitch rather than as a pause.
	Throbber->SetAnimate(false);
	Throbber->NativeOnTick(0.25f);
	TestTrue(TEXT("a stopped throbber does not move"),
		FMath::IsNearlyEqual(Throbber->GetPhase(), 0.5f, 0.001f));

	// The circular shape is the same pieces somewhere else, and the control squares itself off.
	Throbber->SetShape(EDreamThrobberShape::Circular);
	TestEqual(TEXT("the ring's first piece is straight up"),
		static_cast<float>(Throbber->PieceNodes[0]->GetAnchoredPosition().X), 0.0f);
	TestTrue(TEXT("-- at the style's radius"),
		FMath::IsNearlyEqual(static_cast<float>(Throbber->PieceNodes[0]->GetAnchoredPosition().Y),
			Throbber->Style.Radius, 0.01f));
	TestEqual(TEXT("and a ring is square"), Throbber->GetWidth(), Throbber->GetHeight());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBorderTest,
	"DreamGUI.Controls.Border.TheHoleIsHeldOffTheEdgeByTheStylesPadding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBorderTest::RunTest(const FString& Parameters)
{
	using namespace DreamParityControlsTestLocal;

	TDreamTestControl<UDreamBorder> Border(Author<UDreamBorder>());
	Border->Style.Padding = FMargin(7.0f, 8.0f, 9.0f, 10.0f);
	Border->Style.Background = FColor(11, 22, 33, 255);
	Border->Style.BorderThickness = 3.0f;
	Border->Style.BorderColor = FColor(44, 55, 66, 255);
	Border->Initialize();

	if (!TestNotNull(TEXT("the face exists"), Border->FaceNode.Get()) ||
		!TestNotNull(TEXT("and the hole"), Border->ContentNode.Get()))
	{
		return false;
	}
	TestTrue(TEXT("the hole lives inside the face"),
		(UObject*)Border->ContentNode->GetParent() == (UObject*)Border->FaceNode.Get());
	if (UDreamVisual* FaceVisual = Border->FaceNode->GetVisual())
	{
		TestEqual(TEXT("the face wears the style's colour"), FaceVisual->GetColor(), FColor(11, 22, 33, 255));
	}
	// The outline is the rect's own rather than a second widget, which is why a border costs one node.
	if (UDreamRectBlock* Rect = Cast<UDreamRectBlock>(Border->FaceNode->GetVisual()))
	{
		TestTrue(TEXT("a non-zero thickness turns the rect's border on"), Rect->GetEnableBorder());
		TestEqual(TEXT("at the style's width"), Rect->GetBorderWidth(), 3.0f);
		TestEqual(TEXT("in the style's colour"), Rect->GetBorderColor(), FColor(44, 55, 66, 255));
	}
	if (UDreamPanelSlot* ContentSlot = Border->ContentNode->GetPanelSlot())
	{
		TestEqual(TEXT("the padding holds the hole off the left edge"), ContentSlot->Padding.Left, 7.0f);
		TestEqual(TEXT("-- the top"), ContentSlot->Padding.Top, 8.0f);
		TestEqual(TEXT("-- the right"), ContentSlot->Padding.Right, 9.0f);
		TestEqual(TEXT("-- and the bottom"), ContentSlot->Padding.Bottom, 10.0f);
	}

	// The hole is a real named slot, which is what makes `Native.Border { ... }` work: the control's
	// declared slot name and the node the tree can find by that name are the same widget.
	TArray<FName> Declared;
	UDreamUserWidget::CollectDeclaredSlotNames(UDreamBorder::StaticClass(), Declared);
	TestTrue(TEXT("the control declares its hole"), Declared.Contains(UDreamBorder::ContentSlotName));
	TestTrue(TEXT("and the declared name finds the node the control kept"),
		(UObject*)Border->FindSlotWidget(UDreamBorder::ContentSlotName) == (UObject*)Border->ContentNode.Get());

	// Zero thickness is UMG's plain border, and the rect's outline goes off rather than to width zero.
	Border->Style.BorderThickness = 0.0f;
	static_cast<UDreamUIControl*>(Border.Get())->ApplyStyle();
	if (UDreamRectBlock* Rect = Cast<UDreamRectBlock>(Border->FaceNode->GetVisual()))
	{
		TestFalse(TEXT("zero thickness draws no outline at all"), Rect->GetEnableBorder());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextBlockTest,
	"DreamGUI.Controls.RichText.TheControlIsATextNodeWithTheMarkupSwitchedOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextBlockTest::RunTest(const FString& Parameters)
{
	using namespace DreamParityControlsTestLocal;

	TDreamTestControl<UDreamRichTextBlock> Rich(Author<UDreamRichTextBlock>());
	Rich->Style.TextColor = FColor(9, 8, 7, 255);
	Rich->Style.FontSize = 21.0f;
	Rich->Text = FText::AsCultureInvariant(TEXT("plain <b>bold</b> plain"));
	Rich->TagFilterFlags = 0x7;
	Rich->Initialize();

	UDreamText* TextVisual = Rich->TextNode != nullptr ? Cast<UDreamText>(Rich->TextNode->GetVisual()) : nullptr;
	if (!TestNotNull(TEXT("the paragraph exists"), TextVisual))
	{
		return false;
	}
	// The flag IS the control. Everything else it pushes, any text node could have been told.
	TestTrue(TEXT("the markup is on"), TextVisual->GetRichText());
	TestEqual(TEXT("the filter reached the node"), TextVisual->GetRichTextTagFilterFlags(), 0x7);
	TestEqual(TEXT("the prose reached it"), TextVisual->GetText().ToString(),
		FString(TEXT("plain <b>bold</b> plain")));
	TestEqual(TEXT("and the style's colour"), TextVisual->GetColor(), FColor(9, 8, 7, 255));
	TestEqual(TEXT("and its size"), TextVisual->GetFontSize(), 21.0f);

	// Writing the prose does not re-push the style: a control whose whole job is to be written to
	// should not walk two data assets and six fields to change a string.
	Rich->SetText(FText::AsCultureInvariant(TEXT("second")));
	TestEqual(TEXT("a later write reaches the node"), TextVisual->GetText().ToString(), FString(TEXT("second")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorTest,
	"DreamGUI.Controls.MenuAnchor.OpeningPlacesThePopupAgainstTheAnchorAndClosingPutsItAway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMenuAnchorTest::RunTest(const FString& Parameters)
{
	using namespace DreamParityControlsTestLocal;

	TDreamTestControl<UDreamMenuAnchor> Anchor(Author<UDreamMenuAnchor>(120.0f, 30.0f));
	Anchor->Style.Placement = EDreamMenuPlacement::BelowAnchor;
	Anchor->Style.Offset = 4.0f;
	Anchor->Style.TransitionDuration = 0.0f;
	Anchor->MenuSize = FVector2D(160.0, 90.0);
	Anchor->Initialize();

	if (!TestNotNull(TEXT("the popup exists"), Anchor->PopupNode.Get()) ||
		!TestNotNull(TEXT("and the hole inside it"), Anchor->MenuNode.Get()))
	{
		return false;
	}
	// A popup is not part of the layout it hangs off: an Auto row holding this anchor must not grow
	// by the menu's height the moment it opens.
	TestTrue(TEXT("the popup is out of the layout"), Anchor->PopupNode->GetIgnoreLayout());
	TestFalse(TEXT("and starts asleep"), Anchor->PopupNode->GetWidgetActive());
	TestFalse(TEXT("so the anchor starts closed"), Anchor->IsOpen());

	TStrongObjectPtr<UDreamListControlsProbe> Probe(NewObject<UDreamListControlsProbe>(GetTransientPackage()));
	Anchor->OnMenuOpenChanged.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordOpenChanged);

	Anchor->Open();
	TestTrue(TEXT("opening opens it"), Anchor->IsOpen());
	TestTrue(TEXT("the popup woke up"), Anchor->PopupNode->GetWidgetActive());
	if (!TestEqual(TEXT("and it was announced once"), Probe->OpenStates.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("as open"), Probe->OpenStates[0]);
	// WHERE it lands is UDreamLayoutContainerMenuAnchor::CalculateMenuPosition's answer, not a second
	// copy of that arithmetic -- so these numbers are the layout panel's numbers, and a menu opened by
	// either of the two lands in the same place. The popup is pinned by its own top-left to the
	// anchor's top-left, which makes the anchored position that function's answer with y negated
	// (it works in y-down space; widgets here are y-up).
	//
	// Below a thirty-tall anchor with a gap of four: thirty down, then four more.
	TestEqual(TEXT("the popup is pinned to the anchor's top-left"),
		static_cast<float>(Anchor->PopupNode->GetAnchorMin().Y), 1.0f);
	TestEqual(TEXT("by its own top edge"),
		static_cast<float>(Anchor->PopupNode->GetPivot().Y), 1.0f);
	TestEqual(TEXT("and sits the anchor's height plus the gap below it"),
		static_cast<float>(Anchor->PopupNode->GetAnchoredPosition().Y), -34.0f);
	TestEqual(TEXT("with its left edge on the anchor's"),
		static_cast<float>(Anchor->PopupNode->GetAnchoredPosition().X), 0.0f);
	TestEqual(TEXT("at the authored size"), Anchor->PopupNode->GetWidth(), 160.0f);
	TestEqual(TEXT("-- both axes"), Anchor->PopupNode->GetHeight(), 90.0f);
	// No world, so no popup layer and no tween: the fallback is a popup that stays a child of its
	// anchor and is fully opaque rather than mid-fade.
	TestTrue(TEXT("with no world the popup stays where it was built"),
		(UObject*)Anchor->PopupNode->GetParent() == (UObject*)Anchor.Get());
	TestEqual(TEXT("and snaps to visible rather than fading from nothing"),
		Anchor->PopupNode->GetRenderOpacity(), 1.0f);

	// Opening an open menu is not a second open.
	Anchor->Open();
	TestEqual(TEXT("opening twice announces once"), Probe->OpenStates.Num(), 1);

	Anchor->Close();
	TestFalse(TEXT("closing closes it"), Anchor->IsOpen());
	TestFalse(TEXT("and puts the popup back to sleep"), Anchor->PopupNode->GetWidgetActive());
	TestEqual(TEXT("announced again"), Probe->OpenStates.Num(), 2);
	TestFalse(TEXT("as closed"), Probe->OpenStates[1]);
	Anchor->Close();
	TestEqual(TEXT("closing twice announces once"), Probe->OpenStates.Num(), 2);

	// The other placements are the same function's other answers, and the gap widens along whichever
	// direction the placement chose rather than always downwards.
	Anchor->Style.Placement = EDreamMenuPlacement::MenuRight;
	static_cast<UDreamUIControl*>(Anchor.Get())->ApplyStyle();
	TestEqual(TEXT("to the right starts past the anchor's width, plus the gap"),
		static_cast<float>(Anchor->PopupNode->GetAnchoredPosition().X), 124.0f);
	TestEqual(TEXT("with its top on the anchor's"),
		static_cast<float>(Anchor->PopupNode->GetAnchoredPosition().Y), 0.0f);

	Anchor->Style.Placement = EDreamMenuPlacement::AboveAnchor;
	static_cast<UDreamUIControl*>(Anchor.Get())->ApplyStyle();
	TestEqual(TEXT("above means a whole menu height up, and the gap goes up too"),
		static_cast<float>(Anchor->PopupNode->GetAnchoredPosition().Y), 94.0f);

	Anchor->Style.Placement = EDreamMenuPlacement::Center;
	static_cast<UDreamUIControl*>(Anchor.Get())->ApplyStyle();
	TestEqual(TEXT("centred puts the size difference either side -- horizontally"),
		static_cast<float>(Anchor->PopupNode->GetAnchoredPosition().X), -20.0f);
	TestEqual(TEXT("-- and vertically, with no gap to widen"),
		static_cast<float>(Anchor->PopupNode->GetAnchoredPosition().Y), 30.0f);

	// A ComboBox placement forces the menu to the anchor's width. That rule is the layout panel's --
	// asked of it rather than restated here, so the two anchors cannot disagree about which
	// placements it applies to.
	Anchor->Style.Placement = EDreamMenuPlacement::ComboBox;
	static_cast<UDreamUIControl*>(Anchor.Get())->ApplyStyle();
	TestEqual(TEXT("a combo box menu is exactly the anchor's width"),
		Anchor->PopupNode->GetWidth(), 120.0f);
	TestEqual(TEXT("and still the authored height"), Anchor->PopupNode->GetHeight(), 90.0f);

	// The hole is a real named slot, like the border's.
	TArray<FName> Declared;
	UDreamUserWidget::CollectDeclaredSlotNames(UDreamMenuAnchor::StaticClass(), Declared);
	TestTrue(TEXT("the control declares its menu hole"), Declared.Contains(UDreamMenuAnchor::MenuSlotName));
	TestTrue(TEXT("and the declared name finds the node the control kept"),
		(UObject*)Anchor->FindSlotWidget(UDreamMenuAnchor::MenuSlotName) == (UObject*)Anchor->MenuNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEditableTextTest,
	"DreamGUI.Controls.EditableText.TheBorderlessFieldDrawsNoBoxInAnyState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamEditableTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamParityControlsTestLocal;

	// The boxed field first, as the control it is: this is the thing the borderless one differs from,
	// and asserting the difference needs both halves.
	TDreamTestControl<UDreamTextInput> Boxed(Author<UDreamTextInput>());
	Boxed->Initialize();
	if (!TestNotNull(TEXT("the boxed field has a background"), Boxed->BackgroundNode.Get()))
	{
		return false;
	}
	if (UDreamVisual* BoxedFace = Boxed->BackgroundNode->GetVisual())
	{
		TestTrue(TEXT("and it is not transparent"), BoxedFace->GetColor().A > 0);
	}

	TDreamTestControl<UDreamEditableText> Bare(Author<UDreamEditableText>());
	Bare->Initialize();
	if (!TestNotNull(TEXT("the borderless field still has the node"), Bare->BackgroundNode.Get()) ||
		!TestNotNull(TEXT("and the behaviour"), Bare->InputBehaviour.Get()))
	{
		return false;
	}
	const FColor Clear(0, 0, 0, 0);
	if (UDreamVisual* BareFace = Bare->BackgroundNode->GetVisual())
	{
		TestEqual(TEXT("but the face is transparent"), BareFace->GetColor(), Clear);
	}
	// All five states, not just the resting one: a field that merely STARTED transparent would fill
	// in again the first time the pointer crossed it, which is an invisible box that appears on hover.
	TestEqual(TEXT("the resting state draws nothing"), Bare->InputBehaviour->GetNormalColor(), Clear);
	TestEqual(TEXT("nor the hovered one"), Bare->InputBehaviour->GetHoveredColor(), Clear);
	TestEqual(TEXT("nor the pressed one"), Bare->InputBehaviour->GetPressedColor(), Clear);
	TestEqual(TEXT("nor the disabled one"), Bare->InputBehaviour->GetDisabledColor(), Clear);
	TestEqual(TEXT("nor the focused one"), Bare->InputBehaviour->GetFocusedColor(), Clear);
	// Everything else is the boxed field's, which is the point of deriving rather than re-writing.
	TestFalse(TEXT("and it is a single-line field by default"), Bare->bMultiLine);

	TDreamTestControl<UDreamMultiLineEditableText> Multi(Author<UDreamMultiLineEditableText>());
	Multi->Initialize();
	TestTrue(TEXT("the multi-line class is multi-line by construction"), Multi->bMultiLine);
	if (UDreamVisual* MultiFace = Multi->BackgroundNode != nullptr ? Multi->BackgroundNode->GetVisual() : nullptr)
	{
		TestEqual(TEXT("and borderless with it"), MultiFace->GetColor(), Clear);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNativeWidgetHostTest,
	"DreamGUI.Controls.NativeWidgetHost.TheHostNodeCarriesTheBridgeAndTheClassReachesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNativeWidgetHostTest::RunTest(const FString& Parameters)
{
	using namespace DreamParityControlsTestLocal;

	TDreamTestControl<UDreamNativeWidgetHost> Host(Author<UDreamNativeWidgetHost>(200.0f, 100.0f));
	Host->ResolutionScale = 2.0f;
	Host->Initialize();

	if (!TestNotNull(TEXT("the host node exists"), Host->HostNode.Get()) ||
		!TestNotNull(TEXT("and its visual is the bridge"), Host->HostVisual.Get()))
	{
		return false;
	}
	TestTrue(TEXT("the bridge IS the node's visual"),
		(UObject*)Host->HostNode->GetVisual() == (UObject*)Host->HostVisual.Get());
	TestEqual(TEXT("the resolution scale reached it"), Host->HostVisual->GetResolutionScale(), 2.0f);
	// No world, so nothing is instanced -- and that is the documented answer rather than a failure.
	TestNull(TEXT("with no world there is no hosted widget"), Host->GetHostedWidget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTreeViewChildrenProviderTest,
	"DreamGUI.Controls.TreeView.AChildrenProviderFlattensTheWholeHierarchyWithItsDepths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTreeViewChildrenProviderTest::RunTest(const FString& Parameters)
{
	using namespace DreamParityControlsTestLocal;

	// Two roots; the first has two children and the first of those has one of its own. Three depths,
	// so a walk that lost one would be visible in the indents rather than only in the count.
	UObject* Root = NewObject<UDreamUIShowcaseTrack>(GetTransientPackage());
	UObject* ChildA = NewObject<UDreamUIShowcaseTrack>(GetTransientPackage());
	UObject* ChildB = NewObject<UDreamUIShowcaseTrack>(GetTransientPackage());
	UObject* GrandChild = NewObject<UDreamUIShowcaseTrack>(GetTransientPackage());
	UObject* Other = NewObject<UDreamUIShowcaseTrack>(GetTransientPackage());

	TStrongObjectPtr<UDreamListControlsProbe> Provider(NewObject<UDreamListControlsProbe>(GetTransientPackage()));
	Provider->Children.Add(Root, { ChildA, ChildB });
	Provider->Children.Add(ChildA, { GrandChild });

	TDreamTestControl<UDreamTreeView> Tree(Make<UDreamTreeView>());
	Tree->OnGetItemChildren.BindDynamic(Provider.Get(), &UDreamListControlsProbe::ProvideChildren);
	Tree->SetRootItems({ Root, Other });

	// Pre-order, whole: root, its first child, that child's child, its second child, then the other
	// root. The children of a node nobody has collapsed are still in the source -- and so are the
	// children of one somebody has, which is what keeps every index in this control's API stable.
	if (!TestEqual(TEXT("the walk flattened all five nodes"), Tree->GetItemCount(), 5))
	{
		return false;
	}
	TestEqual(TEXT("in pre-order -- the root first"), Tree->ItemDepths[0], 0);
	TestEqual(TEXT("then its child"), Tree->ItemDepths[1], 1);
	TestEqual(TEXT("then the grandchild"), Tree->ItemDepths[2], 2);
	TestEqual(TEXT("then the second child, back out one"), Tree->ItemDepths[3], 1);
	TestEqual(TEXT("and the other root at the top level"), Tree->ItemDepths[4], 0);
	TestEqual(TEXT("every node has a row to begin with"), Tree->GetRowCount(), 5);
	TestTrue(TEXT("a node with children draws a twisty"), Tree->ItemHasChildren(0));
	TestFalse(TEXT("a leaf does not"), Tree->ItemHasChildren(2));

	// Folding is still a skip over the run of deeper rows, which is what the flat source buys.
	Tree->SetItemExpanded(0, false);
	TestEqual(TEXT("folding the root hides its whole subtree"), Tree->GetRowCount(), 2);
	TestTrue(TEXT("and it still knows it has children to unfold"), Tree->ItemHasChildren(0));

	// A re-walk keeps the fold, because folds are kept by item identity: the node is still in the
	// tree, so it is still folded wherever the walk puts it.
	Tree->RefreshTree();
	TestEqual(TEXT("a re-walk keeps the fold"), Tree->GetRowCount(), 2);
	TestFalse(TEXT("on the same node"), Tree->IsItemExpanded(0));

	// A cycle flattens to a tree rather than hanging: an item already visited is not visited twice.
	Provider->Children.Add(GrandChild, { Root });
	Tree->RefreshTree();
	TestEqual(TEXT("a cycle adds no nodes and does not hang"), Tree->GetItemCount(), 5);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
