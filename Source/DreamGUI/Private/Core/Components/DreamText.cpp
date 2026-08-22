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
#include "Core/Components/DreamWidget.h"
#include "Utils/DreamUIUtils.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/World.h"


#define LOCTEXT_NAMESPACE "UIText"

FDreamTextLayoutInput UDreamText::MakeLayoutInput(const UDreamText* Text, float InFontSize)
{
	auto Widget = Text->GetWidget();
	auto RenderCanvas = Widget->GetRenderCanvas();
	auto RootCanvas = RenderCanvas ? RenderCanvas->GetRootCanvas() : nullptr;

	FDreamTextLayoutInput Input;
	Input.Content = Text->GetText().ToString();
	FVector2f ContentSize, ContentPivot;
	UDreamText::GetContentBox(FVector2f(Widget->GetWidth(), Widget->GetHeight()), FVector2f(Widget->GetPivot()), Text->GetMargin(),
		ContentSize, ContentPivot);
	Input.Width = ContentSize.X;
	Input.Height = ContentSize.Y;
	Input.Pivot = ContentPivot;
	Input.Color = Text->GetFinalColor();
	Input.RenderOpacityForRichText = (uint8)((Text->GetRichText() ? Widget->GetFinalRenderOpacity() : 1.0f) * 255);
	Input.FontSpace = FVector2f(Text->GetFontSpace());
	Input.FontSize = InFontSize;
	Input.ParagraphHAlign = Text->GetParagraphHorizontalAlignment();
	Input.ParagraphVAlign = Text->GetParagraphVerticalAlignment();
	Input.OverflowType = Text->GetOverflowType();
	Input.WrappingPolicy = Text->GetWrappingPolicy();
	Input.PhraseWrap = Text->GetPhraseWrap();
	Input.bUseKerning = Text->GetUseKerning();
	Input.FontStyle = Text->GetFontStyle();
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
	return Input;
}

FDreamTextPaintParams UDreamText::MakePaintParams(const UDreamText* Text)
{
	auto Widget = Text->GetWidget();
	auto RenderCanvas = Widget->GetRenderCanvas();

	FDreamTextPaintParams Params;
	const FVector WorldScale = Widget->GetWorldScale();
	const FDreamTextGlyphPaintStyle Style = Text->GetFont()->GetGlyphPaintStyle(FVector2f((float)WorldScale.X, (float)WorldScale.Y));
	Params.ItalicSlope = Style.ItalicSlope;
	Params.bRequireNormalAndTangent = RenderCanvas ? RenderCanvas->GetActualRequireNormalAndTangent() : false;
	Params.BaseColor = Text->GetFinalColor();
	Params.FillSegments = &Text->GetFillSegments();
	Params.FillProgress = Text->GetFillProgress();
	Params.GlowBoost = Text->GetGlowBoost();
	if (Style.bMultiChannelField)
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
		Params.bMultiChannelField = true;
		Params.bSeparateEffectLayer = TextStyle.HasEffects();
		Params.BoldDilateEm = Style.BoldDilateEm;
		Params.FaceReachEm = TextStyle.GetFaceReachEm(ExtraDilateEm);
		Params.EffectReachEm = Params.bSeparateEffectLayer ? TextStyle.GetEffectReachEm(ExtraDilateEm, MaxGlowBoost) : 0.0f;
		Params.EmTexels = Style.EmTexels;
		Params.FieldSpreadTexels = Style.FieldSpreadTexels;
		Params.QuadMarginTexels = Style.QuadMarginTexels;
		Params.TexelToUV = Style.TexelToUV;
	}
	return Params;
}


#if WITH_EDITORONLY_DATA
TWeakObjectPtr<UDreamUIFontData_BaseObject> UDreamText::CurrentUsingFontData = nullptr;
#endif
UDreamText::UDreamText(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
#if WITH_EDITOR
	if (UDreamText::CurrentUsingFontData.IsValid())
	{
		Font = CurrentUsingFontData.Get();
	}
	else
#endif
	{
		Font = UDreamUIFontData_BaseObject::GetDefaultFont();
	}
	UIGeometry->bIsFont = true;
}

