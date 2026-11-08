// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Psd/DreamPsdReader.h"
#include "Psd/DreamPsdColor.h"
#include "DreamPsdBlocks.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"

// ---------------------------------------------------------------------------------------------------------------------
// Messages and images
// ---------------------------------------------------------------------------------------------------------------------

FString FDreamPsdMessage::ToString() const
{
	const TCHAR* Prefix = Severity == ESeverity::Error ? TEXT("error") : (Severity == ESeverity::Warning ? TEXT("warning") : TEXT("info"));
	return Layer.IsEmpty() ? FString::Printf(TEXT("%s: %s"), Prefix, *Text) : FString::Printf(TEXT("%s: [%s] %s"), Prefix, *Layer, *Text);
}

void FDreamPsdMessages::Info(const FString& InText, const FString& InLayer)
{
	Items.Add({ FDreamPsdMessage::ESeverity::Info, InText, InLayer });
}

void FDreamPsdMessages::Warn(const FString& InText, const FString& InLayer)
{
	Items.Add({ FDreamPsdMessage::ESeverity::Warning, InText, InLayer });
}

void FDreamPsdMessages::Error(const FString& InText, const FString& InLayer)
{
	Items.Add({ FDreamPsdMessage::ESeverity::Error, InText, InLayer });
}

bool FDreamPsdMessages::HasErrors() const
{
	return Count(FDreamPsdMessage::ESeverity::Error) > 0;
}

int32 FDreamPsdMessages::Count(FDreamPsdMessage::ESeverity InSeverity) const
{
	int32 N = 0;
	for (const FDreamPsdMessage& M : Items)
	{
		N += M.Severity == InSeverity ? 1 : 0;
	}
	return N;
}

FString FDreamPsdMessages::ToString() const
{
	TArray<FString> Lines;
	for (const FDreamPsdMessage& M : Items)
	{
		Lines.Add(M.ToString());
	}
	return FString::Join(Lines, TEXT("\n"));
}

void FDreamPsdImage::Init(int32 InWidth, int32 InHeight, FColor InFill)
{
	Width = FMath::Max(0, InWidth);
	Height = FMath::Max(0, InHeight);
	Pixels.Init(InFill, Width * Height);
}

void FDreamPsdGray::Init(int32 InWidth, int32 InHeight, uint8 InFill)
{
	Width = FMath::Max(0, InWidth);
	Height = FMath::Max(0, InHeight);
	Pixels.Init(InFill, Width * Height);
}

FBox2D FDreamPsdPath::GetBounds() const
{
	FBox2D Box(ForceInit);
	for (const FDreamPsdSubpath& Sub : Subpaths)
	{
		for (const FDreamPsdPathKnot& K : Sub.Knots)
		{
			Box += K.In;
			Box += K.Anchor;
			Box += K.Out;
		}
	}
	return Box;
}

FLinearColor FDreamPsdGradient::Evaluate(float T) const
{
	T = FMath::Clamp(T, 0.0f, 1.0f);
	auto Blend = [](float InT, float InA, float InB, float InMid) -> float
	{
		// Between two stops the blend is linear on each side of the midpoint, half way at it.
		if (InB <= InA)
		{
			return InT >= InB ? 1.0f : 0.0f;
		}
		const float U = (InT - InA) / (InB - InA);
		const float M = FMath::Clamp(InMid, 0.01f, 0.99f);
		return U < M ? 0.5f * U / M : 0.5f + 0.5f * (U - M) / (1.0f - M);
	};
	FLinearColor Colour = FLinearColor::Black;
	if (ColorStops.Num() == 1)
	{
		Colour = ColorStops[0].Color;
	}
	else if (ColorStops.Num() > 1)
	{
		if (T <= ColorStops[0].Location)
		{
			Colour = ColorStops[0].Color;
		}
		else if (T >= ColorStops.Last().Location)
		{
			Colour = ColorStops.Last().Color;
		}
		else
		{
			for (int32 i = 0; i + 1 < ColorStops.Num(); ++i)
			{
				const FDreamPsdColorStop& A = ColorStops[i];
				const FDreamPsdColorStop& B = ColorStops[i + 1];
				if (T >= A.Location && T <= B.Location)
				{
					Colour = FMath::Lerp(A.Color, B.Color, Blend(T, A.Location, B.Location, A.Midpoint));
					break;
				}
			}
		}
	}
	float Alpha = 1.0f;
	if (AlphaStops.Num() == 1)
	{
		Alpha = AlphaStops[0].Opacity;
	}
	else if (AlphaStops.Num() > 1)
	{
		if (T <= AlphaStops[0].Location)
		{
			Alpha = AlphaStops[0].Opacity;
		}
		else if (T >= AlphaStops.Last().Location)
		{
			Alpha = AlphaStops.Last().Opacity;
		}
		else
		{
			for (int32 i = 0; i + 1 < AlphaStops.Num(); ++i)
			{
				const FDreamPsdAlphaStop& A = AlphaStops[i];
				const FDreamPsdAlphaStop& B = AlphaStops[i + 1];
				if (T >= A.Location && T <= B.Location)
				{
					Alpha = FMath::Lerp(A.Opacity, B.Opacity, Blend(T, A.Location, B.Location, A.Midpoint));
					break;
				}
			}
		}
	}
	Colour.A = Alpha;
	return Colour;
}

