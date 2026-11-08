// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Import/DreamPsdMapper.h"
#include "Psd/DreamPsdColor.h"
#include "Psd/DreamPsdRaster.h"
#include "Psd/DreamPsdReader.h"

namespace DreamPsdMapperLocal
{
	using namespace DreamPsdDui;

	/** Rect-sized pixels at a rectangle, as a box. */
	FBox2D ToBox(const FIntRect& InRect)
	{
		return FBox2D(FVector2D(InRect.Min), FVector2D(InRect.Max));
	}

	FIntRect Outer(const FBox2D& InBox)
	{
		// Path points are 8.24 fixed point of the document's size: 60 comes back as 60.0000024, which is not a pixel more.
		constexpr double Slack = 1e-3;
		return FIntRect(FMath::FloorToInt(InBox.Min.X + Slack), FMath::FloorToInt(InBox.Min.Y + Slack),
			FMath::CeilToInt(InBox.Max.X - Slack), FMath::CeilToInt(InBox.Max.Y - Slack));
	}

	FIntRect Intersect(const FIntRect& A, const FIntRect& B)
	{
		FIntRect R(FMath::Max(A.Min.X, B.Min.X), FMath::Max(A.Min.Y, B.Min.Y), FMath::Min(A.Max.X, B.Max.X), FMath::Min(A.Max.Y, B.Max.Y));
		if (R.Max.X < R.Min.X) { R.Max.X = R.Min.X; }
		if (R.Max.Y < R.Min.Y) { R.Max.Y = R.Min.Y; }
		return R;
	}

	FIntRect Expand(const FIntRect& InRect, int32 InBy)
	{
		return FIntRect(InRect.Min - FIntPoint(InBy), InRect.Max + FIntPoint(InBy));
	}

	FColor WithAlpha(const FLinearColor& InColour, float InAlpha)
	{
		FLinearColor C = InColour;
		C.A = FMath::Clamp(InAlpha, 0.0f, 1.0f);
		return DreamPsdColor::ToColor(C);
	}

	const TCHAR* EffectName(EDreamPsdEffectKind InKind)
	{
		switch (InKind)
		{
		case EDreamPsdEffectKind::DropShadow: return TEXT("drop shadow");
		case EDreamPsdEffectKind::InnerShadow: return TEXT("inner shadow");
		case EDreamPsdEffectKind::OuterGlow: return TEXT("outer glow");
		case EDreamPsdEffectKind::InnerGlow: return TEXT("inner glow");
		case EDreamPsdEffectKind::BevelEmboss: return TEXT("bevel and emboss");
		case EDreamPsdEffectKind::Satin: return TEXT("satin");
		case EDreamPsdEffectKind::ColorOverlay: return TEXT("colour overlay");
		case EDreamPsdEffectKind::GradientOverlay: return TEXT("gradient overlay");
		case EDreamPsdEffectKind::PatternOverlay: return TEXT("pattern overlay");
		case EDreamPsdEffectKind::Stroke: return TEXT("stroke");
		default: return TEXT("effect");
		}
	}

	const TCHAR* AdjustmentName(EDreamPsdAdjustmentKind InKind)
	{
		switch (InKind)
		{
		case EDreamPsdAdjustmentKind::BrightnessContrast: return TEXT("brightness/contrast");
		case EDreamPsdAdjustmentKind::Levels: return TEXT("levels");
		case EDreamPsdAdjustmentKind::Curves: return TEXT("curves");
		case EDreamPsdAdjustmentKind::Exposure: return TEXT("exposure");
		case EDreamPsdAdjustmentKind::Vibrance: return TEXT("vibrance");
		case EDreamPsdAdjustmentKind::HueSaturation: return TEXT("hue/saturation");
		case EDreamPsdAdjustmentKind::ColorBalance: return TEXT("colour balance");
		case EDreamPsdAdjustmentKind::BlackWhite: return TEXT("black and white");
		case EDreamPsdAdjustmentKind::PhotoFilter: return TEXT("photo filter");
		case EDreamPsdAdjustmentKind::ChannelMixer: return TEXT("channel mixer");
		case EDreamPsdAdjustmentKind::Invert: return TEXT("invert");
		case EDreamPsdAdjustmentKind::Posterize: return TEXT("posterize");
		case EDreamPsdAdjustmentKind::Threshold: return TEXT("threshold");
		case EDreamPsdAdjustmentKind::GradientMap: return TEXT("gradient map");
		case EDreamPsdAdjustmentKind::SelectiveColor: return TEXT("selective colour");
		case EDreamPsdAdjustmentKind::ColorLookup: return TEXT("colour lookup");
		default: return TEXT("adjustment");
		}
	}

	/** The corner radii as DreamGUI orders them -- right bottom, right top, left top, left bottom -- from top left, top right, bottom right, bottom left. */
	FVector4 ToDreamCorners(const FVector4& InTlTrBrBl)
	{
		return FVector4(InTlTrBrBl.Z, InTlTrBrBl.Y, InTlTrBrBl.X, InTlTrBrBl.W);
	}

	/** The bounds of a path's anchors: for a live shape, its box. */
	FBox2D AnchorBounds(const FDreamPsdPath& InPath)
	{
		FBox2D Box(ForceInit);
		for (const FDreamPsdSubpath& Sub : InPath.Subpaths)
		{
			for (const FDreamPsdPathKnot& K : Sub.Knots)
			{
				Box += K.Anchor;
			}
		}
		return Box;
	}

	bool IsRecognizedShape(const FDreamPsdLayer& L)
	{
		const EDreamPsdLiveShapeKind K = L.LiveShape.Kind;
		return (K == EDreamPsdLiveShapeKind::Rectangle || K == EDreamPsdLiveShapeKind::RoundedRectangle || K == EDreamPsdLiveShapeKind::Ellipse)
			&& !L.LiveShape.bTransformed && L.VectorMask.Path.Subpaths.Num() == 1 && !L.VectorMask.bInvert;
	}

	/** An FVector4f property, one line per component: the language has no short form for it. */
	void SetCorners(FDreamPsdDuiNode& Out, const TCHAR* InName, const FVector4& InCorners)
	{
		Out.Set(FString::Printf(TEXT("%s.X"), InName), Number(InCorners.X));
		Out.Set(FString::Printf(TEXT("%s.Y"), InName), Number(InCorners.Y));
		Out.Set(FString::Printf(TEXT("%s.Z"), InName), Number(InCorners.Z));
		Out.Set(FString::Printf(TEXT("%s.W"), InName), Number(InCorners.W));
	}
}

const FDreamPsdTextureRequest* FDreamPsdMapResult::FindTexture(const FString& InObjectPath) const
{
	return Textures.FindByPredicate([&InObjectPath](const FDreamPsdTextureRequest& T) { return T.ObjectPath == InObjectPath; });
}

