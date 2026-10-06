// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetDesignerViewportClient.h"

#include "Editor.h"
#include "EditorViewportClient.h"
#include "HitProxies.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Slate/SceneViewport.h"
#include "UnrealWidget.h"

/*
 * The 3D view's rotation gizmo, grabbed and turned with a pointer.
 *
 * UMG's designer has no rotation handle: a widget is turned there through its details. This designer's 2D view has a
 * rotate handle of its own (DreamDesignerRotateHandleAutomationTests.cpp), and its 3D view the engine's transform gizmo
 * (FDreamWidgetDesignerViewportClient::GetWidgetMode hands the perspective view the engine's own W/E/R gizmo, and
 * InputWidgetDelta turns the selection by what it reports). So this is the level editor's rotate gesture, made in the
 * designer: the ring found where it is drawn, by the viewport's own hit proxies, hovered -- Slate asks the viewport for a
 * cursor every frame, and that query is where an editor viewport learns which axis is under the pointer -- pressed,
 * dragged along the ring and let go. The widget turns in its own plane, the asset takes the turn, and one undo takes it
 * back.
 *
 * The view, the gizmo's mode and the rotation grid are set directly: they are how the scene is arranged, and not what is
 * being tested. NonNullRHI, because the hit proxies are drawn: under -nullrhi the map reads back as zeros.
 */
namespace DreamDesignerGizmoTestLocal
{
	using namespace DreamTests;

	/** The rotation grid off for the test, so a turn is what the pointer made and not the nearest step of a grid. */
	struct FScopedRotationGridOff
	{
		bool bWasEnabled = true;

		FScopedRotationGridOff()
		{
			ULevelEditorViewportSettings* Settings = GetMutableDefault<ULevelEditorViewportSettings>();
			bWasEnabled = Settings->RotGridEnabled != 0;
			Settings->RotGridEnabled = false;
		}

		~FScopedRotationGridOff()
		{
			GetMutableDefault<ULevelEditorViewportSettings>()->RotGridEnabled = bWasEnabled;
		}
	};

	/** Where the ring was found, and which way along it a drag turns the widget. */
	struct FRingGrip
	{
		FIntPoint Pixel = FIntPoint::ZeroValue;
		FVector2D Tangent = FVector2D::ZeroVector;
		bool bFound = false;
		FString Searched;
	};

	/** The gizmo's axis under InPixel, if the hit proxy there is one of its axes. */
	bool IsAxisAt(FViewport& InViewport, FIntPoint InPixel, EAxisList::Type InAxis)
	{
		HHitProxy* Hit = InViewport.GetHitProxy(InPixel.X, InPixel.Y);
		return Hit != nullptr && Hit->IsA(HWidgetAxis::StaticGetType()) && static_cast<HWidgetAxis*>(Hit)->Axis == InAxis;
	}

	/**
	 * A pixel on the ring that turns the widget in its own plane -- rotation about its X, the axis its face points down --
	 * walked out from the gizmo's centre along a dozen directions, and kept away from the selection's resize handles,
	 * which take a press within nine pixels of them before the gizmo can.
	 */
	FRingGrip FindRing(FDreamDesignerDriver& InDriver, const UDreamWidget* InPreview)
	{
		FRingGrip Grip;
		TSharedPtr<FSceneViewport> Viewport = InDriver.SceneViewport();
		const TOptional<FIntPoint> Centre = InDriver.WidgetPixel(InPreview);
		const TOptional<FBox2D> Rect = InDriver.WidgetPixelRect(InPreview);
		if (!Viewport.IsValid() || !Centre.IsSet() || !Rect.IsSet())
		{
			Grip.Searched = TEXT("the widget has no place on screen to search around");
			return Grip;
		}
		const FIntPoint Size = InDriver.ViewportPixelSize();
		const FVector2D Handles[] = {
			Rect->Min, Rect->Max, FVector2D(Rect->Min.X, Rect->Max.Y), FVector2D(Rect->Max.X, Rect->Min.Y),
			FVector2D(Rect->GetCenter().X, Rect->Min.Y), FVector2D(Rect->GetCenter().X, Rect->Max.Y),
			FVector2D(Rect->Min.X, Rect->GetCenter().Y), FVector2D(Rect->Max.X, Rect->GetCenter().Y) };
		const double Degrees[] = { 30.0, 60.0, 120.0, 150.0, 210.0, 240.0, 300.0, 330.0, 0.0, 90.0, 180.0, 270.0 };
		int32 Looked = 0;
		for (const double Angle : Degrees)
		{
			const FVector2D Direction(FMath::Cos(FMath::DegreesToRadians(Angle)), FMath::Sin(FMath::DegreesToRadians(Angle)));
			for (int32 Radius = 8; Radius < 600; ++Radius)
			{
				const FIntPoint Pixel(
					Centre->X + FMath::RoundToInt32(Direction.X * Radius),
					Centre->Y + FMath::RoundToInt32(Direction.Y * Radius));
				if (Pixel.X < 1 || Pixel.Y < 1 || Pixel.X >= Size.X - 1 || Pixel.Y >= Size.Y - 1)
				{
					break;
				}
				++Looked;
				if (!IsAxisAt(*Viewport, Pixel, EAxisList::X))
				{
					continue;
				}
				bool bClearOfHandles = true;
				for (const FVector2D& Handle : Handles)
				{
					bClearOfHandles &= FVector2D::Distance(Handle, FVector2D(Pixel)) > 14.0;
				}
				if (!bClearOfHandles)
				{
					continue;
				}
				Grip.Pixel = Pixel;
				// Along the ring, a quarter turn from the way out to it.
				Grip.Tangent = FVector2D(-Direction.Y, Direction.X);
				Grip.bFound = true;
				Grip.Searched = FString::Printf(TEXT("found at (%d, %d), %d px out at %.0f degrees, after %d pixels"), Pixel.X, Pixel.Y, Radius, Angle, Looked);
				return Grip;
			}
		}
		Grip.Searched = FString::Printf(TEXT("no pixel of the X ring clear of the resize handles in %d pixels looked at around (%d, %d)"),
			Looked, Centre->X, Centre->Y);
		return Grip;
	}