double FDreamPsdText::GetScale() const
{
	return FMath::Sqrt(Transform[2] * Transform[2] + Transform[3] * Transform[3]);
}

bool FDreamPsdText::IsAxisAligned() const
{
	return FMath::Abs(Transform[1]) < 1e-6 && FMath::Abs(Transform[2]) < 1e-6 && Transform[0] > 0.0 && Transform[3] > 0.0;
}

bool FDreamPsdLayerEffects::HasEnabled() const
{
	if (!bPresent || !bMasterSwitch)
	{
		return false;
	}
	for (const FDreamPsdEffect& E : Effects)
	{
		if (E.bEnabled)
		{
			return true;
		}
	}
	return false;
}

const FDreamPsdPattern* FDreamPsdDocument::FindPattern(const FString& InId) const
{
	for (const FDreamPsdPattern& P : Patterns)
	{
		if (P.Id == InId)
		{
			return &P;
		}
	}
	return nullptr;
}

FDreamPsdLayerNode FDreamPsdDocument::BuildTree(FDreamPsdMessages* OutMessages) const
{
	// Records run bottom first: a group is its divider, its children, then its own record. A stack of open groups,
	// each closed by the record that names it.
	FDreamPsdLayerNode Root;
	TArray<FDreamPsdLayerNode> Stack;
	Stack.Add(MoveTemp(Root));
	for (int32 i = 0; i < Layers.Num(); ++i)
	{
		const FDreamPsdLayer& L = Layers[i];
		if (L.IsGroupEnd())
		{
			FDreamPsdLayerNode Group;
			Group.bGroup = true;
			Stack.Add(MoveTemp(Group));
		}
		else if (L.IsGroupStart())
		{
			if (Stack.Num() <= 1)
			{
				// A group record with no divider below it: a group with no children.
				if (OutMessages != nullptr)
				{
					OutMessages->Warn(TEXT("a group record with no divider below it; it is read as an empty group"), L.Name);
				}
				FDreamPsdLayerNode Empty;
				Empty.bGroup = true;
				Empty.LayerIndex = i;
				Stack.Last().Children.Add(MoveTemp(Empty));
				continue;
			}
			FDreamPsdLayerNode Group = Stack.Pop();
			Group.LayerIndex = i;
			Stack.Last().Children.Add(MoveTemp(Group));
		}
		else
		{
			FDreamPsdLayerNode Leaf;
			Leaf.LayerIndex = i;
			Stack.Last().Children.Add(MoveTemp(Leaf));
		}
	}
	while (Stack.Num() > 1)
	{
		// A divider never closed: its children are kept, under the level above.
		if (OutMessages != nullptr)
		{
			OutMessages->Warn(TEXT("a group divider with no group record above it; its layers are kept one level up"));
		}
		FDreamPsdLayerNode Open = Stack.Pop();
		Stack.Last().Children.Append(MoveTemp(Open.Children));
	}
	return MoveTemp(Stack[0]);
}

// ---------------------------------------------------------------------------------------------------------------------
// The reader
// ---------------------------------------------------------------------------------------------------------------------

namespace DreamPsdReaderLocal
{
	bool IsSignature(const FDreamPsdStream& S, int64 InAt)
	{
		if (InAt + 4 > S.Size)
		{
			return false;
		}
		const uint8* P = S.Data + InAt;
		return P[0] == '8' && P[1] == 'B' && ((P[2] == 'I' && P[3] == 'M') || (P[2] == '6' && P[3] == '4'));
	}

