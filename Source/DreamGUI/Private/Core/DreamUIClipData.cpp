// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIClipData.h"

#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamVisual.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/Components/DreamCanvas.h"
#include "Engine/Texture2D.h"
#include "Utils/DreamUIUtils.h"

static_assert(sizeof(FMatrix44f) + sizeof(FVector4f) == DreamUIClipLayout::BaseTexels * DreamUIClipLayout::BytesPerTexel,
	"A level without a mask is its matrix and its corner radius.");

int FDreamUIClipData::InheritClipDepth = DreamUIClipLayout::Depth;
int FDreamUIClipData::SingleBlockSizeInBytes = DreamUIClipLayout::BaseTexels * DreamUIClipLayout::BytesPerTexel;
int FDreamUIClipData::BlockSizeInBytes = DreamUIClipLayout::RowBytes;

namespace DreamUIClipDataLocal
{
	/** What a chain's state warns about, each once while it lasts. */
	enum EWarning : uint32
	{
		UnsupportedUnderlay = 1u << 0,
		ThirdTexture = 1u << 1,
		UnreadableHitTexture = 1u << 2,
		TooDeep = 1u << 3,
	};

	/** The clip frame's centre, InCentre in InToWorld's local space, to world: the offset taken through the frame's own scale and rotation. */
	FMatrix44d CentreToWorld(const FMatrix44d& InToWorld, const FVector2D& InCentre)
	{
		return FMatrix44d(FTranslationMatrix(FVector(0.0, InCentre.X, InCentre.Y))) * InToWorld;
	}

	/** Into DreamUIClip_sdRoundRect's order: the widget's is right-bottom, right-top, left-top, left-bottom, with +y up. */
	FVector4f FlipCornerRadius(const FVector4f& InRadius)
	{
		return FVector4f(InRadius.Y, InRadius.X, InRadius.W, InRadius.Z);
	}

	/** InTexture's texel at InUV as the shader samples it: linear colour, its alpha as stored. False where the CPU cannot read it. */
	bool ReadTexel(UTexture* InTexture, FVector2f InUV, FLinearColor& OutTexel)
	{
		UTexture2D* Texture2D = Cast<UTexture2D>(InTexture);
		FColor Pixel;
		if (Texture2D == nullptr || !FDreamUIUtils::ReadTexture2DPixel(Texture2D, FVector2D(InUV), Pixel))
		{
			return false;
		}
		OutTexel = Texture2D->SRGB ? FLinearColor(Pixel) : Pixel.ReinterpretAsLinear();
		OutTexel.A = Pixel.A / 255.0f;
		return true;
	}
}

FDreamUIClipData::FDreamUIClipData(const TSharedPtr<FDreamUIClipData>& InParent, UDreamUIDataAsTexture* InDataTexture, UDreamWidget* InWidget, EKind InKind, UDreamWidget* InUnderlay)
{
	this->Parent = InParent;
	this->DataTexture = InDataTexture;
	this->Widget = InWidget;
	this->Kind = InKind;
	this->Underlay = InUnderlay;
	this->BufferStartPos = this->DataTexture->RegisterBuffer();
}

FDreamUIClipData::~FDreamUIClipData()
{
	if (DataTexture.IsValid())
	{
		this->DataTexture->UnregisterBuffer(this->BufferStartPos);
	}
}

FMatrix44d FDreamUIClipData::GetClipperToWorldMatrix(const UDreamWidget* InWidget)
{
	// Outside a perspective scope this is the call it always was. FMatrix and FTransform inversion
	// do not agree bit for bit, and a clip edge should not start moving by a fraction of a pixel to
	// support a feature the widget is not using.
	return InWidget->HasPerspectiveApplied()
		? FMatrix44d(InWidget->GetWorldMatrix())
		: InWidget->GetWorldTransform().ToMatrixWithScale();
}

FVector FDreamUIClipData::WorldPointToClipperLocal(const UDreamWidget* InWidget, const FVector& InWorldPoint)
{
	if (!InWidget->HasPerspectiveApplied())
	{
		return InWidget->GetWorldTransform().InverseTransformPosition(InWorldPoint);
	}
	return FVector(GetClipperToWorldMatrix(InWidget).Inverse().TransformPosition(InWorldPoint));
}

