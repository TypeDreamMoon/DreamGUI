// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUISettings.h"
#include "DreamUICaptureLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Utils/DreamUIUtils.h"

#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverSequence.h"
#include "DreamInputPipelineTestTypes.h"
#include "RHI/DreamPixelProbe.h"

namespace DreamSplitScreenSharedPixelTestLocal
{
	struct FState
	{
		const EDreamUIRendererAntiAliasingMethod SavedAA = GetDefault<UDreamUISettings>()->AntiAliasingMethod;
		const EDreamUIRendererMSAASampleCount SavedSamples = GetDefault<UDreamUISettings>()->MSAASampleCount;

		FState()
		{
			// Explicitly exercise the final resolve, rather than depending on the host project's AA setting.
			GetMutableDefault<UDreamUISettings>()->AntiAliasingMethod = EDreamUIRendererAntiAliasingMethod::MSAA;
			GetMutableDefault<UDreamUISettings>()->MSAASampleCount = EDreamUIRendererMSAASampleCount::Two;
		}

		~FState()
		{
			GetMutableDefault<UDreamUISettings>()->AntiAliasingMethod = SavedAA;
			GetMutableDefault<UDreamUISettings>()->MSAASampleCount = SavedSamples;
		}
		TWeakObjectPtr<ULocalPlayer> SecondPlayer;
		TWeakObjectPtr<UDreamWidget> PlayerRoots[2];
		TWeakObjectPtr<UDreamWidget> Backgrounds[2];
		TWeakObjectPtr<UDreamPointerLedger> LeftLedger;
		TWeakObjectPtr<UDreamPointerLedger> RightLedger;
		FIntPoint Size = FIntPoint::ZeroValue;
		TArray<FColor> ReferencePixels;
	};

	FIntRect PlayerRect(const ULocalPlayer& InPlayer, FIntPoint InSize)
	{
		const FIntPoint Min(FMath::TruncToInt32(InPlayer.Origin.X * InSize.X), FMath::TruncToInt32(InPlayer.Origin.Y * InSize.Y));
		const FIntPoint Extent(FMath::TruncToInt32(InPlayer.Size.X * InSize.X), FMath::TruncToInt32(InPlayer.Size.Y * InSize.Y));
		return FIntRect(Min, Min + Extent);
	}

	UDreamWidget* AddBlock(FDreamDriverPieRig& InRig, const TCHAR* InName, FVector2D InSize, FVector2D InPosition, FColor InColor)
	{
		UDreamWidget* Widget = InRig.MakeWidgetWithVisual(UDreamTexture::StaticClass(), InName, nullptr, InSize, InPosition);
		if (UDreamTexture* Visual = Widget != nullptr ? Cast<UDreamTexture>(Widget->GetVisual()) : nullptr)
		{
			Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
			Visual->SetColor(InColor);
		}
		return Widget;
	}

