// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#include "Misc/ObjectThumbnail.h"
#include "ObjectTools.h"
#include "ThumbnailRendering/ThumbnailManager.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamWidgetPlacement.h"
#include "Core/DreamWidgetTree.h"
#include "DreamWidgetBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Thumbnail/DreamWidgetBlueprintThumbnailRenderer.h"
#include "Utils/DreamUIUtils.h"

#include "DreamPixelProbe.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

/*
 * A DreamUI Widget Blueprint's thumbnail is the screen it authors, drawn -- not the wireframe of its anchor rects it
 * used to be, which showed most screens as a grey tile with a frame or two on it. Rendered here the way the Content
 * Browser and a package save render one, through ThumbnailTools, so what is checked is the tile a user sees.
 */
namespace DreamWidgetThumbnailTestLocal
{
	static constexpr int32 ThumbnailExtent = 256;

	FColor PixelAt(const FObjectThumbnail& InThumbnail, int32 InX, int32 InY)
	{
		const TArray<uint8>& Data = InThumbnail.GetUncompressedImageData();
		const int32 Index = (InY * InThumbnail.GetImageWidth() + InX) * 4;
		if (!Data.IsValidIndex(Index + 3))
		{
			return FColor::Transparent;
		}
		// FObjectThumbnail keeps its pixels as FColor does: blue, green, red, alpha.
		return FColor(Data[Index + 2], Data[Index + 1], Data[Index + 0], Data[Index + 3]);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetThumbnailIsTheScreenTest,
	"DreamGUI.RHI.AWidgetBlueprintThumbnailIsTheScreenItAuthorsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamWidgetThumbnailIsTheScreenTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetThumbnailTestLocal;

	DreamTests::FDesignerTestAsset Asset = DreamTests::CreateDesignerTestAsset(TEXT("ThumbnailScreen"), true);
	ON_SCOPE_EXIT { DreamTests::ReleaseDesignerTestAsset(Asset); };
	if (!TestTrue(TEXT("a widget blueprint to draw"), Asset.IsValid()))
	{
		return false;
	}
	FThumbnailRenderingInfo* RenderingInfo = UThumbnailManager::Get().GetRenderingInfo(Asset.Blueprint);
	if (!TestTrue(TEXT("DreamUI Widget Blueprints have DreamGUI's thumbnail renderer"),
		RenderingInfo != nullptr && Cast<UDreamWidgetBlueprintThumbnailRenderer>(RenderingInfo->Renderer) != nullptr))
	{
		return false;
	}

	// A red block on the middle quarter of a 1920x1080 design canvas, authored as the designer authors a widget: in the
	// tree, then compiled.
	Asset.Blueprint->DesignerData.CanvasSize = FIntPoint(1920, 1080);
	UDreamWidgetTree* Tree = Asset.Blueprint->GetOrCreateWidgetTree();
	UDreamWidget* Block = Tree->ConstructWidget<UDreamWidget>();
	Block->SetDisplayName(TEXT("Block"));
	Block->SetParentBeforeRegister(Tree->RootWidget.Get());
	FDreamUIAnchorData MiddleQuarter;
	MiddleQuarter.AnchorMin = FVector2D(0.25, 0.25);
	MiddleQuarter.AnchorMax = FVector2D(0.75, 0.75);
	MiddleQuarter.Pivot = FVector2D(0.5, 0.5);
	MiddleQuarter.AnchoredPosition = FVector2D::ZeroVector;
	MiddleQuarter.SizeDelta = FVector2D::ZeroVector;
	Block->SetAnchorData(MiddleQuarter);
	UDreamTexture* Visual = Block->CreateNewVisual<UDreamTexture>();
	if (!TestNotNull(TEXT("a texture visual on the block"), Visual))
	{
		return false;
	}
	Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
	Visual->SetColor(FColor::Red);
	FKismetEditorUtilities::CompileBlueprint(Asset.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

	FObjectThumbnail Thumbnail;
	ThumbnailTools::RenderThumbnail(Asset.Blueprint, ThumbnailExtent, ThumbnailExtent,
		ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush, nullptr, &Thumbnail);
	if (!TestEqual(TEXT("the thumbnail is the size asked for"), Thumbnail.GetImageWidth(), ThumbnailExtent)
		|| !TestEqual(TEXT("...both ways"), Thumbnail.GetImageHeight(), ThumbnailExtent)
		|| !TestEqual(TEXT("...and has every pixel"), Thumbnail.GetUncompressedImageData().Num(), ThumbnailExtent * ThumbnailExtent * 4))
	{
		return false;
	}
	{
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(ThumbnailExtent * ThumbnailExtent);
		for (int32 Y = 0; Y < ThumbnailExtent; ++Y)
		{
			for (int32 X = 0; X < ThumbnailExtent; ++X)
			{
				Pixels[Y * ThumbnailExtent + X] = PixelAt(Thumbnail, X, Y);
			}
		}
		FDreamPixelProbe::SaveCapture(Pixels, FIntPoint(ThumbnailExtent, ThumbnailExtent), TEXT("Thumbnail_WidgetBlueprint"));
	}

	// 16:9 in a square tile: the canvas runs from row 56 to row 200, the block over its middle quarter (rows 92 to 164,
	// columns 64 to 192), and the bars above and below are what the thumbnail was cleared to.
	const FColor Centre = PixelAt(Thumbnail, ThumbnailExtent / 2, ThumbnailExtent / 2);
	TestTrue(FString::Printf(TEXT("the block is drawn, red, in the middle of the tile (%s)"), *Centre.ToString()),
		Centre.R > 200 && Centre.G < 48 && Centre.B < 48);

	// The wireframe filled its canvas with a slate grey (about 74,79,89 on screen) and outlined the block on it, so a
	// dark canvas round a red block is the drawn screen and nothing else.
	const FColor Canvas = PixelAt(Thumbnail, ThumbnailExtent / 2, 70);
	TestTrue(FString::Printf(TEXT("round the block is the canvas's dark backdrop (%s)"), *Canvas.ToString()),
		Canvas.R < 60 && Canvas.G < 60 && Canvas.B < 60 && Canvas.R + Canvas.G + Canvas.B > 30);
	const FColor Bar = PixelAt(Thumbnail, ThumbnailExtent / 2, 20);
	TestTrue(FString::Printf(TEXT("and the letterbox bar is left as the thumbnail was cleared (%s)"), *Bar.ToString()),
		FDreamPixelProbe::IsNear(Bar, FColor::Black, 8));
	return true;
}

#endif
