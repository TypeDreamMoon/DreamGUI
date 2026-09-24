// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverWorldSpace.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "DreamUIBPLibrary.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Extensions/DreamUIRenderTargetGeometrySource.h"
#include "Extensions/DreamUIRenderTargetInteraction.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"

DEFINE_LOG_CATEGORY_STATIC(LogDreamDriverWorld, Log, All);

namespace DreamDriverWorldLocal
{
	/** Say why the world could not be built, where a report will show it: the context's test, else the running one, else the log. */
	void ReportFailure(const FDreamDriverContext* InContext, const FString& InMessage)
	{
		FAutomationTestBase* ReportingTest = InContext != nullptr && InContext->CurrentTest != nullptr
			? InContext->CurrentTest
			: FAutomationTestFramework::Get().GetCurrentTest();
		if (ReportingTest != nullptr)
		{
			ReportingTest->AddError(InMessage);
		}
		else
		{
			UE_LOG(LogDreamDriverWorld, Error, TEXT("%s"), *InMessage);
		}
	}

	/** An actor whose root is a plain scene component, placed at InTransform: something for a tree to follow. */
	USceneComponent* SpawnHost(UWorld& InWorld, const FName InComponentName, const FTransform& InTransform)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		AActor* HostActor = InWorld.SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
		if (HostActor == nullptr)
		{
			return nullptr;
		}
		USceneComponent* Host = NewObject<USceneComponent>(HostActor, InComponentName);
		HostActor->SetRootComponent(Host);
		Host->RegisterComponent();
		// Placed once registered, the order the world-space fixtures use; whatever attaches to it next
		// reads this transform, and follows it from then on.
		Host->SetWorldTransform(InTransform);
		return Host;
	}
}

bool UDreamDriverWorldSpaceRaycaster::GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin,
	FVector& OutRayDirection, FVector& OutRayEnd, float& OutRayLength)
{
	// The length before anything can refuse, as the production override writes it: a caller that reads
	// the length after a refusal reads the configured one rather than whatever it passed in.
	OutRayLength = GetRayLength();
	if (!VirtualCamera.IsValid())
	{
		return false;
	}

	FVector2D ScreenPosition = FVector2D::ZeroVector;
	switch (GetPointerSource())
	{
	case EDreamWorldPointerSource::ScreenCenter:
		// The middle of the WHOLE viewport, as the production branch takes it from the viewport client's
		// size -- not the middle of the constrained rect.
		ScreenPosition = FVector2D(VirtualCamera->ViewportSize) * 0.5;
		break;
	case EDreamWorldPointerSource::Mouse:
	default:
		// A mouse ray is a position, and without a pointer there is none -- the production answer.
		if (InPointerEventData == nullptr)
		{
			return false;
		}
		ScreenPosition = FVector2D(InPointerEventData->PointerPosition);
		break;
	}

	if (!VirtualCamera->Deproject(ScreenPosition, OutRayOrigin, OutRayDirection))
	{
		return false;
	}
	OutRayEnd = OutRayOrigin + OutRayDirection * GetRayLength();
	return true;
}

FMinimalViewInfo DreamDriverWorld::MakeView(const FVector& InLocation, const FRotator& InRotation, float InHorizontalFieldOfView,
	const FIntPoint& InViewportSize)
{
	FMinimalViewInfo View;
	View.Location = InLocation;
	View.Rotation = InRotation;
	View.ProjectionMode = ECameraProjectionMode::Perspective;
	View.FOV = InHorizontalFieldOfView;
	View.DesiredFOV = InHorizontalFieldOfView;
	if (InViewportSize.X > 0 && InViewportSize.Y > 0)
	{
		View.AspectRatio = static_cast<float>(InViewportSize.X) / static_cast<float>(InViewportSize.Y);
	}
	return View;
}

FMinimalViewInfo DreamDriverWorld::MakeOrthographicView(const FVector& InLocation, const FRotator& InRotation, float InOrthoWidth,
	const FIntPoint& InViewportSize)
{
	FMinimalViewInfo View;
	View.Location = InLocation;
	View.Rotation = InRotation;
	View.ProjectionMode = ECameraProjectionMode::Orthographic;
	View.OrthoWidth = InOrthoWidth;
	// Fixed planes: with the automatic ones the near plane follows the view's angle and width, and a ray
	// could start behind the camera by an amount only the engine's heuristics know.
	View.bAutoCalculateOrthoPlanes = false;
	View.OrthoNearClipPlane = 0.0f;
	if (InViewportSize.X > 0 && InViewportSize.Y > 0)
	{
		View.AspectRatio = static_cast<float>(InViewportSize.X) / static_cast<float>(InViewportSize.Y);
	}
	return View;
}

