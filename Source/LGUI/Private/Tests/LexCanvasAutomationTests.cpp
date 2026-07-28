#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexCanvasHierarchyOrderTest,
	"LGUI.Canvas.HierarchyOrderInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexCanvasHierarchyOrderTest::RunTest(const FString& Parameters)
{
	ULexWidget* Parent = NewObject<ULexWidget>();
	ULexWidget* Back = NewObject<ULexWidget>(Parent);
	ULexWidget* Front = NewObject<ULexWidget>(Parent);
	TestNotNull(TEXT("Parent widget created"), Parent);
	TestNotNull(TEXT("Back widget created"), Back);
	TestNotNull(TEXT("Front widget created"), Front);
	if (!Parent || !Back || !Front)
	{
		return false;
	}

	Back->SiblingIndex = 0;
	Front->SiblingIndex = 1;
	Parent->Children = { Front, Back };
	Parent->bNeedSortUIChildren = true;

	const TArray<ULexWidget*>& SortedChildren = Parent->GetChildren();
	TestEqual(TEXT("GetChildren returns the back widget first"), SortedChildren[0], Back);
	TestEqual(TEXT("GetChildren returns the front widget last"), SortedChildren[1], Front);

	ULexCanvas* Canvas = NewObject<ULexCanvas>();
	TestNotNull(TEXT("Canvas created"), Canvas);
	if (!Canvas)
	{
		return false;
	}

	Canvas->bNeedToGenerateWidgetList = false;
	Canvas->bShouldRebuildDrawCall = false;
	Canvas->bCanTickUpdate = false;
	Canvas->MarkCanvasHierarchyChanged();

	TestTrue(TEXT("Hierarchy changes invalidate the cached widget list"), Canvas->bNeedToGenerateWidgetList);
	TestTrue(TEXT("Hierarchy changes rebuild draw calls"), Canvas->bShouldRebuildDrawCall);
	TestTrue(TEXT("Hierarchy changes wake canvas updates"), Canvas->bCanTickUpdate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexCanvasProjectionIsFindableTest,
	"LGUI.Canvas.TheProjectionControlsAreNotBuriedInAdvancedDisplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexCanvasProjectionIsFindableTest::RunTest(const FString& Parameters)
{
	// ProjectionType and FieldOfView define the projection that every widget-level Perspective
	// scope is built against: the eye it re-aims geometry at is derived from them, and an
	// orthographic canvas disables the feature entirely. While they sat behind the Advanced twirl
	// an author had no reachable way to calibrate or diagnose perspective. This asserts the
	// reflection flags rather than any behaviour, because behaviour is exactly what stays green
	// when a property becomes unreachable from the panel.
	auto CheckReachable = [this](const TCHAR* PropertyName)
	{
		const FProperty* Property = ULexCanvas::StaticClass()->FindPropertyByName(FName(PropertyName));
		if (!TestNotNull(*FString::Printf(TEXT("%s is a reflected property"), PropertyName), Property))
		{
			return;
		}
		TestTrue(*FString::Printf(TEXT("%s is editable"), PropertyName),
			Property->HasAnyPropertyFlags(CPF_Edit));
		TestFalse(*FString::Printf(TEXT("%s is not hidden behind Advanced"), PropertyName),
			Property->HasAnyPropertyFlags(CPF_AdvancedDisplay));
	};

	CheckReachable(TEXT("ProjectionType"));
	CheckReachable(TEXT("FieldOfView"));

	// The clip planes stay advanced -- they are genuine tuning, not calibration, and this pins the
	// distinction so the change above is not read as "un-advance everything in the category".
	const FProperty* NearClip = ULexCanvas::StaticClass()->FindPropertyByName(TEXT("NearClipPlane"));
	if (TestNotNull(TEXT("NearClipPlane is a reflected property"), NearClip))
	{
		TestTrue(TEXT("NearClipPlane stays behind Advanced"),
			NearClip->HasAnyPropertyFlags(CPF_AdvancedDisplay));
	}
	return true;
}

#endif