FString FDreamPsdMapResult::Summarize() const
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("%d layers mapped, %d skipped, %d textures."), LayersMapped, LayersSkipped, Textures.Num()));
	if (UnmappedFonts.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("Faces with no font mapped (the default font is used): %s."), *FString::Join(UnmappedFonts, TEXT(", "))));
	}
	if (PendingEffects.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("Layer effects not imported yet: %s."), *FString::Join(PendingEffects, TEXT("; "))));
	}
	if (PendingAdjustments.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("Adjustment layers not imported yet: %s."), *FString::Join(PendingAdjustments, TEXT("; "))));
	}
	if (PendingWarps.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("Warped texts drawn from their pixels: %s."), *FString::Join(PendingWarps, TEXT("; "))));
	}
	return FString::Join(Lines, TEXT("\n"));
}

FDreamPsdMapper::FDreamPsdMapper(const FDreamPsdDocument& InDocument, const FDreamPsdMapOptions& InOptions)
	: Document(InDocument), Options(InOptions)
{
}

FDreamPsdMapper::FNameTags FDreamPsdMapper::ParseName(const FString& InLayerName)
{
	FNameTags Tags;
	FString Name = InLayerName;
	// `!skip`, anywhere.
	const int32 Skip = Name.Find(TEXT("!skip"), ESearchCase::IgnoreCase);
	if (Skip != INDEX_NONE)
	{
		Tags.bSkip = true;
		Name.RemoveAt(Skip, 5);
	}
	// `#9(l,t,r,b)`, `#9(h,v)` or `#9(n)`.
	const int32 Nine = Name.Find(TEXT("#9("), ESearchCase::CaseSensitive);
	if (Nine != INDEX_NONE)
	{
		const int32 Close = Name.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Nine);
		if (Close != INDEX_NONE)
		{
			TArray<FString> Parts;
			Name.Mid(Nine + 3, Close - Nine - 3).ParseIntoArray(Parts, TEXT(","));
			TArray<float> Values;
			for (const FString& Part : Parts)
			{
				Values.Add(FCString::Atof(*Part.TrimStartAndEnd()));
			}
			if (Values.Num() == 1) { Tags.NineSlice = FMargin(Values[0]); Tags.bNineSlice = true; }
			else if (Values.Num() == 2) { Tags.NineSlice = FMargin(Values[0], Values[1]); Tags.bNineSlice = true; }
			else if (Values.Num() >= 4) { Tags.NineSlice = FMargin(Values[0], Values[1], Values[2], Values[3]); Tags.bNineSlice = true; }
			Name.RemoveAt(Nine, Close - Nine + 1);
		}
	}
	// `@Control`: a word after '@'.
	const int32 At = Name.Find(TEXT("@"), ESearchCase::CaseSensitive);
	if (At != INDEX_NONE)
	{
		int32 End = At + 1;
		while (End < Name.Len() && FChar::IsAlnum(Name[End]))
		{
			++End;
		}
		Tags.Control = Name.Mid(At + 1, End - At - 1);
		Name.RemoveAt(At, End - At);
	}
	Tags.Name = Name.TrimStartAndEnd();
	if (Tags.Name.IsEmpty())
	{
		Tags.Name = InLayerName.TrimStartAndEnd();
	}
	return Tags;
}

FString FDreamPsdMapper::BlendModeName(const FString& InKey)
{
	static const TMap<FString, FString> Names = {
		{ TEXT("pass"), TEXT("") }, { TEXT("norm"), TEXT("Alpha") }, { TEXT("diss"), TEXT("Dissolve") }, { TEXT("dark"), TEXT("Darken") },
		{ TEXT("mul "), TEXT("Multiply") }, { TEXT("idiv"), TEXT("ColorBurn") }, { TEXT("lbrn"), TEXT("LinearBurn") }, { TEXT("dkCl"), TEXT("DarkerColor") },
		{ TEXT("lite"), TEXT("Lighten") }, { TEXT("scrn"), TEXT("Screen") }, { TEXT("div "), TEXT("ColorDodge") }, { TEXT("lddg"), TEXT("Additive") },
		{ TEXT("lgCl"), TEXT("LighterColor") }, { TEXT("over"), TEXT("Overlay") }, { TEXT("sLit"), TEXT("SoftLight") }, { TEXT("hLit"), TEXT("HardLight") },
		{ TEXT("vLit"), TEXT("VividLight") }, { TEXT("lLit"), TEXT("LinearLight") }, { TEXT("pLit"), TEXT("PinLight") }, { TEXT("hMix"), TEXT("HardMix") },
		{ TEXT("diff"), TEXT("Difference") }, { TEXT("smud"), TEXT("Exclusion") }, { TEXT("fsub"), TEXT("Subtract") }, { TEXT("fdiv"), TEXT("Divide") },
		{ TEXT("hue "), TEXT("Hue") }, { TEXT("sat "), TEXT("Saturation") }, { TEXT("colr"), TEXT("Color") }, { TEXT("lum "), TEXT("Luminosity") },
	};
	const FString* Name = Names.Find(InKey);
	return Name != nullptr ? *Name : FString(TEXT("Alpha"));
}

FString FDreamPsdMapper::DescriptorBlendModeName(const FString& InMode)
{
	static const TMap<FString, FString> Names = {
		{ TEXT("Nrml"), TEXT("Alpha") }, { TEXT("Dslv"), TEXT("Dissolve") }, { TEXT("Drkn"), TEXT("Darken") }, { TEXT("Mltp"), TEXT("Multiply") },
		{ TEXT("CBrn"), TEXT("ColorBurn") }, { TEXT("linearBurn"), TEXT("LinearBurn") }, { TEXT("darkerColor"), TEXT("DarkerColor") },
		{ TEXT("Lghn"), TEXT("Lighten") }, { TEXT("Scrn"), TEXT("Screen") }, { TEXT("CDdg"), TEXT("ColorDodge") }, { TEXT("linearDodge"), TEXT("Additive") },
		{ TEXT("lighterColor"), TEXT("LighterColor") }, { TEXT("Ovrl"), TEXT("Overlay") }, { TEXT("SftL"), TEXT("SoftLight") }, { TEXT("HrdL"), TEXT("HardLight") },
		{ TEXT("vividLight"), TEXT("VividLight") }, { TEXT("linearLight"), TEXT("LinearLight") }, { TEXT("pinLight"), TEXT("PinLight") },
		{ TEXT("hardMix"), TEXT("HardMix") }, { TEXT("Dfrn"), TEXT("Difference") }, { TEXT("Xclu"), TEXT("Exclusion") },
		{ TEXT("blendSubtraction"), TEXT("Subtract") }, { TEXT("blendDivide"), TEXT("Divide") }, { TEXT("H   "), TEXT("Hue") },
		{ TEXT("Strt"), TEXT("Saturation") }, { TEXT("Clr "), TEXT("Color") }, { TEXT("Lmns"), TEXT("Luminosity") },
	};
	const FString* Name = Names.Find(InMode);
	return Name != nullptr ? *Name : FString(TEXT("Alpha"));
}

