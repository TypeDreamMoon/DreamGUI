// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/DreamWorldSpaceRaycasterBase.h"
#include "Core/DreamUISettings.h"


void UDreamWorldSpaceRaycasterSource::BeginPlay()
{
	Super::BeginPlay();
	DragThresholdSquare = DragThreshold * DragThreshold;
}

void UDreamWorldSpaceRaycasterSource::SetRayLength(float Value)
{
	RayLength = Value;
}
void UDreamWorldSpaceRaycasterSource::SetDragThreshold(float Value)
{
	DragThreshold = Value;
	DragThresholdSquare = DragThreshold * DragThreshold;
}
void UDreamWorldSpaceRaycasterSource::SetHoldToDrag(bool Value)
{
	bHoldToDrag = Value;
}
void UDreamWorldSpaceRaycasterSource::SetHoldToDragTime(float Value)
{
	HoldToDragTime = Value;
}

bool UDreamWorldSpaceRaycasterSource::GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveGenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection, OutRayEnd);
	}
	return false;
}
bool UDreamWorldSpaceRaycasterSource::ShouldStartDrag(UDreamPointerEventData* InPointerEventData)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveShouldStartDrag(InPointerEventData);
	}
	return false;
}

ADreamWorldSpaceRaycasterSourceActor::ADreamWorldSpaceRaycasterSourceActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

UDreamWorldSpaceRaycasterBase::UDreamWorldSpaceRaycasterBase()
{
	TraceChannel = TraceTypeQuery1;
}

void UDreamWorldSpaceRaycasterBase::BeginPlay()
{
	Super::BeginPlay();
}

void UDreamWorldSpaceRaycasterBase::OnRegister()
{
	Super::OnRegister();
}

bool UDreamWorldSpaceRaycasterBase::GetAffectByGamePause()const
{
	return GetDefault<UDreamUISettings>()->bWorldSpaceUIAffectByGamePause;
}

bool UDreamWorldSpaceRaycasterBase::GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, float& OutRayLength)
{
	auto RaycasterSrc = GetRaycasterSourceObject();
	if (!RaycasterSrc)
	{
		auto DebugMsg = FString::Printf(TEXT("[%s].%d There is no RayCasterSourceObject! WorldSpaceRaycaster '%s' will not work!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__
#if WITH_EDITOR
			, *(this->GetOwner()->GetActorLabel())
#else
			, *this->GetPathName()
#endif
			);
		GEngine->AddOnScreenDebugMessage(-1, 10, FColor::Red, DebugMsg);
		return false;
	}
	OutRayLength = RaycasterSrc->GetRayLength();
	return RaycasterSrc->GenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection, OutRayEnd);
}

bool UDreamWorldSpaceRaycasterBase::ShouldStartDrag(UDreamPointerEventData* InPointerEventData)
{
	auto RaycasterSrc = GetRaycasterSourceObject();
	// False, matching UDreamWorldSpaceRaycasterSource::ShouldStartDrag's own default for a source that
	// does not answer. The two used to disagree -- no source at all began a drag on the first frame of
	// every press, a native source that never overrode it began none ever -- and of the two, "a press
	// is not a drag" is the answer that cannot eat a click.
	if (!RaycasterSrc)return false;
	return RaycasterSrc->ShouldStartDrag(InPointerEventData);
}

UDreamWorldSpaceRaycasterSource* UDreamWorldSpaceRaycasterBase::GetRaycasterSourceObject() const
{
	if (!RaycasterSourceObject.IsValid())
	{
		if (RaycasterSourceActor.IsValid())
		{
			RaycasterSourceObject = RaycasterSourceActor->GetRaycasterSource();
		}
	}
	return RaycasterSourceObject.Get();
}

void UDreamWorldSpaceRaycasterBase::SetTraceChannel(TEnumAsByte<ETraceTypeQuery> Value)
{
	TraceChannel = Value;
}
void UDreamWorldSpaceRaycasterBase::SetRaycasterSourceObject(UDreamWorldSpaceRaycasterSource* Value)
{
	RaycasterSourceObject = Value;
}

void UDreamWorldSpaceRaycasterBase::SetRaycasterSourceActor(ADreamWorldSpaceRaycasterSourceActor* Value)
{
	RaycasterSourceActor = Value;
}
