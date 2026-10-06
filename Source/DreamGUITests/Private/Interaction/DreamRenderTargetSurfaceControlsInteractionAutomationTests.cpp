// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamListView.h"
#include "Controls/DreamScrollBox.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverVirtualCamera.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/*
 * A SCROLL BOX, A TEXT FIELD AND A LIST DRAWN INTO A TEXTURE THAT A MESH IN THE LEVEL SHOWS.
 *
 * UMG's UWidgetComponent drawn into a render target and its UWidgetInteractionComponent are the pair this stands for: the
 * interaction finds the hit on the mesh, turns it into a point on the widget, and from there the widget is pointed at
 * like any other -- a right-button drag scrolls an SScrollBox the full distance the pointer travelled, a click puts an
 * SEditableText into edit and the characters typed go into it, a click on an SObjectTableRow selects its item. DreamGUI's
 * road is the world pointer's ray onto the surface, the surface's UV, the canvas's own deprojection
 * (DreamGUI.Driver.RenderTargetMesh.*); these drive the three controls nothing had pointed at on a surface before.
 *
 * Every pixel is the camera's: the driver runs the road backwards (FDreamDriverProjection's camera overloads), so a
 * pixel it aims at is a pixel whose ray lands back on the point it came from. The surface is a 400 by 300 texture shown
 * 400 by 300 cm, three metres ahead, face on -- about two pixels to a texel.
 */
namespace DreamRenderTargetSurfaceControlsTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FIntPoint TargetSize(400, 300);

	struct FSurfaceStage
	{
		UDreamDriverWorldSpaceRaycaster* Pointer = nullptr;
		DreamDriverWorld::FDreamRenderTargetMesh Screen;

		bool IsReady() const { return Pointer != nullptr && Screen.IsComplete(); }
	};

	FSurfaceStage SetUpStage(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		FSurfaceStage Stage;
		Stage.Pointer = DreamDriverWorld::AttachWorldPointer(InRig,
			DreamDriverWorld::MakeView(FVector::ZeroVector, FRotator::ZeroRotator, 90.0f, ViewportSize), EDreamWorldPointerSource::Mouse);
		Stage.Screen = DreamDriverWorld::MakeRenderTargetMesh(InRig, TEXT("Screen"), FTransform(FVector(300.0, 0.0, 0.0)), TargetSize);
		if (InTest.TestNotNull(TEXT("A world pointer"), Stage.Pointer))
		{
			// The only way a world pointer's trace meets a surface at all: the surface is a blocking body.
			Stage.Pointer->SetOccludeByWorld(true);
		}
		InTest.TestTrue(TEXT("The render-target canvas and the surface showing it were built"), Stage.Screen.IsComplete());
		return Stage;
	}

	/** The pixel a widget on the surface's canvas is seen at, through the rig's camera. */
	TOptional<FVector2D> PixelOf(const FDreamDriverRig& InRig, const UDreamWidget* InWidget)
	{
		return FDreamDriverProjection::WidgetCentrePixel(InWidget, InRig.Context().Camera.Get());
	}

	bool IsEditing(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->InputBehaviour != nullptr && InField->InputBehaviour->IsInputActive();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSurfaceScrollBoxDragTest,
	"DreamGUI.ScrollBox.ShownOnARenderTargetSurfaceARightButtonDragKeepsTheContentUnderThePointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSurfaceScrollBoxDragTest, "DreamGUI.ScrollBox.ShownOnARenderTargetSurfaceARightButtonDragKeepsTheContentUnderThePointer", "[Pointer][World]")

/*
 * SScrollBox with bAllowRightClickDragScrolling (on by default): once the pointer has travelled the drag trigger distance,
 * every move scrolls by its own delta, the move that crossed the distance included, so the content stays under the pointer
 * that grabbed it. On the surface the travel is the pointer's on the texture: 150 pixels up the viewport, in the canvas's
 * units, is what the offset comes to. Read with the button still down, as the screen test reads it, because letting go of a
 * moving drag flings the content on.
 */
bool FDreamSurfaceScrollBoxDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTargetSurfaceControlsTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	const FSurfaceStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("Box"), Stage.Screen.CanvasRoot, FVector2D(300.0, 240.0));
	if (!TestTrue(TEXT("The box came up on the surface's canvas with a viewport and a content node"),
		Box != nullptr && Box->ViewportNode != nullptr && Box->GetContentNode() != nullptr))
	{
		return false;
	}
	for (int32 RowIndex = 0; RowIndex < 20; ++RowIndex)
	{
		Rig.MakeWidget(FString::Printf(TEXT("Box_Row%02d"), RowIndex), Box->GetContentNode(), FVector2D(300.0, 60.0));
	}
	Box->RefreshContentExtent();
	Rig.PumpFrames(2);
	const FDreamDriverVirtualCamera* Camera = Rig.Context().Camera.Get();
	const TOptional<FBox2D> ViewportRect = FDreamDriverProjection::WidgetToPixelRect(Box->ViewportNode.Get(), Camera);
	const TOptional<FVector2D> Grab = PixelOf(Rig, Box->ViewportNode.Get());
	if (!TestTrue(TEXT("The box's viewport is seen on the surface, with a height"),
		ViewportRect.IsSet() && Grab.IsSet() && ViewportRect->Max.Y - ViewportRect->Min.Y > 1.0))
	{
		return false;
	}
	// Canvas units per viewport pixel: the surface faces the eye, so the texture is seen at one scale all over.
	const double UnitsPerPixel = Box->ViewportNode->GetHeight() / (ViewportRect->Max.Y - ViewportRect->Min.Y);
	TStrongObjectPtr<UDreamDragInteractionProbe> UserScrolled(NewObject<UDreamDragInteractionProbe>());
	Box->OnUserScrolled.AddDynamic(UserScrolled.Get(), &UDreamDragInteractionProbe::RecordFloat);

	// Past the world pointer's own threshold on the first move -- it is measured where the ray lands, in centimetres, and
	// forty pixels is nearly twenty of them here -- then the rest in two halves.
	const double FirstMove = 40.0;
	const double LaterMove = (150.0 - FirstMove) * 0.5;
	TestTrue(TEXT("The right-button drag up the box on the surface completes"),
		Rig.Driver()->Sequence()
			.MoveToPixel(Grab.GetValue())
			.Press(EDreamUIMouseButtonType::Right)
			.MoveBy(FVector2D(0.0, -FirstMove))
			.MoveBy(FVector2D(0.0, -LaterMove))
			.MoveBy(FVector2D(0.0, -LaterMove))
			.WaitFrames(1)
			.Perform());
	const float Offset = Box->GetScrollOffset();
	TestTrue(TEXT("Letting go of the right button completes"),
		Rig.Driver()->Sequence().Release(EDreamUIMouseButtonType::Right).Perform());
	const float Pulled = static_cast<float>(150.0 * UnitsPerPixel);
	TestTrue(FString::Printf(TEXT("The drag scrolled the content the way it was pulled (offset %.1f)"), Offset), Offset > 0.5f * Pulled);
	TestNearlyEqual(TEXT("The content moved the pointer's whole travel on the texture, staying under the pointer"),
		Offset, Pulled, static_cast<float>(2.0 * UnitsPerPixel));
	TestTrue(TEXT("The drag was reported as the user scrolling"), UserScrolled->NumFloats() >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSurfaceTextInputTypingTest,
	"DreamGUI.TextInput.ShownOnARenderTargetSurfaceAClickBeginsAnEditAndWhatIsTypedGoesIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSurfaceTextInputTypingTest, "DreamGUI.TextInput.ShownOnARenderTargetSurfaceAClickBeginsAnEditAndWhatIsTypedGoesIn", "[Pointer][Text][World]")

/*
 * SEditableText takes the keyboard focus on a click and every character after that goes into it, whatever surface the
 * click was made through. So a click on the field shown on the surface begins its edit, "hi" typed then is its text, and
 * Enter ends the edit keeping it -- ended here rather than left to the tear-down, because the surface's canvas is not under
 * the rig's root and an edit left open on it would be one the rig reports against the test.
 */
bool FDreamSurfaceTextInputTypingTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTargetSurfaceControlsTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FSurfaceStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamTextInput* Field = Rig.MakeControl<UDreamTextInput>(TEXT("Username"), Stage.Screen.CanvasRoot, FVector2D(320.0, 40.0), FVector2D(0.0, 60.0));
	if (!TestNotNull(TEXT("A text field on the surface's canvas"), Field))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const TOptional<FVector2D> Pixel = PixelOf(Rig, Field);
	if (!TestTrue(TEXT("The field shown on the surface has a pixel"), Pixel.IsSet()))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the field on the surface completes"),
		Rig.Driver()->Sequence().MoveToPixel(Pixel.GetValue()).Press().Release().Perform());
	if (!TestTrue(TEXT("The click through the surface began the field's edit"), IsEditing(Field)))
	{
		return false;
	}
	TestTrue(TEXT("Typing completes"), Rig.Driver()->Sequence().Type(TEXT("hi")).Perform());
	TestEqual(TEXT("The field holds what was typed"), Field->GetText(), FString(TEXT("hi")));

	TestTrue(TEXT("Enter completes"), Rig.Driver()->Sequence().Key(EKeys::Enter).Perform());
	TestFalse(TEXT("Enter ended the edit"), IsEditing(Field));
	TestEqual(TEXT("...keeping the text"), Field->GetText(), FString(TEXT("hi")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSurfaceListClickTest,
	"DreamGUI.ListView.ShownOnARenderTargetSurfaceClickingTheThirdRowSelectsItWithOneSelectionChangeAndOneClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSurfaceListClickTest, "DreamGUI.ListView.ShownOnARenderTargetSurfaceClickingTheThirdRowSelectsItWithOneSelectionChangeAndOneClick", "[Pointer][World]")

/*
 * SObjectTableRow's click -- one selection change and one click -- through the surface: the third row of a list shown on
 * the texture, clicked where the camera sees it, is the selection, and nothing else is.
 */
bool FDreamSurfaceListClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTargetSurfaceControlsTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FSurfaceStage Stage = SetUpStage(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamListView* List = Rig.MakeControl<UDreamListView>(TEXT("List"), Stage.Screen.CanvasRoot, FVector2D(300.0, 240.0));
	if (!TestNotNull(TEXT("A list on the surface's canvas"), List))
	{
		return false;
	}
	List->SetStyleSource(EDreamUIStyleSource::Inline);
	List->SetStyle(DreamListsInteraction::WithRows(List->GetStyle(), 40.0f));
	const TArray<UObject*> Items = DreamListsInteraction::MakeItems(12);
	List->SetItemObjects(Items);
	Rig.PumpFrames(2);
	TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	DreamListsInteraction::ListenToList(*List, *Probe);

	const TOptional<FVector2D> Pixel = PixelOf(Rig, List->GetRowWidget(2));
	if (!TestTrue(TEXT("The third item's row is seen on the surface"), Pixel.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("Clicking the third row on the surface completes"),
		Rig.Driver()->Sequence().MoveToPixel(Pixel.GetValue()).Press().Release().WaitFrames(1).Perform());

	TestEqual(TEXT("The third item is the selection"), List->GetSelectedIndex(), 2);
	TestEqual(TEXT("...and the only one"), List->GetSelectedItems().Num(), 1);
	if (TestEqual(TEXT("The selection changed once"), Probe->SelectionChanges.Num(), 1))
	{
		TestEqual(TEXT("...to the third item"), Probe->SelectionChanges[0], 2);
	}
	if (TestEqual(TEXT("One click was announced"), Probe->ClickedItems.Num(), 1))
	{
		TestEqual(TEXT("...for the third item"), Probe->ClickedItems[0], 2);
	}
	return true;
}

#endif
