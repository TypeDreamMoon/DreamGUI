// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/DreamText.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"
#include "Materials/MaterialInterface.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUIRichTextImageData_BaseObject.h"
#include "Core/DreamUIRichTextCustomStyleData.h"
#include "Core/DreamUIFontEmojiData.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayoutFragment.h"
#include "Core/Components/DreamWidget.h"
#include "Utils/DreamUIUtils.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "UObject/Package.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIWorldContext.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/TextTransformer.h"
#include "UObject/ObjectKey.h"
#include "UObject/UObjectIterator.h"
#include "DreamGUI.h"
#include "Core/DreamGradientAsset.h"
#include "Core/Text/DreamTextPaint.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialParameters.h"


#define LOCTEXT_NAMESPACE "UIText"

namespace DreamTextSmallTextLocal
{
	/**
	 * S5: how many sweeps in a row a text must hold still for before it draws from coverage again -- its device scale the
	 * same, and, when a move of it was only noted (GetRepaintsOnTransformChange), its transform not changed at all.
	 */
	constexpr int32 SettleSweeps = 3;
	/** S5: how far S may stray from the raster scale, as a fraction, before coverage glyphs are made again for it. */
	constexpr float RasterScaleHysteresis = 0.01f;
	/** Two device scales this close, as a fraction, are the same scale: what transform arithmetic leaves of an unchanged one. */
	constexpr float SameScaleTolerance = 1.0e-5f;
	/** A shift of the device grid this close to whole pixels is a whole-pixel one: well below what a pixel's coverage shows. */
	constexpr float WholePixelTolerance = 1.0f / 256.0f;
	/** S3: the frames a render layer must have held still for before the small text in it draws from coverage again. */
	constexpr uint64 LayerSettleFrames = 3;

	TAutoConsoleVariable<int32> CVarDreamTextSmallTextOnMove(
		TEXT("DreamGUI.Text.SmallTextOnMove"),
		0,
		TEXT("What small text drawn from coverage glyphs does when it moves off their device pixel grid -- slid by part of a pixel, ")
		TEXT("scaled, turned. 0: it is repainted from coverage at every move. 1: its coverage quads are kept as they are while it ")
		TEXT("moves, and it is repainted once it has held still for 3 frames. 2: it is repainted once from the field, and from ")
		TEXT("coverage once it has held still for 3 frames. A render layer's moves are the layer's, not the text's: its small text ")
		TEXT("is on the field until the layer has held still for 3 frames, whatever this says."),
		ECVF_Default);

	/** DreamGUI.Text.SmallTextOnMove, 0 to 2. */
	int32 GetSmallTextOnMove()
	{
		return FMath::Clamp(CVarDreamTextSmallTextOnMove.GetValueOnGameThread(), 0, 2);
	}

	bool IsSameScale(float A, float B)
	{
		return FMath::Abs(A - B) <= SameScaleTolerance * FMath::Max(FMath::Abs(A), FMath::Abs(B));
	}

	bool IsWholePixel(float InShift)
	{
		return FMath::Abs(InShift - FMath::RoundToFloat(InShift)) <= WholePixelTolerance;
	}

	/** Where a text's device pixel grid lies, when it can be placed on one: FDreamTextCoverageParams' DeviceScale, SnapOrigin and bLinearTarget. */
	struct FPlacement
	{
		float DeviceScale = 0.0f;
		FVector2f SnapOrigin = FVector2f::ZeroVector;
		bool bLinearTarget = false;
	};

	/** How far, in device pixels across the widget, the one S the painter uses for both axes may miss the canvas's own. */
	constexpr double MaxUnevenGridDrift = 0.125;

	/**
	 * Whether a widget's root canvas renders into something with a device pixel grid -- a render target, or the screen at
	 * the screen's resolution -- and if so, which canvas and which of the two. NoCanvas, WorldSpace or RenderScale when not.
	 * The first thing the gate asks, being the cheapest: a wall of world-space panels gets no further.
	 */
	EDreamTextSmallTextGate GetCanvasGate(const UDreamWidget* InWidget, UDreamCanvas*& OutRootCanvas, bool& bOutTarget)
	{
		UDreamCanvas* RenderCanvas = InWidget != nullptr ? InWidget->GetRenderCanvas() : nullptr;
		UDreamCanvas* RootCanvas = RenderCanvas != nullptr ? RenderCanvas->GetRootCanvas() : nullptr;
		if (RootCanvas == nullptr || RootCanvas->GetWidget() == nullptr)
		{
			return EDreamTextSmallTextGate::NoCanvas;
		}
		const EDreamRenderMode RenderMode = RootCanvas->GetRenderMode();
		const bool bTarget = RenderMode == EDreamRenderMode::RenderTarget;
		// An editor world draws a screen-space canvas in the level, through the editor's camera (UDreamCanvas::UpdateRootCanvas).
		const bool bScreen = RenderMode == EDreamRenderMode::ScreenSpaceOverlay && RootCanvas->IsInGameWorld();
		if (!bScreen && !bTarget)
		{
			return EDreamTextSmallTextGate::WorldSpace;
		}
		// A screen-space UI drawn at a fraction of the screen's resolution goes into a smaller target scaled up afterwards, or
		// not: the renderer decides that on its own thread (MSAA, depth testing and post processes refuse it), so there is no
		// pixel grid to be sure of.
		if (bScreen && RootCanvas->GetScreenSpaceRenderScale() < 1.0f)
		{
			return EDreamTextSmallTextGate::RenderScale;
		}
		OutRootCanvas = RootCanvas;
		bOutTarget = bTarget;
		return EDreamTextSmallTextGate::Coverage;
	}

	/**
	 * A text in a render layer is drawn through the layer's row, which is written from where the layer stands when the frame
	 * is submitted: in a layer that holds still it is on the device grid its own world transform says, as any text is. In
	 * one that moved within the last LayerSettleFrames frames it waits on the field (RenderLayerMoving), so that a slide or a
	 * scale tween does not rasterize it again at every step; the layer wakes it once it has held still.
	 */
	EDreamTextSmallTextGate GetRenderLayerGate(const UDreamWidget* InWidget)
	{
		const UDreamWidget* Layer = InWidget->GetRenderLayer();
		if (Layer != nullptr && Layer->GetRenderLayerMovedFrame() != 0 && GFrameCounter - Layer->GetRenderLayerMovedFrame() < LayerSettleFrames)
		{
			return EDreamTextSmallTextGate::RenderLayerMoving;
		}
		return EDreamTextSmallTextGate::Coverage;
	}

	/**
	 * Whether a widget of InRootCanvas -- one GetCanvasGate let through -- is drawn straight onto the pixels of what the
	 * canvas renders into: no perspective or shear, and relative to the canvas's widget nothing but a uniform scale and a
	 * move in the canvas's plane (UDreamCanvas::Is2DUITransform's flatness, with roll, mirroring and uneven scale ruled out
	 * as well) -- and if so, where its device grid lies. That is measured rather than assumed: the widget's local origin and
	 * its unit steps right and up are taken through the matrices the renderer draws the canvas with onto the target's pixels
	 * (the render target's size, or the viewport's). So S and the origin hold whatever the canvas makes of its units -- its
	 * canvas scale, a render target's resolution scale, a canvas size the projection rounds to whole units, a scaler whose
	 * reported scale is not the one on screen -- and, the widget's world transform holding its render layers' places, a
	 * layer's too. Coverage when it can be placed.
	 */
	EDreamTextSmallTextGate MeasureDeviceGrid(const UDreamWidget* InWidget, UDreamCanvas* InRootCanvas, bool bInTarget, FPlacement& OutPlacement)
	{
		UDreamCanvas* const RootCanvas = InRootCanvas;
		const UDreamWidget* CanvasWidget = RootCanvas->GetWidget();
		const bool bTarget = bInTarget;
		// Both draw through a matrix the world transform does not hold.
		if (InWidget->HasPerspectiveApplied() || InWidget->HasShearApplied())
		{
			return EDreamTextSmallTextGate::Transform;
		}
		const FTransform ToCanvas = InWidget->GetWorldTransform() * CanvasWidget->GetWorldTransform().Inverse();
		// The widget's right and up in the canvas's space: a uniform scale k on the canvas's own right and up, nothing else.
		const FVector Right = ToCanvas.TransformVector(FVector(0.0, 1.0, 0.0));
		const FVector Up = ToCanvas.TransformVector(FVector(0.0, 0.0, 1.0));
		const double Scale = 0.5 * (Right.Y + Up.Z);
		// The angle Is2DUITransform lets pass for flat, as a part of k; the same for a roll and for uneven scale.
		const double Threshold = UDreamUISettings::GetAutoBatchThreshold();
		const double OffAxis = FMath::Sin(FMath::DegreesToRadians(Threshold)) * FMath::Abs(Scale);
		if (FMath::Abs(ToCanvas.GetLocation().X) > Threshold
			|| Right.Y <= 0.0 || Up.Z <= 0.0
			|| FMath::Abs(Right.X) > OffAxis || FMath::Abs(Up.X) > OffAxis
			|| FMath::Abs(Right.Z) > OffAxis || FMath::Abs(Up.Y) > OffAxis
			|| FMath::Abs(Right.Y - Up.Z) > OffAxis)
		{
			return EDreamTextSmallTextGate::Transform;
		}

		// What the canvas renders into, in pixels. A render target that follows the canvas, or is not made yet, is the
		// canvas's size times its resolution scale (UDreamCanvas::UpdateRenderTarget makes it so before it draws); one the
		// canvas follows is its own size. The screen is the viewport the canvas sizes itself from, at the screen's resolution
		// (GetCanvasGate saw to that).
		FIntPoint TargetPixels = FIntPoint::ZeroValue;
		if (bTarget)
		{
			const UTextureRenderTarget2D* Target = RootCanvas->GetRenderTarget();
			if (IsValid(Target) && RootCanvas->GetRenderTargetSizeMode() != EDreamCanvasRenderTargetSizeMode::RenderTargetFitToCanvas)
			{
				TargetPixels = FIntPoint((int32)Target->SizeX, (int32)Target->SizeY);
			}
			else
			{
				const float ResolutionScale = RootCanvas->GetRenderTargetResolutionScale();
				TargetPixels = FIntPoint(FMath::TruncToInt32(CanvasWidget->GetWidth() * ResolutionScale),
					FMath::TruncToInt32(CanvasWidget->GetHeight() * ResolutionScale));
			}
		}
		else
		{
			TargetPixels = RootCanvas->GetViewportSize();
		}
		if (TargetPixels.X <= 0 || TargetPixels.Y <= 0)
		{
			return EDreamTextSmallTextGate::NoCanvas;
		}

		// Through the matrices the renderer draws the canvas with (FDreamUIRenderer::UpdateViewParameter_GameThread), from the
		// uncached getters as it reads them, onto the target's pixels: u from its left edge rightward, v from its top edge
		// upward, so v is negative inside it.
		const FMatrix ViewRotation = FInverseRotationMatrix(RootCanvas->GetViewRotator()) * FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));
		const FMatrix WidgetToClip = InWidget->GetWorldTransform().ToMatrixWithScale()
			* FTranslationMatrix(-RootCanvas->GetViewLocation()) * ViewRotation * RootCanvas->GetProjectionMatrix();
		const FVector2D TargetSize((double)TargetPixels.X, (double)TargetPixels.Y);
		auto ToDevice = [&WidgetToClip, &TargetSize](double InX, double InY, FVector2D& OutDevice)
		{
			const FVector4 Clip = WidgetToClip.TransformFVector4(FVector4(0.0, InX, InY, 1.0));
			if (!(Clip.W > UE_SMALL_NUMBER))
			{
				return false;
			}
			OutDevice = FVector2D((Clip.X / Clip.W + 1.0) * 0.5 * TargetSize.X, (Clip.Y / Clip.W - 1.0) * 0.5 * TargetSize.Y);
			return true;
		};
		FVector2D Origin = FVector2D::ZeroVector;
		FVector2D AlongRight = FVector2D::ZeroVector;
		FVector2D AlongUp = FVector2D::ZeroVector;
		if (!ToDevice(0.0, 0.0, Origin) || !ToDevice(1.0, 0.0, AlongRight) || !ToDevice(0.0, 1.0, AlongUp))
		{
			return EDreamTextSmallTextGate::Transform;
		}
		const FVector2D PerRight = AlongRight - Origin;
		const FVector2D PerUp = AlongUp - Origin;
		// The painter places both axes with one S, the horizontal one, where the phases are. A canvas whose projection rounds
		// its size to whole units draws a unit a hair taller than wide, which is harmless while it adds up to less than an
		// eighth of a pixel across the widget -- and so is a roll that small.
		const double ReachX = FMath::Max(FMath::Abs((double)InWidget->GetLocalSpaceLeft()), FMath::Abs((double)InWidget->GetLocalSpaceRight()));
		const double ReachY = FMath::Max(FMath::Abs((double)InWidget->GetLocalSpaceTop()), FMath::Abs((double)InWidget->GetLocalSpaceBottom()));
		if (!(PerRight.X > UE_KINDA_SMALL_NUMBER) || !(PerUp.Y > UE_KINDA_SMALL_NUMBER)
			|| FMath::Abs(PerUp.Y - PerRight.X) * ReachY > MaxUnevenGridDrift
			|| FMath::Abs(PerRight.Y) * ReachX > MaxUnevenGridDrift || FMath::Abs(PerUp.X) * ReachY > MaxUnevenGridDrift)
		{
			return EDreamTextSmallTextGate::Transform;
		}
		OutPlacement.DeviceScale = (float)PerRight.X;
		// u = S x + SnapOrigin.X, v = S y + SnapOrigin.Y: the widget's local origin is where it lands.
		OutPlacement.SnapOrigin = FVector2f((float)Origin.X, (float)Origin.Y);
		OutPlacement.bLinearTarget = bTarget;
		return EDreamTextSmallTextGate::Coverage;
	}

	/**
	 * Whether a widget is drawn straight onto the device pixels of what its root canvas renders into, and if so where its
	 * device grid lies: the canvas (GetCanvasGate), its render layer (GetRenderLayerGate), then the measurement
	 * (MeasureDeviceGrid). Counted as a placement (SmallTextPlacements).
	 */
	EDreamTextSmallTextGate PlaceOnDeviceGrid(const UDreamWidget* InWidget, FPlacement& OutPlacement)
	{
		DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::SmallTextPlacements, 1);
		UDreamCanvas* RootCanvas = nullptr;
		bool bTarget = false;
		EDreamTextSmallTextGate Gate = GetCanvasGate(InWidget, RootCanvas, bTarget);
		if (Gate == EDreamTextSmallTextGate::Coverage)
		{
			Gate = GetRenderLayerGate(InWidget);
		}
		if (Gate == EDreamTextSmallTextGate::Coverage)
		{
			Gate = MeasureDeviceGrid(InWidget, RootCanvas, bTarget, OutPlacement);
		}
		return Gate;
	}

	/**
	 * A render layer's own place on the device grid, which the coverage quads of the small text in it are measured against
	 * when it moves: as PlaceOnDeviceGrid, the layer itself not asked about.
	 */
	EDreamTextSmallTextGate PlaceLayerOnDeviceGrid(const UDreamWidget* InLayer, FPlacement& OutPlacement)
	{
		DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::SmallTextPlacements, 1);
		UDreamCanvas* RootCanvas = nullptr;
		bool bTarget = false;
		const EDreamTextSmallTextGate Gate = GetCanvasGate(InLayer, RootCanvas, bTarget);
		return Gate == EDreamTextSmallTextGate::Coverage ? MeasureDeviceGrid(InLayer, RootCanvas, bTarget, OutPlacement) : Gate;
	}

	/** A move from InFrom to InTo keeps quads placed at InFrom on the device grid: the same scale, a whole-pixel shift. */
	bool IsWholePixelMove(const FPlacement& InFrom, const FPlacement& InTo)
	{
		const FVector2f Shift = InTo.SnapOrigin - InFrom.SnapOrigin;
		return IsSameScale(InTo.DeviceScale, InFrom.DeviceScale) && IsWholePixel(Shift.X) && IsWholePixel(Shift.Y);
	}

	/**
	 * Whether a material shades through MF_DreamUI_Shade -- DreamUIShade.ush, which alone knows coverage glyphs, colour glyphs
	 * and paints: the shading's marker parameter is there, above 0.5, as the material's default value says, which a cooked
	 * game reads as an editor does. Anything else may read the quads any way it likes. Kept for the frame: every text a
	 * material draws asks at every paint, and a material edited in the editor is asked again the frame after.
	 */
	bool HasShadeMarker(const UMaterialInterface* InMaterial)
	{
		static TMap<TObjectKey<UMaterialInterface>, bool> Answers;
		static uint64 AnswersFrame = 0;
		if (AnswersFrame != GFrameCounter)
		{
			AnswersFrame = GFrameCounter;
			Answers.Reset();
		}
		const TObjectKey<UMaterialInterface> Key(InMaterial);
		if (const bool* Known = Answers.Find(Key))
		{
			return *Known;
		}
		float Marker = 0.0f;
		const bool bShades = InMaterial->GetScalarParameterDefaultValue(FHashedMaterialParameterInfo(DreamUIShadeMaterial::ShadeMarkerParameter), Marker)
			&& Marker > 0.5f;
		Answers.Add(Key, bShades);
		return bShades;
	}

	/**
	 * Pixel snapping as the gate reads it: Disabled anywhere up the chain, before a widget that says SnapToPixel, rules
	 * coverage out; Inherit all the way to the root does not.
	 */
	bool IsSnappingAllowed(const UDreamWidget* InWidget)
	{
		constexpr int32 MaxDepth = 1024;
		int32 Depth = 0;
		for (const UDreamWidget* Widget = InWidget; Widget != nullptr && Depth < MaxDepth; Widget = Widget->GetParent(), ++Depth)
		{
			switch (Widget->GetPixelSnapping())
			{
			case EWidgetPixelSnapping::SnapToPixel:
				return true;
			case EWidgetPixelSnapping::Disabled:
				return false;
			default:
				break;
			}
		}
		return true;
	}

	/**
	 * The smallest glyph of a display list the painter would look for a coverage glyph for, by its GlyphSize in the text's
	 * units: an emitted glyph, or one whose field glyph is still being made (coverage may draw it meanwhile); never a colour
	 * glyph. MAX_flt when there is none.
	 */
	float GetSmallestCoverageGlyphSize(const FDreamTextDisplayList& InDisplayList)
	{
		float Smallest = MAX_flt;
		for (const FDreamTextGlyphItem& Item : InDisplayList.Items)
		{
			if (Item.Kind != EDreamTextItemKind::Glyph || Item.Glyph.bColor || Item.GlyphSize <= 0.0f
				|| !(Item.bEmit || (Item.bCountsAsVisible && Item.Glyph.bPending)))
			{
				continue;
			}
			Smallest = FMath::Min(Smallest, Item.GlyphSize);
		}
		return Smallest;
	}

	/** A render layer with small text in it that draws from coverage glyphs, or waits for the layer to hold still. */
	struct FLayerTexts
	{
		/** The layer's own place on the device grid when its first holder since its last move off their grid was painted; bPlaced false when it could not be placed. */
		FPlacement Placement;
		bool bPlaced = false;
		/**
		 * Texts that hear of its moves (each one's SmallTextLayerIndex is its place here): drawing coverage glyphs in it, or kept
		 * on the field by its turn or its scale. All repainted when it moves anywhere but by whole pixels at the same scale.
		 */
		TArray<TWeakObjectPtr<UDreamText>> Holders;
		/** Texts on the field because it moved: repainted once it has held still for LayerSettleFrames frames. */
		TArray<TWeakObjectPtr<UDreamText>> Waiting;
	};

	/**
	 * One world's small text that is watched rather than measured: its texts waiting for their device scale to settle or for
	 * a move to end, the render layers with such text in them, and the binding of its sweep while any of it is to be swept.
	 */
	struct FSharpenSet
	{
		TWeakObjectPtr<UDreamUIManagerWorldSubsystem> Manager;
		/** Each one's SmallTextSharpenIndex is its place here. */
		TArray<TWeakObjectPtr<UDreamText>> Texts;
		TMap<TObjectKey<UDreamWidget>, FLayerTexts> Layers;
		FDelegateHandle SweepHandle;
	};

	/** The sharpen sets, one per world's UI manager. Game thread only, as painting is. */
	TMap<TObjectKey<UDreamUIManagerWorldSubsystem>, FSharpenSet>& GetSharpenSets()
	{
		static TMap<TObjectKey<UDreamUIManagerWorldSubsystem>, FSharpenSet> Sets;
		return Sets;
	}

	FSharpenSet* FindSharpenSet(UDreamUIManagerWorldSubsystem* InManager)
	{
		return InManager != nullptr ? GetSharpenSets().Find(TObjectKey<UDreamUIManagerWorldSubsystem>(InManager)) : nullptr;
	}

	FSharpenSet& FindOrAddSharpenSet(UDreamUIManagerWorldSubsystem* InManager)
	{
		TMap<TObjectKey<UDreamUIManagerWorldSubsystem>, FSharpenSet>& Sets = GetSharpenSets();
		const TObjectKey<UDreamUIManagerWorldSubsystem> Key(InManager);
		if (FSharpenSet* Found = Sets.Find(Key))
		{
			return *Found;
		}
		// A world torn down while texts waited in it leaves its set behind; the sets are few, and looked over when one is made.
		for (auto It = Sets.CreateIterator(); It; ++It)
		{
			if (!It->Value.Manager.IsValid())
			{
				It.RemoveCurrent();
			}
		}
		FSharpenSet& Set = Sets.Add(Key);
		Set.Manager = InManager;
		return Set;
	}

	/**
	 * InManager's set, when nothing in it is left to sweep, unbinds its sweep -- from inside its own broadcast, should the
	 * sweep be what emptied it, which the delegate allows -- and with nothing left in it at all, goes: the manager's tick
	 * goes back to costing nothing for it. No pointer into the set is to be used after this.
	 */
	void TidySharpenSet(UDreamUIManagerWorldSubsystem* InManager)
	{
		TMap<TObjectKey<UDreamUIManagerWorldSubsystem>, FSharpenSet>& Sets = GetSharpenSets();
		const TObjectKey<UDreamUIManagerWorldSubsystem> Key(InManager);
		FSharpenSet* Set = InManager != nullptr ? Sets.Find(Key) : nullptr;
		if (Set == nullptr)
		{
			return;
		}
		bool bToSweep = Set->Texts.Num() > 0;
		for (const TPair<TObjectKey<UDreamWidget>, FLayerTexts>& Record : Set->Layers)
		{
			bToSweep |= Record.Value.Waiting.Num() > 0;
		}
		if (bToSweep)
		{
			return;
		}
		if (Set->SweepHandle.IsValid())
		{
			InManager->GetOnBeforeRootCanvasesUpdate().Remove(Set->SweepHandle);
			Set->SweepHandle.Reset();
		}
		if (Set->Layers.Num() == 0)
		{
			Sets.Remove(Key);
		}
	}

	/**
	 * Take InText off InList, from InIndex, where it was put: the last entry is swapped into its place, and that one's own
	 * record of where it stands written by InSetIndex. Should it not be found there, it is looked for.
	 */
	void RemoveFromTextList(TArray<TWeakObjectPtr<UDreamText>>& InList, int32 InIndex, const TWeakObjectPtr<UDreamText>& InText,
		TFunctionRef<void(UDreamText&, int32)> InSetIndex)
	{
		int32 Found = InList.IsValidIndex(InIndex) && InList[InIndex].HasSameIndexAndSerialNumber(InText) ? InIndex : INDEX_NONE;
		if (Found == INDEX_NONE)
		{
			Found = InList.IndexOfByPredicate([&InText](const TWeakObjectPtr<UDreamText>& InEntry) { return InEntry.HasSameIndexAndSerialNumber(InText); });
		}
		if (Found == INDEX_NONE)
		{
			return;
		}
		InList.RemoveAtSwap(Found, EAllowShrinking::No);
		if (Found < InList.Num())
		{
			if (UDreamText* Moved = InList[Found].Get())
			{
				InSetIndex(*Moved, Found);
			}
		}
	}
}

