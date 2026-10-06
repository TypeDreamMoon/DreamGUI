// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamGUIClickSmoke.h"

#include "Containers/Ticker.h"
#include "Controls/DreamButton.h"
#include "Debugging/SlateDebugging.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "DreamUIBPLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProperties.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

/*
 * THE CLICK SMOKE PROBE.
 *
 * Every other click the suite makes is the test driver's, in the editor: it goes in at the viewport or at the input
 * system, past the platform's half of the road. This one is a game's, end to end. The button is made the way a game makes
 * one (the Create Dream Widget node's calls for the player, AddToViewport, placed by hand off the middle) and clicked the
 * way the platform clicks: a mouse move, a button down and a button up handed to FSlateApplication, which hit-tests its
 * window, routes them to the game viewport, which hands the button to the player controller's input and the position to
 * the viewport's cursor -- and from there DreamGUI's own input, whichever source the project set up, takes the press to
 * the button. Then the same three events well away from it, which must click nothing.
 *
 * Two things are set up so that the mouse the probe moves is the only one there is, and neither is what is being tested:
 *  - Slate.EnableSyntheticCursorMoves off for the run. Slate sends a move of its own every frame from where the desk's
 *    cursor really is (FSlateUser::SynthesizeCursorMoveIfNeeded), which would carry the game's cursor off the button
 *    between the probe's move and its press -- and the desk's cursor is not the probe's to move.
 *  - The player in the mode a menu runs in, the engine's Game and UI: the cursor shown, the mouse not hidden while a
 *    button is held and not locked to the window. A mouse hidden while held is put back where the desk had it on the
 *    release (FSceneViewport), which is the desk's cursor again.
 * Off unless the game is started with -DreamGUIClickSmoke=<dir>; read with FParse, so a Shipping build takes it too.
 */
namespace DreamGUIClickSmokeLocal
{
	constexpr double WorldTimeoutSeconds = 120.0;
	/** How long the button may take to be drawn where it can be clicked: its screen and its event system come up first. */
	constexpr double DrawnTimeoutSeconds = 30.0;
	/** Frames from the player being ready to placing the button, so the viewport has its size. */
	constexpr int32 WarmFrames = 5;
	/** Frames the button has to have been drawn for before the first move: the screen's raycaster is made with it. */
	constexpr int32 DrawnFramesNeeded = 10;
	/** Frames after each step before what the button announced is read: the input is read on the next world tick. */
	constexpr int32 StepFrames = 3;
	const FVector2D ButtonSize(240.0, 80.0);
	/** Where the button stands, from the middle of the screen: off it, so a click in the middle would not find it. */
	const FVector2D ButtonPosition(200.0, 120.0);
	/** Where the click away lands, as fractions of the viewport: a corner the button is nowhere near. */
	const FVector2D AwayFraction(0.08, 0.9);

	double Round2(double InValue)
	{
		return FMath::RoundToDouble(InValue * 100.0) / 100.0;
	}