void UDreamText::ApplyFontTextureChange()
{
	if (IsValid(Font))
	{
		MarkVerticesDirty(true, true, true, true);
		MarkTextureDirty();
		UIGeometry->Texture = GetTextureToCreateGeometry();
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
		CacheTextGeometryData.MarkDirty();
		MarkVertexPositionDirty();
	}
}

void UDreamText::ApplyFontEmojiChange()
{
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
	UnregisterFont();
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

void UDreamText::OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	if (InTriangleChanged || InVertexPositionChanged || InVertexUVChanged || InVertexColorChanged)
	{
		UpdateCacheTextGeometry();
		if (!IsValid(Font))return;
		auto Widget = GetWidget();
		auto RenderCanvas = Widget->GetRenderCanvas();
		if (!RenderCanvas)return;
		// The geometry was cleared before this call; painting from the cached display list is what
		// fills it again, whether or not the layout itself had to run.
		CacheTextGeometryData.Paint(InGeo, MakePaintParams(this));
		if (CacheTextGeometryData.GetLayoutInput().bPixelPerfect)
		{
			FDreamUIGeometry::AdjustPixelPerfectPos_For_UIText(InGeo.OriginVertices, CacheTextGeometryData.GetCharPropertyArray(), RenderCanvas, this);
		}
	}
}

uint8 UDreamText::GetFontMark_WidgetPropertyDataForMaterial()
{
	return static_cast<uint8>(this->Font->GetFontTextureMark());
}

void UDreamText::FillWidgetPropertyDataForMaterial_Extra(UDreamUIDataAsTexture* DataAsTexture)
{
	const int32 StartPosition = GetWidgetPropertyDataStartPosition();
	if (StartPosition == INDEX_NONE || !DataAsTexture)return;
	TArray<uint8> Packed;
	TextStyle.Pack(Packed);
	DataAsTexture->UpdateBlock(FDreamTextStyle::PackedPixelStart, StartPosition, MoveTemp(Packed), FDreamTextStyle::PackedPixelCount);
}

void UDreamText::OnCultureChanged_Implementation()
{
	auto originText = Text;
	Text = FText::GetEmpty();//just make it work, because SetText will compare text value
	SetText(originText);
}