bool FDreamUIClipData::DescribeUnderlay(const UDreamWidget* InUnderlay, FDreamClipUnderlay& OutUnderlay)
{
	OutUnderlay = FDreamClipUnderlay();
	const UDreamVisual* Visual = IsValid(InUnderlay) ? InUnderlay->GetVisual() : nullptr;
	if (Visual == nullptr)
	{
		OutUnderlay.Unsupported = TEXT("it draws nothing of its own");
		return false;
	}
	if (!Visual->GetClipUnderlay(OutUnderlay))
	{
		OutUnderlay.Kind = FDreamClipUnderlay::EKind::None;
		return false;
	}
	// Its own opacity: what is above it is above the widget clipping to it too, and fades both. Nothing while it is not
	// drawn, as a hidden base layer hides what is clipped to it.
	OutUnderlay.Opacity = InUnderlay->GetRenderVisibleInHierarchy()
		? FDreamUIUtils::ByteToFloat01(Visual->GetColor().A) * Visual->GetColorMultiplier().A * InUnderlay->GetRenderOpacity()
		: 0.0f;
	return true;
}

bool FDreamUIClipData::FLevel::Pack(FVector4f OutMask[DreamUIClipLayout::MaskTexels]) const
{
	if (Node->GetKind() == EKind::Below)
	{
		FDreamUIClipMaskMath::PackUnderlay(Underlay, TextureSlot, OutMask);
		return true;
	}
	return Mask != nullptr && FDreamUIClipMaskMath::PackMask(*Mask, HalfSize, TextureSlot, OutMask);
}

bool FDreamUIClipData::DescribeLevel(FLevel& OutLevel) const
{
	OutLevel = FLevel();
	OutLevel.Node = this;
	const UDreamWidget* Owner = Widget.Get();
	if (Kind == EKind::Bounds)
	{
		OutLevel.Frame = Owner;
		FVector2D Centre = Owner->GetLocalSpaceCenter();
		const FMargin ClippingMargin = Owner->GetClippingMargin();
		Centre += FVector2D(ClippingMargin.Right - ClippingMargin.Left, ClippingMargin.Top - ClippingMargin.Bottom) * 0.5f;
		OutLevel.CentreToWorld = DreamUIClipDataLocal::CentreToWorld(GetClipperToWorldMatrix(Owner), Centre);
		OutLevel.HalfSize = (FVector2f(Owner->GetWidth(), Owner->GetHeight()) + ClippingMargin.GetDesiredSize2f()) * 0.5f;
		OutLevel.CornerRadius = Owner->GetClippingCornerRadius();
		const FDreamClipMask& Mask = Owner->GetClipMask();
		if (Mask.IsActive())
		{
			OutLevel.Mask = &Mask;
			OutLevel.Texture = Mask.GetActiveTexture();
		}
		return true;
	}
	const UDreamWidget* Below = Underlay.Get();
	if (!IsValid(Below) || !DescribeUnderlay(Below, OutLevel.Underlay))
	{
		return false;
	}
	OutLevel.Frame = Below;
	OutLevel.CentreToWorld = DreamUIClipDataLocal::CentreToWorld(GetClipperToWorldMatrix(Below), FVector2D(OutLevel.Underlay.Center));
	OutLevel.HalfSize = OutLevel.Underlay.HalfSize;
	if (OutLevel.Underlay.Kind == FDreamClipUnderlay::EKind::Shape)
	{
		OutLevel.CornerRadius = OutLevel.Underlay.CornerRadius;
	}
	else
	{
		OutLevel.Texture = OutLevel.Underlay.Texture.Get();
	}
	return true;
}

