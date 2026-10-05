// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Thumbnail/DreamWidgetBlueprintThumbnailRenderer.h"

#include "DreamWidgetBlueprint.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamWidgetPlacement.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Preview/DreamWidgetPreviewScene.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "TextureResource.h"
#include "UObject/GCObject.h"
#include "UObject/Package.h"

namespace DreamWidgetBlueprintThumbnailLocal
{
	/**
	 * What the picture is cleared to before the screen is drawn over it. Opaque, so the picture goes onto the tile
	 * without blending; the render tests' backdrop, a dark screen, which a screen that draws nothing still reads as.
	 */
	static const FColor Backdrop(28, 30, 38, 255);

	/**
	 * The picture is drawn at twice the tile and halved on the way onto it. At tile size a 1920-wide screen is drawn at
	 * an eighth of its size, where a one-pixel line or the edge of a glyph is a coin toss; halving averages each 2x2
	 * block, which is what bilinear sampling exactly between four texels does.
	 */
	static constexpr int32 Supersample = 2;
	static constexpr int32 MaxPictureExtent = 1024;

	/**
	 * Manager ticks, each followed by the end-of-frame submit. The first builds the canvas's sections, but its mesh gets
	 * a scene proxy only in that submit's component updates -- after the draw, which then had nothing to draw but the
	 * clear colour. The second draws. The third is margin for a screen that asks for another pass as it settles.
	 */
	static constexpr int32 Passes = 3;
	static constexpr float PassDeltaSeconds = 1.0f / 30.0f;
}

/**
 * Where the thumbnails are drawn: a preview world of their own and one render target, both made on the first picture
 * and kept, since a folder of screens asks for one picture after another.
 *
 * Nothing of an asset is kept between pictures. Each one builds a design canvas and an instance under it, draws, and
 * takes both down again, so the world is empty while nobody asks -- and its manager is not ticked by the editor
 * (bShouldTickInEditor stays false), so an empty world costs nothing either.
 */
class FDreamWidgetThumbnailStage : public FGCObject
{
public:
	/**
	 * InBlueprint's screen, laid out at InCanvasSize and drawn into a target InPictureSize pixels big. The draw is
	 * enqueued for the render thread, so anything enqueued after this returns -- the thumbnail canvas's tile -- sees it
	 * finished. Null when nothing could be drawn: the engine is exiting, or the Blueprint has no class to instance.
	 */
	UTextureRenderTarget2D* Render(UDreamWidgetBlueprint* InBlueprint, FIntPoint InCanvasSize, FIntPoint InPictureSize);

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(Target);
	}
	virtual FString GetReferencerName() const override
	{
		return TEXT("FDreamWidgetThumbnailStage");
	}

private:
	UWorld* EnsureWorld();
	UTextureRenderTarget2D* EnsureTarget(FIntPoint InSize);

	TUniquePtr<FDreamWidgetPreviewScene> Scene;
	TObjectPtr<UTextureRenderTarget2D> Target = nullptr;
	/**
	 * A widget's construction can reach anything, the Content Browser included. A picture asked for from inside this
	 * one would build in the middle of a half-built screen; it is refused instead, and that tile falls back.
	 */
	bool bRendering = false;
};

UWorld* FDreamWidgetThumbnailStage::EnsureWorld()
{
	if (!Scene.IsValid())
	{
		// No lights, no physics, no undo: nothing here is lit or moves, and nothing done to a thumbnail is an edit.
		FDreamWidgetPreviewScene::ConstructionValues Values;
		Values.SetCreateDefaultLighting(false)
			.SetCreatePhysicsScene(false)
			.SetTransactional(false)
			.SetForceMipsResident(false);
		Scene = MakeUnique<FDreamWidgetPreviewScene>(Values);
	}
	// Null once the engine has begun to exit: the scene lets its world go then (OnEnginePreExit).
	return Scene->GetWorld();
}

UTextureRenderTarget2D* FDreamWidgetThumbnailStage::EnsureTarget(FIntPoint InSize)
{
	if (Target == nullptr)
	{
		Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->AddressX = TextureAddress::TA_Clamp;
		Target->AddressY = TextureAddress::TA_Clamp;
		Target->ClearColor = FLinearColor(DreamWidgetBlueprintThumbnailLocal::Backdrop);
	}
	if (Target->SizeX != InSize.X || Target->SizeY != InSize.Y || Target->GetResource() == nullptr)
	{
		// The format the render tests draw into: 8-bit sRGB, the colours a screen shows.
		Target->InitCustomFormat(static_cast<uint32>(InSize.X), static_cast<uint32>(InSize.Y), EPixelFormat::PF_B8G8R8A8, false);
		Target->UpdateResourceImmediate(true);
	}
	return Target;
}

