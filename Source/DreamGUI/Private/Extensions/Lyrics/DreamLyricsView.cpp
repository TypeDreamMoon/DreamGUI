// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/Lyrics/DreamLyricsView.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamText.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "DreamUIBPLibrary.h"
#include "Engine/World.h"

UDreamLyricsView::UDreamLyricsView()
{
	bStartWithTickEnabled = true;
	TextStyle.FillDimAlpha = 0.35f;
	TextStyle.FillFadeWidth = 0.25f;
	// A faint glow on every line; the emphasis boost widens it on the word being sung.
	TextStyle.GlowColor = FColor(255, 255, 255, 110);
	TextStyle.GlowWidth = 0.05f;
	TextStyle.GlowPower = 2.0f;
}

void UDreamLyricsView::Awake()
{
	Super::Awake();
	bLinesDirty = true;
	bSnapNextLayout = true;
}

void UDreamLyricsView::OnDestroy()
{
	DestroyLines();
	Super::OnDestroy();
}

void UDreamLyricsView::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	if (WidthChanged || HeightChanged)
	{
		// Wrap widths changed: heights will be re-read and targets recomputed next tick.
		bSnapNextLayout = bSnapNextLayout || Lines.Num() == 0;
	}
}

#if WITH_EDITOR
void UDreamLyricsView::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	bLinesDirty = true;
	bSnapNextLayout = true;
}
#endif

void UDreamLyricsView::SetLyrics(const FDreamLyrics& InLyrics)
{
	Lyrics = InLyrics;
	bLinesDirty = true;
	bSnapNextLayout = true;
	ActiveLine = INDEX_NONE;
}

bool UDreamLyricsView::LoadTTML(const FString& Xml)
{
	FDreamLyrics Parsed;
	FString Error;
	if (!UDreamLyricsLibrary::ParseTTML(Xml, Parsed, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("[UDreamLyricsView::LoadTTML] %s"), *Error);
		return false;
	}
	SetLyrics(Parsed);
	return true;
}

bool UDreamLyricsView::LoadLRC(const FString& Text)
{
	FDreamLyrics Parsed;
	if (!UDreamLyricsLibrary::ParseLRC(Text, Parsed))
	{
		return false;
	}
	SetLyrics(Parsed);
	return true;
}

void UDreamLyricsView::SetTime(float Seconds)
{
	CurrentTime = FMath::Max(Seconds, 0.0f);
}

void UDreamLyricsView::Seek(float Seconds)
{
	SetTime(Seconds);
	bSnapNextLayout = true;
}

UDreamText* UDreamLyricsView::GetLineText(int32 LineIndex) const
{
	return Lines.IsValidIndex(LineIndex) ? Lines[LineIndex].Text.Get() : nullptr;
}

int32 UDreamLyricsView::CountElements(const FString& Text)
{
	// The text pipeline addresses characters by code point; a surrogate pair is one.
	int32 Count = 0;
	for (int32 i = 0; i < Text.Len(); i++)
	{
		if (!(Text[i] >= 0xDC00 && Text[i] <= 0xDFFF))
		{
			Count++;
		}
	}
	return Count;
}

void UDreamLyricsView::DestroyLines()
{
	for (FLineState& Line : Lines)
	{
		if (UDreamWidget* Widget = Line.Widget.Get())
		{
			Widget->DestroyWidget();
		}
		if (UDreamWidget* Widget = Line.TranslationWidget.Get())
		{
			Widget->DestroyWidget();
		}
	}
	Lines.Reset();
}

void UDreamLyricsView::ApplyTextSettings(UDreamText* Text, bool bBackground, bool bSecondary, bool bTranslation) const
{
	if (Font != nullptr)
	{
		Text->SetFont(Font);
	}
	float Size = FontSize;
	if (bBackground)Size *= BackgroundLineScale;
	if (bTranslation)Size *= TranslationScale;
	Text->SetFontSize(Size);
	Text->SetColor(TextColor);
	Text->SetOverflowType(EDreamUITextOverflowType::VerticalOverflow);
	Text->SetPhraseWrap(EDreamTextPhraseWrap::CJKDictionary);
	Text->SetParagraphHorizontalAlignment(bSecondary ? EDreamUITextParagraphHorizontalAlign::Right : EDreamUITextParagraphHorizontalAlign::Left);
	Text->SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Top);
	Text->SetTextStyle(TextStyle);
	Text->SetRichText(false);
}

