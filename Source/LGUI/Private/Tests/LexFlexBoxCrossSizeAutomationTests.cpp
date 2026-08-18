// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Engine/World.h"

/*
 * The cross size handed to each child was the LINE's cross size, and a line was only ever as deep as its
 * tallest child - LineData.TotalPreferred[SecondaryAxis] is a running max over the children. The one term
 * that could grow it to the container, SecondaryStretchedExtraSize, is non-zero only when
 * SecondaryAlignment (align-content, defaulting to Start) is Stretch.
 *
 * So SecondaryLineAlignment (align-items) Stretch, whose whole description is "expand size to fill all
 * area", stretched children to their tallest sibling and looked like it did nothing at all until a second,
 * unrelated property was also set to Stretch. And every child shrank whenever the tallest one did.
 *
 * CSS is unambiguous here: a single-line flex container's line cross size IS the container's inner cross
 * size. align-content distributes *lines*, and one line has nothing to distribute.
 */

namespace LexFlexBoxCrossSizeTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** A child that opts into being stretched on the cross axis: PreferredHeight must be enabled. */
	static ULexWidget* MakeStretchableChild(ULexWidget* Parent, float PreferredHeight)
	{
		ULexWidget* Child = NewObject<ULexWidget>(Parent);
		Child->SetWidth(40.0f);
		Child->SetHeight(PreferredHeight);
		if (!Child->TrySetParent(Parent, false))
		{
			return nullptr;
		}
		ULexLayoutSelfFlexBox* Self = Child->CreateNewLayoutSelf<ULexLayoutSelfFlexBox>();
		if (!Self)
		{
			return nullptr;
		}
		Self->SetPreferredHeight(FLexLayoutSize(ELexLayoutSizeType::Fixed, PreferredHeight));
		return Child;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexFlexBoxStretchFillsContainerCrossAxisTest,
	"LGUI.Layout.FlexBoxCrossSize.StretchFillsContainerNotTallestSibling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexFlexBoxStretchFillsContainerCrossAxisTest::RunTest(const FString& Parameters)
{
	using namespace LexFlexBoxCrossSizeTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	ULexWidget* Root = NewObject<ULexWidget>(TestWorld.World);
	Root->SetWidth(400.0f);
	Root->SetHeight(200.0f);
	ULexWidget* Short = MakeStretchableChild(Root, 50.0f);
	ULexWidget* Tall = MakeStretchableChild(Root, 80.0f);
	ULexLayoutContainerFlexBox* Flex = Root->CreateNewLayoutContainer<ULexLayoutContainerFlexBox>();
	if (!TestNotNull(TEXT("Short child"), Short) || !TestNotNull(TEXT("Tall child"), Tall)
		|| !TestNotNull(TEXT("FlexBox"), Flex))
	{
		return false;
	}
	// align-items: stretch. align-content (SecondaryAlignment) is deliberately left at its Start default -
	// that is exactly the combination that used to do nothing.
	Flex->SetSecondaryLineAlignment(ELexLayoutFlexBoxSecondaryAxisLineAlignment::Stretch);
	Root->OnRegister();
	Short->OnRegister();
	Tall->OnRegister();

	ULexWidget::MarkLayoutForRebuild(Root);
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);

	TestEqual(TEXT("align-content is still at its default"),
		Flex->GetSecondaryAlignment(), ELexLayoutFlexBoxSecondaryAxisAlignment::Start);
	TestTrue(TEXT("The short child stretches to the container, not to its tallest sibling"),
		FMath::IsNearlyEqual(Short->GetHeight(), 200.0f, 0.01f));
	TestTrue(TEXT("The tall child stretches to the container too"),
		FMath::IsNearlyEqual(Tall->GetHeight(), 200.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexFlexBoxStretchTracksContainerResizeTest,
	"LGUI.Layout.FlexBoxCrossSize.StretchTracksContainerResize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexFlexBoxStretchTracksContainerResizeTest::RunTest(const FString& Parameters)
{
	using namespace LexFlexBoxCrossSizeTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	ULexWidget* Root = NewObject<ULexWidget>(TestWorld.World);
	Root->SetWidth(400.0f);
	Root->SetHeight(200.0f);
	ULexWidget* Child = MakeStretchableChild(Root, 50.0f);
	ULexLayoutContainerFlexBox* Flex = Root->CreateNewLayoutContainer<ULexLayoutContainerFlexBox>();
	if (!TestNotNull(TEXT("Child"), Child) || !TestNotNull(TEXT("FlexBox"), Flex))
	{
		return false;
	}
	Flex->SetSecondaryLineAlignment(ELexLayoutFlexBoxSecondaryAxisLineAlignment::Stretch);
	Root->OnRegister();
	Child->OnRegister();

	ULexWidget::MarkLayoutForRebuild(Root);
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);
	TestTrue(TEXT("Stretched to the container"), FMath::IsNearlyEqual(Child->GetHeight(), 200.0f, 0.01f));

	// The cross size follows the container rather than the content, so a resize has to carry through.
	Root->SetHeight(120.0f);
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);
	TestTrue(TEXT("Stretched child follows the container shrinking"),
		FMath::IsNearlyEqual(Child->GetHeight(), 120.0f, 0.01f));

	Root->SetHeight(310.0f);
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);
	TestTrue(TEXT("Stretched child follows the container growing"),
		FMath::IsNearlyEqual(Child->GetHeight(), 310.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexFlexBoxNonStretchKeepsPreferredCrossSizeTest,
	"LGUI.Layout.FlexBoxCrossSize.NonStretchKeepsPreferredCrossSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexFlexBoxNonStretchKeepsPreferredCrossSizeTest::RunTest(const FString& Parameters)
{
	using namespace LexFlexBoxCrossSizeTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	// The half that must not regress: growing the line must not resize children that did not ask to
	// stretch. Only their alignment within the line changes.
	ULexWidget* Root = NewObject<ULexWidget>(TestWorld.World);
	Root->SetWidth(400.0f);
	Root->SetHeight(200.0f);
	ULexWidget* Child = MakeStretchableChild(Root, 50.0f);
	ULexLayoutContainerFlexBox* Flex = Root->CreateNewLayoutContainer<ULexLayoutContainerFlexBox>();
	if (!TestNotNull(TEXT("Child"), Child) || !TestNotNull(TEXT("FlexBox"), Flex))
	{
		return false;
	}
	Flex->SetSecondaryLineAlignment(ELexLayoutFlexBoxSecondaryAxisLineAlignment::Center);
	Root->OnRegister();
	Child->OnRegister();

	ULexWidget::MarkLayoutForRebuild(Root);
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);

	TestTrue(TEXT("A centred child keeps its preferred cross size"),
		FMath::IsNearlyEqual(Child->GetHeight(), 50.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

#endif