UTextureRenderTarget2D* FDreamWidgetThumbnailStage::Render(UDreamWidgetBlueprint* InBlueprint, FIntPoint InCanvasSize, FIntPoint InPictureSize)
{
	using namespace DreamWidgetBlueprintThumbnailLocal;
	if (bRendering)
	{
		return nullptr;
	}
	TGuardValue<bool> Rendering(bRendering, true);

	UWorld* World = EnsureWorld();
	UDreamUIManagerWorldSubsystem* Manager = World != nullptr ? UDreamUIManagerWorldSubsystem::GetInstance(World) : nullptr;
	if (Manager == nullptr)
	{
		return nullptr;
	}
	UTextureRenderTarget2D* Picture = EnsureTarget(InPictureSize);

	// The design canvas, as the designer's agent is one (FDreamWidgetDesignerScene::EnsureRootAgent), but drawing into
	// the picture. The target is a render-target canvas's viewport, so its scaler matching the width to InCanvasSize
	// lays the root out at InCanvasSize whatever size the picture is.
	UDreamWidget* Root = NewObject<UDreamWidget>(World, NAME_None, RF_Transient);
	Root->SetWidth(static_cast<float>(InCanvasSize.X));
	Root->SetHeight(static_cast<float>(InCanvasSize.Y));
	Root->OnRegister();
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (Canvas == nullptr)
	{
		Root->DestroyWidget();
		return nullptr;
	}
	Canvas->SetRenderMode(EDreamRenderMode::RenderTarget);
	Canvas->SetRenderTargetClearColor(Backdrop);
	Canvas->SetRenderTargetResolutionScale(1.0f);
	Canvas->SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget);
	Canvas->SetRenderTargetUpdateMode(EDreamCanvasRenderTargetUpdateMode::Always);
	Canvas->SetScaleMode(EDreamCanvasScaleMode::ScaleWithScreenSize);
	Canvas->SetScreenMatchMode(EDreamCanvasScreenMatchMode::MatchWidthOrHeight);
	Canvas->SetMatchFromWidthToHeight(0.0f);
	Canvas->SetReferenceResolution(FVector2D(InCanvasSize));
	Canvas->SetRenderTarget(Picture);

	UDreamUserWidget* Preview = FDreamWidgetPreviewHost::InstancePreview(InBlueprint, Root);
	if (Preview != nullptr)
	{
		// What the preview host does after the same call: tell the canvas it has something to draw, and put away what
		// the author put away in the designer.
		UDreamUIManagerWorldSubsystem::RefreshAllUI(World);
		FDreamWidgetPreviewHost::ApplyHiddenInDesigner(InBlueprint, Preview->GetContentRoot());

		// Every glyph rasterised on the spot. A glyph handed to a worker lands after the picture is taken, and the
		// text is drawn without it.
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
			Manager->TickDreamUI(PassDeltaSeconds);
			World->SendAllEndOfFrameUpdates();
		}
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1);
	}

	// Taken down before the picture is even drawn: the draw is already enqueued, and the render thread holds what it
	// needs of the canvas's renderer. Deliberately not IsValid() -- see FDreamWidgetPreviewHost::DestroyPreview.
	if (Preview != nullptr && !Preview->HasAnyFlags(RF_FinishDestroyed))
	{
		Preview->DestroyWidget();
	}
	Root->DestroyWidget();
	return Preview != nullptr ? Picture : nullptr;
}

bool UDreamWidgetBlueprintThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	const UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(Object);
	if (Blueprint == nullptr)
	{
		return false;
	}
	// Nothing authored, here or in a parent class, means nothing to draw, and an empty frame says less than the generic
	// Blueprint icon the browser falls back to.
	const UDreamWidgetTree* Tree = FDreamWidgetPreviewHost::FindArchetypeForPreview(Blueprint);
	return IsValid(Tree) && IsValid(Tree->RootWidget);
}

EThumbnailRenderFrequency UDreamWidgetBlueprintThumbnailRenderer::GetThumbnailRenderFrequency(UObject* Object) const
{
	return EThumbnailRenderFrequency::OnPropertyChange;
}

void UDreamWidgetBlueprintThumbnailRenderer::BeginDestroy()
{
	// Where the engine's own renderers let their preview scenes go (UStaticMeshThumbnailRenderer::BeginDestroy).
	Stage.Reset();
	Super::BeginDestroy();
}

