// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamShapeBlendGroup.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBlendMath.h"
#include "Event/DreamPointerEventData.h"
#include "Extensions/Effects/DreamHoloFoil.h"
#include "Extensions/Effects/DreamPointerLight.h"
#include "Extensions/Effects/DreamTiltCard.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"

/*
 * Shape blends, the pointer light and the holo foil, drawn: members melted by a union, a cut and an intersection, and a group
 * with a layer style round its melted shape; every shown pixel a hit and every hit a shown pixel; bevels and borders lit from
 * where a light stands; and a holo foil on a tilted card.
 */
namespace DreamShapeBlendPixelTestLocal
{
	using namespace DreamGalleryStage;

	UDreamRectBlock* AddMember(FGalleryStage& InStage, UDreamWidget* InGroup, const TCHAR* InName, FVector2D InSize, FVector2D InPosition, FColor InColor, float InCorner,
		EDreamShapeBlendOperation InOperation = EDreamShapeBlendOperation::Union)
	{
		UDreamWidget* Widget = InStage.AddWidget(InName, InSize, InPosition, InGroup);
		UDreamRectBlock* Block = Widget->CreateNewVisual<UDreamRectBlock>();
		if (Block != nullptr)
		{
			Block->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Value);
			Block->SetCornerRadius(FVector4(InCorner, InCorner, InCorner, InCorner));
			Block->SetBodyColor(InColor);
			Block->SetShapeBlendOperation(InOperation);
		}
		return Block;
	}

	UDreamShapeBlendGroup* MakeGroup(UDreamWidget* InWidget, float InSmoothness)
	{
		UDreamShapeBlendGroup* Group = InWidget->AddComponent<UDreamShapeBlendGroup>();
		Group->SetSmoothness(InSmoothness);
		Group->Refresh();
		return Group;
	}

	/** The gallery: a union of two discs, three with a cut, an intersection, and a styled pair. */
	void BuildGallery(FGalleryStage& InStage, TArray<UDreamShapeBlendGroup*>* OutGroups = nullptr)
	{
		{
			UDreamWidget* Group = InStage.AddWidget(TEXT("Union"), FVector2D(160.0, 100.0), FVector2D(-110.0, 60.0));
			AddMember(InStage, Group, TEXT("UnionA"), FVector2D(70.0, 70.0), FVector2D(-28.0, 0.0), FColor(250, 110, 90), 35.0f);
			AddMember(InStage, Group, TEXT("UnionB"), FVector2D(54.0, 54.0), FVector2D(34.0, 6.0), FColor(90, 150, 250), 27.0f);
			UDreamShapeBlendGroup* Blend = MakeGroup(Group, 26.0f);
			if (OutGroups != nullptr)
			{
				OutGroups->Add(Blend);
			}
		}
		{
			UDreamWidget* Group = InStage.AddWidget(TEXT("Cut"), FVector2D(180.0, 100.0), FVector2D(110.0, 60.0));
			AddMember(InStage, Group, TEXT("CutA"), FVector2D(60.0, 60.0), FVector2D(-45.0, 0.0), FColor(240, 200, 80), 14.0f);
			AddMember(InStage, Group, TEXT("CutB"), FVector2D(60.0, 60.0), FVector2D(0.0, 10.0), FColor(120, 220, 140), 14.0f);
			AddMember(InStage, Group, TEXT("CutC"), FVector2D(60.0, 60.0), FVector2D(45.0, 0.0), FColor(200, 120, 230), 14.0f);
			AddMember(InStage, Group, TEXT("CutHole"), FVector2D(26.0, 26.0), FVector2D(0.0, 10.0), FColor::White, 13.0f, EDreamShapeBlendOperation::Subtract);
			UDreamShapeBlendGroup* Blend = MakeGroup(Group, 14.0f);
			if (OutGroups != nullptr)
			{
				OutGroups->Add(Blend);
			}
		}
		{
			UDreamWidget* Group = InStage.AddWidget(TEXT("Meet"), FVector2D(120.0, 100.0), FVector2D(-110.0, -60.0));
			AddMember(InStage, Group, TEXT("MeetA"), FVector2D(80.0, 80.0), FVector2D(-12.0, 0.0), FColor(230, 120, 160), 40.0f);
			AddMember(InStage, Group, TEXT("MeetB"), FVector2D(80.0, 80.0), FVector2D(12.0, 0.0), FColor(230, 120, 160), 40.0f, EDreamShapeBlendOperation::Intersect);
			UDreamShapeBlendGroup* Blend = MakeGroup(Group, 10.0f);
			if (OutGroups != nullptr)
			{
				OutGroups->Add(Blend);
			}
		}
		{
			UDreamWidget* Group = InStage.AddWidget(TEXT("Styled"), FVector2D(180.0, 100.0), FVector2D(110.0, -60.0));
			AddMember(InStage, Group, TEXT("StyledA"), FVector2D(70.0, 40.0), FVector2D(-34.0, 8.0), FColor(70, 90, 140), 20.0f);
			AddMember(InStage, Group, TEXT("StyledB"), FVector2D(70.0, 40.0), FVector2D(34.0, -8.0), FColor(70, 90, 140), 20.0f);
			UDreamShapeBlendGroup* Blend = MakeGroup(Group, 30.0f);
			FDreamLayerEffects S;
			FDreamStyleDropShadow& Shadow = S.DropShadows.AddDefaulted_GetRef();
			Shadow.Distance = 6.0f;
			Shadow.Size = 8.0f;
			FDreamStyleStroke& Stroke = S.Strokes.AddDefaulted_GetRef();
			Stroke.Size = 2.0f;
			Stroke.Color = FColor(230, 240, 255);
			S.BevelEmboss.bEnabled = true;
			S.BevelEmboss.Size = 6.0f;
			Group->AddComponent<UDreamLayerStyle>()->SetStyle(S);
			if (OutGroups != nullptr)
			{
				OutGroups->Add(Blend);
			}
		}
	}

	void EnqueueReadBack(const FStageRef& InStage, TFunction<void(const TArray<FColor>&, FIntPoint)> InCheck)
	{
		EnqueueDo([InStage, InCheck]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (InStage->ReadBack(Pixels, Size))
			{
				InCheck(Pixels, Size);
			}
			else
			{
				InStage->GetTest().AddError(TEXT("The picture was not read back"));
			}
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamShapeBlendGalleryPixelTest,
	"DreamGUI.RHI.ShapeBlend.MembersMeltIntoOneShapeAndMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamShapeBlendGalleryPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamShapeBlendPixelTestLocal;
	FStageRef Stage = BeginStage(*this, FIntPoint(448, 256));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	BuildGallery(*Stage);
	EnqueuePictureCheck(Stage, TEXT("Gallery_ShapeBlend"), 15000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamShapeBlendHitPixelTest,
	"DreamGUI.RHI.ShapeBlend.AShownPixelIsAHitAndAHitIsAShownPixel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamShapeBlendHitPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamShapeBlendPixelTestLocal;
	const FIntPoint Size(448, 256);
	// A black backdrop, so that how much of a pixel a shape covers is how bright it is.
	FStageRef Stage = BeginStage(*this, Size, FColor::Black);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	TSharedRef<TArray<UDreamShapeBlendGroup*>> Groups = MakeShared<TArray<UDreamShapeBlendGroup*>>();
	BuildGallery(*Stage, &*Groups);
	// The styled group's stroke and shadow reach past the shape itself: it is left out of this one.
	EnqueueFramesUntilDrawn(Stage, 10000);
	EnqueueFramesUntilStable(Stage);
	EnqueueReadBack(Stage, [this, Stage, Groups](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		int32 Shown = 0, Hidden = 0, Wrong = 0, Reported = 0;
		const UDreamWidget* Root = Stage->GetRoot();
		auto Brightest = [&InPixels, &InSize](int32 InX, int32 InY)
		{
			const FColor& Pixel = InPixels[InY * InSize.X + InX];
			return FMath::Max3(Pixel.R, Pixel.G, Pixel.B);
		};
		for (int32 Y = 3; Y < InSize.Y - 3; Y += 2)
		{
			for (int32 X = 3; X < InSize.X - 3; X += 2)
			{
				// Clear of every edge: the pixel and those two pixels round it all lit, or all dark. An edge pixel's coverage,
				// ramped linearly and then encoded, is too bright to tell a hair inside from a hair outside by its value.
				bool bClearlyShown = true;
				bool bClearlyHidden = true;
				for (int32 DY = -2; DY <= 2; DY += 2)
				{
					for (int32 DX = -2; DX <= 2; DX += 2)
					{
						const uint8 Value = Brightest(X + DX, Y + DY);
						bClearlyShown &= Value > 70;
						bClearlyHidden &= Value < 8;
					}
				}
				if (!bClearlyShown && !bClearlyHidden)
				{
					continue;
				}
				const FColor Pixel = InPixels[Y * InSize.X + X];
				const FVector World = Root->GetWorldTransform().TransformPosition(FVector(0.0, X + 0.5 - InSize.X * 0.5, InSize.Y * 0.5 - (Y + 0.5)));
				bool bHit = false;
				for (int32 Group = 0; Group < 3 && !bHit; ++Group)
				{
					TArray<UDreamRectBlock*> Members;
					(*Groups)[Group]->GetMembers(Members);
					for (const UDreamRectBlock* Member : Members)
					{
						bHit |= (*Groups)[Group]->HitTest(Member, World);
					}
				}
				const bool bStyled = X > InSize.X / 2 && Y > InSize.Y / 2;
				if (bStyled)
				{
					continue;
				}
				(bClearlyShown ? Shown : Hidden) += 1;
				if (bHit != bClearlyShown)
				{
					++Wrong;
					if (Reported++ < 12)
					{
						AddInfo(FString::Printf(TEXT("(%d, %d): %s, hit %s"), X, Y, *FDreamPixelProbe::Describe(Pixel), bHit ? TEXT("yes") : TEXT("no")));
					}
				}
			}
		}
		TestTrue(*FString::Printf(TEXT("Shown and hidden pixels were both sampled (%d, %d)"), Shown, Hidden), Shown > 500 && Hidden > 500);
		TestEqual(TEXT("Every clearly shown pixel is a hit and every clearly hidden one is not"), Wrong, 0);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPointerLightPixelTest,
	"DreamGUI.RHI.PointerLight.BevelsAndBordersAreLitFromWhereTheLightStands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamPointerLightPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamShapeBlendPixelTestLocal;
	FStageRef Stage = BeginStage(*this, FIntPoint(384, 192));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	FDreamLayerEffects Bevel;
	Bevel.BevelEmboss.bEnabled = true;
	Bevel.BevelEmboss.Size = 10.0f;
	Bevel.BevelEmboss.Depth = 2.0f;
	Bevel.BevelEmboss.HighlightOpacity = 1.0f;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UDreamRectBlock* Block = Stage->AddRectBlock(*FString::Printf(TEXT("Key%d"), Index), FVector2D(70.0, 70.0), FVector2D(-130.0 + 65.0 * Index, 40.0), FColor(70, 80, 100), 12.0f);
		Block->GetWidget()->AddComponent<UDreamLayerStyle>()->SetStyle(Bevel);
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UDreamRectBlock* Block = Stage->AddRectBlock(*FString::Printf(TEXT("Tile%d"), Index), FVector2D(90.0, 56.0), FVector2D(-100.0 + 100.0 * Index, -50.0), FColor(24, 26, 34), 8.0f);
		Block->SetEnableBorder(true);
		Block->SetBorderWidth(2.0f);
		Block->SetBorderColor(FColor(60, 64, 80));
	}
	// On the stage's root, so that every key and tile is under it.
	UDreamPointerLight* Light = Stage->GetRoot()->AddComponent<UDreamPointerLight>();
	Light->SetFollowPointer(false);
	Light->SetLightColor(FColor(255, 230, 190));
	Light->SetRadius(150.0f);
	Light->SetHeight(50.0f);
	Light->SetLightPosition(FVector2D(-60.0, -10.0));
	EnqueueDo([Light]() { Light->Update(0.0f); });
	EnqueuePictureCheck(Stage, TEXT("Gallery_PointerLight"), 15000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamHoloFoilPixelTest,
	"DreamGUI.RHI.HoloFoil.AFoilCardTiltedInPerspectiveMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamHoloFoilPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamShapeBlendPixelTestLocal;
	FStageRef Stage = BeginStage(*this, FIntPoint(384, 256));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	UDreamWidget* Scope = Stage->AddWidget(TEXT("Scope"), FVector2D(384.0, 256.0), FVector2D::ZeroVector);
	Scope->SetPerspective(true);
	Scope->SetPerspectiveFieldOfView(50.0f);
	const FVector2D Views[2] = { FVector2D(-0.6, 0.3), FVector2D(0.5, -0.4) };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UDreamWidget* Card = Stage->AddWidget(*FString::Printf(TEXT("Card%d"), Index), FVector2D(130.0, 180.0), FVector2D(-85.0 + 170.0 * Index, 0.0), Scope);
		UDreamRectBlock* Block = Card->CreateNewVisual<UDreamRectBlock>();
		if (Block == nullptr)
		{
			continue;
		}
		Block->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Value);
		Block->SetCornerRadius(FVector4(12.0, 12.0, 12.0, 12.0));
		Block->SetBodyColor(FColor(36, 40, 70));
		UDreamTiltCard* Tilt = Card->AddComponent<UDreamTiltCard>();
		Tilt->SetFollowPointer(false);
		Tilt->SetTargetTilt(Views[Index]);
		Tilt->Step(0.0f);
		UDreamHoloFoil* Foil = Card->AddComponent<UDreamHoloFoil>();
		Foil->SetViewSource(EDreamHoloFoilView::Tilt);
		Foil->Update(0.0f);
	}
	EnqueuePictureCheck(Stage, TEXT("Gallery_HoloFoil"), 20000);
	EnqueueTearDown(Stage);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
