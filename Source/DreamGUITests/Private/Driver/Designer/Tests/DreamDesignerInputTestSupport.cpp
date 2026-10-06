// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"

#include "Driver/Designer/DreamDesignerDriver.h"

#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"

#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Settings/LevelEditorViewportSettings.h"

namespace DreamTests
{
	// ------------------------------------------------------------------------------------------------ preferences

	FScopedDesignerPreferences::FScopedDesignerPreferences(bool bInSaveWhenPuttingBack)
		: bSaveWhenPuttingBack(bInSaveWhenPuttingBack)
	{
		const UDreamUIDesignerSettings* Settings = GetDefault<UDreamUIDesignerSettings>();
		bGridSnapEnabled = Settings->bGridSnapEnabled;
		GridSize = Settings->GridSize;
		bShowDesignerGuides = Settings->bShowDesignerGuides;
		bPreviewDPIScale = Settings->bPreviewDPIScale;
		bShowDesignerRulers = Settings->bShowDesignerRulers;
	}

	FScopedDesignerPreferences::~FScopedDesignerPreferences()
	{
		UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
		const bool bChanged = Settings->bGridSnapEnabled != bGridSnapEnabled
			|| Settings->GridSize != GridSize
			|| Settings->bShowDesignerGuides != bShowDesignerGuides
			|| Settings->bPreviewDPIScale != bPreviewDPIScale
			|| Settings->bShowDesignerRulers != bShowDesignerRulers;
		Settings->bGridSnapEnabled = bGridSnapEnabled;
		Settings->GridSize = GridSize;
		Settings->bShowDesignerGuides = bShowDesignerGuides;
		Settings->bPreviewDPIScale = bPreviewDPIScale;
		Settings->bShowDesignerRulers = bShowDesignerRulers;
		if (bSaveWhenPuttingBack && bChanged)
		{
			// The toolbar saved what it flipped; saving again puts the author's own choice back on disk too.
			Settings->SaveConfig();
		}
	}

	void FScopedDesignerPreferences::SetGridSnap(bool bInEnabled, float InGridSize)
	{
		UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
		Settings->bGridSnapEnabled = bInEnabled;
		Settings->GridSize = FMath::Max(1.0f, InGridSize);
	}

	void FScopedDesignerPreferences::SetGuides(bool bInShown)
	{
		GetMutableDefault<UDreamUIDesignerSettings>()->bShowDesignerGuides = bInShown;
	}

	void FScopedDesignerPreferences::SetPreviewDPIScale(bool bInPreview)
	{
		GetMutableDefault<UDreamUIDesignerSettings>()->bPreviewDPIScale = bInPreview;
	}

	FScopedViewportCameraPreferences::FScopedViewportCameraPreferences(bool bInCenterZoomAroundCursor, bool bInPanMovesCanvas)
	{
		ULevelEditorViewportSettings* Settings = GetMutableDefault<ULevelEditorViewportSettings>();
		bCenterZoomAroundCursor = Settings->bCenterZoomAroundCursor != 0;
		bPanMovesCanvas = Settings->bPanMovesCanvas != 0;
		Settings->bCenterZoomAroundCursor = bInCenterZoomAroundCursor;
		Settings->bPanMovesCanvas = bInPanMovesCanvas;
	}

	FScopedViewportCameraPreferences::~FScopedViewportCameraPreferences()
	{
		ULevelEditorViewportSettings* Settings = GetMutableDefault<ULevelEditorViewportSettings>();
		Settings->bCenterZoomAroundCursor = bCenterZoomAroundCursor;
		Settings->bPanMovesCanvas = bPanMovesCanvas;
	}

	// ------------------------------------------------------------------------------------------------ one frame at a time

	FIntPoint PointInBox(const FBox2D& InBox, double InFractionX, double InFractionY)
	{
		return FIntPoint(
			FMath::RoundToInt32(FMath::Lerp(InBox.Min.X, InBox.Max.X, InFractionX)),
			FMath::RoundToInt32(FMath::Lerp(InBox.Min.Y, InBox.Max.Y, InFractionY)));
	}