	/** The angle a rotation turns by, in degrees, and the axis it turns about. */
	void AngleAndAxis(const FQuat& InRotation, double& OutDegrees, FVector& OutAxis)
	{
		double Radians = 0.0;
		InRotation.ToAxisAndAngle(OutAxis, Radians);
		OutDegrees = FMath::RadiansToDegrees(Radians);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerGizmoRotateTest,
	"DreamGUI.Designer.RHI.DraggingTheRotateGizmosRingInThe3DViewTurnsTheWidgetInItsPlaneAndUndoTurnsItBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerGizmoRotateTest::RunTest(const FString&)
{
	using namespace DreamDesignerGizmoTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerGizmoRotate"));
	// Held by the teardown, which lets it go -- and the grid with it -- once the designer has closed.
	TSharedPtr<FScopedRotationGridOff> RotationGrid = MakeShared<FScopedRotationGridOff>();
	State->OnTeardown.Add([RotationGrid]() mutable { RotationGrid.Reset(); });
	TSharedRef<FRingGrip> Grip = MakeShared<FRingGrip>();
	TSharedRef<FQuat> RotationBefore = MakeShared<FQuat>(FQuat::Identity);
	EnqueueDesignerFrames(State, 2, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, RotationBefore]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		UDreamWidget* Template = DropOntoRootAndFindTemplate(Driver, /*Plain Widget row*/ nullptr, Driver.ViewportCentrePixel());
		FEditorViewportClient* Client = Driver.ViewportClient();
		if (!TestNotNull(TEXT("A plain widget was dropped under the root"), Template) || Client == nullptr)
		{
			State->bAlive = false;
			return;
		}
		State->Made.Add(TEXT("Target"), Template);
		*RotationBefore = Template->GetRelativeRotation();
		SelectOnlyInDesigner(Driver, Template);
		// The 3D view, looking at the canvas square on, with the gizmo in its rotate mode.
		Client->SetViewportType(LVT_Perspective);
		if (FDreamWidgetDesignerViewportClient* Designer = static_cast<FDreamWidgetDesignerViewportClient*>(Client); Designer->CanFrameFromCanvasEye())
		{
			Designer->FrameFromCanvasEye();
		}
		else
		{
			Designer->FocusViewportToTargets();
		}
		Client->SetWidgetMode(UE::Widget::WM_Rotate);
	});
	EnqueueDesignerUntil(State, this, [State]()
	{
		FEditorViewportClient* Client = State->Driver->ViewportClient();
		return Client != nullptr && Client->IsPerspective() && !Client->GetViewTransform().IsPlaying()
			&& Client->GetWidgetMode() == UE::Widget::WM_Rotate;
	}, 3.0, TEXT("the 3D view standing still with the rotate gizmo up"), /*bInDraw*/ true);
	EnqueueDesignerFrames(State, 2, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, Grip]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		*Grip = FindRing(Driver, Driver.PreviewFor(State->Get(TEXT("Target"))));
		AddInfo(FString::Printf(TEXT("The rotate gizmo's X ring: %s."), *Grip->Searched));
		if (!TestTrue(TEXT("The rotate gizmo's in-plane ring is drawn where a pointer can grab it"), Grip->bFound))
		{
			State->bAlive = false;
			return;
		}
		// Onto the ring.
		const bool bMoved = Driver.MoveTo(Grip->Pixel);
		Grip->Searched += FString::Printf(TEXT("; the move %s"), bMoved ? TEXT("taken") : TEXT("not taken"));
	});
	// Asked for a cursor every frame, as Slate asks the widget under the pointer every frame, until the hover lands: the
	// query is where an editor viewport notes the hit proxy under the pointer, and its next tick makes that the axis.
	EnqueueDesignerUntil(State, this, [State]()
	{
		FEditorViewportClient* Client = State->Driver->ViewportClient();
		State->Driver->QueryCursor();
		return Client != nullptr && Client->GetCurrentWidgetAxis() == EAxisList::X;
	}, 2.0, TEXT("the pointer over the ring hovering its axis"), /*bInDraw*/ true, [State, Grip]()
	{
		// Where the hover was looked for: the viewport's own cursor and what its hit proxies have there.
		FEditorViewportClient* Client = State->Driver->ViewportClient();
		TSharedPtr<FSceneViewport> Viewport = State->Driver->SceneViewport();
		const int32 MouseX = Viewport.IsValid() ? Viewport->GetMouseX() : -1;
		const int32 MouseY = Viewport.IsValid() ? Viewport->GetMouseY() : -1;
		HHitProxy* Hit = Viewport.IsValid() ? Viewport->GetHitProxy(MouseX, MouseY) : nullptr;
		const bool bAxisThere = Hit != nullptr && Hit->IsA(HWidgetAxis::StaticGetType());
		return FString::Printf(TEXT("The ring at (%d, %d), %s; the viewport's cursor at (%d, %d), with %s there; the cursor %s; tracking %d; the axis %d."),
			Grip->Pixel.X, Grip->Pixel.Y, *Grip->Searched, MouseX, MouseY,
			bAxisThere ? *FString::Printf(TEXT("axis %d"), static_cast<int32>(static_cast<HWidgetAxis*>(Hit)->Axis)) : (Hit != nullptr ? TEXT("another hit proxy") : TEXT("no hit proxy")),
			Viewport.IsValid() && Viewport->IsCursorVisible() ? TEXT("visible") : TEXT("hidden"),
			Client != nullptr && Client->IsTracking() ? 1 : 0, Client != nullptr ? static_cast<int32>(Client->GetCurrentWidgetAxis()) : -1);
	});
	// Grabbed and carried along the ring, a frame a move, then let go.
	EnqueueDesignerPointerDrag(State,
		[Grip]() { return Grip->Pixel; },
		[Grip]() { return Grip->Pixel + FIntPoint(FMath::RoundToInt32(Grip->Tangent.X * 150.0), FMath::RoundToInt32(Grip->Tangent.Y * 150.0)); },
		/*Steps*/ 6);
	EnqueueDesignerFrames(State, 2, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, RotationBefore]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		UDreamWidget* Template = State->Get(TEXT("Target"));
		const UDreamWidget* Preview = Driver.PreviewFor(Template);
		if (!TestTrue(TEXT("The widget and its preview are still there"), Template != nullptr && Preview != nullptr))
		{
			State->bAlive = false;
			return;
		}
		const FQuat Turn = RotationBefore->Inverse() * Template->GetRelativeRotation();
		double Degrees = 0.0;
		FVector Axis = FVector::ZeroVector;
		AngleAndAxis(Turn, Degrees, Axis);
		TestTrue(FString::Printf(TEXT("The drag along the ring turned the widget, in the asset: %.2f degrees"), Degrees), FMath::Abs(Degrees) > 1.0);
		TestTrue(FString::Printf(TEXT("...about the axis its face points down, its own X: the axis is %s"), *Axis.ToString()),
			FMath::Abs(Axis.X) > 0.99);
		TestTrue(TEXT("...and the preview shows the asset's turn"), Preview->GetRelativeRotation().Equals(Template->GetRelativeRotation(), 1e-3));
		TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
	});
	EnqueueDesignerFrames(State, 2, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, RotationBefore]()
	{
		const UDreamWidget* Template = State->Get(TEXT("Target"));
		TestTrue(TEXT("One undo turns the widget back"), Template != nullptr && Template->GetRelativeRotation().Equals(*RotationBefore, 1e-3));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

#endif
