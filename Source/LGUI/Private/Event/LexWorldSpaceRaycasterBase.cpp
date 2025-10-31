// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/LexWorldSpaceRaycasterBase.h"
#include "Core/LexUISettings.h"


void ULexWorldSpaceRaycasterSource::BeginPlay()
{
	Super::BeginPlay();
	DragThresholdSquare = DragThreshold * DragThreshold;
}

void ULexWorldSpaceRaycasterSource::SetRayLength(float Value)
{
	RayLength = Value;
}
void ULexWorldSpaceRaycasterSource::SetDragThreshold(float Value)
{
	DragThreshold = Value;
	DragThresholdSquare = DragThreshold * DragThreshold;
}
void ULexWorldSpaceRaycasterSource::SetHoldToDrag(bool Value)
{
	bHoldToDrag = Value;
}
void ULexWorldSpaceRaycasterSource::SetHoldToDragTime(float Value)
{
	HoldToDragTime = Value;
}

bool ULexWorldSpaceRaycasterSource::GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveGenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection, OutRayEnd);
	}
	return false;
}
bool ULexWorldSpaceRaycasterSource::ShouldStartDrag(ULexPointerEventData* InPointerEventData)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveShouldStartDrag(InPointerEventData);
	}
	return false;
}



ULexWorldSpaceRaycasterBase::ULexWorldSpaceRaycasterBase()
{
	RaycasterSource = FLexUIComponentReference(ULexWorldSpaceRaycasterSource::StaticClass());
	TraceChannel = TraceTypeQuery1;
}

void ULexWorldSpaceRaycasterBase::BeginPlay()
{
	Super::BeginPlay();
}

void ULexWorldSpaceRaycasterBase::OnRegister()
{
	Super::OnRegister();
}

bool ULexWorldSpaceRaycasterBase::GetAffectByGamePause()const
{
	return GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
}

bool ULexWorldSpaceRaycasterBase::GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd)
{
	auto RaycasterSrc = GetRaycasterSourceObject();
	if (!RaycasterSrc)return false;
	return RaycasterSrc->GenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection, OutRayEnd);
}

bool ULexWorldSpaceRaycasterBase::ShouldStartDrag(ULexPointerEventData* InPointerEventData) 
{
	auto RaycasterSrc = GetRaycasterSourceObject();
	if (!RaycasterSrc)return true;
	return RaycasterSrc->ShouldStartDrag(InPointerEventData);
}

ULexWorldSpaceRaycasterSource* ULexWorldSpaceRaycasterBase::GetRaycasterSourceObject() const
{
	if (!RaycasterSourceObject.IsValid())
	{
		auto Object = RaycasterSource.GetComponent<ULexWorldSpaceRaycasterSource>();
		RaycasterSourceObject = Object;
	}
	return RaycasterSourceObject.Get();
}

void ULexWorldSpaceRaycasterBase::SetTraceChannel(TEnumAsByte<ETraceTypeQuery> Value)
{
	TraceChannel = Value;
}
void ULexWorldSpaceRaycasterBase::SetRaycasterSourceObject(ULexWorldSpaceRaycasterSource* NewSource)
{
	RaycasterSource = NewSource;
}
