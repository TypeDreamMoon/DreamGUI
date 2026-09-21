// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "UObject/Package.h"

/**
 * A subtree parented through SetParentBeforeRegister takes its place in the HIT ORDER, not only on
 * screen.
 *
 * UDreamBaseRaycaster sorts the hits it collects within a canvas by FlattenHierarchyIndex,
 * descending -- so the index is not bookkeeping, it is the answer to "which of these overlapping
 * widgets did the user click". The index is born -1 and is only recalculated when the hierarchy
 * ROOT is marked dirty, and that mark rides the attachment event, which SetParentBeforeRegister
 * deliberately does not raise. Every node of such a subtree therefore kept -1, and -1 loses to
 * every widget that has a real index.
 *
 * It still drew, which is what made this so hard to see: the render-canvas half of the same hole
 * was patched earlier (RefreshRenderCanvasFromParentChain), so the subtree was visible and simply
 * un-clickable. Measured on the modal dialog: scrim, panel and both buttons read -1 while the page
 * behind them ran 0..215, so every click was awarded to the page.
 *
 * The shape below is the modal subsystem's exactly: a page attached first, then a layer attached
 * over it the same way ShowNow attaches its scrim.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRegisteredSubtreeTakesItsPlaceInHitOrder,
	"DreamGUI.WidgetTree.ARegisteredSubtreeTakesItsPlaceInTheHitOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRegisteredSubtreeTakesItsPlaceInHitOrder::RunTest(const FString& Parameters)
{
	UDreamWidget* ScreenRoot = NewObject<UDreamWidget>(GetTransientPackage());
	ScreenRoot->SetDisplayName(TEXT("ScreenRoot"));
	TDreamTestControl<UDreamWidget> Owned(ScreenRoot);

	// The page, with a child, so the page occupies more than one index and the layer's index has to
	// clear the whole of it rather than merely beating the page's own.
	UDreamWidget* Page = NewObject<UDreamWidget>(ScreenRoot);
	Page->SetDisplayName(TEXT("Page"));
	Page->SetParentBeforeRegister(ScreenRoot);
	UDreamWidget* PageChild = NewObject<UDreamWidget>(ScreenRoot);
	PageChild->SetDisplayName(TEXT("PageChild"));
	PageChild->SetParentBeforeRegister(Page);
	RegisterDreamWidgetHierarchy(Page);

	const int32 PageIndex = Page->GetFlattenHierarchyIndex();
	const int32 PageChildIndex = PageChild->GetFlattenHierarchyIndex();
	if (!TestTrue(TEXT("the page was indexed"), PageIndex >= 0) ||
		!TestTrue(TEXT("and so was its child"), PageChildIndex > PageIndex))
	{
		return false;
	}

	// The layer, attached AFTER the page was already indexed -- which is the case that used to be
	// missed, because there is no attachment event on this path to carry the invalidation.
	UDreamWidget* Layer = NewObject<UDreamWidget>(ScreenRoot);
	Layer->SetDisplayName(TEXT("Layer"));
	Layer->SetParentBeforeRegister(ScreenRoot);
	UDreamWidget* LayerButton = NewObject<UDreamWidget>(ScreenRoot);
	LayerButton->SetDisplayName(TEXT("LayerButton"));
	LayerButton->SetParentBeforeRegister(Layer);
	RegisterDreamWidgetHierarchy(Layer);

	TestTrue(TEXT("the layer was indexed too, not left at -1"),
		Layer->GetFlattenHierarchyIndex() >= 0);
	TestTrue(TEXT("its button as well"),
		LayerButton->GetFlattenHierarchyIndex() >= 0);

	// THE claim: everything in the later layer outranks everything in the page, which is what makes
	// a dialog take the click instead of the screen it covers.
	TestTrue(TEXT("the layer sorts above the page"),
		Layer->GetFlattenHierarchyIndex() > PageIndex);
	TestTrue(TEXT("-- above the whole of it, children included"),
		Layer->GetFlattenHierarchyIndex() > PageChildIndex);
	TestTrue(TEXT("and the layer's own child outranks the layer"),
		LayerButton->GetFlattenHierarchyIndex() > Layer->GetFlattenHierarchyIndex());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
