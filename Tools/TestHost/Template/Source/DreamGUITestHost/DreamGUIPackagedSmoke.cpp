// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamGUIPackagedSmoke.h"

#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/DreamUserWidget.h"
#include "Core/Text/DreamFontFaceResolver.h"
#include "Core/Text/DreamTextDisplayList.h"
#include "Core/Text/DreamTextLayout.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "DreamUIBPLibrary.h"
#include "DreamUICaptureLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProperties.h"
#include "HAL/PlatformTime.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/StringOutputDevice.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

/*
 * THE PACKAGED TEXT SMOKE PROBE.
 *
 * What a packaged game does with DreamGUI's text, written down field by field so it can be held to what the same build
 * does on uncooked content (-game with the editor binary): which face each code point resolves to, where every glyph of
 * every text lands, what the small text drew from, whether the emoji came out in colour, where the safe zone puts its
 * child, and what the fonts and canvases hold in memory. Off unless the game is started with -DreamGUITextSmoke=<dir>;
 * read with FParse rather than through a console variable so that a Shipping build, which has no console, takes it too.
 *
 * The run, frame by frame: wait for the game world's player and viewport, put the smoke screen on the viewport, time
 * every frame from there (the first frame that paints the texts is where a cold glyph atlas shows), wait until every
 * glyph has landed and the small text has settled, hold that for SettledFramesNeeded frames, then take the picture,
 * write TextSmoke.json and TextSmoke.png and ask the game to exit. The picture comes from the engine's screenshot request:
 * a game's viewport is drawn straight into its window, whose back buffer is its target only while it draws a frame, so it
 * cannot be read back between frames (UDreamUICaptureLibrary::ReadViewportPixels returns false there).
 */
namespace DreamGUIPackagedSmokeLocal
{
	/** The widget class the asset script builds from DUI/TextSmoke.dui (Tools/TestHost/make_text_smoke_assets.py). */
	const TCHAR* const ScreenClassPath = TEXT("/Game/DreamGUISmoke/WBP_TextSmoke.WBP_TextSmoke_C");
	/** Frames every condition of IsSettled has to hold for: a coverage glyph that lands repaints its text a frame later. */
	constexpr int32 SettledFramesNeeded = 10;
	/** Frames from the player being ready to placing the screen, so the viewport has its size. */
	constexpr int32 WarmFrames = 3;
	constexpr double WorldTimeoutSeconds = 120.0;
	constexpr double SettleTimeoutSeconds = 60.0;
	/** Ticks the screenshot request may take before the run is written without a picture. */
	constexpr int32 CaptureTicks = 30;
	/** Frames after placement whose times are written out. */
	constexpr int32 FramesTimed = 60;
	/** Items written per text: the screen's texts have far fewer, so this only guards the file's size. */
	constexpr int32 MaxItemsPerText = 400;
	/** A pixel this colourful (largest channel less smallest) counts as saturated colour, which no grey or white glyph has. */
	constexpr int32 SaturatedChroma = 96;
	/** The code points every font of the screen is asked about: Latin, a Han ideograph, a kana, an emoji. */
	const uint32 ProbeCodepoints[] = { 0x41, 0x6F22, 0x3042, 0x1F600 };

	struct FTextEntry
	{
		FString Name;
		TWeakObjectPtr<UDreamWidget> Widget;
		TWeakObjectPtr<UDreamText> Text;
	};

	const TCHAR* GateName(EDreamTextSmallTextGate InGate)
	{
		switch (InGate)
		{
		case EDreamTextSmallTextGate::NotPainted: return TEXT("NotPainted");
		case EDreamTextSmallTextGate::Coverage: return TEXT("Coverage");
		case EDreamTextSmallTextGate::Off: return TEXT("Off");
		case EDreamTextSmallTextGate::Font: return TEXT("Font");
		case EDreamTextSmallTextGate::OverrideMaterial: return TEXT("OverrideMaterial");
		case EDreamTextSmallTextGate::Material: return TEXT("Material");
		case EDreamTextSmallTextGate::Style: return TEXT("Style");
		case EDreamTextSmallTextGate::Modifier: return TEXT("Modifier");
		case EDreamTextSmallTextGate::Snapping: return TEXT("Snapping");
		case EDreamTextSmallTextGate::NoCanvas: return TEXT("NoCanvas");
		case EDreamTextSmallTextGate::WorldSpace: return TEXT("WorldSpace");
		case EDreamTextSmallTextGate::RenderScale: return TEXT("RenderScale");
		case EDreamTextSmallTextGate::RenderLayer: return TEXT("RenderLayer");
		case EDreamTextSmallTextGate::Transform: return TEXT("Transform");
		case EDreamTextSmallTextGate::Large: return TEXT("Large");
		case EDreamTextSmallTextGate::Settling: return TEXT("Settling");
		case EDreamTextSmallTextGate::StyleFace: return TEXT("StyleFace");
		case EDreamTextSmallTextGate::RenderLayerMoving: return TEXT("RenderLayerMoving");
		}
		return TEXT("Unknown");
	}

	const TCHAR* ItemKindName(EDreamTextItemKind InKind)
	{
		switch (InKind)
		{
		case EDreamTextItemKind::Glyph: return TEXT("Glyph");
		case EDreamTextItemKind::Space: return TEXT("Space");
		case EDreamTextItemKind::Image: return TEXT("Image");
		case EDreamTextItemKind::Emoji: return TEXT("Emoji");
		}
		return TEXT("Unknown");
	}