int32 FDreamPsdMapper::LayerIdOf(const FDreamPsdLayer& InLayer) const
{
	// Without ids (an old writer), the record's place stands in; a reimport then matches by position.
	return InLayer.LayerId >= 0 ? InLayer.LayerId : 1000000 + InLayer.RecordIndex;
}

FString FDreamPsdMapper::TextureName(const TCHAR* InPrefix, const FDreamPsdLayer& InLayer, const FNameTags& InTags) const
{
	return FString::Printf(TEXT("%s_%s_%s_%d"), InPrefix, *DreamPsdDui::AssetSafe(Options.DocumentName, TEXT("Document")),
		*DreamPsdDui::AssetSafe(InTags.Name, TEXT("Layer")), LayerIdOf(InLayer));
}

FString FDreamPsdMapper::AddTexture(FDreamPsdMapResult& OutResult, const FString& InName, const FDreamPsdImage& InImage, int32 InLayerId, const TCHAR* InPurpose, bool bInTile) const
{
	FDreamPsdTextureRequest& T = OutResult.Textures.AddDefaulted_GetRef();
	T.Name = InName;
	T.ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Options.TextureFolder, *InName, *InName);
	T.Image = InImage;
	T.LayerId = InLayerId;
	T.Purpose = InPurpose;
	T.bTile = bInTile;
	return T.ObjectPath;
}

FString FDreamPsdMapper::AddMaskTexture(FDreamPsdMapResult& OutResult, const FString& InName, const FDreamPsdGray& InMask, int32 InLayerId) const
{
	FDreamPsdTextureRequest& T = OutResult.Textures.AddDefaulted_GetRef();
	T.Name = InName;
	T.ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Options.TextureFolder, *InName, *InName);
	T.Gray = InMask;
	T.bGray = true;
	T.LayerId = InLayerId;
	T.Purpose = TEXT("mask");
	return T.ObjectPath;
}

FString FDreamPsdMapper::FindFont(const FString& InFace, FDreamPsdMapResult& OutResult) const
{
	if (const FString* Exact = Options.FontMap.Find(InFace))
	{
		return *Exact;
	}
	for (const TPair<FString, FString>& Entry : Options.FontMap)
	{
		if (Entry.Key.Equals(InFace, ESearchCase::IgnoreCase))
		{
			return Entry.Value;
		}
	}
	// The family alone: "Roboto" maps "Roboto-Bold" when there is no entry for the face itself.
	FString Family, Style;
	if (!InFace.Split(TEXT("-"), &Family, &Style))
	{
		Family = InFace;
	}
	for (const TPair<FString, FString>& Entry : Options.FontMap)
	{
		if (Entry.Key.Equals(Family, ESearchCase::IgnoreCase))
		{
			return Entry.Value;
		}
	}
	if (!InFace.IsEmpty())
	{
		OutResult.UnmappedFonts.AddUnique(InFace);
	}
	return Options.DefaultFontPath;
}

void FDreamPsdMapper::Place(FDreamPsdDuiNode& Out, const FBox2D& InRect, const FIntRect& InParent)
{
	using namespace DreamPsdDui;
	// Top-left anchored: the document's y runs down, a widget's up.
	Out.Set(TEXT("AnchorData.AnchorMin"), Vec2(0, 1));
	Out.Set(TEXT("AnchorData.AnchorMax"), Vec2(0, 1));
	Out.Set(TEXT("AnchorData.Pivot"), Vec2(0, 1));
	Out.Set(TEXT("AnchorData.AnchoredPosition"), Vec2(InRect.Min.X - InParent.Min.X, -(InRect.Min.Y - InParent.Min.Y)));
	Out.Set(TEXT("AnchorData.SizeDelta"), Vec2(InRect.GetSize().X, InRect.GetSize().Y));
}

void FDreamPsdMapper::Stretch(FDreamPsdDuiNode& Out)
{
	using namespace DreamPsdDui;
	Out.Set(TEXT("AnchorData.AnchorMin"), Vec2(0, 0));
	Out.Set(TEXT("AnchorData.AnchorMax"), Vec2(1, 1));
	Out.Set(TEXT("AnchorData.Pivot"), Vec2(0.5, 0.5));
	Out.Set(TEXT("AnchorData.AnchoredPosition"), Vec2(0, 0));
	Out.Set(TEXT("AnchorData.SizeDelta"), Vec2(0, 0));
}

void FDreamPsdMapper::ApplyCommon(const FDreamPsdLayer& L, FDreamPsdDuiNode& Out, bool bInBlendOnVisual) const
{
	using namespace DreamPsdDui;
	if (L.Opacity < 255)
	{
		Out.Set(TEXT("RenderOpacity"), Number(L.Opacity / 255.0));
	}
	if (!L.bVisible)
	{
		Out.Set(TEXT("Visibility"), TEXT("Hidden"));
	}
	if (L.bClipping)
	{
		Out.Set(TEXT("bClipToBelow"), Bool(true));
	}
	if (bInBlendOnVisual)
	{
		const FString Mode = BlendModeName(L.BlendKey);
		if (!Mode.IsEmpty() && Mode != TEXT("Alpha"))
		{
			Out.Set(TEXT("BlendMode"), Mode);
		}
	}
}