void UDreamLyricsView::RebuildLines()
{
	DestroyLines();
	UDreamWidget* Container = GetWidget();
	if (Container == nullptr)
	{
		return;
	}
	Lines.Reserve(Lyrics.Lines.Num());
	for (int32 i = 0; i < Lyrics.Lines.Num(); i++)
	{
		const FDreamLyricLine& Source = Lyrics.Lines[i];
		FLineState Line;

		auto MakeLineWidget = [&](const FString& Name, bool bTranslation) -> UDreamWidget*
		{
			UDreamWidget* Widget = UDreamUIBPLibrary::ConstructWidget(this, Name, UDreamText::StaticClass());
			if (Widget == nullptr)return nullptr;
			Widget->SetParent(Container, false);
			// Stretch across the frame, hang from its top, scale about the edge the text is aligned to.
			Widget->SetAnchorMin(FVector2D(0.0, 1.0));
			Widget->SetAnchorMax(FVector2D(1.0, 1.0));
			Widget->SetPivot(FVector2D(Source.bSecondary ? 1.0 : 0.0, 1.0));
			Widget->SetSizeDelta(FVector2D(-2.0 * HorizontalPadding, FontSize));
			Widget->SetAnchoredPosition(FVector2D(0.0, 0.0));
			Widget->SetRaycastable(EDreamWidgetRaycastableType::Disabled);
			if (UDreamText* Text = Cast<UDreamText>(Widget->GetVisual()))
			{
				ApplyTextSettings(Text, Source.bBackground, Source.bSecondary, bTranslation);
			}
			Widget->SetWidgetActive(true);
			return Widget;
		};

		Line.Widget = MakeLineWidget(FString::Printf(TEXT("LyricLine_%d"), i), false);
		if (Line.Widget.IsValid())
		{
			Line.Text = Cast<UDreamText>(Line.Widget->GetVisual());
		}
		if (Line.Text.IsValid())
		{
			const FString LineText = Source.GetText();
			Line.Text->SetText(FText::FromString(LineText));
			int32 Cursor = 0;
			for (const FDreamLyricWord& Word : Source.Words)
			{
				Line.WordElementStarts.Add(Cursor);
				Cursor += CountElements(Word.Text);
			}
			Line.WordElementStarts.Add(Cursor);
		}
		if (bShowTranslation && !Source.Translation.IsEmpty())
		{
			Line.TranslationWidget = MakeLineWidget(FString::Printf(TEXT("LyricLine_%d_Translation"), i), true);
			if (Line.TranslationWidget.IsValid())
			{
				Line.TranslationText = Cast<UDreamText>(Line.TranslationWidget->GetVisual());
				if (Line.TranslationText.IsValid())
				{
					Line.TranslationText->SetText(FText::FromString(Source.Translation));
				}
			}
		}
		Line.Scale.Value = Line.Scale.Target = InactiveScale;
		Line.Alpha = InactiveAlpha;
		Lines.Add(MoveTemp(Line));
	}
	bLinesDirty = false;
	bSnapNextLayout = true;
	ActiveLine = INDEX_NONE;
}

void UDreamLyricsView::UpdateLineHeights()
{
	for (FLineState& Line : Lines)
	{
		float Height = 0.0f;
		if (UDreamText* Text = Line.Text.Get())
		{
			const float Preferred = FMath::Max(Text->GetPreferredHeight(), 0.0f);
			if (UDreamWidget* Widget = Line.Widget.Get())
			{
				if (!FMath::IsNearlyEqual(Widget->GetHeight(), Preferred, 0.5f))
				{
					Widget->SetHeight(Preferred);
				}
			}
			Height += Preferred;
		}
		if (UDreamText* Translation = Line.TranslationText.Get())
		{
			const float Preferred = FMath::Max(Translation->GetPreferredHeight(), 0.0f);
			if (UDreamWidget* Widget = Line.TranslationWidget.Get())
			{
				if (!FMath::IsNearlyEqual(Widget->GetHeight(), Preferred, 0.5f))
				{
					Widget->SetHeight(Preferred);
				}
			}
			Height += Preferred;
		}
		Line.Height = Height;
	}
}