	bool ReadPicture(FAutomationTestBase& InTest, UWorld* InWorld, TArray<FColor>& OutPixels, FIntPoint& OutSize)
	{
		return InTest.TestTrue(TEXT("The real play viewport reads back"),
			UDreamUICaptureLibrary::ReadViewportPixels(UDreamUICaptureLibrary::FindViewportOf(InWorld), OutPixels, OutSize));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSplitScreenSharedPictureTest,
	"DreamGUI.Screen.RHI.ASharedScreenKeepsItsFullViewportPixelsAboveBothPlayersAndBlendsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamSplitScreenSharedPictureTest::RunTest(const FString& Parameters)
{
	using namespace DreamSplitScreenSharedPixelTestLocal;
	const TSharedRef<FState> State = MakeShared<FState>();
	const TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([this, State](FDreamDriverPieRig& InRig)
	{
		UDreamCanvas* Canvas = InRig.RootCanvas();
		UGameViewportClient* Viewport = InRig.GetViewportClient();
		if (!TestNotNull(TEXT("The shared screen has a canvas"), Canvas)
			|| !TestNotNull(TEXT("The play session has its own viewport client"), Viewport))
		{
			return;
		}
		Canvas->SetViewportPlayerIndex(INDEX_NONE);
		Canvas->SetScreenSpaceRenderScale(1.0f);
		State->Size = Canvas->GetViewportSize();
		if (!TestTrue(TEXT("The viewport can contain the pixel probes"), State->Size.X >= 400 && State->Size.Y >= 240))
		{
			return;
		}
		// Change only this session's table: whichever two-player layout the project chooses, give it left/right rects.
		Viewport->SplitscreenInfo[ESplitScreenType::TwoPlayer_Horizontal] = Viewport->SplitscreenInfo[ESplitScreenType::TwoPlayer_Vertical];
		const FVector2D HalfScreen(State->Size.X / 2.0, State->Size.Y);
		State->Backgrounds[0] = AddBlock(InRig, TEXT("LeftPlayerBackground"), HalfScreen, FVector2D(-State->Size.X / 4.0, 0.0), FColor::Blue);
		State->Backgrounds[1] = AddBlock(InRig, TEXT("RightPlayerBackground"), HalfScreen, FVector2D(State->Size.X / 4.0, 0.0), FColor::Green);
		UDreamWidget* Left = AddBlock(InRig, TEXT("SharedLeft"), FVector2D(80.0, 64.0), FVector2D(-State->Size.X * 0.3, State->Size.Y * 0.25), FColor::Red);
		UDreamWidget* Right = AddBlock(InRig, TEXT("SharedRight"), FVector2D(80.0, 64.0), FVector2D(State->Size.X * 0.2, -State->Size.Y * 0.25), FColor::Yellow);
		State->LeftLedger = Left != nullptr ? Left->AddComponent<UDreamPointerLedger>() : nullptr;
		State->RightLedger = Right != nullptr ? Right->AddComponent<UDreamPointerLedger>() : nullptr;
		AddBlock(InRig, TEXT("SharedAcrossTheSplit"), FVector2D(200.0, 32.0), FVector2D::ZeroVector, FColor(255, 255, 255, 128));
	});

	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.WaitFrames(8);
	Steps.Then([this, State](FDreamDriverContext& InContext)
	{
		FIntPoint ReferenceSize;
		if (!ReadPicture(*this, InContext.World, State->ReferencePixels, ReferenceSize)
			|| !TestEqual(TEXT("The single-player reference has the canvas's viewport size"), ReferenceSize, State->Size))
		{
			return;
		}
		UGameInstance* GameInstance = InContext.World != nullptr ? InContext.World->GetGameInstance() : nullptr;
		if (!TestNotNull(TEXT("The play session has a game instance"), GameInstance))
		{
			return;
		}
		FString WhyNot;
		ULocalPlayer* Second = GameInstance->CreateLocalPlayer(1, WhyNot, true);
		if (!TestNotNull(*FString::Printf(TEXT("A second player joins (%s)"), *WhyNot), Second))
		{
			return;
		}
		State->SecondPlayer = Second;
		for (int32 PlayerIndex = 0; PlayerIndex < 2; ++PlayerIndex)
		{
			// Registered free roots are held by the world's UI manager and released at PIE teardown.
			UDreamWidget* Root = NewObject<UDreamWidget>(InContext.World, NAME_None, RF_Transient);
			Root->SetDisplayName(FString::Printf(TEXT("Player%dScreen"), PlayerIndex));
			Root->OnRegister();
			State->PlayerRoots[PlayerIndex] = Root;
			UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
			if (!TestNotNull(TEXT("Each player screen has a canvas"), Canvas))
			{
				continue;
			}
			Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
			Canvas->SetViewportPlayerIndex(PlayerIndex);
			Root->BeginPlay();
			if (UDreamWidget* Background = State->Backgrounds[PlayerIndex].Get())
			{
				if (!TestTrue(TEXT("The background moves onto its player's own screen"), Background->TrySetParent(Root, false)))
				{
					continue;
				}
				Background->SetAnchorMin(FVector2D::ZeroVector);
				Background->SetAnchorMax(FVector2D(1.0, 1.0));
				Background->SetAnchoredPosition(FVector2D::ZeroVector);
				Background->SetSizeDelta(FVector2D::ZeroVector);
			}
		}
	});
	// Player rects, canvas sizing, worker batching and the render thread all have to see the joined player.
	Steps.WaitFrames(8);
	Steps.Then([this, State](FDreamDriverContext& InContext)
	{
		TArray<FColor> Pixels;
		FIntPoint Size;
		const ULocalPlayer* Second = State->SecondPlayer.Get();
		if (!TestNotNull(TEXT("Player 0 is still in the session"), InContext.LocalPlayer)
			|| !TestNotNull(TEXT("Player 1 is still in the session"), Second)
			|| !ReadPicture(*this, InContext.World, Pixels, Size)
			|| !TestEqual(TEXT("Splitting preserves the viewport size"), Size, State->Size)
			|| !TestEqual(TEXT("The blend reference has every pixel"), State->ReferencePixels.Num(), Size.X * Size.Y))
		{
			return;
		}
		const FIntRect LeftPart = PlayerRect(*InContext.LocalPlayer, Size);
		const FIntRect RightPart = PlayerRect(*Second, Size);
		if (!TestTrue(TEXT("Two different views occupy the left and right parts of the viewport"),
			LeftPart.Min == FIntPoint::ZeroValue && LeftPart.Max.X == RightPart.Min.X
			&& FMath::Abs(RightPart.Max.X - Size.X) <= 1 && RightPart.Max.Y == Size.Y
			&& LeftPart.Height() == Size.Y && RightPart.Height() == Size.Y))
		{
			return;
		}
		const FIntPoint LeftPixel(FMath::RoundToInt32(Size.X * 0.2), FMath::RoundToInt32(Size.Y * 0.25));
		const FIntPoint RightPixel(FMath::RoundToInt32(Size.X * 0.7), FMath::RoundToInt32(Size.Y * 0.75));
		FDreamPixelProbe::ExpectColorAt(*this, Pixels, Size, LeftPixel, FColor::Red, 8, TEXT("the shared red block at its full-viewport position"));
		FDreamPixelProbe::ExpectColorAt(*this, Pixels, Size, RightPixel, FColor::Yellow, 8, TEXT("the shared yellow block at a different full-viewport position"));
		TestEqual(TEXT("The shared red block is not repeated in player 1's view"), FDreamPixelProbe::CountColor(Pixels, Size, RightPart, FColor::Red, 8), 0);
		TestEqual(TEXT("The shared yellow block is not repeated in player 0's view"), FDreamPixelProbe::CountColor(Pixels, Size, LeftPart, FColor::Yellow, 8), 0);
		const int32 RedHeight = FDreamPixelProbe::CountColor(Pixels, Size, FIntRect(LeftPixel.X, 0, LeftPixel.X + 1, Size.Y), FColor::Red, 8);
		const int32 YellowHeight = FDreamPixelProbe::CountColor(Pixels, Size, FIntRect(RightPixel.X, 0, RightPixel.X + 1, Size.Y), FColor::Yellow, 8);
		TestTrue(TEXT("Both shared blocks retain their full 64-pixel height"), FMath::Abs(RedHeight - 64) <= 4 && FMath::Abs(YellowHeight - 64) <= 4);
		FDreamPixelProbe::ExpectColorAt(*this, Pixels, Size, FIntPoint(Size.X / 10, Size.Y / 2), FColor::Blue, 8, TEXT("player 0's own layer"));
		FDreamPixelProbe::ExpectColorAt(*this, Pixels, Size, FIntPoint(Size.X * 9 / 10, Size.Y / 2), FColor::Green, 8, TEXT("player 1's own layer"));
		for (int32 Side : {-1, 1})
		{
			const FIntPoint Pixel(Size.X / 2 + Side * 20, Size.Y / 2);
			const FColor Expected = State->ReferencePixels[Pixel.Y * Size.X + Pixel.X];
			FDreamPixelProbe::ExpectColorAt(*this, Pixels, Size, Pixel, Expected, 8,
				Side < 0 ? TEXT("the shared translucent strip above player 0, blended once") : TEXT("the shared translucent strip above player 1, blended once"));
			TestFalse(TEXT("The reference strip actually changed the opaque player's colour"),
				FDreamPixelProbe::IsNear(Expected, Side < 0 ? FColor::Blue : FColor::Green, 8));
		}
		FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("SplitScreen_SharedFullViewport"));
	});
	// The production screen-space raycaster aims at the same full-viewport pixels that were read above.
	Steps.Click(Rig->Made(TEXT("SharedLeft"))).Click(Rig->Made(TEXT("SharedRight")));
	Steps.Then([this, State](FDreamDriverContext& InContext)
	{
		if (TestNotNull(TEXT("The left shared block kept its pointer ledger"), State->LeftLedger.Get())
			&& TestNotNull(TEXT("The right shared block kept its pointer ledger"), State->RightLedger.Get()))
		{
			TestEqual(TEXT("A click at the rendered left block hits it once"), State->LeftLedger->Click, 1);
			TestEqual(TEXT("A click at the rendered right block hits it once"), State->RightLedger->Click, 1);
		}
		// Also exercise a split session with only a shared root: there are no PlayerParts to select the path.
		for (const TWeakObjectPtr<UDreamWidget>& Root : State->PlayerRoots)
		{
			if (Root.IsValid())
			{
				Root->DestroyWidget();
			}
		}
	});
	Steps.WaitFrames(8);
	Steps.Then([this, State](FDreamDriverContext& InContext)
	{
		TArray<FColor> Pixels;
		FIntPoint Size;
		if (ReadPicture(*this, InContext.World, Pixels, Size) && TestEqual(TEXT("A shared-only split keeps the viewport size"), Size, State->Size))
		{
			FDreamPixelProbe::ExpectColorAt(*this, Pixels, Size,
				FIntPoint(FMath::RoundToInt32(Size.X * 0.2), FMath::RoundToInt32(Size.Y * 0.25)), FColor::Red, 8, TEXT("the left shared block with no player roots"));
			FDreamPixelProbe::ExpectColorAt(*this, Pixels, Size,
				FIntPoint(FMath::RoundToInt32(Size.X * 0.7), FMath::RoundToInt32(Size.Y * 0.75)), FColor::Yellow, 8, TEXT("the right shared block with no player roots"));
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
