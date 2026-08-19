// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "PrefabEditor/LexUIPrefabEditorViewportClient.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexLayoutSelfAspectRatio.h"
#include "Core/Components/LexPanelSlot.h"
#include "Engine/World.h"

// The designer suppressed every resize handle for every child of any panel, on the reasoning that
// a panel owns its children's geometry. A CanvasPanel does not: it writes a child's size only when
// that child's slot says Auto Size, so the one panel whose children you resize by hand was the one
// whose handles were taken away -- while Move still worked, which reads as a broken handle rather
// than a deliberate rule. The question is per axis, and every container already answers it.
namespace LexDesignerHandlePolicyTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeWidget(UObject* Outer, const TCHAR* Name, float Width, float Height)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(Outer);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(Width);
		Widget->SetHeight(Height);
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexHandlesSuppressedUnderArrangingPanelTest,
	"LGUI.Editor.DesignerHandles.ArrangingPanelOwnsBothAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexHandlesSuppressedUnderArrangingPanelTest::RunTest(const FString& Parameters)
{
	using namespace LexDesignerHandlePolicyTestLocal;
	FScopedTestWorld TestWorld;

	ULexWidget* Root = MakeWidget(TestWorld.World, TEXT("Root"), 800.0f, 600.0f);
	Root->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	ULexWidget* Child = MakeWidget(Root, TEXT("Child"), 100.0f, 50.0f);
	Child->TrySetParent(Root, false);

	const FLexLayoutControlAnchorData Control = FLexUIPrefabEditorViewportClient::GetEffectiveLayoutControl(Child);
	TestTrue(TEXT("a vertical box decides width"), Control.bCanControlHorizontalSize);
	TestTrue(TEXT("and height"), Control.bCanControlVerticalSize);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexHandlesFreeUnderCanvasPanelTest,
	"LGUI.Editor.DesignerHandles.CanvasPanelLeavesSizeAloneUnlessAutoSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexHandlesFreeUnderCanvasPanelTest::RunTest(const FString& Parameters)
{
	using namespace LexDesignerHandlePolicyTestLocal;
	FScopedTestWorld TestWorld;

	ULexWidget* Root = MakeWidget(TestWorld.World, TEXT("Root"), 800.0f, 600.0f);
	ULexLayoutContainerCanvasPanel* Canvas = Root->CreateNewLayoutContainer<ULexLayoutContainerCanvasPanel>();
	if (!TestNotNull(TEXT("canvas panel"), (UObject*)Canvas))return false;
	ULexWidget* Child = MakeWidget(Root, TEXT("Child"), 100.0f, 50.0f);
	Child->TrySetParent(Root, false);

	// This is the case the blanket rule got wrong: a plain canvas child's size is the author's.
	const FLexLayoutControlAnchorData Free = FLexUIPrefabEditorViewportClient::GetEffectiveLayoutControl(Child);
	TestFalse(TEXT("width stays the author's"), Free.bCanControlHorizontalSize);
	TestFalse(TEXT("height stays the author's"), Free.bCanControlVerticalSize);

	ULexPanelSlot* Slot = Child->GetPanelSlot();
	if (!TestNotNull(TEXT("the child has a slot"), (UObject*)Slot))return false;
	Slot->SetAutoSize(true);
	const FLexLayoutControlAnchorData Auto = FLexUIPrefabEditorViewportClient::GetEffectiveLayoutControl(Child);
	TestTrue(TEXT("auto size hands width to the panel"), Auto.bCanControlHorizontalSize);
	TestTrue(TEXT("and height"), Auto.bCanControlVerticalSize);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexHandlesFreeWhenIgnoringLayoutTest,
	"LGUI.Editor.DesignerHandles.IgnoreLayoutTakesTheAxesBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexHandlesFreeWhenIgnoringLayoutTest::RunTest(const FString& Parameters)
{
	using namespace LexDesignerHandlePolicyTestLocal;
	FScopedTestWorld TestWorld;

	ULexWidget* Root = MakeWidget(TestWorld.World, TEXT("Root"), 800.0f, 600.0f);
	Root->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	ULexWidget* Child = MakeWidget(Root, TEXT("Child"), 100.0f, 50.0f);
	Child->TrySetParent(Root, false);

	TestTrue(TEXT("arranged to begin with"), FLexUIPrefabEditorViewportClient::GetEffectiveLayoutControl(Child).bCanControlHorizontalSize);
	Child->SetIgnoreLayout(true);
	const FLexLayoutControlAnchorData Control = FLexUIPrefabEditorViewportClient::GetEffectiveLayoutControl(Child);
	TestFalse(TEXT("an ignored child owns its width again"), Control.bCanControlHorizontalSize);
	TestFalse(TEXT("and its height"), Control.bCanControlVerticalSize);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexHandlesRespectLayoutSelfTest,
	"LGUI.Editor.DesignerHandles.LayoutSelfClaimsItsOwnAxis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexHandlesRespectLayoutSelfTest::RunTest(const FString& Parameters)
{
	using namespace LexDesignerHandlePolicyTestLocal;
	FScopedTestWorld TestWorld;

	// No parent container at all, so anything claimed here came from the widget's own layout-self.
	// A blanket "does the parent have a container" test could never see this.
	ULexWidget* Root = MakeWidget(TestWorld.World, TEXT("Root"), 800.0f, 600.0f);
	ULexWidget* Child = MakeWidget(Root, TEXT("Child"), 100.0f, 50.0f);
	Child->TrySetParent(Root, false);
	TestFalse(TEXT("nothing arranges it yet"), FLexUIPrefabEditorViewportClient::GetEffectiveLayoutControl(Child).bCanControlVerticalSize);

	ULexLayoutSelfAspectRatio* Aspect = Child->CreateNewLayoutSelf<ULexLayoutSelfAspectRatio>();
	if (!TestNotNull(TEXT("aspect ratio layout self"), (UObject*)Aspect))return false;
	Aspect->SetAspectRatio(1.0f);
	Aspect->SetAspectRatioType(ELexLayoutAspectRatioType::HeightControlWidth);
	const FLexLayoutControlAnchorData Control = FLexUIPrefabEditorViewportClient::GetEffectiveLayoutControl(Child);
	TestTrue(TEXT("height-controls-width takes the width"), Control.bCanControlHorizontalSize);
	return true;
}

#endif