namespace DreamTextComponentPaintLocal
{
	/** The tag paint slots there are: FirstTagSlot to MaxSlot. */
	constexpr int32 TagSlotCount = DreamTextQuadCode::MaxSlot - DreamTextQuadCode::FirstTagSlot + 1;

	/** Whether two styles differ in nothing but their paints, the overlay's blend and the paint boxes. */
	bool IsSameButPaints(const FDreamTextStyle& InA, const FDreamTextStyle& InB)
	{
		FDreamTextStyle WithPaintsOfB = InA;
		WithPaintsOfB.FacePaint = InB.FacePaint;
		WithPaintsOfB.OutlinePaint = InB.OutlinePaint;
		WithPaintsOfB.OverlayPaint = InB.OverlayPaint;
		WithPaintsOfB.OverlayBlend = InB.OverlayBlend;
		WithPaintsOfB.PaintBoxHorizontal = InB.PaintBoxHorizontal;
		WithPaintsOfB.PaintBoxVertical = InB.PaintBoxVertical;
		return WithPaintsOfB == InB;
	}

	/** A slot's box aspect (FDreamTextPaintSlot::BoxAspect): its box's width over its height when both are single boxes, else 1. */
	float GetBoxAspect(const FDreamTextDisplayList& InDisplayList, EDreamTextPaintBox InHorizontal, EDreamTextPaintBox InVertical)
	{
		auto IsSingleBox = [](EDreamTextPaintBox InBox) { return InBox == EDreamTextPaintBox::TextBlock || InBox == EDreamTextPaintBox::ContentBox; };
		if (!IsSingleBox(InHorizontal) || !IsSingleBox(InVertical))
		{
			return 1.0f;
		}
		const float Width = (InHorizontal == EDreamTextPaintBox::TextBlock ? InDisplayList.TextBlockBox : InDisplayList.ContentBox).GetWidth();
		const float Height = (InVertical == EDreamTextPaintBox::TextBlock ? InDisplayList.TextBlockBox : InDisplayList.ContentBox).GetHeight();
		return Width > UE_KINDA_SMALL_NUMBER && Height > UE_KINDA_SMALL_NUMBER ? Width / Height : 1.0f;
	}

	/**
	 * A number of the paint table as the shader reads it, which guards against no NaN or infinity -- a phase or a scale a
	 * script or a tween drove there: InFallback, the table's neutral value, in its place.
	 */
	float FiniteOr(float InValue, float InFallback)
	{
		return FMath::IsFinite(InValue) ? InValue : InFallback;
	}

	/** InLayer's gradient row given back to InManager, the one it came from. */
	void ReleaseLayerRow(FDreamTextPaintLayerState& InLayer, UDreamUIManagerWorldSubsystem* InManager)
	{
		if (InLayer.Row != INDEX_NONE)
		{
			if (InManager != nullptr)
			{
				InManager->ReleasePaintGradientRow(InLayer.Row);
			}
			InLayer.Row = INDEX_NONE;
		}
	}

	/**
	 * InLayer brought to paint with InWanted, null for not at all. With bInRows it holds InManager's row for its gradient,
	 * and without, none. Another gradient than the one its row holds takes another row: the old one is given back once the
	 * new one is taken, so it is never written again under the texts sharing it, nor handed back as the new one.
	 */
	void SetPaintLayer(FDreamTextPaintLayerState& InLayer, const FDreamGradient* InWanted, UDreamUIManagerWorldSubsystem* InManager, bool bInRows)
	{
		if (InWanted == nullptr)
		{
			ReleaseLayerRow(InLayer, InManager);
			if (InLayer.bPainting)
			{
				InLayer.bPainting = false;
				InLayer.Gradient = FDreamGradient();
			}
			return;
		}
		int32 OldRow = INDEX_NONE;
		if (!InLayer.bPainting || InLayer.Gradient != *InWanted)
		{
			OldRow = InLayer.Row;
			InLayer.Row = INDEX_NONE;
			// Not a copy of itself: a layer handed its own gradient back keeps it as it is.
			if (&InLayer.Gradient != InWanted)
			{
				InLayer.Gradient = *InWanted;
			}
		}
		InLayer.bPainting = true;
		if (!bInRows)
		{
			ReleaseLayerRow(InLayer, InManager);
		}
		else if (InLayer.Row == INDEX_NONE && InManager != nullptr)
		{
			InLayer.Row = InManager->AcquirePaintGradientRow(InLayer.Gradient);
		}
		if (OldRow != INDEX_NONE && InManager != nullptr)
		{
			InManager->ReleasePaintGradientRow(OldRow);
		}
	}

	/** Whether a layer paints for the painter: it paints, through a row it holds -- or through none, painted in vertex colours. */
	bool IsLayerPainted(const FDreamTextPaintLayerState& InLayer, bool bInVertexColorFallback)
	{
		return InLayer.bPainting && (bInVertexColorFallback || InLayer.Row != INDEX_NONE);
	}

	/** What the painter is handed of a text's paints, in a word: which layers paint, and whether in vertex colours. */
	uint32 GetPaintedLayersMask(const FDreamTextPaintState& InState)
	{
		const bool bFallback = InState.bVertexColorFallback;
		const bool bTable = bFallback || InState.TextRow != INDEX_NONE;
		uint32 Mask = bFallback ? 1u : 0u;
		for (int32 Layer = 0; Layer < 3; ++Layer)
		{
			Mask |= bTable && IsLayerPainted(InState.Own[Layer], bFallback) ? 1u << (1 + Layer) : 0u;
		}
		for (int32 Tag = 0; Tag < TagSlotCount; ++Tag)
		{
			Mask |= bTable && IsLayerPainted(InState.Tags[Tag], bFallback) ? 1u << (4 + Tag) : 0u;
		}
		return Mask;
	}
}

FDreamTextLayoutInput UDreamText::MakeLayoutInput(const UDreamText* Text, float InFontSize)
{
	auto Widget = Text->GetWidget();
	auto RenderCanvas = Widget->GetRenderCanvas();
	auto RootCanvas = RenderCanvas ? RenderCanvas->GetRootCanvas() : nullptr;

	FDreamTextLayoutInput Input;
	Input.Content = Text->GetText().ToString();
	// Case is presentation: the Text property keeps what was authored, so a copy and anything reading the
	// text back still see it. UMG's TextBlock transforms at the same point, and the same way: through ICU,
	// in the current culture, which is what turns a German sharp s into "SS" and knows Turkish's dotted and
	// dotless i. FString's own ToUpper maps one code unit to one and gets both wrong. A mapping that changes
	// the length (the sharp s to "SS") lays out a longer string than the authored one, and its carets count
	// in that string.
	switch (Text->GetTextTransform())
	{
	case EDreamUITextTransformPolicy::ToLower: Input.Content = FTextTransformer::ToLower(Input.Content); break;
	case EDreamUITextTransformPolicy::ToUpper: Input.Content = FTextTransformer::ToUpper(Input.Content); break;
	default: break;
	}
	FVector2f ContentSize, ContentPivot;
	UDreamText::GetContentBox(FVector2f(Widget->GetWidth(), Widget->GetHeight()), FVector2f(Widget->GetPivot()), Text->GetMargin(),
		ContentSize, ContentPivot);
	Input.Width = ContentSize.X;
	Input.Height = ContentSize.Y;
	Input.Pivot = ContentPivot;
	Input.Color = Text->GetFinalColor();
	Input.FontSpace = FVector2f(Text->GetFontSpace());
	Input.FontSize = InFontSize;
	Input.ParagraphHAlign = Text->GetParagraphHorizontalAlignment();
	Input.ParagraphVAlign = Text->GetParagraphVerticalAlignment();
	Input.OverflowType = Text->GetOverflowType();
	Input.WrappingPolicy = Text->GetWrappingPolicy();
	Input.PhraseWrap = Text->GetPhraseWrap();
	Input.bUseKerning = Text->GetUseKerning();
	// A ligature is one glyph for several characters, and whatever animates the characters one by one moves
	// glyphs by their index: it would find fewer of them than it counts.
	Input.bAllowLigatures = Text->GetLigatures() && !Text->HasPerCharacterAnimation();
	Input.FontStyle = Text->GetFontStyle();
	Input.bUnderline = Text->GetUnderline();
	Input.bStrikethrough = Text->GetStrikethrough();
	Input.TextTransform = Text->GetTextTransform();
	Input.FlowDirection = Text->GetFlowDirection();
	// Named here when the text names none, rather than left for the layout to look up: the game switching language is then
	// a change of this input like any other, and the layout it compares equal to is one made in the same language.
	Input.Language = Text->GetLanguage().IsEmpty() ? FInternationalization::Get().GetCurrentLanguage()->GetName() : Text->GetLanguage();
	Input.TabSize = Text->GetTabSize();
	Input.TextJustify = Text->GetTextJustify();
	Input.LastLineAlign = Text->GetLastLineAlign();
	Input.bAutoWrapText = Text->GetAutoWrapText();
	Input.bRichText = Text->GetRichText();
	Input.RichTextFilterFlags = Text->GetRichTextTagFilterFlags();
	Input.LineHeightPercentage = Text->GetLineHeightPercentage();
	Input.WrapTextAt = Text->GetWrapTextAt();
	Input.ExpandMeshSize = Text->GetExpandMeshSize();
	Input.DynamicPixelsPerUnit = Text->GetDynamicPixelsPerUnit();
	Input.RootCanvasScale = RootCanvas ? RootCanvas->GetCanvasScale() : 1.0f;
	Input.bRenderToWorldSpace = RootCanvas ? RootCanvas->IsRenderToWorldSpace() : false;
	Input.bPixelPerfect = Text->GetShouldAffectByPixelSnapping() && Widget->GetPixelSnappingInHierarchy();
	Input.Font = Text->GetFont();
	Input.RichTextImageData = Text->GetRichTextImageData();
	Input.RichTextCustomStyleData = Text->GetRichTextCustomStyleData();
	// A colour glyph's quad is decoded by DreamGUI's shading alone: drawn by any other material, an emoji is laid out as if
	// the font had no colour face -- its emoji data's image, else a monochrome face's glyph -- rather than read as a field.
	Input.bAllowColorFaces = Text->IsShadingThroughDreamGUI();
	return Input;
}

FDreamTextPaintParams UDreamText::MakePaintParams(const UDreamText* Text)
{
	auto Widget = Text->GetWidget();
	auto RenderCanvas = Widget->GetRenderCanvas();

	FDreamTextPaintParams Params;
	const FVector WorldScale = Widget->GetWorldScale();
	const FDreamTextGlyphPaintStyle Style = Text->GetFont()->GetGlyphPaintStyle(
		FVector2f((float)WorldScale.X, (float)WorldScale.Y), Text->GetExpandMeshSize());
	Params.ItalicSlope = Style.ItalicSlope;
	Params.bRequireNormalAndTangent = RenderCanvas ? RenderCanvas->GetActualRequireNormalAndTangent() : false;
	Params.BaseColor = Text->GetFinalColor();
	// Rich-text tag colours are stored unfaded, so the fade reaches them here instead of through a layout -- and so does the
	// alpha of the content tint the text's ancestors lay over it, whose colour leaves a colour the markup chose alone.
	Params.RichTextTagOpacity = Widget->GetFinalRenderOpacity() * Widget->GetInheritedContentTint().A;
	Params.FillSegments = &Text->GetFillSegments();
	Params.FillProgress = Text->GetFillProgress();
	Params.GlowBoost = Text->GetGlowBoost();
	// A hovered or pressed link, recoloured where it is painted: nothing about the layout changes with it.
	Params.TagColorOverrides = Text->GetTagColorOverrides().Num() > 0 ? &Text->GetTagColorOverrides() : nullptr;
	// Whatever animates the characters one by one moves, fades and reveals each character's vertices; an underline drawn
	// as one strip across them would stay put. Each character gets its own piece of stroke while anything animates them.
	Params.bStrokesPerCharacter = Text->HasPerCharacterAnimation();
	if (Style.bDistanceField)
	{
		const FDreamTextStyle& TextStyle = Text->GetTextStyle();
		// Bold may show up anywhere in rich text; size the quads for it rather than re-layout on a tag.
		const bool bMayBold = Text->GetRichText() || Text->GetFontStyle() == EDreamUITextFontStyle::Bold || Text->GetFontStyle() == EDreamUITextFontStyle::BoldAndItalic;
		const float ExtraDilateEm = bMayBold ? Style.BoldDilateEm : 0.0f;
		float MaxGlowBoost = Text->GetGlowBoost();
		for (const auto& Segment : Text->GetFillSegments())
		{
			MaxGlowBoost = FMath::Max(MaxGlowBoost, Segment.GlowBoost);
		}
		Params.bDistanceField = true;
		Params.bSeparateEffectLayer = TextStyle.HasEffects();
		Params.BoldDilateEm = Style.BoldDilateEm;
		Params.FaceReachEm = TextStyle.GetFaceReachEm(ExtraDilateEm);
		Params.EffectReachEm = Params.bSeparateEffectLayer ? TextStyle.GetEffectReachEm(ExtraDilateEm, MaxGlowBoost) : 0.0f;
		Params.EmTexels = Style.EmTexels;
		Params.FieldSpreadTexels = Style.FieldSpreadTexels;
		Params.QuadMarginTexels = Style.QuadMarginTexels;
		Params.TexelToUV = Style.TexelToUV;
		// A colour glyph draws its shadow from its own alpha, and only when the style has one.
		Params.bHasUnderlay = TextStyle.UnderlayColor.A > 0;
	}
	else
	{
		// A bitmap atlas holds the face and nothing else, so the same style knobs are drawn as offset
		// copies of the glyphs instead of by the field shader: the underlay becomes a drop shadow, the
		// outline becomes a ring of taps. Same authored values, so a text reads the same either way --
		// they used to do nothing at all on a bitmap font.
		const FDreamTextStyle& TextStyle = Text->GetTextStyle();
		Params.BitmapShadowColor = TextStyle.UnderlayColor;
		Params.BitmapShadowOffsetEm = TextStyle.UnderlayOffset;
		Params.BitmapOutlineColor = TextStyle.OutlineColor;
		Params.BitmapOutlineWidthEm = TextStyle.OutlineWidth;
	}
	// Small sizes from coverage glyphs, as the gate decided right before this paint (ResolveSmallTextRaster).
	const FDreamTextSmallTextState& SmallText = Text->GetSmallTextState();
	if (SmallText.Gate == EDreamTextSmallTextGate::Coverage && IsValid(Text->GetFont()))
	{
		FDreamTextCoverageParams& Coverage = Params.Coverage;
		Coverage.bEnabled = true;
		Coverage.Font = Text->GetFont();
		Coverage.DeviceScale = SmallText.DeviceScale;
		Coverage.RasterScale = SmallText.RasterScale;
		Coverage.MaxPixelSize = Text->GetFont()->GetCoverageMaxPixelSize();
		Coverage.SnapOrigin = SmallText.SnapOrigin;
		Coverage.Contrast = UDreamGUISettings::Get()->SmallTextContrast;
		Coverage.bLinearTarget = SmallText.bLinearTarget;
		Coverage.Report = &Text->SmallTextReport;
		// A style with effects reached coverage only because the setting lets their face come from it (the gate's Style).
		Coverage.EffectFace = UDreamGUISettings::Get()->SmallTextEffectFace;
		const FDreamTextStyle& CoverageStyle = Text->GetTextStyle();
		Coverage.OutlineWidthEm = CoverageStyle.OutlineColor.A > 0 ? CoverageStyle.OutlineWidth : 0.0f;
	}

	// The paints, as the text's last update of them left them (UpdatePaintState, right before every paint). A layer paints
	// only through rows the text holds -- or through none, in its vertex colours -- so a text that could take no table from
	// its world paints as if it had no paints.
	if (const FDreamTextPaintState* TextPaints = Text->PaintState.Get())
	{
		const bool bFallback = TextPaints->bVertexColorFallback;
		if (bFallback || TextPaints->TextRow != INDEX_NONE)
		{
			FDreamTextPaints& Paints = Params.Paints;
			auto PaintedGradient = [bFallback](const FDreamTextPaintLayerState& InLayer) -> const FDreamGradient*
			{
				return DreamTextComponentPaintLocal::IsLayerPainted(InLayer, bFallback) ? &InLayer.Gradient : nullptr;
			};
			FDreamTextPaintSlot& OwnSlot = Paints.Slots[DreamTextQuadCode::TextSlot];
			OwnSlot.Face = PaintedGradient(TextPaints->Own[0]);
			OwnSlot.Outline = PaintedGradient(TextPaints->Own[1]);
			OwnSlot.Overlay = PaintedGradient(TextPaints->Own[2]);
			OwnSlot.HorizontalBox = Text->GetTextStyle().PaintBoxHorizontal;
			OwnSlot.VerticalBox = Text->GetTextStyle().PaintBoxVertical;
			OwnSlot.BoxAspect = TextPaints->OwnBoxAspect;
			// A tag paint fills the faces of its run, measured across the run's pieces laid end to end (CSS's slice).
			for (int32 Tag = 0; Tag < DreamTextComponentPaintLocal::TagSlotCount; ++Tag)
			{
				FDreamTextPaintSlot& TagSlot = Paints.Slots[DreamTextQuadCode::FirstTagSlot + Tag];
				TagSlot.Face = PaintedGradient(TextPaints->Tags[Tag]);
				TagSlot.HorizontalBox = EDreamTextPaintBox::Run;
				TagSlot.VerticalBox = EDreamTextPaintBox::Run;
				TagSlot.BoxAspect = 1.0f;
			}
			Paints.NameSlots = &TextPaints->NameSlots;
			Paints.bVertexColorFallback = bFallback;
			Paints.FaceAnimation = Text->GetFacePaintAnimation();
		}
	}
	// What a painted face is coloured instead of the text's Color, which the gradient stands in for: white, tinted as the
	// text would be, at its opacity. And the alpha its effects are drawn at, which the text colour's alpha is no part of,
	// so a hollow text keeps its outline and a faded one fades all of it.
	Params.PaintBaseColor = Text->GetFinalTintColor();
	Params.EffectOpacity = (float)Params.PaintBaseColor.A / 255.0f;
	return Params;
}


UDreamText::UDreamText(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
	// The project's default font, and only that. The font last picked in a details panel used to be the
	// default of every text made afterwards, anywhere in the process -- a play session's, a test's, a
	// preview's -- which made what a new text looks like depend on what somebody had clicked earlier.
	Font = UDreamUIFontData_BaseObject::GetDefaultFont();
	UIGeometry->bIsFont = true;
}

void UDreamText::ApplyFontTextureChange()
{
	if (IsValid(Font))
	{
		MarkVerticesDirty(true, true, true, true);
		MarkTextureDirty();
		UIGeometry->Texture = GetTextureToCreateGeometry();
		// A new atlas may be another kind of field (an SdfSource edited on the font), and the record's font mark is
		// what the shader decodes it by: an MTSDF read as one channel has every corner rounded off.
		bWidgetPropertyDataFontMarkDirty = true;
	}
}

void UDreamText::ApplyFontMaterialChange()
{
	if (IsValid(Font))
	{
		MarkVerticesDirty(true, true, true, true);
		MarkMaterialDirty();
		UIGeometry->Material = GetMaterialToCreateGeometry();
	}
}

