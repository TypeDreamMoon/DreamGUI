// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Math/Color.h"
#include "Templates/Function.h"
#include "UObject/StrongObjectPtr.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUISettings.h"

class FAutomationTestBase;
class UDreamRectBlock;
class UDreamText;
class UDreamUIFontData_FreeTypeRender;
class UTexture;
class UWorld;

/*
 * The stage the picture tests draw on: a RenderTarget root canvas in the editor's world, a scene put on it, and the
 * latent steps that let the scene settle -- drawn, every glyph in its atlas, then still -- before its picture is read
 * back and held to a golden image.
 *
 * It is the editor's world for the reason DreamRenderTargetPixelAutomationTests.cpp gives: it is the one world a
 * viewport draws. Every picture is written to Saved/DreamGUITests/Captures whatever happens, so that there is always a
 * picture to look at; FDreamPixelProbe::ExpectMatchesGolden says how a golden image comes to be.
 *
 * The gallery, the .dui scenes and the text parity tests all draw here, so a scene looks the same whichever of them
 * put it up.
 */
namespace DreamGalleryStage
{
	static constexpr int32 Extent = 256;
	/** Opaque and not black, so that a draw that came out black and one that never happened look different. */
	static const FColor Backdrop = FColor(28, 30, 38, 255);
	/** Per channel, and the share of the pixels that may be past it: see FDreamPixelProbe::ExpectMatchesGolden. */
	static constexpr uint8 GoldenTolerance = 8;
	static constexpr double GoldenAllowedFraction = 0.002;
	/** How long a scene may take to show at all: a material's shaders compile the first time anything draws with them. */
	static constexpr double DrawnTimeoutSeconds = 180.0;
	/** Consecutive frames a picture must stay the same before it is believed. */
	static constexpr int32 StableFrames = 3;
	static constexpr double StableTimeoutSeconds = 30.0;
	/** How long the text of a scene may take to have every glyph it asked for in its font's atlas, and to stop changing after. */
	static constexpr double SettleTimeoutSeconds = 90.0;

	/** What a picture check does when the golden image it is held to does not exist yet. */
	enum class EMissingGolden : uint8
	{
		/** Pass with a warning that names the file: the gallery's long-standing behaviour. */
		Warn,
		/** Fail, naming the file and the switch that writes it, once the picture has been saved where it can be looked at. */
		Fail,
	};

	/** A RenderTarget root canvas in the editor's world, Extent pixels square unless asked otherwise, and the scene a test puts on it. */
	class FGalleryStage
	{
	public:
		explicit FGalleryStage(FAutomationTestBase& InTest, FIntPoint InSize = FIntPoint(Extent, Extent), FColor InBackdrop = Backdrop);
		~FGalleryStage();

		FGalleryStage(const FGalleryStage&) = delete;
		FGalleryStage& operator=(const FGalleryStage&) = delete;

		bool IsUsable() const;
		const FString& GetFailure() const { return Failure; }
		FAutomationTestBase& GetTest() const { return Test; }
		UDreamCanvas* GetCanvas() const { return CanvasComponent.Get(); }
		UDreamWidget* GetRoot() const { return RootWidget.Get(); }
		UWorld* GetWorld() const { return World; }
		/** The colour the target is cleared to: what CountDrawn counts as nothing drawn. */
		FColor GetBackdrop() const { return BackdropColour; }
		FIntPoint GetSize() const { return StageSize; }

		/** A registered widget of InSize under InParent (the root by default), its centre InPosition from the parent's, +Y up. */
		UDreamWidget* AddWidget(const TCHAR* InName, FVector2D InSize, FVector2D InPosition, UDreamWidget* InParent = nullptr);
		/** A block showing InTexture (plain white when none) tinted InColour. */
		UDreamWidget* AddBlock(const TCHAR* InName, FVector2D InSize, FVector2D InPosition, FColor InColour, UTexture* InTexture = nullptr, UDreamWidget* InParent = nullptr);
		UDreamRectBlock* AddRectBlock(const TCHAR* InName, FVector2D InSize, FVector2D InPosition, FColor InBody, float InCornerRadius);
		UDreamText* AddText(const TCHAR* InName, const TCHAR* InText, float InFontSize, FVector2D InSize, FVector2D InPosition, FColor InColour);

		/** An InExtent-square texture, filtered nearest so its pattern keeps hard edges, held until the stage goes. */
		UTexture2D* MakeTexture(int32 InExtent, TFunctionRef<FColor(int32 X, int32 Y)> InColourAt);
		UTexture2D* MakeChecker(int32 InExtent, int32 InCell, FColor InA, FColor InB);
		/** InTop along the first row, InBottom along the last, in even steps between. */
		UTexture2D* MakeGradient(int32 InExtent, FColor InTop, FColor InBottom);