bool DreamDriverWorld::MirrorPlayerView(const APlayerController* InController, FDreamDriverVirtualCamera& OutCamera, FString& OutWhyNot)
{
	if (!IsValid(InController))
	{
		OutWhyNot = TEXT("there is no player controller to mirror");
		return false;
	}
	const ULocalPlayer* LocalPlayer = InController->GetLocalPlayer();
	if (LocalPlayer == nullptr)
	{
		OutWhyNot = TEXT("the player controller has no local player, so nothing is seen through it");
		return false;
	}
	UGameViewportClient* ViewportClient = LocalPlayer->ViewportClient;
	if (ViewportClient == nullptr)
	{
		OutWhyNot = TEXT("the local player has no viewport client, so there is no viewport to project onto");
		return false;
	}
	// A split-screen player sees a sub-rect of the viewport, which the camera does not model.
	if (!LocalPlayer->Origin.IsNearlyZero() || !LocalPlayer->Size.Equals(FVector2D(1.0, 1.0)))
	{
		OutWhyNot = FString::Printf(TEXT("the local player owns the part of the viewport at origin %s size %s, and only a whole-viewport player can be mirrored"),
			*LocalPlayer->Origin.ToString(), *LocalPlayer->Size.ToString());
		return false;
	}
	FVector2D ViewportSize = FVector2D::ZeroVector;
	ViewportClient->GetViewportSize(ViewportSize);
	if (ViewportSize.X < 1.0 || ViewportSize.Y < 1.0)
	{
		OutWhyNot = TEXT("the viewport has no area yet");
		return false;
	}

	// ULocalPlayer::GetViewPoint, which is protected: the camera manager's cached view, its FOV angle,
	// and the controller's view point over both.
	FMinimalViewInfo View;
	if (const APlayerCameraManager* CameraManager = InController->PlayerCameraManager.Get())
	{
		View = CameraManager->GetCameraCacheView();
		View.FOV = CameraManager->GetFOVAngle();
	}
	InController->GetPlayerViewPoint(View.Location, View.Rotation);
	View.DesiredFOV = View.FOV;
	// The player's own constraint where the view does not state one -- which is the fallback
	// GetProjectionData hands the engine, where the camera would otherwise use the class default's.
	if (!View.AspectRatioAxisConstraint.IsSet())
	{
		View.AspectRatioAxisConstraint = LocalPlayer->AspectRatioAxisConstraint.GetValue();
	}

	OutCamera.View = View;
	OutCamera.ViewportSize = FIntPoint(FMath::TruncToInt(ViewportSize.X), FMath::TruncToInt(ViewportSize.Y));
	return true;
}

UDreamDriverWorldSpaceRaycaster* DreamDriverWorld::AttachWorldPointer(FDreamDriverRig& InRig, const FMinimalViewInfo& InView,
	EDreamWorldPointerSource InSource)
{
	// The viewport the rig's pointer pixels are measured on is its root canvas's substituted one; the
	// overlay and the world are seen on one screen.
	const UDreamCanvas* ScreenCanvas = InRig.RootCanvas();
	const FIntPoint ViewportSize = IsValid(ScreenCanvas) ? ScreenCanvas->GetViewportSize() : InRig.GetOptions().ViewportSize;
	return AttachWorldPointer(InRig.Context(), InRig.GetHostActor(), InView, ViewportSize, InSource);
}

