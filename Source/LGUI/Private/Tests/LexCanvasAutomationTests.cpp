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

#endif