void UDreamText::ApplyRecreateText()
{
	if (IsValid(Font))
	{
		// For when what the layout reads changed under an unchanged input -- a font's metrics, a style asset edited in
		// place -- which the input's equality cannot see, so the layout is thrown away rather than compared.
		MarkLayoutDirty();
		// Whole: a markup resolved anew can draw a different number of quads (an underline a style turned on) in other
		// colours, so every vertex channel is stale, and the text may come out another size.
		MarkVerticesDirty(true, true, true, true);
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::ApplyFontEmojiChange()
{
	// The emoji data sizes and places the emoji, and it changed under the same font.
	MarkLayoutDirty();
	this->MarkVerticesDirty(false, true, true, false);
}

void UDreamText::BeginPlay()
{
	Super::BeginPlay();
	if (IsValid(Font))
	{
		Font->InitFont();
		RegisterFont();
	}
	if (IsValid(RichTextImageData))
	{
		this->RegisterOnRichTextImageDataChange();
	}
	if (IsValid(RichTextCustomStyleData))
	{
		this->RegisterOnRichTextCustomStyleDataChange();
	}
}

void UDreamText::EndPlay()
{
	Super::EndPlay();
	if (IsValid(Font))
	{
		UnregisterFont();
	}
	if (IsValid(RichTextImageData))
	{
		this->UnregisterOnRichTextImageDataChange();
	}
	if (IsValid(RichTextCustomStyleData))
	{
		this->UnregisterOnRichTextCustomStyleDataChange();
	}

	for (int i = 0; i < CreatedRichTextImageObjectArray.Num(); i++)
	{
		auto item = CreatedRichTextImageObjectArray[i];
		if (IsValid(item))
		{
			item->DestroyWidget();
		}
	}
	CreatedRichTextImageObjectArray.Empty();
}

void UDreamText::OnRegister()
{
	Super::OnRegister();
	BindProjectSettingsChanges();
	if (auto World = this->GetWorld())
	{
#if WITH_EDITOR
		if (!World->IsGameWorld())
		{
			if (IsValid(Font))
			{
				RegisterFont();
			}
			if (!RichTextImageDataChangedDelegateHandle.IsValid())
			{
				if (IsValid(RichTextImageData))
				{
					this->RegisterOnRichTextImageDataChange();
				}
			}
			if (!RichTextCustomStyleDataChangedDelegateHandle.IsValid())
			{
				if (IsValid(RichTextCustomStyleData))
				{
					this->RegisterOnRichTextCustomStyleDataChange();
				}
			}
		}
		else
#endif
		{
			UDreamUIManagerWorldSubsystem::RegisterDreamUICultureChangedEvent(this);
		}
	}
}
void UDreamText::OnUnregister()
{
	Super::OnUnregister();
	// Nothing to repaint it in once it is gone from its tree, and nothing to paint: its world's rows go back, and so do the
	// coverage cells its quads drew from -- registered again, it repaints before it is drawn (UDreamVisual::OnRegister).
	LeaveSmallTextSharpenSet();
	LeaveSmallTextLayer();
	ReleasePaintState();
	HoldCoverageEpoch(nullptr, 0);
	if (auto World = this->GetWorld())
	{
#if WITH_EDITOR
		if (!World->IsGameWorld())
		{
			if (IsValid(Font))
			{
				UnregisterFont();
			}
			if (IsValid(RichTextImageData))
			{
				if (RichTextImageDataChangedDelegateHandle.IsValid())
				{
					this->UnregisterOnRichTextImageDataChange();
				}
			}
			if (IsValid(RichTextCustomStyleData))
			{
				if (RichTextCustomStyleDataChangedDelegateHandle.IsValid())
				{
					this->UnregisterOnRichTextCustomStyleDataChange();
				}
			}
		}
		else
#endif
		{
			UDreamUIManagerWorldSubsystem::UnregisterDreamUICultureChangedEvent(this);
		}
	}
}

void UDreamText::BeginDestroy()
{
	Super::BeginDestroy();
	// The coverage epoch it held goes with its font (UnregisterFont), its rows with its world, its places in the sets with it.
	UnregisterFont();
	ReleasePaintState();
	LeaveSmallTextSharpenSet();
	LeaveSmallTextLayer();
}

void UDreamText::OnRenderCanvasChanged(UDreamCanvas* InOldCanvas, UDreamCanvas* InNewCanvas)
{
	Super::OnRenderCanvasChanged(InOldCanvas, InNewCanvas);
	// Another canvas is another device grid, perhaps in another world: the text starts over there, and its first paint
	// on it counts as settled.
	LeaveSmallTextSharpenSet();
	LeaveSmallTextLayer();
	SmallTextState = FDreamTextSmallTextState();
	// Its quads are drawn by the old canvas no more, and the new one draws them only once they are painted again
	// (UDreamVisual::OnRenderCanvasChanged marks everything dirty): the coverage cells they drew from are let go of.
	HoldCoverageEpoch(nullptr, 0);
	// The paint rows are its world's: a canvas of another world, or none, takes them back, and the next paint there takes its
	// own (the record, which the canvas writes again whole, links to them then).
	if (PaintState.IsValid())
	{
		const UDreamWidget* NewCanvasWidget = InNewCanvas != nullptr ? InNewCanvas->GetWidget() : nullptr;
		UDreamUIManagerWorldSubsystem* NewManager = NewCanvasWidget != nullptr ? NewCanvasWidget->GetRegisteredManager() : nullptr;
		if (NewManager == nullptr || NewManager != PaintState->Manager.Get())
		{
			ReleasePaintRows();
		}
	}
}

void UDreamText::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
	Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	MarkVertexPositionDirty();
	MarkVertexUVDirty();
}

UTexture* UDreamText::GetTextureToCreateGeometry()
{
	if (!IsValid(Font))
	{
		Font = UDreamUIFontData_BaseObject::GetDefaultFont();
	}
	Font->InitFont();
	UIGeometry->Font = Font;
	return Font->GetFontTexture();
}

UMaterialInterface* UDreamText::GetMaterialToCreateGeometry()
{
	if (IsValid(OverrideMaterial))
	{
		return OverrideMaterial;
	}
	if (!IsValid(Font))
	{
		Font = UDreamUIFontData_BaseObject::GetDefaultFont();
	}
	Font->InitFont();
	return Font->GetFontMaterial();
}

void UDreamText::OnBeforeCreateOrUpdateGeometry()
{
	if (IsValid(Font))
	{
		RegisterFont();
	}
	// Drawn again after its quads were not (OnNotDrawn) -- its canvas updates a widget's geometry only while it is drawn. The
	// coverage cells they drew from are where they were while their epoch is still the font's own: held again. Retired
	// since, they may hold another glyph by now, and the quads are painted again before they are drawn.
	if (CoverageUndrawnEpoch != 0)
	{
		UDreamUIFontData_BaseObject* const UndrawnFont = CoverageUndrawnFont.Get();
		const uint32 UndrawnEpoch = CoverageUndrawnEpoch;
		CoverageUndrawnFont.Reset();
		CoverageUndrawnEpoch = 0;
		if (UndrawnFont != nullptr && UndrawnFont == Font.Get() && UndrawnFont->GetCoverageEpoch() == UndrawnEpoch)
		{
			HoldCoverageEpoch(UndrawnFont, UndrawnEpoch);
		}
		else
		{
			MarkVerticesDirty(true, true, true, false);
		}
	}
	if (bRichText && !RichTextImageDataChangedDelegateHandle.IsValid())
	{
		if (IsValid(RichTextImageData))
		{
			this->RegisterOnRichTextImageDataChange();
		}
	}
	if (bRichText && !RichTextCustomStyleDataChangedDelegateHandle.IsValid())
	{
		if (IsValid(RichTextCustomStyleData))
		{
			this->RegisterOnRichTextCustomStyleDataChange();
		}
	}
}

bool UDreamText::GetShouldAffectByPixelSnapping()const
{
	if (IsValid(Font))
	{
		return Font->GetShouldAffectByPixelPerfect();
	}
	return Super::GetShouldAffectByPixelSnapping();
}

bool UDreamText::GetRepaintsOnTransformChange()const
{
	using namespace DreamTextSmallTextLocal;
	// Asked by the update of a text whose transform changed: what the move means to it is noted here, on the text.
	UDreamText* const MutableThis = const_cast<UDreamText*>(this);
	const FDreamTextSmallTextState& State = SmallTextState;
	switch (State.Gate)
	{
	case EDreamTextSmallTextGate::Coverage:
	{
		const int32 OnMove = GetSmallTextOnMove();
		if (OnMove == 1)
		{
			// Its coverage quads go where the move takes them -- a little soft while it slides, a turned bitmap while it turns
			// -- and its world's sweep places it once it has held still, repainting it then if they are off its grid.
			MutableThis->WatchSmallTextMove(/*bInMoving*/ true);
			return false;
		}
		// Its coverage quads sit on the device grid of its last paint. A move by whole device pixels at the same scale --
		// a scroll -- keeps them on it; anything else does not, and a move that cannot be placed on a grid at all (a roll,
		// into a render layer that moves) needs the repaint that puts the text back on the field.
		FPlacement Painted;
		Painted.DeviceScale = State.DeviceScale;
		Painted.SnapOrigin = State.SnapOrigin;
		FPlacement Placement;
		if (PlaceOnDeviceGrid(GetWidget(), Placement) == EDreamTextSmallTextGate::Coverage && IsWholePixelMove(Painted, Placement))
		{
			return false;
		}
		if (OnMove == 2)
		{
			// One repaint, onto the field, and coverage again once it has held still (ResolveSmallTextRaster, then the sweep).
			MutableThis->WatchSmallTextMove(/*bInMoving*/ true);
		}
		DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::TextMoveRepaints, 1);
		return true;
	}
	case EDreamTextSmallTextGate::Transform:
	case EDreamTextSmallTextGate::Large:
	case EDreamTextSmallTextGate::RenderLayer:
	case EDreamTextSmallTextGate::RenderLayerMoving:
	case EDreamTextSmallTextGate::Settling:
		// On the field only because of where it is or how it moves -- rolled, too large, in a render layer that moves,
		// settling, moving: the move is noted and nothing measured, however often it comes. Its world's sweep places it once
		// it has held still, and repaints it then if it can draw from coverage there.
		MutableThis->WatchSmallTextMove(/*bInMoving*/ false);
		return false;
	default:
		// Off, or ruled out by something no move changes: field quads land wherever the transform puts them.
		return false;
	}
}

void UDreamText::OnPixelSnappingChanged()
{
	// The layout of a pixel-perfect font reads the snapping, and so does the small-text gate; neither asks again until the
	// text is repainted, and nothing else here would repaint it.
	if (GetWidget() != nullptr)
	{
		MarkVerticesDirty(true, true, true, false);
	}
}

void UDreamText::OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_TextUpdateGeometry);
	if (InTriangleChanged || InVertexPositionChanged || InVertexUVChanged || InVertexColorChanged)
	{
		UpdateCacheTextGeometry();
		auto Widget = GetWidget();
		auto RenderCanvas = Widget->GetRenderCanvas();
		if (!IsValid(Font) || !RenderCanvas)
		{
			// Nothing painted: the geometry was cleared before this call, and no quad of it draws from coverage any more.
			SmallTextReport = FDreamTextCoverageReport();
			HoldCoverageEpoch(nullptr, 0);
			return;
		}
		// Whether this paint draws its small sizes from coverage glyphs, and on which device grid: against the layout
		// just made, since what is small depends on it. MakePaintParams reads the answer.
		const bool bDrewFromCoverage = SmallTextState.Gate == EDreamTextSmallTextGate::Coverage;
		ResolveSmallTextRaster();
		// The paints, against the same layout: the names of its tag paints resolved when they changed, the world's rows taken
		// and the table written -- and the record's link to it, which this same update writes -- before the quads reading them.
		UpdatePaintState();
		// The geometry was cleared before this call; painting from the cached display list is what
		// fills it again, whether or not the layout itself had to run.
		CacheTextGeometryData.Paint(InGeo, MakePaintParams(this));
		// The cells this paint drew coverage glyphs from stay out of the font's pool while the text holds their epoch, so a
		// world that draws without ticking never samples a cell another world has handed out again.
		UDreamUIFontData_BaseObject* const CoverageFont = SmallTextReport.CoverageItems > 0 ? Font.Get() : nullptr;
		HoldCoverageEpoch(CoverageFont, CoverageFont != nullptr ? CoverageFont->GetCoverageEpoch() : 0);
		// The gate changed its mind in a paint nothing asked to move -- a colour change after coverage was switched off for
		// the font, say: coverage quads and field quads lie differently, so the vertices are transformed again with them.
		if (!InVertexPositionChanged && bDrewFromCoverage != (SmallTextState.Gate == EDreamTextSmallTextGate::Coverage))
		{
			MarkVertexPositionDirty();
		}
		// And a paint not asked to change the triangles that changed how many there are -- a character whose field glyph is
		// still being made, drawn from its coverage glyph or no longer -- says so, so that this update writes every vertex's
		// widget record (new memory comes zeroed) and the modifiers hear of it.
		if (!InTriangleChanged && InGeo.Vertices.Num() != PaintedVertexCount)
		{
			MarkVerticesDirty(true, false, false, false);
		}
		PaintedVertexCount = InGeo.Vertices.Num();
		// Never over coverage quads: they are on whole device pixels already, in quarter-pixel phases this would round away.
		if (CacheTextGeometryData.GetLayoutInput().bPixelPerfect && SmallTextState.Gate != EDreamTextSmallTextGate::Coverage)
		{
			FDreamUIGeometry::AdjustPixelPerfectPos_For_UIText(InGeo.OriginVertices, CacheTextGeometryData.GetCharPropertyArray(), RenderCanvas, this);
		}
	}
}

void UDreamText::ResolveSmallTextRaster()
{
	using namespace DreamTextSmallTextLocal;
	FDreamTextSmallTextState& State = SmallTextState;
	SmallTextReport = FDreamTextCoverageReport();
	UDreamWidget* const Widget = GetWidget();
	// Where it is drawn, before anything about the text itself: a text on a canvas with no device pixel grid -- a wall of
	// world-space panels -- costs one look at its root canvas, and none of the rest.
	UDreamCanvas* RootCanvas = nullptr;
	bool bTarget = false;
	EDreamTextSmallTextGate Gate = GetCanvasGate(Widget, RootCanvas, bTarget);
	if (Gate == EDreamTextSmallTextGate::Coverage)
	{
		Gate = GetSmallTextConditionsGate();
	}
	FPlacement Placement;
	if (Gate == EDreamTextSmallTextGate::Coverage)
	{
		DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::SmallTextPlacements, 1);
		Gate = GetRenderLayerGate(Widget);
		if (Gate == EDreamTextSmallTextGate::Coverage)
		{
			Gate = MeasureDeviceGrid(Widget, RootCanvas, bTarget, Placement);
		}
	}
	if (Gate != EDreamTextSmallTextGate::Coverage)
	{
		State.Gate = Gate;
		State.bMoving = false;
		LeaveSmallTextSharpenSet();
		// In a render layer that moves: on the field until the layer has held still, which wakes it with nothing measured
		// meanwhile. Turned in a layer -- the layer's own turn, which tells the text nothing when it moves -- it is listed to
		// hear of the layer's next move. Anywhere else, whatever moves it to where it could draw from coverage is noted
		// (GetRepaintsOnTransformChange).
		const bool bListedWithLayer = Gate == EDreamTextSmallTextGate::RenderLayerMoving || Gate == EDreamTextSmallTextGate::Transform;
		SetSmallTextLayer(bListedWithLayer ? Widget->GetRenderLayer() : nullptr, /*bInWaiting*/ Gate == EDreamTextSmallTextGate::RenderLayerMoving);
		return;
	}

	// What the debounce keeps of S, whatever is decided below: a scale is settled once SettleSweeps sweeps in a row have
	// found the text holding still at it (SweepSmallTextSharpenSet), and the first one a text is painted at counts as settled.
	if (!State.bScaleSeen)
	{
		State.bScaleSeen = true;
		State.SettlingScale = Placement.DeviceScale;
		State.SettledSweeps = SettleSweeps;
	}
	else if (!IsSameScale(Placement.DeviceScale, State.SettlingScale))
	{
		State.SettlingScale = Placement.DeviceScale;
		State.SettledSweeps = 0;
	}
	const bool bWasCoverage = State.Gate == EDreamTextSmallTextGate::Coverage;
	State.DeviceScale = Placement.DeviceScale;
	State.SnapOrigin = Placement.SnapOrigin;
	State.bLinearTarget = Placement.bLinearTarget;
	State.MinGlyphSize = GetSmallestCoverageGlyphSize(CacheTextGeometryData.GetDisplayList());
	if (State.MinGlyphSize * Placement.DeviceScale > Font->GetCoverageMaxPixelSize())
	{
		// Nothing the painter would draw from coverage: no debounce and no repaints on moves for a text that cannot gain. In a
		// render layer scaled up, it hears of the layer's moves, as a text drawing from coverage there does.
		State.Gate = EDreamTextSmallTextGate::Large;
		State.bMoving = false;
		LeaveSmallTextSharpenSet();
		SetSmallTextLayer(Widget->GetRenderLayer(), /*bInWaiting*/ false);
		return;
	}
	if (State.bMoving && GetSmallTextOnMove() == 2)
	{
		// Moving off its grid with DreamGUI.Text.SmallTextOnMove 2: on the field until it has held still, which its world's
		// sweep watches for.
		State.Gate = EDreamTextSmallTextGate::Settling;
		JoinSmallTextSharpenSet();
		SetSmallTextLayer(nullptr, /*bInWaiting*/ false);
		return;
	}
	if (bWasCoverage && FMath::Abs(Placement.DeviceScale / State.RasterScale - 1.0f) <= RasterScaleHysteresis)
	{
		// Within the hysteresis the glyphs keep their raster; the painter sizes their quads for the new scale.
		State.Gate = EDreamTextSmallTextGate::Coverage;
	}
	else if (State.SettledSweeps >= SettleSweeps)
	{
		State.Gate = EDreamTextSmallTextGate::Coverage;
		State.RasterScale = Placement.DeviceScale;
	}
	else
	{
		// The scale is still moving -- a zoom, a live window resize: on the field until it holds still, so a text is not
		// rasterized again at every step of it. Its world's sweep repaints it once it has.
		State.Gate = EDreamTextSmallTextGate::Settling;
		JoinSmallTextSharpenSet();
		SetSmallTextLayer(nullptr, /*bInWaiting*/ false);
		return;
	}
	// From coverage. In a render layer it goes on the layer's list of such texts, to hear of the layer's moves.
	SetSmallTextLayer(Widget->GetRenderLayer(), /*bInWaiting*/ false);
	// A text moving with its coverage quads kept (SmallTextOnMove 1) is still watched: the sweep places it once it holds still.
	if (!State.bMoving)
	{
		LeaveSmallTextSharpenSet();
	}
}

EDreamTextSmallTextGate UDreamText::GetSmallTextConditionsGate()
{
	if (SmallTextRaster == EDreamTextSmallTextRaster::Off)
	{
		return EDreamTextSmallTextGate::Off;
	}
	if (!IsValid(Font) || !Font->SupportsCoverageGlyphs())
	{
		return EDreamTextSmallTextGate::Font;
	}
	// Only DreamGUI's shading -- the built-in UI shader, or a material built on MF_DreamUI_Shade -- knows a coverage quad;
	// another material may read its quads any way it likes.
	if (!IsShadingThroughDreamGUI())
	{
		return IsValid(OverrideMaterial) ? EDreamTextSmallTextGate::OverrideMaterial : EDreamTextSmallTextGate::Material;
	}
	// Softness and dilation reshape the face, which a hinted raster cannot. The effects come from the field either way,
	// moved onto the coverage face (UDreamGUISettings::SmallTextEffectFace) -- unless that says Field, which keeps such a text
	// on the field as before. A paint rules nothing out: the shader paints a coverage face as it paints a field one.
	if (!FMath::IsNearlyZero(TextStyle.FaceSoftness) || !FMath::IsNearlyZero(TextStyle.FaceDilate))
	{
		return EDreamTextSmallTextGate::StyleFace;
	}
	if (TextStyle.HasEffects() && UDreamGUISettings::Get()->SmallTextEffectFace == EDreamSmallTextEffectFace::Field)
	{
		return EDreamTextSmallTextGate::Style;
	}
	if (!DreamTextSmallTextLocal::IsSnappingAllowed(GetWidget()))
	{
		return EDreamTextSmallTextGate::Snapping;
	}
	// Coverage quads are on whole device pixels and sampled texel for texel; a modifier that moves or re-maps vertices
	// would take them off both.
	bool bTriangles = false, bPositions = false, bUVs = false, bColors = false;
	GeometryModifierWillChangeVertexData(bTriangles, bPositions, bUVs, bColors);
	if (bPositions || bUVs)
	{
		return EDreamTextSmallTextGate::Modifier;
	}
	return EDreamTextSmallTextGate::Coverage;
}

void UDreamText::WatchSmallTextMove(bool bInMoving)
{
	// The sweeps it has held still for are counted again from this move on; the move itself measures nothing.
	SmallTextState.SettledSweeps = 0;
	if (bInMoving)
	{
		SmallTextState.bMoving = true;
	}
	JoinSmallTextSharpenSet();
}

bool UDreamText::RecheckSmallTextPlacement(bool& bOutListedWithLayer)
{
	using namespace DreamTextSmallTextLocal;
	bOutListedWithLayer = false;
	FDreamTextSmallTextState& State = SmallTextState;
	// Coverage quads kept while it moved (SmallTextOnMove 1) are repainted unless they are still on the grid.
	const bool bKeptCoverageQuads = State.Gate == EDreamTextSmallTextGate::Coverage;
	State.bMoving = false;
	FPlacement Placement;
	const EDreamTextSmallTextGate Gate = PlaceOnDeviceGrid(GetWidget(), Placement);
	if (Gate != EDreamTextSmallTextGate::Coverage)
	{
		if (bKeptCoverageQuads)
		{
			return true;
		}
		// On the field, and staying there: it says why, and its next move is noted as this one was -- in a render layer, with
		// the layer (ResolveSmallTextRaster lists it so).
		State.Gate = Gate;
		bOutListedWithLayer = Gate == EDreamTextSmallTextGate::RenderLayerMoving || Gate == EDreamTextSmallTextGate::Transform;
		return false;
	}
	if (IsValid(Font) && State.MinGlyphSize * Placement.DeviceScale > Font->GetCoverageMaxPixelSize())
	{
		if (bKeptCoverageQuads)
		{
			return true;
		}
		State.Gate = EDreamTextSmallTextGate::Large;
		bOutListedWithLayer = true;
		return false;
	}
	if (bKeptCoverageQuads)
	{
		FPlacement Painted;
		Painted.DeviceScale = State.DeviceScale;
		Painted.SnapOrigin = State.SnapOrigin;
		if (IsWholePixelMove(Painted, Placement))
		{
			return false;
		}
	}
	// Placed, at a scale it has held still at for long enough: repainted from coverage there, with no further wait.
	State.SettlingScale = Placement.DeviceScale;
	State.SettledSweeps = SettleSweeps;
	return true;
}

void UDreamText::JoinSmallTextSharpenSet()
{
	if (SmallTextState.bWaitingToSharpen)
	{
		return;
	}
	UDreamWidget* Widget = GetWidget();
	UDreamUIManagerWorldSubsystem* Manager = Widget != nullptr ? Widget->GetRegisteredManager() : nullptr;
	if (Manager == nullptr)
	{
		// No world ticks it: it stays on the field until something repaints it.
		return;
	}
	DreamTextSmallTextLocal::FSharpenSet& Set = DreamTextSmallTextLocal::FindOrAddSharpenSet(Manager);
	SmallTextSharpenIndex = Set.Texts.Add(this);
	SmallTextState.bWaitingToSharpen = true;
	SmallTextSharpenManager = Manager;
	BindSmallTextSweep(Manager);
}

void UDreamText::BindSmallTextSweep(UDreamUIManagerWorldSubsystem* InManager)
{
	DreamTextSmallTextLocal::FSharpenSet* Set = DreamTextSmallTextLocal::FindSharpenSet(InManager);
	if (Set == nullptr || Set->SweepHandle.IsValid())
	{
		return;
	}
	const TWeakObjectPtr<UDreamUIManagerWorldSubsystem> WeakManager(InManager);
	Set->SweepHandle = InManager->GetOnBeforeRootCanvasesUpdate().AddLambda([WeakManager]()
	{
		UDreamText::SweepSmallTextSharpenSet(WeakManager.Get());
	});
}

void UDreamText::LeaveSmallTextSharpenSet()
{
	if (!SmallTextState.bWaitingToSharpen)
	{
		return;
	}
	SmallTextState.bWaitingToSharpen = false;
	UDreamUIManagerWorldSubsystem* Manager = SmallTextSharpenManager.Get();
	SmallTextSharpenManager.Reset();
	const int32 Index = SmallTextSharpenIndex;
	SmallTextSharpenIndex = INDEX_NONE;
	DreamTextSmallTextLocal::FSharpenSet* Set = DreamTextSmallTextLocal::FindSharpenSet(Manager);
	if (Set == nullptr)
	{
		return;
	}
	// From the place it was put at, the last one swapped into it: a panel destroyed mid-zoom lets go of thousands at once.
	DreamTextSmallTextLocal::RemoveFromTextList(Set->Texts, Index, TWeakObjectPtr<UDreamText>(this),
		[](UDreamText& InMoved, int32 InMovedIndex) { InMoved.SmallTextSharpenIndex = InMovedIndex; });
	DreamTextSmallTextLocal::TidySharpenSet(Manager);
}

