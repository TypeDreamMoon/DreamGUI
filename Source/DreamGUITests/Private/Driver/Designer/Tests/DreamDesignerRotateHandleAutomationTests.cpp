// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "DreamWidgetBlueprint.h"

#include "Editor.h"
#include "Editor/Transactor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

/*
 * The designer's rotate handle, grabbed and carried round with a pointer.
 *
 * UMG's designer has no such handle -- a UWidget is turned there through its render transform's angle -- so this one
 * takes the shape common design tools give it: a disc on a short stem above the middle of a single selection's top edge,
 * twenty-four viewport pixels off it along the widget's OWN up, so the handle turns with the widget. Carried round the
 * widget's pivot it turns the widget in its own plane by the angle the pointer went round, clockwise on screen being a
 * positive roll (FRotationMatrix carries a widget's right edge down as its roll grows). With Shift held the angle lands on
 * a whole number of fifteen degrees; the grid, which is about places, leaves angles alone. The angle is written to
 * RelativeRotationEuler, the face of the rotation a .dui spells (DreamGUI.Designer.ARotationAndAScaleReachTheTextFile),
 * through the same commit and flush the resize handles use, so a drag is one undo step and Esc during it puts the angle
 * back and leaves no step behind. Two widgets selected have no handle. The 3D view has the engine's gizmo instead, whose
 * test is DreamDesignerGizmoAutomationTests.cpp.
 *
 * As in DreamDesignerViewportGestureAutomationTests.cpp, every press, move, key and release goes in through
 * FSceneViewport's ISlateViewport entry points (FDreamDesignerDriver); the designer hit-tests its handle against the pixel
 * and its tick turns the held press into the turn. Where the handle stands and where the pointer is carried are worked
 * out from the widget's projected corners and pivot, and every expected angle is the one those pixels went round.
 */
namespace DreamDesignerRotateHandleTestLocal
{
	using namespace DreamTests;

	/** How far the designer stands the rotate handle off the middle of the top edge, in viewport pixels. */
	constexpr double HandleStandOff = 24.0;

	/** Where a selected widget's rotate handle stands, the pivot it turns the widget about and the rect's middle, in unrounded pixels. */
	struct FRotateGrip
	{
		FVector2D Handle = FVector2D::ZeroVector;
		FVector2D Pivot = FVector2D::ZeroVector;
		FVector2D Centre = FVector2D::ZeroVector;
		bool bFound = false;
	};

	/** The rotate handle's place, worked out as the designer works it out: off the top edge's middle, along the rect's up. */
	FRotateGrip FindRotateGrip(const FDreamDesignerDriver& InDriver, const UDreamWidget* InPreview)
	{
		FRotateGrip Grip;
		TArray<FVector2D> Corners;
		TArray<FVector2D> PivotPixels;
		if (!::IsValid(InPreview) || !InDriver.WidgetPixelCorners(InPreview, Corners)
			|| !InDriver.WorldToPixels(TArray<FVector>({ InPreview->GetWorldTransform().GetLocation() }), PivotPixels))
		{
			return Grip;
		}
		// Bottom-left, bottom-right, top-right, top-left of the widget's own rect.
		const FVector2D TopMiddle = (Corners[2] + Corners[3]) * 0.5;
		const FVector2D Up = (TopMiddle - (Corners[0] + Corners[1]) * 0.5).GetSafeNormal();
		Grip.Handle = TopMiddle + Up * HandleStandOff;
		Grip.Pivot = PivotPixels[0];
		Grip.Centre = (Corners[0] + Corners[1] + Corners[2] + Corners[3]) * 0.25;
		Grip.bFound = !Up.IsNearlyZero();
		return Grip;
	}

	FIntPoint RoundPixel(const FVector2D& InPixel)
	{
		return FIntPoint(FMath::RoundToInt32(InPixel.X), FMath::RoundToInt32(InPixel.Y));
	}

	/**
	 * The angle on screen, in degrees and clockwise, from InFrom round InPivot to InTo, in (-180, 180]. Pixel Y grows
	 * downwards, so the angle a pixel offset makes already counts clockwise.
	 */
	double ScreenTurn(const FVector2D& InPivot, const FVector2D& InFrom, const FVector2D& InTo)
	{
		const FVector2D From = InFrom - InPivot;
		const FVector2D To = InTo - InPivot;
		return FMath::RadiansToDegrees(FMath::Atan2(From.X * To.Y - From.Y * To.X, From.X * To.X + From.Y * To.Y));
	}