		/** The built-in shader or the material path for plain widgets; put back when the stage is torn down. */
		void UseBuiltInShader(bool bInUse);
		/** Multisampling for every DreamUI renderer, 1 for none; put back when the stage is torn down. */
		void UseMultisampling(uint8 InSamples);

		/** Held until the stage is torn down: a font, a built tree, anything the scene must not lose to a collection meanwhile. */
		void KeepAlive(UObject* InObject);

		/** Every rasterizing font a text under the root draws with. */
		void CollectRasterFonts(TArray<UDreamUIFontData_FreeTypeRender*>& OutFonts) const;
		/**
		 * Glyphs the fonts of the stage's texts have handed out as pending and not yet put in their atlases. With
		 * bInFinishThem, each font that has some is first made to wait for its worker, so that the count is what is
		 * still missing after that: the next layout then has every glyph it asked for.
		 */
		int32 CountPendingGlyphs(bool bInFinishThem) const;

		void RequestRedraw() const;
		bool ReadBack(TArray<FColor>& OutPixels, FIntPoint& OutSize) const;

		/** Idempotent; the destructor calls it too. */
		void TearDown();

	private:
		FAutomationTestBase& Test;
		FString Failure;
		UWorld* World = nullptr;
		FIntPoint StageSize = FIntPoint(Extent, Extent);
		FColor BackdropColour = Backdrop;
		TStrongObjectPtr<UTextureRenderTarget2D> TargetTexture;
		TStrongObjectPtr<UDreamWidget> RootWidget;
		TStrongObjectPtr<UDreamCanvas> CanvasComponent;
		TArray<TStrongObjectPtr<UTexture2D>> Textures;
		TArray<TStrongObjectPtr<UObject>> KeptAlive;
		TOptional<bool> SavedBuiltInShader;
		TOptional<EDreamUIRendererAntiAliasingMethod> SavedAntiAliasing;
		EDreamUIRendererMSAASampleCount SavedSampleCount = EDreamUIRendererMSAASampleCount::One;
		bool bTornDown = false;
	};

	using FStageRef = TSharedRef<FGalleryStage>;

	FStageRef BeginStage(FAutomationTestBase& InTest, FIntPoint InSize = FIntPoint(Extent, Extent), FColor InBackdrop = Backdrop);

	/** Through a named local: a lambda's capture list carries commas, and the macro would cut its argument at the first. */
	void EnqueueStep(TFunction<bool()> InStep);
	void EnqueueDo(TFunction<void()> InAction);
	void EnqueueFrames(const FStageRef& InStage, int32 InFrames);

	/** Pixels of the picture that are not InBackdrop. */
	int32 CountDrawn(const TArray<FColor>& InPixels, FIntPoint InSize, FColor InBackdrop = Backdrop);

	/**
	 * Frames, a redraw asked for on each, until at least InMinDrawn pixels are something other than the backdrop, or
	 * DrawnTimeoutSeconds: for the first draw with a material, whose shaders the editor compiles only once something
	 * asks for them, and for glyphs, which rasterise on worker threads. The picture's own check says which it was.
	 */
	void EnqueueFramesUntilDrawn(const FStageRef& InStage, int32 InMinDrawn);
	/** Frames until the picture is the same StableFrames times running, or StableTimeoutSeconds. */
	void EnqueueFramesUntilStable(const FStageRef& InStage);
	/**
	 * Frames until no font of the stage has a glyph pending and the picture has then been the same StableFrames times
	 * running, or SettleTimeoutSeconds. A picture can stand still for a few frames while a worker is still busy, so a
	 * frame only counts towards the run while nothing is pending; and a layout that lands its glyphs can ask for new
	 * ones (a fallback face's), so the pending count is asked again on every frame, not once.
	 */
	void EnqueueFramesUntilSettled(const FStageRef& InStage);

	/** Let the scene settle -- drawn, then still -- and hold the picture to Golden/<InName>.png. */
	void EnqueuePictureCheck(const FStageRef& InStage, const FString& InName, int32 InMinDrawn);
	/**
	 * Let the scene settle -- drawn, every glyph landed, then still -- and hold the picture to Golden/<InName>.png. With
	 * EMissingGolden::Fail a golden that does not exist yet is a failure, after the picture has been saved.
	 */
	void EnqueueSettledPictureCheck(const FStageRef& InStage, const FString& InName, int32 InMinDrawn, EMissingGolden InMissing);
	/**
	 * The picture held to Golden/<InName>.png as FDreamPixelProbe::ExpectMatchesGolden does it, with the gallery's
	 * tolerance, except that with EMissingGolden::Fail a golden that does not exist yet fails the test.
	 */
	bool CheckGolden(FAutomationTestBase& InTest, const TArray<FColor>& InPixels, FIntPoint InSize, const FString& InName, EMissingGolden InMissing);

	void EnqueueTearDown(const FStageRef& InStage);
}