void FDreamPsdMapper::ApplyMasks(const FDreamPsdLayer& L, const FIntRect& InNodeRect, FDreamPsdDuiNode& Out, FDreamPsdDuiComponent* InGroup,
	bool bInVector, FDreamPsdMapResult& R)
{
	using namespace DreamPsdDui;
	using namespace DreamPsdMapperLocal;
	const bool bPixel = L.Mask.bPresent && !L.Mask.bDisabled && !L.Mask.Pixels.IsEmpty();
	const bool bVector = bInVector && L.VectorMask.bPresent && !L.VectorMask.bDisabled && !L.VectorMask.Path.IsEmpty();
	if (!bPixel && !bVector)
	{
		return;
	}
	// The mask's own properties go on the group's GroupMask when the group isolates; on the widget's ClipMask otherwise.
	const FString Prefix = InGroup != nullptr ? TEXT("GroupMask.") : TEXT("ClipMask.");
	auto SetMask = [&Out, InGroup, &Prefix](const FString& InName, const FString& InValue)
	{
		if (InGroup != nullptr)
		{
			const FString Name = Prefix + InName;
			FDreamPsdDuiProperty* P = InGroup->Properties.FindByPredicate([&Name](const FDreamPsdDuiProperty& X) { return X.Name == Name; });
			if (P != nullptr) { P->Value = InValue; } else { InGroup->Properties.Add({ Name, InValue }); }
		}
		else
		{
			Out.Set(Prefix + InName, InValue);
		}
	};
	auto SetClipRect = [&Out, &InNodeRect, InGroup](const FBox2D& InClip)
	{
		if (InGroup == nullptr)
		{
			Out.Set(TEXT("Clipping"), TEXT("ClipToBounds"));
		}
		const double Left = InNodeRect.Min.X - InClip.Min.X;
		const double Top = InNodeRect.Min.Y - InClip.Min.Y;
		const double Right = InClip.Max.X - InNodeRect.Max.X;
		const double Bottom = InClip.Max.Y - InNodeRect.Max.Y;
		if (FMath::Abs(Left) > 1e-3 || FMath::Abs(Top) > 1e-3 || FMath::Abs(Right) > 1e-3 || FMath::Abs(Bottom) > 1e-3)
		{
			Out.Set(TEXT("ClippingMargin"), Margin(Left, Top, Right, Bottom));
		}
	};

	// A live shape alone: a shape clip mask, nothing drawn into a texture. A group's mask has no corner radius, so a
	// rounded rectangle on a group is drawn into a texture like any other path.
	const bool bRounded = L.LiveShape.Kind == EDreamPsdLiveShapeKind::RoundedRectangle;
	if (!bPixel && IsRecognizedShape(L) && L.VectorMask.Density >= 1.0f && !(bRounded && InGroup != nullptr))
	{
		const FBox2D Box = AnchorBounds(L.VectorMask.Path);
		SetClipRect(Box);
		if (L.LiveShape.Kind == EDreamPsdLiveShapeKind::Ellipse)
		{
			SetMask(TEXT("Shape"), TEXT("Ellipse"));
		}
		else if (bRounded)
		{
			const FVector4 C = ToDreamCorners(L.LiveShape.Radii);
			SetCorners(Out, TEXT("ClippingCornerRadius"), C);
		}
		if (L.VectorMask.Feather > 0.0f)
		{
			const double F = L.VectorMask.Feather * Options.MaskFeatherScale;
			SetMask(TEXT("Feather"), Margin(F, F, F, F));
		}
		return;
	}

	{
		// One texture of both masks, over the part of the node they can show.
		FIntRect Clip = InNodeRect;
		const int32 PixelBlur = FMath::CeilToInt(L.Mask.Feather * Options.MaskFeatherScale * 3.0f);
		const int32 VectorBlur = FMath::CeilToInt(L.VectorMask.Feather * Options.MaskFeatherScale * 3.0f);
		if (bPixel && L.Mask.DefaultColor == 0 && !L.Mask.bInvertOnBlend)
		{
			Clip = Intersect(Clip, Expand(L.Mask.Rect, PixelBlur));
		}
		if (bVector && !L.VectorMask.bInvert && !L.VectorMask.Path.bStartsFilled)
		{
			Clip = Intersect(Clip, Expand(Outer(L.VectorMask.Path.GetBounds()), VectorBlur));
		}
		if (Clip.Width() <= 0 || Clip.Height() <= 0)
		{
			// Nothing of the layer shows through its mask; keep the node, clipped to nothing.
			Clip = FIntRect(InNodeRect.Min, InNodeRect.Min + FIntPoint(1, 1));
		}
		FDreamPsdGray Mask;
		Mask.Init(Clip.Width(), Clip.Height(), 255);
		if (bPixel)
		{
			Mask = DreamPsdRaster::BakeLayerMask(L.Mask, Clip, Options.MaskFeatherScale);
		}
		if (bVector)
		{
			DreamPsdRaster::MultiplyMask(Mask, DreamPsdRaster::BakeVectorMask(L.VectorMask, Clip, Options.MaskFeatherScale));
		}
		if (DreamPsdRaster::IsAllOpaque(Mask) && Clip == InNodeRect)
		{
			return;
		}
		FNameTags Tags = ParseName(L.Name);
		const FString Path = AddMaskTexture(R, TextureName(TEXT("TM"), L, Tags), Mask, LayerIdOf(L));
		SetClipRect(ToBox(Clip));
		SetMask(TEXT("Source"), TEXT("Texture"));
		SetMask(TEXT("Texture"), Asset(Path));
		// A one-channel texture reads as gray: its red is the mask.
		SetMask(TEXT("Channel"), TEXT("Red"));
	}
}

void FDreamPsdMapper::AddControl(const FNameTags& InTags, FDreamPsdDuiNode& Out, const FDreamPsdLayer& L, FDreamPsdMapResult& R) const
{
	if (InTags.Control.IsEmpty())
	{
		return;
	}
	static const TMap<FString, FString> Controls = {
		{ TEXT("Button"), TEXT("UIButton") }, { TEXT("Toggle"), TEXT("UIToggle") }, { TEXT("Slider"), TEXT("UISlider") },
	};
	for (const TPair<FString, FString>& Entry : Controls)
	{
		if (Entry.Key.Equals(InTags.Control, ESearchCase::IgnoreCase))
		{
			Out.AddComponent(Entry.Value);
			return;
		}
	}
	R.Messages.Warn(FString::Printf(TEXT("'@%s' names no control the importer makes (Button, Toggle, Slider); the name is read without it"), *InTags.Control), L.Name);
}

bool FDreamPsdMapper::Map(FDreamPsdMapResult& R)
{
	using namespace DreamPsdDui;
	R = FDreamPsdMapResult();
	if (Document.Width <= 0 || Document.Height <= 0)
	{
		R.Messages.Error(TEXT("the document has no size"));
		return false;
	}
	if (Document.Layers.ContainsByPredicate([](const FDreamPsdLayer& L) { return L.LayerId < 0 && !L.IsGroupEnd(); }))
	{
		R.Messages.Warn(TEXT("the document's layers have no ids (an old writer): its nodes are named by their place, and a reimport after layers are added or removed may match the wrong ones"));
	}
	R.Root.Type = TEXT("Widget");
	R.Root.Id = TEXT("Root");
	// The root's placement is the author's after the first import: a screen usually stretches it.
	R.Root.Set(TEXT("AnchorData.AnchorMin"), Vec2(0.5, 0.5), true);
	R.Root.Set(TEXT("AnchorData.AnchorMax"), Vec2(0.5, 0.5), true);
	R.Root.Set(TEXT("AnchorData.Pivot"), Vec2(0.5, 0.5), true);
	R.Root.Set(TEXT("AnchorData.SizeDelta"), Vec2(Document.Width, Document.Height), true);
	FDreamPsdMessages TreeMessages;
	const FDreamPsdLayerNode Tree = Document.BuildTree(&TreeMessages);
	R.Messages.Items.Append(TreeMessages.Items);
	MapChildren(Tree, R.Root, FIntRect(0, 0, Document.Width, Document.Height), R);
	return true;
}