	/**
	 * InSteps pixels on the circle of InRadius round InPivot, from the direction InFrom lies in to InDegrees clockwise of
	 * it: a hand carrying the pointer round rather than across. The last one is where the drag lets go.
	 */
	TArray<FIntPoint> ArcAround(const FVector2D& InPivot, const FVector2D& InFrom, double InDegrees, double InRadius, int32 InSteps)
	{
		TArray<FIntPoint> Path;
		const FVector2D Offset = InFrom - InPivot;
		const double Start = FMath::Atan2(Offset.Y, Offset.X);
		const int32 StepCount = FMath::Max(InSteps, 1);
		for (int32 Step = 1; Step <= StepCount; ++Step)
		{
			const double Angle = Start + FMath::DegreesToRadians(InDegrees) * Step / StepCount;
			Path.Add(RoundPixel(InPivot + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * InRadius));
		}
		return Path;
	}

	/** The pointer to InPixel and the left button down there, with a frame after each, as a hand begins a drag. */
	void PressAt(FDreamDesignerDriver& InDriver, FIntPoint InPixel)
	{
		InDriver.MoveTo(InPixel);
		InDriver.PumpFrame();
		InDriver.Press(EKeys::LeftMouseButton);
		InDriver.PumpFrame();
	}

	/** The held pointer through InPath, a frame after each move: the designer's tick is where a move is applied. */
	void CarryThrough(FDreamDesignerDriver& InDriver, TConstArrayView<FIntPoint> InPath)
	{
		for (const FIntPoint& Pixel : InPath)
		{
			InDriver.MoveTo(Pixel);
			InDriver.PumpFrame();
		}
	}

	void LetGo(FDreamDesignerDriver& InDriver)
	{
		InDriver.Release(EKeys::LeftMouseButton);
		InDriver.PumpFrame();
	}

	/** Degrees between two rotations, whichever of the two quaternions for each it is written as. */
	double DegreesBetween(const FQuat& InA, const FQuat& InB)
	{
		return FMath::RadiansToDegrees(InA.AngularDistance(InB));
	}

	/** The undo stack as it stands: how long it is and which step is last, which between them change whenever a step is added. */
	struct FUndoMark
	{
		int32 Length = 0;
		const FTransaction* Last = nullptr;

		static FUndoMark Now()
		{
			FUndoMark Mark;
			if (GEditor != nullptr && GEditor->Trans != nullptr)
			{
				Mark.Length = GEditor->Trans->GetQueueLength();
				Mark.Last = Mark.Length > 0 ? GEditor->Trans->GetTransaction(Mark.Length - 1) : nullptr;
			}
			return Mark;
		}

		bool operator==(const FUndoMark& InOther) const
		{
			return Length == InOther.Length && Last == InOther.Last;
		}
	};

	/**
	 * A Widget Blueprint whose hierarchy is a .dui on disk, with its designer open and sized: the class a text author makes
	 * and opens. The file is under Saved/, where no DUI root is and so no source watcher looks, and the package under
	 * /Temp, both with a fresh suffix. The designer closes first, a Slate tick lets the close happen, and only then do the
	 * asset and the file go -- the order FScopedDesignerSession keeps, for its reason.
	 */
	struct FScopedDuiDesigner
	{
		FString FilePath;
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		TSharedPtr<FDreamDesignerDriver> Driver;
		FString Failure;