void FDreamUIClipData::CollectLevels(TArray<FLevel, TInlineAllocator<16>>& OutLevels) const
{
	OutLevels.Reset();
	const FDreamUIClipData* Node = this;
	// Held while walked: the levels above are held weakly, by this one, and strongly only by the canvas's list.
	TSharedPtr<FDreamUIClipData> Held;
	while (Node != nullptr && OutLevels.Num() < InheritClipDepth)
	{
		//the clipper widgets up the chain are held weakly and are destroyed on their own schedule,
		//while this clip data lives until the canvas rebuilds its list: a clipper that is gone clips nothing
		if (!Node->Widget.IsValid())
		{
			break;
		}
		FLevel Level;
		if (Node->DescribeLevel(Level))
		{
			OutLevels.Add(Level);
		}
		Held = Node->Parent.Pin();
		Node = Held.Get();
	}
	// The chain's textures take the slots from the outside in: an outer clip's texture keeps its slot whatever is inside it.
	int32 NextSlot = 0;
	for (int32 Index = OutLevels.Num() - 1; Index >= 0; --Index)
	{
		if (OutLevels[Index].Texture != nullptr && NextSlot < DreamUIClipLayout::TextureSlots)
		{
			OutLevels[Index].TextureSlot = NextSlot++;
		}
	}
}

void FDreamUIClipData::GetMaskTextures(FDreamUIMaskTextures& OutTextures) const
{
	OutTextures = FDreamUIMaskTextures();
	TArray<UTexture*, TInlineAllocator<16>> Textures;
	const FDreamUIClipData* Node = this;
	TSharedPtr<FDreamUIClipData> Held;
	while (Node != nullptr && Textures.Num() < InheritClipDepth)
	{
		const UDreamWidget* Owner = Node->Widget.Get();
		if (Owner == nullptr)
		{
			break;
		}
		if (Node->Kind == EKind::Bounds)
		{
			const FDreamClipMask& Mask = Owner->GetClipMask();
			Textures.Add(Mask.IsActive() ? Mask.GetActiveTexture() : nullptr);
		}
		else
		{
			FDreamClipUnderlay Below;
			const bool bFollowed = Node->Underlay.IsValid() && DescribeUnderlay(Node->Underlay.Get(), Below);
			Textures.Add(bFollowed && Below.Kind == FDreamClipUnderlay::EKind::Image ? Below.Texture.Get() : nullptr);
		}
		Held = Node->Parent.Pin();
		Node = Held.Get();
	}
	int32 NextSlot = 0;
	for (int32 Index = Textures.Num() - 1; Index >= 0 && NextSlot < DreamUIClipLayout::TextureSlots; --Index)
	{
		if (Textures[Index] != nullptr)
		{
			OutTextures.Slots[NextSlot++] = Textures[Index];
		}
	}
}

