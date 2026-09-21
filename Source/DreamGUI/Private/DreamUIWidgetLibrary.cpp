// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIWidgetLibrary.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/DreamDragDropOperation.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Event/DreamPointerEventData.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectIterator.h"

namespace DreamUIWidgetLibraryLocal
{
	UWorld* GetWorldFrom(const UObject* WorldContextObject)
	{
		return GEngine != nullptr
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr;
	}

	/**
	 * Every registered widget in the world.
	 *
	 * The manager's array is the one place this can come from: widgets are UObjects outered to a
	 * widget tree rather than actors, so an actor iterator cannot see them and a ForEachObjectOfClass
	 * would also sweep up every archetype, every designer preview and every widget belonging to
	 * another world.
	 */
	const TArray<TObjectPtr<UDreamWidget>>* GetAllWidgets(const UObject* WorldContextObject)
	{
		UWorld* World = GetWorldFrom(WorldContextObject);
		UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
		return Manager != nullptr ? &Manager->GetAllWidgetArray() : nullptr;
	}

	FDreamUIImageBrush MakeBrush(UObject* InResource, int32 InWidth, int32 InHeight)
	{
		FDreamUIImageBrush Brush;
		Brush.SetResourceObject(InResource);
		// Zero means "whatever the resource is", which is the brush's own default -- overwriting it
		// with (0,0) would make a texture draw into nothing.
		if (InWidth > 0 && InHeight > 0)
		{
			Brush.ImageSize = FVector2f(static_cast<float>(InWidth), static_cast<float>(InHeight));
		}
		return Brush;
	}
}

void UDreamUIWidgetLibrary::GetAllWidgetsOfClass(UObject* WorldContextObject, TSubclassOf<UDreamWidget> InWidgetClass,
	TArray<UDreamWidget*>& OutFoundWidgets, bool bTopLevelOnly)
{
	OutFoundWidgets.Reset();
	const TArray<TObjectPtr<UDreamWidget>>* AllWidgets = DreamUIWidgetLibraryLocal::GetAllWidgets(WorldContextObject);
	if (AllWidgets == nullptr || InWidgetClass == nullptr)
	{
		return;
	}
	for (const TObjectPtr<UDreamWidget>& Entry : *AllWidgets)
	{
		UDreamWidget* Widget = Entry.Get();
		if (!IsValid(Widget) || !Widget->IsA(InWidgetClass))
		{
			continue;
		}
		if (bTopLevelOnly && !Widget->IsRootWidgetInHierarchy())
		{
			continue;
		}
		OutFoundWidgets.Add(Widget);
	}
}

void UDreamUIWidgetLibrary::GetAllWidgetsWithInterface(UObject* WorldContextObject, TSubclassOf<UInterface> InInterface,
	TArray<UDreamWidget*>& OutFoundWidgets, bool bTopLevelOnly)
{
	OutFoundWidgets.Reset();
	const TArray<TObjectPtr<UDreamWidget>>* AllWidgets = DreamUIWidgetLibraryLocal::GetAllWidgets(WorldContextObject);
	if (AllWidgets == nullptr || InInterface == nullptr)
	{
		return;
	}
	for (const TObjectPtr<UDreamWidget>& Entry : *AllWidgets)
	{
		UDreamWidget* Widget = Entry.Get();
		if (!IsValid(Widget) || !Widget->GetClass()->ImplementsInterface(InInterface))
		{
			continue;
		}
		if (bTopLevelOnly && !Widget->IsRootWidgetInHierarchy())
		{
			continue;
		}
		OutFoundWidgets.Add(Widget);
	}
}

UDreamDragDropOperation* UDreamUIWidgetLibrary::CreateDragDropOperation(
	TSubclassOf<UDreamDragDropOperation> InOperationClass)
{
	// The transient package, as UMG does: an operation outlives the widget that made it -- a list row
	// recycled mid-drag is the ordinary case -- and outering it to that widget is exactly how a drag
	// ends up carrying a payload nobody can read any more.
	UClass* OperationClass = InOperationClass != nullptr
		? InOperationClass.Get()
		: UDreamDragDropOperation::StaticClass();
	return NewObject<UDreamDragDropOperation>(GetTransientPackage(), OperationClass);
}