		FScopedDuiDesigner(const TCHAR* InName, const TArray<FString>& InLines)
		{
			const FString Unique = FString::Printf(TEXT("%s_%s"), InName, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
			FilePath = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), Unique + TEXT(".dui")));
			FPaths::NormalizeFilename(FilePath);
			if (!FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *FilePath))
			{
				Failure = FString::Printf(TEXT("the .dui could not be written to %s"), *FilePath);
				return;
			}
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), *Unique));
			if (Package == nullptr)
			{
				Failure = TEXT("the package could not be created");
				return;
			}
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTextUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr)
			{
				Failure = TEXT("the Widget Blueprint could not be created");
				return;
			}
			if (!DreamUITextAuthoring::SetAuthoredSourcePath(Blueprint, FilePath))
			{
				Failure = TEXT("the Widget Blueprint would not take the .dui as its source");
				return;
			}
			TSharedPtr<FDreamDesignerDriver> Opened = FDreamDesignerDriver::Open(Blueprint);
			if (!Opened.IsValid())
			{
				Failure = TEXT("the designer did not open, or opened without a viewport (see LogDreamDesignerDriver)");
				return;
			}
			if (!Opened->EnsureHeadlessSize(FIntPoint(1280, 720)))
			{
				Failure = FString::Printf(TEXT("the designer viewport could not be given a size; it measures %dx%d"),
					Opened->ViewportPixelSize().X, Opened->ViewportPixelSize().Y);
				Opened->Close();
				if (FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().Tick();
				}
				return;
			}
			Opened->PumpFrame();
			Driver = Opened;
		}

		~FScopedDuiDesigner()
		{
			if (Driver.IsValid())
			{
				Driver->Close();
				Driver.Reset();
				if (FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().Tick();
				}
			}
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
			if (!FilePath.IsEmpty())
			{
				// Quiet, and even read-only: a file left behind is read by the next run of this test.
				IFileManager::Get().Delete(*FilePath, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);
			}
		}

		FScopedDuiDesigner(const FScopedDuiDesigner&) = delete;
		FScopedDuiDesigner& operator=(const FScopedDuiDesigner&) = delete;

		bool IsReady() const
		{
			return Driver.IsValid();
		}

		/** The file as it is on disk now, which is what a reviewer, a diff and the next compile read. */
		FString ReadFile() const
		{
			FString Text;
			FFileHelper::LoadFileToString(Text, *FilePath);
			return Text;
		}

		UDreamWidget* FindTemplate(const FString& InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Blueprint != nullptr && ::IsValid(Blueprint->WidgetTree))
			{
				Blueprint->WidgetTree->ForEachWidget([&Found, &InDisplayName](UDreamWidget* InWidget)
				{
					if (Found == nullptr && InWidget->GetDisplayName() == InDisplayName)
					{
						Found = InWidget;
					}
				});
			}
			return Found;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRotateHandleQuarterTurnTest,
	"DreamGUI.Designer.Driver.CarryingTheRotateHandleAQuarterTurnClockwiseTurnsTheWidgetNinetyDegreesAndOneUndoTurnsItBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A selected widget's rotate handle, carried a quarter turn clockwise round the widget's pivot with the grid off. The
 * widget turns by the angle the pointer went round -- ninety degrees clockwise on screen, a roll of +90 -- in the preview
 * and in the asset, where the euler the .dui spells and the quaternion the asset keeps say the same; its pivot stays
 * where it was. The drag is one undo step, and that step turns the widget back.
 */