void FDreamUIClipData::UpdateData()
{
	using namespace DreamUIClipDataLocal;
	UDreamWidget* Owner = this->GetWidget();
	if (!IsValid(Owner) || !Owner->GetRenderCanvas())
	{
		return;
	}
	if (Kind == EKind::Below)
	{
		/**
		 * The element below can change under this level without this widget being told: a sibling put in between, the one
		 * below made to clip to the layer below itself, or its own clip made anew, which this level sits in. A level that no
		 * longer matches is made again on the next update; until then it clips as it did.
		 */
		const UDreamWidget* Expected = Owner->FindClipToBelowUnderlay();
		const bool bOwnParent = Owner->GetClipping() != EDreamWidgetClipping::ClipToBoundsWithoutIntersecting && Owner->GetClipping() != EDreamWidgetClipping::Disabled;
		const TSharedPtr<FDreamUIClipData> ExpectedParent = (bOwnParent && IsValid(Expected)) ? Expected->GetClipData().Pin() : nullptr;
		if (Expected != Underlay.Get() || ExpectedParent != Parent.Pin())
		{
			Owner->MarkClipDirty(true);
		}
	}

	TArray<FLevel, TInlineAllocator<16>> Levels;
	CollectLevels(Levels);

	// Every element this chain clips binds the chain's textures; when they change, each is batched again with the new ones.
	FDreamUIMaskTextures Textures;
	for (const FLevel& Level : Levels)
	{
		if (Level.TextureSlot != INDEX_NONE)
		{
			Textures.Slots[Level.TextureSlot] = Level.Texture;
		}
	}
	const bool bTexturesChanged = !Textures.IsSameAs(LastMaskTextures);
	LastMaskTextures = Textures;
	if (bTexturesChanged)
	{
		Owner->MarkClipMaskTexturesChanged();
	}

	// What the chain warns about, each once while it lasts.
	uint32 Warnings = 0;
	{
		const FDreamUIClipData* Node = this;
		TSharedPtr<FDreamUIClipData> Held;
		int32 Count = 0;
		while (Node != nullptr && Node->Widget.IsValid())
		{
			if (++Count > InheritClipDepth)
			{
				Warnings |= TooDeep;
				break;
			}
			Held = Node->Parent.Pin();
			Node = Held.Get();
		}
	}
	FString UnsupportedReason;
	if (Kind == EKind::Below && Underlay.IsValid())
	{
		FDreamClipUnderlay Below;
		if (!DescribeUnderlay(Underlay.Get(), Below) && !Below.bPending)
		{
			Warnings |= UnsupportedUnderlay;
			UnsupportedReason = Below.Unsupported;
		}
	}
	for (const FLevel& Level : Levels)
	{
		if (Level.Texture != nullptr && Level.TextureSlot == INDEX_NONE && Level.Node == this)
		{
			Warnings |= ThirdTexture;
		}
		if (Level.Node == this && Level.Mask != nullptr && Level.Mask->HitTest == EDreamClipMaskHitTest::Mask
			&& Level.Texture != nullptr && !Level.Texture->IsA<UTexture2D>())
		{
			Warnings |= UnreadableHitTexture;
		}
	}
	const uint32 NewWarnings = Warnings & ~LastWarnings;
	LastWarnings = Warnings;
	if (NewWarnings != 0)
	{
		const FString Name = Owner->GetDisplayName();
		if (NewWarnings & TooDeep)
		{
			/**
			 * There is another clipper above the deepest level this block can carry, so it will not
			 * clip at all -- previously the loop just ended and the extra clippers were ignored in
			 * silence. The depth is fixed by the block layout here AND by DREAMUI_CLIP_DEPTH in
			 * DreamUIClip.ush; raising it means raising both.
			 */
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Widget '%s' is inside more than %d nested clipping widgets. Only the innermost %d clip it; the ones above are ignored.")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Name, InheritClipDepth, InheritClipDepth);
		}
		if (NewWarnings & UnsupportedUnderlay)
		{
			UE_LOG(DreamGUI, Warning, TEXT("Widget '%s' clips to the layer below, '%s', but %s, so it is not clipped. A simple or nine-sliced image or texture, or a rect block, can be clipped to.")
				, *Name, *Underlay->GetDisplayName(), *UnsupportedReason);
		}
		if (NewWarnings & ThirdTexture)
		{
			UE_LOG(DreamGUI, Warning, TEXT("Widget '%s' has a texture mask inside %d other texture masks: only the outermost %d of a chain of clips read their textures, so its texture is ignored and its shape alone clips.")
				, *Name, DreamUIClipLayout::TextureSlots, DreamUIClipLayout::TextureSlots);
		}
		if (NewWarnings & UnreadableHitTexture)
		{
			UE_LOG(DreamGUI, Warning, TEXT("Widget '%s' hit tests by its mask, whose texture is not a Texture2D the CPU can read: the texture counts as white for hits.")
				, *Name);
		}
	}

	bool bAnyMasked = false;
	for (const FLevel& Level : Levels)
	{
		bAnyMasked |= Level.IsMasked();
	}
	// A chain with no mask is the row it was before masks, byte for byte, and uploads that many bytes: the levels are where
	// they always were, and the zero that ends the chain is inside them.
	const int32 Bytes = bAnyMasked ? DreamUIClipLayout::RowBytes : DreamUIClipLayout::UnmaskedRowBytes;
	TArray<uint8> BlockBuffer;
	BlockBuffer.SetNumZeroed(Bytes);
	int BlockDataOffset = 0;
	auto RenderCanvasWidget = Owner->GetRenderCanvas()->GetWidget();
	const FMatrix44d CanvasToWorldMatrix = RenderCanvasWidget->GetWorldTransform().ToMatrixWithScale();
	for (const FLevel& Level : Levels)
	{
		FVector4f Mask[DreamUIClipLayout::MaskTexels];
		const bool bMasked = Level.Pack(Mask);
		auto CanvasToWidgetMatrix = FMatrix44f(CanvasToWorldMatrix * Level.CentreToWorld.Inverse());
		auto& M = CanvasToWidgetMatrix.M;
		M[0][3] = Level.HalfSize.X;//half width
		M[1][3] = Level.HalfSize.Y;//half height
		M[2][3] = bMasked ? 1.0f : 0.0f;//the mask's texels follow the corner radius
		M[3][3] = 1;//isValid
		CanvasToWidgetMatrix = CanvasToWidgetMatrix.GetTransposed();//matrix in memory is aligned as row-primary, so transpose it then in hlsl we can read as column-primary
		FMemory::Memcpy(BlockBuffer.GetData() + BlockDataOffset, &CanvasToWidgetMatrix, sizeof(FMatrix44f));
		BlockDataOffset += sizeof(FMatrix44f);
		const FVector4f CornerRadius = FlipCornerRadius(Level.CornerRadius);
		FMemory::Memcpy(BlockBuffer.GetData() + BlockDataOffset, &CornerRadius, sizeof(FVector4f));
		BlockDataOffset += sizeof(FVector4f);
		if (bMasked)
		{
			FMemory::Memcpy(BlockBuffer.GetData() + BlockDataOffset, Mask, sizeof(Mask));
			BlockDataOffset += sizeof(Mask);
		}
	}
	// Nothing moved since the last upload: skip the GPU write. This is what keeps a per-tick refresh cheap.
	if (LastUploadedBlock.Num() == BlockBuffer.Num()
		&& FMemory::Memcmp(LastUploadedBlock.GetData(), BlockBuffer.GetData(), BlockBuffer.Num()) == 0)
	{
		return;
	}
	LastUploadedBlock = BlockBuffer;
	DataTexture->UpdateBlock(0, BufferStartPos, MoveTemp(BlockBuffer), Bytes / DreamUIClipLayout::BytesPerTexel);
}