	/** Tagged blocks from here to the end of InStream. bInGlobal: the document's own (lengths padded to 4). */
	template <typename FHandler>
	void ReadTaggedBlocks(FDreamPsdStream& S, bool bInLarge, bool bInGlobal, FDreamPsdMessages& M, const FString& InLayer, FHandler&& InHandle)
	{
		while (S.Remaining() >= 12)
		{
			if (!IsSignature(S, S.Pos))
			{
				// Some writers pad a block's length to even, others to four, without counting the pad: look a few bytes on.
				bool bFound = false;
				for (int32 Ahead = 1; Ahead <= 3; ++Ahead)
				{
					if (IsSignature(S, S.Pos + Ahead))
					{
						S.Skip(Ahead);
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					if (S.Remaining() > 3)
					{
						M.Warn(FString::Printf(TEXT("%lld bytes after the last tagged block that are not one"), (long long)S.Remaining()), InLayer);
					}
					return;
				}
			}
			S.Skip(4);
			const FString Key = S.Key4();
			const uint64 Length = (bInLarge && DreamPsdBlocks::HasWideLength(Key)) ? S.U64() : (uint64)S.U32();
			if (S.bOverrun || Length > (uint64)S.Remaining())
			{
				M.Warn(FString::Printf(TEXT("tagged block '%s' claims %llu bytes, more than are left"), *Key, (unsigned long long)Length), InLayer);
				return;
			}
			FDreamPsdStream Block = S.Sub((int64)Length);
			InHandle(Key, Block);
			if (bInGlobal && Length % 4 != 0 && !IsSignature(S, S.Pos))
			{
				S.Skip(FMath::Min<int64>(4 - Length % 4, S.Remaining()));
			}
		}
	}

	struct FReader
	{
		const TArray<uint8>& Bytes;
		FDreamPsdDocument& Doc;
		FDreamPsdMessages& M;
		const FDreamPsdReadOptions& Options;

		FReader(const TArray<uint8>& InBytes, FDreamPsdDocument& InDoc, FDreamPsdMessages& InMessages, const FDreamPsdReadOptions& InOptions)
			: Bytes(InBytes), Doc(InDoc), M(InMessages), Options(InOptions) {}

		bool Large() const { return Doc.IsLarge(); }

		bool ReadHeader(FDreamPsdStream& S)
		{
			if (S.Key4() != TEXT("8BPS"))
			{
				M.Error(TEXT("not a layered image document: the file does not start with 8BPS"));
				return false;
			}
			Doc.Version = S.U16();
			if (Doc.Version != 1 && Doc.Version != 2)
			{
				M.Error(FString::Printf(TEXT("version %d; 1 (PSD) and 2 (PSB) are the versions there are"), Doc.Version));
				return false;
			}
			S.Skip(6);
			Doc.ChannelCount = S.U16();
			Doc.Height = (int32)S.U32();
			Doc.Width = (int32)S.U32();
			Doc.Depth = S.U16();
			Doc.ColorMode = (EDreamPsdColorMode)S.U16();
			if (S.bOverrun)
			{
				M.Error(TEXT("the header is cut short"));
				return false;
			}
			const int32 MaxSide = Doc.IsLarge() ? 300000 : 30000;
			if (Doc.Width <= 0 || Doc.Height <= 0 || Doc.Width > MaxSide || Doc.Height > MaxSide)
			{
				M.Error(FString::Printf(TEXT("a document of %d x %d, outside 1..%d"), Doc.Width, Doc.Height, MaxSide));
				return false;
			}
			if (int64(Doc.Width) * Doc.Height > int64(16384) * 16384)
			{
				M.Error(FString::Printf(TEXT("a document of %d x %d pixels, more than this importer holds in memory"), Doc.Width, Doc.Height));
				return false;
			}
			if (Doc.ChannelCount < 1 || Doc.ChannelCount > 56)
			{
				M.Error(FString::Printf(TEXT("%d channels, outside 1..56"), Doc.ChannelCount));
				return false;
			}
			if (Doc.Depth != 1 && Doc.Depth != 8 && Doc.Depth != 16 && Doc.Depth != 32)
			{
				M.Error(FString::Printf(TEXT("%d bits per channel; 1, 8, 16 and 32 are read"), Doc.Depth));
				return false;
			}
			switch (Doc.ColorMode)
			{
			case EDreamPsdColorMode::RGB:
			case EDreamPsdColorMode::Grayscale:
				break;
			case EDreamPsdColorMode::Indexed:
				M.Info(TEXT("an indexed document: its colours come from its colour table"));
				break;
			case EDreamPsdColorMode::CMYK:
				M.Warn(TEXT("a CMYK document: converted to RGB by formula, without its colour profile -- colours will be close, not exact. Convert it to RGB in the editor for an exact import."));
				break;
			case EDreamPsdColorMode::Lab:
				M.Warn(TEXT("a Lab document: converted to sRGB by formula -- colours will be close, not exact. Convert it to RGB in the editor for an exact import."));
				break;
			case EDreamPsdColorMode::Duotone:
				M.Warn(TEXT("a duotone document: read as grayscale (the duotone inks are not applied)"));
				break;
			case EDreamPsdColorMode::Bitmap:
				M.Info(TEXT("a bitmap (1-bit) document: read as black and white"));
				break;
			case EDreamPsdColorMode::Multichannel:
				M.Warn(TEXT("a multichannel document: its first channel is read as gray"));
				break;
			default:
				M.Error(FString::Printf(TEXT("colour mode %d, which the format does not define"), (int32)Doc.ColorMode));
				return false;
			}
			return true;
		}

		void ReadColorModeData(FDreamPsdStream& S)
		{
			const uint32 Length = S.U32();
			FDreamPsdStream Data = S.Sub(Length);
			if (Doc.ColorMode == EDreamPsdColorMode::Indexed && Length >= 768)
			{
				Doc.ColorTable.SetNum(256);
				for (int32 c = 0; c < 3; ++c)
				{
					for (int32 i = 0; i < 256; ++i)
					{
						const uint8 V = Data.U8();
						if (c == 0) { Doc.ColorTable[i].R = V; }
						else if (c == 1) { Doc.ColorTable[i].G = V; }
						else { Doc.ColorTable[i].B = V; }
						Doc.ColorTable[i].A = 255;
					}
				}
			}
		}

		void ReadResources(FDreamPsdStream& S)
		{
			const uint32 Length = S.U32();
			FDreamPsdStream R = S.Sub(Length);
			while (R.Remaining() >= 12)
			{
				const FString Signature = R.Key4();
				if (Signature != TEXT("8BIM") && Signature != TEXT("MeSa") && Signature != TEXT("AgHg") && Signature != TEXT("PHUT") && Signature != TEXT("DCSR"))
				{
					M.Warn(FString::Printf(TEXT("an image resource with the signature '%s'; the rest of the resources are skipped"), *Signature));
					return;
				}
				const uint16 Id = R.U16();
				R.Pascal(2);
				const uint32 Size = R.U32();
				FDreamPsdStream Data = R.Sub(Size);
				if (Size % 2 == 1)
				{
					R.Skip(1);
				}
				Doc.ResourceIds.Add(Id);
				switch (Id)
				{
				case 1005:
				{
					Doc.ResolutionX = Data.U32() / 65536.0;
					Data.Skip(4);
					Doc.ResolutionY = Data.U32() / 65536.0;
					break;
				}
				case 1037:
					Doc.GlobalAngle = (float)Data.I32();
					break;
				case 1049:
					Doc.GlobalAltitude = (float)Data.I32();
					break;
				case 1057:
					Data.U32();
					Doc.bHasRealMergedData = Data.U8() != 0;
					break;
				default:
					break;
				}
			}
		}

		/** The layer mask / adjustment layer data of a record. */
		void ReadMaskData(FDreamPsdStream& S, FDreamPsdLayer& L, FIntRect& OutFirstRect, FIntRect& OutRealRect, bool& bOutHasReal)
		{
			const uint32 Length = S.U32();
			if (Length == 0)
			{
				return;
			}
			FDreamPsdStream D = S.Sub(Length);
			auto ReadRect = [&D]()
			{
				const int32 Top = D.I32();
				const int32 Left = D.I32();
				const int32 Bottom = D.I32();
				const int32 Right = D.I32();
				return FIntRect(Left, Top, Right, Bottom);
			};
			OutFirstRect = ReadRect();
			const uint8 Default = D.U8();
			const uint8 Flags = D.U8();
			uint8 RealFlags = 0;
			uint8 RealDefault = 0;
			bOutHasReal = false;
			if (Length >= 36)
			{
				// Order as the editor writes it, which is not the order the format's description gives: the "real"
				// user mask (the pixel mask, when a vector mask is present as well) before the mask parameters.
				RealFlags = D.U8();
				RealDefault = D.U8();
				OutRealRect = ReadRect();
				bOutHasReal = true;
			}
			float UserDensity = 1.0f, UserFeather = 0.0f, VectorDensity = 1.0f, VectorFeather = 0.0f;
			if ((Flags & 0x10) != 0 && D.Remaining() >= 1)
			{
				const uint8 Params = D.U8();
				if (Params & 0x01) { UserDensity = D.U8() / 255.0f; }
				if (Params & 0x02) { UserFeather = (float)D.F64(); }
				if (Params & 0x04) { VectorDensity = D.U8() / 255.0f; }
				if (Params & 0x08) { VectorFeather = (float)D.F64(); }
			}
			L.Mask.Density = UserDensity;
			L.Mask.Feather = UserFeather;
			L.VectorMask.Density = VectorDensity;
			L.VectorMask.Feather = VectorFeather;
			// Which rectangle and flags the pixel mask has is settled once the channels are known (ResolveMask).
			L.Mask.DefaultColor = Default;
			L.Mask.bPositionRelative = (Flags & 0x01) != 0;
			L.Mask.bDisabled = (Flags & 0x02) != 0;
			L.Mask.bInvertOnBlend = (Flags & 0x04) != 0;
			L.Mask.bFromRenderedData = (Flags & 0x08) != 0;
			if (bOutHasReal)
			{
				RealMaskFlags.Add(L.RecordIndex, RealFlags);
				RealMaskDefaults.Add(L.RecordIndex, RealDefault);
			}
		}

		TMap<int32, uint8> RealMaskFlags;
		TMap<int32, uint8> RealMaskDefaults;
		TMap<int32, FIntRect> FirstMaskRects;
		TMap<int32, FIntRect> RealMaskRects;

		bool ReadLayerRecord(FDreamPsdStream& S, FDreamPsdLayer& L)
		{
			const int32 Top = S.I32();
			const int32 Left = S.I32();
			const int32 Bottom = S.I32();
			const int32 Right = S.I32();
			L.Rect = FIntRect(Left, Top, FMath::Max(Left, Right), FMath::Max(Top, Bottom));
			const uint16 Channels = S.U16();
			if (Channels > 56)
			{
				M.Error(FString::Printf(TEXT("a layer record with %u channels"), Channels));
				return false;
			}
			for (uint16 c = 0; c < Channels; ++c)
			{
				FDreamPsdChannelInfo Info;
				Info.Id = S.I16();
				Info.Length = S.Length(Large());
				L.Channels.Add(Info);
			}
			const FString Signature = S.Key4();
			if (Signature != TEXT("8BIM"))
			{
				M.Error(FString::Printf(TEXT("a layer record's blend signature is '%s', not 8BIM"), *Signature));
				return false;
			}
			L.BlendKey = S.Key4();
			L.Opacity = S.U8();
			L.bClipping = S.U8() != 0;
			const uint8 Flags = S.U8();
			L.bTransparencyProtected = (Flags & 0x01) != 0;
			// The format calls bit 1 "visible"; the editor sets it for a hidden layer.
			L.bVisible = (Flags & 0x02) == 0;
			L.bPixelDataIrrelevant = (Flags & 0x08) != 0 && (Flags & 0x10) != 0;
			S.U8();
			const uint32 ExtraLength = S.U32();
			FDreamPsdStream Extra = S.Sub(ExtraLength);
			if (S.bOverrun)
			{
				M.Error(TEXT("a layer record runs past its section"));
				return false;
			}
			FIntRect FirstRect, RealRect;
			bool bHasReal = false;
			ReadMaskData(Extra, L, FirstRect, RealRect, bHasReal);
			FirstMaskRects.Add(L.RecordIndex, FirstRect);
			if (bHasReal)
			{
				RealMaskRects.Add(L.RecordIndex, RealRect);
			}
			const uint32 RangesLength = Extra.U32();
			Extra.Skip(RangesLength);
			L.Name = Extra.Pascal(4);
			ReadTaggedBlocks(Extra, Large(), false, M, L.Name, [this, &L](const FString& InKey, FDreamPsdStream& InBlock)
			{
				L.BlockKeys.Add(InKey);
				DreamPsdBlocks::ParseLayerBlock(InKey, InBlock, L, Doc, M);
			});
			return true;
		}

		/** Settle the pixel mask's rectangle and flags from the channels the record has. */
		void ResolveMask(FDreamPsdLayer& L)
		{
			const bool bHasUser = L.Channels.ContainsByPredicate([](const FDreamPsdChannelInfo& C) { return C.Id == -2; });
			const bool bHasReal = L.Channels.ContainsByPredicate([](const FDreamPsdChannelInfo& C) { return C.Id == -3; });
			if (bHasReal && RealMaskRects.Contains(L.RecordIndex))
			{
				const uint8 Flags = RealMaskFlags.FindRef(L.RecordIndex);
				L.Mask.bPresent = true;
				L.Mask.Rect = RealMaskRects[L.RecordIndex];
				L.Mask.DefaultColor = RealMaskDefaults.FindRef(L.RecordIndex);
				L.Mask.bPositionRelative = (Flags & 0x01) != 0;
				L.Mask.bDisabled = (Flags & 0x02) != 0;
				L.Mask.bInvertOnBlend = (Flags & 0x04) != 0;
				L.Mask.bFromRenderedData = false;
			}
			else if (bHasUser && !(L.Mask.bFromRenderedData && L.VectorMask.bPresent))
			{
				L.Mask.bPresent = true;
				L.Mask.Rect = FirstMaskRects.FindRef(L.RecordIndex);
			}
		}

		/** Decode the channels of every record, which follow the records in the same order. */
		void ReadChannelData(FDreamPsdStream& S, TArray<FDreamPsdLayer>& InLayers)
		{
			for (FDreamPsdLayer& L : InLayers)
			{
				ResolveMask(L);
				TArray<uint8> R, G, B, A, K, UserMask, RealMask;
				bool bHasAlpha = false, bHasUser = false, bHasReal = false;
				for (FDreamPsdChannelInfo& Info : L.Channels)
				{
					if (Info.Length > (uint64)S.Remaining())
					{
						M.Warn(FString::Printf(TEXT("channel %d claims %llu bytes, more than the section has left; the rest of the layers have no pixels"),
							Info.Id, (unsigned long long)Info.Length), L.Name);
						S.Seek(S.Size);
						return;
					}
					FDreamPsdStream C = S.Sub((int64)Info.Length);
					if (Info.Length < 2)
					{
						continue;
					}
					Info.Compression = C.U16();
					FIntRect Rect = L.Rect;
					if (Info.Id == -2)
					{
						Rect = FirstMaskRects.FindRef(L.RecordIndex);
					}
					else if (Info.Id == -3)
					{
						Rect = RealMaskRects.FindRef(L.RecordIndex);
					}
					const int32 W = Rect.Width();
					const int32 H = Rect.Height();
					if (W <= 0 || H <= 0)
					{
						continue;
					}
					const bool bWanted = Options.bLayerPixels || Info.Id <= -2;
					if (!bWanted)
					{
						continue;
					}
					TArray<uint8> Raw;
					FString Error;
					if (!DreamPsdPixels::DecodePlane(C.Data + C.Pos, C.Remaining(), Info.Compression, W, H, Doc.Depth, Large(), Raw, Error))
					{
						M.Warn(FString::Printf(TEXT("channel %d: %s"), Info.Id, *Error), L.Name);
					}
					TArray<uint8> Eight;
					DreamPsdPixels::ToEightBit(Raw, Doc.Depth, W, H, Eight);
					switch (Info.Id)
					{
					case 0: R = MoveTemp(Eight); break;
					case 1: G = MoveTemp(Eight); break;
					case 2: B = MoveTemp(Eight); break;
					case 3: K = MoveTemp(Eight); break;
					case -1: A = MoveTemp(Eight); bHasAlpha = true; break;
					case -2: UserMask = MoveTemp(Eight); bHasUser = true; break;
					case -3: RealMask = MoveTemp(Eight); bHasReal = true; break;
					default: break;
					}
				}
				if (L.Mask.bPresent)
				{
					TArray<uint8>& Source = bHasReal ? RealMask : UserMask;
					L.Mask.Pixels.Width = L.Mask.Rect.Width();
					L.Mask.Pixels.Height = L.Mask.Rect.Height();
					L.Mask.Pixels.Pixels = MoveTemp(Source);
					if (L.Mask.Pixels.IsEmpty())
					{
						L.Mask.bPresent = false;
					}
				}
				const int32 W = L.Rect.Width();
				const int32 H = L.Rect.Height();
				if (!Options.bLayerPixels || W <= 0 || H <= 0 || R.Num() == 0)
				{
					continue;
				}
				L.Pixels.Init(W, H, FColor::Black);
				const int32 Count = W * H;
				for (int32 i = 0; i < Count; ++i)
				{
					FColor& P = L.Pixels.Pixels[i];
					const uint8 r = R.IsValidIndex(i) ? R[i] : 0;
					const uint8 g = G.IsValidIndex(i) ? G[i] : r;
					const uint8 b = B.IsValidIndex(i) ? B[i] : r;
					switch (Doc.ColorMode)
					{
					case EDreamPsdColorMode::CMYK:
					{
						// Stored inverted: 255 is no ink.
						const FLinearColor C = DreamPsdColor::CmykToRgb(1.0f - r / 255.0f, 1.0f - g / 255.0f, 1.0f - b / 255.0f,
							1.0f - (K.IsValidIndex(i) ? K[i] : 255) / 255.0f);
						P = DreamPsdColor::ToColor(C);
						break;
					}
					case EDreamPsdColorMode::Lab:
						P = DreamPsdColor::ToColor(DreamPsdColor::LabToRgb(r * 100.0f / 255.0f, g - 128.0f, b - 128.0f));
						break;
					case EDreamPsdColorMode::RGB:
						P = FColor(r, g, b, 255);
						break;
					default:
						P = FColor(r, r, r, 255);
						break;
					}
					P.A = bHasAlpha && A.IsValidIndex(i) ? A[i] : 255;
				}
			}
		}

		/** A layer info structure: the count, the records, the channel data. */
		void ReadLayerInfo(FDreamPsdStream& S)
		{
			const int16 Count = S.I16();
			Doc.bMergedHasTransparency = Count < 0;
			const int32 N = FMath::Abs((int32)Count);
			TArray<FDreamPsdLayer> Records;
			Records.Reserve(N);
			for (int32 i = 0; i < N; ++i)
			{
				FDreamPsdLayer L;
				L.RecordIndex = i;
				if (!ReadLayerRecord(S, L))
				{
					M.Error(FString::Printf(TEXT("layer record %d does not read; no layers are imported"), i));
					return;
				}
				Records.Add(MoveTemp(L));
			}
			ReadChannelData(S, Records);
			for (FDreamPsdLayer& L : Records)
			{
				ClassifyLayer(L);
			}
			Doc.Layers = MoveTemp(Records);
		}

		void ClassifyLayer(FDreamPsdLayer& L)
		{
			if (L.SectionType == 3)
			{
				L.Kind = EDreamPsdLayerKind::GroupEnd;
			}
			else if (L.SectionType == 1 || L.SectionType == 2)
			{
				L.Kind = EDreamPsdLayerKind::GroupStart;
			}
			else if (L.Text.IsValid())
			{
				L.Kind = EDreamPsdLayerKind::Text;
			}
			else if (L.Adjustment.IsValid())
			{
				L.Kind = EDreamPsdLayerKind::Adjustment;
			}
			else if (L.Fill.Kind != FDreamPsdFill::EKind::None)
			{
				L.Kind = L.VectorMask.bPresent ? EDreamPsdLayerKind::Shape : EDreamPsdLayerKind::Fill;
			}
			else
			{
				L.Kind = EDreamPsdLayerKind::Pixel;
			}
		}

		void ReadLayerAndMask(FDreamPsdStream& S)
		{
			const uint64 Length = S.Length(Large());
			if (Length == 0)
			{
				return;
			}
			if (Length > (uint64)S.Remaining())
			{
				M.Error(TEXT("the layer section claims more bytes than the file has; no layers are imported"));
				S.Seek(S.Size);
				return;
			}
			FDreamPsdStream Section = S.Sub((int64)Length);
			const uint64 InfoLength = Section.Length(Large());
			if (InfoLength > 0)
			{
				FDreamPsdStream Info = Section.Sub((int64)InfoLength);
				ReadLayerInfo(Info);
			}
			const uint32 GlobalMaskLength = Section.U32();
			Section.Skip(GlobalMaskLength);
			ReadTaggedBlocks(Section, Large(), true, M, FString(), [this](const FString& InKey, FDreamPsdStream& InBlock)
			{
				if ((InKey == TEXT("Lr16") || InKey == TEXT("Lr32")) && Doc.Layers.Num() == 0)
				{
					ReadLayerInfo(InBlock);
				}
				else if (InKey == TEXT("Patt") || InKey == TEXT("Pat2") || InKey == TEXT("Pat3"))
				{
					DreamPsdBlocks::ParsePatterns(InBlock, Doc, M);
				}
			});
		}

		void ReadComposite(FDreamPsdStream& S)
		{
			if (S.Remaining() < 2)
			{
				M.Info(TEXT("the file stores no merged image"));
				return;
			}
			const uint16 Compression = S.U16();
			const int32 W = Doc.Width;
			const int32 H = Doc.Height;
			const int32 Planes = Doc.ChannelCount;
			const int64 RowBytes = Doc.Depth == 1 ? (int64(W) + 7) / 8 : int64(W) * (Doc.Depth / 8);
			TArray<TArray<uint8>> Eight;
			Eight.SetNum(Planes);
			if (Compression == 1)
			{
				// Every plane's row counts first, then every plane's rows.
				const int32 CountSize = Large() ? 4 : 2;
				const int64 CountsBytes = int64(Planes) * H * CountSize;
				if (S.Remaining() < CountsBytes)
				{
					M.Warn(TEXT("the merged image's row counts are cut short; it is not read"));
					return;
				}
				const uint8* Counts = S.Data + S.Pos;
				int64 Pos = S.Pos + CountsBytes;
				for (int32 p = 0; p < Planes; ++p)
				{
					TArray<uint8> Plane;
					bool bOk = true;
					for (int32 Row = 0; Row < H; ++Row)
					{
						const uint8* C = Counts + (int64(p) * H + Row) * CountSize;
						const int64 Count = CountSize == 4 ? (int64(C[0]) << 24 | int64(C[1]) << 16 | int64(C[2]) << 8 | int64(C[3])) : (int64(C[0]) << 8 | int64(C[1]));
						const int64 Available = FMath::Clamp<int64>(S.Size - Pos, 0, Count);
						bOk &= DreamPsdPixels::UnpackBits(S.Data + Pos, Available, Plane, RowBytes);
						Pos += Count;
					}
					if (!bOk)
					{
						M.Warn(FString::Printf(TEXT("the merged image's plane %d does not decode cleanly"), p));
					}
					DreamPsdPixels::ToEightBit(Plane, Doc.Depth, W, H, Eight[p]);
				}
			}
			else if (Compression == 0 || Compression == 2 || Compression == 3)
			{
				TArray<uint8> All;
				FString Error;
				if (Compression == 0)
				{
					const int64 Needed = RowBytes * H * Planes;
					if (S.Remaining() < Needed)
					{
						M.Warn(TEXT("the merged image is cut short; it is not read"));
						return;
					}
					All.Append(S.Data + S.Pos, (int32)Needed);
				}
				else
				{
					All.SetNumUninitialized((int32)(RowBytes * H * Planes));
					if (!FCompression::UncompressMemory(NAME_Zlib, All.GetData(), All.Num(), S.Data + S.Pos, (int32)S.Remaining()))
					{
						M.Warn(TEXT("the merged image's zip data does not inflate; it is not read"));
						return;
					}
				}
				for (int32 p = 0; p < Planes; ++p)
				{
					TArray<uint8> Plane(All.GetData() + int64(p) * RowBytes * H, (int32)(RowBytes * H));
					if (Compression == 3)
					{
						for (int32 Row = 0; Row < H; ++Row)
						{
							DreamPsdPixels::UndoPrediction(Plane.GetData() + int64(Row) * RowBytes, W, Doc.Depth);
						}
					}
					DreamPsdPixels::ToEightBit(Plane, Doc.Depth, W, H, Eight[p]);
				}
			}
			else
			{
				M.Warn(FString::Printf(TEXT("the merged image uses compression %u; it is not read"), Compression));
				return;
			}

			int32 BaseChannels = 3;
			switch (Doc.ColorMode)
			{
			case EDreamPsdColorMode::CMYK: BaseChannels = 4; break;
			case EDreamPsdColorMode::Grayscale:
			case EDreamPsdColorMode::Duotone:
			case EDreamPsdColorMode::Indexed:
			case EDreamPsdColorMode::Bitmap:
			case EDreamPsdColorMode::Multichannel: BaseChannels = 1; break;
			default: break;
			}
			const bool bAlpha = Doc.bMergedHasTransparency && Planes > BaseChannels;
			Doc.Composite.Init(W, H, FColor::Black);
			const int32 Count = W * H;
			auto Sample = [&Eight](int32 InPlane, int32 InIndex) -> uint8
			{
				return Eight.IsValidIndex(InPlane) && Eight[InPlane].IsValidIndex(InIndex) ? Eight[InPlane][InIndex] : 0;
			};
			for (int32 i = 0; i < Count; ++i)
			{
				FColor& P = Doc.Composite.Pixels[i];
				switch (Doc.ColorMode)
				{
				case EDreamPsdColorMode::RGB:
					P = FColor(Sample(0, i), Sample(1, i), Sample(2, i), 255);
					break;
				case EDreamPsdColorMode::CMYK:
					P = DreamPsdColor::ToColor(DreamPsdColor::CmykToRgb(1.0f - Sample(0, i) / 255.0f, 1.0f - Sample(1, i) / 255.0f,
						1.0f - Sample(2, i) / 255.0f, 1.0f - Sample(3, i) / 255.0f));
					break;
				case EDreamPsdColorMode::Lab:
					P = DreamPsdColor::ToColor(DreamPsdColor::LabToRgb(Sample(0, i) * 100.0f / 255.0f, Sample(1, i) - 128.0f, Sample(2, i) - 128.0f));
					break;
				case EDreamPsdColorMode::Indexed:
					P = Doc.ColorTable.IsValidIndex(Sample(0, i)) ? Doc.ColorTable[Sample(0, i)] : FColor::Black;
					break;
				default:
				{
					const uint8 V = Sample(0, i);
					P = FColor(V, V, V, 255);
					break;
				}
				}
				P.A = bAlpha ? Sample(BaseChannels, i) : 255;
			}
			if (bAlpha && Doc.Composite.Pixels.Num() > 0)
			{
				// The merged image is stored matted against white where it is transparent: take the matte back out, so
				// the image is the colour it is over the alpha it has.
				for (FColor& P : Doc.Composite.Pixels)
				{
					if (P.A == 0 || P.A == 255)
					{
						if (P.A == 0)
						{
							P = FColor(0, 0, 0, 0);
						}
						continue;
					}
					const float Alpha = P.A / 255.0f;
					auto Unmatte = [Alpha](uint8 C) { return (uint8)FMath::Clamp(FMath::RoundToInt((C / 255.0f - (1.0f - Alpha)) / Alpha * 255.0f), 0, 255); };
					P.R = Unmatte(P.R);
					P.G = Unmatte(P.G);
					P.B = Unmatte(P.B);
				}
			}
		}

		bool Run()
		{
			FDreamPsdStream S(Bytes.GetData(), Bytes.Num());
			if (!ReadHeader(S))
			{
				return false;
			}
			ReadColorModeData(S);
			ReadResources(S);
			if (S.bOverrun)
			{
				M.Error(TEXT("the file ends inside its image resources"));
				return false;
			}
			ReadLayerAndMask(S);
			if (Options.bComposite)
			{
				ReadComposite(S);
			}
			if (Doc.Layers.Num() == 0 && !Doc.Composite.IsEmpty())
			{
				// A flat document: its one image is its one layer.
				FDreamPsdLayer Background;
				Background.RecordIndex = 0;
				Background.Name = TEXT("Background");
				Background.Rect = FIntRect(0, 0, Doc.Width, Doc.Height);
				Background.Pixels = Doc.Composite;
				Doc.Layers.Add(MoveTemp(Background));
				M.Info(TEXT("the document has no layers; its merged image is imported as one"));
			}
			return !M.HasErrors();
		}
	};
}

bool DreamPsdReader::Read(const TArray<uint8>& InBytes, FDreamPsdDocument& OutDocument, FDreamPsdMessages& OutMessages, const FDreamPsdReadOptions& InOptions)
{
	OutDocument = FDreamPsdDocument();
	DreamPsdReaderLocal::FReader Reader(InBytes, OutDocument, OutMessages, InOptions);
	return Reader.Run();
}

bool DreamPsdReader::ReadFile(const FString& InPath, FDreamPsdDocument& OutDocument, FDreamPsdMessages& OutMessages, const FDreamPsdReadOptions& InOptions)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *InPath))
	{
		OutMessages.Error(FString::Printf(TEXT("cannot read %s"), *InPath));
		return false;
	}
	return Read(Bytes, OutDocument, OutMessages, InOptions);
}