void FDreamPsdMapper::MapChildren(const FDreamPsdLayerNode& InNode, FDreamPsdDuiNode& OutParent, const FIntRect& InParentRect, FDreamPsdMapResult& R)
{
	// Children are in drawing order, bottom first -- the order a .dui draws its children in.
	for (const FDreamPsdLayerNode& Child : InNode.Children)
	{
		FDreamPsdDuiNode Node;
		if (MapLayer(Child, InParentRect, Node, R))
		{
			R.NodeIds.Add(Node.LayerId, Node.Id);
			OutParent.Children.Add(MoveTemp(Node));
			++R.LayersMapped;
		}
		else
		{
			++R.LayersSkipped;
		}
	}
}

bool FDreamPsdMapper::MapLayer(const FDreamPsdLayerNode& InNode, const FIntRect& InParentRect, FDreamPsdDuiNode& Out, FDreamPsdMapResult& R)
{
	if (!Document.Layers.IsValidIndex(InNode.LayerIndex))
	{
		return false;
	}
	const FDreamPsdLayer& L = Document.Layers[InNode.LayerIndex];
	FNameTags Tags = Options.bNamingConventions ? ParseName(L.Name) : FNameTags{ L.Name };
	if (Tags.bSkip)
	{
		R.Messages.Info(TEXT("left out: its name says !skip"), L.Name);
		return false;
	}
	if (!L.bVisible && !Options.bImportHiddenLayers)
	{
		R.Messages.Info(TEXT("left out: it is hidden"), L.Name);
		return false;
	}
	Out.LayerId = LayerIdOf(L);
	Out.LayerName = L.Name;
	Out.Id = DreamPsdDui::MakeGeneratedId(Tags.Name, Out.LayerId);
	bool bMapped = false;
	if (InNode.bGroup || L.IsGroupStart())
	{
		bMapped = MapGroup(InNode, L, Tags, InParentRect, Out, R);
	}
	else
	{
		switch (L.Kind)
		{
		case EDreamPsdLayerKind::Text: bMapped = MapText(L, Tags, InParentRect, Out, R); break;
		case EDreamPsdLayerKind::Shape: bMapped = MapShape(L, Tags, InParentRect, Out, R); break;
		case EDreamPsdLayerKind::Fill: bMapped = MapFill(L, Tags, InParentRect, Out, R); break;
		case EDreamPsdLayerKind::Adjustment:
		{
			FIntRect Scope = InParentRect;
			bMapped = MapAdjustment(L, Scope, Out, R);
			break;
		}
		default:
			if (L.bSmartObject)
			{
				R.Messages.Info(TEXT("a smart object: imported as the pixels it renders to"), L.Name);
			}
			bMapped = MapPixels(L, Tags, InParentRect, Out, R);
			break;
		}
	}
	if (!bMapped)
	{
		return false;
	}
	if (L.Effects.HasEnabled())
	{
		MapLayerEffects(L, Out, R);
	}
	AddControl(Tags, Out, L, R);
	return true;
}

bool FDreamPsdMapper::MapGroup(const FDreamPsdLayerNode& InNode, const FDreamPsdLayer& L, const FNameTags& InTags, const FIntRect& InParentRect,
	FDreamPsdDuiNode& Out, FDreamPsdMapResult& R)
{
	using namespace DreamPsdDui;
	Out.Type = TEXT("Widget");
	FIntRect Rect = InParentRect;
	if (L.bArtboard && L.ArtboardRect.Width() > 0 && L.ArtboardRect.Height() > 0)
	{
		// An artboard: its own rectangle, its content clipped to it.
		Rect = L.ArtboardRect;
		Place(Out, DreamPsdMapperLocal::ToBox(Rect), InParentRect);
		Out.Set(TEXT("Clipping"), TEXT("ClipToBounds"));
	}
	else
	{
		Stretch(Out);
	}
	ApplyCommon(L, Out, false);
	// The group's own opacity is the group's, not each child's.
	Out.Properties.RemoveAll([](const FDreamPsdDuiProperty& P) { return P.Name == TEXT("RenderOpacity"); });

	const FString Mode = BlendModeName(L.BlendKey);
	const bool bPassThrough = Mode.IsEmpty();
	const bool bFades = L.Opacity < 255;
	const bool bMasked = (L.Mask.bPresent && !L.Mask.bDisabled) || (L.VectorMask.bPresent && !L.VectorMask.bDisabled);
	if (!bPassThrough || bFades)
	{
		// Blends or fades as a whole: a layer group, which draws the group apart and puts it down once.
		FDreamPsdDuiComponent& Group = Out.AddComponent(TEXT("DreamLayerGroup"));
		Group.Properties.Add({ TEXT("Isolation"), TEXT("Isolated") });
		if (bFades)
		{
			Group.Properties.Add({ TEXT("GroupOpacity"), Number(L.Opacity / 255.0) });
		}
		if (!bPassThrough && Mode != TEXT("Alpha"))
		{
			Group.Properties.Add({ TEXT("GroupBlendMode"), Mode });
		}
		if (bMasked)
		{
			ApplyMasks(L, Rect, Out, &Group, true, R);
		}
		if (bPassThrough)
		{
			R.Messages.Info(TEXT("a pass-through group with an opacity: drawn as an isolated group (the editor's pass-through fade looks the same unless its layers blend with what is below the group)"), L.Name);
		}
	}
	else if (bMasked)
	{
		// Passes through and is masked: each child is clipped by the group's mask, which is what passing through means.
		ApplyMasks(L, Rect, Out, nullptr, true, R);
	}
	MapChildren(InNode, Out, Rect, R);
	return true;
}