FBox2D UDreamWidgetBlueprintThumbnailRenderer::ResolveAnchoredRect(const FBox2D& InParentRect, const FDreamUIAnchorData& InAnchors)
{
	const FVector2D ParentSize = InParentRect.Max - InParentRect.Min;
	// The rect the anchors alone describe: a fraction of the parent on each axis.
	const FVector2D AnchorRefMin = InParentRect.Min + InAnchors.AnchorMin * ParentSize;
	const FVector2D AnchorRefMax = InParentRect.Min + InAnchors.AnchorMax * ParentSize;
	// SizeDelta grows the anchored span. With min == max (a point anchor) the span is zero and
	// SizeDelta IS the size, which is the case an author meets most often.
	const FVector2D Size = (AnchorRefMax - AnchorRefMin) + InAnchors.SizeDelta;
	// AnchoredPosition moves the PIVOT, and the pivot decides which part of the rect that names --
	// which is why the two are always read together (see FinishDesignerDrag's snapshot restore).
	const FVector2D PivotPosition = AnchorRefMin + (AnchorRefMax - AnchorRefMin) * InAnchors.Pivot + InAnchors.AnchoredPosition;
	const FVector2D Min = PivotPosition - InAnchors.Pivot * Size;
	FBox2D Result(Min, Min + Size);
	// A negative size is authored data the designer permits and a rect nobody can draw; normalising
	// it here keeps the wireframe readable rather than inside out.
	if (Result.Min.X > Result.Max.X) { Swap(Result.Min.X, Result.Max.X); }
	if (Result.Min.Y > Result.Max.Y) { Swap(Result.Min.Y, Result.Max.Y); }
	Result.bIsValid = true;
	return Result;
}

FBox2D UDreamWidgetBlueprintThumbnailRenderer::FitCanvasIntoThumbnail(const FBox2D& InThumbnailRect, FIntPoint InCanvasSize)
{
	const FVector2D Available = InThumbnailRect.Max - InThumbnailRect.Min;
	if (Available.X <= 0.0 || Available.Y <= 0.0 || InCanvasSize.X <= 0 || InCanvasSize.Y <= 0)
	{
		return InThumbnailRect;
	}
	// Letterboxed rather than stretched: the aspect ratio is half of what makes one screen
	// recognisable from another at tile size, and a 16:9 HUD squeezed into a square tile looks like
	// the 4:3 one next to it.
	const double CanvasAspect = (double)InCanvasSize.X / (double)InCanvasSize.Y;
	const double AvailableAspect = Available.X / Available.Y;
	FVector2D Size = Available;
	if (CanvasAspect > AvailableAspect)
	{
		Size.Y = Available.X / CanvasAspect;
	}
	else
	{
		Size.X = Available.Y * CanvasAspect;
	}
	const FVector2D Min = InThumbnailRect.Min + (Available - Size) * 0.5;
	FBox2D Result(Min, Min + Size);
	Result.bIsValid = true;
	return Result;
}

void UDreamWidgetBlueprintThumbnailRenderer::CollectWidgetRects(const UDreamWidget* InWidget, const FBox2D& InParentRect,
	int32 InDepth, TArray<TPair<FBox2D, int32>>& OutRects)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	// Four levels is what a 64-pixel tile can still show apart; below that the lines merge into a
	// smudge and each one costs a draw call per thumbnail the browser scrolls past.
	constexpr int32 MaxDepth = 4;
	constexpr int32 MaxRects = 64;
	if (InDepth > MaxDepth || OutRects.Num() >= MaxRects)
	{
		return;
	}
	const FBox2D Rect = ResolveAnchoredRect(InParentRect, InWidget->GetAnchorData());
	OutRects.Emplace(Rect, InDepth);
	for (const UDreamWidget* Child : InWidget->GetChildren())
	{
		CollectWidgetRects(Child, Rect, InDepth + 1, OutRects);
	}
}

void UDreamWidgetBlueprintThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
	FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(Object);
	if (Blueprint == nullptr || Canvas == nullptr || Width == 0 || Height == 0)
	{
		return;
	}
	const FBox2D ThumbnailRect(FVector2D(X, Y), FVector2D(X + (double)Width, Y + (double)Height));
	FIntPoint CanvasSize = Blueprint->DesignerData.CanvasSize;
	if (CanvasSize.X <= 0 || CanvasSize.Y <= 0)
	{
		// Never authored, or authored as zero. 16:9 rather than the tile's own shape, because an
		// unsaved designer state should not make a screen look like a different screen.
		CanvasSize = FIntPoint(1920, 1080);
	}
	const FBox2D CanvasRect = FitCanvasIntoThumbnail(ThumbnailRect, CanvasSize);
	if (!DrawPicture(Blueprint, CanvasSize, CanvasRect, Canvas))
	{
		DrawWireframe(Blueprint, CanvasRect, Canvas);
	}
}