UDreamDriverWorldSpaceRaycaster* DreamDriverWorld::AttachWorldPointer(FDreamDriverContext& InContext, AActor* InHost,
	const FMinimalViewInfo& InView, const FIntPoint& InViewportSize, EDreamWorldPointerSource InSource)
{
	using namespace DreamDriverWorldLocal;
	if (!IsValid(InHost) || InContext.World == nullptr)
	{
		ReportFailure(&InContext, TEXT("A world pointer needs a world and an actor to ride on, and the context has not got them."));
		return nullptr;
	}
	const int32 UserIndex = IsValid(InContext.EventSystem) ? InContext.EventSystem->GetUserIndex() : 0;

	// One world pointer per player. A second one -- the manager's own, made by EnsureInteractionForPlayer
	// for a world widget that began play, or an earlier call to this -- would trace the same panels from
	// a different eye, and which of the two answered a click would be decided by list order.
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(InContext.World))
	{
		for (const TWeakObjectPtr<UDreamBaseRaycaster>& Enrolled : Manager->GetAllRaycasterArray())
		{
			const UDreamBaseRaycaster* Existing = Enrolled.Get();
			if (IsValid(Existing) && Existing->GetUserIndex() == UserIndex && Existing->IsA<UDreamWorldSpaceRaycaster>())
			{
				ReportFailure(&InContext, FString::Printf(
					TEXT("Player %d already has a world-space raycaster (%s); attach the driver's before anything asks the manager to ensure interaction, and only once."),
					UserIndex, *Existing->GetPathName()));
				return nullptr;
			}
		}
	}

	const TSharedRef<FDreamDriverVirtualCamera> Camera = MakeShared<FDreamDriverVirtualCamera>();
	Camera->View = InView;
	Camera->ViewportSize = InViewportSize;

	// EnsureInteractionForPlayer's registration, step for step.
	UDreamDriverWorldSpaceRaycaster* Raycaster = NewObject<UDreamDriverWorldSpaceRaycaster>(InHost, NAME_None, RF_Transient);
	Raycaster->SetUserIndex(UserIndex);
	Raycaster->SetPointerSource(InSource);
	Raycaster->VirtualCamera = Camera;
	InHost->AddInstanceComponent(Raycaster);
	Raycaster->RegisterComponent();
	// And the step a level gives it that a headless world does not: an actor nobody initialized for play
	// never auto-activates its components, so the raycaster would never reach AllRaycasterArray -- the
	// list both UDreamPointerInputModule::LineTrace and EnsureInteractionForPlayer read. AddRaycaster
	// refuses duplicates, so this is harmless where activation did happen.
	Raycaster->ActivateRaycaster();

	InContext.Camera = Camera;
	return Raycaster;
}

UDreamWidget* DreamDriverWorld::MakeWorldPanel(FDreamDriverRig& InRig, const FString& InName, const FTransform& InTransform,
	const FVector2D& InSize, bool bInHitTestableBackground, EDreamRenderMode InRenderMode)
{
	using namespace DreamDriverWorldLocal;
	UWorld* World = InRig.GetWorld();
	if (World == nullptr)
	{
		ReportFailure(&InRig.Context(), TEXT("A world panel needs the rig's world, and the rig has none."));
		return nullptr;
	}
	USceneComponent* Host = SpawnHost(*World, TEXT("PanelHost"), InTransform);
	if (Host == nullptr)
	{
		ReportFailure(&InRig.Context(), FString::Printf(TEXT("Could not spawn a host actor for the world panel '%s'."), *InName));
		return nullptr;
	}

	// ConstructWidget, the runtime's verb for a root made in code: it registers the widget, begins play
	// on it once the manager has (the rig opened that gate), and parks it until it is attached.
	UDreamWidget* Root = UDreamUIBPLibrary::ConstructWidget(World, InName, nullptr);
	if (Root == nullptr)
	{
		ReportFailure(&InRig.Context(), FString::Printf(TEXT("ConstructWidget made no root for the world panel '%s'."), *InName));
		return nullptr;
	}
	// A root with no parent is as wide as its size delta, which is what these set; a world-space root has
	// no viewport to stretch to.
	Root->SetWidth(InSize.X);
	Root->SetHeight(InSize.Y);
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (Canvas == nullptr)
	{
		ReportFailure(&InRig.Context(), FString::Printf(TEXT("The world panel '%s' would not take a canvas."), *InName));
		Root->DestroyWidget();
		return nullptr;
	}
	Canvas->SetRenderMode(InRenderMode);
	// The creation verb for a world-space root: the canvas places the tree at the host and follows it
	// from then on, and leaving the parked state is what lets it be drawn and hit.
	if (!UDreamUIBPLibrary::AttachWidgetToSceneComponent(Root, Host))
	{
		ReportFailure(&InRig.Context(), FString::Printf(TEXT("The world panel '%s' could not be attached to its host."), *InName));
		Root->DestroyWidget();
		return nullptr;
	}
	if (bInHitTestableBackground)
	{
		// After the canvas: a visual is enrolled with its widget's render canvas only if one exists by
		// the time it is created, and one that was never enrolled is never traced.
		Root->CreateNewVisual<UDreamVisualEmpty>();
	}
	return Root;
}

USceneComponent* DreamDriverWorld::GetPanelHost(const UDreamWidget* InPanelRoot)
{
	if (!IsValid(InPanelRoot))
	{
		return nullptr;
	}
	const UDreamCanvas* Canvas = InPanelRoot->GetComponent<UDreamCanvas>();
	return IsValid(Canvas) ? Canvas->GetAttachedRootSceneComponent() : nullptr;
}

bool DreamDriverWorld::FDreamRenderTargetMesh::IsComplete() const
{
	return IsValid(CanvasRoot) && IsValid(Canvas) && IsValid(RenderTarget) && IsValid(Surface) && IsValid(Interaction) && IsValid(Actor);
}