bool FDreamPsdMapper::MapPixels(const FDreamPsdLayer& L, const FNameTags& InTags, const FIntRect& InParentRect, FDreamPsdDuiNode& Out, FDreamPsdMapResult& R, bool bInVectorIsMask)
{
	using namespace DreamPsdDui;
	if (!L.HasPixels())
	{
		R.Messages.Info(TEXT("left out: it has no pixels"), L.Name);
		return false;
	}
	FIntRect Content(0, 0, L.Pixels.Width, L.Pixels.Height);
	if (Options.bTrimTransparentPixels)
	{
		Content = DreamPsdRaster::FindOpaqueBounds(L.Pixels);
		if (Content.Width() <= 0 || Content.Height() <= 0)
		{
			R.Messages.Info(TEXT("left out: every pixel is transparent"), L.Name);
			return false;
		}
	}
	FDreamPsdImage Image = Content == FIntRect(0, 0, L.Pixels.Width, L.Pixels.Height) ? L.Pixels : DreamPsdRaster::Crop(L.Pixels, Content);
	const FIntRect DocRect(L.Rect.Min + Content.Min, L.Rect.Min + Content.Max);
	const bool bBake = Options.bBakeLayerMasks;
	if (bBake)
	{
		const bool bPixel = L.Mask.bPresent && !L.Mask.bDisabled;
		const bool bVector = bInVectorIsMask && L.VectorMask.bPresent && !L.VectorMask.bDisabled;
		if (bPixel)
		{
			DreamPsdRaster::MultiplyAlpha(Image, DreamPsdRaster::BakeLayerMask(L.Mask, DocRect, Options.MaskFeatherScale));
		}
		if (bVector)
		{
			DreamPsdRaster::MultiplyAlpha(Image, DreamPsdRaster::BakeVectorMask(L.VectorMask, DocRect, Options.MaskFeatherScale));
		}
	}
	const FString Path = AddTexture(R, TextureName(TEXT("T"), L, InTags), Image, LayerIdOf(L), TEXT("layer"));
	Out.Type = TEXT("Image");
	Place(Out, DreamPsdMapperLocal::ToBox(DocRect), InParentRect);
	Out.Set(TEXT("Brush.ResourceObject"), Asset(Path));
	if (InTags.bNineSlice && Image.Width > 0 && Image.Height > 0)
	{
		// The brush's margins are fractions of the image.
		Out.Set(TEXT("Brush.DrawAs"), TEXT("Box"));
		Out.Set(TEXT("Brush.Margin"), Margin(InTags.NineSlice.Left / Image.Width, InTags.NineSlice.Top / Image.Height,
			InTags.NineSlice.Right / Image.Width, InTags.NineSlice.Bottom / Image.Height));
	}
	if (L.FillOpacity < 255)
	{
		Out.Set(TEXT("Brush.TintColor"), Color(FColor(255, 255, 255, L.FillOpacity)));
	}
	ApplyCommon(L, Out, true);
	if (!bBake)
	{
		ApplyMasks(L, DocRect, Out, nullptr, bInVectorIsMask, R);
	}
	return true;
}

