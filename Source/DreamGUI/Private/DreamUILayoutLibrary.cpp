// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUILayoutLibrary.h"

#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace DreamUILayoutLibraryLocal
{
	UWorld* GetWorldFrom(const UObject* WorldContextObject)
	{
		return GEngine != nullptr
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr;
	}
}

FVector2D UDreamUILayoutLibrary::GetViewportSize(UObject* WorldContextObject)
{
	const UWorld* World = DreamUILayoutLibraryLocal::GetWorldFrom(WorldContextObject);
	if (World == nullptr || World->GetGameViewport() == nullptr)
	{
		return FVector2D::ZeroVector;
	}
	FVector2D ViewportSize = FVector2D::ZeroVector;
	World->GetGameViewport()->GetViewportSize(ViewportSize);
	return ViewportSize;
}

float UDreamUILayoutLibrary::GetViewportScale(UObject* WorldContextObject)
{
	const FVector2D ViewportSize = GetViewportSize(WorldContextObject);
	if (ViewportSize.IsNearlyZero())
	{
		// One, not zero: a scale of zero would make every DPI division below produce infinities, and
		// "no viewport" is not "everything is infinitely small".
		return 1.0f;
	}
	return GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(
		FIntPoint(FMath::TruncToInt(ViewportSize.X), FMath::TruncToInt(ViewportSize.Y)));
}

bool UDreamUILayoutLibrary::GetMousePositionOnViewport(UObject* WorldContextObject, FVector2D& OutMousePosition)
{
	OutMousePosition = FVector2D::ZeroVector;
	const UWorld* World = DreamUILayoutLibraryLocal::GetWorldFrom(WorldContextObject);
	if (World == nullptr)
	{
		return false;
	}
	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!IsValid(PlayerController))
	{
		return false;
	}
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!PlayerController->GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	OutMousePosition = FVector2D(MouseX, MouseY);
	return true;
}

bool UDreamUILayoutLibrary::GetMousePositionOnPlatform(FVector2D& OutMousePosition)
{
	OutMousePosition = FVector2D::ZeroVector;
	// The desktop cursor only exists while Slate does. A cooked server, a commandlet and a test run
	// under -nullrhi all have no Slate application, and answering (0,0) there would read as "the
	// cursor is in the top-left corner".
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}
	OutMousePosition = FSlateApplication::Get().GetCursorPos();
	return true;
}

bool UDreamUILayoutLibrary::GetMousePositionScaledByDPI(APlayerController* InPlayer, FVector2D& OutMousePosition)
{
	OutMousePosition = FVector2D::ZeroVector;
	if (!IsValid(InPlayer))
	{
		return false;
	}
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!InPlayer->GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	const float Scale = GetViewportScale(InPlayer);
	OutMousePosition = Scale > UE_KINDA_SMALL_NUMBER
		? FVector2D(MouseX / Scale, MouseY / Scale)
		: FVector2D(MouseX, MouseY);
	return true;
}

bool UDreamUILayoutLibrary::ProjectWorldLocationToWidgetPosition(APlayerController* InPlayer,
	FVector InWorldLocation, FVector2D& OutScreenPosition, bool bPlayerViewportRelative)
{
	float UnusedDistance = 0.0f;
	return ProjectWorldLocationToWidgetPositionWithDistance(
		InPlayer, InWorldLocation, OutScreenPosition, UnusedDistance, bPlayerViewportRelative);
}

bool UDreamUILayoutLibrary::ProjectWorldLocationToWidgetPositionWithDistance(APlayerController* InPlayer,
	FVector InWorldLocation, FVector2D& OutScreenPosition, float& OutDistance, bool bPlayerViewportRelative)
{
	OutScreenPosition = FVector2D::ZeroVector;
	OutDistance = 0.0f;
	if (!IsValid(InPlayer))
	{
		return false;
	}
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	InPlayer->GetPlayerViewPoint(ViewLocation, ViewRotation);
	// Signed along the view direction, so a location BEHIND the camera reports a negative distance
	// instead of the same positive number as its mirror image in front. The projection below refuses
	// that case anyway; the sign is what lets a caller sort and fade by depth without re-deriving it.
	OutDistance = static_cast<float>(FVector::DotProduct(InWorldLocation - ViewLocation, ViewRotation.Vector()));
	return UGameplayStatics::ProjectWorldToScreen(InPlayer, InWorldLocation, OutScreenPosition, bPlayerViewportRelative);
}

FDreamUIWidgetGeometry UDreamUILayoutLibrary::GetPlayerScreenWidgetGeometry(UObject* WorldContextObject,
	APlayerController* InPlayer)
{
	UWorld* World = DreamUILayoutLibraryLocal::GetWorldFrom(WorldContextObject);
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(World);
	if (ScreenUI == nullptr)
	{
		return FDreamUIWidgetGeometry();
	}
	// GetScreenRoot rather than GetOrCreate: reading a geometry must not bring a screen into
	// existence for a player who has none.
	return UDreamUIWidgetGeometryLibrary::GetWidgetGeometry(ScreenUI->GetScreenRoot(InPlayer));
}

UDreamPanelSlot* UDreamUILayoutLibrary::SlotAsPanelSlot(UDreamWidget* InWidget)
{
	return IsValid(InWidget) ? InWidget->GetPanelSlot() : nullptr;
}

void UDreamUILayoutLibrary::RemoveAllWidgets(UObject* WorldContextObject)
{
	UWorld* World = DreamUILayoutLibraryLocal::GetWorldFrom(WorldContextObject);
	if (UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(World))
	{
		ScreenUI->RemoveAllUI();
	}
}