FString DreamPsdReader::DescribeBlendKey(const FString& InKey)
{
	static const TMap<FString, FString> Names = {
		{ TEXT("pass"), TEXT("Pass Through") }, { TEXT("norm"), TEXT("Normal") }, { TEXT("diss"), TEXT("Dissolve") },
		{ TEXT("dark"), TEXT("Darken") }, { TEXT("mul "), TEXT("Multiply") }, { TEXT("idiv"), TEXT("Color Burn") },
		{ TEXT("lbrn"), TEXT("Linear Burn") }, { TEXT("dkCl"), TEXT("Darker Color") }, { TEXT("lite"), TEXT("Lighten") },
		{ TEXT("scrn"), TEXT("Screen") }, { TEXT("div "), TEXT("Color Dodge") }, { TEXT("lddg"), TEXT("Linear Dodge (Add)") },
		{ TEXT("lgCl"), TEXT("Lighter Color") }, { TEXT("over"), TEXT("Overlay") }, { TEXT("sLit"), TEXT("Soft Light") },
		{ TEXT("hLit"), TEXT("Hard Light") }, { TEXT("vLit"), TEXT("Vivid Light") }, { TEXT("lLit"), TEXT("Linear Light") },
		{ TEXT("pLit"), TEXT("Pin Light") }, { TEXT("hMix"), TEXT("Hard Mix") }, { TEXT("diff"), TEXT("Difference") },
		{ TEXT("smud"), TEXT("Exclusion") }, { TEXT("fsub"), TEXT("Subtract") }, { TEXT("fdiv"), TEXT("Divide") },
		{ TEXT("hue "), TEXT("Hue") }, { TEXT("sat "), TEXT("Saturation") }, { TEXT("colr"), TEXT("Color") }, { TEXT("lum "), TEXT("Luminosity") },
	};
	const FString* Name = Names.Find(InKey);
	return Name != nullptr ? *Name : FString::Printf(TEXT("'%s'"), *InKey);
}
