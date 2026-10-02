// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "DreamGalleryStage.h"

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PixelFormat.h"
#include "TextureResource.h"
#include "UObject/Package.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/DreamUISettings.h"
#include "Utils/DreamUIUtils.h"

#include "DreamPixelProbe.h"

namespace DreamGalleryStage
{
	FGalleryStage::FGalleryStage(FAutomationTestBase& InTest, FIntPoint InSize, FColor InBackdrop)
		: Test(InTest)
		, StageSize(InSize)
		, BackdropColour(InBackdrop)
	{
		if (GEditor == nullptr || GEditor->GetEditorWorldContext().World() == nullptr)
		{
			Failure = TEXT("there is no editor world to render");
			return;
		}
		if (GEditor->GetAllViewportClients().Num() == 0)
		{
			Failure = TEXT("the editor has no viewport, so nothing ever renders its world");
			return;
		}
		World = GEditor->GetEditorWorldContext().World();

		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->AddressX = TextureAddress::TA_Clamp;
		Target->AddressY = TextureAddress::TA_Clamp;
		Target->ClearColor = FLinearColor::Black;
		Target->InitCustomFormat(static_cast<uint32>(InSize.X), static_cast<uint32>(InSize.Y), EPixelFormat::PF_B8G8R8A8, false);
		Target->UpdateResourceImmediate(true);
		TargetTexture.Reset(Target);

		UDreamWidget* Root = NewObject<UDreamWidget>(World, NAME_None, RF_Transient);
		Root->SetDisplayName(TEXT("DreamRenderGalleryRoot"));
		Root->SetWidth(static_cast<float>(InSize.X));
		Root->SetHeight(static_cast<float>(InSize.Y));
		Root->OnRegister();
		RootWidget.Reset(Root);

		UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
		if (Canvas == nullptr)
		{
			Failure = TEXT("the root widget would not take a canvas");
			return;
		}
		CanvasComponent.Reset(Canvas);
		Canvas->SetRenderMode(EDreamRenderMode::RenderTarget);
		Canvas->SetRenderTargetClearColor(BackdropColour);
		Canvas->SetRenderTargetResolutionScale(1.0f);
		Canvas->SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget);
		Canvas->SetRenderTargetUpdateMode(EDreamCanvasRenderTargetUpdateMode::Always);
		Canvas->SetRenderTarget(Target);
	}

	FGalleryStage::~FGalleryStage()
	{
		TearDown();
	}

	bool FGalleryStage::IsUsable() const
	{
		return Failure.IsEmpty() && World != nullptr && IsValid(TargetTexture.Get()) && IsValid(RootWidget.Get()) && IsValid(CanvasComponent.Get());
	}

	UDreamWidget* FGalleryStage::AddWidget(const TCHAR* InName, FVector2D InSize, FVector2D InPosition, UDreamWidget* InParent)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Transient);
		Widget->SetDisplayName(InName);
		Widget->SetWidth(static_cast<float>(InSize.X));
		Widget->SetHeight(static_cast<float>(InSize.Y));
		Widget->OnRegister();
		Widget->TrySetParent(InParent != nullptr ? InParent : RootWidget.Get(), false);
		Widget->SetAnchoredPosition(InPosition);
		return Widget;
	}

	UDreamWidget* FGalleryStage::AddBlock(const TCHAR* InName, FVector2D InSize, FVector2D InPosition, FColor InColour, UTexture* InTexture, UDreamWidget* InParent)
	{
		UDreamWidget* Widget = AddWidget(InName, InSize, InPosition, InParent);
		if (UDreamTexture* Visual = Widget->CreateNewVisual<UDreamTexture>())
		{
			Visual->SetTexture(InTexture != nullptr ? InTexture : FDreamUIUtils::GetDefaultWhiteTexture());
			Visual->SetColor(InColour);
		}
		return Widget;
	}

	UDreamRectBlock* FGalleryStage::AddRectBlock(const TCHAR* InName, FVector2D InSize, FVector2D InPosition, FColor InBody, float InCornerRadius)
	{
		UDreamWidget* Widget = AddWidget(InName, InSize, InPosition);
		UDreamRectBlock* Block = Widget->CreateNewVisual<UDreamRectBlock>();
		if (Block != nullptr)
		{
			// In pixels: a rect block's sizes default to fractions of its own size.
			Block->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Value);
			Block->SetBorderWidthUnitMode(EDreamRectBlockUnitMode::Value);
			Block->SetInnerShadowSizeUnitMode(EDreamRectBlockUnitMode::Value);
			Block->SetInnerShadowBlurUnitMode(EDreamRectBlockUnitMode::Value);
			Block->SetOuterShadowSizeUnitMode(EDreamRectBlockUnitMode::Value);
			Block->SetOuterShadowBlurUnitMode(EDreamRectBlockUnitMode::Value);
			Block->SetOuterShadowDistanceUnitMode(EDreamRectBlockUnitMode::Value);
			Block->SetBodyColor(InBody);
			Block->SetCornerRadius(FVector4(InCornerRadius, InCornerRadius, InCornerRadius, InCornerRadius));
		}
		return Block;
	}

	UDreamText* FGalleryStage::AddText(const TCHAR* InName, const TCHAR* InText, float InFontSize, FVector2D InSize, FVector2D InPosition, FColor InColour)
	{
		UDreamWidget* Widget = AddWidget(InName, InSize, InPosition);
		UDreamText* Text = Widget->CreateNewVisual<UDreamText>();
		if (Text != nullptr)
		{
			Text->SetText(FText::FromString(InText));
			Text->SetFontSize(InFontSize);
			Text->SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
			Text->SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
			// One line each: a line that wrapped would run into the next one's.
			Text->SetOverflowType(EDreamUITextOverflowType::HorizontalOverflow);
			Text->SetColor(InColour);
		}
		return Text;
	}

	UTexture2D* FGalleryStage::MakeTexture(int32 InExtent, TFunctionRef<FColor(int32 X, int32 Y)> InColourAt)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(InExtent, InExtent, PF_B8G8R8A8);
		if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}
		Texture->Filter = TF_Nearest;
		Texture->SRGB = true;
		Texture->LODGroup = TEXTUREGROUP_UI;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Y = 0; Y < InExtent; ++Y)
		{
			for (int32 X = 0; X < InExtent; ++X)
			{
				Pixels[Y * InExtent + X] = InColourAt(X, Y);
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		Textures.Emplace(Texture);
		return Texture;
	}

	UTexture2D* FGalleryStage::MakeChecker(int32 InExtent, int32 InCell, FColor InA, FColor InB)
	{
		return MakeTexture(InExtent, [InCell, InA, InB](int32 X, int32 Y) { return ((X / InCell) + (Y / InCell)) % 2 == 0 ? InA : InB; });
	}

	UTexture2D* FGalleryStage::MakeGradient(int32 InExtent, FColor InTop, FColor InBottom)
	{
		return MakeTexture(InExtent, [InExtent, InTop, InBottom](int32 X, int32 Y)
		{
			const float Alpha = InExtent > 1 ? static_cast<float>(Y) / static_cast<float>(InExtent - 1) : 0.0f;
			return FColor(
				static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(InTop.R), static_cast<float>(InBottom.R), Alpha))),
				static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(InTop.G), static_cast<float>(InBottom.G), Alpha))),
				static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(InTop.B), static_cast<float>(InBottom.B), Alpha))),
				255);
		});
	}

	void FGalleryStage::UseBuiltInShader(bool bInUse)
	{
		UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
		if (!SavedBuiltInShader.IsSet())
		{
			SavedBuiltInShader = Settings->bUseBuiltInUIShader;
		}
		Settings->bUseBuiltInUIShader = bInUse;
	}

	void FGalleryStage::UseMultisampling(uint8 InSamples)
	{
		UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
		if (!SavedAntiAliasing.IsSet())
		{
			SavedAntiAliasing = Settings->AntiAliasingMethod;
			SavedSampleCount = Settings->MSAASampleCount;
		}
		Settings->AntiAliasingMethod = InSamples > 1 ? EDreamUIRendererAntiAliasingMethod::MSAA : EDreamUIRendererAntiAliasingMethod::None;
		Settings->MSAASampleCount = static_cast<EDreamUIRendererMSAASampleCount>(InSamples > 1 ? InSamples : 1);
	}

	void FGalleryStage::KeepAlive(UObject* InObject)
	{
		if (InObject != nullptr)
		{
			KeptAlive.Emplace(InObject);
		}
	}

	void FGalleryStage::CollectRasterFonts(TArray<UDreamUIFontData_FreeTypeRender*>& OutFonts) const
	{
		OutFonts.Reset();
		UDreamWidget* Root = RootWidget.Get();
		if (!IsValid(Root))
		{
			return;
		}
		TArray<UDreamWidget*> Widgets;
		UDreamWidget::CollectChildrenWidgets(Root, Widgets, true);
		for (UDreamWidget* Widget : Widgets)
		{
			const UDreamText* Text = IsValid(Widget) ? Cast<UDreamText>(Widget->GetVisual()) : nullptr;
			if (UDreamUIFontData_FreeTypeRender* Font = Text != nullptr ? Cast<UDreamUIFontData_FreeTypeRender>(Text->GetFont()) : nullptr)
			{
				OutFonts.AddUnique(Font);
			}
		}
	}

	int32 FGalleryStage::CountPendingGlyphs(bool bInFinishThem) const
	{
		TArray<UDreamUIFontData_FreeTypeRender*> Fonts;
		CollectRasterFonts(Fonts);
		int32 Pending = 0;
		for (UDreamUIFontData_FreeTypeRender* Font : Fonts)
		{
			if (bInFinishThem && Font->GetPendingAsyncGlyphCount() > 0)
			{
				Font->WaitForAsyncGlyphs();
			}
			Pending += Font->GetPendingAsyncGlyphCount();
		}
		return Pending;
	}

	void FGalleryStage::RequestRedraw() const
	{
		if (GEditor != nullptr)
		{
			GEditor->RedrawAllViewports(false);
		}
	}

	bool FGalleryStage::ReadBack(TArray<FColor>& OutPixels, FIntPoint& OutSize) const
	{
		return FDreamPixelProbe::ReadBack(TargetTexture.Get(), OutPixels, OutSize);
	}

	void FGalleryStage::TearDown()
	{
		if (bTornDown)
		{
			return;
		}
		bTornDown = true;
		UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
		if (SavedBuiltInShader.IsSet())
		{
			Settings->bUseBuiltInUIShader = SavedBuiltInShader.GetValue();
		}
		if (SavedAntiAliasing.IsSet())
		{
			Settings->AntiAliasingMethod = SavedAntiAliasing.GetValue();
			Settings->MSAASampleCount = SavedSampleCount;
		}
		if (UDreamWidget* Root = RootWidget.Get(); IsValid(Root))
		{
			Root->DestroyWidget();
		}
		CanvasComponent.Reset();
		RootWidget.Reset();
		TargetTexture.Reset();
		Textures.Reset();
		KeptAlive.Reset();
		World = nullptr;
	}

	FStageRef BeginStage(FAutomationTestBase& InTest, FIntPoint InSize, FColor InBackdrop)
	{
		FStageRef Stage = MakeShared<FGalleryStage>(InTest, InSize, InBackdrop);
		if (!Stage->IsUsable())
		{
			InTest.AddError(FString::Printf(TEXT("The gallery's render-target stage did not come up: %s."), *Stage->GetFailure()));
		}
		return Stage;
	}

	void EnqueueStep(TFunction<bool()> InStep)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(InStep));
	}

	void EnqueueDo(TFunction<void()> InAction)
	{
		EnqueueStep([InAction]() { InAction(); return true; });
	}

	void EnqueueFrames(const FStageRef& InStage, int32 InFrames)
	{
		TSharedRef<int32> Remaining = MakeShared<int32>(InFrames);
		EnqueueStep([InStage, Remaining]()
		{
			if (*Remaining <= 0)
			{
				return true;
			}
			InStage->RequestRedraw();
			--(*Remaining);
			return false;
		});
	}

	int32 CountDrawn(const TArray<FColor>& InPixels, FIntPoint InSize, FColor InBackdrop)
	{
		return InSize.X * InSize.Y - FDreamPixelProbe::CountColor(InPixels, InSize, FIntRect(0, 0, InSize.X, InSize.Y), InBackdrop, GoldenTolerance);
	}

	void EnqueueFramesUntilDrawn(const FStageRef& InStage, int32 InMinDrawn)
	{
		TSharedRef<double> Deadline = MakeShared<double>(0.0);
		EnqueueStep([InStage, InMinDrawn, Deadline]()
		{
			if (*Deadline == 0.0)
			{
				*Deadline = FPlatformTime::Seconds() + DrawnTimeoutSeconds;
			}
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if ((InStage->ReadBack(Pixels, Size) && CountDrawn(Pixels, Size, InStage->GetBackdrop()) >= InMinDrawn) || FPlatformTime::Seconds() > *Deadline)
			{
				return true;
			}
			InStage->RequestRedraw();
			return false;
		});
	}

	void EnqueueFramesUntilStable(const FStageRef& InStage)
	{
		struct FState
		{
			TArray<FColor> Previous;
			FIntPoint PreviousSize = FIntPoint::ZeroValue;
			int32 SameInARow = 0;
			double Deadline = 0.0;
		};
		TSharedRef<FState> State = MakeShared<FState>();
		EnqueueStep([InStage, State]()
		{
			if (State->Deadline == 0.0)
			{
				State->Deadline = FPlatformTime::Seconds() + StableTimeoutSeconds;
			}
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			const bool bRead = InStage->ReadBack(Pixels, Size);
			State->SameInARow = bRead && Size == State->PreviousSize && Pixels == State->Previous ? State->SameInARow + 1 : 0;
			State->Previous = MoveTemp(Pixels);
			State->PreviousSize = Size;
			if (State->SameInARow >= StableFrames || FPlatformTime::Seconds() > State->Deadline)
			{
				return true;
			}
			InStage->RequestRedraw();
			return false;
		});
	}

	void EnqueueFramesUntilSettled(const FStageRef& InStage)
	{
		struct FState
		{
			TArray<FColor> Previous;
			FIntPoint PreviousSize = FIntPoint::ZeroValue;
			int32 SameInARow = 0;
			double Deadline = 0.0;
		};
		TSharedRef<FState> State = MakeShared<FState>();
		EnqueueStep([InStage, State]()
		{
			if (State->Deadline == 0.0)
			{
				State->Deadline = FPlatformTime::Seconds() + SettleTimeoutSeconds;
			}
			const int32 Pending = InStage->CountPendingGlyphs(true);
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			const bool bRead = InStage->ReadBack(Pixels, Size);
			State->SameInARow = Pending == 0 && bRead && Size == State->PreviousSize && Pixels == State->Previous ? State->SameInARow + 1 : 0;
			State->Previous = MoveTemp(Pixels);
			State->PreviousSize = Size;
			if (State->SameInARow >= StableFrames)
			{
				return true;
			}
			if (FPlatformTime::Seconds() > State->Deadline)
			{
				InStage->GetTest().AddWarning(FString::Printf(TEXT("The stage did not settle within %.0f s (%d glyph(s) still pending); the picture is read as it stands."),
					SettleTimeoutSeconds, Pending));
				return true;
			}
			InStage->RequestRedraw();
			return false;
		});
	}

	void EnqueuePictureCheck(const FStageRef& InStage, const FString& InName, int32 InMinDrawn)
	{
		EnqueueFrames(InStage, 3);
		EnqueueFramesUntilDrawn(InStage, InMinDrawn);
		EnqueueFramesUntilStable(InStage);
		EnqueueDo([InStage, InName, InMinDrawn]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			FAutomationTestBase& Test = InStage->GetTest();
			if (!InStage->ReadBack(Pixels, Size))
			{
				Test.AddError(FString::Printf(TEXT("%s: the render target could not be read back at all."), *InName));
				return;
			}
			const int32 Drawn = CountDrawn(Pixels, Size, InStage->GetBackdrop());
			Test.TestTrue(FString::Printf(TEXT("%s draws something (%d pixel(s) are not the backdrop, at least %d expected)"), *InName, Drawn, InMinDrawn), Drawn >= InMinDrawn);
			FDreamPixelProbe::ExpectMatchesGolden(Test, Pixels, Size, InName, GoldenTolerance, GoldenAllowedFraction);
		});
	}

	bool CheckGolden(FAutomationTestBase& InTest, const TArray<FColor>& InPixels, FIntPoint InSize, const FString& InName, EMissingGolden InMissing)
	{
		const FString GoldenPath = FPaths::Combine(FDreamPixelProbe::GetGoldenDirectory(), InName + TEXT(".png"));
		const bool bWriteGoldens = FParse::Param(FCommandLine::Get(), TEXT("DreamGUIWriteGoldens"));
		if (InMissing == EMissingGolden::Fail && !bWriteGoldens && !FPaths::FileExists(GoldenPath))
		{
			const FString CapturePath = FDreamPixelProbe::SaveCapture(InPixels, InSize, InName);
			InTest.AddError(FString::Printf(TEXT("%s has no golden image at %s. This run's picture is %s: look at it, and once it is right, run again with -DreamGUIWriteGoldens on the editor's command line to write it."),
				*InName, *GoldenPath, CapturePath.IsEmpty() ? TEXT("(not written)") : *CapturePath));
			return false;
		}
		return FDreamPixelProbe::ExpectMatchesGolden(InTest, InPixels, InSize, InName, GoldenTolerance, GoldenAllowedFraction);
	}

	void EnqueueSettledPictureCheck(const FStageRef& InStage, const FString& InName, int32 InMinDrawn, EMissingGolden InMissing)
	{
		EnqueueFrames(InStage, 3);
		EnqueueFramesUntilDrawn(InStage, InMinDrawn);
		EnqueueFramesUntilSettled(InStage);
		EnqueueDo([InStage, InName, InMinDrawn, InMissing]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			FAutomationTestBase& Test = InStage->GetTest();
			if (!InStage->ReadBack(Pixels, Size))
			{
				Test.AddError(FString::Printf(TEXT("%s: the render target could not be read back at all."), *InName));
				return;
			}
			const int32 Drawn = CountDrawn(Pixels, Size, InStage->GetBackdrop());
			Test.TestTrue(FString::Printf(TEXT("%s draws something (%d pixel(s) are not the backdrop, at least %d expected)"), *InName, Drawn, InMinDrawn), Drawn >= InMinDrawn);
			CheckGolden(Test, Pixels, Size, InName, InMissing);
		});
	}

	void EnqueueTearDown(const FStageRef& InStage)
	{
		EnqueueDo([InStage]() { InStage->TearDown(); });
	}
}

#endif