bool UDreamUIWidgetLibrary::IsDragDropping(UObject* WorldContextObject)
{
	UDreamUIDragDropSubsystem* DragDrop = UDreamUIDragDropSubsystem::Get(WorldContextObject);
	return DragDrop != nullptr && DragDrop->IsDragInProgress();
}

UDreamDragDropOperation* UDreamUIWidgetLibrary::GetDragDroppingContent(UObject* WorldContextObject, int32 InPointerID)
{
	UDreamUIDragDropSubsystem* DragDrop = UDreamUIDragDropSubsystem::Get(WorldContextObject);
	return DragDrop != nullptr ? DragDrop->GetDragOperationForPointer(InPointerID) : nullptr;
}

bool UDreamUIWidgetLibrary::CancelDragDrop(UObject* WorldContextObject)
{
	UDreamUIDragDropSubsystem* DragDrop = UDreamUIDragDropSubsystem::Get(WorldContextObject);
	return DragDrop != nullptr && DragDrop->CancelActiveDrag();
}

bool UDreamUIWidgetLibrary::BeginDragWithOperation(UDreamPointerEventData* InPointerEvent,
	UDreamDragDropOperation* InOperation)
{
	if (!IsValid(InPointerEvent) || !IsValid(InOperation))
	{
		return false;
	}
	// Already carrying one: refusing beats replacing, because the operation in flight has drop
	// targets lit up against it and handlers waiting on its cancel.
	if (IsValid(InPointerEvent->DragOperation))
	{
		return false;
	}
	UDreamWidget* Source = InPointerEvent->PressWidget;
	if (!IsValid(Source))
	{
		return false;
	}
	InOperation->SourceWidget = Source;
	// The same field UDreamUIDragSource writes: the pipeline reads the operation off the pointer,
	// so this is the whole of "give this drag a meaning".
	InPointerEvent->DragOperation = InOperation;
	return true;
}

FDreamUIImageBrush UDreamUIWidgetLibrary::MakeBrushFromTexture(UTexture2D* InTexture, int32 InWidth, int32 InHeight)
{
	return DreamUIWidgetLibraryLocal::MakeBrush(InTexture, InWidth, InHeight);
}

FDreamUIImageBrush UDreamUIWidgetLibrary::MakeBrushFromMaterial(UMaterialInterface* InMaterial, int32 InWidth, int32 InHeight)
{
	return DreamUIWidgetLibraryLocal::MakeBrush(InMaterial, InWidth, InHeight);
}

FDreamUIImageBrush UDreamUIWidgetLibrary::MakeBrushFromAsset(UObject* InResource, int32 InWidth, int32 InHeight)
{
	return DreamUIWidgetLibraryLocal::MakeBrush(InResource, InWidth, InHeight);
}

FDreamUIImageBrush UDreamUIWidgetLibrary::NoResourceBrush()
{
	// Not the default-constructed brush: that one holds the white solid sprite, which is the opposite
	// of nothing -- it paints a white rectangle.
	FDreamUIImageBrush Brush;
	Brush.SetResourceObject(nullptr);
	return Brush;
}

bool UDreamUIWidgetLibrary::EqualEqual_DreamUIImageBrush(const FDreamUIImageBrush& A, const FDreamUIImageBrush& B)
{
	return A == B;
}

UObject* UDreamUIWidgetLibrary::GetBrushResource(const FDreamUIImageBrush& InBrush)
{
	return InBrush.GetResourceObject();
}

UTexture2D* UDreamUIWidgetLibrary::GetBrushResourceAsTexture2D(const FDreamUIImageBrush& InBrush)
{
	return Cast<UTexture2D>(InBrush.GetResourceObject());
}

