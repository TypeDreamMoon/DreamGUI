// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "PrefabEditor/LexUIPrefabEditor.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

// The toolkit itself needs a live asset editor, so what is pinned here is each decision it used to
// make inline: what a selection does with a widget that is already in it, where the camera is sent
// when nothing in the prefab is active, where a designer preference is stored, and the two bits of
// arithmetic behind the zoom and screen-size controls.
namespace LexEditorToolkitTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeWidget(UWorld* World, const TCHAR* Name, float Width, float Height)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(World);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(Width);
		Widget->SetHeight(Height);
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexSelectionTogglesInsteadOfDuplicatingTest,
	"LGUI.Editor.Selection.CtrlClickTogglesAndNeverDuplicates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexSelectionTogglesInsteadOfDuplicatingTest::RunTest(const FString& Parameters)
{
	using namespace LexEditorToolkitTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<ULexUISelection> Selection(NewObject<ULexUISelection>(GetTransientPackage()));
	TStrongObjectPtr<ULexWidget> First(MakeWidget(TestWorld.World, TEXT("First"), 100.0f, 30.0f));
	TStrongObjectPtr<ULexWidget> Second(MakeWidget(TestWorld.World, TEXT("Second"), 100.0f, 30.0f));

	int32 BroadcastCount = 0;
	Selection->OnSelectionChanged.AddLambda([&BroadcastCount]() { ++BroadcastCount; });

	Selection->SelectWidget(First.Get());
	Selection->SelectWidget(First.Get());
	// A second entry for the same widget makes Align and Distribute apply their delta to it twice.
	TestEqual(TEXT("re-selecting a widget leaves it in the selection once"), Selection->GetSelectedWidgets().Num(), 1);

	Selection->SelectWidget(Second.Get());
	Selection->DeselectWidget(First.Get());
	TestFalse(TEXT("the deselected widget is gone"), Selection->IsSelected(First.Get()));
	TestTrue(TEXT("the other one stays"), Selection->IsSelected(Second.Get()));
	TestEqual(TEXT("and only it is left"), Selection->GetSelectedWidgets().Num(), 1);

	const int32 CountBeforeNoOp = BroadcastCount;
	Selection->DeselectWidget(First.Get());
	TestEqual(TEXT("deselecting what is not selected changes nothing"), BroadcastCount, CountBeforeNoOp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexFramingBoundsAreAlwaysAnsweredTest,
	"LGUI.Editor.Framing.NothingActiveStillProducesAPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexFramingBoundsAreAlwaysAnsweredTest::RunTest(const FString& Parameters)
{
	// Deliberately poisoned: the failure being pinned is bounds that were never written at all, and
	// a default-constructed FBoxSphereBounds would hide it by looking plausible.
	FBoxSphereBounds Bounds(FVector(12345.0, -777.0, 9999.0), FVector(4242.0), 5150.0f);
	const bool bAnyBounds = FLexUIPrefabEditor::AccumulateWidgetsBounds(TArray<ULexWidget*>(), Bounds);

	TestFalse(TEXT("an empty prefab contributes no bounds"), bAnyBounds);
	TestTrue(TEXT("and the out param is zeroed rather than left as it was found"),
		Bounds.Origin.IsNearlyZero() && Bounds.BoxExtent.IsNearlyZero() && FMath::IsNearlyZero(Bounds.SphereRadius));

	const FBoxSphereBounds Fallback = FLexUIPrefabEditor::MakeCanvasFramingBounds(FIntPoint(1920, 1080));
	TestTrue(TEXT("the fallback framing is finite"), FMath::IsFinite(Fallback.SphereRadius) && !Fallback.Origin.ContainsNaN());
	TestTrue(TEXT("and covers the canvas"), Fallback.SphereRadius > 0.0f
		&& FMath::IsNearlyEqual(Fallback.BoxExtent.Y, 960.0, 0.01)
		&& FMath::IsNearlyEqual(Fallback.BoxExtent.Z, 540.0, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexFramingBoundsUnionActiveWidgetsTest,
	"LGUI.Editor.Framing.ActiveWidgetsAreUnioned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexFramingBoundsUnionActiveWidgetsTest::RunTest(const FString& Parameters)
{
	using namespace LexEditorToolkitTestLocal;
	FScopedTestWorld TestWorld;

	TStrongObjectPtr<ULexWidget> Root(MakeWidget(TestWorld.World, TEXT("Root"), 800.0f, 600.0f));
	TStrongObjectPtr<ULexWidget> Child(MakeWidget(TestWorld.World, TEXT("Child"), 100.0f, 50.0f));
	Child->TrySetParent(Root.Get(), false);
	Child->SetAnchoredPosition(FVector2D(300.0f, 200.0f));

	FBoxSphereBounds Bounds;
	const TArray<ULexWidget*> Widgets = { Root.Get(), Child.Get() };
	TestTrue(TEXT("two active widgets contribute bounds"), FLexUIPrefabEditor::AccumulateWidgetsBounds(Widgets, Bounds));

	const FBox RootBox = FLexUIPrefabEditor::GetWidgetWorldBox(Root.Get());
	const FBox ChildBox = FLexUIPrefabEditor::GetWidgetWorldBox(Child.Get());
	const FBox Union = Bounds.GetBox();
	TestTrue(TEXT("the union holds the root"), Union.IsInsideOrOn(RootBox.Min) && Union.IsInsideOrOn(RootBox.Max));
	TestTrue(TEXT("the union holds the child"), Union.IsInsideOrOn(ChildBox.Min) && Union.IsInsideOrOn(ChildBox.Max));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexDesignerPreferencesArePerUserTest,
	"LGUI.Editor.DesignerSettings.PreferencesArePerUserNotPerAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexDesignerPreferencesArePerUserTest::RunTest(const FString& Parameters)
{
	// A grid size stored on the prefab dirties an asset nobody edited and travels to teammates in
	// the diff; stored in DefaultEditor.ini it is still one shared value for the whole project.
	TestEqual(TEXT("designer preferences live in the per-user ini"),
		ULexUIDesignerSettings::StaticClass()->ClassConfigName, FName(TEXT("EditorPerProjectUserSettings")));

	const ULexUIDesignerSettings* Settings = GetDefault<ULexUIDesignerSettings>();
	TestNotNull(TEXT("the settings object exists"), Settings);
	TestTrue(TEXT("and a grid size that can be snapped to"), Settings->GridSize >= 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexDesignerZoomRatioTest,
	"LGUI.Editor.DesignerZoom.OneToOneScalesZoomByTheRatio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexDesignerZoomRatioTest::RunTest(const FString& Parameters)
{
	// Half a pixel per unit now, one pixel per unit wanted: the view has to zoom IN, which is a
	// smaller ortho zoom. Inverting the ratio zooms the wrong way and still looks like it did something.
	TestEqual(TEXT("zooming in halves the ortho zoom"),
		FLexUIPrefabEditor::DesignerOrthoZoomFor(1000.0f, 2.0f, 1.0f), 500.0f, 0.01f);
	// 2 units per pixel already IS half a pixel per unit, so asking for half again is the no-op;
	// a quarter is the request that actually zooms out.
	TestEqual(TEXT("asking for the scale it already has changes nothing"),
		FLexUIPrefabEditor::DesignerOrthoZoomFor(1000.0f, 2.0f, 0.5f), 1000.0f, 0.01f);
	TestEqual(TEXT("zooming out doubles it"),
		FLexUIPrefabEditor::DesignerOrthoZoomFor(1000.0f, 2.0f, 0.25f), 2000.0f, 0.01f);
	TestEqual(TEXT("a viewport with no scale yet leaves the view alone"),
		FLexUIPrefabEditor::DesignerOrthoZoomFor(1000.0f, 0.0f, 1.0f), 1000.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexDesignerDesiredSizeTest,
	"LGUI.Editor.DesignerScreenSize.DesiredSizeFallsBackPerAxis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexDesignerDesiredSizeTest::RunTest(const FString& Parameters)
{
	const FIntPoint Current(1920, 1080);
	TestTrue(TEXT("a measured content size becomes the canvas"),
		FLexUIPrefabEditor::DesignerViewportSizeFromDesired(FVector2D(321.4, 200.6), Current) == FIntPoint(321, 201));
	// Zero is a measurement that failed, not a request for a canvas with no width.
	TestTrue(TEXT("an axis that measured nothing keeps the canvas it had"),
		FLexUIPrefabEditor::DesignerViewportSizeFromDesired(FVector2D(0.0, 200.0), Current) == FIntPoint(1920, 200));
	const double NotANumber = std::numeric_limits<double>::quiet_NaN();
	TestTrue(TEXT("and so does one that measured nonsense"),
		FLexUIPrefabEditor::DesignerViewportSizeFromDesired(FVector2D(NotANumber, NotANumber), Current) == Current);
	return true;
}

#endif