bool FDreamUIClipData::IsPointVisible(const FVector& WorldPoint) const
{
	TArray<FLevel, TInlineAllocator<16>> Levels;
	CollectLevels(Levels);
	for (const FLevel& Level : Levels)
	{
		if (Level.Node->GetKind() == EKind::Bounds && (Level.Mask == nullptr || Level.Mask->HitTest == EDreamClipMaskHitTest::Bounds))
		{
			// The clip rect alone, as it always was tested.
			UDreamWidget* TargetWidget = const_cast<UDreamWidget*>(Level.Frame);
			auto LocalPoint = WorldPointToClipperLocal(TargetWidget, WorldPoint);
			auto ClippingMargin = TargetWidget->GetClippingMargin();
			if (LocalPoint.Y < TargetWidget->GetLocalSpaceLeft() - ClippingMargin.Left)return false;
			if (LocalPoint.Y > TargetWidget->GetLocalSpaceRight() + ClippingMargin.Right)return false;
			if (LocalPoint.Z < TargetWidget->GetLocalSpaceBottom() - ClippingMargin.Bottom)return false;
			if (LocalPoint.Z > TargetWidget->GetLocalSpaceTop() + ClippingMargin.Top)return false;
			if (!IsPointVisible_CheckCornerRadius(FVector2D(LocalPoint.Y, LocalPoint.Z), TargetWidget))
				return false;
			continue;
		}
		/**
		 * The mask, through the frame and the formula the shader uses (FDreamUIClipMaskMath), with a hard edge: a point is
		 * in where the picture is at least half there. The layer below a clipped widget is hit wherever it is drawn at all;
		 * its texture is not read, nor is a mask's unless it asks to be (EDreamClipMaskHitTest::Mask).
		 */
		FLevel HitLevel = Level;
		if (HitLevel.Node->GetKind() == EKind::Below)
		{
			HitLevel.Underlay.Opacity = HitLevel.Underlay.Opacity > 0.0f ? 1.0f : 0.0f;
		}
		FDreamUIClipLevel Packed;
		Packed.HalfSize = HitLevel.HalfSize;
		Packed.CornerRadius = DreamUIClipDataLocal::FlipCornerRadius(HitLevel.CornerRadius);
		Packed.bMasked = HitLevel.Pack(Packed.Mask);
		const FVector Local = HitLevel.CentreToWorld.Inverse().TransformPosition(WorldPoint);
		UTexture* const ReadTexture = HitLevel.Node->GetKind() == EKind::Bounds ? HitLevel.Texture : nullptr;
		const float Alpha = FDreamUIClipMaskMath::EvaluateLevel(Packed, FVector2f(Local.Y, Local.Z), 0.0f,
			[ReadTexture](FVector2f InUV, FLinearColor& OutTexel)
			{
				return ReadTexture != nullptr && DreamUIClipDataLocal::ReadTexel(ReadTexture, InUV, OutTexel);
			});
		if (Alpha < 0.5f)
		{
			return false;
		}
	}
	return true;
}