UMaterialInterface* UDreamUIWidgetLibrary::GetBrushResourceAsMaterial(const FDreamUIImageBrush& InBrush)
{
	return Cast<UMaterialInterface>(InBrush.GetResourceObject());
}

void UDreamUIWidgetLibrary::SetBrushResourceToTexture(FDreamUIImageBrush& InBrush, UTexture2D* InTexture)
{
	InBrush.SetResourceObject(InTexture);
}

void UDreamUIWidgetLibrary::SetBrushResourceToMaterial(FDreamUIImageBrush& InBrush, UMaterialInterface* InMaterial)
{
	InBrush.SetResourceObject(InMaterial);
}

UMaterialInstanceDynamic* UDreamUIWidgetLibrary::GetDynamicMaterial(FDreamUIImageBrush& InBrush)
{
	UObject* Resource = InBrush.GetResourceObject();
	if (UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Resource))
	{
		return Existing;
	}
	if (UMaterialInterface* Material = Cast<UMaterialInterface>(Resource))
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, nullptr);
		InBrush.SetResourceObject(Instance);
		return Instance;
	}
	return nullptr;
}

void UDreamUIWidgetLibrary::SetFocusToGameViewport()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

bool UDreamUIWidgetLibrary::SetMousePosition(APlayerController* InPlayer, FVector2D InPosition)
{
	if (!IsValid(InPlayer))
	{
		return false;
	}
	InPlayer->SetMouseLocation(FMath::TruncToInt(InPosition.X), FMath::TruncToInt(InPosition.Y));
	return true;
}

int32 UDreamUIWidgetLibrary::DismissAllMenus(UObject* WorldContextObject)
{
	const UWorld* World = DreamUIWidgetLibraryLocal::GetWorldFrom(WorldContextObject);
	if (World == nullptr)
	{
		return 0;
	}
	int32 ClosedCount = 0;
	for (TObjectIterator<UDreamLayoutContainerMenuAnchor> It; It; ++It)
	{
		UDreamLayoutContainerMenuAnchor* Anchor = *It;
		// Class defaults and the archetypes a Blueprint keeps are real objects of this class and they
		// are never open; iterating them would only ever cost time, and closing one would write to a
		// template.
		if (!IsValid(Anchor) || Anchor->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			continue;
		}
		// Per world, because PIE runs several at once and a menu in one of them is not a menu the
		// caller can see.
		if (Anchor->GetWorld() != World || !Anchor->IsOpen())
		{
			continue;
		}
		Anchor->SetIsOpen(false);
		++ClosedCount;
	}
	return ClosedCount;
}

bool UDreamUIWidgetLibrary::GetSafeZonePadding(UObject* WorldContextObject, FVector4& OutSafePadding,
	FVector2D& OutSafePaddingScale)
{
	OutSafePadding = FVector4(0.0, 0.0, 0.0, 0.0);
	OutSafePaddingScale = FVector2D::ZeroVector;
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}
	FDisplayMetrics Metrics;
	FSlateApplication::Get().GetCachedDisplayMetrics(Metrics);
	OutSafePadding = Metrics.TitleSafePaddingSize;

	UWorld* World = DreamUIWidgetLibraryLocal::GetWorldFrom(WorldContextObject);
	if (World == nullptr || World->GetGameViewport() == nullptr)
	{
		// The inset is real; only its expression as a fraction needs a viewport to divide by.
		return true;
	}
	FVector2D ViewportSize = FVector2D::ZeroVector;
	World->GetGameViewport()->GetViewportSize(ViewportSize);
	if (ViewportSize.X > UE_KINDA_SMALL_NUMBER && ViewportSize.Y > UE_KINDA_SMALL_NUMBER)
	{
		OutSafePaddingScale = FVector2D(OutSafePadding.X / ViewportSize.X, OutSafePadding.Y / ViewportSize.Y);
	}
	return true;
}