#if WITH_EDITOR
void UDreamText::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
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
}
void UDreamText::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	auto MemberProperty = PropertyChangedEvent.MemberProperty;
	auto Property = PropertyChangedEvent.Property;
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
				UDreamText::CurrentUsingFontData = Font;
				ClearEmojiObject();
				ConditionalUpdateCacheTextGeometry();
			}
			else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, bUseKerning))
			{
				MarkVertexPositionDirty();
				CacheTextGeometryData.MarkDirty();
			}
			else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDreamText, TextStyle))
			{
				bWidgetPropertyDataFontMarkDirty = true;
				if (auto Widget = GetWidget())
				{
					Widget->MarkCanvasUpdate(false);
				}
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
			UDreamWidget::MarkLayoutForRebuild(GetWidget());
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif
void UDreamText::RegisterOnRichTextImageDataChange()
{
	RichTextImageDataChangedDelegateHandle = RichTextImageData->OnDataChange.AddWeakLambda(this, [this] {
		this->MarkVerticesDirty(true, true, true, false);
		});
}
void UDreamText::UnregisterOnRichTextImageDataChange()
{
	RichTextImageData->OnDataChange.Remove(RichTextImageDataChangedDelegateHandle);
	RichTextImageDataChangedDelegateHandle.Reset();
}

void UDreamText::RegisterOnRichTextCustomStyleDataChange()
{
	RichTextCustomStyleDataChangedDelegateHandle = RichTextCustomStyleData->OnDataChange.AddWeakLambda(this, [this] {
		this->MarkVerticesDirty(true, true, true, false);
		});
}
void UDreamText::UnregisterOnRichTextCustomStyleDataChange()
{
	RichTextCustomStyleData->OnDataChange.Remove(RichTextCustomStyleDataChangedDelegateHandle);
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
		//remove from old
		if (IsValid(Font))
		{
			UnregisterFont();
		}
		Font = Value;

		MarkTextureDirty();
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

void UDreamText::SetBestFit(bool Value)
{
	if (bBestFit != Value)
	{
		bBestFit = Value;
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
		if (OverflowType == EDreamUITextOverflowType::Truncate
			|| Value == EDreamUITextOverflowType::Truncate
			)
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
		RichTextCustomStyleData = Value;
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamText::SetOverrideMaterial(UMaterialInterface* Value)
{
	if (OverrideMaterial != Value)
	{
		OverrideMaterial = Value;
		MarkMaterialDirty();
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
	if (!bHasAddToFont)
	{
		bHasAddToFont = true;
		Font->AddUIText(this);
		EmojiDataChangedDelegateHandle = Font->OnEmojiDataChanged.AddWeakLambda(this, [=, this]()
		{
			ClearEmojiObject();
			MarkVerticesDirty(true, true, true, true);
		});
		GlyphsReadyDelegateHandle = Font->OnGlyphsReady.AddWeakLambda(this, [this]()
		{
			// Only texts that were laid out with missing quads care; the advances were already right,
			// so this is a repaint with the quads filled in, not a size change.
			if (bWaitingForGlyphs)
			{
				bWaitingForGlyphs = false;
				CacheTextGeometryData.MarkDirty();
				MarkVerticesDirty(true, true, true, true);
			}
		});
	}
}

void UDreamText::UnregisterFont()
{
	if (bHasAddToFont)
	{
		bHasAddToFont = false;
		Font->RemoveUIText(this);
		Font->OnEmojiDataChanged.Remove(EmojiDataChangedDelegateHandle);
		EmojiDataChangedDelegateHandle.Reset();
		Font->OnGlyphsReady.Remove(GlyphsReadyDelegateHandle);
		GlyphsReadyDelegateHandle.Reset();
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

void UDreamText::MarkVerticesDirty(bool InTriangleDirty, bool InVertexPositionDirty, bool InVertexUVDirty, bool InVertexColorDirty)
{
	// Colour is a paint input, not a layout input: the painter reads it off the text every time.
	if (InTriangleDirty || InVertexPositionDirty || InVertexUVDirty)
	{
		CacheTextGeometryData.MarkDirty();
	}
	Super::MarkVerticesDirty(InTriangleDirty, InVertexPositionDirty, InVertexUVDirty, InVertexColorDirty);
}
void UDreamText::MarkTextureDirty()
{
	CacheTextGeometryData.MarkDirty();
	Super::MarkTextureDirty();
}

void UDreamText::MarkAllDirty()
{
	CacheTextGeometryData.MarkDirty();
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

float UDreamText::GetPreferredWidth() const
{
	UpdateCacheTextGeometry();
	// textPreferredSize measures the glyphs, which were laid out inside the inset box, so the
	// padding has to be added back or a content-sized parent would squeeze it straight out again.
	return CacheTextGeometryData.GetPreferredSize().X + Margin.Left + Margin.Right;
}

float UDreamText::GetPreferredHeight() const
{
	UpdateCacheTextGeometry();
	return CacheTextGeometryData.GetPreferredSize().Y + Margin.Top + Margin.Bottom;
}


bool UDreamText::MoveCaret(int32 moveType, int32& inOutCaretPositionIndex, int32& inOutCaretPositionLineIndex, FVector2f& inOutCaretPosition)
{
	auto originCaretPositionIndex = inOutCaretPositionIndex;
	auto originCaretPositionLineIndex = inOutCaretPositionLineIndex;

	UpdateCacheTextGeometry();
	auto& cacheLinePropertyArray = CacheTextGeometryData.GetLines();
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
		auto& lineProperty = cacheLinePropertyArray[inOutCaretPositionLineIndex];
		float minDistance = MAX_FLT;
		int accumulatedCaretIndex = 0;
		for (int lineIndex = 0; lineIndex < inOutCaretPositionLineIndex; lineIndex++)
		{
			accumulatedCaretIndex += cacheLinePropertyArray[lineIndex].CaretPropertyList.Num();
		}
		auto originCaretPosition = inOutCaretPosition;
		for (int caretIndex = 0; caretIndex < lineProperty.CaretPropertyList.Num(); caretIndex++)
		{
			auto& caretProperty = lineProperty.CaretPropertyList[caretIndex];
			auto distance = FMath::Abs(originCaretPosition.X - caretProperty.CaretPosition.X);
			if (distance < minDistance)
			{
				minDistance = distance;
				inOutCaretPositionIndex = accumulatedCaretIndex;
				inOutCaretPosition = caretProperty.CaretPosition;
			}
			else//found min distance at prev
			{
				break;
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
	int accumulatedCaretIndex = 0;
	for (int lineIndex = 0; lineIndex < cacheLinePropertyArray.Num(); lineIndex++)
	{
		auto& lineProperty = cacheLinePropertyArray[lineIndex];
		for (int caretIndex = 0; caretIndex < lineProperty.CaretPropertyList.Num(); caretIndex++)
		{
			if (accumulatedCaretIndex == inCaretPositionIndex)//find caret
			{
				return lineProperty.CaretPropertyList[caretIndex].CharIndex;
			}
			accumulatedCaretIndex++;
		}
	}
	//not found caret, use last one
	auto& lastLineProperty = cacheLinePropertyArray[cacheLinePropertyArray.Num() - 1];
	return lastLineProperty.CaretPropertyList[lastLineProperty.CaretPropertyList.Num() - 1].CharIndex;
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
	inOutCaretPosition = lineItem.CaretPropertyList[nearestIndex].CaretPosition;
}
//find caret by position, caret is on left side of char
void UDreamText::FindCaretByWorldPosition(FVector inWorldPosition, FVector2f& outCaretPosition, int32& outCaretPositionLineIndex, int32& outCaretPositionIndex)
{
	if (Text.ToString().Len() == 0)//no text
	{
		outCaretPositionIndex = 0;
		int tempVisibleCharStartIndex = 0;
		FindCaretByIndex(outCaretPositionIndex, outCaretPosition, outCaretPositionLineIndex, tempVisibleCharStartIndex);
	}
	else
	{
		UpdateCacheTextGeometry();
		auto& cacheLinePropertyArray = CacheTextGeometryData.GetLines();

		auto localPosition = GetWidget()->GetWorldTransform().InverseTransformPosition(inWorldPosition);
		auto localPosition2D = FVector2f(localPosition.Y, localPosition.Z);

		float nearestDistance = MAX_FLT;
		int accumulatedCaretIndex = 0;
		//find the nearest line, only need to compare Y
		int foundLineIndex = -1;
		for (int lineIndex = 0; lineIndex < cacheLinePropertyArray.Num(); lineIndex++)
		{
			auto& lineItem = cacheLinePropertyArray[lineIndex];
			float distance = FMath::Abs(lineItem.CaretPropertyList[0].CaretPosition.Y - localPosition2D.Y);
			if (distance <= nearestDistance)
			{
				nearestDistance = distance;
				outCaretPositionLineIndex = lineIndex;
				accumulatedCaretIndex += lineItem.CaretPropertyList.Num();
			}
			else
			{
				foundLineIndex = lineIndex - 1;
				break;
			}
		}
		if (foundLineIndex == -1)
		{
			foundLineIndex = cacheLinePropertyArray.Num() - 1;
		}
		accumulatedCaretIndex -= cacheLinePropertyArray[foundLineIndex].CaretPropertyList.Num();//remove prev line's caret count, because we need to add it when compare X pos
		//then find nearest char, only need to compare X
		nearestDistance = MAX_FLT;
		auto& nearestLine = cacheLinePropertyArray[outCaretPositionLineIndex];
		for (int caretIndex = 0; caretIndex < nearestLine.CaretPropertyList.Num(); caretIndex++)
		{
			auto& caretItem = nearestLine.CaretPropertyList[caretIndex];
			float distance = FMath::Abs(caretItem.CaretPosition.X - localPosition2D.X);
			if (distance <= nearestDistance)
			{
				nearestDistance = distance;
				outCaretPositionIndex = accumulatedCaretIndex + caretIndex;
				outCaretPosition = caretItem.CaretPosition;
			}
			else
			{
				break;
			}
		}
	}
}

int UDreamText::GetCaretIndexByCharIndex(int32 inCharIndex)
{
	UpdateCacheTextGeometry();
	int accumulatedCaretIndex = 0;
	auto& cacheLinePropertyArray = CacheTextGeometryData.GetLines();
	for (int lineIndex = 0; lineIndex < cacheLinePropertyArray.Num(); lineIndex++)
	{
		auto& lineProperty = cacheLinePropertyArray[lineIndex];
		for (int caretIndex = 0; caretIndex < lineProperty.CaretPropertyList.Num(); caretIndex++)
		{
			if (lineProperty.CaretPropertyList[caretIndex].CharIndex == inCharIndex)//find char
			{
				return accumulatedCaretIndex;
			}
			else
			{
				accumulatedCaretIndex++;
			}
		}
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

void UDreamText::GetSelectionProperty(int32 InSelectionStartCaretIndex, int32 InSelectionEndCaretIndex, TArray<FDreamUITextSelectionProperty>& OutSelectionProeprtyArray)
{
	OutSelectionProeprtyArray.Reset();
	UpdateCacheTextGeometry();
	auto& cacheTextPropertyArray = CacheTextGeometryData.GetLines();
	//start
	FVector2f startCaretPosition;
	int32 startCaretPositionLineIndex;
	int visibleCharStartIndex = 0;
	FindCaretByIndex(InSelectionStartCaretIndex, startCaretPosition, startCaretPositionLineIndex, visibleCharStartIndex);
	//end
	FVector2f endCaretPosition;
	int32 endCaretPositionLineIndex;
	FindCaretByIndex(InSelectionEndCaretIndex, endCaretPosition, endCaretPositionLineIndex, visibleCharStartIndex);
	//if select from down to up, then convert it from up to down
	if (startCaretPositionLineIndex > endCaretPositionLineIndex)
	{
		auto tempInt = endCaretPositionLineIndex;
		endCaretPositionLineIndex = startCaretPositionLineIndex;
		startCaretPositionLineIndex = tempInt;
		auto tempV2 = endCaretPosition;
		endCaretPosition = startCaretPosition;
		startCaretPosition = tempV2;
	}
	
	if (startCaretPositionLineIndex == endCaretPositionLineIndex)//same line
	{
		FDreamUITextSelectionProperty selectionProperty;
		selectionProperty.Pos = startCaretPosition;
		selectionProperty.Size = endCaretPosition.X - startCaretPosition.X;
		OutSelectionProeprtyArray.Add(selectionProperty);
	}
	else//different line
	{
		//first line
		FDreamUITextSelectionProperty selectionProperty;
		selectionProperty.Pos = startCaretPosition;
		auto& firstLineCharPropertyList = cacheTextPropertyArray[startCaretPositionLineIndex].CaretPropertyList;
		auto& firstLineLastCharProperty = firstLineCharPropertyList[firstLineCharPropertyList.Num() - 1];
		selectionProperty.Size = FMath::RoundToInt(firstLineLastCharProperty.CaretPosition.X - startCaretPosition.X);
		//selectionProperty.Size = (1.0f - this->GetPivot().X) * this->GetWidth() - startCaretPosition.X;
		OutSelectionProeprtyArray.Add(selectionProperty);
		//middle line, use this->GetWidth() as size
		int middleLineCount = endCaretPositionLineIndex - startCaretPositionLineIndex - 1;
		for (int i = 0; i < middleLineCount; i++)
		{
			auto& charPropertyList = cacheTextPropertyArray[startCaretPositionLineIndex + i + 1].CaretPropertyList;
			auto& firstPosition = charPropertyList[0].CaretPosition;
			auto& lasPosition = charPropertyList[charPropertyList.Num() - 1].CaretPosition;
			selectionProperty.Pos = firstPosition;
			selectionProperty.Size = FMath::RoundToInt(lasPosition.X - firstPosition.X);
			OutSelectionProeprtyArray.Add(selectionProperty);
		}
		//end line
		auto& firstPosition = cacheTextPropertyArray[endCaretPositionLineIndex].CaretPropertyList[0].CaretPosition;
		selectionProperty.Pos = firstPosition;
		selectionProperty.Size = FMath::RoundToInt(endCaretPosition.X - firstPosition.X);
		OutSelectionProeprtyArray.Add(selectionProperty);
	}
}

#undef LOCTEXT_NAMESPACE