bool FDreamUIClipData::IsPointVisible_CheckCornerRadius(const FVector2D& InLocalHitPoint, UDreamWidget* InWidget) const
{
	auto CornerRadius = InWidget->GetClippingCornerRadius();
	if (CornerRadius.X <= 0 && CornerRadius.Y <= 0 && CornerRadius.Z <= 0 && CornerRadius.W <= 0)
		return true;
	auto ClippingMargin = InWidget->GetClippingMargin();
	auto HalfWidth = (InWidget->GetWidth() + ClippingMargin.Left + ClippingMargin.Right) * 0.5f;
	auto HalfHeight = (InWidget->GetHeight() + ClippingMargin.Bottom + ClippingMargin.Top) * 0.5f;
	auto MinSize = FMath::Min(HalfWidth, HalfHeight);
	CornerRadius.X = FMath::Min(CornerRadius.X, MinSize);
	CornerRadius.Y = FMath::Min(CornerRadius.Y, MinSize);
	CornerRadius.Z = FMath::Min(CornerRadius.Z, MinSize);
	CornerRadius.W = FMath::Min(CornerRadius.W, MinSize);
	//right bottom area of rect
	{
		auto Radius = CornerRadius.X;
		auto RoundCornerCenterPos = FVector2D(InWidget->GetLocalSpaceRight() + ClippingMargin.Right - Radius, InWidget->GetLocalSpaceBottom() - ClippingMargin.Bottom + Radius);
		if (InLocalHitPoint.X > RoundCornerCenterPos.X && InLocalHitPoint.Y < RoundCornerCenterPos.Y)
		{
			if (FVector2D::DistSquared(InLocalHitPoint, RoundCornerCenterPos) > Radius * Radius)
			{
				return false;
			}
			return true;
		}
	}
	//right top area of rect
	{
		auto Radius = CornerRadius.Y;
		auto RoundCornerCenterPos = FVector2D(InWidget->GetLocalSpaceRight() + ClippingMargin.Right - Radius, InWidget->GetLocalSpaceTop() + ClippingMargin.Top - Radius);
		if (InLocalHitPoint.X > RoundCornerCenterPos.X && InLocalHitPoint.Y > RoundCornerCenterPos.Y)
		{
			if (FVector2D::DistSquared(InLocalHitPoint, RoundCornerCenterPos) > Radius * Radius)
			{
				return false;
			}
			return true;
		}
	}
	//left top area of rect
	{
		auto Radius = CornerRadius.Z;
		auto RoundCornerCenterPos = FVector2D(InWidget->GetLocalSpaceLeft() - ClippingMargin.Left + Radius, InWidget->GetLocalSpaceTop() + ClippingMargin.Top - Radius);
		if (InLocalHitPoint.X < RoundCornerCenterPos.X && InLocalHitPoint.Y > RoundCornerCenterPos.Y)
		{
			if (FVector2D::DistSquared(InLocalHitPoint, RoundCornerCenterPos) > Radius * Radius)
			{
				return false;
			}
			return true;
		}
	}
	//left bottom area of rect
	{
		auto Radius = CornerRadius.W;
		auto RoundCornerCenterPos = FVector2D(InWidget->GetLocalSpaceLeft() - ClippingMargin.Left + Radius, InWidget->GetLocalSpaceBottom() - ClippingMargin.Bottom + Radius);
		if (InLocalHitPoint.X < RoundCornerCenterPos.X && InLocalHitPoint.Y < RoundCornerCenterPos.Y)
		{
			if (FVector2D::DistSquared(InLocalHitPoint, RoundCornerCenterPos) > Radius * Radius)
			{
				return false;
			}
			return true;
		}
	}
	return true;
}
