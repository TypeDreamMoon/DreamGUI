// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "UObject/Package.h"

/*
 * Nine-slice: what a test can actually hold, and what it cannot.
 *
 * The remap itself lives in DreamUIRectBlock.ush and is verified on screen -- this suite runs
 * -nullrhi, so nothing here draws a pixel and a test claiming otherwise would be measuring its own
 * arithmetic. What IS testable on this side is the part with an opinion in it: how big the caps are
 * allowed to be, which is the only decision C++ makes before the numbers become memcpy.
 *
 * That decision matters because the shader's middle span is "quad minus both caps". Hand it caps
 * that together exceed the rect and the span goes NEGATIVE -- the two caps read past each other and
 * a small button wearing a big button's skin draws the frame inside out. There is no assert to
 * catch that: it is one subtraction, and the result is a look rather than a crash.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNineSliceMarginClampTest,
	"DreamGUI.RectBlock.NineSliceCapsShrinkTogetherRatherThanOverlapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNineSliceMarginClampTest::RunTest(const FString& Parameters)
{
	// The ordinary case: a rect with room for its margins gets them unchanged.
	{
		const FVector4f Margins = UDreamRectBlock::ResolveSliceMarginPixels(
			FMargin(8.0f, 6.0f, 8.0f, 6.0f), 200.0f, 100.0f);
		TestEqual(TEXT("the left cap is what was authored"), Margins.X, 8.0f);
		TestEqual(TEXT("the top cap is what was authored"), Margins.Y, 6.0f);
		TestEqual(TEXT("the right cap is what was authored"), Margins.Z, 8.0f);
		TestEqual(TEXT("the bottom cap is what was authored"), Margins.W, 6.0f);
	}

	// The case that matters. Caps of 40 + 40 on a rect 60 wide leave a middle of -20, and a shader
	// asked to stretch a negative span reads the two caps through each other.
	{
		const FVector4f Margins = UDreamRectBlock::ResolveSliceMarginPixels(
			FMargin(40.0f, 4.0f, 40.0f, 4.0f), 60.0f, 100.0f);
		TestTrue(TEXT("the caps together fit the rect"), Margins.X + Margins.Z <= 60.0f + KINDA_SMALL_NUMBER);
		TestEqual(TEXT("and they split it evenly, being equal"), Margins.X, 30.0f);
		TestEqual(TEXT("the axis with room is untouched"), Margins.Y, 4.0f);
	}

	// Together, not one at a time. Clamping the first to fit and taking the whole squeeze out of the
	// second is the easy wrong answer, and it turns an asymmetric frame lopsided as it shrinks: what
	// an author drew as one-third / two-thirds has to stay one-third / two-thirds.
	{
		const FVector4f Margins = UDreamRectBlock::ResolveSliceMarginPixels(
			FMargin(30.0f, 0.0f, 60.0f, 0.0f), 45.0f, 100.0f);
		TestEqual(TEXT("the caps fill the rect exactly"), Margins.X + Margins.Z, 45.0f);
		TestEqual(TEXT("and keep their ratio: a third"), Margins.X, 15.0f);
		TestEqual(TEXT("and two thirds"), Margins.Z, 30.0f);
	}

	// Both axes squeeze independently -- a rect can be short of room across and not down.
	{
		const FVector4f Margins = UDreamRectBlock::ResolveSliceMarginPixels(
			FMargin(40.0f, 40.0f, 40.0f, 40.0f), 40.0f, 200.0f);
		TestEqual(TEXT("the cramped axis shrank"), Margins.X, 20.0f);
		TestEqual(TEXT("and the roomy one did not"), Margins.Y, 40.0f);
	}

	// A negative margin is not a thing anyone means, and it would flip the cap inside out.
	{
		const FVector4f Margins = UDreamRectBlock::ResolveSliceMarginPixels(
			FMargin(-10.0f, -10.0f, 4.0f, 4.0f), 100.0f, 100.0f);
		TestEqual(TEXT("a negative left cap is no cap"), Margins.X, 0.0f);
		TestEqual(TEXT("a negative top cap is no cap"), Margins.Y, 0.0f);
	}

	// A degenerate rect must not produce a NaN through the divide: a widget is routinely zero-sized
	// for the frame between being made and being arranged, and one NaN in the data block is a
	// texture upload of garbage rather than a missing frame.
	{
		const FVector4f Margins = UDreamRectBlock::ResolveSliceMarginPixels(
			FMargin(8.0f), 0.0f, 0.0f);
		TestFalse(TEXT("no NaN across"), FMath::IsNaN(Margins.X));
		TestFalse(TEXT("no NaN down"), FMath::IsNaN(Margins.Y));
		TestEqual(TEXT("and nothing is claimed on a rect with no width"), Margins.X, 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNineSliceSpriteGateTest,
	"DreamGUI.RectBlock.AnAtlasSpriteIsNotSliced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNineSliceSpriteGateTest::RunTest(const FString& Parameters)
{
	using namespace DreamUI;

	// On a real widget, because a visual without one is not a state production ever reaches: the
	// setters mark the widget's batch dirty, and a bare NewObject rect block crashes in the first
	// one of them. (It did, the first time this test was written.)
	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	Tree->RootWidget = Realize(Tree, Node<UDreamRectBlock>("Face").Stretch());
	UDreamRectBlock* Rect = Tree->RootWidget != nullptr
		? Cast<UDreamRectBlock>(Tree->RootWidget->GetVisual()) : nullptr;
	if (!TestNotNull(TEXT("the rect block exists on a widget"), Rect))
	{
		return false;
	}

	// A rect block starts in SPRITE mode -- that is the default, and SkinFace is what switches it to
	// Texture when a brush holds a plain one. So the gate is already shut before anything is asked
	// to slice, which is the right way round: an unconfigured block cannot be sliced by accident.
	Rect->SetBodyTextureDrawMode(EDreamRectBlockTextureDrawMode::Box);
	TestNull(TEXT("a sprite offers no texture to measure a margin against"),
		Rect->GetBodySampledTexture());

	// A plain texture is measurable, so slicing is honoured. Through the setter, which self-heals
	// null to the library's white: a block that was never given a texture holds null until something
	// writes one, and a plain white fill has no edges to keep anyway.
	Rect->SetBodyTextureMode(EDreamRectBlockTextureMode::Texture);
	Rect->SetBodyTexture(nullptr);
	TestNotNull(TEXT("a plain texture is there to measure a margin against"),
		Rect->GetBodySampledTexture());

	// Back to a sprite, and the gate shuts again. A sprite's UV span is a SUB-RECT of its atlas and
	// the remap works in 0..1, so slicing one would walk the caps straight into the neighbouring
	// sprite -- a frame quietly built out of somebody else's art.
	Rect->SetBodyTextureMode(EDreamRectBlockTextureMode::Sprite);
	TestNull(TEXT("and a sprite is not measurable however it got there"),
		Rect->GetBodySampledTexture());
	// The property keeps what the author set -- switching back to a plain texture must restore the
	// slicing rather than having silently forgotten it.
	TestEqual(TEXT("the authored draw mode is remembered, not overwritten"),
		Rect->GetBodyTextureDrawMode(), EDreamRectBlockTextureDrawMode::Box);

	Tree->RootWidget->DestroyWidget();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