void UDreamText::SetSmallTextLayer(UDreamWidget* InLayer, bool bInWaiting)
{
	using namespace DreamTextSmallTextLocal;
	if (InLayer == nullptr)
	{
		LeaveSmallTextLayer();
		return;
	}
	const TObjectKey<UDreamWidget> Key(InLayer);
	if (SmallTextLayerIndex != INDEX_NONE && SmallTextLayerKey == Key && bSmallTextLayerWaiting == bInWaiting)
	{
		return;
	}
	LeaveSmallTextLayer();
	UDreamWidget* Widget = GetWidget();
	UDreamUIManagerWorldSubsystem* Manager = Widget != nullptr ? Widget->GetRegisteredManager() : nullptr;
	if (Manager == nullptr)
	{
		return;
	}
	FSharpenSet& Set = FindOrAddSharpenSet(Manager);
	FLayerTexts& Record = Set.Layers.FindOrAdd(Key);
	if (bInWaiting)
	{
		SmallTextLayerIndex = Record.Waiting.Add(this);
	}
	else
	{
		// The first text drawing coverage in it since the layer last moved its holders off their grid, or ever: where the
		// layer stands now is what its moves are measured from.
		if (Record.Holders.Num() == 0 || !InLayer->GetLayerHoldsCoverageText())
		{
			Record.bPlaced = PlaceLayerOnDeviceGrid(InLayer, Record.Placement) == EDreamTextSmallTextGate::Coverage;
		}
		SmallTextLayerIndex = Record.Holders.Add(this);
		InLayer->SetLayerHoldsCoverageText(true);
	}
	SmallTextLayerKey = Key;
	SmallTextLayerManager = Manager;
	bSmallTextLayerWaiting = bInWaiting;
	if (bInWaiting)
	{
		BindSmallTextSweep(Manager);
	}
}

void UDreamText::LeaveSmallTextLayer()
{
	using namespace DreamTextSmallTextLocal;
	if (SmallTextLayerIndex == INDEX_NONE)
	{
		return;
	}
	const TObjectKey<UDreamWidget> Key = SmallTextLayerKey;
	UDreamUIManagerWorldSubsystem* Manager = SmallTextLayerManager.Get();
	const int32 Index = SmallTextLayerIndex;
	const bool bWaiting = bSmallTextLayerWaiting;
	ForgetSmallTextLayer();
	FSharpenSet* Set = FindSharpenSet(Manager);
	FLayerTexts* Record = Set != nullptr ? Set->Layers.Find(Key) : nullptr;
	if (Record == nullptr)
	{
		return;
	}
	RemoveFromTextList(bWaiting ? Record->Waiting : Record->Holders, Index, TWeakObjectPtr<UDreamText>(this),
		[](UDreamText& InMoved, int32 InMovedIndex) { InMoved.SmallTextLayerIndex = InMovedIndex; });
	if (!bWaiting && Record->Holders.Num() == 0)
	{
		// Nothing draws coverage in it now: its moves need not call.
		if (UDreamWidget* Layer = Key.ResolveObjectPtr())
		{
			Layer->SetLayerHoldsCoverageText(false);
		}
	}
	if (Record->Holders.Num() == 0 && Record->Waiting.Num() == 0)
	{
		Set->Layers.Remove(Key);
	}
	TidySharpenSet(Manager);
}

void UDreamText::ForgetSmallTextLayer()
{
	SmallTextLayerKey = TObjectKey<UDreamWidget>();
	SmallTextLayerManager.Reset();
	SmallTextLayerIndex = INDEX_NONE;
	bSmallTextLayerWaiting = false;
}

void UDreamText::OnRenderLayerMoved(UDreamWidget* InLayer)
{
	using namespace DreamTextSmallTextLocal;
	if (InLayer == nullptr)
	{
		return;
	}
	const TObjectKey<UDreamWidget> Key(InLayer);
	UDreamUIManagerWorldSubsystem* Manager = InLayer->GetRegisteredManager();
	FSharpenSet* Set = FindSharpenSet(Manager);
	FLayerTexts* Record = Set != nullptr ? Set->Layers.Find(Key) : nullptr;
	if (Record == nullptr || Record->Holders.Num() == 0)
	{
		// Nothing of its small text hears of its moves any more: they need not call.
		InLayer->SetLayerHoldsCoverageText(false);
		return;
	}
	// The layer measured once, against where it stood when its holders were painted. Flat at the same scale, the move is a
	// translation: only coverage quads can be off the grid for it, and not even those when it is by whole pixels. A text on
	// the field for the layer's turn or scale is left as it is by a translation, and repainted for anything else.
	FPlacement Placement;
	const bool bPlacedNow = PlaceLayerOnDeviceGrid(InLayer, Placement) == EDreamTextSmallTextGate::Coverage;
	const bool bSameScale = Record->bPlaced && bPlacedNow && IsSameScale(Placement.DeviceScale, Record->Placement.DeviceScale);
	if (bSameScale && IsWholePixelMove(Record->Placement, Placement))
	{
		return;
	}
	TArray<UDreamText*, TInlineAllocator<16>> ToRepaint;
	for (int32 Index = Record->Holders.Num() - 1; Index >= 0; --Index)
	{
		UDreamText* Text = Record->Holders[Index].Get();
		const bool bLive = IsValid(Text);
		if (bLive && bSameScale && Text->SmallTextState.Gate != EDreamTextSmallTextGate::Coverage)
		{
			continue;
		}
		Record->Holders.RemoveAtSwap(Index, EAllowShrinking::No);
		if (Index < Record->Holders.Num())
		{
			if (UDreamText* Moved = Record->Holders[Index].Get())
			{
				Moved->SmallTextLayerIndex = Index;
			}
		}
		if (bLive)
		{
			Text->ForgetSmallTextLayer();
			ToRepaint.Add(Text);
		}
	}
	// What the texts left, all on the field, are measured from now.
	Record->Placement = Placement;
	Record->bPlaced = bPlacedNow;
	if (Record->Holders.Num() == 0)
	{
		InLayer->SetLayerHoldsCoverageText(false);
		if (Record->Waiting.Num() == 0)
		{
			Set->Layers.Remove(Key);
		}
	}
	// Each repainted -- in this frame, when the move is announced before the canvases update -- from the field, to wait for
	// the layer to hold still (ResolveSmallTextRaster).
	for (UDreamText* Text : ToRepaint)
	{
		if (Text->GetWidget() != nullptr)
		{
			Text->MarkVerticesDirty(true, true, true, false);
			DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::TextMoveRepaints, 1);
		}
	}
	TidySharpenSet(Manager);
}

void UDreamText::SweepSmallTextSharpenSet(UDreamUIManagerWorldSubsystem* InManager)
{
	using namespace DreamTextSmallTextLocal;
	FSharpenSet* Set = FindSharpenSet(InManager);
	if (Set == nullptr)
	{
		return;
	}
	// What this world may repaint onto coverage this frame, the watched texts and the layers' together: once a screen's
	// labels all stop moving at once, the rest wait for the next frames. The sweep runs once a frame for its world, before
	// its canvases update.
	int32 Budget = FMath::Max(1, UDreamGUISettings::Get()->SmallTextRepaintBudgetPerFrame);
	TArray<UDreamText*, TInlineAllocator<8>> ToRepaint;
	TArray<UDreamText*, TInlineAllocator<4>> ToListWithLayer;

	// The texts watched since a move, or settling: counted while they hold still, and looked at once they have.
	for (int32 Index = Set->Texts.Num() - 1; Index >= 0; --Index)
	{
		UDreamText* Text = Set->Texts[Index].Get();
		bool bLeaves = true;
		if (IsValid(Text) && Text->GetWidget() != nullptr)
		{
			DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::SharpenSweepTexts, 1);
			FDreamTextSmallTextState& State = Text->SmallTextState;
			// A move starts the count again (WatchSmallTextMove), and so does a settling text's scale changing again.
			if (State.SettledSweeps < SettleSweeps)
			{
				++State.SettledSweeps;
			}
			bLeaves = State.SettledSweeps >= SettleSweeps && Budget > 0;
			if (bLeaves)
			{
				// Held still long enough: one look at where it is now, and out of the set whatever that finds.
				bool bListedWithLayer = false;
				if (Text->RecheckSmallTextPlacement(bListedWithLayer))
				{
					--Budget;
					ToRepaint.Add(Text);
					DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::SharpenRepaints, 1);
				}
				else if (bListedWithLayer)
				{
					ToListWithLayer.Add(Text);
				}
			}
		}
		if (bLeaves)
		{
			if (Text != nullptr)
			{
				Text->SmallTextState.bWaitingToSharpen = false;
				Text->SmallTextSharpenManager.Reset();
				Text->SmallTextSharpenIndex = INDEX_NONE;
			}
			Set->Texts.RemoveAtSwap(Index, EAllowShrinking::No);
			if (Index < Set->Texts.Num())
			{
				if (UDreamText* Moved = Set->Texts[Index].Get())
				{
					Moved->SmallTextSharpenIndex = Index;
				}
			}
		}
	}

	// The render layers texts wait on: woken once the layer has held still for LayerSettleFrames frames -- told by its
	// stamp, with nothing measured -- or is a layer no longer, or is gone.
	for (auto LayerIt = Set->Layers.CreateIterator(); LayerIt; ++LayerIt)
	{
		FLayerTexts& Record = LayerIt.Value();
		if (Record.Waiting.Num() > 0)
		{
			const UDreamWidget* Layer = LayerIt.Key().ResolveObjectPtr();
			const bool bStill = Layer == nullptr || !Layer->IsRenderLayer()
				|| GFrameCounter - Layer->GetRenderLayerMovedFrame() >= LayerSettleFrames;
			for (int32 Index = Record.Waiting.Num() - 1; bStill && Index >= 0 && Budget > 0; --Index)
			{
				UDreamText* Text = Record.Waiting[Index].Get();
				Record.Waiting.RemoveAtSwap(Index, EAllowShrinking::No);
				if (Index < Record.Waiting.Num())
				{
					if (UDreamText* Moved = Record.Waiting[Index].Get())
					{
						Moved->SmallTextLayerIndex = Index;
					}
				}
				if (!IsValid(Text))
				{
					continue;
				}
				Text->ForgetSmallTextLayer();
				if (Text->GetWidget() == nullptr)
				{
					continue;
				}
				DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::SharpenSweepTexts, 1);
				// Repainted with its scale counted as settled: the layer's wait was its wait, and no second one follows.
				Text->SmallTextState.bScaleSeen = false;
				--Budget;
				ToRepaint.Add(Text);
				DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::SharpenRepaints, 1);
			}
		}
		if (Record.Holders.Num() == 0 && Record.Waiting.Num() == 0)
		{
			LayerIt.RemoveCurrent();
		}
	}

	// In a render layer, the texts the look left on the field because of the layer: waiting for it to hold still, or listed
	// to hear of its next move (ResolveSmallTextRaster lists them the same way). Done once the set is no longer walked.
	for (UDreamText* Text : ToListWithLayer)
	{
		if (UDreamWidget* TextWidget = Text->GetWidget())
		{
			Text->SetSmallTextLayer(TextWidget->GetRenderLayer(), /*bInWaiting*/ Text->SmallTextState.Gate == EDreamTextSmallTextGate::RenderLayerMoving);
		}
	}
	// Unbinds the sweep -- from inside its own broadcast, which the delegate allows -- once nothing in the set is left to
	// sweep. Nothing of the set is touched after this.
	TidySharpenSet(InManager);
	for (UDreamText* Text : ToRepaint)
	{
		// This frame's update repaints it, and the gate finds its scale settled: coverage glyphs at that scale. Never a
		// layout -- nothing the layout reads has changed.
		Text->MarkVerticesDirty(true, true, true, false);
	}
}

void UDreamText::BindProjectSettingsChanges()
{
	// Bound once, by the first text registered: the delegates are the settings', and last as long as the process does.
	static bool bBound = false;
	if (!bBound)
	{
		bBound = true;
		UDreamGUISettings::GetOnSmallTextCoverageChanged().AddStatic(&UDreamText::OnSmallTextSettingsChanged);
		DreamGradientPresets::OnPresetsChanged().AddStatic(&UDreamText::OnGradientPresetsChanged);
	}
}

void UDreamText::OnGradientPresetsChanged()
{
	// The project's gradient presets were edited (in the editor: the project settings, or a gradient saved as a preset).
	// Every text that resolved the names of its tag paints resolves them again -- a name may stand for the preset edited,
	// or for one that came or went: another gradient takes other rows, and only what the quads carry changing repaints.
	// Never a layout. A text not painted yet resolves its names at its first paint.
	for (TObjectIterator<UDreamText> It(RF_ClassDefaultObject | RF_ArchetypeObject, /*bIncludeDerivedClasses*/ true, EInternalObjectFlags::Garbage); It; ++It)
	{
		UDreamText* Text = *It;
		if (!Text->PaintState.IsValid() || Text->PaintState->ResolvedNames.Num() == 0)
		{
			continue;
		}
		Text->PaintState->bTagPaintsStale = true;
		if (Text->GetWidget() != nullptr)
		{
			Text->RefreshPaintRowsWithoutRepaint();
		}
	}
}

void UDreamText::OnSmallTextSettingsChanged()
{
	// DreamGUI.Text.SmallTextCoverage or SmallTextMaxPixelSize changed: every text that has been painted asks its gate again,
	// its small-text state starting over and its first paint counting as settled. A repaint, never a layout.
	for (TObjectIterator<UDreamText> It(RF_ClassDefaultObject | RF_ArchetypeObject, /*bIncludeDerivedClasses*/ true, EInternalObjectFlags::Garbage); It; ++It)
	{
		UDreamText* Text = *It;
		const UDreamWidget* TextWidget = Text->GetWidget();
		if (Text->SmallTextState.Gate == EDreamTextSmallTextGate::NotPainted || TextWidget == nullptr || TextWidget->GetRenderCanvas() == nullptr)
		{
			continue;
		}
		Text->SmallTextState.bScaleSeen = false;
		Text->MarkVerticesDirty(true, true, true, false);
	}
}

void UDreamText::HoldCoverageEpoch(UDreamUIFontData_BaseObject* InFont, uint32 InEpoch)
{
	const uint32 Epoch = InFont != nullptr ? InEpoch : 0;
	UDreamUIFontData_BaseObject* const Held = CoverageHoldFont.Get();
	const uint32 HeldEpoch = Held != nullptr ? CoverageHoldEpoch : 0;
	if (Epoch == 0 && HeldEpoch == 0)
	{
		CoverageHoldFont.Reset();
		CoverageHoldEpoch = 0;
		return;
	}
	if (Held == InFont && HeldEpoch == Epoch)
	{
		return;
	}
	// One call to each font the hold moves between: off the old one, onto the new one -- a single move when it is one font.
	if (Held != nullptr && Held != InFont && HeldEpoch != 0)
	{
		Held->MoveCoverageHold(HeldEpoch, 0);
	}
	if (InFont != nullptr)
	{
		InFont->MoveCoverageHold(Held == InFont ? HeldEpoch : 0, Epoch);
	}
	CoverageHoldFont = Epoch != 0 ? InFont : nullptr;
	CoverageHoldEpoch = Epoch;
}

void UDreamText::OnNotDrawn()
{
	UDreamUIFontData_BaseObject* const Held = CoverageHoldFont.Get();
	const uint32 HeldEpoch = Held != nullptr ? CoverageHoldEpoch : 0;
	if (HeldEpoch == 0)
	{
		// Nothing held -- its last paint drew nothing from coverage, or it let go already: whatever it let go of is still
		// remembered for when it is drawn again.
		return;
	}
	// A text hidden, collapsed or put away is not repainted when its font flushes the coverage cells -- its canvas updates
	// only what it draws -- and a hold it kept would keep the flushed cells from the pool for as long as it stays so.
	HoldCoverageEpoch(nullptr, 0);
	CoverageUndrawnFont = Held;
	CoverageUndrawnEpoch = HeldEpoch;
}

UMaterialInterface* UDreamText::GetDrawingMaterial()const
{
	if (IsValid(OverrideMaterial))
	{
		return OverrideMaterial;
	}
	// Otherwise the font's own material draws it, when it has one; else the built-in UI shader where its canvas draws with it
	// -- the DreamUI renderer's canvases, with the setting on -- or the canvas's default material where it does not
	// (UDreamCanvas::UpdateDrawCallMaterial).
	UMaterialInterface* DrawingMaterial = IsValid(Font) ? Font->GetFontMaterial() : nullptr;
	if (DrawingMaterial == nullptr)
	{
		const UDreamWidget* TextWidget = GetWidget();
		const UDreamCanvas* RenderCanvas = TextWidget != nullptr ? TextWidget->GetRenderCanvas() : nullptr;
		if (RenderCanvas != nullptr && !(UDreamUISettings::GetUseBuiltInUIShader() && RenderCanvas->IsRenderByDreamUIRendererOrUERenderer()))
		{
			DrawingMaterial = RenderCanvas->GetDefaultMaterial();
		}
	}
	return DrawingMaterial;
}

bool UDreamText::IsShadingThroughDreamGUI()const
{
	const UMaterialInterface* DrawingMaterial = GetDrawingMaterial();
	return DrawingMaterial == nullptr || DreamTextSmallTextLocal::HasShadeMarker(DrawingMaterial);
}

uint8 UDreamText::GetFontMark_WidgetPropertyDataForMaterial()
{
	return IsValid(Font) ? static_cast<uint8>(Font->GetFontTextureMark()) : 0;
}

void UDreamText::FillWidgetPropertyDataForMaterial_Extra(UDreamUIDataAsTexture* DataAsTexture)
{
	const int32 StartPosition = GetWidgetPropertyDataStartPosition();
	if (StartPosition == INDEX_NONE || !DataAsTexture)return;
	// Called right after the record's marks pixel was written with GetFontMark_WidgetPropertyDataForMaterial.
	WrittenFontMark = GetFontMark_WidgetPropertyDataForMaterial();
	TArray<uint8> Packed;
	TextStyle.Pack(Packed);
	DataAsTexture->UpdateBlock(FDreamTextStyle::PackedPixelStart, StartPosition, MoveTemp(Packed), FDreamTextStyle::PackedPixelCount);
}

uint32 UDreamText::GetWidgetMarksRecordLink()const
{
	// The text's table in its world's paint rows, as the row plus one: a quad whose record links to none paints nothing,
	// whatever its slot says. A text painted in its vertex colours holds no table.
	const FDreamTextPaintState* TextPaints = PaintState.Get();
	return TextPaints != nullptr && TextPaints->TextRow != INDEX_NONE ? (uint32)(TextPaints->TextRow + 1) & DreamPaintRows::RecordRowLinkMask : 0;
}

void UDreamText::OnCultureChanged_Implementation()
{
	// The language a text without one of its own is shaped in, and the culture a case transform maps by, are in the layout
	// input (MakeLayoutInput), so a switch changes it; the line breaker still follows the game's culture from outside it.
	// So the layout is thrown away all the same, and the text repainted, which is what asks for a new one.
	MarkLayoutDirty();
	auto originText = Text;
	Text = FText::GetEmpty();//just make it work, because SetText will compare text value
	SetText(originText);
}


#if WITH_EDITOR
void UDreamText::PreEditChange(FEditPropertyChain& PropertyAboutToChange)
{
	// The property overload below is handed the chain's active node, the field edited -- a paint's gradient, a stop's
	// position -- and never hears TextStyle named for an edit inside it: the member it is in is the chain's, or its head
	// for a chain built with no active member.
	const auto* MemberNode = PropertyAboutToChange.GetActiveMemberNode();
	if (MemberNode == nullptr)
	{
		MemberNode = PropertyAboutToChange.GetHead();
	}
	const FProperty* EditedMember = MemberNode != nullptr ? MemberNode->GetValue() : nullptr;
	if (EditedMember != nullptr && EditedMember->GetFName() == GET_MEMBER_NAME_CHECKED(UDreamText, TextStyle))
	{
		// What the edit is told apart by once it is made (PostEditChangeProperty): again at every step of a drag, so each
		// step is measured against the one before it.
		StyleBeforeEdit = MakeUnique<FDreamTextStyle>(TextStyle);
	}
	Super::PreEditChange(PropertyAboutToChange);
}
void UDreamText::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	// Null means "an undo is about to restore everything", which no per-property branch below can
	// answer. See the note on UDreamWidget::PreEditChange.
	if (PropertyAboutToChange == nullptr)
	{
		return;
	}
	auto PropertyName = PropertyAboutToChange->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, Font))
	{
		if (IsValid(Font))
		{
			UnregisterFont();
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, RichTextImageData))
	{
		if (IsValid(RichTextImageData))//unregister event from prev
		{
			UnregisterOnRichTextImageDataChange();
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, RichTextCustomStyleData))
	{
		if (IsValid(RichTextCustomStyleData))
		{
			UnregisterOnRichTextCustomStyleDataChange();
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, TextStyle))
	{
		// What the edit is told apart by once it is made: a change of the paints alone costs what their setters cost.
		StyleBeforeEdit = MakeUnique<FDreamTextStyle>(TextStyle);
	}
}
void UDreamText::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	auto MemberProperty = PropertyChangedEvent.MemberProperty;
	auto Property = PropertyChangedEvent.Property;
	bPaintEditHandled = false;
	if (MemberProperty != nullptr && Property != nullptr)
	{
		if (!this->GetName().StartsWith("Default__"))
		{
			auto MemberPropertyName = MemberProperty->GetFName();
			if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, Text))
			{
				if (IsValid(Font))
				{
					RegisterFont();
				}
				ConditionalUpdateCacheTextGeometry();
			}
			else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, Font))
			{
				ClearEmojiObject();
				ConditionalUpdateCacheTextGeometry();
			}
			else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, bUseKerning))
			{
				MarkVertexPositionDirty();
				MarkLayoutDirty();
			}
			else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, TextStyle))
			{
				if (StyleBeforeEdit.IsValid() && DreamTextComponentPaintLocal::IsSameButPaints(*StyleBeforeEdit, TextStyle))
				{
					// The paints alone, written in place by the details panel -- its gradient editor included: what their setters
					// would cost. Another gradient takes other rows; a layer turned on or off, and the boxes, repaint. (An undo
					// comes with no property, and repaints and lays the text out whole, as any undo of a text does.)
					const FDreamTextStyle& Before = *StyleBeforeEdit;
					const bool bQuadsChange = Before.FacePaint.IsPainting() != TextStyle.FacePaint.IsPainting()
						|| Before.OutlinePaint.IsPainting() != TextStyle.OutlinePaint.IsPainting()
						|| Before.OverlayPaint.IsPainting() != TextStyle.OverlayPaint.IsPainting()
						|| Before.PaintBoxHorizontal != TextStyle.PaintBoxHorizontal
						|| Before.PaintBoxVertical != TextStyle.PaintBoxVertical;
					if (bQuadsChange)
					{
						MarkVerticesDirty(true, true, true, true);
					}
					else
					{
						OnPaintsChanged(false);
					}
					bPaintEditHandled = true;
				}
				else
				{
					// Same road as SetTextStyle: the record, and the quads, whose reach the style decides -- and the paints,
					// which the repaint brings up to date.
					bWidgetPropertyDataFontMarkDirty = true;
					MarkVerticesDirty(true, true, true, false);
				}
				// Kept through a drag (interactive changes) for every step of it to be told apart against where it began.
				if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
				{
					StyleBeforeEdit.Reset();
				}
			}
			else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, FacePaintPhase)
				|| MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, OutlinePaintPhase)
				|| MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, OverlayPaintPhase)
				|| MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, PaintAngleOffset))
			{
				// As their setters do: one pixel of the table a slot.
				OnPaintAnimationChanged();
				bPaintEditHandled = true;
			}
			else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, bRichText))
			{
				if (bRichText)
				{
					ConditionalUpdateCacheTextGeometry();
				}
				else
				{
					ClearCreatedRichTextImageObject();
				}
			}
			else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, RichTextImageData))
			{
				UnregisterOnRichTextImageDataChange();
				if (!IsValid(RichTextImageData))//clear richTextImageData, then need to delete created object
				{
					ClearCreatedRichTextImageObject();
				}
				else
				{
					ConditionalUpdateCacheTextGeometry();
				}
			}
			else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, RichTextTagFilterFlags))
			{
				if (!(RichTextTagFilterFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::Image)))
				{
					ClearCreatedRichTextImageObject();
				}
				else
				{
					ConditionalUpdateCacheTextGeometry();
				}
			}
			else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, RichTextCustomStyleData))
			{
				if (IsValid(RichTextCustomStyleData))
				{
					RegisterOnRichTextCustomStyleDataChange();
				}
			}
			// A paint or its animation is nothing a layout reads.
			if (!bPaintEditHandled)
			{
				UDreamWidget::MarkLayoutForRebuild(GetWidget());
			}
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
	bPaintEditHandled = false;
}

