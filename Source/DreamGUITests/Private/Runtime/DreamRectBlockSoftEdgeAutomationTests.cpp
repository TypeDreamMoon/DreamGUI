// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/DreamUISpriteInfo.h"

#include "Driver/DreamDriverRig.h"

/*
 * A SOFT EDGE GROWS A RECT BLOCK'S QUAD, AND THE SPRITE IS SAMPLED WHERE THE GROWN QUAD IS.
 *
 * A soft edge fades out over a unit past the rect, so the quad is grown by a unit on every side. The UVs
 * that place the block's own shape (UV0) were moved out with the corners; the UVs that sample the texture
 * and the sprite (UV2) stayed on the sprite's corners, so the sprite was stretched over the grown quad and
 * no longer lined up with the shape it was drawn in.
 *
 * Moved out by as much of the SPRITE as the margin is of the rect. A sprite packed into an atlas covers a
 * part of its texture, and a margin moved out by that fraction of the whole texture instead samples the
 * sprite's neighbours -- at the rect's own edges too, since UV2 is interpolated across the quad. The
 * default white sprite is packed and covers a single texel, so every default block would have drawn a
 * fringe of whatever sits next to it in the atlas.
 */
namespace DreamRectBlockSoftEdgeTestLocal
{
	/**
	 * Each corner of InGeometry samples the sprite where the corner falls across the rect, mapped onto
	 * InSprite's rectangle in its texture. InMinX and InMaxY place the rect in the widget's plane.
	 */
	void CheckSpriteUVs(FAutomationTestBase& InTest, const TCHAR* InWhat, const FDreamUIGeometry& InGeometry, const FDreamUISpriteInfo& InSprite,
		float InMinX, float InMaxY, float InWidth, float InHeight)
	{
		const FVector2f SpriteSize = InSprite.MaxUV - InSprite.MinUV;
		// Measured in the sprite: the margin's share of a sprite a thousandth of its atlas across is a few
		// millionths, and a thousandth of the sprite is well inside that.
		const float Tolerance = FMath::Max(1.e-3f * FMath::Min(SpriteSize.X, SpriteSize.Y), 1.e-9f);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector3f& Position = InGeometry.OriginVertices[Index].Position;
			// Across the rect, 0 at its left and top edges and 1 at its right and bottom ones.
			const FVector2f Across((Position.Y - InMinX) / InWidth, (InMaxY - Position.Z) / InHeight);
			const FVector2f Expected = InSprite.MinUV + Across * SpriteSize;
			const FVector2f Sampled = InGeometry.Vertices[Index].TextureCoordinate[2];
			InTest.TestTrue(FString::Printf(TEXT("%s: corner %d, (%.4f, %.4f) across the rect, samples the sprite at (%.7f, %.7f) (sampled at (%.7f, %.7f))"),
					InWhat, Index, Across.X, Across.Y, Expected.X, Expected.Y, Sampled.X, Sampled.Y),
				Sampled.Equals(Expected, Tolerance));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockSoftEdgeSpriteUVTest,
	"DreamGUI.RectBlock.WithASoftEdgeTheSpriteIsSampledWhereTheGrownQuadIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRectBlockSoftEdgeSpriteUVTest::RunTest(const FString& Parameters)
{
	using namespace DreamRectBlockSoftEdgeTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FVector2D Size(200.0, 100.0);
	UDreamWidget* Widget = Rig.MakeWidget(TEXT("SoftEdgedBlock"), nullptr, Size);
	UDreamRectBlock* Block = Widget != nullptr ? Widget->CreateNewVisual<UDreamRectBlock>() : nullptr;
	if (!TestNotNull(TEXT("A rect block"), Block))
	{
		return false;
	}
	Block->SetSoftEdge(true);
	Rig.PumpFrames(2);

	// The rect in the widget's plane: +Y right, +Z up, the pivot at the origin.
	const FVector2D Pivot = Widget->GetPivot();
	const float MinX = static_cast<float>(-Size.X * Pivot.X);
	const float MaxY = static_cast<float>(Size.Y * (1.0 - Pivot.Y));
	const float Width = static_cast<float>(Size.X);
	const float Height = static_cast<float>(Size.Y);

	// A sprite covering part of its texture, put through the block's own geometry function: here a
	// fraction of the rect and the same fraction of the sprite are different UVs.
	{
		FDreamUISpriteInfo WholeTexture;
		FDreamUISpriteInfo Sprite;
		Sprite.MinUV = FVector2f(0.25f, 0.5f);
		Sprite.MaxUV = FVector2f(0.75f, 0.75f);
		FDreamUIGeometry Geometry;
		FDreamUIGeometry::UpdateRectBlockVertex(&Geometry, /*bEnableOuterShadow*/ false, FVector2f::ZeroVector, 0.0f, 0.0f, /*bSoftEdge*/ true,
			Width, Height, FVector2f(Pivot), WholeTexture, Sprite, Widget->GetRenderCanvas(), Block, FColor::White,
			true, true, true, true);
		if (TestTrue(TEXT("The quad is built"), Geometry.OriginVertices.Num() == 4 && Geometry.Vertices.Num() == 4))
		{
			TestTrue(TEXT("The soft edge grows the quad past the rect"), Geometry.OriginVertices[0].Position.Y < MinX);
			CheckSpriteUVs(*this, TEXT("A sprite covering part of its texture"), Geometry, Sprite, MinX, MaxY, Width, Height);
		}
	}

	// The block as it draws by default, from the packed white sprite.
	const FDreamUIGeometry* Geometry = Block->GetGeometry();
	UDreamUISpriteData_BaseObject* DefaultSprite = Block->GetBodySpriteTexture();
	if (TestTrue(TEXT("The block has its quad"), Geometry != nullptr && Geometry->OriginVertices.Num() == 4 && Geometry->Vertices.Num() == 4)
		&& TestTrue(TEXT("and draws its body from a sprite"), Block->GetBodyTextureMode() == EDreamRectBlockTextureMode::Sprite && DefaultSprite != nullptr))
	{
		CheckSpriteUVs(*this, TEXT("The default white sprite"), *Geometry, DefaultSprite->GetSpriteInfo(), MinX, MaxY, Width, Height);
	}
	return true;
}

#endif