bool FDreamDesignerRotateHandleQuarterTurnTest::RunTest(const FString&)
{
	using namespace DreamDesignerRotateHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDesignerSession Session(TEXT("DesignerRotateQuarterTurn"), /*bGiveRootAPanel*/ true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Template = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.4, 0.55));
	if (Template == nullptr)
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Template);
	Driver.PumpFrame();
	const FRotateGrip Grip = FindRotateGrip(Driver, Driver.PreviewFor(Template));
	if (!TestTrue(FString::Printf(TEXT("The selected widget's rotate handle is on screen at %s"), *Grip.Handle.ToString()),
		Grip.bFound && WorkArea.IsInside(Grip.Handle)))
	{
		return false;
	}
	const double RollBefore = Template->GetRelativeRotationEuler().Roll;
	const FQuat RotationBefore = Template->GetRelativeRotation();
	const FVector2D PivotPlaceBefore = Template->GetAnchoredPosition();
	const FIntPoint Press = RoundPixel(Grip.Handle);
	const TArray<FIntPoint> Path = ArcAround(Grip.Pivot, FVector2D(Press), 90.0, FVector2D::Distance(FVector2D(Press), Grip.Pivot), /*Steps*/ 6);

	PressAt(Driver, Press);
	CarryThrough(Driver, Path);
	LetGo(Driver);

	const double Turned = ScreenTurn(Grip.Pivot, FVector2D(Press), FVector2D(Path.Last()));
	const double Expected = RollBefore + Turned;
	const double Roll = Template->GetRelativeRotationEuler().Roll;
	TestTrue(FString::Printf(TEXT("The pointer went a quarter turn clockwise round the pivot: %.3f degrees"), Turned),
		FMath::IsNearlyEqual(Turned, 90.0, 1.0));
	TestTrue(FString::Printf(TEXT("The asset's angle is the angle the pointer went round: expected %.3f, holds %.3f"), Expected, Roll),
		FMath::IsNearlyEqual(Roll, Expected, 0.1));
	TestTrue(FString::Printf(TEXT("...which is ninety degrees: %.3f"), Roll), FMath::IsNearlyEqual(Roll, 90.0, 1.0));
	const double QuatGap = DegreesBetween(Template->GetRelativeRotation(), FRotator(0.0, 0.0, Expected).Quaternion());
	TestTrue(FString::Printf(TEXT("...and so is the rotation the asset keeps, %.4f degrees from it"), QuatGap), QuatGap < 0.1);
	const UDreamWidget* Preview = Driver.PreviewFor(Template);
	TestTrue(TEXT("The preview shows the asset's angle"),
		Preview != nullptr && FMath::IsNearlyEqual(Preview->GetRelativeRotationEuler().Roll, Roll, 1e-3));
	TestTrue(FString::Printf(TEXT("The widget turned about its pivot, which stayed where it was: %s, was %s"),
		*Template->GetAnchoredPosition().ToString(), *PivotPlaceBefore.ToString()),
		Template->GetAnchoredPosition().Equals(PivotPlaceBefore, 0.01));

	TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
	Driver.PumpFrame();
	TestTrue(FString::Printf(TEXT("One undo turns the widget back: %.3f degrees from where it was"),
		DegreesBetween(Template->GetRelativeRotation(), RotationBefore)),
		DegreesBetween(Template->GetRelativeRotation(), RotationBefore) < 0.01
		&& FMath::IsNearlyEqual(Template->GetRelativeRotationEuler().Roll, RollBefore, 1e-3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRotateHandleShiftStepTest,
	"DreamGUI.Designer.Driver.WithShiftHeldTheRotateHandleLandsOnTheFifteenDegreeStepNearestThePointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The rotate handle carried round to thirty-seven degrees, Shift going down once the drag is under way -- when a hand
 * reaches for it -- and still held when the button is let go. The widget's angle lands on the whole number of fifteen
 * degrees nearest what the pointer said, thirty rather than forty-five, as design tools step a Shift-held turn; the asset
 * holds the step exactly.
 */
bool FDreamDesignerRotateHandleShiftStepTest::RunTest(const FString&)
{
	using namespace DreamDesignerRotateHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDesignerSession Session(TEXT("DesignerRotateShiftStep"), /*bGiveRootAPanel*/ true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Template = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.45, 0.55));
	if (Template == nullptr)
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Template);
	Driver.PumpFrame();
	const FRotateGrip Grip = FindRotateGrip(Driver, Driver.PreviewFor(Template));
	if (!TestTrue(FString::Printf(TEXT("The selected widget's rotate handle is on screen at %s"), *Grip.Handle.ToString()),
		Grip.bFound && WorkArea.IsInside(Grip.Handle)))
	{
		return false;
	}
	const double RollBefore = Template->GetRelativeRotationEuler().Roll;
	const FIntPoint Press = RoundPixel(Grip.Handle);
	const TArray<FIntPoint> Path = ArcAround(Grip.Pivot, FVector2D(Press), 37.0, FVector2D::Distance(FVector2D(Press), Grip.Pivot), /*Steps*/ 4);

	PressAt(Driver, Press);
	CarryThrough(Driver, MakeArrayView(Path).Left(1));
	TestTrue(TEXT("Shift goes down with the drag under way"), Driver.HoldModifiers(EDreamDriverModifierKeys::Shift));
	CarryThrough(Driver, MakeArrayView(Path).RightChop(1));
	LetGo(Driver);
	TestTrue(TEXT("...and comes up after the button"), Driver.ReleaseModifiers());
	Driver.PumpFrame();

	const double PointerAngle = RollBefore + ScreenTurn(Grip.Pivot, FVector2D(Press), FVector2D(Path.Last()));
	const double Step = FMath::GridSnap(PointerAngle, 15.0);
	const double Roll = Template->GetRelativeRotationEuler().Roll;
	TestTrue(FString::Printf(TEXT("The pointer said about thirty-seven degrees: %.3f"), PointerAngle), FMath::IsNearlyEqual(PointerAngle, 37.0, 1.5));
	TestTrue(FString::Printf(TEXT("...whose nearest fifteen-degree step is thirty: %.1f"), Step), FMath::IsNearlyEqual(Step, 30.0, 1e-6));
	TestTrue(FString::Printf(TEXT("The asset's angle landed on that step: holds %.4f"), Roll), FMath::IsNearlyEqual(Roll, Step, 1e-6));
	const double QuatGap = DegreesBetween(Template->GetRelativeRotation(), FRotator(0.0, 0.0, Step).Quaternion());
	TestTrue(FString::Printf(TEXT("...and so did the rotation it keeps, %.4f degrees from it"), QuatGap), QuatGap < 1e-3);
	TestFalse(TEXT("No transaction is left open"), GEditor->IsTransactionActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRotateHandleIgnoresGridTest,
	"DreamGUI.Designer.Driver.WithGridSnappingOnTheRotateHandleStillLeavesTheAngleWhereThePointerPutIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The grid on at twenty-five units and the rotate handle carried round to thirty-seven degrees, no Shift. The widget's
 * angle is the pointer's, thirty-seven, not thirty or forty-five: the grid snaps places -- a move's position, a resize's
 * edge -- and only Shift steps an angle.
 */
bool FDreamDesignerRotateHandleIgnoresGridTest::RunTest(const FString&)
{
	using namespace DreamDesignerRotateHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(true, 25.0f);
	FScopedDesignerSession Session(TEXT("DesignerRotateIgnoresGrid"), /*bGiveRootAPanel*/ true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Template = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.45, 0.55));
	if (Template == nullptr)
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Template);
	Driver.PumpFrame();
	const FRotateGrip Grip = FindRotateGrip(Driver, Driver.PreviewFor(Template));
	if (!TestTrue(FString::Printf(TEXT("The selected widget's rotate handle is on screen at %s"), *Grip.Handle.ToString()),
		Grip.bFound && WorkArea.IsInside(Grip.Handle)))
	{
		return false;
	}
	const double RollBefore = Template->GetRelativeRotationEuler().Roll;
	const FIntPoint Press = RoundPixel(Grip.Handle);
	const TArray<FIntPoint> Path = ArcAround(Grip.Pivot, FVector2D(Press), 37.0, FVector2D::Distance(FVector2D(Press), Grip.Pivot), /*Steps*/ 4);

	PressAt(Driver, Press);
	CarryThrough(Driver, Path);
	LetGo(Driver);

	const double Expected = RollBefore + ScreenTurn(Grip.Pivot, FVector2D(Press), FVector2D(Path.Last()));
	const double Roll = Template->GetRelativeRotationEuler().Roll;
	TestTrue(FString::Printf(TEXT("The asset's angle is the pointer's: expected %.3f, holds %.3f"), Expected, Roll),
		FMath::IsNearlyEqual(Roll, Expected, 0.1));
	TestTrue(FString::Printf(TEXT("...not a fifteen-degree step: %.3f"), Roll),
		!FMath::IsNearlyEqual(Roll, 30.0, 1.0) && !FMath::IsNearlyEqual(Roll, 45.0, 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRotateHandleMultiSelectionTest,
	"DreamGUI.Designer.Driver.WithTwoWidgetsSelectedThereIsNoRotateHandleAndADragFromWhereItWouldStandTurnsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Two widgets side by side, both selected. Where one of them alone would have had its rotate handle there is none: a
 * press there, carried a quarter turn round that widget's pivot, turns neither widget -- in the asset or in the preview.
 * A selection is turned as one shape only by the 3D view's gizmo, about the selection's centre; the 2D handle belongs to
 * a single widget and its own pivot.
 */
bool FDreamDesignerRotateHandleMultiSelectionTest::RunTest(const FString&)
{
	using namespace DreamDesignerRotateHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDesignerSession Session(TEXT("DesignerRotateMultiSelection"), /*bGiveRootAPanel*/ true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	// Level with each other, so the selection's box has its top where each widget's is and the handle's place is outside it.
	UDreamWidget* Left = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.3, 0.55));
	UDreamWidget* Right = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.7, 0.55));
	if (Left == nullptr || Right == nullptr)
	{
		return false;
	}
	// Where the left widget's handle stands while it is selected alone.
	SelectOnlyInDesigner(Driver, Left);
	Driver.PumpFrame();
	const FRotateGrip Grip = FindRotateGrip(Driver, Driver.PreviewFor(Left));
	if (!TestTrue(FString::Printf(TEXT("The left widget's rotate handle would be on screen at %s"), *Grip.Handle.ToString()),
		Grip.bFound && WorkArea.IsInside(Grip.Handle)))
	{
		return false;
	}
	UDreamWidget* LeftPreview = Driver.PreviewFor(Left);
	UDreamWidget* RightPreview = Driver.PreviewFor(Right);
	if (!TestTrue(TEXT("Both widgets have previews"), LeftPreview != nullptr && RightPreview != nullptr))
	{
		return false;
	}
	Driver.Toolkit()->SelectWidgets(TSet<UDreamWidget*>({ LeftPreview, RightPreview }), /*bAppendOrToggle*/ false);
	Driver.PumpFrame();
	if (!TestEqual(TEXT("Both are selected"), Driver.SelectedWidgets().Num(), 2))
	{
		return false;
	}
	const FQuat LeftBefore = Left->GetRelativeRotation();
	const FQuat RightBefore = Right->GetRelativeRotation();
	const double LeftRollBefore = Left->GetRelativeRotationEuler().Roll;
	const double RightRollBefore = Right->GetRelativeRotationEuler().Roll;
	const FIntPoint Press = RoundPixel(Grip.Handle);

	PressAt(Driver, Press);
	CarryThrough(Driver, ArcAround(Grip.Pivot, FVector2D(Press), 90.0, FVector2D::Distance(FVector2D(Press), Grip.Pivot), /*Steps*/ 6));
	LetGo(Driver);

	TestTrue(FString::Printf(TEXT("The left widget did not turn: %.4f degrees, roll %.4f, was %.4f"),
		DegreesBetween(Left->GetRelativeRotation(), LeftBefore), Left->GetRelativeRotationEuler().Roll, LeftRollBefore),
		DegreesBetween(Left->GetRelativeRotation(), LeftBefore) < 1e-3 && FMath::IsNearlyEqual(Left->GetRelativeRotationEuler().Roll, LeftRollBefore, 1e-6));
	TestTrue(FString::Printf(TEXT("...nor did the right one: %.4f degrees, roll %.4f, was %.4f"),
		DegreesBetween(Right->GetRelativeRotation(), RightBefore), Right->GetRelativeRotationEuler().Roll, RightRollBefore),
		DegreesBetween(Right->GetRelativeRotation(), RightBefore) < 1e-3 && FMath::IsNearlyEqual(Right->GetRelativeRotationEuler().Roll, RightRollBefore, 1e-6));
	const UDreamWidget* LeftNow = Driver.PreviewFor(Left);
	const UDreamWidget* RightNow = Driver.PreviewFor(Right);
	TestTrue(TEXT("...and neither preview turned"),
		LeftNow != nullptr && RightNow != nullptr
		&& DegreesBetween(LeftNow->GetRelativeRotation(), LeftBefore) < 1e-3
		&& DegreesBetween(RightNow->GetRelativeRotation(), RightBefore) < 1e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRotateHandleFollowsTheTurnTest,
	"DreamGUI.Designer.Driver.ATurnedWidgetsRotateHandleStandsOffItsOwnTopEdgeAndCarryingItTurnsTheWidgetOnFromThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A widget already turned a quarter clockwise, the way its details turn it -- the angle on the preview, then committed to
 * the asset. Its top edge now faces the right of the screen, and the rotate handle stands off that edge: to the right of
 * the widget, level with its middle. Carried a further quarter turn clockwise from there, round the pivot, it turns the
 * widget on to a half turn, counted from the angle it had.
 */
bool FDreamDesignerRotateHandleFollowsTheTurnTest::RunTest(const FString&)
{
	using namespace DreamDesignerRotateHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDesignerSession Session(TEXT("DesignerRotateFollowsTheTurn"), /*bGiveRootAPanel*/ true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Template = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.4, 0.5));
	if (Template == nullptr)
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Template);
	Driver.PumpFrame();
	UDreamWidget* Preview = Driver.PreviewFor(Template);
	if (!TestNotNull(TEXT("The widget has a preview"), Preview))
	{
		return false;
	}
	Preview->SetRelativeRotationEuler(FRotator(0.0, 0.0, 90.0));
	Driver.Toolkit()->CommitWidgetGeometryToTemplate(TArray<UDreamWidget*>({ Preview }));
	Driver.PumpFrame();
	if (!TestTrue(FString::Printf(TEXT("The asset holds the quarter turn: %.3f"), Template->GetRelativeRotationEuler().Roll),
		FMath::IsNearlyEqual(Template->GetRelativeRotationEuler().Roll, 90.0, 1e-3)))
	{
		return false;
	}
	// Asked again, and selected again: the commit may have had the preview rebuilt.
	SelectOnlyInDesigner(Driver, Template);
	Driver.PumpFrame();
	Preview = Driver.PreviewFor(Template);
	const FRotateGrip Grip = FindRotateGrip(Driver, Preview);
	if (!TestTrue(FString::Printf(TEXT("The turned widget's rotate handle is on screen at %s"), *Grip.Handle.ToString()),
		Grip.bFound && WorkArea.IsInside(Grip.Handle)))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("The handle stands to the right of the widget, past its own stand-off: handle %s, middle %s"),
		*Grip.Handle.ToString(), *Grip.Centre.ToString()), Grip.Handle.X > Grip.Centre.X + HandleStandOff);
	TestTrue(FString::Printf(TEXT("...level with its middle: %.2f pixels apart"), Grip.Handle.Y - Grip.Centre.Y),
		FMath::Abs(Grip.Handle.Y - Grip.Centre.Y) < 1.5);
	const double RollBefore = Preview != nullptr ? Preview->GetRelativeRotationEuler().Roll : 90.0;
	const FIntPoint Press = RoundPixel(Grip.Handle);
	const TArray<FIntPoint> Path = ArcAround(Grip.Pivot, FVector2D(Press), 90.0, FVector2D::Distance(FVector2D(Press), Grip.Pivot), /*Steps*/ 6);

	PressAt(Driver, Press);
	CarryThrough(Driver, Path);
	LetGo(Driver);

	const double Expected = RollBefore + ScreenTurn(Grip.Pivot, FVector2D(Press), FVector2D(Path.Last()));
	const double Roll = Template->GetRelativeRotationEuler().Roll;
	TestTrue(FString::Printf(TEXT("The drag turned the widget on from the quarter turn: expected %.3f, holds %.3f"), Expected, Roll),
		FMath::IsNearlyEqual(Roll, Expected, 0.1));
	TestTrue(FString::Printf(TEXT("...to a half turn: %.3f"), Roll), FMath::IsNearlyEqual(Roll, 180.0, 1.0));
	const double QuatGap = DegreesBetween(Template->GetRelativeRotation(), FRotator(0.0, 0.0, Expected).Quaternion());
	TestTrue(FString::Printf(TEXT("...and so is the rotation the asset keeps, %.4f degrees from it"), QuatGap), QuatGap < 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRotateHandleEscapeTest,
	"DreamGUI.Designer.Driver.EscapeHalfwayThroughARotateDragPutsTheAngleBackAndLeavesNothingToUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The rotate handle carried half of a quarter turn, the widget visibly turning, then Escape with the button still down:
 * the angle goes back to exactly what it was -- the euler and the quaternion both, in the preview and in the asset --
 * and the rest of the carry and the release turn nothing, as Esc ends the resize handles' drags and the level editor's
 * gizmo drag. Nothing is left open on the transaction stack and no step was added to it.
 */
bool FDreamDesignerRotateHandleEscapeTest::RunTest(const FString&)
{
	using namespace DreamDesignerRotateHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDesignerSession Session(TEXT("DesignerRotateEscape"), /*bGiveRootAPanel*/ true);
	if (!Session.IsReady())
	{
		AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
		return false;
	}
	FDreamDesignerDriver& Driver = Session.GetDriver();
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Template = DropPlainWidgetAt(*this, Driver, PointInBox(WorkArea, 0.4, 0.55));
	if (Template == nullptr)
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Template);
	Driver.PumpFrame();
	const UDreamWidget* PreviewBefore = Driver.PreviewFor(Template);
	const FRotateGrip Grip = FindRotateGrip(Driver, PreviewBefore);
	if (!TestTrue(FString::Printf(TEXT("The selected widget's rotate handle is on screen at %s"), *Grip.Handle.ToString()),
		Grip.bFound && WorkArea.IsInside(Grip.Handle) && PreviewBefore != nullptr))
	{
		return false;
	}
	const FQuat TemplateRotation = Template->GetRelativeRotation();
	const FRotator TemplateEuler = Template->GetRelativeRotationEuler();
	const FQuat PreviewRotation = PreviewBefore->GetRelativeRotation();
	const FRotator PreviewEuler = PreviewBefore->GetRelativeRotationEuler();
	const FIntPoint Press = RoundPixel(Grip.Handle);
	const TArray<FIntPoint> Path = ArcAround(Grip.Pivot, FVector2D(Press), 90.0, FVector2D::Distance(FVector2D(Press), Grip.Pivot), /*Steps*/ 6);
	const FUndoMark Before = FUndoMark::Now();

	PressAt(Driver, Press);
	CarryThrough(Driver, MakeArrayView(Path).Left(3));
	const UDreamWidget* Turning = Driver.PreviewFor(Template);
	TestTrue(FString::Printf(TEXT("Halfway, the preview is turning: roll %.3f"), Turning != nullptr ? Turning->GetRelativeRotationEuler().Roll : 0.0),
		Turning != nullptr && FMath::Abs(Turning->GetRelativeRotationEuler().Roll - PreviewEuler.Roll) > 10.0);

	TestTrue(TEXT("Escape goes down"), Driver.KeyDown(EKeys::Escape));
	Driver.PumpFrame();
	Driver.KeyUp(EKeys::Escape);
	const UDreamWidget* Restored = Driver.PreviewFor(Template);
	TestTrue(TEXT("Escape puts the preview's angle back exactly, the euler and the quaternion"),
		Restored != nullptr
		&& Restored->GetRelativeRotationEuler().Euler().Equals(PreviewEuler.Euler(), 1e-9)
		&& Restored->GetRelativeRotation().Equals(PreviewRotation, 1e-9));

	// The button is still down, and the rest of the way round turns nothing: the drag is over.
	CarryThrough(Driver, MakeArrayView(Path).RightChop(3));
	LetGo(Driver);
	const UDreamWidget* After = Driver.PreviewFor(Template);
	TestTrue(TEXT("The rest of the carry and the release leave the preview where Escape put it"),
		After != nullptr && After->GetRelativeRotationEuler().Euler().Equals(PreviewEuler.Euler(), 1e-9)
		&& After->GetRelativeRotation().Equals(PreviewRotation, 1e-9));
	TestTrue(FString::Printf(TEXT("The asset's angle never moved: roll %.4f, was %.4f"), Template->GetRelativeRotationEuler().Roll, TemplateEuler.Roll),
		Template->GetRelativeRotationEuler().Euler().Equals(TemplateEuler.Euler(), 1e-9)
		&& Template->GetRelativeRotation().Equals(TemplateRotation, 1e-9));
	TestFalse(TEXT("No transaction is left open"), GEditor->IsTransactionActive());
	TestTrue(TEXT("...and none was added to undo"), FUndoMark::Now() == Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRotateHandleWritesTheDuiTest,
	"DreamGUI.Designer.Driver.TurningAWidgetOfADuiBackedClassWithTheRotateHandleWritesTheAngleIntoTheDui",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A class whose hierarchy is a .dui, its one widget turned a quarter clockwise by the rotate handle with Shift held (so
 * the angle is a whole ninety and its spelling is exact). The file on disk takes the angle on the widget's own lines,
 * spelled as the euler -- `RelativeRotationEuler = (0, 0, 90)`, what the write-back prints for a rotation
 * (DreamGUI.Designer.ARotationAndAScaleReachTheTextFile) -- and the one undo the drag is takes the file back too: the
 * asset and the file are one step.
 */
bool FDreamDesignerRotateHandleWritesTheDuiTest::RunTest(const FString&)
{
	using namespace DreamDesignerRotateHandleTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	FScopedDuiDesigner Scoped(TEXT("DesignerRotateDui"), {
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    Widget Spinner {"),
		TEXT("        AnchorData.SizeDelta = (160, 100)"),
		TEXT("    }"),
		TEXT("}")
	});
	if (!Scoped.IsReady())
	{
		AddError(FString::Printf(TEXT("No text-authored designer to drive: %s."), *Scoped.Failure));
		return false;
	}
	FDreamDesignerDriver& Driver = *Scoped.Driver;
	if (!TestTrue(TEXT("The class is text-authored"), DreamUITextAuthoring::IsTextAuthored(Scoped.Blueprint)))
	{
		return false;
	}
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Template = Scoped.FindTemplate(TEXT("Spinner"));
	if (!TestNotNull(TEXT("The file's widget is in the asset"), Template))
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Template);
	Driver.PumpFrame();
	const FRotateGrip Grip = FindRotateGrip(Driver, Driver.PreviewFor(Template));
	if (!TestTrue(FString::Printf(TEXT("The widget's rotate handle is on screen at %s"), *Grip.Handle.ToString()),
		Grip.bFound && WorkArea.IsInside(Grip.Handle)))
	{
		return false;
	}
	const FString TextBefore = Scoped.ReadFile();
	if (!TestFalse(TEXT("The file says nothing of a rotation to begin with"), TextBefore.Contains(TEXT("RelativeRotationEuler"))))
	{
		return false;
	}
	const FIntPoint Press = RoundPixel(Grip.Handle);

	TestTrue(TEXT("Shift goes down"), Driver.HoldModifiers(EDreamDriverModifierKeys::Shift));
	PressAt(Driver, Press);
	CarryThrough(Driver, ArcAround(Grip.Pivot, FVector2D(Press), 90.0, FVector2D::Distance(FVector2D(Press), Grip.Pivot), /*Steps*/ 6));
	LetGo(Driver);
	TestTrue(TEXT("...and comes up"), Driver.ReleaseModifiers());
	Driver.PumpFrame();

	TestTrue(FString::Printf(TEXT("The asset holds the quarter turn: %.4f"), Template->GetRelativeRotationEuler().Roll),
		FMath::IsNearlyEqual(Template->GetRelativeRotationEuler().Roll, 90.0, 1e-6));
	const FString TextAfter = Scoped.ReadFile();
	if (!TestTrue(TEXT("The file on disk takes the angle, spelled as the euler"), TextAfter.Contains(TEXT("RelativeRotationEuler = (0, 0, 90)"))))
	{
		AddInfo(FString::Printf(TEXT("The file reads:\n%s"), *TextAfter));
	}

	TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
	Driver.PumpFrame();
	const FString TextUndone = Scoped.ReadFile();
	if (!TestFalse(TEXT("The one undo takes the file's angle back with the asset's"), TextUndone.Contains(TEXT("RelativeRotationEuler"))))
	{
		AddInfo(FString::Printf(TEXT("The file reads:\n%s"), *TextUndone));
	}
	TestTrue(FString::Printf(TEXT("...and the asset's: roll %.4f"), Template->GetRelativeRotationEuler().Roll),
		FMath::IsNearlyZero(Template->GetRelativeRotationEuler().Roll, 1e-3));
	return true;
}

#endif