	bool PrepareDesignerOneToOne(FAutomationTestBase& InTest, FDreamDesignerDriver& InDriver, FBox2D& OutWorkArea)
	{
		FDreamWidgetBlueprintEditor* Toolkit = InDriver.Toolkit();
		if (Toolkit == nullptr)
		{
			InTest.AddError(TEXT("The designer has no toolkit to zoom."));
			return false;
		}
		Toolkit->SetDesignerPixelsPerUnit(1.0f);
		InDriver.PumpFrame();
		const TOptional<FBox2D> RootRect = InDriver.WidgetPixelRect(InDriver.BlueprintRoot());
		if (!RootRect.IsSet())
		{
			InTest.AddError(TEXT("The Blueprint root does not project onto the viewport, so there is nowhere to put anything."));
			return false;
		}
		const FIntPoint Size = InDriver.ViewportPixelSize();
		const FVector2D Margin(Size.X * 0.1, Size.Y * 0.1);
		OutWorkArea = FBox2D(
			FVector2D(FMath::Max(RootRect->Min.X, Margin.X), FMath::Max(RootRect->Min.Y, Margin.Y)),
			FVector2D(FMath::Min(RootRect->Max.X, Size.X - Margin.X), FMath::Min(RootRect->Max.Y, Size.Y - Margin.Y)));
		if (OutWorkArea.Max.X - OutWorkArea.Min.X < 200.0 || OutWorkArea.Max.Y - OutWorkArea.Min.Y < 200.0)
		{
			InTest.AddError(FString::Printf(TEXT("Too little of the Blueprint root is on screen to work in: %s."), *OutWorkArea.ToString()));
			return false;
		}
		return true;
	}

	UDreamWidget* DropPlainWidgetAt(FAutomationTestBase& InTest, FDreamDesignerDriver& InDriver, FIntPoint InPixel)
	{
		UDreamWidget* Template = DropOntoRootAndFindTemplate(InDriver, /*Plain Widget row*/ nullptr, InPixel);
		if (Template == nullptr)
		{
			InTest.AddError(FString::Printf(TEXT("A plain widget dropped at (%d, %d) did not arrive under the Blueprint root."), InPixel.X, InPixel.Y));
			return nullptr;
		}
		InDriver.PumpFrame();
		return Template;
	}

	void SelectOnlyInDesigner(FDreamDesignerDriver& InDriver, const UDreamWidget* InTemplate)
	{
		FDreamWidgetBlueprintEditor* Toolkit = InDriver.Toolkit();
		UDreamWidget* Preview = InDriver.PreviewFor(InTemplate);
		if (Toolkit != nullptr && Preview != nullptr)
		{
			Toolkit->SelectWidgets(TSet<UDreamWidget*>({ Preview }), /*bAppendOrToggle*/ false);
		}
	}

	void SelectNothingInDesigner(FDreamDesignerDriver& InDriver)
	{
		if (FDreamWidgetBlueprintEditor* Toolkit = InDriver.Toolkit())
		{
			Toolkit->SelectWidgets(TSet<UDreamWidget*>(), /*bAppendOrToggle*/ false);
		}
	}

	FVector2D PixelsPerUnitFor(const FDreamDesignerDriver& InDriver, const UDreamWidget* InPreviewWidget)
	{
		const UDreamWidget* Parent = ::IsValid(InPreviewWidget) ? InPreviewWidget->GetParent() : nullptr;
		const TOptional<FBox2D> ParentRect = InDriver.WidgetPixelRect(Parent);
		if (!ParentRect.IsSet() || Parent->GetWidth() <= 0.0f || Parent->GetHeight() <= 0.0f)
		{
			return FVector2D::ZeroVector;
		}
		return FVector2D(
			(ParentRect->Max.X - ParentRect->Min.X) / Parent->GetWidth(),
			(ParentRect->Max.Y - ParentRect->Min.Y) / Parent->GetHeight());
	}

	// ------------------------------------------------------------------------------------------------ across frames

	UDreamWidget* FDesignerLatentState::Get(FName InName) const
	{
		const TWeakObjectPtr<UDreamWidget>* Found = Made.Find(InName);
		return Found != nullptr ? Found->Get() : nullptr;
	}

