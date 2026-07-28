// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Core/Components/LexWidget.h"
#include "Event/LexPointerEventData.h"
#include "Interaction/UIScrollView.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAnchoredScrollViewTest,
	"LGUI.Interaction.ScrollView.AnchoredProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAnchoredScrollViewTest::RunTest(const FString& Parameters)
{
	ULexWidget* Viewport = NewObject<ULexWidget>(GetTransientPackage());
	Viewport->SetWidth(320.0f);
	Viewport->SetHeight(200.0f);
	Viewport->SetPivot(FVector2D(0.5f));

	ULexWidget* Content = NewObject<ULexWidget>(Viewport);
	Content->SetParent(Viewport);
	Content->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0f, 1.0f), FVector2D(0.0f, 1.0f), false, false);
	Content->SetPivot(FVector2D(0.0f, 1.0f));
	Content->SetWidth(320.0f);
	Content->SetHeight(600.0f);
	Content->SetAnchoredPosition(FVector2D::ZeroVector);

	UUIScrollView* ScrollView = Viewport->AddComponent<UUIScrollView>();
	ScrollView->SetContent(Content);
	ScrollView->SetHorizontal(false);
	ScrollView->SetVertical(true);
	ScrollView->SetCanScrollInSmallSize(false);
	ScrollView->SetCoordinateMode(ELexScrollCoordinateMode::AnchoredPosition);
	ScrollView->SetKeepProgress(true);
	ScrollView->SetWheelProgressStep(0.125f);
	ScrollView->SetScrollProgress(FVector2D::ZeroVector);

	TestEqual(TEXT("Top progress uses authored anchored origin"), Content->GetVerticalAnchoredPosition(), 0.0f);
	TestEqual(TEXT("Vertical overflow is represented in anchored coordinates"), ScrollView->GetVerticalRange(), FVector2D(0.0f, 400.0f));
	TestFalse(TEXT("Null pointer events are ignored"), ScrollView->OnPointerScroll_Implementation(nullptr));

	ULexPointerEventData* ScrollEvent = NewObject<ULexPointerEventData>();
	ScrollEvent->EnterWidget = Content;
	ScrollEvent->ScrollAxisValue = FVector2D(0.0f, -1.0f);
	ScrollView->OnPointerScroll_Implementation(ScrollEvent);
	TestEqual(TEXT("Wheel advances normalized progress"), ScrollView->GetScrollProgress().Y, 0.125);
	TestEqual(TEXT("Wheel moves anchored content by overflow progress"), Content->GetVerticalAnchoredPosition(), 50.0f);

	Content->SetHeight(1000.0f);
	ScrollView->RectRangeChanged();
	TestEqual(TEXT("Range refresh preserves progress"), ScrollView->GetScrollProgress().Y, 0.125);
	TestEqual(TEXT("Resized content reapplies anchored progress"), Content->GetVerticalAnchoredPosition(), 100.0f);
	return true;
}

#endif
