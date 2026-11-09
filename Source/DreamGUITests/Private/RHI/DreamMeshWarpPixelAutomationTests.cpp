// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Extensions/DreamMaskTransition.h"
#include "Extensions/DreamPath.h"
#include "MeshModifier/DreamMeshModifierJelly.h"
#include "MeshModifier/DreamMeshModifierTextOnPath.h"
#include "MeshModifier/DreamMeshModifierWarp.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"

/*
 * Warps, jellies, text on paths and mask transitions, drawn: a picture of each held to its golden image, the pixels of a
 * transition held to its hit test, and a cross-fade's two halves held to each other.
 */
namespace DreamMeshWarpPixelTestLocal
{
	using namespace DreamGalleryStage;

	/** A checker block, its squares showing how the mesh bends inside, with a warp of InStyle on it. */
	UDreamMeshModifierWarp* AddWarpedChecker(FGalleryStage& InStage, UTexture2D* InChecker, const TCHAR* InName, FVector2D InPosition, EDreamWarpStyle InStyle, float InBend)
	{
		UDreamWidget* Block = InStage.AddBlock(InName, FVector2D(72.0, 34.0), InPosition, FColor::White, InChecker);
		UDreamMeshModifierWarp* Warp = Block->AddComponent<UDreamMeshModifierWarp>();
		Warp->SetStyle(InStyle);
		Warp->SetBend(InBend);
		Warp->SetCellLength(4.0f);
		return Warp;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryWarpStylesTest,
	"DreamGUI.RHI.MeshWarp.StylesMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryWarpStylesTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpPixelTestLocal;
	FStageRef Stage = BeginStage(*this, FIntPoint(384, 320));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Checker = Stage->MakeChecker(64, 8, FColor(235, 235, 235, 255), FColor(70, 110, 190, 255));
	// Fifteen styles in the editors' order, row by row from the top left, and a moved lattice on a rect block last.
	for (int32 Index = 0; Index < 15; ++Index)
	{
		const int32 Column = Index % 4;
		const int32 Row = Index / 4;
		const FVector2D Position(-144.0 + Column * 96.0, 120.0 - Row * 80.0);
		const EDreamWarpStyle Style = static_cast<EDreamWarpStyle>(Index + 1);
		AddWarpedChecker(*Stage, Checker, *StaticEnum<EDreamWarpStyle>()->GetNameStringByValue(Index + 1), Position, Style, Style == EDreamWarpStyle::Fisheye ? 90.0f : 50.0f);
	}
	UDreamRectBlock* Card = Stage->AddRectBlock(TEXT("Lattice"), FVector2D(72.0, 34.0), FVector2D(144.0, -120.0), FColor(240, 170, 70, 255), 10.0f);
	UDreamMeshModifierWarp* Lattice = Card->GetWidget()->AddComponent<UDreamMeshModifierWarp>();
	Lattice->SetMode(EDreamWarpMode::Lattice);
	Lattice->SetLatticeSize(3, 3);
	Lattice->SetLatticePoint(1, 0, FVector2D(0.5, -0.6));
	Lattice->SetLatticePoint(2, 2, FVector2D(1.25, 1.2));
	Lattice->SetCellLength(4.0f);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Warp_Styles"), 20000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryWarpTextTest,
	"DreamGUI.RHI.MeshWarp.TextWarpsMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryWarpTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpPixelTestLocal;
	FStageRef Stage = BeginStage(*this, FIntPoint(384, 256));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	struct FCase { const TCHAR* Text; EDreamWarpStyle Style; float Bend; FVector2D Position; };
	const FCase Cases[] = {
		{ TEXT("ARC"), EDreamWarpStyle::Arc, 60.0f, FVector2D(-100.0, 80.0) },
		{ TEXT("FLAG"), EDreamWarpStyle::Flag, 60.0f, FVector2D(100.0, 80.0) },
		{ TEXT("FISH"), EDreamWarpStyle::Fish, 70.0f, FVector2D(-100.0, 0.0) },
		{ TEXT("BULGE"), EDreamWarpStyle::Bulge, 70.0f, FVector2D(100.0, 0.0) },
		{ TEXT("TWIST"), EDreamWarpStyle::Twist, 60.0f, FVector2D(-100.0, -80.0) },
		{ TEXT("RISE"), EDreamWarpStyle::Rise, 60.0f, FVector2D(100.0, -80.0) },
	};
	for (const FCase& Case : Cases)
	{
		UDreamText* Text = Stage->AddText(Case.Text, Case.Text, 40.0f, FVector2D(170.0, 60.0), Case.Position, FColor(250, 230, 160, 255));
		UDreamMeshModifierWarp* Warp = Text->GetWidget()->AddComponent<UDreamMeshModifierWarp>();
		Warp->SetStyle(Case.Style);
		Warp->SetBend(Case.Bend);
		Warp->SetCellLength(3.0f);
	}
	EnqueueSettledPictureCheck(Stage, TEXT("Gallery_Warp_Text"), 3000, EMissingGolden::Warn);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryJellyTest,
	"DreamGUI.RHI.MeshWarp.JelliesMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryJellyTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Checker = Stage->MakeChecker(64, 8, FColor(235, 235, 235, 255), FColor(200, 90, 120, 255));
	auto AddJelly = [&](const TCHAR* InName, FVector2D InPosition)
	{
		UDreamWidget* Block = Stage->AddBlock(InName, FVector2D(80.0, 80.0), InPosition, FColor::White, Checker);
		UDreamMeshModifierJelly* Jelly = Block->AddComponent<UDreamMeshModifierJelly>();
		// Pressed harder than the default, so that the picture shows it.
		Jelly->SetPressSquash(0.25f);
		// Held still by hand, so that the picture is the state the test made and not wherever the frames carried it.
		Jelly->SetCanExecuteTick(false);
		return Jelly;
	};
	// Pressed off its middle and held until it has settled; dragged out to the right; and caught mid-wobble after a push.
	UDreamMeshModifierJelly* Pressed = AddJelly(TEXT("Pressed"), FVector2D(-70.0, 60.0));
	UDreamMeshModifierJelly* Dragged = AddJelly(TEXT("Dragged"), FVector2D(60.0, 60.0));
	UDreamMeshModifierJelly* Pushed = AddJelly(TEXT("Pushed"), FVector2D(0.0, -60.0));
	EnqueueDo([Pressed, Dragged, Pushed]()
	{
		const FVector2D PressedCentre = Pressed->GetWidget()->GetLocalSpaceCenter();
		Pressed->SetPressed(true, PressedCentre + FVector2D(20.0, 20.0));
		const FVector2D DraggedCentre = Dragged->GetWidget()->GetLocalSpaceCenter();
		Dragged->SetPressed(true, DraggedCentre + FVector2D(30.0, 0.0));
		Dragged->DragTo(DraggedCentre + FVector2D(70.0, -10.0));
		for (int32 Frame = 0; Frame < 240; ++Frame)
		{
			Pressed->Simulate(1.0f / 60.0f, FVector2f::ZeroVector);
			Dragged->Simulate(1.0f / 60.0f, FVector2f::ZeroVector);
		}
		Pushed->Poke(FVector2D(900.0, 400.0));
		for (int32 Frame = 0; Frame < 4; ++Frame)
		{
			Pushed->Simulate(1.0f / 60.0f, FVector2f::ZeroVector);
		}
	});
	EnqueuePictureCheck(Stage, TEXT("Gallery_Jelly"), 12000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryTextOnPathTest,
	"DreamGUI.RHI.TextOnPath.MatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryTextOnPathTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpPixelTestLocal;
	FStageRef Stage = BeginStage(*this, FIntPoint(384, 320));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	// A badge: round the top of a circle, and round the bottom of the same one, flipped so it reads left to right.
	Stage->AddRectBlock(TEXT("Disc"), FVector2D(120.0, 120.0), FVector2D(-90.0, 70.0), FColor(60, 70, 100, 255), 60.0f);
	UDreamText* Top = Stage->AddText(TEXT("Top"), TEXT("DREAM BADGE"), 18.0f, FVector2D(200.0, 30.0), FVector2D(-90.0, 70.0), FColor(250, 220, 120, 255));
	UDreamMeshModifierTextOnPath* AlongTop = Top->GetWidget()->AddComponent<UDreamMeshModifierTextOnPath>();
	AlongTop->SetCircle(66.0f, FVector2D::ZeroVector, -90.0f, 180.0f, true);
	UDreamText* Bottom = Stage->AddText(TEXT("Bottom"), TEXT("SINCE 2026"), 16.0f, FVector2D(200.0, 30.0), FVector2D(-90.0, 70.0), FColor(180, 220, 250, 255));
	UDreamMeshModifierTextOnPath* AlongBottom = Bottom->GetWidget()->AddComponent<UDreamMeshModifierTextOnPath>();
	AlongBottom->SetCircle(66.0f, FVector2D::ZeroVector, 90.0f, 180.0f, true);
	AlongBottom->SetFlip(true);
	AlongBottom->SetAnchor(EDreamTextPathAnchor::Top);
	// A drawn wave and a text bent along it, glyph by glyph, spaced out.
	UDreamWidget* WaveWidget = Stage->AddWidget(TEXT("Wave"), FVector2D(340.0, 100.0), FVector2D(0.0, -90.0));
	UDreamPath* Wave = WaveWidget->CreateNewVisual<UDreamPath>();
	Wave->SetPoints({ FVector2D(-160.0, -10.0), FVector2D(-80.0, 30.0), FVector2D(0.0, -10.0), FVector2D(80.0, 30.0), FVector2D(160.0, -10.0) });
	Wave->SetLineWidth(3.0f);
	Wave->SetColor(FColor(120, 200, 140, 255));
	UDreamText* Rider = Stage->AddText(TEXT("Rider"), TEXT("riding a gentle wave"), 22.0f, FVector2D(300.0, 30.0), FVector2D(0.0, -60.0), FColor::White);
	UDreamMeshModifierTextOnPath* AlongWave = Rider->GetWidget()->AddComponent<UDreamMeshModifierTextOnPath>();
	AlongWave->SetSource(EDreamTextPathSource::Path);
	AlongWave->SetPathWidget(WaveWidget);
	AlongWave->SetBendGlyphs(true);
	AlongWave->SetLetterSpacing(2.0f);
	AlongWave->SetBaselineOffset(4.0f);
	// Round a whole small circle, as a ring.
	UDreamText* Ring = Stage->AddText(TEXT("Ring"), TEXT("* AROUND * AND * AROUND "), 13.0f, FVector2D(300.0, 20.0), FVector2D(100.0, 70.0), FColor(240, 150, 200, 255));
	UDreamMeshModifierTextOnPath* AlongRing = Ring->GetWidget()->AddComponent<UDreamMeshModifierTextOnPath>();
	AlongRing->SetCircle(52.0f, FVector2D::ZeroVector, 0.0f, 360.0f, true);
	AlongRing->SetAlign(EDreamTextPathAlign::Start);
	EnqueueSettledPictureCheck(Stage, TEXT("Gallery_TextOnPath"), 3000, EMissingGolden::Warn);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryMaskTransitionTest,
	"DreamGUI.RHI.MaskTransition.PresetsMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryMaskTransitionTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpPixelTestLocal;
	FStageRef Stage = BeginStage(*this, FIntPoint(400, 200));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Checker = Stage->MakeChecker(64, 8, FColor(235, 235, 235, 255), FColor(90, 150, 110, 255));
	UTexture2D* Order = Stage->MakeTexture(64, [](int32 X, int32 Y)
	{
		// The Texture preset's order: rings out from the top left corner.
		const float Distance = FMath::Sqrt(static_cast<float>(X * X + Y * Y)) / 90.0f;
		const uint8 Value = static_cast<uint8>(FMath::Clamp(FMath::Frac(Distance * 3.0f) * 0.5f + Distance * 0.5f, 0.0f, 1.0f) * 255.0f);
		return FColor(Value, Value, Value, 255);
	});
	for (int32 Index = 0; Index < 10; ++Index)
	{
		const int32 Column = Index % 5;
		const int32 Row = Index / 5;
		const EDreamMaskTransitionPreset Preset = static_cast<EDreamMaskTransitionPreset>(Index);
		UDreamWidget* Panel = Stage->AddBlock(*StaticEnum<EDreamMaskTransitionPreset>()->GetNameStringByValue(Index), FVector2D(70.0, 80.0),
			FVector2D(-160.0 + Column * 80.0, 45.0 - Row * 90.0), FColor::White, Checker);
		UDreamMaskTransition* Transition = Panel->AddComponent<UDreamMaskTransition>();
		Transition->SetPreset(Preset);
		Transition->SetCount(Preset == EDreamMaskTransitionPreset::Shatter ? 10 : 4);
		Transition->SetSoftness(0.04f);
		if (Preset == EDreamMaskTransitionPreset::Texture)
		{
			Transition->SetTexture(Order, EDreamClipMaskChannel::Red);
		}
		if (Preset == EDreamMaskTransitionPreset::Wipe)
		{
			Transition->SetAngle(30.0f);
		}
		Transition->SetProgress(0.55f);
	}
	EnqueuePictureCheck(Stage, TEXT("Gallery_Transition_Presets"), 15000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMaskTransitionPixelAgainstHitTest,
	"DreamGUI.RHI.MaskTransition.AShownPixelIsAHitAndAHitIsAShownPixel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamMaskTransitionPixelAgainstHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpPixelTestLocal;
	/**
	 * White blocks under the transitions whose masks the hit test can measure, reversed or not and halfway, on the dark
	 * backdrop. Every other pixel is read and the point under its centre is asked of the hit test: clearly shown must hit,
	 * clearly not shown must miss. A reversed source read one way by the shader and another by the CPU shows up here.
	 */
	FStageRef Stage = BeginStage(*this, FIntPoint(384, 256));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	TArray<UDreamWidget*> Contents;
	const EDreamMaskTransitionPreset Presets[] = { EDreamMaskTransitionPreset::Wipe, EDreamMaskTransitionPreset::Clock, EDreamMaskTransitionPreset::Iris,
		EDreamMaskTransitionPreset::Diamond, EDreamMaskTransitionPreset::Blinds, EDreamMaskTransitionPreset::Dissolve };
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const int32 Column = Index % 3;
		const int32 Row = Index / 3;
		UDreamWidget* Panel = Stage->AddWidget(*FString::Printf(TEXT("Panel%d"), Index), FVector2D(100.0, 90.0), FVector2D(-120.0 + Column * 120.0, 60.0 - Row * 120.0));
		Contents.Add(Stage->AddBlock(*FString::Printf(TEXT("Content%d"), Index), FVector2D(100.0, 90.0), FVector2D::ZeroVector, FColor::White, nullptr, Panel));
		UDreamMaskTransition* Transition = Panel->AddComponent<UDreamMaskTransition>();
		Transition->SetPreset(Presets[Index]);
		Transition->SetReverse(Index % 2 == 1);
		Transition->SetSoftness(0.0f);
		Transition->SetCount(3);
		Transition->SetGrain(12.0f);
		Transition->SetAngle(Presets[Index] == EDreamMaskTransitionPreset::Wipe ? 25.0f : 0.0f);
		Transition->SetProgress(0.5f);
	}
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, 3000);
	EnqueueFramesUntilStable(Stage);
	EnqueueDo([this, Stage, Contents]()
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!TestTrue(TEXT("The picture reads back"), Stage->ReadBack(Pixels, Size)))
		{
			return;
		}
		FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("MaskTransition_PixelAgainstHit"));
		const UDreamWidget* Root = Stage->GetRoot();
		const float Left = Root->GetLocalSpaceLeft();
		const float Top = Root->GetLocalSpaceTop();
		const float UnitsPerPixelX = Root->GetWidth() / Size.X;
		const float UnitsPerPixelY = Root->GetHeight() / Size.Y;
		const float Ground = FLinearColor(Stage->GetBackdrop()).G;
		int32 Shown = 0, Hidden = 0, Disagree = 0;
		FString FirstDisagreement;
		for (int32 Y = 0; Y < Size.Y; Y += 2)
		{
			for (int32 X = 0; X < Size.X; X += 2)
			{
				const FVector Local(0.0, Left + (X + 0.5f) * UnitsPerPixelX, Top - (Y + 0.5f) * UnitsPerPixelY);
				const FVector World = Root->GetWorldTransform().TransformPosition(Local);
				const UDreamWidget* Owner = nullptr;
				for (const UDreamWidget* Content : Contents)
				{
					const FVector InContent = Content->GetWorldTransform().InverseTransformPosition(World);
					if (InContent.Y > Content->GetLocalSpaceLeft() + 1.0 && InContent.Y < Content->GetLocalSpaceRight() - 1.0
						&& InContent.Z > Content->GetLocalSpaceBottom() + 1.0 && InContent.Z < Content->GetLocalSpaceTop() - 1.0)
					{
						Owner = Content;
						break;
					}
				}
				if (Owner == nullptr)
				{
					continue;
				}
				const float Coverage = (FLinearColor(Pixels[Y * Size.X + X]).G - Ground) / FMath::Max(1.0f - Ground, 0.0001f);
				if (Coverage > 0.3f && Coverage < 0.7f)
				{
					continue;
				}
				const bool bShown = Coverage >= 0.7f;
				const bool bHit = Owner->IsPointVisibleOnClip(World);
				(bShown ? Shown : Hidden)++;
				if (bShown != bHit)
				{
					if (Disagree++ == 0)
					{
						FirstDisagreement = FString::Printf(TEXT("pixel (%d, %d) of %s: %s on screen, %s by the hit test"), X, Y, *Owner->GetDisplayName(),
							bShown ? TEXT("shown") : TEXT("hidden"), bHit ? TEXT("hit") : TEXT("missed"));
					}
				}
			}
		}
		TestTrue(TEXT("Enough shown pixels were asked"), Shown > 400);
		TestTrue(TEXT("Enough hidden pixels were asked"), Hidden > 400);
		if (Disagree > 0)
		{
			AddError(FString::Printf(TEXT("%d pixels disagree with the hit test; the first, %s"), Disagree, *FirstDisagreement));
		}
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryCrossFadeTest,
	"DreamGUI.RHI.MaskTransition.ACrossFadeShowsOneOrTheOtherAndMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryCrossFadeTest::RunTest(const FString& Parameters)
{
	using namespace DreamMeshWarpPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	// A red card giving way to a blue one in the same place through ink, halfway, with a hard edge: every pixel of the card
	// is one or the other. (A soft edge lets the backdrop through a little where the two half-cover the same pixel: each
	// is drawn over what is there, and two halves over a backdrop are not a whole.)
	UDreamWidget* From = Stage->AddBlock(TEXT("From"), FVector2D(200.0, 140.0), FVector2D::ZeroVector, FColor(220, 60, 50, 255));
	UDreamWidget* To = Stage->AddBlock(TEXT("To"), FVector2D(200.0, 140.0), FVector2D::ZeroVector, FColor(50, 90, 220, 255));
	UDreamMaskTransition::FindOrAdd(To)->SetSoftness(0.0f);
	UDreamMaskTransition::CrossFade(From, To, EDreamMaskTransitionPreset::Ink, 1000.0f);
	UDreamMaskTransition* Going = From->GetComponent<UDreamMaskTransition>();
	UDreamMaskTransition* Coming = To->GetComponent<UDreamMaskTransition>();
	if (!TestTrue(TEXT("Both have transitions"), Going != nullptr && Coming != nullptr))
	{
		Stage->TearDown();
		return false;
	}
	// Held halfway: the tween is stopped and the two set by hand.
	Going->Stop();
	Going->SetProgress(0.5f);
	Coming->SetProgress(0.5f);
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, 20000);
	EnqueueFramesUntilStable(Stage);
	EnqueueDo([this, Stage]()
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!TestTrue(TEXT("The picture reads back"), Stage->ReadBack(Pixels, Size)))
		{
			return;
		}
		// Inside the card, a few pixels in from its edge: no backdrop shows through, anywhere.
		int32 Red = 0, Blue = 0, Gap = 0;
		for (int32 Y = Size.Y / 2 - 60; Y < Size.Y / 2 + 60; ++Y)
		{
			for (int32 X = Size.X / 2 - 90; X < Size.X / 2 + 90; ++X)
			{
				const FColor Pixel = Pixels[Y * Size.X + X];
				const int32 Lit = Pixel.R + Pixel.B;
				if (Lit < 180)
				{
					++Gap;
				}
				Red += Pixel.R > 150 && Pixel.B < 120;
				Blue += Pixel.B > 150 && Pixel.R < 120;
			}
		}
		TestTrue(TEXT("Both cards show"), Red > 2000 && Blue > 2000);
		TestEqual(TEXT("Nowhere does the backdrop show through"), Gap, 0);
	});
	EnqueuePictureCheck(Stage, TEXT("Gallery_Transition_CrossFade"), 20000);
	EnqueueTearDown(Stage);
	return true;
}

#endif