void UDreamLyricsView::UpdateTargets(bool bSnap)
{
	UDreamWidget* Container = GetWidget();
	if (Container == nullptr || Lines.Num() == 0)
	{
		return;
	}
	const int32 Anchor = FMath::Clamp(ActiveLine, 0, Lines.Num() - 1);
	// Tops of every line when the anchor line's top sits at ActiveLineAnchor of the frame.
	TArray<float> Tops;
	Tops.SetNumUninitialized(Lines.Num());
	float Y = 0.0f;
	for (int32 i = 0; i < Lines.Num(); i++)
	{
		Tops[i] = Y;
		Y += Lines[i].Height + LineSpacing;
	}
	const float Offset = Container->GetHeight() * ActiveLineAnchor - Tops[Anchor];
	for (int32 i = 0; i < Lines.Num(); i++)
	{
		FLineState& Line = Lines[i];
		const float TargetY = Tops[i] + Offset;
		const bool bActive = i == ActiveLine;
		Line.Scale.Target = bActive ? 1.0f : InactiveScale;
		if (bSnap)
		{
			Line.bHasPendingTarget = false;
			Line.PositionY.Target = TargetY;
			Line.PositionY.Settle();
			Line.Scale.Settle();
			Line.Alpha = bActive ? 1.0f : (i < ActiveLine ? SungAlpha : InactiveAlpha);
		}
		else if (!FMath::IsNearlyEqual(Line.PositionY.Target, TargetY, 0.01f) || (Line.bHasPendingTarget && !FMath::IsNearlyEqual(Line.PendingTargetY, TargetY, 0.01f)))
		{
			// Further lines wait longer, so the list ripples away from the current line.
			const int32 Distance = FMath::Min(FMath::Abs(i - Anchor), StaggerMaxLines);
			const float Delay = StaggerSeconds * Distance;
			if (Delay <= 0.0f)
			{
				Line.bHasPendingTarget = false;
				Line.PositionY.Target = TargetY;
			}
			else
			{
				Line.PendingTargetY = TargetY;
				Line.PendingDelay = Line.bHasPendingTarget ? FMath::Min(Line.PendingDelay, Delay) : Delay;
				Line.bHasPendingTarget = true;
			}
		}
	}
}

void UDreamLyricsView::UpdateFill(int32 LineIndex)
{
	FLineState& Line = Lines[LineIndex];
	UDreamText* Text = Line.Text.Get();
	if (Text == nullptr || !Lyrics.Lines.IsValidIndex(LineIndex))
	{
		return;
	}
	const FDreamLyricLine& Source = Lyrics.Lines[LineIndex];
	const bool bInWindow = CurrentTime >= Source.StartTime && CurrentTime < Source.EndTime;
	if (bInWindow && Source.Words.Num() > 0 && Line.WordElementStarts.Num() == Source.Words.Num() + 1)
	{
		TArray<FDreamTextFillSegment> Segments;
		Segments.Reserve(Source.Words.Num());
		for (int32 w = 0; w < Source.Words.Num(); w++)
		{
			const FDreamLyricWord& Word = Source.Words[w];
			FDreamTextFillSegment Segment;
			Segment.StartCharIndex = Line.WordElementStarts[w];
			Segment.EndCharIndex = Line.WordElementStarts[w + 1] - 1;
			if (Segment.EndCharIndex < Segment.StartCharIndex)continue;
			const float Duration = Word.Duration();
			const float Progress = Duration > 0.0f ? FMath::Clamp((CurrentTime - Word.StartTime) / Duration, 0.0f, 1.0f) : (CurrentTime >= Word.StartTime ? 1.0f : 0.0f);
			Segment.Progress = Progress;
			if (Duration >= EmphasisMinDuration && Progress > 0.0f && Progress < 1.0f)
			{
				Segment.GlowBoost = EmphasisGlowBoost * FMath::Sin(Progress * PI);
			}
			Segments.Add(Segment);
		}
		Text->SetFillSegments(Segments);
		Line.bHadSegments = true;
		Line.LastFillProgress = -1.0f;
	}
	else
	{
		if (Line.bHadSegments)
		{
			Text->ClearFillSegments();
			Line.bHadSegments = false;
		}
		const float Progress = CurrentTime >= Source.EndTime ? 1.0f : 0.0f;
		if (Line.LastFillProgress != Progress)
		{
			Text->SetFillProgress(Progress);
			Line.LastFillProgress = Progress;
		}
	}
}