bool UDreamWidgetBlueprintThumbnailRenderer::DrawPicture(UDreamWidgetBlueprint* InBlueprint, FIntPoint InCanvasSize, const FBox2D& InCanvasRect, FCanvas* InCanvas)
{
	using namespace DreamWidgetBlueprintThumbnailLocal;
	// Without a GPU nothing is drawn, and a commandlet saving packages has none.
	if (!FApp::CanEverRender())
	{
		return false;
	}
	// A class mid-compile, or one a newer compile has replaced, is not the screen the asset describes. A class whose
	// last compile failed is still drawn, as the designer draws it: what it last managed to build.
	UClass* GeneratedClass = InBlueprint->GeneratedClass;
	if (GeneratedClass == nullptr || InBlueprint->bBeingCompiled || !InBlueprint->bHasBeenRegenerated
		|| GeneratedClass->HasAnyClassFlags(CLASS_NewerVersionExists)
		|| !GeneratedClass->IsChildOf(UDreamUserWidget::StaticClass()))
	{
		return false;
	}

	// Whole pixels, so that halving lands each pixel of the tile exactly on a 2x2 block of the picture.
	const FIntPoint DrawMin(FMath::RoundToInt32(InCanvasRect.Min.X), FMath::RoundToInt32(InCanvasRect.Min.Y));
	const FIntPoint DrawMax(FMath::RoundToInt32(InCanvasRect.Max.X), FMath::RoundToInt32(InCanvasRect.Max.Y));
	const FIntPoint DrawSize(FMath::Max(1, DrawMax.X - DrawMin.X), FMath::Max(1, DrawMax.Y - DrawMin.Y));
	FIntPoint PictureSize = DrawSize * Supersample;
	const int32 Largest = FMath::Max(PictureSize.X, PictureSize.Y);
	if (Largest > MaxPictureExtent)
	{
		// Only for a thumbnail asked for far bigger than the browser's; that picture is just softer.
		PictureSize.X = FMath::Max(1, PictureSize.X * MaxPictureExtent / Largest);
		PictureSize.Y = FMath::Max(1, PictureSize.Y * MaxPictureExtent / Largest);
	}

	if (!Stage.IsValid())
	{
		Stage = MakeShared<FDreamWidgetThumbnailStage>();
	}
	UTextureRenderTarget2D* Picture = Stage->Render(InBlueprint, InCanvasSize, PictureSize);
	if (Picture == nullptr || Picture->GetResource() == nullptr)
	{
		return false;
	}
	// Opaque: the picture was cleared to an opaque backdrop and the screen drawn over it.
	InCanvas->DrawTile(DrawMin.X, DrawMin.Y, DrawSize.X, DrawSize.Y, 0.0f, 0.0f, 1.0f, 1.0f,
		FLinearColor::White, Picture->GetResource(), false);
	return true;
}

void UDreamWidgetBlueprintThumbnailRenderer::DrawWireframe(UDreamWidgetBlueprint* InBlueprint, const FBox2D& InCanvasRect, FCanvas* InCanvas)
{
	// The canvas itself, filled, so the tile reads as a screen rather than as a floating diagram.
	InCanvas->DrawTile(InCanvasRect.Min.X, InCanvasRect.Min.Y,
		InCanvasRect.Max.X - InCanvasRect.Min.X, InCanvasRect.Max.Y - InCanvasRect.Min.Y,
		0.0f, 0.0f, 1.0f, 1.0f, FLinearColor(0.07f, 0.08f, 0.10f, 1.0f));

	TArray<TPair<FBox2D, int32>> Rects;
	if (IsValid(InBlueprint->WidgetTree))
	{
		CollectWidgetRects(InBlueprint->WidgetTree->RootWidget, InCanvasRect, 0, Rects);
	}
	for (const TPair<FBox2D, int32>& Entry : Rects)
	{
		const FBox2D& Rect = Entry.Key;
		// Clipped to the canvas: a widget authored outside its screen is legitimate (an off-screen
		// panel waiting to slide in) and would otherwise draw over the neighbouring tile.
		const FVector2D Min(FMath::Max(Rect.Min.X, InCanvasRect.Min.X), FMath::Max(Rect.Min.Y, InCanvasRect.Min.Y));
		const FVector2D Max(FMath::Min(Rect.Max.X, InCanvasRect.Max.X), FMath::Min(Rect.Max.Y, InCanvasRect.Max.Y));
		if (Max.X - Min.X < 1.0 || Max.Y - Min.Y < 1.0)
		{
			continue;
		}
		// Deeper is dimmer, so nesting is visible at a glance instead of every rect competing.
		const float Fade = FMath::Clamp(1.0f - Entry.Value * 0.18f, 0.35f, 1.0f);
		const FLinearColor LineColor(0.35f * Fade, 0.62f * Fade, 0.95f * Fade, 1.0f);
		const FVector2D Corners[4] = { Min, FVector2D(Max.X, Min.Y), Max, FVector2D(Min.X, Max.Y) };
		for (int32 Corner = 0; Corner < 4; Corner++)
		{
			FCanvasLineItem Line(Corners[Corner], Corners[(Corner + 1) % 4]);
			Line.SetColor(LineColor);
			Line.LineThickness = 1.0f;
			InCanvas->DrawItem(Line);
		}
	}
}