bool UDreamText::MarksAllDirtyOnPropertyEdit(const FPropertyChangedEvent& /*InEvent*/)const
{
	return !bPaintEditHandled;
}

void UDreamText::PreEditUndo()
{
	// Release the old subscriptions before transaction serialization restores their asset pointers.
	UnregisterOnRichTextImageDataChange();
	UnregisterOnRichTextCustomStyleDataChange();
	Super::PreEditUndo();
}

void UDreamText::PostEditUndo()
{
	// An undo puts the properties back with nothing told first (PreEditChange is asked with no property): a font it put in
	// place of the one the text was added to, the old one is let go of now, with the coverage epoch held through it -- not
	// at the next paint, which a hidden text may not have for a long while. The repaint the undo asks for (through
	// PostEditChangeProperty, with no property) adds the text to the font it has now.
	if (bHasAddToFont && FontAddedTo.Get() != Font.Get())
	{
		UnregisterFont();
	}
	Super::PostEditUndo();
	if (!IsValid(this) || !bRichText)return;
	// Super may already repaint and register them. Hidden text still needs the restored listeners now.
	if (IsValid(RichTextImageData) && !RichTextImageDataChangedDelegateHandle.IsValid())
	{
		RegisterOnRichTextImageDataChange();
	}
	if (IsValid(RichTextCustomStyleData) && !RichTextCustomStyleDataChangedDelegateHandle.IsValid())
	{
		RegisterOnRichTextCustomStyleDataChange();
	}
}

#endif
void UDreamText::RegisterOnRichTextImageDataChange()
{
	RichTextImageDataChangedDelegateHandle = RichTextImageData->OnDataChange.AddWeakLambda(this, [this] {
		// Edited in place: the layout input still holds the same asset, so it cannot tell.
		this->MarkLayoutDirty();
		this->MarkVerticesDirty(true, true, true, false);
		});
}
void UDreamText::UnregisterOnRichTextImageDataChange()
{
	// Null-safe: the details panel clears the asset and then lets go of it, and a setter lets go of one it is replacing.
	if (IsValid(RichTextImageData))
	{
		RichTextImageData->OnDataChange.Remove(RichTextImageDataChangedDelegateHandle);
	}
	RichTextImageDataChangedDelegateHandle.Reset();
}

void UDreamText::RegisterOnRichTextCustomStyleDataChange()
{
	RichTextCustomStyleDataChangedDelegateHandle = RichTextCustomStyleData->OnDataChange.AddWeakLambda(this, [this] {
		// Edited in place, as above -- and a style's paint is what a tag paint's name may resolve to.
		if (PaintState.IsValid())
		{
			PaintState->bTagPaintsStale = true;
		}
		this->MarkLayoutDirty();
		this->MarkVerticesDirty(true, true, true, false);
		});
}
void UDreamText::UnregisterOnRichTextCustomStyleDataChange()
{
	if (IsValid(RichTextCustomStyleData))
	{
		RichTextCustomStyleData->OnDataChange.Remove(RichTextCustomStyleDataChangedDelegateHandle);
	}
	RichTextCustomStyleDataChangedDelegateHandle.Reset();
}

bool UDreamText::IsTextTruncated()const
{
	UpdateCacheTextGeometry();
	return CacheTextGeometryData.IsTextTruncated();
}



