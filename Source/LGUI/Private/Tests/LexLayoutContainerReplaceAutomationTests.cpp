// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"

/*
 * What the Hierarchy's "Replace With..." rests on. In UMG the entry swaps one panel WIDGET for
 * another, so it has to carry names, slots and children across and warns that some of it will be
 * lost. In this fork the panel is an instanced ULexLayoutContainer hanging off the widget, so the
 * command is a single subobject swap and the widget is not touched at all -- which is only worth
 * claiming in a menu tooltip if it is actually true. These pin that, and pin the one case where
 * the swap must refuse rather than silently drop children.
 */

namespace LexLayoutContainerReplaceTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeWidget(UWorld* World, ULexWidget* Parent, const TCHAR* Name, float W, float H)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexLayoutContainerReplaceKeepsWidgetTest,
	"LGUI.Layout.Replace.SwapKeepsTheWidgetAndItsChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexLayoutContainerReplaceKeepsWidgetTest::RunTest(const FString& Parameters)
{
	using namespace LexLayoutContainerReplaceTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 400.0f);
	ULexWidget* Panel = MakeWidget(TestWorld.World, Root, TEXT("Panel"), 300.0f, 300.0f);
	ULexWidget* Sibling = MakeWidget(TestWorld.World, Root, TEXT("Sibling"), 50.0f, 50.0f);
	TArray<ULexWidget*> ExpectedChildren;
	for (int32 i = 0; i < 3; i++)
	{
		ExpectedChildren.Add(MakeWidget(TestWorld.World, Panel, *FString::Printf(TEXT("Child%d"), i), 80.0f, 40.0f));
	}
	Root->OnRegister();

	ULexLayoutContainer* Before = Panel->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	if (!TestNotNull(TEXT("Vertical box created"), Before))return false;
	// The designer's per-child slot settings are the thing a "nothing else changes" claim is really
	// about, so give one a value that is nobody's default and follow it across the swap.
	ULexPanelSlot* SlotBefore = ExpectedChildren[1]->GetPanelSlot();
	if (!TestNotNull(TEXT("Middle child has a panel slot"), SlotBefore))return false;
	SlotBefore->SetPadding(FMargin(3.0f, 7.0f, 11.0f, 13.0f));

	const FString NameBefore = Panel->GetDisplayName();
	const int32 SiblingIndexBefore = Root->GetChildren().IndexOfByKey(Panel);

	ULexLayoutContainer* After = Panel->CreateNewLayoutContainer<ULexLayoutContainerHorizontalBox>();
	if (!TestNotNull(TEXT("Horizontal box created"), After))return false;

	TestTrue(TEXT("The container is a different object of the requested class"),
		After != Before && After->IsA<ULexLayoutContainerHorizontalBox>());
	TestTrue(TEXT("The new container is registered"), After->IsRegistered());
	TestFalse(TEXT("The replaced container is unregistered"), Before->IsRegistered());

	TestEqual(TEXT("The widget keeps its display name"), Panel->GetDisplayName(), NameBefore);
	TestTrue(TEXT("The widget keeps its parent"), Panel->GetParent() == Root);
	TestEqual(TEXT("The widget keeps its sibling index"), Root->GetChildren().IndexOfByKey(Panel), SiblingIndexBefore);
	TestTrue(TEXT("The sibling is untouched"), Root->GetChildren().Contains(Sibling));

	if (TestEqual(TEXT("Child count is unchanged"), Panel->GetChildren().Num(), ExpectedChildren.Num()))
	{
		for (int32 i = 0; i < ExpectedChildren.Num(); i++)
		{
			TestTrue(*FString::Printf(TEXT("Child %d is the same object in the same order"), i),
				Panel->GetChildren()[i] == ExpectedChildren[i]);
		}
	}
	// Panel-to-panel reuses the existing slot rather than minting a fresh one, so authored padding
	// survives. UMG has to warn about losing this; here there is nothing to warn about.
	TestTrue(TEXT("The middle child keeps its slot object"),
		ExpectedChildren[1]->GetPanelSlot() == SlotBefore);
	TestTrue(TEXT("The authored padding survives the swap"),
		ExpectedChildren[1]->GetPanelSlot()->Padding == FMargin(3.0f, 7.0f, 11.0f, 13.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexLayoutContainerReplaceRefusesOverfullTest,
	"LGUI.Layout.Replace.OverfullSingleChildPanelIsRefusedNotTruncated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexLayoutContainerReplaceRefusesOverfullTest::RunTest(const FString& Parameters)
{
	using namespace LexLayoutContainerReplaceTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"), 300.0f, 300.0f);
	for (int32 i = 0; i < 3; i++)
	{
		MakeWidget(TestWorld.World, Panel, *FString::Printf(TEXT("Child%d"), i), 80.0f, 40.0f);
	}
	Panel->OnRegister();
	ULexLayoutContainer* Before = Panel->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	if (!TestNotNull(TEXT("Vertical box created"), Before))return false;

	// A size box takes one child. Three do not fit, and dropping two of them to make the swap
	// succeed would be a data loss the designer never asked for -- so the swap has to fail whole.
	ULexLayoutContainer* Refused = Panel->CreateNewLayoutContainer<ULexLayoutContainerSizeBox>();
	TestNull(TEXT("The swap is refused"), Refused);
	TestTrue(TEXT("The original container is still in place"), Panel->GetLayoutContainer() == Before);
	TestTrue(TEXT("The original container is still registered"), Before->IsRegistered());
	TestEqual(TEXT("No child was dropped"), Panel->GetChildren().Num(), 3);

	// One child does fit, and then the same swap must go through.
	Panel->GetChildren()[2]->TrySetParent(nullptr, false);
	Panel->GetChildren()[1]->TrySetParent(nullptr, false);
	ULexLayoutContainer* Accepted = Panel->CreateNewLayoutContainer<ULexLayoutContainerSizeBox>();
	TestNotNull(TEXT("With one child the size box is accepted"), Accepted);
	return true;
}

#endif