	TSharedRef<FJsonObject> PointToJson(const FVector2D& InPoint)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), Round2(InPoint.X));
		Object->SetNumberField(TEXT("y"), Round2(InPoint.Y));
		return Object;
	}

	TSharedRef<FJsonObject> RectToJson(const FBox2D& InRect)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetBoolField(TEXT("valid"), InRect.bIsValid != 0);
		Object->SetNumberField(TEXT("left"), Round2(InRect.Min.X));
		Object->SetNumberField(TEXT("top"), Round2(InRect.Min.Y));
		Object->SetNumberField(TEXT("right"), Round2(InRect.Max.X));
		Object->SetNumberField(TEXT("bottom"), Round2(InRect.Max.Y));
		return Object;
	}

	/**
	 * A point of InWidget's local space on its root canvas's viewport, in pixels from the top left: the canvas's own view
	 * projection, as the text smoke probe and the test driver project a widget (its rect lies on its local X = 0 plane, 2D
	 * X along local Y and 2D Y along local Z). Screen-space canvases only.
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

	/** InWidget's rect on the viewport, in pixels; invalid when a corner has no pixel. */
	FBox2D WidgetToViewportRect(const UDreamWidget* InWidget)
	{
		FBox2D Rect(ForceInit);
		if (!IsValid(InWidget))
		{
			return Rect;
		}
		const FVector2D Corners[4] = {
			FVector2D(InWidget->GetLocalSpaceLeft(), InWidget->GetLocalSpaceBottom()),
			FVector2D(InWidget->GetLocalSpaceRight(), InWidget->GetLocalSpaceBottom()),
			FVector2D(InWidget->GetLocalSpaceLeft(), InWidget->GetLocalSpaceTop()),
			FVector2D(InWidget->GetLocalSpaceRight(), InWidget->GetLocalSpaceTop()) };
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

	class FProbe
	{
	public:
		explicit FProbe(const FString& InDirectory)
			: Directory(InDirectory)
		{
			StartSeconds = FPlatformTime::Seconds();
			if (IConsoleVariable* Synthetic = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.EnableSyntheticCursorMoves")))
			{
				SyntheticMovesWere = Synthetic->GetInt();
				Synthetic->Set(0, ECVF_SetByCode);
			}
			TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FProbe::Tick), 0.0f);
		}

		~FProbe()
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			if (SyntheticMovesWere != INDEX_NONE)
			{
				if (IConsoleVariable* Synthetic = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.EnableSyntheticCursorMoves")))
				{
					Synthetic->Set(SyntheticMovesWere, ECVF_SetByCode);
				}
			}
		}

	private:
		enum class EStage : uint8
		{
			WaitForWorld,
			Warm,
			WaitDrawn,
			Steps,
			Done,
		};

		/** One input of the click and the click away, and what had been announced the given frames after it. */
		struct FStep
		{
			FString Name;
			FVector2D Pixel = FVector2D::ZeroVector;
			FVector2D Screen = FVector2D::ZeroVector;
			/** EKeys::Invalid for a move. */
			FKey Button;
			bool bDown = false;
			/** Which of Slate's widgets answered the step's event, as Slate reports it (builds with Slate debugging only). */
			TArray<FString> SlateAnswers;
			TSharedPtr<FJsonObject> Record;
		};

		FString Directory;
		TStrongObjectPtr<UDreamGUIClickSmokeListener> Listener;
		EStage Stage = EStage::WaitForWorld;
		double StartSeconds = 0.0;
		double PlacedSeconds = 0.0;
		int32 WarmFramesLeft = WarmFrames;
		int32 DrawnFrames = 0;
		int32 SyntheticMovesWere = INDEX_NONE;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UDreamButton> Button;
		FBox2D ButtonRect = FBox2D(ForceInit);
		FIntPoint ViewportSize = FIntPoint::ZeroValue;
		TArray<FStep> Steps;
		int32 NextStep = 0;
		int32 StepFramesLeft = 0;
		FVector2D LastScreen = FVector2D::ZeroVector;
		FTSTicker::FDelegateHandle TickerHandle;

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

		static UGameViewportClient* GameViewport()
		{
			return GEngine != nullptr ? GEngine->GameViewport.Get() : nullptr;
		}

		/** A pixel of the game viewport where Slate has it on the desk, in its absolute units: what a mouse event carries. */
		static bool ViewportPixelToScreen(const FVector2D& InPixel, FVector2D& OutScreen)
		{
			UGameViewportClient* Client = GameViewport();
			const TSharedPtr<SViewport> Widget = Client != nullptr ? Client->GetGameViewportWidget() : nullptr;
			const FViewport* Viewport = Client != nullptr ? Client->Viewport : nullptr;
			if (!Widget.IsValid() || Viewport == nullptr)
			{
				return false;
			}
			// Tick space: where the widget is on the desk. Its paint space is its window's, and a mouse event carries the
			// desk's coordinates -- aimed by the window's, the moves landed wherever the window's corner is from the desk's.
			const FGeometry& Geometry = Widget->GetTickSpaceGeometry();
			const FIntPoint Size = Viewport->GetSizeXY();
			const FVector2D LocalSize = Geometry.GetLocalSize();
			if (Size.X <= 0 || Size.Y <= 0 || LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
			{
				return false;
			}
			OutScreen = FVector2D(Geometry.LocalToAbsolute(FVector2D(InPixel.X * LocalSize.X / Size.X, InPixel.Y * LocalSize.Y / Size.Y)));
			return true;
		}

		/**
		 * The player in a menu's mode, the engine's Game and UI: the cursor shown, and the mouse neither hidden while a
		 * button is held nor locked to the window. The mouse is captured while a button is held (CaptureDuringMouseDown, which
		 * the mode sets), and that is not to be taken away: a game viewport hands a press to its player only when it captures
		 * on the press or has the capture already (FSceneViewport::OnMouseButtonDown), so with NoCapture every press stops at
		 * the viewport and only the release goes on.
		 */
		static void ReadyThePlayerForAMouse(APlayerController* InPlayer)
		{
			InPlayer->SetShowMouseCursor(true);
			FInputModeGameAndUI Mode;
			Mode.SetHideCursorDuringCapture(false);
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			InPlayer->SetInputMode(Mode);
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
					ReadyThePlayerForAMouse(GameWorld->GetFirstPlayerController());
					Stage = EStage::Warm;
				}
				else if (Now - StartSeconds > WorldTimeoutSeconds)
				{
					Finish(FString::Printf(TEXT("no game world with a player and a viewport after %.0f seconds"), WorldTimeoutSeconds));
				}
				return true;

			case EStage::Warm:
				if (--WarmFramesLeft <= 0)
				{
					Place();
				}
				return true;

			case EStage::WaitDrawn:
				ButtonRect = WidgetToViewportRect(Button.Get());
				if (IsDrawnInside(ButtonRect))
				{
					if (++DrawnFrames >= DrawnFramesNeeded)
					{
						PlanSteps();
					}
				}
				else
				{
					DrawnFrames = 0;
					if (Now - PlacedSeconds > DrawnTimeoutSeconds)
					{
						Finish(FString::Printf(TEXT("the button was not drawn inside the viewport in %.0f seconds"), DrawnTimeoutSeconds));
					}
				}
				return true;

			case EStage::Steps:
				if (StepFramesLeft > 0)
				{
					if (--StepFramesLeft == 0)
					{
						Steps[NextStep - 1].Record = RecordStep(Steps[NextStep - 1]);
					}
					return true;
				}
				if (!Button.IsValid() || !World.IsValid())
				{
					Finish(TEXT("the button or the game world went partway through the clicks"));
					return true;
				}
				if (Steps.IsValidIndex(NextStep))
				{
					Send(Steps[NextStep]);
					++NextStep;
					StepFramesLeft = StepFrames;
				}
				else
				{
					Finish(FString());
				}
				return true;

			case EStage::Done:
			default:
				return true;
			}
		}

		bool IsDrawnInside(const FBox2D& InRect)
		{
			const UGameViewportClient* Client = GameViewport();
			ViewportSize = Client != nullptr && Client->Viewport != nullptr ? Client->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
			return InRect.bIsValid && ViewportSize.X > 0 && ViewportSize.Y > 0
				&& InRect.Min.X >= 0.0 && InRect.Min.Y >= 0.0 && InRect.Max.X <= ViewportSize.X && InRect.Max.Y <= ViewportSize.Y
				&& InRect.GetArea() > 1.0;
		}

		/** The button made as the Create Dream Widget node makes it for the player, put on the screen, placed by hand. */
		void Place()
		{
			UWorld* GameWorld = World.Get();
			APlayerController* Player = GameWorld != nullptr ? GameWorld->GetFirstPlayerController() : nullptr;
			if (Player == nullptr)
			{
				Finish(TEXT("the game world or its player went before the button was placed"));
				return;
			}
			UDreamUserWidget* Begun = UDreamUIBPLibrary::BeginDeferredCreateDreamWidget(GameWorld, UDreamButton::StaticClass(), Player);
			UDreamButton* Made = Cast<UDreamButton>(Begun != nullptr ? UDreamUIBPLibrary::FinishDeferredCreateDreamWidget(Begun) : nullptr);
			if (Made == nullptr)
			{
				Finish(TEXT("the Create Dream Widget calls made no button"));
				return;
			}
			// Made with the button rather than with the probe, which starts with the game module, before any world.
			Listener.Reset(NewObject<UDreamGUIClickSmokeListener>());
			Made->OnHovered.AddDynamic(Listener.Get(), &UDreamGUIClickSmokeListener::HandleHovered);
			Made->OnUnhovered.AddDynamic(Listener.Get(), &UDreamGUIClickSmokeListener::HandleUnhovered);
			Made->OnPressed.AddDynamic(Listener.Get(), &UDreamGUIClickSmokeListener::HandlePressed);
			Made->OnReleased.AddDynamic(Listener.Get(), &UDreamGUIClickSmokeListener::HandleReleased);
			Made->OnClicked.AddDynamic(Listener.Get(), &UDreamGUIClickSmokeListener::HandleClicked);
			Made->AddToViewport(0);
			Made->SetAnchorsInViewport(FVector2D(0.5, 0.5), FVector2D(0.5, 0.5));
			Made->SetDesiredSizeInViewport(ButtonSize);
			Made->SetPositionInViewport(ButtonPosition);
			Button = Made;
			PlacedSeconds = FPlatformTime::Seconds();
			Stage = EStage::WaitDrawn;
		}

		/** The click on the button's middle and the click away, as the moves and presses a hand makes. */
		void PlanSteps()
		{
			const FVector2D Middle = ButtonRect.GetCenter();
			const FVector2D Away(ViewportSize.X * AwayFraction.X, ViewportSize.Y * AwayFraction.Y);
			const auto Add = [this](const TCHAR* InName, const FVector2D& InPixel, const FKey& InButton, bool bInDown)
			{
				FStep& Step = Steps.AddDefaulted_GetRef();
				Step.Name = InName;
				Step.Pixel = InPixel;
				Step.Button = InButton;
				Step.bDown = bInDown;
			};
			Add(TEXT("move onto the button"), Middle, EKeys::Invalid, false);
			Add(TEXT("press on the button"), Middle, EKeys::LeftMouseButton, true);
			Add(TEXT("release on the button"), Middle, EKeys::LeftMouseButton, false);
			Add(TEXT("move away"), Away, EKeys::Invalid, false);
			Add(TEXT("press away"), Away, EKeys::LeftMouseButton, true);
			Add(TEXT("release away"), Away, EKeys::LeftMouseButton, false);
			if (!ViewportPixelToScreen(Middle, LastScreen))
			{
				Finish(TEXT("the game viewport has no place on the desk to aim a mouse at"));
				return;
			}
			Stage = EStage::Steps;
		}

		/** One step into FSlateApplication, as the platform's mouse message for it goes in. */
		void Send(FStep& InStep)
		{
			if (!ViewportPixelToScreen(InStep.Pixel, InStep.Screen))
			{
				Finish(FString::Printf(TEXT("the game viewport had no place on the desk at the step \"%s\""), *InStep.Name));
				return;
			}
			FSlateApplication& Slate = FSlateApplication::Get();
			const FModifierKeysState NoModifiers;
#if WITH_SLATE_DEBUGGING
			TArray<FString>& Answers = InStep.SlateAnswers;
			const FDelegateHandle Watch = FSlateDebugging::InputEvent.AddLambda([&Answers](const FSlateDebuggingInputEventArgs& InArgs)
			{
				Answers.Add(InArgs.ToText().ToString());
			});
			ON_SCOPE_EXIT { FSlateDebugging::InputEvent.Remove(Watch); };
#endif
			// And what the game viewport client hands on to a player, and to which.
			UGameViewportClient* Client = GameViewport();
			FDelegateHandle KeyWatch;
			if (Client != nullptr)
			{
				TArray<FString>& Keys = InStep.SlateAnswers;
				KeyWatch = Client->OnInputKey().AddLambda([&Keys, Client](const FInputKeyEventArgs& InArgs)
				{
					const ULocalPlayer* Target = GEngine->GetLocalPlayerFromInputDevice(Client, InArgs.InputDevice);
					Keys.Add(FString::Printf(TEXT("viewport client: %s %d from device %d, for %s"), *InArgs.Key.ToString(), static_cast<int32>(InArgs.Event),
						InArgs.InputDevice.GetId(), Target != nullptr ? *Target->GetName() : TEXT("no player")));
				});
			}
			ON_SCOPE_EXIT { if (Client != nullptr) { Client->OnInputKey().Remove(KeyWatch); } };
			if (!InStep.Button.IsValid())
			{
				TSet<FKey> Held;
				Slate.ProcessMouseMoveEvent(FPointerEvent(FSlateApplicationBase::CursorPointerIndex, InStep.Screen, LastScreen, Held,
					EKeys::Invalid, 0.0f, NoModifiers));
			}
			else if (InStep.bDown)
			{
				TSet<FKey> Held;
				Held.Add(InStep.Button);
				// No platform window: the one thing Slate does with it is ask the platform to capture the desk's mouse.
				Slate.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(FSlateApplicationBase::CursorPointerIndex, InStep.Screen, InStep.Screen, Held,
					InStep.Button, 0.0f, NoModifiers));
			}
			else
			{
				Slate.ProcessMouseButtonUpEvent(FPointerEvent(FSlateApplicationBase::CursorPointerIndex, InStep.Screen, InStep.Screen, TSet<FKey>(),
					InStep.Button, 0.0f, NoModifiers));
			}
			LastScreen = InStep.Screen;
		}

		TSharedRef<FJsonObject> Counts() const
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			const UDreamGUIClickSmokeListener* Heard = Listener.Get();
			Object->SetNumberField(TEXT("hovered"), Heard != nullptr ? Heard->Hovered : 0);
			Object->SetNumberField(TEXT("unhovered"), Heard != nullptr ? Heard->Unhovered : 0);
			Object->SetNumberField(TEXT("pressed"), Heard != nullptr ? Heard->Pressed : 0);
			Object->SetNumberField(TEXT("released"), Heard != nullptr ? Heard->Released : 0);
			Object->SetNumberField(TEXT("clicked"), Heard != nullptr ? Heard->Clicked : 0);
			return Object;
		}

		/** What the step reached, read StepFrames frames after it: the viewport's cursor, the player's mouse, the button's events. */
		TSharedPtr<FJsonObject> RecordStep(const FStep& InStep) const
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("name"), InStep.Name);
			Object->SetObjectField(TEXT("pixel"), PointToJson(InStep.Pixel));
			Object->SetObjectField(TEXT("screen"), PointToJson(InStep.Screen));
			Object->SetStringField(TEXT("button"), InStep.Button.IsValid() ? InStep.Button.ToString() : FString());
			Object->SetBoolField(TEXT("down"), InStep.bDown);
			const UGameViewportClient* Client = GameViewport();
			if (Client != nullptr && Client->Viewport != nullptr)
			{
				Object->SetObjectField(TEXT("viewportCursor"), PointToJson(FVector2D(Client->Viewport->GetMouseX(), Client->Viewport->GetMouseY())));
			}
			const UWorld* GameWorld = World.Get();
			const APlayerController* Player = GameWorld != nullptr ? GameWorld->GetFirstPlayerController() : nullptr;
			float MouseX = 0.0f;
			float MouseY = 0.0f;
			const bool bPlayerMouse = Player != nullptr && Player->GetMousePosition(MouseX, MouseY);
			Object->SetBoolField(TEXT("playerMouseValid"), bPlayerMouse);
			Object->SetObjectField(TEXT("playerMouse"), PointToJson(FVector2D(MouseX, MouseY)));
			Object->SetBoolField(TEXT("buttonHovered"), Button.IsValid() && Button->IsHovered());
			// Where a press stops when it does not reach the button: the viewport's keys, then the player's.
			Object->SetBoolField(TEXT("viewportLeftDown"), Client != nullptr && Client->Viewport != nullptr && Client->Viewport->KeyState(EKeys::LeftMouseButton));
			Object->SetBoolField(TEXT("playerLeftDown"), Player != nullptr && Player->IsInputKeyDown(EKeys::LeftMouseButton));
			TArray<TSharedPtr<FJsonValue>> Answers;
			for (const FString& Answer : InStep.SlateAnswers)
			{
				Answers.Add(MakeShared<FJsonValueString>(Answer));
			}
			Object->SetArrayField(TEXT("slateAnswers"), Answers);
			Object->SetObjectField(TEXT("counts"), Counts());
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
			Root->SetBoolField(TEXT("finished"), InFailure.IsEmpty());
			Root->SetStringField(TEXT("failure"), InFailure);

			TSharedRef<FJsonObject> Build = MakeShared<FJsonObject>();
			Build->SetStringField(TEXT("configuration"), LexToString(FApp::GetBuildConfiguration()));
			Build->SetBoolField(TEXT("cooked"), FPlatformProperties::RequiresCookedData());
			Build->SetBoolField(TEXT("withEditor"), WITH_EDITOR != 0);
			Build->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
			Build->SetStringField(TEXT("platform"), FString(ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName())));
			Root->SetObjectField(TEXT("build"), Build);

			TSharedRef<FJsonObject> Viewport = MakeShared<FJsonObject>();
			Viewport->SetNumberField(TEXT("width"), ViewportSize.X);
			Viewport->SetNumberField(TEXT("height"), ViewportSize.Y);
			Root->SetObjectField(TEXT("viewport"), Viewport);
			Root->SetNumberField(TEXT("syntheticCursorMovesWere"), SyntheticMovesWere);

			TSharedRef<FJsonObject> ButtonObject = MakeShared<FJsonObject>();
			ButtonObject->SetObjectField(TEXT("rect"), RectToJson(ButtonRect));
			ButtonObject->SetObjectField(TEXT("middle"), PointToJson(ButtonRect.bIsValid ? ButtonRect.GetCenter() : FVector2D::ZeroVector));
			ButtonObject->SetNumberField(TEXT("secondsToPlace"), PlacedSeconds > 0.0 ? Round2(PlacedSeconds - StartSeconds) : -1.0);
			Root->SetObjectField(TEXT("button"), ButtonObject);

			TArray<TSharedPtr<FJsonValue>> StepValues;
			for (const FStep& Step : Steps)
			{
				if (Step.Record.IsValid())
				{
					StepValues.Add(MakeShared<FJsonValueObject>(Step.Record));
				}
			}
			Root->SetArrayField(TEXT("steps"), StepValues);
			Root->SetObjectField(TEXT("counts"), Counts());
			TArray<TSharedPtr<FJsonValue>> Order;
			for (const FString& Event : Listener.IsValid() ? Listener->Order : TArray<FString>())
			{
				Order.Add(MakeShared<FJsonValueString>(Event));
			}
			Root->SetArrayField(TEXT("order"), Order);

			FString JsonText;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonText);
			FJsonSerializer::Serialize(Root, Writer);
			const FString JsonPath = FPaths::Combine(Directory, TEXT("ClickSmoke.json"));
			const bool bWritten = FFileHelper::SaveStringToFile(JsonText, *JsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogTemp, Display, TEXT("DreamGUI click smoke: %s %s (%s)."), bWritten ? TEXT("wrote") : TEXT("could not write"), *JsonPath,
				InFailure.IsEmpty() ? TEXT("finished") : *InFailure);
			RequestEngineExit(TEXT("DreamGUI click smoke finished"));
		}
	};

	TUniquePtr<FProbe> ActiveProbe;
}

void DreamGUIClickSmoke::StartIfAsked()
{
	using namespace DreamGUIClickSmokeLocal;
	FString Directory;
	if (ActiveProbe.IsValid() || GIsEditor || IsRunningCommandlet() || !FParse::Value(FCommandLine::Get(), TEXT("DreamGUIClickSmoke="), Directory))
	{
		return;
	}
	if (Directory.IsEmpty())
	{
		Directory = TEXT("DreamGUIClickSmoke");
	}
	if (FPaths::IsRelative(Directory))
	{
		Directory = FPaths::Combine(FPaths::LaunchDir(), Directory);
	}
	ActiveProbe = MakeUnique<FProbe>(FPaths::ConvertRelativePathToFull(Directory));
}

void DreamGUIClickSmoke::Stop()
{
	DreamGUIClickSmokeLocal::ActiveProbe.Reset();
}