	FDesignerLatentRef OpenLatentDesigner(FAutomationTestBase& InTest, const TCHAR* InName, bool bGiveRootAPanel)
	{
		FDesignerLatentRef State = MakeShared<FDesignerLatentState>();
		State->Asset = CreateDesignerTestAsset(InName, bGiveRootAPanel);
		if (!State->Asset.IsValid())
		{
			InTest.AddError(FString::Printf(TEXT("The Widget Blueprint for %s could not be created."), InName));
			State->bAlive = false;
			return State;
		}
		State->Driver = FDreamDesignerDriver::Open(State->Asset.Blueprint);
		if (!State->Driver.IsValid())
		{
			InTest.AddError(TEXT("The designer did not open, or opened without a viewport (see LogDreamDesignerDriver)."));
			State->bAlive = false;
			return State;
		}
		// Changes nothing on a designer a real window has laid out; fills the hole where nothing has.
		if (!State->Driver->EnsureHeadlessSize(FIntPoint(1280, 720)))
		{
			InTest.AddError(FString::Printf(TEXT("The designer viewport could not be given a size; it measures %dx%d."),
				State->Driver->ViewportPixelSize().X, State->Driver->ViewportPixelSize().Y));
			State->bAlive = false;
		}
		return State;
	}

	void EnqueueDesignerStep(TFunction<bool()> InStep)
	{
		// Through a named local rather than straight into the macro: a lambda's capture list carries commas.
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(InStep));
	}

	void EnqueueDesignerAction(const FDesignerLatentRef& InState, TFunction<void()> InAction)
	{
		EnqueueDesignerStep([InState, InAction]()
		{
			if (InState->bAlive && InState->Driver.IsValid() && InState->Driver->IsUsable())
			{
				InAction();
			}
			return true;
		});
	}

	void EnqueueDesignerFrames(const FDesignerLatentRef& InState, int32 InFrames, bool bInDraw)
	{
		TSharedRef<int32> Remaining = MakeShared<int32>(FMath::Max(InFrames, 0));
		EnqueueDesignerStep([InState, Remaining, bInDraw]()
		{
			// Reaches zero on one Update and reports done on the next: a latent command that finishes lets the following
			// one run in the SAME tick, so finishing on the last frame would put the next step into the frame it belongs to.
			if (*Remaining <= 0)
			{
				return true;
			}
			if (bInDraw && InState->bAlive && InState->Driver.IsValid())
			{
				InState->Driver->DrawFrame();
			}
			--(*Remaining);
			++InState->Frames;
			return false;
		});
	}

	void EnqueueDesignerUntil(const FDesignerLatentRef& InState, FAutomationTestBase* InTest, TFunction<bool()> InReady,
		double InTimeoutSeconds, const FString& InWhat, bool bInDraw, TFunction<FString()> InWhatIsThere)
	{
		TSharedRef<double> Since = MakeShared<double>(-1.0);
		EnqueueDesignerStep([InState, InTest, InReady, InTimeoutSeconds, InWhat, bInDraw, InWhatIsThere, Since]()
		{
			if (!InState->bAlive || !InState->Driver.IsValid())
			{
				return true;
			}
			if (*Since < 0.0)
			{
				*Since = FPlatformTime::Seconds();
			}
			if (InReady())
			{
				return true;
			}
			if (bInDraw)
			{
				InState->Driver->DrawFrame();
			}
			++InState->Frames;
			const double Waited = FPlatformTime::Seconds() - *Since;
			if (Waited >= InTimeoutSeconds)
			{
				InTest->AddError(FString::Printf(TEXT("After %.2f seconds (%d frames into the test) %s still was not so; the test stops here.%s"),
					Waited, InState->Frames, *InWhat, InWhatIsThere ? *(TEXT(" ") + InWhatIsThere()) : TEXT("")));
				InState->bAlive = false;
				return true;
			}
			return false;
		});
	}

	void EnqueueDesignerPointerDrag(const FDesignerLatentRef& InState, FIntPoint InFrom, FIntPoint InTo, int32 InSteps, const FKey& InButton)
	{
		EnqueueDesignerPointerDrag(InState, [InFrom]() { return InFrom; }, [InTo]() { return InTo; }, InSteps, InButton);
	}

	void EnqueueDesignerPointerDrag(const FDesignerLatentRef& InState, TFunction<FIntPoint()> InFrom, TFunction<FIntPoint()> InTo,
		int32 InSteps, const FKey& InButton)
	{
		const int32 StepCount = FMath::Max(InSteps, 1);
		TSharedRef<int32> Frame = MakeShared<int32>(0);
		TSharedRef<FIntPoint> From = MakeShared<FIntPoint>(FIntPoint::ZeroValue);
		TSharedRef<FIntPoint> To = MakeShared<FIntPoint>(FIntPoint::ZeroValue);
		EnqueueDesignerStep([InState, InFrom, InTo, StepCount, InButton, Frame, From, To]()
		{
			if (!InState->bAlive || !InState->Driver.IsValid())
			{
				return true;
			}
			FDreamDesignerDriver& Driver = *InState->Driver;
			const int32 Now = (*Frame)++;
			++InState->Frames;
			if (Now == 0)
			{
				*From = InFrom();
				*To = InTo();
				Driver.MoveTo(*From);
			}
			else if (Now == 1)
			{
				Driver.Press(InButton);
			}
			else if (Now <= StepCount + 1)
			{
				const double Alpha = static_cast<double>(Now - 1) / StepCount;
				Driver.MoveTo(FIntPoint(
					From->X + FMath::RoundToInt32((To->X - From->X) * Alpha),
					From->Y + FMath::RoundToInt32((To->Y - From->Y) * Alpha)));
			}
			else if (Now == StepCount + 2)
			{
				// The frame the last move is applied on; finishing reads nothing new.
			}
			else if (Now == StepCount + 3)
			{
				Driver.Release(InButton);
			}
			else
			{
				return true;
			}
			return false;
		});
	}

	void EnqueueDesignerKeyboardFocus(const FDesignerLatentRef& InState, FAutomationTestBase* InTest, double InTimeoutSeconds)
	{
		TSharedRef<FString> WhyNot = MakeShared<FString>();
		EnqueueDesignerUntil(InState, InTest, [InState, WhyNot]()
		{
			return InState->Driver.IsValid() && InState->Driver->FocusForKeyboard(*WhyNot);
		}, InTimeoutSeconds, TEXT("Slate giving the designer viewport the keyboard"));
		EnqueueDesignerAction(InState, [InState]()
		{
			InState->Route = TEXT("through Slate: the keyboard focus on the designer viewport, the keys to it by FSlateApplication::ProcessKeyDownEvent");
		});
	}

	void EnqueueDesignerShortcut(const FDesignerLatentRef& InState, FAutomationTestBase* InTest, const FKey& InKey, EDreamDriverModifierKeys InModifiers)
	{
		EnqueueDesignerAction(InState, [InState, InTest, InKey, InModifiers]()
		{
			FString WhyNot;
			if (!InState->Driver->PressShortcut(InKey, InModifiers, WhyNot))
			{
				InTest->AddError(FString::Printf(TEXT("The shortcut could not be sent: %s."), *WhyNot));
				InState->bAlive = false;
			}
		});
	}

	void EnqueueDesignerTeardown(const FDesignerLatentRef& InState)
	{
		EnqueueDesignerStep([InState]()
		{
			if (InState->Driver.IsValid())
			{
				InState->Driver->ReleaseModifiers();
				InState->Driver->Close();
				InState->Driver.Reset();
			}
			return true;
		});
		EnqueueDesignerStep([InState]()
		{
			ReleaseDesignerTestAsset(InState->Asset);
			InState->Preferences.Reset();
			InState->CameraPreferences.Reset();
			for (const TFunction<void()>& Undo : InState->OnTeardown)
			{
				if (Undo)
				{
					Undo();
				}
			}
			InState->OnTeardown.Reset();
			return true;
		});
	}
}