bool FDreamPsdMapper::MapText(const FDreamPsdLayer& L, const FNameTags& InTags, const FIntRect& InParentRect, FDreamPsdDuiNode& Out, FDreamPsdMapResult& R)
{
	using namespace DreamPsdDui;
	const FDreamPsdText& T = *L.Text;
	bool bRasterize = Options.bRasterizeText;
	if (!bRasterize && !T.Warp.IsNone())
	{
		if (!MapTextWarp(L, Out, R))
		{
			bRasterize = true;
		}
	}
	if (!bRasterize && (!T.IsAxisAligned() || T.bVertical))
	{
		R.Messages.Warn(T.bVertical ? TEXT("vertical text: drawn from its pixels") : TEXT("turned or skewed text: drawn from its pixels"), L.Name);
		bRasterize = true;
	}
	if (bRasterize)
	{
		if (MapPixels(L, InTags, InParentRect, Out, R))
		{
			return true;
		}
		R.Messages.Warn(TEXT("a text to draw from its pixels has none; it is imported as text"), L.Name);
	}
	if (T.StyleRuns.Num() == 0)
	{
		return MapPixels(L, InTags, InParentRect, Out, R);
	}
	const FDreamPsdTextStyleRun& Style = T.StyleRuns[0];
	if (T.StyleRuns.Num() > 1)
	{
		R.Messages.Warn(FString::Printf(TEXT("%d style runs: the text takes the first one's face, size and colour"), T.StyleRuns.Num()), L.Name);
	}
	const double Scale = T.GetScale();
	const double Size = Style.FontSize * Scale;
	Out.Type = TEXT("Text");
	FBox2D Rect;
	if (T.bBoxText && T.BoxBounds.bIsValid)
	{
		const FVector2D A(T.BoxBounds.Min.X * T.Transform[0] + T.Transform[4], T.BoxBounds.Min.Y * T.Transform[3] + T.Transform[5]);
		const FVector2D B(T.BoxBounds.Max.X * T.Transform[0] + T.Transform[4], T.BoxBounds.Max.Y * T.Transform[3] + T.Transform[5]);
		Rect = FBox2D(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
	}
	else if (L.Rect.Width() > 0 && L.Rect.Height() > 0)
	{
		// Point text: the pixels it draws, with room either side for a face that is wider than the one it was set in.
		Rect = DreamPsdMapperLocal::ToBox(L.Rect);
	}
	else
	{
		Rect = FBox2D(FVector2D(T.Transform[4], T.Transform[5] - Size), FVector2D(T.Transform[4] + Size * FMath::Max(1, T.Text.Len()), T.Transform[5] + Size * 0.3));
	}
	Place(Out, Rect, InParentRect);
	FString Text = T.Text;
	Text.ReplaceInline(TEXT("\r"), TEXT("\n"));
	Out.Set(TEXT("Text"), String(Text));
	const FString Font = FindFont(Style.FontName, R);
	if (!Font.IsEmpty())
	{
		Out.Set(TEXT("Font"), Asset(Font));
	}
	Out.Set(TEXT("FontSize"), Number(FMath::RoundToDouble(Size * 10.0) / 10.0));
	Out.Set(TEXT("Color"), Color(DreamPsdMapperLocal::WithAlpha(Style.Color, Style.Color.A * L.FillOpacity / 255.0f)));
	const int32 Justification = T.ParagraphRuns.Num() > 0 ? T.ParagraphRuns[0].Justification : 0;
	Out.Set(TEXT("HAlign"), Justification == 1 ? TEXT("Right") : (Justification == 2 ? TEXT("Center") : (Justification >= 3 ? TEXT("Justify") : TEXT("Left"))));
	Out.Set(TEXT("VAlign"), T.bBoxText ? TEXT("Top") : TEXT("Middle"));
	Out.Set(TEXT("OverflowType"), T.bBoxText ? TEXT("VerticalOverflow") : TEXT("HorizontalOverflow"));
	if (FMath::Abs(Style.Tracking) > 0.5f)
	{
		Out.Set(TEXT("FontSpace"), Vec2(Style.Tracking / 1000.0 * Size, 0));
	}
	if (!Style.bAutoLeading && Style.Leading > 0.0f && Style.FontSize > 0.0f)
	{
		// The editor's automatic leading is 120 % of the size.
		Out.Set(TEXT("LineHeightPercentage"), Number(Style.Leading / (Style.FontSize * 1.2)));
	}
	if (Style.bUnderline)
	{
		Out.Set(TEXT("bUnderline"), Bool(true));
	}
	if (Style.bStrikethrough)
	{
		Out.Set(TEXT("bStrikethrough"), Bool(true));
	}
	if (Style.bFauxBold || Style.bFauxItalic)
	{
		Out.Set(TEXT("FontStyle"), Style.bFauxBold && Style.bFauxItalic ? TEXT("BoldAndItalic") : (Style.bFauxBold ? TEXT("Bold") : TEXT("Italic")));
	}
	ApplyCommon(L, Out, true);
	ApplyMasks(L, DreamPsdMapperLocal::Outer(Rect), Out, nullptr, true, R);
	return true;
}

bool FDreamPsdMapper::MapShape(const FDreamPsdLayer& L, const FNameTags& InTags, const FIntRect& InParentRect, FDreamPsdDuiNode& Out, FDreamPsdMapResult& R)
{
	using namespace DreamPsdDui;
	using namespace DreamPsdMapperLocal;
	const bool bShape = IsRecognizedShape(L);
	const bool bSolid = L.Fill.Kind == FDreamPsdFill::EKind::Solid;
	const bool bGradient = L.Fill.Kind == FDreamPsdFill::EKind::Gradient;
	FBox2D Box = AnchorBounds(L.VectorMask.Path);
	const bool bCircle = FMath::Abs(Box.GetSize().X - Box.GetSize().Y) < 0.5;
	const bool bEllipse = L.LiveShape.Kind == EDreamPsdLiveShapeKind::Ellipse;
	const bool bStroke = L.Stroke.bPresent && L.Stroke.bEnabled && L.Stroke.Width > 0.0f;
	const bool bRectBlock = bShape && (bSolid || bGradient) && Box.bIsValid && !(bEllipse && !bCircle && bStroke);
	if (!bRectBlock)
	{
		if (L.HasPixels())
		{
			// The editor stores what the shape renders to: draw that. The vector mask is the shape, so it is not a mask here.
			R.Messages.Info(TEXT("a shape other than a rectangle, rounded rectangle or circle: drawn from its pixels"), L.Name);
			return MapPixels(L, InTags, InParentRect, Out, R, false);
		}
		// No pixels: fill the path ourselves.
		const FIntRect Region = Intersect(Outer(L.VectorMask.Path.GetBounds()), FIntRect(0, 0, Document.Width, Document.Height));
		if (Region.Width() <= 0 || Region.Height() <= 0)
		{
			R.Messages.Info(TEXT("left out: its shape covers nothing"), L.Name);
			return false;
		}
		FDreamPsdGray Coverage = DreamPsdRaster::FillPath(L.VectorMask.Path, Region);
		FDreamPsdImage Image;
		if (bGradient)
		{
			Image = DreamPsdRaster::DrawGradient(L.Fill.Gradient, ToBox(Region), Region.Size());
		}
		else
		{
			Image.Init(Region.Width(), Region.Height(), DreamPsdColor::ToColor(L.Fill.Color));
		}
		DreamPsdRaster::MultiplyAlpha(Image, Coverage);
		const FString Path = AddTexture(R, TextureName(TEXT("T"), L, InTags), Image, LayerIdOf(L), TEXT("shape"));
		Out.Type = TEXT("Image");
		Place(Out, ToBox(Region), InParentRect);
		Out.Set(TEXT("Brush.ResourceObject"), Asset(Path));
		if (L.FillOpacity < 255)
		{
			Out.Set(TEXT("Brush.TintColor"), Color(FColor(255, 255, 255, L.FillOpacity)));
		}
		ApplyCommon(L, Out, true);
		ApplyMasks(L, Region, Out, nullptr, false, R);
		return true;
	}

	// A RectBlock: the box, grown for a stroke that is not all inside, the stroke as its border.
	FVector4 Radii = bEllipse ? FVector4(Box.GetSize().X * 0.5) : (L.LiveShape.Kind == EDreamPsdLiveShapeKind::RoundedRectangle ? L.LiveShape.Radii : FVector4(0, 0, 0, 0));
	if (bEllipse && !bCircle)
	{
		Radii = FVector4(0, 0, 0, 0);
	}
	if (bStroke && L.Stroke.Alignment != 0)
	{
		const double Grow = L.Stroke.Alignment == 2 ? L.Stroke.Width : L.Stroke.Width * 0.5;
		Box = Box.ExpandBy(Grow);
		Radii += FVector4(Grow, Grow, Grow, Grow);
		if (bEllipse && !bCircle)
		{
			Radii = FVector4(0, 0, 0, 0);
		}
	}
	Out.Type = TEXT("RectBlock");
	Place(Out, Box, InParentRect);
	const FVector4 Corners = ToDreamCorners(Radii);
	SetCorners(Out, TEXT("CornerRadius"), Corners);
	Out.Set(TEXT("CornerRadiusUnitMode"), TEXT("Value"));
	const float Fill = L.FillOpacity / 255.0f;
	if (!L.Stroke.bFillEnabled && L.Stroke.bPresent)
	{
		Out.Set(TEXT("bEnableBody"), Bool(false));
	}
	else if (bGradient)
	{
		const FIntPoint Size(FMath::Clamp(FMath::CeilToInt(Box.GetSize().X), 1, Options.MaxGradientTextureSize),
			FMath::Clamp(FMath::CeilToInt(Box.GetSize().Y), 1, Options.MaxGradientTextureSize));
		const FString Path = AddTexture(R, TextureName(TEXT("TG"), L, InTags), DreamPsdRaster::DrawGradient(L.Fill.Gradient, Box, Size), LayerIdOf(L), TEXT("gradient"));
		Out.Set(TEXT("BodyColor"), Color(FColor(255, 255, 255, (uint8)FMath::RoundToInt(Fill * 255.0f))));
		Out.Set(TEXT("BodyTextureMode"), TEXT("Texture"));
		Out.Set(TEXT("BodyTexture"), Asset(Path));
	}
	else
	{
		Out.Set(TEXT("BodyColor"), Color(WithAlpha(L.Fill.Color, Fill)));
	}
	if (bStroke)
	{
		Out.Set(TEXT("bEnableBorder"), Bool(true));
		Out.Set(TEXT("BorderWidth"), Number(L.Stroke.Width));
		Out.Set(TEXT("BorderColor"), Color(WithAlpha(L.Stroke.Color, L.Stroke.Opacity * Fill)));
		if (L.Stroke.BlendMode != TEXT("Nrml") && !L.Stroke.BlendMode.IsEmpty())
		{
			R.Messages.Warn(FString::Printf(TEXT("its stroke blends as '%s', which a border cannot: drawn Normal"), *L.Stroke.BlendMode), L.Name);
		}
	}
	ApplyCommon(L, Out, true);
	if (bEllipse && !bCircle)
	{
		// An ellipse that is not a circle: the block's rectangle, clipped to the ellipse inside it.
		Out.Set(TEXT("Clipping"), TEXT("ClipToBounds"));
		Out.Set(TEXT("ClipMask.Shape"), TEXT("Ellipse"));
		if (L.Mask.bPresent && !L.Mask.bDisabled)
		{
			R.Messages.Warn(TEXT("an ellipse with a pixel mask: the mask is multiplied in as a texture, the ellipse made a texture with it"), L.Name);
			Out.Properties.RemoveAll([](const FDreamPsdDuiProperty& P) { return P.Name == TEXT("ClipMask.Shape"); });
			ApplyMasks(L, Outer(Box), Out, nullptr, true, R);
		}
		return true;
	}
	ApplyMasks(L, Outer(Box), Out, nullptr, false, R);
	return true;
}

bool FDreamPsdMapper::MapFill(const FDreamPsdLayer& L, const FNameTags& InTags, const FIntRect& InParentRect, FDreamPsdDuiNode& Out, FDreamPsdMapResult& R)
{
	using namespace DreamPsdDui;
	using namespace DreamPsdMapperLocal;
	// A fill layer fills the whole canvas; its masks say where it shows.
	const FIntRect Canvas(0, 0, Document.Width, Document.Height);
	const float Fill = L.FillOpacity / 255.0f;
	if (L.Fill.Kind == FDreamPsdFill::EKind::Pattern)
	{
		const FDreamPsdPattern* Pattern = Document.FindPattern(L.Fill.Pattern.PatternId);
		if (Pattern == nullptr)
		{
			if (L.HasPixels())
			{
				R.Messages.Warn(TEXT("its pattern is not in the document: drawn from its pixels"), L.Name);
				return MapPixels(L, InTags, InParentRect, Out, R);
			}
			R.Messages.Warn(TEXT("left out: its pattern is not in the document"), L.Name);
			return false;
		}
		const FString Name = FString::Printf(TEXT("TP_%s_%s"), *AssetSafe(Options.DocumentName, TEXT("Document")), *AssetSafe(Pattern->Name, TEXT("Pattern")));
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Options.TextureFolder, *Name, *Name);
		const FString Path = R.FindTexture(ObjectPath) != nullptr ? ObjectPath : AddTexture(R, Name, Pattern->Image, LayerIdOf(L), TEXT("pattern"), true);
		Out.Type = TEXT("Image");
		Place(Out, ToBox(Canvas), InParentRect);
		Out.Set(TEXT("Brush.ResourceObject"), Asset(Path));
		Out.Set(TEXT("Brush.DrawAs"), TEXT("Tiled"));
		Out.Set(TEXT("Brush.ImageSize"), Vec2(Pattern->Image.Width * L.Fill.Pattern.Scale, Pattern->Image.Height * L.Fill.Pattern.Scale));
		if (L.FillOpacity < 255)
		{
			Out.Set(TEXT("Brush.TintColor"), Color(FColor(255, 255, 255, L.FillOpacity)));
		}
		ApplyCommon(L, Out, true);
		ApplyMasks(L, Canvas, Out, nullptr, true, R);
		return true;
	}
	Out.Type = TEXT("RectBlock");
	Place(Out, ToBox(Canvas), InParentRect);
	SetCorners(Out, TEXT("CornerRadius"), FVector4(0, 0, 0, 0));
	Out.Set(TEXT("CornerRadiusUnitMode"), TEXT("Value"));
	if (L.Fill.Kind == FDreamPsdFill::EKind::Gradient)
	{
		const FIntPoint Size(FMath::Clamp(Canvas.Width(), 1, Options.MaxGradientTextureSize), FMath::Clamp(Canvas.Height(), 1, Options.MaxGradientTextureSize));
		const FString Path = AddTexture(R, TextureName(TEXT("TG"), L, InTags), DreamPsdRaster::DrawGradient(L.Fill.Gradient, ToBox(Canvas), Size), LayerIdOf(L), TEXT("gradient"));
		Out.Set(TEXT("BodyColor"), Color(FColor(255, 255, 255, (uint8)FMath::RoundToInt(Fill * 255.0f))));
		Out.Set(TEXT("BodyTextureMode"), TEXT("Texture"));
		Out.Set(TEXT("BodyTexture"), Asset(Path));
	}
	else
	{
		Out.Set(TEXT("BodyColor"), Color(WithAlpha(L.Fill.Color, Fill)));
	}
	ApplyCommon(L, Out, true);
	ApplyMasks(L, Canvas, Out, nullptr, true, R);
	return true;
}

