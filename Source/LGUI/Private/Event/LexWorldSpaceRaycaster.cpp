// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/LexWorldSpaceRaycaster.h"
#include "Event/RaycasterSource/LexUIWorldSpaceRaycasterSource_Mouse.h"
#include "Core/Components/LexCanvas.h"
#include "Core/LexUISettings.h"


ULexBaseRaycaster* ULexUIWorldSpaceRaycasterSource::GetRaycasterObject()const
{
	return RaycasterObject.Get();
}
void ULexUIWorldSpaceRaycasterSource::Init(ULexBaseRaycaster* InRaycaster)
{
	RaycasterObject = InRaycaster;
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveInit(InRaycaster);
	}
}
bool ULexUIWorldSpaceRaycasterSource::GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveGenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection);
	}
	return false;
}
bool ULexUIWorldSpaceRaycasterSource::ShouldStartDrag(ULexPointerEventData* InPointerEventData)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveShouldStartDrag(InPointerEventData);
	}
	return false;
}



ULexWorldSpaceRaycaster::ULexWorldSpaceRaycaster()
{
	
}

void ULexWorldSpaceRaycaster::BeginPlay()
{
	Super::BeginPlay();
}

void ULexWorldSpaceRaycaster::OnRegister()
{
	Super::OnRegister();
	if (RaycasterSourceObject == nullptr)
	{
		RaycasterSourceObject = NewObject<ULexUIWorldSpaceRaycasterSource_Mouse>(this);
	}
	RaycasterSourceObject->Init(this);
	RenderModeArray = { ELexRenderMode::WorldSpace, ELexRenderMode::WorldSpace_LexUI };
}

bool ULexWorldSpaceRaycaster::ShouldSkipCanvas(class ULexCanvas* UICanvas)
{
	return false;
}
bool ULexWorldSpaceRaycaster::GetAffectByGamePause()const
{
	return GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
}
bool ULexWorldSpaceRaycaster::Raycast(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, FHitResult& OutHitResult, TArray<USceneComponent*>& OutHoverArray)
{
	switch (InteractionTarget)
	{
	case ELexUIInteractionTarget::UI:
		return Super::RaycastUI(InPointerEventData, RenderModeArray, OutRayOrigin, OutRayDirection, OutRayEnd, OutHitResult, OutHoverArray);
	case ELexUIInteractionTarget::World:
		return Super::RaycastWorld(bRequireFaceIndex, InPointerEventData, OutRayOrigin, OutRayDirection, OutRayEnd, OutHitResult, OutHoverArray);
	case ELexUIInteractionTarget::UIAndWorld:
	{
		FVector UIRayOrigin, UIRayDirection, UIRayEnd;
		FVector WorldRayOrigin, WorldRayDirection, WorldRayEnd;
		FHitResult UIHitResult, WorldHitResult;
		TArray<USceneComponent*> UIHoverArray, WorldHoverArray;
		auto HitUI = Super::RaycastUI(InPointerEventData, RenderModeArray, UIRayOrigin, UIRayDirection, UIRayEnd, UIHitResult, UIHoverArray);
		auto HitWorld = Super::RaycastWorld(bRequireFaceIndex, InPointerEventData, WorldRayOrigin, WorldRayDirection, WorldRayEnd, WorldHitResult, WorldHoverArray);
		if (HitUI && HitWorld)
		{
			if (UIHitResult.Distance <= WorldHitResult.Distance)
			{
				goto HIT_UI;
			}
			else
			{
				goto HIT_WORLD;
			}
		}
		else
		{
			if (HitUI)
			{
				goto HIT_UI;
			}
			if (HitWorld)
			{
				goto HIT_WORLD;
			}
			return false;
		}
		HIT_UI:
		{
			OutRayOrigin = UIRayOrigin;
			OutRayDirection = UIRayDirection;
			OutRayEnd = UIRayEnd;
			OutHitResult = UIHitResult;
			OutHoverArray = UIHoverArray;
			return true;
		}
		HIT_WORLD:
		{
			OutRayOrigin = WorldRayOrigin;
			OutRayDirection = WorldRayDirection;
			OutRayEnd = WorldRayEnd;
			OutHitResult = WorldHitResult;
			OutHoverArray = WorldHoverArray;
			return true;
		}
	}
	}
	return false;
}

bool ULexWorldSpaceRaycaster::GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection)
{
	if (!IsValid(RaycasterSourceObject))return false;
	return RaycasterSourceObject->GenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection);
}

bool ULexWorldSpaceRaycaster::ShouldStartDrag(ULexPointerEventData* InPointerEventData) 
{
	if (!IsValid(RaycasterSourceObject))return false;
	if (ShouldStartDrag_HoldToDrag(InPointerEventData))return true;
	return RaycasterSourceObject->ShouldStartDrag(InPointerEventData);
}

void ULexWorldSpaceRaycaster::SetRaycasterSourceObject(ULexUIWorldSpaceRaycasterSource* NewSource)
{
	RaycasterSourceObject = NewSource;
}
