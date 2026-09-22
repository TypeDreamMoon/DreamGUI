// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "DreamUILayoutLibrary.h"
#include "DreamUIWidgetGeometryLibrary.h"
#include "DreamUIWidgetLibrary.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

/*
 * The three Blueprint libraries that took the coordinate conversions, the widget searches and the
 * drag helpers out of C++-only territory.
 *
 * The assertions concentrate on the ONE promise that separates these from a naive port: a
 * conversion that cannot be made answers false rather than a number from the wrong space. A widget
 * with no canvas, a canvas drawn into a render target and a world-space canvas with no camera all
 * arrive here as "no viewport position", and a caller that reads the bool can tell them from a
 * position at the origin. Getting that backwards is how a marker ends up pinned to the corner of
 * the screen with nothing in the logs.
 */

namespace DreamUILibraryParityTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** A registered parent/child pair with a real panel slot on the child. */
	struct FSlottedPairFixture
	{
		UDreamWidget* Root = nullptr;
		UDreamWidget* Child = nullptr;

		bool Build(UWorld* World)
		{
			Root = NewObject<UDreamWidget>(World);
			Child = NewObject<UDreamWidget>(Root);
			Root->SetWidth(400.0f);
			Root->SetHeight(300.0f);
			Child->SetWidth(120.0f);
			Child->SetHeight(80.0f);
			if (!Child->TrySetParent(Root, false))
			{
				return false;
			}
			if (!Root->CreateNewLayoutContainer(UDreamLayoutContainerOverlay::StaticClass()))
			{
				return false;
			}
			Root->OnRegister();
			Child->OnRegister();
			return true;
		}

		void Destroy()
		{
			if (IsValid(Root))
			{
				Root->DestroyWidget();
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUILayoutLibrarySlotAsPanelSlotTest,
	"DreamGUI.Library.Layout.TheOneSlotAccessorAnswersForEveryKindOfParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUILayoutLibrarySlotAsPanelSlotTest::RunTest(const FString& Parameters)
{
	using namespace DreamUILibraryParityTestLocal;
	FScopedTestWorld TestWorld;
	FSlottedPairFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	// UMG needs a dozen SlotAs* nodes because each panel has its own slot class. There is one here,
	// so the node has to hand back exactly what the widget holds -- no cast, nothing to pick wrong.
	TestTrue(TEXT("the accessor returns the widget's own slot"),
		UDreamUILayoutLibrary::SlotAsPanelSlot(Fixture.Child) == Fixture.Child->GetPanelSlot());

	// A widget under a parent that hands out no slots is an ordinary state here, and it has to read
	// as null rather than as a mistake.
	TestNull(TEXT("a root with no parent has no slot"), UDreamUILayoutLibrary::SlotAsPanelSlot(Fixture.Root));
	TestNull(TEXT("and neither does nothing at all"), UDreamUILayoutLibrary::SlotAsPanelSlot(nullptr));

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWidgetLibraryFindsRegisteredWidgetsTest,
	"DreamGUI.Library.Find.WidgetsAreFoundByClassAndFilteredToRootsOnDemand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWidgetLibraryFindsRegisteredWidgetsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUILibraryParityTestLocal;
	FScopedTestWorld TestWorld;
	FSlottedPairFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	TArray<UDreamWidget*> Found;
	UDreamUIWidgetLibrary::GetAllWidgetsOfClass(Fixture.Root, UDreamWidget::StaticClass(), Found, false);
	TestTrue(TEXT("the root is found"), Found.Contains(Fixture.Root));
	TestTrue(TEXT("so is the nested child, which is the usual thing a caller is after"),
		Found.Contains(Fixture.Child));

	// The top-level filter is the half that is easy to get backwards: it must keep hierarchy ROOTS,
	// not "widgets with no parent pointer yet".
	UDreamUIWidgetLibrary::GetAllWidgetsOfClass(Fixture.Root, UDreamWidget::StaticClass(), Found, true);
	TestTrue(TEXT("the root survives the top-level filter"), Found.Contains(Fixture.Root));
	TestFalse(TEXT("the nested child does not"), Found.Contains(Fixture.Child));

	UDreamUIWidgetLibrary::GetAllWidgetsOfClass(Fixture.Root, nullptr, Found, false);
	TestEqual(TEXT("asking for no class finds nothing rather than everything"), Found.Num(), 0);

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIGeometryRefusesWhatItCannotAnswerTest,
	"DreamGUI.Library.Geometry.AWidgetWithNoViewportPositionSaysSoInsteadOfAnsweringZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIGeometryRefusesWhatItCannotAnswerTest::RunTest(const FString& Parameters)
{
	using namespace DreamUILibraryParityTestLocal;
	FScopedTestWorld TestWorld;
	FSlottedPairFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	// No canvas over this tree, so there is no space to place its rect in. That is the state every
	// world-space and render-target case degenerates to, and the whole point of the bool returns.
	const FDreamUIWidgetGeometry Geometry = UDreamUIWidgetGeometryLibrary::GetWidgetGeometry(Fixture.Child);
	TestTrue(TEXT("the geometry still names its widget"), Geometry.IsValidGeometry());
	TestEqual(TEXT("local size is known whatever else is not"),
		UDreamUIWidgetGeometryLibrary::GetLocalSize(Geometry), Fixture.Child->GetSize());
	TestFalse(TEXT("but it has no viewport position"), Geometry.bHasAbsolute);

	FVector2D Answer = FVector2D(999.0, 999.0);
	TestFalse(TEXT("local to absolute refuses"),
		UDreamUIWidgetGeometryLibrary::LocalToAbsolute(Geometry, FVector2D(10.0, 10.0), Answer));
	TestFalse(TEXT("absolute to local refuses"),
		UDreamUIWidgetGeometryLibrary::AbsoluteToLocal(Geometry, FVector2D(10.0, 10.0), Answer));
	TestFalse(TEXT("the absolute size refuses"),
		UDreamUIWidgetGeometryLibrary::GetAbsoluteSize(Geometry, Answer));
	TestFalse(TEXT("a point is not under a rect that has no place"),
		UDreamUIWidgetGeometryLibrary::IsUnderLocation(Geometry, FVector2D::ZeroVector));

	float Scalar = 999.0f;
	TestFalse(TEXT("a length cannot be converted either"),
		UDreamUIWidgetGeometryLibrary::TransformScalarLocalToAbsolute(Geometry, 10.0f, Scalar));
	TestEqual(TEXT("and the refused answer is zeroed, not left stale"), Scalar, 0.0f);

	// An empty geometry -- what a graph gets from a null widget -- must behave the same way rather
	// than crash or claim an origin.
	const FDreamUIWidgetGeometry Empty = UDreamUIWidgetGeometryLibrary::GetWidgetGeometry(nullptr);
	TestFalse(TEXT("an empty geometry names no widget"), Empty.IsValidGeometry());
	TestFalse(TEXT("and converts nothing"),
		UDreamUIWidgetGeometryLibrary::LocalToAbsolute(Empty, FVector2D::ZeroVector, Answer));

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWidgetLibraryDragHelpersTest,
	"DreamGUI.Library.DragDrop.AnOperationIsGivenToAPressedPointerAndNeverReplacesOneInFlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWidgetLibraryDragHelpersTest::RunTest(const FString& Parameters)
{
	using namespace DreamUILibraryParityTestLocal;
	FScopedTestWorld TestWorld;
	FSlottedPairFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	UDreamDragDropOperation* Operation = UDreamUIWidgetLibrary::CreateDragDropOperation(
		UDreamDragDropOperation::StaticClass());
	if (!TestNotNull(TEXT("an operation is made"), Operation))
	{
		Fixture.Destroy();
		return false;
	}
	// Outered to the transient package on purpose: an operation outlives the widget that made it,
	// and a list row recycled mid-drag is the ordinary case rather than the exotic one.
	TestTrue(TEXT("the operation belongs to nothing that can be destroyed under it"),
		Operation->GetOuter() == GetTransientPackage());

	UDreamPointerEventData* PointerEvent = NewObject<UDreamPointerEventData>(TestWorld.World);
	TestFalse(TEXT("a pointer that pressed nothing cannot be given a drag"),
		UDreamUIWidgetLibrary::BeginDragWithOperation(PointerEvent, Operation));

	PointerEvent->PressWidget = Fixture.Child;
	TestTrue(TEXT("a pressed pointer takes the operation"),
		UDreamUIWidgetLibrary::BeginDragWithOperation(PointerEvent, Operation));
	TestTrue(TEXT("it lands on the field the pipeline reads"), PointerEvent->DragOperation.Get() == Operation);
	TestTrue(TEXT("and the operation knows where it came from"),
		Operation->SourceWidget.Get() == Fixture.Child);

	// Replacing a live operation would leave drop targets lit up against one object and handlers
	// waiting on the cancel of another, so the second attempt has to be refused rather than obeyed.
	UDreamDragDropOperation* Second = UDreamUIWidgetLibrary::CreateDragDropOperation(nullptr);
	TestFalse(TEXT("a pointer already carrying one refuses a second"),
		UDreamUIWidgetLibrary::BeginDragWithOperation(PointerEvent, Second));
	TestTrue(TEXT("and keeps the first"), PointerEvent->DragOperation.Get() == Operation);

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWidgetLibraryBrushRoundTripTest,
	"DreamGUI.Library.Brush.AResourceSetThroughTheLibraryReadsBackAsItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWidgetLibraryBrushRoundTripTest::RunTest(const FString& Parameters)
{
	UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());

	FDreamUIImageBrush Brush = UDreamUIWidgetLibrary::MakeBrushFromTexture(Texture, 64, 32);
	TestTrue(TEXT("the texture reads back"), UDreamUIWidgetLibrary::GetBrushResource(Brush) == Texture);
	TestTrue(TEXT("and reads back typed"),
		UDreamUIWidgetLibrary::GetBrushResourceAsTexture2D(Brush) == Texture);
	TestNull(TEXT("a texture is not a material"), UDreamUIWidgetLibrary::GetBrushResourceAsMaterial(Brush));
	TestEqual(TEXT("the requested size is taken"), Brush.ImageSize, FVector2f(64.0f, 32.0f));

	// Zero means "whatever the resource is". Writing (0,0) instead would make the brush draw into
	// nothing, which is the failure a caller would blame on the texture.
	const FDreamUIImageBrush Unsized = UDreamUIWidgetLibrary::MakeBrushFromTexture(Texture, 0, 0);
	TestEqual(TEXT("an unsized brush keeps the default size"), Unsized.ImageSize, FDreamUIImageBrush().ImageSize);

	UDreamUIWidgetLibrary::SetBrushResourceToTexture(Brush, nullptr);
	TestNull(TEXT("clearing the resource clears it"), UDreamUIWidgetLibrary::GetBrushResource(Brush));
	TestNull(TEXT("and a brush that draws nothing holds nothing"),
		UDreamUIWidgetLibrary::GetBrushResource(UDreamUIWidgetLibrary::NoResourceBrush()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUILayoutLibraryViewportFallbacksTest,
	"DreamGUI.Library.Viewport.WithNoViewportTheScaleIsOneAndThePositionsRefuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUILayoutLibraryViewportFallbacksTest::RunTest(const FString& Parameters)
{
	using namespace DreamUILibraryParityTestLocal;
	FScopedTestWorld TestWorld;

	// A world built for a test has no game viewport, which is the same state a dedicated server and
	// a commandlet are in. A DPI scale of zero there would turn every division by it into an
	// infinity several call sites downstream, so the fallback is one.
	TestEqual(TEXT("the DPI scale falls back to one"),
		UDreamUILayoutLibrary::GetViewportScale(TestWorld.World), 1.0f);
	TestTrue(TEXT("the viewport size is zero rather than a guess"),
		UDreamUILayoutLibrary::GetViewportSize(TestWorld.World).IsNearlyZero());

	FVector2D MousePosition = FVector2D(999.0, 999.0);
	TestFalse(TEXT("there is no mouse to report"),
		UDreamUILayoutLibrary::GetMousePositionOnViewport(TestWorld.World, MousePosition));
	TestTrue(TEXT("and the refused answer is zeroed"), MousePosition.IsNearlyZero());

	FVector2D ScreenPosition = FVector2D::ZeroVector;
	float Distance = 999.0f;
	TestFalse(TEXT("a projection with no player refuses"),
		UDreamUILayoutLibrary::ProjectWorldLocationToWidgetPositionWithDistance(
			nullptr, FVector::ZeroVector, ScreenPosition, Distance, false));
	TestEqual(TEXT("and leaves no stale distance behind"), Distance, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUILibraryDynamicMaterialTest,
	"DreamGUI.Library.Brush.AskingABrushForItsDynamicMaterialTwiceAnswersWithTheSameInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUILibraryDynamicMaterialTest::RunTest(const FString& Parameters)
{
	// A brush with nothing a material could be made from says so, rather than inventing one.
	FDreamUIImageBrush Empty = UDreamUIWidgetLibrary::NoResourceBrush();
	TestNull(TEXT("a brush with no resource has no dynamic material"), UDreamUIWidgetLibrary::GetDynamicMaterial(Empty));
	TestNull(TEXT("and asking did not put one there"), Empty.GetResourceObject());

	UMaterialInterface* Parent = UMaterial::GetDefaultMaterial(MD_Surface);
	if (!TestNotNull(TEXT("the engine's default surface material is available"), Parent))
	{
		return false;
	}
	FDreamUIImageBrush Brush = UDreamUIWidgetLibrary::MakeBrushFromMaterial(Parent);

	UMaterialInstanceDynamic* First = UDreamUIWidgetLibrary::GetDynamicMaterial(Brush);
	if (!TestNotNull(TEXT("a material brush yields an instance"), First))
	{
		return false;
	}
	TestTrue(TEXT("the instance is of the material the brush held"), First->Parent == Parent);
	// The rewrite is the point: a parameter set on the returned instance only shows if the brush
	// now DRAWS that instance.
	TestTrue(TEXT("the brush now holds the instance"), Brush.GetResourceObject() == First);

	// Twice is the same object. A second Create here would hand back a fresh instance with none of
	// the parameters the caller set on the first, which is the bug this verb exists to avoid.
	TestTrue(TEXT("asking again answers with the same instance"), UDreamUIWidgetLibrary::GetDynamicMaterial(Brush) == First);
	return true;
}

#endif