bool FDreamPsdMapper::MapLayerEffects(const FDreamPsdLayer& L, FDreamPsdDuiNode& InOutNode, FDreamPsdMapResult& R)
{
	TArray<FString> Kinds;
	for (const FDreamPsdEffect& E : L.Effects.Effects)
	{
		if (E.bEnabled)
		{
			Kinds.AddUnique(DreamPsdMapperLocal::EffectName(E.Kind));
		}
	}
	R.PendingEffects.Add(FString::Printf(TEXT("%s (%d): %s"), *L.Name, LayerIdOf(L), *FString::Join(Kinds, TEXT(", "))));
	R.Messages.Warn(FString::Printf(TEXT("its layer effects (%s) are read but not imported yet"), *FString::Join(Kinds, TEXT(", "))), L.Name);
	return false;
}

bool FDreamPsdMapper::MapAdjustment(const FDreamPsdLayer& L, const FIntRect& InScope, FDreamPsdDuiNode& OutNode, FDreamPsdMapResult& R)
{
	const TCHAR* Kind = L.Adjustment.IsValid() ? DreamPsdMapperLocal::AdjustmentName(L.Adjustment->Kind) : TEXT("adjustment");
	R.PendingAdjustments.Add(FString::Printf(TEXT("%s (%d): %s"), *L.Name, LayerIdOf(L), Kind));
	R.Messages.Warn(FString::Printf(TEXT("an adjustment layer (%s): read, not imported yet"), Kind), L.Name);
	return false;
}

bool FDreamPsdMapper::MapTextWarp(const FDreamPsdLayer& L, FDreamPsdDuiNode& InOutNode, FDreamPsdMapResult& R)
{
	R.PendingWarps.Add(FString::Printf(TEXT("%s (%d): %s"), *L.Name, LayerIdOf(L), *L.Text->Warp.Style));
	R.Messages.Warn(FString::Printf(TEXT("its text is warped (%s): drawn from its pixels until warps are imported"), *L.Text->Warp.Style), L.Name);
	return false;
}