void UDreamLyricsView::ApplyLineTransform(FLineState& Line)
{
	const float Scale = Line.Scale.Value;
	auto Place = [&](UDreamWidget* Widget, float Top)
	{
		if (Widget == nullptr)return;
		Widget->SetAnchoredPosition(FVector2D(0.0, -Top));
		Widget->SetRelativeScale(FVector(Scale, Scale, Scale));
		if (UDreamVisual* Visual = Widget->GetVisual())
		{
			Visual->SetAlpha(Line.Alpha);
		}
	};
	Place(Line.Widget.Get(), Line.PositionY.Value);
	float TranslationTop = Line.PositionY.Value;
	if (UDreamText* Text = Line.Text.Get())
	{
		TranslationTop += FMath::Max(Text->GetPreferredHeight(), 0.0f) * Scale;
	}
	Place(Line.TranslationWidget.Get(), TranslationTop);
}

void UDreamLyricsView::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bLinesDirty)
	{
		RebuildLines();
	}
	if (Lines.Num() == 0)
	{
		return;
	}
	if (bPlaying)
	{
		CurrentTime += DeltaTime * PlaybackRate;
	}

	// Which line is current: the last non-background line that has started.
	int32 NewActive = INDEX_NONE;
	for (int32 i = 0; i < Lyrics.Lines.Num(); i++)
	{
		if (Lyrics.Lines[i].bBackground)continue;
		if (Lyrics.Lines[i].StartTime <= CurrentTime)NewActive = i;
		else break;
	}
	const bool bActiveChanged = NewActive != ActiveLine;
	ActiveLine = NewActive;

	UpdateLineHeights();
	UpdateTargets(bSnapNextLayout);
	const bool bSnapped = bSnapNextLayout;
	bSnapNextLayout = false;
	if (bActiveChanged)
	{
		OnActiveLineChanged.Broadcast(ActiveLine);
	}

	const float AlphaLerp = bSnapped ? 1.0f : FMath::Clamp(DeltaTime / FMath::Max(AlphaSmoothing, 0.01f) * 3.0f, 0.0f, 1.0f);
	for (int32 i = 0; i < Lines.Num(); i++)
	{
		FLineState& Line = Lines[i];
		if (Line.bHasPendingTarget)
		{
			Line.PendingDelay -= DeltaTime;
			if (Line.PendingDelay <= 0.0f)
			{
				Line.bHasPendingTarget = false;
				Line.PositionY.Target = Line.PendingTargetY;
			}
		}
		FDreamSpring::Step(PositionSpring, Line.PositionY, DeltaTime);
		FDreamSpring::Step(ScaleSpring, Line.Scale, DeltaTime);

		const bool bActive = i == ActiveLine;
		const float TargetAlpha = bActive ? 1.0f : (i < ActiveLine ? SungAlpha : InactiveAlpha);
		Line.Alpha = FMath::Lerp(Line.Alpha, TargetAlpha, AlphaLerp);

		// Softness by distance from the current line, written only when the bucket changes: the
		// style lives in the widget property record, so this is a few pixels, not a relayout.
		if (UDreamText* Text = Line.Text.Get())
		{
			const int32 SoftnessLines = ActiveLine == INDEX_NONE ? BlurMaxLines : FMath::Min(FMath::Abs(i - ActiveLine), BlurMaxLines);
			if (SoftnessLines != Line.LastSoftnessLines)
			{
				FDreamTextStyle Style = TextStyle;
				Style.FaceSoftness += BlurPerLine * SoftnessLines;
				Text->SetTextStyle(Style);
				if (UDreamText* Translation = Line.TranslationText.Get())
				{
					Translation->SetTextStyle(Style);
				}
				Line.LastSoftnessLines = SoftnessLines;
			}
		}
		UpdateFill(i);
		ApplyLineTransform(Line);
	}
}