void UDreamText::SetFont(UDreamUIFontData_BaseObject* Value) {
	if (Font != Value)
	{
		// The coverage epoch it held is the old font's: let go of, whatever became of the font.
		HoldCoverageEpoch(nullptr, 0);
		//remove from old
		if (IsValid(Font))
		{
			UnregisterFont();
		}
		Font = Value;

		MarkTextureDirty();
		// The quads too, not only the atlas they are drawn from: a texture change alone repaints nothing, and the
		// old font's quads would go on sampling the new atlas until something else asked for a repaint.
		MarkVerticesDirty(true, true, true, true);
		// And the record's font mark, which says how the shader reads the atlas: one channel for a bitmap or a
		// single-channel field, the median of three for MTSDF. Left as it was, a switch between kinds of font
		// decoded the new atlas the old way.
		bWidgetPropertyDataFontMarkDirty = true;
		// The emoji objects came out of the old font's emoji data, as they do when the font is changed in the editor.
		ClearEmojiObject();
		//add to new
		if (IsValid(Font))
		{
			RegisterFont();
		}
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}
void UDreamText::SetText(const FText& Value) {
	if (!Text.EqualTo(Value))
	{
		Text = Value;
		MarkVerticesDirty(true, true, true, false);
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
		ConditionalUpdateCacheTextGeometry();
	}
}


void UDreamText::SetFontSize(float Value) {
	if (FontSize != Value)
	{
		FontSize = Value;
		MarkVertexPositionDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}
void UDreamText::SetUseKerning(bool Value)
{
	if (bUseKerning != Value)
	{
		bUseKerning = Value;
		MarkVertexPositionDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}
void UDreamText::SetLigatures(bool Value)
{
	if (bLigatures != Value)
	{
		bLigatures = Value;
		MarkLigaturesDirty();
	}
}

void UDreamText::RegisterPerCharacterAnimation(const UObject* InAnimator)
{
	if (InAnimator == nullptr)return;
	const bool bHadAnimation = HasPerCharacterAnimation();
	PerCharacterAnimators.RemoveAll([](const TWeakObjectPtr<const UObject>& Animator) { return !Animator.IsValid(); });
	PerCharacterAnimators.AddUnique(TWeakObjectPtr<const UObject>(InAnimator));
	if (!bHadAnimation)
	{
		if (bLigatures)
		{
			// Ligatures go: the characters are laid out a glyph each from now on.
			MarkLigaturesDirty();
		}
		// And the underlines and strikethroughs are painted a piece per character (MakePaintParams).
		if (GetWidget() != nullptr)
		{
			MarkVerticesDirty(true, true, true, true);
		}
	}
}

void UDreamText::UnregisterPerCharacterAnimation(const UObject* InAnimator)
{
	const bool bHadAnimation = HasPerCharacterAnimation();
	PerCharacterAnimators.RemoveAll([InAnimator](const TWeakObjectPtr<const UObject>& Animator)
	{
		return !Animator.IsValid() || Animator.Get() == InAnimator;
	});
	if (bHadAnimation && !HasPerCharacterAnimation())
	{
		if (bLigatures)
		{
			MarkLigaturesDirty();
		}
		// The strokes go back to one strip per run.
		if (GetWidget() != nullptr)
		{
			MarkVerticesDirty(true, true, true, true);
		}
	}
}

void UDreamText::MarkLigaturesDirty()
{
	// A ligature replaces several glyphs with one, so the glyphs and the width can both change. An animator lets go
	// of its text as it is torn down, when the text may have no widget left.
	if (GetWidget() != nullptr)
	{
		MarkVerticesDirty(true, true, true, false);
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

bool UDreamText::HasPerCharacterAnimation()const
{
	for (const TWeakObjectPtr<const UObject>& Animator : PerCharacterAnimators)
	{
		if (Animator.IsValid())
		{
			return true;
		}
	}
	return false;
}
void UDreamText::SetFontSpace(FVector2D Value) {
	if (FontSpace != Value)
	{
		MarkVertexPositionDirty();
		FontSpace = Value;
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}
// Paragraph alignment reaches nothing that layout reads. UpdateUITextGeometry fills textPreferredSize
// from glyph advances and line heights, and only *afterwards* uses paragraphHAlign/paragraphVAlign to
// offset the vertices inside the rect that size describes. So the desired size does not move, the
// widget does not move inside its parent, and there is nothing for a layout pass to recompute -
// MarkVertexPositionDirty already reaches the canvas through MarkVerticesDirty -> MarkCanvasUpdate,
// which is the whole of what a re-alignment needs.
void UDreamText::SetMargin(const FMargin& Value)
{
	if (!(Margin == Value))
	{
		Margin = Value;
		MarkVertexPositionDirty();
		// Unlike paragraph alignment, a margin narrows the wrap width, so the text itself comes out
		// a different size and whatever is sizing itself to this text has to hear about it.
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetLineHeightPercentage(float Value)
{
	Value = FMath::Max(0.0f, Value);
	if (LineHeightPercentage != Value)
	{
		LineHeightPercentage = Value;
		MarkVertexPositionDirty();
		// Changes how tall the paragraph comes out, so a content-sized parent has to re-measure.
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetWrapTextAt(float Value)
{
	Value = FMath::Max(0.0f, Value);
	if (WrapTextAt != Value)
	{
		WrapTextAt = Value;
		MarkVertexPositionDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetMinDesiredWidth(float Value)
{
	Value = FMath::Max(0.0f, Value);
	if (MinDesiredWidth != Value)
	{
		MinDesiredWidth = Value;
		// Layout only: the glyphs do not move, but what the parent measures does.
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetAutoWrapText(bool Value)
{
	if (bAutoWrapText != Value)
	{
		bAutoWrapText = Value;
		MarkVertexPositionDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetTextTransform(EDreamUITextTransformPolicy Value)
{
	if (TextTransform != Value)
	{
		TextTransform = Value;
		MarkVertexPositionDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetFlowDirection(EDreamTextFlowDirection Value)
{
	if (FlowDirection != Value)
	{
		FlowDirection = Value;
		MarkVertexPositionDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

/*
 * The language, the tab stops and the justification are layout inputs (MakeLayoutInput): the faces a run is shaped from,
 * how far a tab reaches and where a justified line's room goes all move glyphs, and a fallback face of another size can
 * change how tall the paragraph is. So their setters do what SetFlowDirection does.
 */
void UDreamText::SetLanguage(const FString& Value)
{
	if (!Language.Equals(Value, ESearchCase::CaseSensitive))
	{
		Language = Value;
		MarkVertexPositionDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetTabSize(float Value)
{
	Value = FMath::Max(0.0f, Value);
	if (TabSize != Value)
	{
		TabSize = Value;
		MarkVertexPositionDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetTextJustify(EDreamTextJustify Value)
{
	if (TextJustify != Value)
	{
		TextJustify = Value;
		MarkVertexPositionDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetLastLineAlign(EDreamTextLastLineAlign Value)
{
	if (LastLineAlign != Value)
	{
		LastLineAlign = Value;
		MarkVertexPositionDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetSmallTextRaster(EDreamTextSmallTextRaster Value)
{
	if (SmallTextRaster != Value)
	{
		SmallTextRaster = Value;
		// Coverage glyphs only stand in for quads at paint time, so this is a repaint and never a layout.
		if (GetWidget() != nullptr)
		{
			MarkVerticesDirty(true, true, true, false);
		}
	}
}

void UDreamText::SetIncrementalLayout(bool bInEnabled)
{
	CacheTextGeometryData.SetIncrementalLayout(bInEnabled);
}

void UDreamText::SetUnderline(bool Value)
{
	if (bUnderline != Value)
	{
		bUnderline = Value;
		// An underline is a strip of its own quads, so this changes the triangles, not just positions.
		MarkAllDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetStrikethrough(bool Value)
{
	if (bStrikethrough != Value)
	{
		bStrikethrough = Value;
		MarkAllDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetBestFit(bool Value)
{
	if (bBestFit != Value)
	{
		bBestFit = Value;
		// Neither Best Fit nor its minimum is in the layout input, so the size it remembers for an input may have
		// been searched for under other settings: it goes with the layout.
		MarkLayoutDirty();
		MarkVertexPositionDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetBestFitMinSize(float Value)
{
	Value = FMath::Max(1.0f, Value);
	if (BestFitMinSize != Value)
	{
		BestFitMinSize = Value;
		if (bBestFit)
		{
			// The remembered size was searched for with the old minimum (see SetBestFit).
			MarkLayoutDirty();
			MarkVertexPositionDirty();
			UDreamWidget::MarkLayoutForRebuild(GetWidget());
		}
	}
}

float UDreamText::FindBestFitFontSize(const FVector2f& InBox, float InMinSize, float InMaxSize,
	TFunctionRef<FVector2f(float)> InMeasure)
{
	const int32 MinSize = FMath::Max(1, FMath::FloorToInt(InMinSize));
	const int32 MaxSize = FMath::FloorToInt(InMaxSize);
	if (MaxSize <= MinSize)return (float)MinSize;
	auto Fits = [&InBox, &InMeasure](int32 InSize)
	{
		const FVector2f Measured = InMeasure((float)InSize);
		// Both axes: with wrapping on, width is respected for us and only height can fail, but
		// without it a long single line overflows sideways and nothing else would catch that.
		return Measured.X <= InBox.X + UE_KINDA_SMALL_NUMBER
			&& Measured.Y <= InBox.Y + UE_KINDA_SMALL_NUMBER;
	};
	// Asking for the biggest size first means the common case -- the text already fits -- costs one
	// measurement rather than a whole bisection.
	if (Fits(MaxSize))return (float)MaxSize;
	int32 Low = MinSize;
	int32 High = MaxSize;
	int32 Best = MinSize;
	// Fits() is monotonic in size for a fixed box, so bisection lands on the largest that fits.
	while (Low <= High)
	{
		const int32 Mid = Low + (High - Low) / 2;
		if (Fits(Mid))
		{
			Best = Mid;
			Low = Mid + 1;
		}
		else
		{
			High = Mid - 1;
		}
	}
	return (float)Best;
}

void UDreamText::GetContentBox(const FVector2f& InWidgetSize, const FVector2f& InPivot, const FMargin& InMargin,
	FVector2f& OutSize, FVector2f& OutPivot)
{
	OutSize.X = FMath::Max(0.0f, InWidgetSize.X - InMargin.Left - InMargin.Right);
	OutSize.Y = FMath::Max(0.0f, InWidgetSize.Y - InMargin.Top - InMargin.Bottom);
	// The layout reads the box as a centre offset from the pivot: centre = Size * (0.5 - Pivot).
	// Solving that for the inset box's centre is what keeps an asymmetric margin asymmetric --
	// simply shrinking Size around the same pivot would inset both edges by the average instead.
	// Y counts upward here, so it is the BOTTOM margin that pushes the centre up.
	auto SolvePivot = [](float InSize, float InNewSize, float InPivotOnAxis, float InShift)
	{
		if (InNewSize <= UE_SMALL_NUMBER)return InPivotOnAxis;
		const float Centre = InSize * (0.5f - InPivotOnAxis) + InShift * 0.5f;
		return 0.5f - Centre / InNewSize;
	};
	OutPivot.X = SolvePivot(InWidgetSize.X, OutSize.X, InPivot.X, InMargin.Left - InMargin.Right);
	OutPivot.Y = SolvePivot(InWidgetSize.Y, OutSize.Y, InPivot.Y, InMargin.Bottom - InMargin.Top);
}

void UDreamText::SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign Value) {
	if (HAlign != Value)
	{
		MarkVertexPositionDirty();
		HAlign = Value;
	}
}
void UDreamText::SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign Value) {
	if (VAlign != Value)
	{
		MarkVertexPositionDirty();
		VAlign = Value;
	}
}
void UDreamText::SetOverflowType(EDreamUITextOverflowType Value) {
	if (OverflowType != Value)
	{
		// Truncation, at the end or in the middle, cuts glyphs out of what is drawn: the quad count changes with it.
		auto CutsGlyphs = [](EDreamUITextOverflowType InType)
		{
			return InType == EDreamUITextOverflowType::Truncate || InType == EDreamUITextOverflowType::MiddleEllipsis;
		};
		if (CutsGlyphs(OverflowType) || CutsGlyphs(Value))
			MarkVerticesDirty(true, true, true, true);
		else
			MarkVertexPositionDirty();
		OverflowType = Value;
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetWrappingPolicy(ETextWrappingPolicy Value)
{
	if (WrappingPolicy != Value)
	{
		WrappingPolicy = Value;
		MarkVertexPositionDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetPhraseWrap(EDreamTextPhraseWrap Value)
{
	if (PhraseWrap != Value)
	{
		PhraseWrap = Value;
		MarkVertexPositionDirty();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

/*
 * The style, the fill and the glow are paint inputs: MakePaintParams reads them, MakeLayoutInput does not, and no
 * glyph is placed or measured differently for them -- the quads they widen are sized by the painter. So the setters
 * below only repaint (see MarkLayoutDirty). A lyric line sets its fill segments every frame of a word, and that used
 * to cost a rich-text parse, a shaping pass and a line break per frame, and with Best Fit on a whole size search.
 */
void UDreamText::SetTextStyle(const FDreamTextStyle& Value)
{
	if (TextStyle != Value)
	{
		TextStyle = Value;
		// The style lives in the widget property record, and it also decides how far the glyph quads
		// reach and whether the effects get quads of their own.
		bWidgetPropertyDataFontMarkDirty = true;
		MarkVerticesDirty(true, true, true, false);
	}
}

void UDreamText::SetFillProgress(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	if (FillProgress != Value)
	{
		FillProgress = Value;
		MarkVertexUVDirty();
	}
}

void UDreamText::SetGlowBoost(float Value)
{
	Value = FMath::Max(Value, 0.0f);
	if (GlowBoost != Value)
	{
		GlowBoost = Value;
		// The boost widens the glow, which can widen the quads.
		MarkVerticesDirty(false, true, true, false);
	}
}

void UDreamText::SetFillSegments(const TArray<FDreamTextFillSegment>& Value)
{
	FillSegments = Value;
	MarkVerticesDirty(false, true, true, false);
}

void UDreamText::ClearFillSegments()
{
	if (FillSegments.Num() > 0)
	{
		FillSegments.Reset();
		MarkVerticesDirty(false, true, true, false);
	}
}

/*
 * PAINTS. Another gradient for a layer already painting is rows only -- another gradient row taken from the world, the
 * table written, no repaint: a row is never written again under the texts that share it. A layer turned on or off, and the
 * boxes, change what the quads carry (their slots, UV4, colours): a repaint. Nothing here lays the text out again. A text
 * painted in its vertex colours (its material does not shade through MF_DreamUI_Shade) repaints for any of it.
 */
void UDreamText::SetFacePaint(const FDreamTextPaint& Value)
{
	if (TextStyle.FacePaint != Value)
	{
		const bool bWasPainting = TextStyle.FacePaint.IsPainting();
		TextStyle.FacePaint = Value;
		OnPaintsChanged(bWasPainting != TextStyle.FacePaint.IsPainting());
	}
}

void UDreamText::SetOutlinePaint(const FDreamTextPaint& Value)
{
	if (TextStyle.OutlinePaint != Value)
	{
		const bool bWasPainting = TextStyle.OutlinePaint.IsPainting();
		TextStyle.OutlinePaint = Value;
		OnPaintsChanged(bWasPainting != TextStyle.OutlinePaint.IsPainting());
	}
}

void UDreamText::SetOverlayPaint(const FDreamTextPaint& Value)
{
	if (TextStyle.OverlayPaint != Value)
	{
		const bool bWasPainting = TextStyle.OverlayPaint.IsPainting();
		TextStyle.OverlayPaint = Value;
		OnPaintsChanged(bWasPainting != TextStyle.OverlayPaint.IsPainting());
	}
}

void UDreamText::SetOverlayBlend(EDreamTextOverlayBlend Value)
{
	if (TextStyle.OverlayBlend != Value)
	{
		TextStyle.OverlayBlend = Value;
		// It lives in the table, beside the overlay's row: a write, no repaint.
		OnPaintsChanged(false);
	}
}

void UDreamText::SetPaintBoxes(EDreamTextPaintBox InHorizontal, EDreamTextPaintBox InVertical)
{
	if (TextStyle.PaintBoxHorizontal != InHorizontal || TextStyle.PaintBoxVertical != InVertical)
	{
		TextStyle.PaintBoxHorizontal = InHorizontal;
		TextStyle.PaintBoxVertical = InVertical;
		// Every painted quad's UV4 is measured in them, and under a Glyph box a stroke is cut into a piece per character.
		if (GetWidget() != nullptr)
		{
			MarkVerticesDirty(true, true, true, true);
		}
	}
}

void UDreamText::SetFacePaintPhase(float Value)
{
	if (FacePaintPhase != Value)
	{
		FacePaintPhase = Value;
		OnPaintAnimationChanged();
	}
}

void UDreamText::SetOutlinePaintPhase(float Value)
{
	if (OutlinePaintPhase != Value)
	{
		OutlinePaintPhase = Value;
		OnPaintAnimationChanged();
	}
}

void UDreamText::SetOverlayPaintPhase(float Value)
{
	if (OverlayPaintPhase != Value)
	{
		OverlayPaintPhase = Value;
		OnPaintAnimationChanged();
	}
}

void UDreamText::SetPaintAngleOffset(float Value)
{
	if (PaintAngleOffset != Value)
	{
		PaintAngleOffset = Value;
		OnPaintAnimationChanged();
	}
}

void UDreamText::SetPaintCenterOffset(FVector2D Value)
{
	const FVector2f Offset((float)Value.X, (float)Value.Y);
	if (PaintCenterOffset != Offset)
	{
		PaintCenterOffset = Offset;
		OnPaintAnimationChanged();
	}
}

FVector2D UDreamText::GetPaintCenterOffset()const
{
	return FVector2D(PaintCenterOffset);
}

void UDreamText::SetPaintScale(float Value)
{
	if (PaintScaleMultiplier != Value)
	{
		PaintScaleMultiplier = Value;
		OnPaintAnimationChanged();
	}
}

float UDreamText::GetPaintScale()const
{
	return PaintScaleMultiplier;
}

int32 UDreamText::GetPaintTextRow()const
{
	return PaintState.IsValid() ? PaintState->TextRow : INDEX_NONE;
}

bool UDreamText::GetPaintTablePixel(int32 InPixel, FVector4f& OutPixel)const
{
	if (!PaintState.IsValid() || PaintState->TextRow == INDEX_NONE || !PaintState->bTableWritten || InPixel < 0 || InPixel >= DreamPaintRows::RowWidth)
	{
		return false;
	}
	OutPixel = PaintState->TablePixels[InPixel];
	return true;
}

FDreamPaintAnimation UDreamText::GetFacePaintAnimation()const
{
	// As the table holds them (WritePaintTable): a value that is no number is the neutral one.
	using DreamTextComponentPaintLocal::FiniteOr;
	FDreamPaintAnimation Animation;
	Animation.Phase = FiniteOr(FacePaintPhase, 0.0f);
	Animation.AngleOffset = FiniteOr(PaintAngleOffset, 0.0f);
	Animation.CenterOffset = FVector2f(FiniteOr(PaintCenterOffset.X, 0.0f), FiniteOr(PaintCenterOffset.Y, 0.0f));
	Animation.ScaleMultiplier = FiniteOr(PaintScaleMultiplier, 1.0f);
	return Animation;
}

void UDreamText::OnPaintsChanged(bool bInLayerTurnedOnOrOff)
{
	if (GetWidget() == nullptr)
	{
		// Kept as it is, and taken up by the first paint.
		return;
	}
	if (bInLayerTurnedOnOrOff)
	{
		MarkVerticesDirty(true, true, true, true);
		return;
	}
	RefreshPaintRowsWithoutRepaint();
}

void UDreamText::RefreshPaintRowsWithoutRepaint()
{
	// Nothing to bring up to date before the first paint, which takes the paints as they are; and a paint already on its way
	// -- anything about the vertices asked for -- brings them up to date itself.
	if (!PaintState.IsValid() || GetAnythingDirty())
	{
		return;
	}
	if (PaintState->bVertexColorFallback || CacheTextGeometryData.IsLayoutDirty())
	{
		// The gradients are in the vertex colours, or the boxes they are measured in are about to change: a repaint.
		MarkVerticesDirty(true, true, true, true);
		return;
	}
	if (UpdatePaintState())
	{
		// The change reached what the quads carry -- a tag paint's name resolves to nothing now, say: a repaint after all.
		MarkVerticesDirty(true, true, true, true);
	}
}

void UDreamText::OnPaintAnimationChanged()
{
	if (!PaintState.IsValid())
	{
		// Kept, and written into the table the text takes when it first paints.
		return;
	}
	if (PaintState->bVertexColorFallback)
	{
		// Painted in its vertex colours, of which the animation is a part: its colours are painted again.
		if (GetWidget() != nullptr)
		{
			MarkVerticesDirty(false, false, false, true);
		}
		return;
	}
	WritePaintTable();
}

void UDreamText::OnPaintPresetChanged()
{
	if (!PaintState.IsValid())
	{
		return;
	}
	// A tag paint may resolve through the asset as well as the text's own paints: every name is resolved again.
	PaintState->bTagPaintsStale = true;
	if (GetWidget() != nullptr)
	{
		RefreshPaintRowsWithoutRepaint();
	}
}

bool UDreamText::UpdatePaintState()
{
	using namespace DreamTextComponentPaintLocal;
	const FDreamTextDisplayList& DisplayList = CacheTextGeometryData.GetDisplayList();
	if (!TextStyle.HasPaints() && DisplayList.PaintNames.Num() == 0)
	{
		// Nothing paints, nor could: the rows go back, the record's link with them, and the presets are heard no more.
		const bool bPainted = PaintState.IsValid() && GetPaintedLayersMask(*PaintState) != 0;
		ReleasePaintState();
		return bPainted;
	}
	if (!PaintState.IsValid())
	{
		PaintState = MakeUnique<FDreamTextPaintState>();
	}
	FDreamTextPaintState& State = *PaintState;
	const uint32 PaintedBefore = GetPaintedLayersMask(State);
	const TArray<uint8> NameSlotsBefore = State.NameSlots;
	const int32 TextRowBefore = State.TextRow;

	// The tag paints, by name, in the order the text first names them (slots 2 to 9): resolved again only when the layout
	// names others, or what they resolve through -- a custom style, a gradient asset -- changed.
	TArray<FDreamGradient, TInlineAllocator<4>> Resolved;
	const bool bResolve = State.bTagPaintsStale || State.ResolvedGeneration != DisplayList.Generation || State.ResolvedNames != DisplayList.PaintNames;
	if (bResolve)
	{
		State.bTagPaintsStale = false;
		State.ResolvedGeneration = DisplayList.Generation;
		State.ResolvedNames = DisplayList.PaintNames;
		State.NameSlots.Reset();
		State.NameSlots.AddZeroed(DisplayList.PaintNames.Num());
		for (int32 NameIndex = 0; NameIndex < DisplayList.PaintNames.Num(); ++NameIndex)
		{
			FDreamGradient Gradient;
			if (!FDreamTextPaint::ResolveTagPaint(DisplayList.PaintNames[NameIndex], RichTextCustomStyleData.Get(), Gradient))
			{
				// Resolves to nothing: its items are drawn as if no tag paint were on them.
				continue;
			}
			if (Resolved.Num() >= TagSlotCount)
			{
				static bool bLoggedSlotsFull = false;
				if (!bLoggedSlotsFull)
				{
					bLoggedSlotsFull = true;
					UE_LOG(DreamGUI, Warning, TEXT("[%s].%d %s paints with more than %d gradients by name: the runs of the rest are drawn without theirs."),
						ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName(), TagSlotCount);
				}
				continue;
			}
			State.NameSlots[NameIndex] = (uint8)(DreamTextQuadCode::FirstTagSlot + Resolved.Num());
			Resolved.Add(MoveTemp(Gradient));
		}
	}
	const FDreamTextPaint* const OwnPaints[3] = { &TextStyle.FacePaint, &TextStyle.OutlinePaint, &TextStyle.OverlayPaint };
	bool bAnyPainting = bResolve && Resolved.Num() > 0;
	for (int32 Tag = 0; !bResolve && Tag < TagSlotCount; ++Tag)
	{
		bAnyPainting |= State.Tags[Tag].bPainting;
	}
	for (const FDreamTextPaint* OwnPaint : OwnPaints)
	{
		bAnyPainting |= OwnPaint->IsPainting();
	}

	// Through the rows of the world it is registered in -- a table of its own, a gradient row a layer -- unless what draws it
	// cannot read them: then in its vertex colours, holding no rows at all.
	State.bVertexColorFallback = bAnyPainting && !IsShadingThroughDreamGUI();
	UDreamWidget* const TextWidget = GetWidget();
	UDreamUIManagerWorldSubsystem* const Manager = TextWidget != nullptr ? TextWidget->GetRegisteredManager() : nullptr;
	const bool bWantRows = bAnyPainting && !State.bVertexColorFallback && Manager != nullptr;
	if (!bWantRows || (State.TextRow != INDEX_NONE && State.Manager.Get() != Manager))
	{
		ReleasePaintRows();
	}
	if (bWantRows && State.TextRow == INDEX_NONE)
	{
		// INDEX_NONE again for a world with no paint rows, or none left: the text then paints as if it had no paints.
		State.Manager = Manager;
		State.TextRow = Manager->AcquirePaintTextRow();
		State.bTableWritten = false;
	}
	const bool bRows = bWantRows && State.TextRow != INDEX_NONE;
	UDreamUIManagerWorldSubsystem* const RowsManager = State.Manager.Get();
	for (int32 Layer = 0; Layer < 3; ++Layer)
	{
		SetPaintLayer(State.Own[Layer], OwnPaints[Layer]->IsPainting() ? &OwnPaints[Layer]->GetEffectiveGradient() : nullptr, RowsManager, bRows);
	}
	for (int32 Tag = 0; Tag < TagSlotCount; ++Tag)
	{
		FDreamTextPaintLayerState& TagLayer = State.Tags[Tag];
		const FDreamGradient* Wanted = bResolve
			? (Tag < Resolved.Num() ? &Resolved[Tag] : nullptr)
			: (TagLayer.bPainting ? &TagLayer.Gradient : nullptr);
		SetPaintLayer(TagLayer, Wanted, RowsManager, bRows);
	}
	State.OwnBoxAspect = GetBoxAspect(DisplayList, TextStyle.PaintBoxHorizontal, TextStyle.PaintBoxVertical);

	// The gradient assets the paints come from, heard while they are: an edit of one takes other rows, with no repaint.
	TArray<UDreamGradientAsset*, TInlineAllocator<4>> Presets;
	for (const FDreamTextPaint* OwnPaint : OwnPaints)
	{
		if (OwnPaint->IsPainting() && OwnPaint->Preset != nullptr)
		{
			Presets.AddUnique(OwnPaint->Preset.Get());
		}
	}
	if (const UDreamUIRichTextCustomStyleData* Styles = RichTextCustomStyleData.Get())
	{
		for (const FName& PaintName : State.ResolvedNames)
		{
			const FDreamUIRichTextCustomStyleItemData* Entry = Styles->GetDataMap().Find(PaintName);
			if (Entry != nullptr && Entry->paintType == EDreamUIRichTextCustomStyleData_PaintType::Set && Entry->paint.Preset != nullptr)
			{
				Presets.AddUnique(Entry->paint.Preset.Get());
			}
		}
	}
	for (int32 Index = State.PresetBindings.Num() - 1; Index >= 0; --Index)
	{
		UDreamGradientAsset* Bound = State.PresetBindings[Index].Key.Get();
		if (Bound == nullptr || !Presets.Contains(Bound))
		{
			if (Bound != nullptr)
			{
				Bound->OnGradientChanged().Remove(State.PresetBindings[Index].Value);
			}
			State.PresetBindings.RemoveAtSwap(Index);
		}
	}
	for (UDreamGradientAsset* Preset : Presets)
	{
		if (!State.PresetBindings.ContainsByPredicate([Preset](const auto& InBinding) { return InBinding.Key.Get() == Preset; }))
		{
			State.PresetBindings.Emplace(Preset, Preset->OnGradientChanged().AddUObject(this, &UDreamText::OnPaintPresetChanged));
		}
	}

	if (bRows)
	{
		WritePaintTable();
	}
	if (State.TextRow != TextRowBefore)
	{
		// The record links to the table: written again with the marks, in this update when a paint got here.
		bWidgetPropertyDataFontMarkDirty = true;
	}
	return GetPaintedLayersMask(State) != PaintedBefore || State.NameSlots != NameSlotsBefore;
}

void UDreamText::ReleasePaintRows()
{
	if (!PaintState.IsValid())
	{
		return;
	}
	FDreamTextPaintState& State = *PaintState;
	// Given back to the manager they came from; one gone with its world takes them with it.
	UDreamUIManagerWorldSubsystem* const RowsManager = State.Manager.Get();
	for (FDreamTextPaintLayerState& OwnLayer : State.Own)
	{
		DreamTextComponentPaintLocal::ReleaseLayerRow(OwnLayer, RowsManager);
	}
	for (FDreamTextPaintLayerState& TagLayer : State.Tags)
	{
		DreamTextComponentPaintLocal::ReleaseLayerRow(TagLayer, RowsManager);
	}
	if (State.TextRow != INDEX_NONE)
	{
		if (RowsManager != nullptr)
		{
			RowsManager->ReleasePaintTextRow(State.TextRow);
		}
		State.TextRow = INDEX_NONE;
		// The record's link goes with the table.
		bWidgetPropertyDataFontMarkDirty = true;
	}
	State.bTableWritten = false;
	State.Manager.Reset();
}

void UDreamText::ReleasePaintState()
{
	if (!PaintState.IsValid())
	{
		return;
	}
	ReleasePaintRows();
	for (const TPair<TWeakObjectPtr<UDreamGradientAsset>, FDelegateHandle>& Binding : PaintState->PresetBindings)
	{
		if (UDreamGradientAsset* Preset = Binding.Key.Get())
		{
			Preset->OnGradientChanged().Remove(Binding.Value);
		}
	}
	PaintState.Reset();
}

void UDreamText::WritePaintTable()
{
	if (!PaintState.IsValid() || PaintState->TextRow == INDEX_NONE)
	{
		return;
	}
	FDreamTextPaintState& State = *PaintState;
	UDreamUIManagerWorldSubsystem* const RowsManager = State.Manager.Get();
	if (RowsManager == nullptr)
	{
		return;
	}
	using namespace DreamPaintRows;
	FVector4f Table[RowWidth];
	for (FVector4f& Pixel : Table)
	{
		Pixel = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	}
	const FVector4f Unused(NoRow, NoRow, NoRow, NoRow);
	auto RowOf = [](const FDreamTextPaintLayerState& InLayer) { return InLayer.bPainting && InLayer.Row != INDEX_NONE ? (float)InLayer.Row : NoRow; };
	// The shader takes these as they come: none of them may be a NaN or an infinity.
	using DreamTextComponentPaintLocal::FiniteOr;
	const FVector4f Phases(FiniteOr(FacePaintPhase, 0.0f), FiniteOr(OutlinePaintPhase, 0.0f), FiniteOr(OverlayPaintPhase, 0.0f), 1.0f);
	const FVector4f Moves(FiniteOr(PaintAngleOffset, 0.0f), FiniteOr(PaintCenterOffset.X, 0.0f), FiniteOr(PaintCenterOffset.Y, 0.0f),
		FiniteOr(PaintScaleMultiplier, 1.0f));
	const float OwnAspect = State.OwnBoxAspect > 0.0f ? FiniteOr(State.OwnBoxAspect, 1.0f) : 1.0f;
	int32 HighestSlot = 0;
	for (int32 Slot = DreamTextQuadCode::TextSlot; Slot <= DreamTextQuadCode::MaxSlot; ++Slot)
	{
		const bool bOwnSlot = Slot == DreamTextQuadCode::TextSlot;
		const float FaceRow = RowOf(bOwnSlot ? State.Own[0] : State.Tags[Slot - DreamTextQuadCode::FirstTagSlot]);
		const float OutlineRow = bOwnSlot ? RowOf(State.Own[1]) : NoRow;
		const float OverlayRow = bOwnSlot ? RowOf(State.Own[2]) : NoRow;
		if (FaceRow < 0.0f && OutlineRow < 0.0f && OverlayRow < 0.0f)
		{
			Table[GetSlotRowsPixel(Slot)] = Unused;
			Table[GetSlotAnimationPixel(Slot)] = Unused;
			Table[GetSlotTransformPixel(Slot)] = Unused;
			continue;
		}
		HighestSlot = Slot;
		Table[GetSlotRowsPixel(Slot)] = FVector4f(FaceRow, OutlineRow, OverlayRow, bOwnSlot ? (float)static_cast<uint8>(TextStyle.OverlayBlend) : 0.0f);
		// The text's animation moves every slot it paints with: its tag paints move with it.
		Table[GetSlotAnimationPixel(Slot)] = FVector4f(Phases.X, Phases.Y, Phases.Z, bOwnSlot ? OwnAspect : 1.0f);
		Table[GetSlotTransformPixel(Slot)] = Moves;
	}
	Table[TextHeaderPixel] = FVector4f((float)HighestSlot, 0.0f, 0.0f, 0.0f);
	// What changed alone, a run of pixels at a time: a phase animated every frame sends the slots' animation pixels and no more.
	int32 Pixel = 0;
	while (Pixel < RowWidth)
	{
		if (State.bTableWritten && Table[Pixel] == State.TablePixels[Pixel])
		{
			++Pixel;
			continue;
		}
		const int32 FirstPixel = Pixel;
		while (Pixel < RowWidth && !(State.bTableWritten && Table[Pixel] == State.TablePixels[Pixel]))
		{
			State.TablePixels[Pixel] = Table[Pixel];
			++Pixel;
		}
		RowsManager->WritePaintRowPixels(State.TextRow, FirstPixel, TConstArrayView<FVector4f>(&Table[FirstPixel], Pixel - FirstPixel));
	}
	State.bTableWritten = true;
}

void UDreamText::SetFontStyle(EDreamUITextFontStyle Value) {
	if (FontStyle != Value)
	{
		if ((FontStyle == EDreamUITextFontStyle::None || FontStyle == EDreamUITextFontStyle::Italic)
			&& (Value == EDreamUITextFontStyle::None || Value == EDreamUITextFontStyle::Italic))//these only affect vertex position
		{
			MarkVertexPositionDirty();
		}
		else
		{
			MarkVerticesDirty(true, true, true, true);
		}
		FontStyle = Value;
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}
void UDreamText::SetRichText(bool Value)
{
	if (bRichText != Value)
	{
		MarkVerticesDirty(true, true, true, true);
		bRichText = Value;
		if (!bRichText)
		{
			ClearCreatedRichTextImageObject();
		}
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}
void UDreamText::SetRichTextTagFilterFlags(int32 Value)
{
	if (RichTextTagFilterFlags != Value)
	{
		MarkVerticesDirty(true, true, true, true);
		RichTextTagFilterFlags = Value;
		if (!(RichTextTagFilterFlags & (1 << (int)EDreamUIText_RichTextTagFilterFlags::Image)))
		{
			ClearCreatedRichTextImageObject();
		}
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}
void UDreamText::SetRichTextImageData(UDreamUIRichTextImageData_BaseObject* Value)
{
	if (RichTextImageData != Value)
	{
		MarkVerticesDirty(true, true, true, true);
		// An edit made in place reaches the layout only through this text listening to the asset, so it stops
		// listening to the one going; the next geometry update listens to the new one (OnBeforeCreateOrUpdateGeometry).
		UnregisterOnRichTextImageDataChange();
		RichTextImageData = Value;
		if (!IsValid(RichTextImageData))//clear richTextImageData, then need to delete created object
		{
			ClearCreatedRichTextImageObject();
		}
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}
void UDreamText::SetRichTextCustomStyleData(UDreamUIRichTextCustomStyleData* Value)
{
	if (RichTextCustomStyleData != Value)
	{
		MarkVerticesDirty(true, true, true, true);
		// As in SetRichTextImageData.
		UnregisterOnRichTextCustomStyleDataChange();
		RichTextCustomStyleData = Value;
		// The names of the tag paints are looked up in it first.
		if (PaintState.IsValid())
		{
			PaintState->bTagPaintsStale = true;
		}
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetOverrideMaterial(UMaterialInterface* Value)
{
	if (OverrideMaterial != Value)
	{
		OverrideMaterial = Value;
		MarkMaterialDirty();
		// And the quads: whether small sizes draw from coverage glyphs depends on it (only the built-in shading reads them),
		// and the gate is asked again only when the text is repainted.
		MarkVerticesDirty(true, true, true, false);
	}
}

void UDreamText::SetExpandMeshSize(float Value)
{
	if (ExpandMeshSize != Value)
	{
		ExpandMeshSize = Value;
		MarkVerticesDirty(false, true, true, false);
	}
}
void UDreamText::SetDynamicPixelsPerUnit(float Value)
{
	if (DynamicPixelsPerUnit != Value)
	{
		DynamicPixelsPerUnit = Value;
		MarkVerticesDirty(false, true, true, false);
	}
}

void UDreamText::ClearCreatedRichTextImageObject()
{
	for (auto& ImageObj : CreatedRichTextImageObjectArray)
	{
		if (IsValid(ImageObj))
		{
			ImageObj->DestroyWidget();
		}
	}
	CreatedRichTextImageObjectArray.Empty();
}

void UDreamText::ClearEmojiObject()
{
	for (auto& ItemObj : CreatedEmojiObjectArray)
	{
		if (IsValid(ItemObj))
		{
			ItemObj->DestroyWidget();
		}
	}
	CreatedEmojiObjectArray.Empty();
}

void UDreamText::RegisterFont()
{
	// Added to another font than Font -- an undo put this one back with nothing told first, or the one added to is gone:
	// taken off that one, and added to this one.
	if (bHasAddToFont && FontAddedTo.Get() != Font.Get())
	{
		UnregisterFont();
	}
	if (!bHasAddToFont)
	{
		bHasAddToFont = true;
		FontAddedTo = Font.Get();
		Font->AddUIText(this);
		EmojiDataChangedDelegateHandle = Font->OnEmojiDataChanged.AddWeakLambda(this, [=, this]()
		{
			ClearEmojiObject();
			// The same font with other emoji data: the emoji are sized and placed by the layout, which cannot tell.
			MarkLayoutDirty();
			MarkVerticesDirty(true, true, true, true);
		});
		GlyphsReadyDelegateHandle = Font->OnGlyphsReady.AddWeakLambda(this, [this]()
		{
			// Only texts that were laid out with missing quads care; the advances were already right,
			// so this is a repaint with the quads filled in, not a size change -- but the quads are in the
			// display list, so it is laid out again to take them.
			if (bWaitingForGlyphs)
			{
				bWaitingForGlyphs = false;
				MarkLayoutDirty();
				MarkVerticesDirty(true, true, true, true);
			}
		});
		CoverageGlyphsChangedDelegateHandle = Font->OnCoverageGlyphsChanged.AddWeakLambda(this, [this]()
		{
			// Coverage glyphs landed, failed, or were flushed with their cells. A coverage glyph only stands in for a quad
			// and never changes an advance, so this is a repaint -- positions and UVs -- and never a layout; and only for a
			// text whose last paint drew from coverage or waited for it.
			if ((SmallTextReport.CoverageItems > 0 || SmallTextReport.PendingItems > 0) && GetWidget() != nullptr)
			{
				MarkVerticesDirty(true, true, true, false);
			}
		});
	}
}

void UDreamText::UnregisterFont()
{
	// Done with the font: the coverage cells of the epoch it held may go back to the pool.
	HoldCoverageEpoch(nullptr, 0);
	if (bHasAddToFont)
	{
		bHasAddToFont = false;
		// The font it was added to, which is not Font once an undo has put another there; one that is gone has nothing left
		// to take it off.
		if (UDreamUIFontData_BaseObject* const AddedTo = FontAddedTo.Get())
		{
			AddedTo->RemoveUIText(this);
			AddedTo->OnEmojiDataChanged.Remove(EmojiDataChangedDelegateHandle);
			AddedTo->OnGlyphsReady.Remove(GlyphsReadyDelegateHandle);
			AddedTo->OnCoverageGlyphsChanged.Remove(CoverageGlyphsChangedDelegateHandle);
		}
		FontAddedTo.Reset();
		EmojiDataChangedDelegateHandle.Reset();
		GlyphsReadyDelegateHandle.Reset();
		CoverageGlyphsChangedDelegateHandle.Reset();
		bWaitingForGlyphs = false;
	}
}

void UDreamText::UpdateCacheTextGeometry()const
{
	if (!IsValid(this->GetFont()))return;
	auto Widget = GetWidget();
	// Layout used to be skipped entirely without a render canvas; the canvas supplied the root scale
	// and the world-space flag. Those are inputs now, so keep the same gate rather than laying out
	// against guessed values and caching the result.
	if (!Widget->GetRenderCanvas())return;

	FVector2f ContentSize, ContentPivot;
	GetContentBox(FVector2f(Widget->GetWidth(), Widget->GetHeight()), FVector2f(Widget->GetPivot()), Margin,
		ContentSize, ContentPivot);

	bool bAnyLayoutRan = false;
	auto LayOutAt = [&](float InFontSize)
	{
		CacheTextGeometryData.SetLayoutInput(MakeLayoutInput(this, InFontSize));
		bAnyLayoutRan |= CacheTextGeometryData.EnsureLayout();
		return CacheTextGeometryData.GetPreferredSize();
	};

	RenderedFontSize = this->GetFontSize();
	if (bBestFit && ContentSize.X > 0.0f && ContentSize.Y > 0.0f)
	{
		// FontSize is the ceiling here, not the size drawn: Best Fit looks for the largest that
		// fits and only then lays out at it. Each probe is a measure-only layout, so the search
		// costs no geometry, and its answer is remembered against the ceiling input so a repeat
		// query with nothing changed costs no layout at all.
		const FDreamTextLayoutInput CeilingInput = MakeLayoutInput(this, this->GetFontSize());
		if (!CacheTextGeometryData.TryGetBestFit(CeilingInput, RenderedFontSize))
		{
			RenderedFontSize = FindBestFitFontSize(ContentSize, BestFitMinSize, this->GetFontSize(), LayOutAt);
			CacheTextGeometryData.SetBestFit(CeilingInput, RenderedFontSize);
		}
	}
	LayOutAt(RenderedFontSize);
	if (bAnyLayoutRan)
	{
		bWaitingForGlyphs = CacheTextGeometryData.GetDisplayList().bHasPendingGlyphs;
	}

	if (bAnyLayoutRan)
	{
		// Inline objects are widgets; they follow the layout, not the paint.
		auto MutableThis = const_cast<UDreamText*>(this);
		MutableThis->GenerateRichTextImageObject();
		MutableThis->GenerateEmojiObject();
		// The tag colour overrides name their tags by index, and this layout may have put other tags at those indices.
		// They are taken off before anything paints with them: a layout only runs for a change that has asked for a
		// repaint already, and that repaint reads what is left.
		MutableThis->DropMovedTagColorOverrides();
	}
}

void UDreamText::ConditionalUpdateCacheTextGeometry() const
{
	/**
	 * RichTextImageData and EmojiData could cause create or delete widget, so we should make it happen before Canvas-Update,
	 * because unexpected thing will happed if we create or delete widget during Canvas-Update.
	 */
	if (IsValid(RichTextImageData) || (IsValid(Font) && IsValid(Font->GetEmojiData())))
	{
		UpdateCacheTextGeometry();
	}
}

void UDreamText::MarkLayoutDirty()
{
	CacheTextGeometryData.MarkDirty();
	if (MeasureAtWidthCache.IsValid())
	{
		MeasureAtWidthCache->MarkDirty();
	}
}

void UDreamText::MarkTextureDirty()
{
	// A new atlas, or the same one refilled: every glyph's UVs in the display list are stale.
	MarkLayoutDirty();
	Super::MarkTextureDirty();
}

void UDreamText::MarkAllDirty()
{
	// Everything, as the canvas asks when the text moves to another canvas or render mode, and the editor after any edit.
	MarkLayoutDirty();
	Super::MarkAllDirty();
}
int UDreamText::VisibleCharCountInString(const FString& srcStr)
{
	int count = srcStr.Len();
	if (count == 0)return 0;
	int result = 0;
	for (int i = 0; i < count; i++)
	{
		auto charIndexItem = srcStr[i];
		if (IsVisibleChar(charIndexItem) == false)
		{
			continue;
		}
		result++;
	}
	return result;
}

const TArray<FDreamUITextCharProperty>& UDreamText::GetCharPropertyArray()const
{
	UpdateCacheTextGeometry();
	return CacheTextGeometryData.GetCharPropertyArray();
}
int32 UDreamText::GetVisibleCharCount()const
{
	UpdateCacheTextGeometry();
	return CacheTextGeometryData.GetCharPropertyArray().Num();
}
const TArray<FDreamUIText_RichTextCustomTag>& UDreamText::GetRichTextCustomTagArray()const
{
	UpdateCacheTextGeometry();
	return CacheTextGeometryData.GetCustomTags();
}
const TArray<FDreamUIText_RichTextImageTag>& UDreamText::GetRichTextImageTagArray()const
{
	UpdateCacheTextGeometry();
	return CacheTextGeometryData.GetImageTags();
}

void UDreamText::GenerateRichTextImageObject()
{
	if (!IsValid(RichTextImageData))return;
	RichTextImageData->CreateOrUpdateObject(this->GetWidget(), CacheTextGeometryData.GetImageTags(), CreatedRichTextImageObjectArray);
}

void UDreamText::GenerateEmojiObject()
{
	if (auto EmojiData = Font->GetEmojiData())
	{
		EmojiData->CreateOrUpdateObject(this->GetWidget(), CacheTextGeometryData.GetEmojis(), CreatedEmojiObjectArray);
	}
}

/*
 * UpdateCacheTextGeometry gives up on two conditions -- no font, and no render canvas, the latter
 * being the state of every text in a headless test and in a Blueprint authoring tree -- and when it
 * does, the display list it would have filled is still the empty one it was constructed with, whose
 * PreferredSize is (0,0).
 *
 * Returning that plus the margins is a claim: zero means "this text wants no room", and a caller
 * cannot tell it apart from a text that measured itself and found nothing. It is not a theoretical
 * problem. A ring menu whose labels had not laid out yet measured its whole ring at nothing,
 * because each label answered 0 where it should have abstained, and 0 wins a Max against -1.
 *
 * LayoutRunCount is the honest test for "has a measurement ever produced this display list", and it
 * separates the two zeros: a laid-out empty string may legitimately answer 0 (it really does want
 * no room), while a text that has never been laid out has nothing to say.
 */
float UDreamText::GetPreferredWidth() const
{
	UpdateCacheTextGeometry();
	if (CacheTextGeometryData.GetLayoutRunCount() == 0)
	{
		return -1;
	}
	// textPreferredSize measures the glyphs, which were laid out inside the inset box, so the
	// padding has to be added back or a content-sized parent would squeeze it straight out again.
	// MinDesiredWidth is a floor under the answer, not part of the measurement.
	return FMath::Max(CacheTextGeometryData.GetPreferredSize().X + Margin.Left + Margin.Right, MinDesiredWidth);
}

float UDreamText::GetPreferredHeight() const
{
	UpdateCacheTextGeometry();
	if (CacheTextGeometryData.GetLayoutRunCount() == 0)
	{
		return -1;
	}
	return CacheTextGeometryData.GetPreferredSize().Y + Margin.Top + Margin.Bottom;
}

FVector2f UDreamText::GetPreferredSizeWithin(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	const FVector2f Own(GetPreferredWidth(), GetPreferredHeight());
	// Only a text that wraps at its box has a height that depends on the width it is given -- WrapTextAt is a width of its
	// own, and Best Fit picks its size against the box it has -- and only a bounded offer says what that width will be.
	const bool bWrapsAtItsBox = (OverflowType == EDreamUITextOverflowType::VerticalOverflow || bAutoWrapText)
		&& WrapTextAt <= 0.0f && !bBestFit;
	if (!bWrapsAtItsBox || Own.X < 0.0f || Own.Y < 0.0f)
	{
		return Own;
	}
	if (!InWidthSpec.IsBounded())
	{
		// With no width offered, the height answered is the paragraph wrapped at the width the widget has now: what the
		// previous pass wrote, or what it was authored at. The arranging panel is told it was read, and measures this
		// text again once it has given it its width (FDreamLayoutPassContext::FCurrentSizeReadScope).
		if (const UDreamWidget* Widget = GetWidget())
		{
			Widget->GetLayoutPassContext().NoteCurrentSizeRead();
		}
		return Own;
	}
	const float Offered = FMath::Max(0.0f, InWidthSpec.Value);
	// Laid out at the width it has, and offered that width: the answer it has is the one asked for.
	if (FMath::IsNearlyEqual(static_cast<float>(GetWidget()->GetWidth()), Offered, 0.5f))
	{
		return Own;
	}
	FDreamTextLayoutInput Input = MakeLayoutInput(this, this->GetFontSize());
	Input.Width = FMath::Max(0.0f, Offered - Margin.Left - Margin.Right);
	// No height: a box would cut a clamped paragraph's lines, and what is asked is how tall the paragraph is at this width.
	Input.Height = 0.0f;
	if (!MeasureAtWidthCache.IsValid())
	{
		MeasureAtWidthCache = MakeUnique<FDreamUITextGeometryCache>();
	}
	MeasureAtWidthCache->SetLayoutInput(Input);
	MeasureAtWidthCache->EnsureLayout();
	const FVector2f Measured = MeasureAtWidthCache->GetPreferredSize();
	// The layout's preferred width is the paragraph on one line; wrapped at the offer it is no wider than the offer.
	const float Width = FMath::Max(FMath::Min(Measured.X + Margin.Left + Margin.Right, Offered), MinDesiredWidth);
	return FVector2f(Width, Measured.Y + Margin.Top + Margin.Bottom);
}


bool UDreamText::MoveCaret(int32 moveType, int32& inOutCaretPositionIndex, int32& inOutCaretPositionLineIndex, FVector2f& inOutCaretPosition)
{
	auto originCaretPositionIndex = inOutCaretPositionIndex;
	auto originCaretPositionLineIndex = inOutCaretPositionLineIndex;

	UpdateCacheTextGeometry();
	auto& cacheLinePropertyArray = CacheTextGeometryData.GetLines();
	// A text that has never been laid out -- no font, or no render canvas -- has no lines, and every move
	// below reads one: the last line for left/right past the end, the caller's line for up/down, the first
	// and last for start/end. There is no caret to move, so nothing moves.
	if (cacheLinePropertyArray.Num() == 0)
	{
		return false;
	}
	//moveType 0-left, 1-right, 2-up, 3-down, 4-start, 5-end
	switch (moveType)
	{
	case 0:
	case 1:
	{
		if (moveType == 0)
		{
			if (inOutCaretPositionIndex > 0)
			{
				inOutCaretPositionIndex--;
			}
		}
		else
		{
			inOutCaretPositionIndex++;
		}

		bool foundCaret = false;
		int totalCaretIndex = 0;
		for (int lineIndex = 0; lineIndex < cacheLinePropertyArray.Num(); lineIndex++)
		{
			auto& lineProperty = cacheLinePropertyArray[lineIndex];
			for (int caretIndex = 0; caretIndex < lineProperty.CaretPropertyList.Num(); caretIndex++)
			{
				if (totalCaretIndex == inOutCaretPositionIndex)//find caret
				{
					inOutCaretPositionLineIndex = lineIndex;
					inOutCaretPosition = lineProperty.CaretPropertyList[caretIndex].CaretPosition;
					//stop loop
					foundCaret = true;
					caretIndex = lineProperty.CaretPropertyList.Num();
					lineIndex = cacheLinePropertyArray.Num();
				}
				else
				{
					totalCaretIndex++;
				}
			}
		}
		if (!foundCaret)//could be out of range, use last caret
		{
			inOutCaretPositionIndex = totalCaretIndex - 1;
			inOutCaretPositionLineIndex = cacheLinePropertyArray.Num() - 1;
			auto& lastLineProperty = cacheLinePropertyArray[cacheLinePropertyArray.Num() - 1];
			inOutCaretPosition = lastLineProperty.CaretPropertyList[lastLineProperty.CaretPropertyList.Num() - 1].CaretPosition;
		}
	}
	break;
	case 2:
	case 3:
	{
		if (moveType == 2)
		{
			if (inOutCaretPositionLineIndex > 0)
			{
				inOutCaretPositionLineIndex--;
			}
		}
		else
		{
			if (inOutCaretPositionLineIndex < cacheLinePropertyArray.Num() - 1)
			{
				inOutCaretPositionLineIndex++;
			}
		}
		// The line index is the caller's, remembered from an earlier layout: the text may have lost lines since.
		inOutCaretPositionLineIndex = FMath::Clamp(inOutCaretPositionLineIndex, 0, cacheLinePropertyArray.Num() - 1);
		auto& lineProperty = cacheLinePropertyArray[inOutCaretPositionLineIndex];
		float minDistance = MAX_FLT;
		int accumulatedCaretIndex = 0;
		for (int lineIndex = 0; lineIndex < inOutCaretPositionLineIndex; lineIndex++)
		{
			accumulatedCaretIndex += cacheLinePropertyArray[lineIndex].CaretPropertyList.Num();
		}
		auto originCaretPosition = inOutCaretPosition;
		// Over the whole line, ties to the later caret, as FindCaretByWorldPosition: carets in the order of the text are
		// not left to right where directions mix or negative letter spacing overlaps the glyphs, so the first caret
		// further away than the one before it is not where the nearest one has been passed.
		for (int caretIndex = 0; caretIndex < lineProperty.CaretPropertyList.Num(); caretIndex++)
		{
			auto& caretProperty = lineProperty.CaretPropertyList[caretIndex];
			auto distance = FMath::Abs(originCaretPosition.X - caretProperty.CaretPosition.X);
			if (distance <= minDistance)
			{
				minDistance = distance;
				inOutCaretPositionIndex = accumulatedCaretIndex;
				inOutCaretPosition = caretProperty.CaretPosition;
			}
			accumulatedCaretIndex++;
		}
	}
	break;
	case 4:
	{
		inOutCaretPositionIndex = 0;
		inOutCaretPositionLineIndex = 0;
		inOutCaretPosition = cacheLinePropertyArray[0].CaretPropertyList[0].CaretPosition;
	}
	break;
	case 5:
	{
		int32 accumulatedCaretIndex = 0;
		for (int lineIndex = 0; lineIndex < cacheLinePropertyArray.Num(); lineIndex++)
		{
			accumulatedCaretIndex += cacheLinePropertyArray[lineIndex].CaretPropertyList.Num();
		}
		inOutCaretPositionIndex = accumulatedCaretIndex - 1;
		inOutCaretPositionLineIndex = cacheLinePropertyArray.Num() - 1;
		auto& lastLineProperty = cacheLinePropertyArray[cacheLinePropertyArray.Num() - 1];
		inOutCaretPosition = lastLineProperty.CaretPropertyList[lastLineProperty.CaretPropertyList.Num() - 1].CaretPosition;
	}
	break;
	}
	if (originCaretPositionIndex != inOutCaretPositionIndex || originCaretPositionLineIndex != inOutCaretPositionLineIndex)
	{
		return true;
	}
	return false;
}

int UDreamText::GetCharIndexByCaretIndex(int32 inCaretPositionIndex)
{
	UpdateCacheTextGeometry();
	auto& cacheLinePropertyArray = CacheTextGeometryData.GetLines();
	// A text that has never been laid out has no lines: no font, or no render canvas, which is the
	// state of every text in a headless test and in a Blueprint authoring tree. FindCaretByIndex has
	// always handled it; the rest of the caret family indexed into the empty array instead.
	if (cacheLinePropertyArray.Num() == 0)
	{
		return 0;
	}
	int accumulatedCaretIndex = 0;
	// The caret that ends a soft-wrapped line names no character (its CharIndex is -1): the wrap is not in
	// the text, so the end of the wrapped line and the start of the next are ONE position in it. That
	// caret answers with the next caret's offset, which is the start of the next line. Handed back as -1,
	// it was inserted at, deleted from and selected to as if it were an offset -- a write before the
	// string's buffer, a deletion from the start of the text.
	bool bFoundSoftWrapCaret = false;
	for (int lineIndex = 0; lineIndex < cacheLinePropertyArray.Num(); lineIndex++)
	{
		auto& lineProperty = cacheLinePropertyArray[lineIndex];
		for (int caretIndex = 0; caretIndex < lineProperty.CaretPropertyList.Num(); caretIndex++)
		{
			if (bFoundSoftWrapCaret || accumulatedCaretIndex == inCaretPositionIndex)//find caret
			{
				const int32 CharIndex = lineProperty.CaretPropertyList[caretIndex].CharIndex;
				if (CharIndex >= 0)
				{
					return CharIndex;
				}
				bFoundSoftWrapCaret = true;
			}
			accumulatedCaretIndex++;
		}
	}
	//not found caret, use last one
	auto& lastLineProperty = cacheLinePropertyArray[cacheLinePropertyArray.Num() - 1];
	if (lastLineProperty.CaretPropertyList.Num() == 0)
	{
		return 0;
	}
	return FMath::Max(0, lastLineProperty.CaretPropertyList[lastLineProperty.CaretPropertyList.Num() - 1].CharIndex);
}
int UDreamText::GetLastCaret()
{
	UpdateCacheTextGeometry();
	auto& cacheLinePropertyArray = CacheTextGeometryData.GetLines();
	int totalCaretIndex = 0;
	for (int lineIndex = 0; lineIndex < cacheLinePropertyArray.Num(); lineIndex++)
	{
		auto& lineProperty = cacheLinePropertyArray[lineIndex];
		totalCaretIndex += lineProperty.CaretPropertyList.Num();
	}
	return totalCaretIndex - 1;
}
//caret is at left side of char
void UDreamText::FindCaretByIndex(int32& inOutCaretPositionIndex, FVector2f& outCaretPosition, int32& outCaretPositionLineIndex, int32& outVisibleCaretStartIndex)
{
	UpdateCacheTextGeometry();
	auto& cacheLinePropertyArray = CacheTextGeometryData.GetLines();

	auto Widget = GetWidget();
	if (inOutCaretPositionIndex < 0)inOutCaretPositionIndex = 0;
	outCaretPosition.X = outCaretPosition.Y = 0;
	outCaretPositionLineIndex = 0;
	outVisibleCaretStartIndex = 0;
	if (cacheLinePropertyArray.Num() == 0)
	{
		float pivotOffsetX = Widget->GetWidth() * (0.5f - Widget->GetPivot().X);
		float pivotOffsetY = Widget->GetHeight() * (0.5f - Widget->GetPivot().Y);
		switch (HAlign)
		{
		case EDreamUITextParagraphHorizontalAlign::Left:
		// An empty line has nothing to spread: a justified paragraph's lone caret stands at the start, as its last line would.
		case EDreamUITextParagraphHorizontalAlign::Justify:
		{
			outCaretPosition.X = pivotOffsetX - Widget->GetWidth() * 0.5f;
		}
			break;
		case EDreamUITextParagraphHorizontalAlign::Center:
		{
			outCaretPosition.X = pivotOffsetX;
		}
			break;
		case EDreamUITextParagraphHorizontalAlign::Right:
		{
			outCaretPosition.X = pivotOffsetX + Widget->GetWidth() * 0.5f;
		}
			break;
		}
		switch (VAlign)
		{
		case EDreamUITextParagraphVerticalAlign::Top:
		{
			outCaretPosition.Y = pivotOffsetY + Widget->GetHeight() * 0.5f - FontSize * 0.5f;//fixed offset
		}
			break;
		case EDreamUITextParagraphVerticalAlign::Middle:
		{
			outCaretPosition.Y = pivotOffsetY;
		}
			break;
		case EDreamUITextParagraphVerticalAlign::Bottom:
		{
			outCaretPosition.Y = pivotOffsetY - Widget->GetHeight() * 0.5f + FontSize * 0.5f;//fixed offset
		}
			break;
		}
	}
	else
	{
		if (inOutCaretPositionIndex == 0)//first char
		{
			outCaretPosition = cacheLinePropertyArray[0].CaretPropertyList[0].CaretPosition;
			outCaretPositionLineIndex = 0;
			outVisibleCaretStartIndex = 0;
		}
		else//not first char
		{
			bool foundCaret = false;
			int accumulatedCaretIndex = 0;
			for (int lineIndex = 0; lineIndex < cacheLinePropertyArray.Num(); lineIndex++)
			{
				auto& lineProperty = cacheLinePropertyArray[lineIndex];
				for (int caretIndex = 0; caretIndex < lineProperty.CaretPropertyList.Num(); caretIndex++)
				{
					if (accumulatedCaretIndex == inOutCaretPositionIndex)//find caret
					{
						outCaretPositionLineIndex = lineIndex;
						outCaretPosition = lineProperty.CaretPropertyList[caretIndex].CaretPosition;
						outVisibleCaretStartIndex = accumulatedCaretIndex;
						//stop loop
						foundCaret = true;
						caretIndex = lineProperty.CaretPropertyList.Num();
						lineIndex = cacheLinePropertyArray.Num();
					}
					else
					{
						accumulatedCaretIndex++;
					}
				}
			}
			if (!foundCaret)//could be out of range
			{
				auto& lastLineProperty = cacheLinePropertyArray[cacheLinePropertyArray.Num() - 1];
				inOutCaretPositionIndex = accumulatedCaretIndex - 1;
				outCaretPosition = lastLineProperty.CaretPropertyList[lastLineProperty.CaretPropertyList.Num() - 1].CaretPosition;
				outCaretPositionLineIndex = cacheLinePropertyArray.Num() - 1;
				outVisibleCaretStartIndex = 0;
			}
		}
	}
}
void UDreamText::FindCaret(FVector2f& inOutCaretPosition, int32 inCaretPositionLineIndex, int32& outCaretPositionIndex)
{
	if (Text.ToString().Len() == 0)//no text
		return;
	UpdateCacheTextGeometry();
	auto& cacheTextPropertyArray = CacheTextGeometryData.GetLines();
	auto lineCount = cacheTextPropertyArray.Num();//line count
	outCaretPositionIndex = 0;
	// Non-empty text is not the same thing as laid-out text: without a font or a render canvas the
	// layout is skipped and there are no lines at all. The line index is the caller's, too.
	if (!cacheTextPropertyArray.IsValidIndex(inCaretPositionLineIndex))
	{
		return;
	}

	//find nearest char to caret from this line
	auto& lineItem = cacheTextPropertyArray[inCaretPositionLineIndex];
	int charPropertyCount = lineItem.CaretPropertyList.Num();//char count of this line
	float nearestDistance = MAX_FLT;
	int32 nearestIndex = -1;
	for (int charPropertyIndex = 0; charPropertyIndex < charPropertyCount; charPropertyIndex++)
	{
		auto& charItem = lineItem.CaretPropertyList[charPropertyIndex];
		float distance = FMath::Abs(charItem.CaretPosition.X - inOutCaretPosition.X);
		if (distance <= nearestDistance)
		{
			nearestDistance = distance;
			nearestIndex = charPropertyIndex;
			outCaretPositionIndex = charItem.CharIndex;
		}
	}
	if (nearestIndex == INDEX_NONE)//a line with no carets leaves the caret where the caller had it
	{
		return;
	}
	inOutCaretPosition = lineItem.CaretPropertyList[nearestIndex].CaretPosition;
}
//find caret by position, caret is on left side of char
void UDreamText::FindCaretByWorldPosition(FVector inWorldPosition, FVector2f& outCaretPosition, int32& outCaretPositionLineIndex, int32& outCaretPositionIndex)
{
	UpdateCacheTextGeometry();
	// Empty text and never-laid-out text land in the same place: FindCaretByIndex is the one function
	// of the family that already knows how to answer without any lines.
	if (Text.ToString().Len() == 0 || CacheTextGeometryData.GetLines().Num() == 0)
	{
		outCaretPositionIndex = 0;
		int tempVisibleCharStartIndex = 0;
		FindCaretByIndex(outCaretPositionIndex, outCaretPosition, outCaretPositionLineIndex, tempVisibleCharStartIndex);
	}
	else
	{
		auto& cacheLinePropertyArray = CacheTextGeometryData.GetLines();

		auto localPosition = GetWidget()->GetWorldTransform().InverseTransformPosition(inWorldPosition);
		auto localPosition2D = FVector2f(localPosition.Y, localPosition.Z);

		// The line first, by height alone: every caret of a line stands on the line's centre.
		float nearestDistance = MAX_FLT;
		int32 nearestLineIndex = INDEX_NONE;
		int32 nearestLineFirstCaretIndex = 0;
		int32 accumulatedCaretIndex = 0;
		for (int32 lineIndex = 0; lineIndex < cacheLinePropertyArray.Num(); lineIndex++)
		{
			const auto& caretList = cacheLinePropertyArray[lineIndex].CaretPropertyList;
			if (caretList.Num() > 0)
			{
				const float distance = FMath::Abs(caretList[0].CaretPosition.Y - localPosition2D.Y);
				if (distance <= nearestDistance)
				{
					nearestDistance = distance;
					nearestLineIndex = lineIndex;
					nearestLineFirstCaretIndex = accumulatedCaretIndex;
				}
			}
			accumulatedCaretIndex += caretList.Num();
		}
		if (nearestLineIndex == INDEX_NONE)
		{
			outCaretPositionIndex = 0;
			int tempVisibleCharStartIndex = 0;
			FindCaretByIndex(outCaretPositionIndex, outCaretPosition, outCaretPositionLineIndex, tempVisibleCharStartIndex);
			return;
		}
		outCaretPositionLineIndex = nearestLineIndex;
		// Then the caret nearest by X over the WHOLE line, with ties going to the later caret, as FindCaret does.
		// Carets come in the order of the text, which is not left to right on a line that mixes directions -- the
		// carets of a right-to-left word run back across it -- nor where negative letter spacing overlaps the glyphs,
		// so the first caret further away than the one before it is not where the nearest one has been passed.
		nearestDistance = MAX_FLT;
		const auto& nearestLine = cacheLinePropertyArray[nearestLineIndex];
		for (int32 caretIndex = 0; caretIndex < nearestLine.CaretPropertyList.Num(); caretIndex++)
		{
			const auto& caretItem = nearestLine.CaretPropertyList[caretIndex];
			const float distance = FMath::Abs(caretItem.CaretPosition.X - localPosition2D.X);
			if (distance <= nearestDistance)
			{
				nearestDistance = distance;
				outCaretPositionIndex = nearestLineFirstCaretIndex + caretIndex;
				outCaretPosition = caretItem.CaretPosition;
			}
		}
	}
}

int UDreamText::GetCaretIndexByCharIndex(int32 inCharIndex)
{
	UpdateCacheTextGeometry();
	int accumulatedCaretIndex = 0;
	auto& cacheLinePropertyArray = CacheTextGeometryData.GetLines();
	// An offset no caret stands at is one inside a cluster the layout keeps whole -- between the halves of
	// a surrogate pair, inside an emoji sequence -- and its caret is the one just past that cluster. The
	// end of the text, which is where such an offset used to be sent, can be any distance away.
	int32 NearestCaretIndexAfter = INDEX_NONE;
	int32 NearestCharIndexAfter = MAX_int32;
	for (int lineIndex = 0; lineIndex < cacheLinePropertyArray.Num(); lineIndex++)
	{
		auto& lineProperty = cacheLinePropertyArray[lineIndex];
		for (int caretIndex = 0; caretIndex < lineProperty.CaretPropertyList.Num(); caretIndex++)
		{
			const int32 CaretCharIndex = lineProperty.CaretPropertyList[caretIndex].CharIndex;
			if (CaretCharIndex == inCharIndex)//find char
			{
				return accumulatedCaretIndex;
			}
			if (CaretCharIndex > inCharIndex && CaretCharIndex < NearestCharIndexAfter)
			{
				NearestCharIndexAfter = CaretCharIndex;
				NearestCaretIndexAfter = accumulatedCaretIndex;
			}
			accumulatedCaretIndex++;
		}
	}
	if (NearestCaretIndexAfter != INDEX_NONE)
	{
		return NearestCaretIndexAfter;
	}
	return accumulatedCaretIndex - 1;//not found, return last one
}

bool UDreamText::GetVisibleCharRangeForMultiLine(int32& inOutCaretPositionIndex, int32& inOutCaretPositionLineIndex, int32& inOutVisibleCaretStartLineIndex, int32& inOutVisibleCaretStartIndex, int inMaxLineCount, int32& outVisibleCharStartIndex, int32& outVisibleCharCount)
{
	UpdateCacheTextGeometry();
	auto& cacheLinePropertyArray = CacheTextGeometryData.GetLines();
	int accumulatedCaretIndex = 0;
	bool foundCaret = false;
	for (int lineIndex = 0; lineIndex < cacheLinePropertyArray.Num(); lineIndex++)
	{
		auto& lineProperty = cacheLinePropertyArray[lineIndex];
		for (int caretIndex = 0; caretIndex < lineProperty.CaretPropertyList.Num(); caretIndex++)
		{
			if (inOutCaretPositionIndex == accumulatedCaretIndex)//find caret
			{
				inOutCaretPositionLineIndex = lineIndex;
				lineIndex = cacheLinePropertyArray.Num();
				foundCaret = true;
				break;
			}
			else
			{
				accumulatedCaretIndex++;
			}
		}
	}
	if (!foundCaret)//could be last caret
	{
		inOutCaretPositionLineIndex = cacheLinePropertyArray.Num() - 1;
	}

	inOutCaretPositionLineIndex = FMath::Clamp(inOutCaretPositionLineIndex, 0, cacheLinePropertyArray.Num() - 1);

	if (inOutVisibleCaretStartLineIndex > inOutCaretPositionLineIndex)
	{
		inOutVisibleCaretStartLineIndex = inOutCaretPositionLineIndex;
	}
	if (inOutVisibleCaretStartLineIndex + (inMaxLineCount - 1) < inOutCaretPositionLineIndex)
	{
		inOutVisibleCaretStartLineIndex = inOutCaretPositionLineIndex - (inMaxLineCount - 1);
	}

	int calculatedLineCount = 0;
	bool outOfRange = false;
	int VisibleCaretEndLineIndex = inOutCaretPositionLineIndex;
	//check from CaretLineIndex to VisibleCaretStartLineIndex
	for (int lineIndex = inOutCaretPositionLineIndex; lineIndex >= 0 && lineIndex >= inOutVisibleCaretStartLineIndex; lineIndex--)
	{
		auto& lineProperty = cacheLinePropertyArray[lineIndex];
		calculatedLineCount++;
		if (calculatedLineCount >= inMaxLineCount)
		{
			outOfRange = true;
			inOutVisibleCaretStartLineIndex = lineIndex;
			break;
		}
	}
	if (!outOfRange)
	{
		//check from CaretLineIndex to bottom end
		for (int lineIndex = inOutCaretPositionLineIndex + 1; lineIndex < cacheLinePropertyArray.Num(); lineIndex++)
		{
			auto& lineProperty = cacheLinePropertyArray[lineIndex];
			calculatedLineCount++;
			VisibleCaretEndLineIndex++;
			if (calculatedLineCount >= inMaxLineCount)
			{
				outOfRange = true;
				break;
			}
		}

		if (!outOfRange)
		{
			//check from VisibleCaretStartLineIndex to top
			for (int lineIndex = inOutVisibleCaretStartLineIndex - 1; lineIndex >= 0 && lineIndex < cacheLinePropertyArray.Num(); lineIndex--)
			{
				auto& lineProperty = cacheLinePropertyArray[lineIndex];
				calculatedLineCount++;
				if (calculatedLineCount >= inMaxLineCount)
				{
					outOfRange = true;
					break;
				}
				inOutVisibleCaretStartLineIndex--;
			}
		}
	}
	inOutVisibleCaretStartIndex = 0;
	for (int lineIndex = 0; lineIndex < inOutVisibleCaretStartLineIndex; lineIndex++)
	{
		auto& lineProperty = cacheLinePropertyArray[lineIndex];
		inOutVisibleCaretStartIndex += lineProperty.CaretPropertyList.Num();
	}
	auto& startLineProperty = cacheLinePropertyArray[inOutVisibleCaretStartLineIndex];
	auto& endLineProperty = cacheLinePropertyArray[VisibleCaretEndLineIndex];
	outVisibleCharStartIndex = startLineProperty.CaretPropertyList[0].CharIndex;
	auto lastIndex = endLineProperty.CaretPropertyList.Num() - 1;
	auto lastCharIndex = endLineProperty.CaretPropertyList[lastIndex].CharIndex;
	if (lastCharIndex == -1)//-1 means newline break, so use next caret's char index
	{
		lastCharIndex = endLineProperty.CaretPropertyList[lastIndex - 1].CharIndex + 1;
	}
	outVisibleCharCount = lastCharIndex - outVisibleCharStartIndex;
	return outOfRange;
}

TArray<FDreamUIText_RichTextCustomTag> UDreamText::GetHyperlinks()const
{
	TArray<FDreamUIText_RichTextCustomTag> Result;
	for (const FDreamUIText_RichTextCustomTag& Tag : GetRichTextCustomTagArray())
	{
		if (Tag.bHyperlink)
		{
			Result.Add(Tag);
		}
	}
	return Result;
}

bool UDreamText::FindHyperlinkByWorldPosition(FVector InWorldPosition, FName& OutId)const
{
	OutId = NAME_None;
	const int32 TagIndex = FindHyperlinkIndexByWorldPosition(InWorldPosition);
	if (TagIndex == INDEX_NONE)return false;
	OutId = GetRichTextCustomTagArray()[TagIndex].TagName;
	return true;
}

int32 UDreamText::FindHyperlinkIndexByWorldPosition(FVector InWorldPosition)const
{
	const TArray<FDreamUIText_RichTextCustomTag>& Tags = GetRichTextCustomTagArray();
	if (Tags.Num() == 0)return INDEX_NONE;
	const TArray<FDreamUITextCharProperty>& Chars = GetCharPropertyArray();
	if (Chars.Num() == 0 || !UIGeometry.IsValid())return INDEX_NONE;
	const TArray<FDreamUIOriginVertexData>& Vertices = UIGeometry->OriginVertices;
	if (Vertices.Num() == 0)return INDEX_NONE;

	const FVector LocalPosition = GetWidget()->GetWorldTransform().InverseTransformPosition(InWorldPosition);
	const FVector2f Point(LocalPosition.Y, LocalPosition.Z);
	// A glyph's quad is tight to its ink, so a link made of "o"s would only be clickable across the
	// middle third of the line. A quarter of the font size on each side is about the em box, which is
	// what a pointer is aiming at.
	const float Padding = FMath::Max(RenderedFontSize, 1.0f) * 0.25f;

	for (int32 TagIndex = 0; TagIndex < Tags.Num(); TagIndex++)
	{
		const FDreamUIText_RichTextCustomTag& Tag = Tags[TagIndex];
		if (!Tag.bHyperlink)continue;
		for (int32 CharIndex = FMath::Max(0, Tag.CharIndexStart); CharIndex <= Tag.CharIndexEnd && CharIndex < Chars.Num(); CharIndex++)
		{
			const FDreamUITextCharProperty& Property = Chars[CharIndex];
			if (Property.VertCount <= 0)continue;
			const int32 LastVertex = Property.StartVertIndex + Property.VertCount - 1;
			if (!Vertices.IsValidIndex(Property.StartVertIndex) || !Vertices.IsValidIndex(LastVertex))continue;
			float MinX = MAX_FLT, MaxX = -MAX_FLT, MinY = MAX_FLT, MaxY = -MAX_FLT;
			for (int32 v = Property.StartVertIndex; v <= LastVertex; v++)
			{
				const FVector3f& Position = Vertices[v].Position;
				MinX = FMath::Min(MinX, Position.Y);
				MaxX = FMath::Max(MaxX, Position.Y);
				MinY = FMath::Min(MinY, Position.Z);
				MaxY = FMath::Max(MaxY, Position.Z);
			}
			if (Point.X >= MinX - Padding && Point.X <= MaxX + Padding
				&& Point.Y >= MinY - Padding && Point.Y <= MaxY + Padding)
			{
				return TagIndex;
			}
		}
	}
	return INDEX_NONE;
}

bool UDreamText::TryClickHyperlinkAtWorldPosition(FVector InWorldPosition)
{
	FName Id = NAME_None;
	if (!FindHyperlinkByWorldPosition(InWorldPosition, Id))
	{
		return false;
	}
	OnHyperlinkClickedCPP.Broadcast(Id);
	OnHyperlinkClickedBP.Broadcast(Id);
	return true;
}

void UDreamText::GetSelectionProperty(int32 InSelectionStartCaretIndex, int32 InSelectionEndCaretIndex, TArray<FDreamUITextSelectionProperty>& OutSelectionProeprtyArray)
{
	OutSelectionProeprtyArray.Reset();
	UpdateCacheTextGeometry();
	const TArray<FDreamUITextLineProperty>& Lines = CacheTextGeometryData.GetLines();
	// A text that was never laid out has nothing on screen to highlight.
	if (Lines.Num() == 0)
	{
		return;
	}
	// What is selected is a stretch of the text -- the offsets the two carets stand at, whichever way the selection was
	// made -- not a stretch of the screen. Where it lies on screen is the layout's visual runs' business.
	const int32 LastCaretIndex = FMath::Max(0, GetLastCaret());
	const int32 AnchorOffset = GetCharIndexByCaretIndex(FMath::Clamp(InSelectionStartCaretIndex, 0, LastCaretIndex));
	const int32 CaretOffset = GetCharIndexByCaretIndex(FMath::Clamp(InSelectionEndCaretIndex, 0, LastCaretIndex));
	const int32 SelectionStart = FMath::Min(AnchorOffset, CaretOffset);
	const int32 SelectionEnd = FMath::Max(AnchorOffset, CaretOffset);
	if (SelectionStart >= SelectionEnd)
	{
		return;
	}

	// Where an offset falls across a run. Its two ends are its edges -- the run's logical start is its left edge when
	// it reads left to right and its right edge when it reads right to left -- and inside, the caret standing at the
	// offset says, since the layout puts it on the edge between that character and the one before it.
	auto OffsetX = [](const FDreamTextVisualRun& InRun, const FDreamUITextLineProperty& InLine, int32 InOffset) -> float
	{
		if (InOffset <= InRun.SourceStart)
		{
			return InRun.bRightToLeft ? InRun.Right : InRun.Left;
		}
		if (InOffset >= InRun.SourceEnd)
		{
			return InRun.bRightToLeft ? InRun.Left : InRun.Right;
		}
		for (const FDreamUITextCaretProperty& Caret : InLine.CaretPropertyList)
		{
			if (Caret.CharIndex == InOffset)
			{
				return FMath::Clamp(Caret.CaretPosition.X, InRun.Left, InRun.Right);
			}
		}
		// No caret of its own, as inside a ligature: the run's share of the way to it.
		const float Fraction = (float)(InOffset - InRun.SourceStart) / (float)(InRun.SourceEnd - InRun.SourceStart);
		return InRun.bRightToLeft ? FMath::Lerp(InRun.Right, InRun.Left, Fraction) : FMath::Lerp(InRun.Left, InRun.Right, Fraction);
	};

	// One highlight wherever the selection meets a run, left to right along each line. On a line that mixes directions
	// one stretch of the text is several stretches of the screen -- "ab" + a right-to-left word + "cd", selected from
	// inside "ab" into the word, is the end of "ab" and, apart from it, the right end of the word -- and a single bar
	// from caret to caret covered what was not selected. Measured from the run's edges, no highlight is ever negative.
	for (const FDreamTextVisualRun& Run : CacheTextGeometryData.GetDisplayList().VisualRuns)
	{
		const int32 From = FMath::Max(SelectionStart, Run.SourceStart);
		const int32 To = FMath::Min(SelectionEnd, Run.SourceEnd);
		if (From >= To || !Lines.IsValidIndex(Run.LineIndex))
		{
			continue;
		}
		const FDreamUITextLineProperty& Line = Lines[Run.LineIndex];
		if (Line.CaretPropertyList.Num() == 0)
		{
			continue;
		}
		const float FromX = OffsetX(Run, Line, From);
		const float ToX = OffsetX(Run, Line, To);
		FDreamUITextSelectionProperty SelectionProperty;
		// The line's centre, where its carets stand and where the highlight's pivot is.
		SelectionProperty.Pos = FVector2f(FMath::Min(FromX, ToX), Line.CaretPropertyList[0].CaretPosition.Y);
		SelectionProperty.Size = FMath::RoundToInt(FMath::Abs(ToX - FromX));
		OutSelectionProeprtyArray.Add(SelectionProperty);
	}
}

namespace DreamTextTagColorLocal
{
	/** Whether two descriptions of a tag are the same tag in the same place: its name, its kind and its characters. */
	static bool IsSameTag(const FDreamUIText_RichTextCustomTag& A, const FDreamUIText_RichTextCustomTag& B)
	{
		return A.TagName == B.TagName && A.bHyperlink == B.bHyperlink
			&& A.CharIndexStart == B.CharIndexStart && A.CharIndexEnd == B.CharIndexEnd;
	}
}

void UDreamText::SetTagColorOverride(int32 InTagIndex, FColor InColor)
{
	if (InTagIndex < 0)return;
	// Read off the tags as they are now -- which lays a changed text out first, and takes off the overrides that change
	// moved -- so the override is on the tag at InTagIndex today, and stays on it only while it is there.
	const TArray<FDreamUIText_RichTextCustomTag>& Tags = GetRichTextCustomTagArray();
	FDreamUIText_RichTextCustomTag OnTag;
	if (Tags.IsValidIndex(InTagIndex))
	{
		OnTag = Tags[InTagIndex];
	}
	else
	{
		// No such tag yet, the text not laid out yet most likely: the next layout says which tag this is.
		OnTag.CharIndexStart = INDEX_NONE;
	}
	const int32 Existing = TagColorOverrides.IndexOfByPredicate([InTagIndex](const TPair<int32, FColor>& Override) { return Override.Key == InTagIndex; });
	if (Existing != INDEX_NONE)
	{
		TagColorOverrideTags[Existing] = OnTag;
		if (TagColorOverrides[Existing].Value == InColor)return;
		TagColorOverrides[Existing].Value = InColor;
	}
	else
	{
		TagColorOverrides.Emplace(InTagIndex, InColor);
		TagColorOverrideTags.Add(OnTag);
	}
	MarkTagColorsDirty();
}

void UDreamText::ClearTagColorOverride(int32 InTagIndex)
{
	bool bRemoved = false;
	for (int32 Index = TagColorOverrides.Num() - 1; Index >= 0; Index--)
	{
		if (TagColorOverrides[Index].Key == InTagIndex)
		{
			TagColorOverrides.RemoveAt(Index);
			TagColorOverrideTags.RemoveAt(Index);
			bRemoved = true;
		}
	}
	if (bRemoved)
	{
		MarkTagColorsDirty();
	}
}

void UDreamText::ClearTagColorOverrides()
{
	if (TagColorOverrides.Num() > 0)
	{
		TagColorOverrides.Reset();
		TagColorOverrideTags.Reset();
		MarkTagColorsDirty();
	}
}

void UDreamText::DropMovedTagColorOverrides()
{
	const TArray<FDreamUIText_RichTextCustomTag>& Tags = CacheTextGeometryData.GetCustomTags();
	for (int32 Index = TagColorOverrides.Num() - 1; Index >= 0; Index--)
	{
		const int32 TagIndex = TagColorOverrides[Index].Key;
		FDreamUIText_RichTextCustomTag& OnTag = TagColorOverrideTags[Index];
		if (Tags.IsValidIndex(TagIndex) && OnTag.CharIndexStart == INDEX_NONE)
		{
			OnTag = Tags[TagIndex];//put on before there was a tag at its index: this is the tag it is on
		}
		else if (!Tags.IsValidIndex(TagIndex) || !DreamTextTagColorLocal::IsSameTag(OnTag, Tags[TagIndex]))
		{
			TagColorOverrides.RemoveAt(Index);
			TagColorOverrideTags.RemoveAt(Index);
		}
	}
}

void UDreamText::MarkTagColorsDirty()
{
	// Colour is a paint input: the text is repainted and its layout left alone. Asked from teardown too (a hyperlink
	// giving its colour back as it goes), when the text may have no widget left to repaint in.
	if (GetWidget() != nullptr)
	{
		MarkVerticesDirty(false, false, false, true);
	}
}

bool UDreamText::GetTagColorOverride(int32 InTagIndex, FColor& OutColor)const
{
	// As the text will be painted: a changed text is laid out first, which takes off the overrides it moved.
	UpdateCacheTextGeometry();
	for (const TPair<int32, FColor>& Override : TagColorOverrides)
	{
		if (Override.Key == InTagIndex)
		{
			OutColor = Override.Value;
			return true;
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE



DECLARE_DREAM_GUI_VISUAL("Text", UDreamText)