	/** A value rounded to a ten-thousandth, which is finer than any difference that matters and keeps the file readable. */
	double Round4(double InValue)
	{
		return FMath::RoundToDouble(InValue * 10000.0) / 10000.0;
	}

	TSharedRef<FJsonObject> BoxToJson(const FDreamTextBox& InBox)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("left"), Round4(InBox.Left));
		Object->SetNumberField(TEXT("right"), Round4(InBox.Right));
		Object->SetNumberField(TEXT("bottom"), Round4(InBox.Bottom));
		Object->SetNumberField(TEXT("top"), Round4(InBox.Top));
		return Object;
	}

	TSharedRef<FJsonObject> RectToJson(const FBox2D& InRect)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("left"), Round4(InRect.Min.X));
		Object->SetNumberField(TEXT("top"), Round4(InRect.Min.Y));
		Object->SetNumberField(TEXT("right"), Round4(InRect.Max.X));
		Object->SetNumberField(TEXT("bottom"), Round4(InRect.Max.Y));
		return Object;
	}

	TSharedRef<FJsonObject> MarginToJson(const FMargin& InMargin)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("left"), Round4(InMargin.Left));
		Object->SetNumberField(TEXT("top"), Round4(InMargin.Top));
		Object->SetNumberField(TEXT("right"), Round4(InMargin.Right));
		Object->SetNumberField(TEXT("bottom"), Round4(InMargin.Bottom));
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> StringsToJson(const TArray<FString>& InStrings)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& String : InStrings)
		{
			Values.Add(MakeShared<FJsonValueString>(String));
		}
		return Values;
	}

	/**
	 * Where a point of a widget's local space lands on its root canvas's viewport, in pixels from the top left: the
	 * canvas's own view projection, as the driver's projection computes a widget's pixel (a widget's rect lies on its
	 * local X = 0 plane, 2D X along local Y and 2D Y along local Z). Screen-space canvases only.
	 */
	bool LocalPointToViewportPixel(const UDreamWidget* InWidget, const FVector2D& InLocalPoint, FVector2D& OutPixel)
	{
		const UDreamCanvas* RootCanvas = IsValid(InWidget) ? InWidget->GetRootCanvas() : nullptr;
		if (!IsValid(RootCanvas))
		{
			return false;
		}
		const FIntPoint ViewportSize = RootCanvas->GetViewportSize();
		if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
		{
			return false;
		}
		const FVector WorldPoint = InWidget->GetWorldTransform().TransformPosition(FVector(0.0, InLocalPoint.X, InLocalPoint.Y));
		const FVector4 Clip = RootCanvas->GetViewProjectionMatrix().TransformFVector4(FVector4(WorldPoint, 1.0));
		if (Clip.W <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		OutPixel = FVector2D((Clip.X / Clip.W * 0.5 + 0.5) * ViewportSize.X, (1.0 - (Clip.Y / Clip.W * 0.5 + 0.5)) * ViewportSize.Y);
		return true;
	}

	/** A rectangle of InWidget's local space (x right, y up) on the viewport, in pixels; empty when a corner has no pixel. */
	FBox2D LocalRectToViewportRect(const UDreamWidget* InWidget, float InLeft, float InRight, float InBottom, float InTop)
	{
		FBox2D Rect(ForceInit);
		const FVector2D Corners[4] = { FVector2D(InLeft, InBottom), FVector2D(InRight, InBottom), FVector2D(InLeft, InTop), FVector2D(InRight, InTop) };
		for (const FVector2D& Corner : Corners)
		{
			FVector2D Pixel;
			if (!LocalPointToViewportPixel(InWidget, Corner, Pixel))
			{
				return FBox2D(ForceInit);
			}
			Rect += Pixel;
		}
		return Rect;
	}

	FBox2D WidgetToViewportRect(const UDreamWidget* InWidget)
	{
		return IsValid(InWidget)
			? LocalRectToViewportRect(InWidget, InWidget->GetLocalSpaceLeft(), InWidget->GetLocalSpaceRight(), InWidget->GetLocalSpaceBottom(), InWidget->GetLocalSpaceTop())
			: FBox2D(ForceInit);
	}

	/** The layout the text was drawn from, made again from the same input: the display list the component keeps is its own. */
	void LayOutAgain(const UDreamText* InText, FDreamTextDisplayList& OutList)
	{
		const float Size = InText->GetRenderedFontSize() > 0.0f ? InText->GetRenderedFontSize() : InText->GetFontSize();
		const FDreamTextLayoutInput Input = UDreamText::MakeLayoutInput(InText, Size);
		FDreamTextLayoutEngine::Layout(Input, OutList);
	}

	class FProbe
	{
	public:
		explicit FProbe(const FString& InDirectory)
			: Directory(InDirectory)
		{
			StartSeconds = FPlatformTime::Seconds();
			TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FProbe::Tick), 0.0f);
			BeginFrameHandle = FCoreDelegates::OnBeginFrame.AddRaw(this, &FProbe::HandleBeginFrame);
			EndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FProbe::HandleEndFrame);
		}

		~FProbe()
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			FCoreDelegates::OnBeginFrame.Remove(BeginFrameHandle);
			FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
			UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
		}

	private:
		enum class EStage : uint8
		{
			WaitForWorld,
			Warm,
			Placed,
			Capture,
			Done,
		};

		FString Directory;
		EStage Stage = EStage::WaitForWorld;
		double StartSeconds = 0.0;
		double PlacedSeconds = 0.0;
		int32 WarmFramesLeft = WarmFrames;
		int32 SettledFrames = 0;
		FString WaitingFor;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UDreamWidget> Screen;
		TArray<FTextEntry> Texts;
		/** Wall time of every frame from the one the screen was placed in on, in milliseconds. */
		TArray<double> FrameMilliseconds;
		double FrameBeginSeconds = 0.0;
		/** Index into FrameMilliseconds of the first frame at whose end every text had painted. */
		int32 FirstTextFrame = INDEX_NONE;
		FTSTicker::FDelegateHandle TickerHandle;
		FDelegateHandle BeginFrameHandle;
		FDelegateHandle EndFrameHandle;
		FDelegateHandle ScreenshotHandle;
		/** Why the run stopped early, kept while the picture is taken; empty when it settled. */
		FString CaptureFailure;
		int32 CaptureTicksLeft = 0;
		bool bCaptured = false;
		TArray<FColor> CapturedPixels;
		FIntPoint CapturedSize = FIntPoint::ZeroValue;

		void HandleBeginFrame()
		{
			FrameBeginSeconds = FPlatformTime::Seconds();
		}

		void HandleEndFrame()
		{
			if (Stage != EStage::Placed || FrameBeginSeconds <= 0.0)
			{
				return;
			}
			FrameMilliseconds.Add((FPlatformTime::Seconds() - FrameBeginSeconds) * 1000.0);
			if (FirstTextFrame == INDEX_NONE && AreAllTextsPainted())
			{
				FirstTextFrame = FrameMilliseconds.Num() - 1;
			}
		}

		bool AreAllTextsPainted() const
		{
			for (const FTextEntry& Entry : Texts)
			{
				const UDreamText* Text = Entry.Text.Get();
				if (Text == nullptr || Text->GetSmallTextState().Gate == EDreamTextSmallTextGate::NotPainted)
				{
					return false;
				}
			}
			return Texts.Num() > 0;
		}

		static UWorld* FindGameWorld()
		{
			if (GEngine == nullptr)
			{
				return nullptr;
			}
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* ContextWorld = Context.World();
				if (ContextWorld != nullptr && Context.WorldType == EWorldType::Game && ContextWorld->HasBegunPlay()
					&& ContextWorld->GetFirstPlayerController() != nullptr && ContextWorld->GetGameViewport() != nullptr)
				{
					return ContextWorld;
				}
			}
			return nullptr;
		}

		void CollectTexts(UDreamWidget* InWidget)
		{
			if (!IsValid(InWidget))
			{
				return;
			}
			if (UDreamText* Text = Cast<UDreamText>(InWidget->GetVisual()))
			{
				FTextEntry& Entry = Texts.AddDefaulted_GetRef();
				Entry.Name = InWidget->GetDisplayName();
				Entry.Widget = InWidget;
				Entry.Text = Text;
			}
			for (UDreamWidget* Child : InWidget->GetChildren())
			{
				CollectTexts(Child);
			}
		}

		bool Tick(float InDeltaSeconds)
		{
			const double Now = FPlatformTime::Seconds();
			switch (Stage)
			{
			case EStage::WaitForWorld:
				if (UWorld* GameWorld = FindGameWorld())
				{
					World = GameWorld;
					Stage = EStage::Warm;
				}
				else if (Now - StartSeconds > WorldTimeoutSeconds)
				{
					Finish(FString::Printf(TEXT("no game world with a player and a viewport after %.0f seconds"), WorldTimeoutSeconds));
				}
				return true;

			case EStage::Warm:
				if (--WarmFramesLeft > 0)
				{
					return true;
				}
				Place();
				return true;

			case EStage::Placed:
				if (IsSettled(WaitingFor))
				{
					if (++SettledFrames >= SettledFramesNeeded)
					{
						BeginCapture(FString());
					}
				}
				else
				{
					SettledFrames = 0;
					if (Now - PlacedSeconds > SettleTimeoutSeconds)
					{
						BeginCapture(FString::Printf(TEXT("the screen did not settle in %.0f seconds: waiting for %s"), SettleTimeoutSeconds, *WaitingFor));
					}
				}
				return true;

			case EStage::Capture:
				if (bCaptured || --CaptureTicksLeft <= 0)
				{
					Finish(CaptureFailure);
				}
				return true;

			case EStage::Done:
			default:
				return true;
			}
		}

		/** Ask for the picture: the next frame's draw reads the viewport and hands it to HandleScreenshot. */
		void BeginCapture(const FString& InFailure)
		{
			CaptureFailure = InFailure;
			CaptureTicksLeft = CaptureTicks;
			ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddRaw(this, &FProbe::HandleScreenshot);
			FScreenshotRequest::RequestScreenshot(/*bInShowUI*/ false);
			Stage = EStage::Capture;
		}

		void HandleScreenshot(int32 InWidth, int32 InHeight, const TArray<FColor>& InColors)
		{
			if (Stage != EStage::Capture || bCaptured || InWidth <= 0 || InHeight <= 0 || InColors.Num() < InWidth * InHeight)
			{
				return;
			}
			CapturedPixels = InColors;
			CapturedSize = FIntPoint(InWidth, InHeight);
			bCaptured = true;
		}

		void Place()
		{
			UWorld* GameWorld = World.Get();
			UClass* ScreenClass = LoadClass<UDreamUserWidget>(nullptr, ScreenClassPath);
			if (GameWorld == nullptr || ScreenClass == nullptr)
			{
				Finish(GameWorld == nullptr
					? FString(TEXT("the game world went before the screen was placed"))
					: FString::Printf(TEXT("%s is not in this build: run Tools/TestHost/make_text_smoke_assets.py, and cook /Game/DreamGUISmoke"), ScreenClassPath));
				return;
			}
			UDreamWidget* Placed = UDreamUIBPLibrary::AddWidgetOfClassToViewport(GameWorld, ScreenClass, 0);
			if (Placed == nullptr)
			{
				Finish(TEXT("the smoke screen could not be added to the viewport"));
				return;
			}
			Screen = Placed;
			CollectTexts(Placed);
			PlacedSeconds = FPlatformTime::Seconds();
			Stage = EStage::Placed;
		}

		/** Every text painted, every glyph landed, the small text sharp. OutWaitingFor names the first thing that is not. */
		bool IsSettled(FString& OutWaitingFor) const
		{
			if (!Screen.IsValid() || Texts.Num() == 0)
			{
				OutWaitingFor = TEXT("the screen, which went or has no text");
				return false;
			}
			for (const FTextEntry& Entry : Texts)
			{
				const UDreamText* Text = Entry.Text.Get();
				if (Text == nullptr)
				{
					OutWaitingFor = Entry.Name + TEXT(", which went");
					return false;
				}
				const FDreamTextSmallTextState& State = Text->GetSmallTextState();
				if (State.Gate == EDreamTextSmallTextGate::NotPainted || State.Gate == EDreamTextSmallTextGate::Settling || State.bWaitingToSharpen)
				{
					OutWaitingFor = Entry.Name + TEXT(" to paint and settle");
					return false;
				}
				if (Text->GetSmallTextReport().PendingItems > 0)
				{
					OutWaitingFor = Entry.Name + TEXT("'s coverage glyphs");
					return false;
				}
				if (const UDreamUIFontData_FreeTypeRender* Font = Cast<UDreamUIFontData_FreeTypeRender>(Text->GetFont()); Font != nullptr && Font->GetPendingAsyncGlyphCount() > 0)
				{
					OutWaitingFor = Entry.Name + TEXT("'s font's glyphs");
					return false;
				}
				FDreamTextDisplayList List;
				LayOutAgain(Text, List);
				if (List.bHasPendingGlyphs)
				{
					OutWaitingFor = Entry.Name + TEXT("'s layout, which still has glyphs on the worker");
					return false;
				}
			}
			OutWaitingFor.Reset();
			return true;
		}

		TSharedRef<FJsonObject> DescribeText(const FTextEntry& InEntry, const UDreamText& InText) const
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("name"), InEntry.Name);
			Object->SetStringField(TEXT("text"), InText.GetText().ToString());
			Object->SetStringField(TEXT("font"), GetPathNameSafe(InText.GetFont()));
			Object->SetNumberField(TEXT("fontSize"), Round4(InText.GetFontSize()));
			Object->SetNumberField(TEXT("renderedFontSize"), Round4(InText.GetRenderedFontSize()));
			Object->SetStringField(TEXT("language"), InText.GetLanguage());

			const FDreamTextSmallTextState& State = InText.GetSmallTextState();
			TSharedRef<FJsonObject> SmallText = MakeShared<FJsonObject>();
			SmallText->SetStringField(TEXT("gate"), GateName(State.Gate));
			SmallText->SetNumberField(TEXT("deviceScale"), Round4(State.DeviceScale));
			SmallText->SetNumberField(TEXT("rasterScale"), Round4(State.RasterScale));
			SmallText->SetBoolField(TEXT("waitingToSharpen"), State.bWaitingToSharpen);
			SmallText->SetNumberField(TEXT("coverageItems"), InText.GetSmallTextReport().CoverageItems);
			SmallText->SetNumberField(TEXT("pendingItems"), InText.GetSmallTextReport().PendingItems);
			Object->SetObjectField(TEXT("smallText"), SmallText);

			FDreamTextDisplayList List;
			LayOutAgain(&InText, List);
			Object->SetBoolField(TEXT("hasPendingGlyphs"), List.bHasPendingGlyphs);
			Object->SetBoolField(TEXT("truncated"), List.bTruncated);
			Object->SetNumberField(TEXT("visibleChars"), List.VisibleCharCount);
			Object->SetNumberField(TEXT("preferredWidth"), Round4(List.PreferredSize.X));
			Object->SetNumberField(TEXT("preferredHeight"), Round4(List.PreferredSize.Y));
			Object->SetObjectField(TEXT("contentBox"), BoxToJson(List.ContentBox));
			Object->SetObjectField(TEXT("textBlockBox"), BoxToJson(List.TextBlockBox));

			// Per line: where it starts in the source, and how far its glyphs reach -- the pen boxes of its first and last
			// glyph, which a justified line stretches to the content box's edges.
			const int32 LineCount = List.Lines.Num();
			TArray<int32> FirstSource;
			TArray<float> InkLeft;
			TArray<float> InkRight;
			FirstSource.Init(MAX_int32, LineCount);
			InkLeft.Init(MAX_flt, LineCount);
			InkRight.Init(-MAX_flt, LineCount);
			for (const FDreamTextGlyphItem& Item : List.Items)
			{
				if (!FirstSource.IsValidIndex(Item.LineIndex))
				{
					continue;
				}
				FirstSource[Item.LineIndex] = FMath::Min(FirstSource[Item.LineIndex], Item.SourceIndex);
				if (Item.Kind == EDreamTextItemKind::Glyph && Item.bEmit)
				{
					InkLeft[Item.LineIndex] = FMath::Min(InkLeft[Item.LineIndex], Item.Pen.X + Item.DecorationOffset);
					InkRight[Item.LineIndex] = FMath::Max(InkRight[Item.LineIndex], Item.Pen.X + Item.DecorationOffset + Item.AdvanceWithSpace);
				}
			}
			TArray<TSharedPtr<FJsonValue>> Lines;
			for (int32 LineIndex = 0; LineIndex < LineCount; ++LineIndex)
			{
				TSharedRef<FJsonObject> Line = MakeShared<FJsonObject>();
				Line->SetNumberField(TEXT("index"), LineIndex);
				Line->SetNumberField(TEXT("firstSource"), FirstSource[LineIndex] == MAX_int32 ? -1 : FirstSource[LineIndex]);
				const bool bHasInk = InkLeft[LineIndex] <= InkRight[LineIndex];
				Line->SetBoolField(TEXT("hasGlyphs"), bHasInk);
				Line->SetNumberField(TEXT("inkLeft"), bHasInk ? Round4(InkLeft[LineIndex]) : 0.0);
				Line->SetNumberField(TEXT("inkRight"), bHasInk ? Round4(InkRight[LineIndex]) : 0.0);
				if (List.LineBoxes.IsValidIndex(LineIndex))
				{
					Line->SetObjectField(TEXT("box"), BoxToJson(List.LineBoxes[LineIndex]));
				}
				Lines.Add(MakeShared<FJsonValueObject>(Line));
			}
			Object->SetArrayField(TEXT("lines"), Lines);

			TArray<TSharedPtr<FJsonValue>> Items;
			for (int32 ItemIndex = 0; ItemIndex < List.Items.Num() && ItemIndex < MaxItemsPerText; ++ItemIndex)
			{
				const FDreamTextGlyphItem& Item = List.Items[ItemIndex];
				TSharedRef<FJsonObject> ItemObject = MakeShared<FJsonObject>();
				ItemObject->SetNumberField(TEXT("codepoint"), (double)Item.Codepoint);
				ItemObject->SetStringField(TEXT("kind"), ItemKindName(Item.Kind));
				ItemObject->SetNumberField(TEXT("line"), Item.LineIndex);
				ItemObject->SetNumberField(TEXT("source"), Item.SourceIndex);
				ItemObject->SetNumberField(TEXT("penX"), Round4(Item.Pen.X));
				ItemObject->SetNumberField(TEXT("penY"), Round4(Item.Pen.Y));
				ItemObject->SetNumberField(TEXT("advance"), Round4(Item.AdvanceWithSpace));
				ItemObject->SetNumberField(TEXT("glyphSize"), Round4(Item.GlyphSize));
				ItemObject->SetNumberField(TEXT("face"), Item.Glyph.FaceIndex);
				ItemObject->SetNumberField(TEXT("glyph"), (double)Item.Glyph.GlyphIndex);
				ItemObject->SetNumberField(TEXT("width"), Round4(Item.Glyph.Width));
				ItemObject->SetNumberField(TEXT("height"), Round4(Item.Glyph.Height));
				ItemObject->SetBoolField(TEXT("color"), Item.Glyph.bColor);
				ItemObject->SetBoolField(TEXT("pending"), Item.Glyph.bPending);
				ItemObject->SetBoolField(TEXT("emit"), Item.bEmit);
				Items.Add(MakeShared<FJsonValueObject>(ItemObject));
			}
			Object->SetNumberField(TEXT("itemCount"), List.Items.Num());
			Object->SetArrayField(TEXT("items"), Items);
			return Object;
		}

		static TSharedRef<FJsonObject> DescribeFont(UDreamUIFontData_FreeTypeRender& InFont)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("path"), InFont.GetPathName());
			Object->SetStringField(TEXT("class"), InFont.GetClass()->GetName());
			const int32 FaceCount = InFont.GetFaceCount();
			Object->SetNumberField(TEXT("faceCount"), FaceCount);
			TArray<TSharedPtr<FJsonValue>> Faces;
			for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
			{
				TSharedRef<FJsonObject> Face = MakeShared<FJsonObject>();
				Face->SetNumberField(TEXT("index"), FaceIndex);
				Face->SetBoolField(TEXT("color"), InFont.IsColorFace(FaceIndex));
				Faces.Add(MakeShared<FJsonValueObject>(Face));
			}
			Object->SetArrayField(TEXT("faces"), Faces);

			TArray<TSharedPtr<FJsonValue>> Fallbacks;
			for (const FDreamUIFontFallback& Fallback : InFont.GetFallbacks())
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("font"), GetPathNameSafe(Fallback.Font));
				Entry->SetStringField(TEXT("cultures"), Fallback.Cultures);
				Entry->SetNumberField(TEXT("scale"), Round4(Fallback.Scale));
				Entry->SetNumberField(TEXT("ranges"), Fallback.Ranges.Num());
				Entry->SetBoolField(TEXT("preferOverPrimary"), Fallback.bPreferOverPrimary);
				Fallbacks.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Object->SetArrayField(TEXT("fallbacks"), Fallbacks);

			// What the font itself answers for each probe code point, in the game's language: the face the resolver picks,
			// and which faces have it at all.
			TArray<TSharedPtr<FJsonValue>> Codepoints;
			for (const uint32 Codepoint : ProbeCodepoints)
			{
				TSharedRef<FJsonObject> Probe = MakeShared<FJsonObject>();
				Probe->SetNumberField(TEXT("codepoint"), (double)Codepoint);
				Probe->SetStringField(TEXT("hex"), FString::Printf(TEXT("U+%04X"), Codepoint));
				FDreamUIGlyphKey Key;
				const bool bResolved = InFont.ResolveCodepoint(Codepoint, Key);
				Probe->SetBoolField(TEXT("resolved"), bResolved);
				Probe->SetNumberField(TEXT("face"), bResolved ? Key.FaceIndex : -1);
				Probe->SetNumberField(TEXT("glyph"), bResolved ? (double)Key.GlyphIndex : -1.0);
				Probe->SetBoolField(TEXT("colorFace"), bResolved && InFont.IsColorFace(Key.FaceIndex));
				TArray<TSharedPtr<FJsonValue>> Has;
				for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
				{
					Has.Add(MakeShared<FJsonValueBoolean>(InFont.FaceHasCodepoint(FaceIndex, Codepoint)));
				}
				Probe->SetArrayField(TEXT("facesHaving"), Has);
				Codepoints.Add(MakeShared<FJsonValueObject>(Probe));
			}
			Object->SetArrayField(TEXT("codepoints"), Codepoints);
			return Object;
		}

		/** The emoji glyphs' inner parts on InPixels: how many pixels, how many of them saturated colour, and their mean chroma. */
		TSharedRef<FJsonObject> MeasureEmojiColour(const TArray<FColor>& InPixels, const FIntPoint& InSize) const
		{
			int64 Pixels = 0;
			int64 Saturated = 0;
			double ChromaSum = 0.0;
			int32 Glyphs = 0;
			TArray<TSharedPtr<FJsonValue>> Rects;
			for (const FTextEntry& Entry : Texts)
			{
				const UDreamText* Text = Entry.Text.Get();
				const UDreamWidget* Widget = Entry.Widget.Get();
				const UDreamCanvas* RootCanvas = Widget != nullptr ? Widget->GetRootCanvas() : nullptr;
				if (Text == nullptr || RootCanvas == nullptr)
				{
					continue;
				}
				// The canvas's viewport and the picture read back are the same pixels; scaled in case they ever are not.
				const FIntPoint CanvasSize = RootCanvas->GetViewportSize();
				const FVector2D Scale(CanvasSize.X > 0 ? (double)InSize.X / CanvasSize.X : 1.0, CanvasSize.Y > 0 ? (double)InSize.Y / CanvasSize.Y : 1.0);
				FDreamTextDisplayList List;
				LayOutAgain(Text, List);
				for (const FDreamTextGlyphItem& Item : List.Items)
				{
					if (!Item.Glyph.bColor || !Item.bEmit)
					{
						continue;
					}
					// The glyph's pen box, from its face's ascent to its descent; its quad when the box has no height.
					float Left = Item.Pen.X + Item.DecorationOffset;
					float Right = Left + Item.AdvanceWithSpace;
					float Bottom = Item.Pen.Y - Item.Descent;
					float Top = Item.Pen.Y + Item.Ascent;
					if (Top - Bottom <= 0.0f || Right - Left <= 0.0f)
					{
						Left = Item.Pen.X + Item.Glyph.XOffset;
						Right = Left + Item.Glyph.Width;
						Top = Item.Pen.Y + Item.Glyph.YOffset;
						Bottom = Top - Item.Glyph.Height;
					}
					// Its middle only, where an emoji is all colour and no antialiased edge or empty corner is.
					const float InsetX = (Right - Left) * 0.25f;
					const float InsetY = (Top - Bottom) * 0.25f;
					FBox2D Rect = LocalRectToViewportRect(Widget, Left + InsetX, Right - InsetX, Bottom + InsetY, Top - InsetY);
					if (!Rect.bIsValid)
					{
						continue;
					}
					Rect.Min *= Scale;
					Rect.Max *= Scale;
					++Glyphs;
					Rects.Add(MakeShared<FJsonValueObject>(RectToJson(Rect)));
					const int32 MinX = FMath::Clamp(FMath::FloorToInt32(Rect.Min.X), 0, InSize.X);
					const int32 MaxX = FMath::Clamp(FMath::CeilToInt32(Rect.Max.X), 0, InSize.X);
					const int32 MinY = FMath::Clamp(FMath::FloorToInt32(Rect.Min.Y), 0, InSize.Y);
					const int32 MaxY = FMath::Clamp(FMath::CeilToInt32(Rect.Max.Y), 0, InSize.Y);
					for (int32 Y = MinY; Y < MaxY; ++Y)
					{
						for (int32 X = MinX; X < MaxX; ++X)
						{
							const FColor& Pixel = InPixels[Y * InSize.X + X];
							const int32 Chroma = FMath::Max3((int32)Pixel.R, (int32)Pixel.G, (int32)Pixel.B) - FMath::Min3((int32)Pixel.R, (int32)Pixel.G, (int32)Pixel.B);
							++Pixels;
							Saturated += Chroma >= SaturatedChroma ? 1 : 0;
							ChromaSum += Chroma;
						}
					}
				}
			}
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetNumberField(TEXT("glyphs"), Glyphs);
			Object->SetNumberField(TEXT("pixels"), (double)Pixels);
			Object->SetNumberField(TEXT("saturatedFraction"), Pixels > 0 ? Round4((double)Saturated / (double)Pixels) : 0.0);
			Object->SetNumberField(TEXT("meanChroma"), Pixels > 0 ? Round4(ChromaSum / (double)Pixels) : 0.0);
			Object->SetArrayField(TEXT("rects"), Rects);
			return Object;
		}

		/** The safe zone's child against its frame on the viewport, and the inset the platform asks for there. */
		TSharedRef<FJsonObject> DescribeSafeZone() const
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			const UDreamWidget* Root = Screen.Get();
			const UDreamWidget* Frame = Root != nullptr ? Root->FindChildByDisplayName(TEXT("SafeFrame"), /*IncludeChildren*/ true) : nullptr;
			const UDreamWidget* Inner = Frame != nullptr ? Frame->FindChildByDisplayName(TEXT("SafeInner"), /*IncludeChildren*/ false) : nullptr;
			Object->SetBoolField(TEXT("found"), Frame != nullptr && Inner != nullptr);
			const IConsoleVariable* TitleRatio = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DebugSafeZone.TitleRatio"));
			Object->SetNumberField(TEXT("titleRatio"), TitleRatio != nullptr ? Round4(TitleRatio->GetFloat()) : -1.0);
			if (Frame == nullptr || Inner == nullptr)
			{
				return Object;
			}
			const FBox2D FrameRect = WidgetToViewportRect(Frame);
			const FBox2D InnerRect = WidgetToViewportRect(Inner);
			Object->SetObjectField(TEXT("frame"), RectToJson(FrameRect));
			Object->SetObjectField(TEXT("inner"), RectToJson(InnerRect));
			Object->SetObjectField(TEXT("inset"), MarginToJson(FMargin(
				(float)(InnerRect.Min.X - FrameRect.Min.X), (float)(InnerRect.Min.Y - FrameRect.Min.Y),
				(float)(FrameRect.Max.X - InnerRect.Max.X), (float)(FrameRect.Max.Y - InnerRect.Max.Y))));
			// What SSafeZone would inset by on this viewport, in its pixels: an editor build fits the zone to the size it is
			// handed, a cooked one answers in pixels of the primary display (FSlateApplicationBase::GetSafeZoneSize), and
			// DreamGUI's safe zone follows the same rule in each.
			const UDreamCanvas* RootCanvas = Frame->GetRootCanvas();
			const FIntPoint ViewportSize = RootCanvas != nullptr ? RootCanvas->GetViewportSize() : FIntPoint::ZeroValue;
			FMargin Platform;
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().GetSafeZoneSize(Platform, FVector2D(ViewportSize));
			}
			Object->SetObjectField(TEXT("platformInset"), MarginToJson(Platform));
			return Object;
		}

		/** Write what the run found -- InFailure says why it stopped early, empty when it did not -- and ask the game to exit. */
		void Finish(const FString& InFailure)
		{
			if (Stage == EStage::Done)
			{
				return;
			}
			Stage = EStage::Done;
			IFileManager::Get().MakeDirectory(*Directory, /*Tree*/ true);

			TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetNumberField(TEXT("version"), 1);
			Root->SetBoolField(TEXT("settled"), InFailure.IsEmpty());
			Root->SetStringField(TEXT("failure"), InFailure);

			TSharedRef<FJsonObject> Build = MakeShared<FJsonObject>();
			Build->SetStringField(TEXT("configuration"), LexToString(FApp::GetBuildConfiguration()));
			Build->SetBoolField(TEXT("cooked"), FPlatformProperties::RequiresCookedData());
			Build->SetBoolField(TEXT("withEditor"), WITH_EDITOR != 0);
			Build->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
			Build->SetStringField(TEXT("platform"), FString(ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName())));
			Root->SetObjectField(TEXT("build"), Build);

			FInternationalization& I18N = FInternationalization::Get();
			TSharedRef<FJsonObject> Culture = MakeShared<FJsonObject>();
			Culture->SetStringField(TEXT("culture"), I18N.GetCurrentCulture()->GetName());
			Culture->SetStringField(TEXT("language"), I18N.GetCurrentLanguage()->GetName());
			Culture->SetArrayField(TEXT("prioritizedZhCN"), StringsToJson(I18N.GetPrioritizedCultureNames(TEXT("zh-CN"))));
			Culture->SetArrayField(TEXT("prioritizedJa"), StringsToJson(I18N.GetPrioritizedCultureNames(TEXT("ja"))));
			// What DreamGUI matches fallbacks against: the engine's names, with what cut-down ICU data leaves out put back.
			Culture->SetArrayField(TEXT("dreamZhCN"), StringsToJson(FDreamTextLanguage::Make(TEXT("zh-CN")).PrioritizedCultureNames));
			Culture->SetArrayField(TEXT("dreamJa"), StringsToJson(FDreamTextLanguage::Make(TEXT("ja")).PrioritizedCultureNames));
			Root->SetObjectField(TEXT("culture"), Culture);

			TSharedRef<FJsonObject> Timing = MakeShared<FJsonObject>();
			Timing->SetNumberField(TEXT("secondsToPlace"), PlacedSeconds > 0.0 ? Round4(PlacedSeconds - StartSeconds) : -1.0);
			Timing->SetNumberField(TEXT("secondsToSettle"), PlacedSeconds > 0.0 ? Round4(FPlatformTime::Seconds() - PlacedSeconds) : -1.0);
			Timing->SetNumberField(TEXT("framesAfterPlacement"), FrameMilliseconds.Num());
			Timing->SetNumberField(TEXT("firstTextFrame"), FirstTextFrame);
			Timing->SetNumberField(TEXT("firstTextFrameMs"), FrameMilliseconds.IsValidIndex(FirstTextFrame) ? Round4(FrameMilliseconds[FirstTextFrame]) : -1.0);
			TArray<TSharedPtr<FJsonValue>> Frames;
			double SlowestMs = 0.0;
			for (int32 FrameIndex = 0; FrameIndex < FrameMilliseconds.Num() && FrameIndex < FramesTimed; ++FrameIndex)
			{
				Frames.Add(MakeShared<FJsonValueNumber>(Round4(FrameMilliseconds[FrameIndex])));
				SlowestMs = FMath::Max(SlowestMs, FrameMilliseconds[FrameIndex]);
			}
			Timing->SetNumberField(TEXT("slowestFrameMs"), Round4(SlowestMs));
			Timing->SetArrayField(TEXT("frameMs"), Frames);
			Root->SetObjectField(TEXT("timing"), Timing);

			UWorld* GameWorld = World.Get();
			TArray<TSharedPtr<FJsonValue>> TextValues;
			TArray<UDreamUIFontData_FreeTypeRender*> Fonts;
			for (const FTextEntry& Entry : Texts)
			{
				if (const UDreamText* Text = Entry.Text.Get())
				{
					TextValues.Add(MakeShared<FJsonValueObject>(DescribeText(Entry, *Text)));
					if (UDreamUIFontData_FreeTypeRender* Font = Cast<UDreamUIFontData_FreeTypeRender>(Text->GetFont()))
					{
						Fonts.AddUnique(Font);
					}
				}
			}
			Root->SetArrayField(TEXT("texts"), TextValues);
			TArray<TSharedPtr<FJsonValue>> FontValues;
			for (UDreamUIFontData_FreeTypeRender* Font : Fonts)
			{
				FontValues.Add(MakeShared<FJsonValueObject>(DescribeFont(*Font)));
			}
			Root->SetArrayField(TEXT("fonts"), FontValues);
			Root->SetObjectField(TEXT("safeZone"), DescribeSafeZone());

			// The picture the screenshot request took, saved and measured where the emoji are.
			UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
			const TArray<FColor>& Pixels = CapturedPixels;
			const FIntPoint PictureSize = CapturedSize;
			const bool bRead = bCaptured;
			const FString PicturePath = FPaths::Combine(Directory, TEXT("TextSmoke.png"));
			TSharedRef<FJsonObject> Picture = MakeShared<FJsonObject>();
			Picture->SetBoolField(TEXT("read"), bRead);
			Picture->SetNumberField(TEXT("width"), PictureSize.X);
			Picture->SetNumberField(TEXT("height"), PictureSize.Y);
			Picture->SetBoolField(TEXT("saved"), bRead && UDreamUICaptureLibrary::SavePixelsToPng(Pixels, PictureSize, PicturePath));
			Picture->SetStringField(TEXT("file"), FPaths::GetCleanFilename(PicturePath));
			if (bRead)
			{
				Picture->SetObjectField(TEXT("emoji"), MeasureEmojiColour(Pixels, PictureSize));
			}
			Root->SetObjectField(TEXT("picture"), Picture);

			// The memory report as the console prints it with Json, kept whole.
			if (IConsoleObject* ConsoleObject = IConsoleManager::Get().FindConsoleObject(TEXT("DreamGUI.Memory")))
			{
				if (IConsoleCommand* Command = ConsoleObject->AsCommand())
				{
					FStringOutputDevice Output;
					const TArray<FString> Args = { TEXT("Json") };
					Command->Execute(Args, GameWorld, Output);
					TSharedPtr<FJsonObject> Memory;
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Output);
					if (FJsonSerializer::Deserialize(Reader, Memory) && Memory.IsValid())
					{
						Root->SetObjectField(TEXT("memory"), Memory);
					}
					else
					{
						Root->SetStringField(TEXT("memoryError"), TEXT("DreamGUI.Memory Json printed something that is not JSON"));
					}
				}
			}
			else
			{
				Root->SetStringField(TEXT("memoryError"), TEXT("there is no DreamGUI.Memory console command"));
			}

			FString JsonText;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonText);
			FJsonSerializer::Serialize(Root, Writer);
			const FString JsonPath = FPaths::Combine(Directory, TEXT("TextSmoke.json"));
			const bool bWritten = FFileHelper::SaveStringToFile(JsonText, *JsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogTemp, Display, TEXT("DreamGUI text smoke: %s %s (%s)."), bWritten ? TEXT("wrote") : TEXT("could not write"), *JsonPath,
				InFailure.IsEmpty() ? TEXT("settled") : *InFailure);
			RequestEngineExit(TEXT("DreamGUI text smoke finished"));
		}
	};

	TUniquePtr<FProbe> ActiveProbe;
}

void DreamGUIPackagedSmoke::StartIfAsked()
{
	using namespace DreamGUIPackagedSmokeLocal;
	FString Directory;
	if (ActiveProbe.IsValid() || GIsEditor || IsRunningCommandlet() || !FParse::Value(FCommandLine::Get(), TEXT("DreamGUITextSmoke="), Directory))
	{
		return;
	}
	if (Directory.IsEmpty())
	{
		Directory = TEXT("DreamGUITextSmoke");
	}
	if (FPaths::IsRelative(Directory))
	{
		Directory = FPaths::Combine(FPaths::LaunchDir(), Directory);
	}
	ActiveProbe = MakeUnique<FProbe>(FPaths::ConvertRelativePathToFull(Directory));
}

void DreamGUIPackagedSmoke::Stop()
{
	DreamGUIPackagedSmokeLocal::ActiveProbe.Reset();
}