DreamDriverWorld::FDreamRenderTargetMesh DreamDriverWorld::MakeRenderTargetMesh(FDreamDriverRig& InRig, const FString& InName,
	const FTransform& InTransform, const FIntPoint& InRenderTargetSize)
{
	using namespace DreamDriverWorldLocal;
	FDreamRenderTargetMesh Made;
	UWorld* World = InRig.GetWorld();
	if (World == nullptr || InRenderTargetSize.X <= 0 || InRenderTargetSize.Y <= 0)
	{
		ReportFailure(&InRig.Context(), FString::Printf(TEXT("A render-target surface needs the rig's world and a target with an area (asked for %s)."),
			*InRenderTargetSize.ToString()));
		return Made;
	}

	// The texture first: a canvas that fits itself to its target takes its size, its rect and its
	// projection from the texture, so the texture has to exist before the canvas is asked what size it
	// is. Headless, InitCustomFormat only records the size (no resource is made without a renderer),
	// which is all the canvas and the surface read.
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Target->AddressX = TextureAddress::TA_Clamp;
	Target->AddressY = TextureAddress::TA_Clamp;
	Target->ClearColor = FLinearColor::Transparent;
	// The format the canvas would have picked had it made the target itself, so nothing provokes its
	// re-initialisation path.
	Target->InitCustomFormat(static_cast<uint32>(InRenderTargetSize.X), static_cast<uint32>(InRenderTargetSize.Y), EPixelFormat::PF_B8G8R8A8, false);
	Made.RenderTarget = Target;

	UDreamWidget* Root = UDreamUIBPLibrary::ConstructWidget(World, InName, nullptr);
	UDreamCanvas* Canvas = Root != nullptr ? Root->AddComponent<UDreamCanvas>() : nullptr;
	if (Canvas == nullptr)
	{
		ReportFailure(&InRig.Context(), FString::Printf(TEXT("The render-target canvas '%s' could not be built."), *InName));
		if (Root != nullptr)
		{
			Root->DestroyWidget();
		}
		return Made;
	}
	Canvas->SetRenderMode(EDreamRenderMode::RenderTarget);
	// The texture is the fixed thing and the canvas follows it -- the other way round, a widget moving
	// could change the size of the surface a test is aiming at.
	Canvas->SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget);
	Canvas->SetRenderTarget(Target);
	Made.CanvasRoot = Root;
	Made.Canvas = Canvas;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	AActor* SurfaceActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (SurfaceActor == nullptr)
	{
		ReportFailure(&InRig.Context(), FString::Printf(TEXT("Could not spawn an actor for the render-target surface '%s'."), *InName));
		return Made;
	}
	Made.Actor = SurfaceActor;

	UDreamUIRenderTargetGeometrySource* Surface = NewObject<UDreamUIRenderTargetGeometrySource>(SurfaceActor, TEXT("Surface"));
	// The canvas BEFORE registering: registration builds the scene proxy and the material, both of which
	// ask the source for its canvas, and a source without one goes looking for a presenter reference and
	// warns that it has none. Given the canvas first, it also builds its mesh and body setup here, and
	// its physics state from them when it registers.
	Surface->SetCanvas(Canvas);
	// Solid to every channel a pointer might trace on, and to nothing physical.
	Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Surface->SetCollisionResponseToAllChannels(ECR_Block);
	SurfaceActor->SetRootComponent(Surface);
	SurfaceActor->AddInstanceComponent(Surface);
	Surface->RegisterComponent();
	Surface->SetWorldTransform(InTransform);
	Made.Surface = Surface;

	// The tree follows the surface, as a presenter's tree follows the presenter; attaching is also what
	// takes the root out of the parked state ConstructWidget left it in. The canvas's own virtual camera
	// moves with the root, so the picture on the texture does not change with where the surface stands.
	UDreamUIBPLibrary::AttachWidgetToSceneComponent(Root, Surface);

	UDreamUIRenderTargetInteraction* Interaction = NewObject<UDreamUIRenderTargetInteraction>(SurfaceActor, TEXT("Interaction"));
	SurfaceActor->AddInstanceComponent(Interaction);
	Interaction->RegisterComponent();
	Made.Interaction = Interaction;
	return Made;
}

void DreamDriverWorld::TickLikeAnEngineFrame(UActorComponent* InComponent, float InDeltaSeconds)
{
	if (!IsValid(InComponent) || !InComponent->IsRegistered())
	{
		return;
	}
	// Through the base class, where TickComponent is public and where the engine's tick function calls
	// it; dispatch is still virtual.
	InComponent->TickComponent(InDeltaSeconds, LEVELTICK_All, nullptr);
}
