// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Localization/DreamUITextLocator.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"

// DreamLoc's in-context pick asks DreamGUI which text is at a point of a viewport: the viewport's pick ray, and the
// front-most DreamText it crosses. These pin that question and the key it answers with.
namespace DreamUITextLocatorTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** A ray straight down -X through the world point (0, Y, Z), which is where widget rects live. */
	void RayAt(float Y, float Z, FVector& OutStart, FVector& OutEnd)
	{
		OutStart = FVector(1000.0f, Y, Z);
		OutEnd = FVector(-1000.0f, Y, Z);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLocatorFindsTheTextTest,
	"DreamGUI.Editor.Localization.InContextFindsTheDreamTextUnderAPointAndItsKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextLocatorFindsTheTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextLocatorTestLocal;
	FScopedTestWorld TestWorld;
	UDreamWidget* Panel = NewObject<UDreamWidget>(TestWorld.World);
	Panel->SetWidth(800.0f);
	Panel->SetHeight(600.0f);
	UDreamWidget* Label = NewObject<UDreamWidget>(TestWorld.World);
	Label->SetWidth(200.0f);
	Label->SetHeight(50.0f);
	UDreamText* Text = Label->CreateNewVisual<UDreamText>();
	if (!TestNotNull(TEXT("a text on the label"), Text))
	{
		return false;
	}
	Text->SetText(NSLOCTEXT("DreamGUITestMenu", "Hello", "你好"));
	const TArray<UDreamWidget*> Widgets = { Panel, Label };

	FVector Start;
	FVector End;
	RayAt(0.0f, 0.0f, Start, End);
	TestTrue(TEXT("the text the ray crosses, not the panel under it"),
		DreamUITextLocator::FindTextAlongRay(TestWorld.World, Widgets, Start, End) == Text);
	// Centre pivot: the text spans [-100, 100] x [-25, 25]; the panel goes on past it.
	RayAt(150.0f, 0.0f, Start, End);
	TestNull(TEXT("a point of the panel beside the text: no text"), DreamUITextLocator::FindTextAlongRay(TestWorld.World, Widgets, Start, End));

	FString Namespace;
	FString Key;
	FString WhyNot;
	TestTrue(TEXT("a keyed text"), DreamUITextLocator::Describe(Text, Namespace, Key, WhyNot));
	TestEqual(TEXT("its namespace and key"), Namespace + TEXT(" / ") + Key, TEXT("DreamGUITestMenu / Hello"));
	Text->SetText(FText::ChangeKey(TEXT("Menu [0A1B2C3D4E5F]"), TEXT("Quit"), FText::FromString(TEXT("退出"))));
	DreamUITextLocator::Describe(Text, Namespace, Key, WhyNot);
	TestEqual(TEXT("an asset's: the package's part of the namespace stripped"), Namespace + TEXT(" / ") + Key, TEXT("Menu / Quit"));
	Text->SetText(FText::FromString(TEXT("plain")));
	TestFalse(TEXT("a text set from a string"), DreamUITextLocator::Describe(Text, Namespace, Key, WhyNot));
	TestTrue(TEXT("says why it has no key"), WhyNot.Contains(TEXT("not localizable")));

	FVector Corners[4];
	DreamUITextLocator::GetWorldCorners(Label, Corners);
	const FBox Box(Corners, 4);
	TestEqual(TEXT("its corners in the world: across"), FString::Printf(TEXT("%.0f..%.0f"), Box.Min.Y, Box.Max.Y), TEXT("-100..100"));
	TestEqual(TEXT("and up"), FString::Printf(TEXT("%.0f..%.0f"), Box.Min.Z, Box.Max.Z), TEXT("-25..25"));

	TArray<UDreamWidget*> All;
	DreamUITextLocator::CollectAll(Panel, All);
	TestEqual(TEXT("a widget alone: itself"), All.Num(), 1);
	return true;
}

#endif
